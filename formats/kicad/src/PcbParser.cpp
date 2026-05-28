// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/formats/kicad/PcbParser.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "circuitcore/sexpr/SExpr.h"

namespace circuitcore::formats::kicad {

using circuitcore::sexpr::Node;
using circuitcore::sexpr::parse;

namespace {

// Convert KiCad mm → SI meters.
constexpr double kMmToM = 1.0e-3;

// Convert degrees → radians.
constexpr double kDegToRad = 0.017453292519943295;  // pi / 180

// Internal control-flow primitive used by the Walker helpers below.
// PcbParser declares a std::expected<Board, ParseError> public API; we
// bridge to it by throwing ParseError out of any deeply-nested helper
// and catching it at parse_string / parse_file. This keeps the helper
// bodies free of expected<> plumbing without affecting the public
// contract -- callers never see an exception.
[[noreturn]] void throw_parse_error(const Node& n, const std::string& msg) {
    throw ParseError{msg, n.line, n.col};
}

// Find first child of `n` whose head symbol matches `tag`. Returns nullptr if none.
const Node* find_child(const Node& n, std::string_view tag) {
    if (!n.is_list()) return nullptr;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

// Collect every child of `n` whose head symbol matches `tag`.
std::vector<const Node*> find_children(const Node& n, std::string_view tag) {
    std::vector<const Node*> out;
    if (!n.is_list()) return out;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) out.push_back(&c);
    }
    return out;
}

double expect_number(const Node& n) {
    if (!n.is_number()) throw_parse_error(n, "expected number");
    return n.number;
}

std::string_view expect_string_or_symbol(const Node& n) {
    if (n.is_string() || n.is_symbol()) return n.text;
    throw_parse_error(n, "expected string or symbol");
}

// Read a 2-element (x y) tail starting at child index `start` in `node`.
// Used for both (at X Y) and (xy X Y) — and (start X Y) / (end X Y).
circuitcore::board::Point2 read_xy_tail(const Node& node, std::size_t start = 1) {
    if (node.children.size() < start + 2) throw_parse_error(node, "expected at least two numeric coordinates");
    return {
        expect_number(node.children[start]) * kMmToM,
        expect_number(node.children[start + 1]) * kMmToM,
    };
}

// Helper: walks (layers ...) inside a section and returns layer name tokens.
// Both segment-style ('(layer "F.Cu")') and via-style ('(layers "F.Cu" "B.Cu")')
// store names as strings; quoted in modern KiCad, sometimes bare symbols in old files.
std::vector<std::string> read_layer_names(const Node& list_form) {
    std::vector<std::string> names;
    for (std::size_t i = 1; i < list_form.children.size(); ++i) {
        names.emplace_back(expect_string_or_symbol(list_form.children[i]));
    }
    return names;
}

class Walker {
public:
    explicit Walker(const Node& root) : root_(root) {}

    circuitcore::board::Board build() {
        if (!root_.is_list() || root_.tag() != "kicad_pcb") {
            throw_parse_error(root_, "top-level form must be (kicad_pcb ...)");
        }
        parse_general();
        parse_layers();
        parse_stackup();
        parse_nets();
        parse_segments();
        parse_vias();
        parse_zones();
        parse_outline();
        parse_footprints();
        parse_graphics();
        return std::move(board_);
    }

private:
    int layer_id_(std::string_view name, const Node& ctx) {
        auto it = layer_name_to_id_.find(std::string(name));
        if (it == layer_name_to_id_.end()) {
            throw_parse_error(ctx, std::format("unknown layer name '{}'", name));
        }
        return it->second;
    }

    // Read a (net X) form's payload. Accepts integer ID (older KiCad) or
    // string net name (KiCad master ~v9-pre). Returns 0 for an unknown net
    // rather than failing -- segments referencing a stripped net should not
    // crash the load.
    int net_id_(const Node& netr) {
        if (netr.children.size() < 2) return 0;
        const Node& v = netr.children[1];
        if (v.is_number()) return static_cast<int>(v.number);
        if (v.is_string() || v.is_symbol()) {
            if (const auto* n = board_.find_net_by_name(v.text)) return n->id;
            // Auto-create -- modern KiCad files may have no explicit net table.
            circuitcore::board::Net created;
            created.id = static_cast<int>(board_.nets.size());
            created.name = v.text;
            board_.nets.push_back(created);
            return created.id;
        }
        return 0;
    }

    void parse_general() {
        if (const Node* g = find_child(root_, "general")) {
            if (const Node* t = find_child(*g, "thickness")) {
                if (t->children.size() >= 2) {
                    board_.stackup.total_thickness = expect_number(t->children[1]) * kMmToM;
                }
            }
        }
    }

