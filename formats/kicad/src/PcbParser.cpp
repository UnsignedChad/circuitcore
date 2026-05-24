#include "circuitcore/formats/kicad/PcbParser.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "circuitcore/sexpr/SExpr.h"

namespace circuitcore::formats::kicad {

using circuitcore::sexpr::Node;
using circuitcore::sexpr::parse;

namespace {

// Convert KiCad mm → SI meters.
constexpr double kMmToM = 1.0e-3;

// Convert degrees → radians.
constexpr double kDegToRad = 0.017453292519943295;  // M_PI / 180.0

[[noreturn]] void fail(const Node& n, const std::string& msg) {
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
    if (!n.is_number()) fail(n, "expected number");
    return n.number;
}

std::string_view expect_string_or_symbol(const Node& n) {
    if (n.is_string() || n.is_symbol()) return n.text;
    fail(n, "expected string or symbol");
}

// Read a 2-element (x y) tail starting at child index `start` in `node`.
// Used for both (at X Y) and (xy X Y) — and (start X Y) / (end X Y).
circuitcore::board::Point2 read_xy_tail(const Node& node, std::size_t start = 1) {
    if (node.children.size() < start + 2) fail(node, "expected at least two numeric coordinates");
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
            fail(root_, "top-level form must be (kicad_pcb ...)");
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
        return std::move(board_);
    }

private:
    int layer_id_(std::string_view name, const Node& ctx) {
        auto it = layer_name_to_id_.find(std::string(name));
        if (it == layer_name_to_id_.end()) {
            fail(ctx, std::format("unknown layer name '{}'", name));
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
                fail(row, "expected (ordinal name type [user_name]) layer row");
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
            if (netn->children.size() < 3) fail(*netn, "expected (net id name)");
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
                if (names.empty()) fail(*lay, "segment missing layer name");
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
                if (names.size() < 2) fail(*lay, "via layers requires two names");
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
            if (!xy.is_list() || xy.tag() != "xy") fail(xy, "expected (xy X Y)");
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

    void parse_footprints() {
        for (const Node* fp : find_children(root_, "footprint")) {
            // Reference designator -- KiCad stores it as a (property "Reference" "C12" ...) child.
            std::string fp_ref;
            for (const Node* prop : find_children(*fp, "property")) {
                if (prop->children.size() >= 3 &&
                    (prop->children[1].is_string() || prop->children[1].is_symbol()) &&
                    prop->children[1].text == "Reference" &&
                    (prop->children[2].is_string() || prop->children[2].is_symbol())) {
                    fp_ref = prop->children[2].text;
                    break;
                }
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