    void parse_layers() {
        const Node* layers = find_child(root_, "layers");
        if (!layers) return;  // unusual but tolerable

        for (std::size_t i = 1; i < layers->children.size(); ++i) {
            const Node& row = layers->children[i];
            if (!row.is_list() || row.children.size() < 3) {
                throw_parse_error(row, "expected (ordinal name type [user_name]) layer row");
            }
            circuitcore::board::Layer L;
            L.ordinal = static_cast<int>(expect_number(row.children[0]));
            L.name = std::string(expect_string_or_symbol(row.children[1]));
            L.type = std::string(expect_string_or_symbol(row.children[2]));
            layer_name_to_id_[L.name] = L.ordinal;
            board_.stackup.layers.push_back(std::move(L));
        }
    }

    void parse_stackup() {
        // (setup (stackup (layer "F.Cu" (type "copper") (thickness 0.035))
        //                 (layer "dielectric 1" (type "core") (thickness 1.6)
        //                                       (material "FR4") (epsilon_r 4.5)
        //                                       (loss_tangent 0.02)) ...))
        const Node* setup = find_child(root_, "setup");
        if (!setup) return;
        const Node* stack = find_child(*setup, "stackup");
        if (!stack) return;

        for (const Node* lay : find_children(*stack, "layer")) {
            if (lay->children.size() < 2) continue;
            const auto& name_node = lay->children[1];
            if (!name_node.is_string() && !name_node.is_symbol()) continue;
            const std::string lname = name_node.text;

            // Look up the named layer in the already-parsed stackup, or skip
            // if its a dielectric / non-copper entry we did not list above
            // (we will still mine it for material/epsilon when implementing
            // a fuller stackup model).
            circuitcore::board::Layer* target = nullptr;
            for (auto& L : board_.stackup.layers) {
                if (L.name == lname) { target = &L; break; }
            }
            if (!target) continue;

            if (const Node* t = find_child(*lay, "thickness")) {
                if (t->children.size() >= 2 && t->children[1].is_number()) {
                    target->thickness = t->children[1].number * kMmToM;
                }
            }
            if (const Node* m = find_child(*lay, "material")) {
                if (m->children.size() >= 2 &&
                    (m->children[1].is_string() || m->children[1].is_symbol())) {
                    target->material = m->children[1].text;
                }
            }
            if (const Node* e = find_child(*lay, "epsilon_r")) {
                if (e->children.size() >= 2 && e->children[1].is_number()) {
                    target->epsilon_r = e->children[1].number;
                }
            }
            if (const Node* l = find_child(*lay, "loss_tangent")) {
                if (l->children.size() >= 2 && l->children[1].is_number()) {
                    target->loss_tangent = l->children[1].number;
                }
            }
        }
    }

    void parse_nets() {
        for (const Node* netn : find_children(root_, "net")) {
            if (netn->children.size() < 3) throw_parse_error(*netn, "expected (net id name)");
            circuitcore::board::Net n;
            n.id = static_cast<int>(expect_number(netn->children[1]));
            n.name = std::string(expect_string_or_symbol(netn->children[2]));
            board_.nets.push_back(std::move(n));
        }
    }

    void parse_segments() {
        for (const Node* segn : find_children(root_, "segment")) {
            circuitcore::board::Segment s;
            if (const Node* start = find_child(*segn, "start")) s.start = read_xy_tail(*start);
            if (const Node* end   = find_child(*segn, "end"))   s.end   = read_xy_tail(*end);
            if (const Node* w     = find_child(*segn, "width")) s.width = expect_number(w->children.at(1)) * kMmToM;
            if (const Node* lay   = find_child(*segn, "layer")) {
                auto names = read_layer_names(*lay);
                if (names.empty()) throw_parse_error(*lay, "segment missing layer name");
                s.layer_ordinal = layer_id_(names[0], *lay);
            }
            if (const Node* netr  = find_child(*segn, "net"))   s.net_id = net_id_(*netr);
            board_.segments.push_back(s);
        }
    }

    void parse_vias() {
        for (const Node* vn : find_children(root_, "via")) {
            circuitcore::board::Via v;
            if (const Node* at    = find_child(*vn, "at"))    v.at = read_xy_tail(*at);
            if (const Node* sz    = find_child(*vn, "size"))  v.outer_diameter = expect_number(sz->children.at(1)) * kMmToM;
            if (const Node* dr    = find_child(*vn, "drill")) v.drill = expect_number(dr->children.at(1)) * kMmToM;
            if (const Node* lay   = find_child(*vn, "layers")) {
                auto names = read_layer_names(*lay);
                if (names.size() < 2) throw_parse_error(*lay, "via layers requires two names");
                v.from_layer = layer_id_(names[0], *lay);
                v.to_layer   = layer_id_(names[1], *lay);
            }
            if (const Node* netr  = find_child(*vn, "net")) v.net_id = net_id_(*netr);
            board_.vias.push_back(v);
        }
    }

    // Read a (pts (xy X Y) (xy X Y) ...) form into a polygon outline.
    static std::vector<circuitcore::board::Point2> read_pts(const Node& pts) {
        std::vector<circuitcore::board::Point2> out;
        for (std::size_t i = 1; i < pts.children.size(); ++i) {
            const Node& xy = pts.children[i];
            if (!xy.is_list() || xy.tag() != "xy") throw_parse_error(xy, "expected (xy X Y)");
            out.push_back(read_xy_tail(xy));
        }
        return out;
    }

    // Expand a KiCad layer-name token into the set of actual copper layer
    // ordinals it covers. Handles plain names ("F.Cu") and the multi-layer
    // shorthand ("F&B.Cu" -> [F.Cu, B.Cu]; "*.Cu" -> every copper layer).
    std::vector<int> resolve_layer_names(const std::string& token) {
        std::vector<int> out;
        if (token == "*.Cu") {
            for (const auto& L : board_.stackup.layers) {
                if (L.is_copper()) out.push_back(L.ordinal);
            }
            return out;
        }
        if (token.find('&') != std::string::npos) {
            // "F&B.Cu" -> ["F.Cu", "B.Cu"]. Split on '&', append the common
            // suffix (everything after the first '.') to each prefix.
            const auto dot = token.find('.');
            if (dot == std::string::npos) return out;
            const std::string suffix = token.substr(dot);  // ".Cu"
            const std::string prefixes = token.substr(0, dot);  // "F&B"
            std::size_t start = 0;
            while (start <= prefixes.size()) {
                const std::size_t amp = prefixes.find('&', start);
                const std::string prefix = (amp == std::string::npos)
                    ? prefixes.substr(start)
                    : prefixes.substr(start, amp - start);
                const std::string full = prefix + suffix;
                auto it = layer_name_to_id_.find(full);
                if (it != layer_name_to_id_.end()) out.push_back(it->second);
                if (amp == std::string::npos) break;
                start = amp + 1;
            }
            return out;
        }
        auto it = layer_name_to_id_.find(token);
        if (it != layer_name_to_id_.end()) out.push_back(it->second);
        return out;
    }

    void parse_zones() {
        for (const Node* zn : find_children(root_, "zone")) {
            // First gather everything that does not depend on the layer.
            int net_id = 0;
            std::string net_name;
            circuitcore::board::Polygon outline_poly;
            std::vector<circuitcore::board::Polygon> filled_polys;

            if (const Node* netr  = find_child(*zn, "net"))      net_id = net_id_(*netr);
            if (const Node* nm    = find_child(*zn, "net_name")) net_name = std::string(expect_string_or_symbol(nm->children.at(1)));

            if (const Node* poly = find_child(*zn, "polygon")) {
                if (const Node* pts = find_child(*poly, "pts")) outline_poly.outline = read_pts(*pts);
            }
            for (const Node* fp : find_children(*zn, "filled_polygon")) {
                circuitcore::board::Polygon p;
                if (const Node* pts = find_child(*fp, "pts")) p.outline = read_pts(*pts);
                filled_polys.push_back(std::move(p));
            }

            // Resolve the layer set. Modern KiCad uses (layer "X") for one,
            // (layers ...) for many. Unknown layers silently skip the zone.
            std::vector<int> layers;
            if (const Node* lay = find_child(*zn, "layer")) {
                auto names = read_layer_names(*lay);
                for (const auto& nm : names) {
                    auto resolved = resolve_layer_names(nm);
                    layers.insert(layers.end(), resolved.begin(), resolved.end());
                }
            } else if (const Node* lays = find_child(*zn, "layers")) {
                auto names = read_layer_names(*lays);
                for (const auto& nm : names) {
                    auto resolved = resolve_layer_names(nm);
                    layers.insert(layers.end(), resolved.begin(), resolved.end());
                }
            }

            // Emit one Zone per resolved layer.
            for (int ord : layers) {
                circuitcore::board::Zone z;
                z.net_id = net_id;
                z.net_name = net_name;
                z.layer_ordinal = ord;
                z.outline = outline_poly;
                z.filled = filled_polys;
                board_.zones.push_back(std::move(z));
            }
        }
    }

    void parse_outline() {
        // KiCad represents the board edge with gr_line / gr_arc / gr_circle
        // on the "Edge.Cuts" layer. We collect straight-line approximations.
        auto on_edge_cuts = [](const Node& n) {
            if (const Node* lay = find_child(n, "layer")) {
                if (lay->children.size() >= 2 &&
                    (lay->children[1].is_string() || lay->children[1].is_symbol()) &&
                    lay->children[1].text == "Edge.Cuts") {
                    return true;
                }
            }
            return false;
        };

        for (const Node* ln : find_children(root_, "gr_line")) {
            if (!on_edge_cuts(*ln)) continue;
            circuitcore::board::OutlineSegment seg;
            if (const Node* st = find_child(*ln, "start")) seg.start = read_xy_tail(*st);
            if (const Node* en = find_child(*ln, "end"))   seg.end   = read_xy_tail(*en);
            board_.outline.push_back(seg);
        }
        // gr_arc: (start, mid, end). Approximate as a polyline through ~24 points.
        for (const Node* arc : find_children(root_, "gr_arc")) {
            if (!on_edge_cuts(*arc)) continue;
            const Node* st = find_child(*arc, "start");
            const Node* mid = find_child(*arc, "mid");
            const Node* en = find_child(*arc, "end");
            if (!st || !mid || !en) continue;
            const circuitcore::board::Point2 P0 = read_xy_tail(*st);
            const circuitcore::board::Point2 P1 = read_xy_tail(*mid);
            const circuitcore::board::Point2 P2 = read_xy_tail(*en);
            // Fit a circle through 3 points: solve perpendicular bisector
            // intersection. Bail out cleanly on collinear input.
            const double ax = P1.x - P0.x, ay = P1.y - P0.y;
            const double bx = P2.x - P1.x, by = P2.y - P1.y;
            const double d = 2.0 * (ax * by - ay * bx);
            if (std::abs(d) < 1.0e-18) {
                circuitcore::board::OutlineSegment seg{P0, P2};
                board_.outline.push_back(seg);
                continue;
            }
            const double aa = P0.x * P0.x + P0.y * P0.y;
            const double bb = P1.x * P1.x + P1.y * P1.y;
            const double cc = P2.x * P2.x + P2.y * P2.y;
            const double cx = ((bb - aa) * by - (cc - bb) * ay) / d;
            const double cy = ((cc - bb) * ax - (bb - aa) * bx) / d;
            const double r = std::hypot(P0.x - cx, P0.y - cy);
            double a0 = std::atan2(P0.y - cy, P0.x - cx);
            double a1 = std::atan2(P1.y - cy, P1.x - cx);
            double a2 = std::atan2(P2.y - cy, P2.x - cx);
            // Sweep direction: pick the way that passes through a1.
            auto wrap = [](double a) {
                while (a > 3.141592653589793)  a -= 2.0 * 3.141592653589793;
                while (a < -3.141592653589793) a += 2.0 * 3.141592653589793;
                return a;
            };
            double da_ccw = wrap(a2 - a0);
            if (da_ccw <= 0.0) da_ccw += 2.0 * 3.141592653589793;
            double da_cw = da_ccw - 2.0 * 3.141592653589793;
            double mid_offset_ccw = std::abs(wrap(a1 - (a0 + 0.5 * da_ccw)));
            double mid_offset_cw  = std::abs(wrap(a1 - (a0 + 0.5 * da_cw)));
            const double sweep = (mid_offset_ccw < mid_offset_cw) ? da_ccw : da_cw;
            constexpr int N = 24;
            circuitcore::board::Point2 prev = P0;
            for (int i = 1; i <= N; ++i) {
                const double a = a0 + sweep * (static_cast<double>(i) / N);
                const circuitcore::board::Point2 p{cx + r * std::cos(a), cy + r * std::sin(a)};
                board_.outline.push_back({prev, p});
                prev = p;
            }
        }
        // gr_circle: approximate as 48-sided polygon outline.
        for (const Node* circ : find_children(root_, "gr_circle")) {
            if (!on_edge_cuts(*circ)) continue;
            const Node* cn = find_child(*circ, "center");
            const Node* en = find_child(*circ, "end");
            if (!cn || !en) continue;
            const circuitcore::board::Point2 C = read_xy_tail(*cn);
            const circuitcore::board::Point2 E = read_xy_tail(*en);
            const double r = std::hypot(E.x - C.x, E.y - C.y);
            constexpr int N = 48;
            circuitcore::board::Point2 prev{C.x + r, C.y};
            for (int i = 1; i <= N; ++i) {
                const double a = 2.0 * 3.141592653589793 * static_cast<double>(i) / N;
                const circuitcore::board::Point2 p{C.x + r * std::cos(a), C.y + r * std::sin(a)};
                board_.outline.push_back({prev, p});
                prev = p;
            }
        }
    }


    // -- Graphic items (silkscreen, mask, courtyard, fab) -----------------

    static void tessellate_arc(circuitcore::board::Point2 P0,
                                circuitcore::board::Point2 P1,
                                circuitcore::board::Point2 P2,
                                std::vector<circuitcore::board::Point2>& out) {
        const double ax = P1.x - P0.x, ay = P1.y - P0.y;
        const double bx = P2.x - P1.x, by = P2.y - P1.y;
        const double d  = 2.0 * (ax * by - ay * bx);
        if (std::abs(d) < 1.0e-18) { out = {P0, P2}; return; }
        const double aa = P0.x * P0.x + P0.y * P0.y;
        const double bb = P1.x * P1.x + P1.y * P1.y;
        const double cc = P2.x * P2.x + P2.y * P2.y;
        const double cx = ((bb - aa) * by - (cc - bb) * ay) / d;
        const double cy = ((cc - bb) * ax - (bb - aa) * bx) / d;
        const double r  = std::hypot(P0.x - cx, P0.y - cy);
        const double a0 = std::atan2(P0.y - cy, P0.x - cx);
        const double a1 = std::atan2(P1.y - cy, P1.x - cx);
        const double a2 = std::atan2(P2.y - cy, P2.x - cx);
        auto wrap = [](double a) {
            while (a >  3.141592653589793) a -= 2.0 * 3.141592653589793;
            while (a < -3.141592653589793) a += 2.0 * 3.141592653589793;
            return a;
        };
        double da_ccw = wrap(a2 - a0);
        if (da_ccw <= 0.0) da_ccw += 2.0 * 3.141592653589793;
        const double da_cw = da_ccw - 2.0 * 3.141592653589793;
        const double off_ccw = std::abs(wrap(a1 - (a0 + 0.5 * da_ccw)));
        const double off_cw  = std::abs(wrap(a1 - (a0 + 0.5 * da_cw)));
        const double sweep = (off_ccw < off_cw) ? da_ccw : da_cw;
        constexpr int N = 24;
        out.clear(); out.reserve(N + 1);
        out.push_back(P0);
        for (int i = 1; i <= N; ++i) {
            const double a = a0 + sweep * (static_cast<double>(i) / N);
            out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
    }

    // Edge.Cuts and copper layers are filtered out -- outline parser
    // already covers Edge.Cuts, and copper graphics aren't meaningful as
    // silk/mask overlays.
    int graphic_layer_ordinal(const std::string& name) const {
        if (name == "Edge.Cuts") return -1;
        auto it = layer_name_to_id_.find(name);
        if (it == layer_name_to_id_.end()) return -1;
        const int ord = it->second;
        for (const auto& L : board_.stackup.layers)
            if (L.ordinal == ord && L.is_copper()) return -1;
        return ord;
    }

    std::string single_layer_name(const Node& n) const {
        if (const Node* lay = find_child(n, "layer")) {
            if (lay->children.size() >= 2 &&
                (lay->children[1].is_string() || lay->children[1].is_symbol()))
                return lay->children[1].text;
        }
        if (const Node* lays = find_child(n, "layers")) {
            for (std::size_t i = 1; i < lays->children.size(); ++i) {
                const auto& c = lays->children[i];
                if (c.is_string() || c.is_symbol()) return c.text;
            }
        }
        return {};
    }

    static double stroke_width(const Node& n) {
        if (const Node* sr = find_child(n, "stroke")) {
            if (const Node* w = find_child(*sr, "width"))
                if (w->children.size() >= 2 && w->children[1].is_number())
                    return w->children[1].number * kMmToM;
        }
        if (const Node* w = find_child(n, "width"))
            if (w->children.size() >= 2 && w->children[1].is_number())
                return w->children[1].number * kMmToM;
        return 0.0;
    }

    static circuitcore::board::Point2 xform_fp(circuitcore::board::Point2 p,
                                                 circuitcore::board::Point2 fp_at,
                                                 double fp_rot) {
        const double cs = std::cos(fp_rot), sn = std::sin(fp_rot);
        return {fp_at.x + cs * p.x - sn * p.y,
                fp_at.y + sn * p.x + cs * p.y};
    }

    // Convert a (gr_line | fp_line)-shaped node into a GraphicItem.
    bool node_to_line(const Node& ln, circuitcore::board::GraphicItem& g) const {
        const int ord = graphic_layer_ordinal(single_layer_name(ln));
        if (ord < 0) return false;
        const Node* st = find_child(ln, "start");
        const Node* en = find_child(ln, "end");
        if (!st || !en) return false;
        g.kind = circuitcore::board::GraphicItem::Kind::Line;
        g.layer_ordinal = ord;
        g.stroke_width  = stroke_width(ln);
        g.points = {read_xy_tail(*st), read_xy_tail(*en)};
        return true;
    }

    bool node_to_arc(const Node& arc, circuitcore::board::GraphicItem& g) const {
        const int ord = graphic_layer_ordinal(single_layer_name(arc));
        if (ord < 0) return false;
        const Node* st  = find_child(arc, "start");
        const Node* mid = find_child(arc, "mid");
        const Node* en  = find_child(arc, "end");
        if (!st || !mid || !en) return false;
        g.kind = circuitcore::board::GraphicItem::Kind::Arc;
        g.layer_ordinal = ord;
        g.stroke_width  = stroke_width(arc);
        tessellate_arc(read_xy_tail(*st), read_xy_tail(*mid),
                        read_xy_tail(*en), g.points);
        return !g.points.empty();
    }

    bool node_to_circle(const Node& circ, circuitcore::board::GraphicItem& g) const {
        const int ord = graphic_layer_ordinal(single_layer_name(circ));
        if (ord < 0) return false;
        const Node* cn = find_child(circ, "center");
        const Node* en = find_child(circ, "end");
        if (!cn || !en) return false;
        const auto C = read_xy_tail(*cn);
        const auto E = read_xy_tail(*en);
        const double r = std::hypot(E.x - C.x, E.y - C.y);
        g.kind = circuitcore::board::GraphicItem::Kind::Circle;
        g.layer_ordinal = ord;
        g.stroke_width  = stroke_width(circ);
        constexpr int N = 48;
        g.points.reserve(N + 1);
        for (int i = 0; i <= N; ++i) {
            const double a = 2.0 * 3.141592653589793 *
                              static_cast<double>(i) / N;
            g.points.push_back({C.x + r * std::cos(a),
                                 C.y + r * std::sin(a)});
        }
        return true;
    }

    bool node_to_poly(const Node& poly, circuitcore::board::GraphicItem& g) const {
        const int ord = graphic_layer_ordinal(single_layer_name(poly));
        if (ord < 0) return false;
        const Node* pts = find_child(poly, "pts");
        if (!pts) return false;
        g.kind = circuitcore::board::GraphicItem::Kind::Polygon;
        g.layer_ordinal = ord;
        g.stroke_width  = stroke_width(poly);
        g.points = read_pts(*pts);
        return g.points.size() >= 3;
    }

    bool node_to_text(const Node& tx, std::size_t text_index,
                       circuitcore::board::GraphicItem& g) const {
        if (tx.children.size() <= text_index) return false;
        const auto& tn = tx.children[text_index];
        if (!tn.is_string() && !tn.is_symbol()) return false;
        const int ord = graphic_layer_ordinal(single_layer_name(tx));
        if (ord < 0) return false;
        g.kind = circuitcore::board::GraphicItem::Kind::Text;
        g.layer_ordinal = ord;
        g.text = tn.text;
        if (const Node* at = find_child(tx, "at")) {
            g.points.push_back(read_xy_tail(*at));
            if (at->children.size() >= 4 && at->children[3].is_number())
                g.text_angle = at->children[3].number * kDegToRad;
        }
        if (const Node* eff = find_child(tx, "effects"))
            if (const Node* font = find_child(*eff, "font"))
                if (const Node* sz = find_child(*font, "size"))
                    if (sz->children.size() >= 3 && sz->children[1].is_number())
                        g.text_size = sz->children[1].number * kMmToM;
        return !g.points.empty();
    }

    void parse_graphics() {
        circuitcore::board::GraphicItem g;
        // gr_line / gr_arc / gr_circle / gr_poly are board-level.
        for (const Node* n : find_children(root_, "gr_line")) {
            g = {}; if (node_to_line  (*n, g)) board_.graphics.push_back(std::move(g));
        }
        for (const Node* n : find_children(root_, "gr_arc")) {
            g = {}; if (node_to_arc   (*n, g)) board_.graphics.push_back(std::move(g));
        }
        for (const Node* n : find_children(root_, "gr_circle")) {
            g = {}; if (node_to_circle(*n, g)) board_.graphics.push_back(std::move(g));
        }
        for (const Node* n : find_children(root_, "gr_poly")) {
            g = {}; if (node_to_poly  (*n, g)) board_.graphics.push_back(std::move(g));
        }
        for (const Node* n : find_children(root_, "gr_text")) {
            g = {}; if (node_to_text(*n, /*text_index=*/1, g))
                board_.graphics.push_back(std::move(g));
        }
    }

    void parse_footprints() {
        for (const Node* fp : find_children(root_, "footprint")) {
            // Footprint library id (eg "Connector:PinHeader_1x02").
            std::string fp_name;
            if (fp->children.size() >= 2 &&
                (fp->children[1].is_string() || fp->children[1].is_symbol())) {
                fp_name = fp->children[1].text;
            }
            // Reference designator -- KiCad stores it as a (property "Reference" "C12" ...) child.
            // Value is the sibling (property "Value" ...).
            std::string fp_ref;
            std::string fp_value;
            for (const Node* prop : find_children(*fp, "property")) {
                if (prop->children.size() < 3) continue;
                if (!(prop->children[1].is_string() || prop->children[1].is_symbol()))
                    continue;
                if (!(prop->children[2].is_string() || prop->children[2].is_symbol()))
                    continue;
                if (prop->children[1].text == "Reference") fp_ref   = prop->children[2].text;
                if (prop->children[1].text == "Value")     fp_value = prop->children[2].text;
            }
            // Footprint origin (mm) + rotation (deg), applied to each pad's local (at).
            circuitcore::board::Point2 fp_at{0, 0};
            double fp_rot = 0.0;
            if (const Node* at = find_child(*fp, "at")) {
                fp_at = read_xy_tail(*at);
                if (at->children.size() >= 4 && at->children[3].is_number()) {
                    fp_rot = at->children[3].number * kDegToRad;
                }
            }

            for (const Node* pad : find_children(*fp, "pad")) {
                circuitcore::board::Pad p;
                // KiCad pad form: (pad "<name>" <type> <shape> ...)
                if (pad->children.size() >= 2 &&
                    (pad->children[1].is_string() || pad->children[1].is_symbol())) {
                    p.name = pad->children[1].text;
                }
                // children[3] is the shape token (circle/rect/oval/roundrect/...).
                if (pad->children.size() >= 4 && pad->children[3].is_symbol()) {
                    const std::string& shp = pad->children[3].text;
                    if (shp == "circle")         p.shape = circuitcore::board::PadShape::Circle;
                    else if (shp == "rect")      p.shape = circuitcore::board::PadShape::Rect;
                    else if (shp == "oval")      p.shape = circuitcore::board::PadShape::Oval;
                    else if (shp == "roundrect") p.shape = circuitcore::board::PadShape::RoundRect;
                    else                          p.shape = circuitcore::board::PadShape::Custom;
                }
                if (const Node* sz = find_child(*pad, "size")) {
                    if (sz->children.size() >= 3) {
                        p.size.x = expect_number(sz->children[1]) * kMmToM;
                        p.size.y = expect_number(sz->children[2]) * kMmToM;
                    }
                }
                if (const Node* at = find_child(*pad, "at")) {
                    circuitcore::board::Point2 local = read_xy_tail(*at);
                    // Rotate local by fp_rot then translate by fp_at.
                    double cs = std::cos(fp_rot), sn = std::sin(fp_rot);
                    p.at.x = fp_at.x + cs * local.x - sn * local.y;
                    p.at.y = fp_at.y + sn * local.x + cs * local.y;
                    if (at->children.size() >= 4 && at->children[3].is_number()) {
                        p.rotation = at->children[3].number * kDegToRad;
                    }
                }
                if (const Node* lay = find_child(*pad, "layers")) {
                    for (const auto& name : read_layer_names(*lay)) {
                        if (name == "*.Cu") {
                            // Wildcard expands to every copper layer in the stackup
                            // (typical for through-hole pad layer lists).
                            for (const auto& L : board_.stackup.layers) {
                                if (L.is_copper()) p.layer_ordinals.push_back(L.ordinal);
                            }
                            continue;
                        }
                        // Other wildcards (*.Mask, F.*, etc.) are not copper layers
                        // and don't matter for PI analysis — skip silently.
                        if (name.find('*') != std::string::npos) continue;

                        auto it = layer_name_to_id_.find(name);
                        if (it != layer_name_to_id_.end()) {
                            p.layer_ordinals.push_back(it->second);
                        }
                    }
                }
                if (const Node* netr = find_child(*pad, "net")) {
                    if (netr->children.size() >= 2) {
                        p.net_id = net_id_(*netr);
                    }
                }
                p.parent_ref = fp_ref;
                board_.pads.push_back(p);
            }

            // fp_line / fp_arc on Edge.Cuts inside a footprint define a
            // board cutout local to that footprint (USB / SD / castellated
            // PMOD-style breakouts that need a slot in the PCB edge). The
            // board-level outline pass only sees gr_*, so without this
            // step the cutouts are dropped and the connector's pads appear
            // to float "outside" the rendered board outline. Transform
            // local points by fp_at + fp_rot and push as outline segments.
            auto fp_on_edge_cuts = [&](const Node& n) {
                return single_layer_name(n) == "Edge.Cuts";
            };
            for (const Node* ln : find_children(*fp, "fp_line")) {
                if (!fp_on_edge_cuts(*ln)) continue;
                const Node* st = find_child(*ln, "start");
                const Node* en = find_child(*ln, "end");
                if (!st || !en) continue;
                circuitcore::board::OutlineSegment seg;
                seg.start = xform_fp(read_xy_tail(*st), fp_at, fp_rot);
                seg.end   = xform_fp(read_xy_tail(*en), fp_at, fp_rot);
                board_.outline.push_back(seg);
            }
            for (const Node* arc : find_children(*fp, "fp_arc")) {
                if (!fp_on_edge_cuts(*arc)) continue;
                const Node* st  = find_child(*arc, "start");
                const Node* mid = find_child(*arc, "mid");
                const Node* en  = find_child(*arc, "end");
                if (!st || !mid || !en) continue;
                const auto P0 = xform_fp(read_xy_tail(*st),  fp_at, fp_rot);
                const auto P1 = xform_fp(read_xy_tail(*mid), fp_at, fp_rot);
                const auto P2 = xform_fp(read_xy_tail(*en),  fp_at, fp_rot);
                std::vector<circuitcore::board::Point2> pts;
                tessellate_arc(P0, P1, P2, pts);
                for (std::size_t i = 1; i < pts.size(); ++i) {
                    board_.outline.push_back({pts[i - 1], pts[i]});
                }
            }

            // Footprint-local graphic items (silk / mask / courtyard / fab).
            // Transform from footprint frame to board frame.
            auto push_fp = [&](circuitcore::board::GraphicItem g) {
                for (auto& pt : g.points) pt = xform_fp(pt, fp_at, fp_rot);
                board_.graphics.push_back(std::move(g));
            };
            for (const Node* ln : find_children(*fp, "fp_line")) {
                circuitcore::board::GraphicItem g;
                if (node_to_line(*ln, g)) push_fp(std::move(g));
            }
            for (const Node* arc : find_children(*fp, "fp_arc")) {
                circuitcore::board::GraphicItem g;
                if (node_to_arc(*arc, g)) push_fp(std::move(g));
            }
            for (const Node* circ : find_children(*fp, "fp_circle")) {
                circuitcore::board::GraphicItem g;
                if (node_to_circle(*circ, g)) push_fp(std::move(g));
            }
            for (const Node* poly : find_children(*fp, "fp_poly")) {
                circuitcore::board::GraphicItem g;
                if (node_to_poly(*poly, g)) push_fp(std::move(g));
            }
            for (const Node* tx : find_children(*fp, "fp_text")) {
                // fp_text form: (fp_text <kind> "<value>" ...).
                // <kind> is one of reference / value / user. v1 skips
                // reference / value -- they're component designators
                // already exposed via property -- and renders only
                // user-placed text.
                if (tx->children.size() < 3) continue;
                const auto& kind = tx->children[1];
                if (kind.is_symbol() && kind.text != "user") continue;
                circuitcore::board::GraphicItem g;
                if (node_to_text(*tx, /*text_index=*/2, g))
                    push_fp(std::move(g));
            }

            // Build the Component record: identifier + bbox of any
            // F.CrtYd / B.CrtYd lines / arcs / circles / polys this
            // footprint defined. Iterating the geometry primitives a
            // second time here keeps the courtyard bbox local to the
            // footprint rather than scanning board_.graphics later.
            circuitcore::board::Component comp;
            comp.name      = fp_name;
            comp.reference = fp_ref;
            comp.value     = fp_value;
            comp.at        = fp_at;
            comp.rotation  = fp_rot;
            double cx_lo =  1e30, cy_lo =  1e30;
            double cx_hi = -1e30, cy_hi = -1e30;
            auto include = [&](circuitcore::board::Point2 local) {
                const auto p = xform_fp(local, fp_at, fp_rot);
                if (p.x < cx_lo) cx_lo = p.x;
                if (p.y < cy_lo) cy_lo = p.y;
                if (p.x > cx_hi) cx_hi = p.x;
                if (p.y > cy_hi) cy_hi = p.y;
            };
            auto on_courtyard = [&](int ord) {
                const auto* L = board_.find_layer(ord);
                if (!L) return false;
                return L->name == "F.CrtYd" || L->name == "B.CrtYd";
            };
            auto consume_geom = [&](const Node& node, const char* tag) {
                circuitcore::board::GraphicItem g;
                bool ok = false;
                if      (std::string(tag) == "fp_line")   ok = node_to_line  (node, g);
                else if (std::string(tag) == "fp_arc")    ok = node_to_arc   (node, g);
                else if (std::string(tag) == "fp_circle") ok = node_to_circle(node, g);
                else if (std::string(tag) == "fp_poly")   ok = node_to_poly  (node, g);
                if (!ok) return;
                if (!on_courtyard(g.layer_ordinal)) return;
                for (const auto& pt : g.points) include(pt);
            };
            for (const Node* ln   : find_children(*fp, "fp_line"))   consume_geom(*ln,  "fp_line");
            for (const Node* ar   : find_children(*fp, "fp_arc"))    consume_geom(*ar,  "fp_arc");
            for (const Node* cir  : find_children(*fp, "fp_circle")) consume_geom(*cir, "fp_circle");
            for (const Node* pol  : find_children(*fp, "fp_poly"))   consume_geom(*pol, "fp_poly");
            if (cx_lo <= cx_hi && cy_lo <= cy_hi) {
                comp.courtyard_lo = {cx_lo, cy_lo};
                comp.courtyard_hi = {cx_hi, cy_hi};
            }
            board_.components.push_back(std::move(comp));
        }
    }

    const Node& root_;
    circuitcore::board::Board board_;
    std::unordered_map<std::string, int> layer_name_to_id_;
};

}  // namespace

std::string ParseError::format() const {
    return std::format("kicad_pcb parse error at line {}, col {}: {}",
                       line, col, message);
}

// Internal helper: bridges sexpr::ParseError into our own ParseError so
// callers only deal with one error type.
namespace {

ParseError from_sexpr(const circuitcore::sexpr::ParseError& e) {
    return ParseError{e.what(), e.line, e.col};
}

}  // namespace

std::expected<circuitcore::board::Board, ParseError>
PcbParser::parse_string(std::string_view src) {
    try {
        Node root = parse(src);
        return Walker(root).build();
    } catch (const ParseError& e) {
        return std::unexpected(e);
    } catch (const circuitcore::sexpr::ParseError& e) {
        return std::unexpected(from_sexpr(e));
    } catch (const std::exception& e) {
        // Anything else that leaks from the s-expr parser or a helper
        // (bad_alloc, std::format, ...) gets wrapped so the public API
        // really is exception-free.
        return std::unexpected(ParseError{e.what(), 0, 0});
    }
}

std::expected<circuitcore::board::Board, ParseError>
PcbParser::parse_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(ParseError{
            std::format("cannot open kicad_pcb file: {}", path.string()),
            0, 0});
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_string(ss.str());
}

}  // namespace circuitcore::formats::kicad
