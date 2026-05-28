// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "pi/IrMesher.h"
#include "circuitcore/board/Bounds.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <iterator>
#include <set>
#include <numbers>
#include <vector>
#include <utility>

namespace pdnkit::pi {

namespace {

// Ray-casting point-in-polygon (duplicated from HitTest to avoid pulling the
// hittest module — they are independent concerns and this is 10 lines).
bool point_in_ring(const std::vector<circuitcore::board::Point2>& ring, double px, double py) {
    bool inside = false;
    const std::size_t n = ring.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& pi_ = ring[i];
        const auto& pj_ = ring[j];
        if (((pi_.y > py) != (pj_.y > py)) &&
            (px < (pj_.x - pi_.x) * (py - pi_.y) / (pj_.y - pi_.y) + pi_.x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_in_polygon(const circuitcore::board::Polygon& poly, double px, double py) {
    if (!point_in_ring(poly.outline, px, py)) return false;
    for (const auto& h : poly.holes) {
        if (point_in_ring(h, px, py)) return false;
    }
    return true;
}

// Returns true if (px, py) lies in any filled polygon of any matching zone.
bool point_in_target_copper(const circuitcore::board::Board& board, int net, int layer,
                            double px, double py) {
    for (const auto& z : board.zones) {
        if (z.net_id != net) continue;
        if (z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            if (point_in_polygon(fp, px, py)) return true;
        }
    }
    return false;
}

// Shoelace area of a closed polygon ring (always positive).
double polygon_ring_area(const std::vector<circuitcore::board::Point2>& ring) {
    if (ring.size() < 3) return 0.0;
    double a = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const std::size_t j = (i + 1) % ring.size();
        a += ring[i].x * ring[j].y - ring[j].x * ring[i].y;
    }
    return std::abs(a) * 0.5;
}

// Total filled-zone area for (net, layer) in m^2. Approximates "is there
// copper to mesh here?" -- holes subtract from the polygon.
double zone_area_on(const circuitcore::board::Board& board, int net, int layer) {
    double total = 0.0;
    for (const auto& z : board.zones) {
        if (z.net_id != net || z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            double a = polygon_ring_area(fp.outline);
            for (const auto& h : fp.holes) a -= polygon_ring_area(h);
            total += std::max(0.0, a);
        }
    }
    return total;
}

// World bbox of all filled polygons on the target (net, layer). Returns
// false if there is no matching geometry. Thin wrapper over the canonical
// board::bounds_of_zone so callers using the four-out-parameter shape
// don't have to change.
bool target_bbox(const circuitcore::board::Board& board, int net, int layer,
                 double& lo_x, double& lo_y, double& hi_x, double& hi_y) {
    const auto b = circuitcore::board::bounds_of_zone(board, net, layer);
    if (!b.valid) return false;
    lo_x = b.lo_x; lo_y = b.lo_y;
    hi_x = b.hi_x; hi_y = b.hi_y;
    return true;
}

}  // namespace

namespace {

// Mesh one copper layer for the target net. Appends nodes to `mesh.nodes`,
// adds sheet-conductance resistors, and returns a (cell -> node id) table
// along with the grid layout for later via-wiring lookups.
struct LayerSubmesh {
    int layer_ordinal = 0;
    std::vector<int> cell_to_node;  // size nx*ny, -1 if outside copper
    int nx = 0;
    int ny = 0;
    double lo_x = 0.0, lo_y = 0.0;
    double cell_size = 0.0;
};

LayerSubmesh mesh_one_layer(const circuitcore::board::Board& board, const MeshConfig& cfg,
                             int layer_ord, IrMesh& mesh, double g_per_square) {
    LayerSubmesh sm;
    sm.layer_ordinal = layer_ord;
    sm.cell_size = cfg.cell_size;

    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    if (!target_bbox(board, cfg.net_id, layer_ord, lo_x, lo_y, hi_x, hi_y)) {
        return sm;
    }
    sm.lo_x = lo_x;
    sm.lo_y = lo_y;
    sm.nx = std::max(1, static_cast<int>((hi_x - lo_x) / cfg.cell_size));
    sm.ny = std::max(1, static_cast<int>((hi_y - lo_y) / cfg.cell_size));
    sm.cell_to_node.assign(static_cast<std::size_t>(sm.nx) * sm.ny, -1);

    auto cell_index = [&sm](int i, int j) { return j * sm.nx + i; };

    for (int j = 0; j < sm.ny; ++j) {
        for (int i = 0; i < sm.nx; ++i) {
            const double cx = lo_x + (i + 0.5) * cfg.cell_size;
            const double cy = lo_y + (j + 0.5) * cfg.cell_size;
            if (!point_in_target_copper(board, cfg.net_id, layer_ord, cx, cy)) {
                continue;
            }
            Node n;
            n.id = static_cast<int>(mesh.nodes.size());
            n.x = cx;
            n.y = cy;
            n.grid_i = i;
            n.grid_j = j;
            n.layer_ordinal = layer_ord;
            mesh.nodes.push_back(n);
            sm.cell_to_node[cell_index(i, j)] = n.id;
        }
    }
    for (int j = 0; j < sm.ny; ++j) {
        for (int i = 0; i < sm.nx; ++i) {
            const int a = sm.cell_to_node[cell_index(i, j)];
            if (a < 0) continue;
            if (i + 1 < sm.nx) {
                const int b = sm.cell_to_node[cell_index(i + 1, j)];
                if (b >= 0) mesh.resistors.push_back({a, b, g_per_square});
            }
            if (j + 1 < sm.ny) {
                const int b = sm.cell_to_node[cell_index(i, j + 1)];
                if (b >= 0) mesh.resistors.push_back({a, b, g_per_square});
            }
        }
    }
    return sm;
}

// Every mesh node whose cell center falls inside the pad footprint on the
// given layer. Used to spread source/sink current across an edge contact
// rather than a single point -- on a 2D sheet, point-load spreading
// resistance diverges as the contact shrinks, so a pad bigger than the
// mesh cell deserves a multi-node attachment.
std::vector<int> nodes_under_pad(const IrMesh& mesh, const circuitcore::board::Pad& pad,
                                  int layer) {
    std::vector<int> out;
    const bool radial = (pad.shape == circuitcore::board::PadShape::Circle) ||
                        (pad.size.x <= 0.0 || pad.size.y <= 0.0);
    if (radial) {
        const double r = (pad.size.x > 0.0) ? 0.5 * pad.size.x : 0.0;
        if (r <= 0.0) return out;
        const double r2 = r * r;
        for (const auto& n : mesh.nodes) {
            if (n.layer_ordinal != layer) continue;
            const double dx = n.x - pad.at.x;
            const double dy = n.y - pad.at.y;
            if (dx * dx + dy * dy <= r2) out.push_back(n.id);
        }
        return out;
    }
    const double hw = 0.5 * pad.size.x;
    const double hh = 0.5 * pad.size.y;
    const double cs = std::cos(-pad.rotation);
    const double sn = std::sin(-pad.rotation);
    for (const auto& n : mesh.nodes) {
        if (n.layer_ordinal != layer) continue;
        const double dx = n.x - pad.at.x;
        const double dy = n.y - pad.at.y;
        const double lx = cs * dx - sn * dy;
        const double ly = sn * dx + cs * dy;
        if (std::abs(lx) <= hw && std::abs(ly) <= hh) out.push_back(n.id);
    }
    return out;
}

// Build a 1D resistor-graph IrMesh from track segments on (net, layer).
// Used as a fallback when the zone-based mesher comes up empty (boards
// that route power via tracks instead of pours). Each segment becomes a
// single resistor R = rho * L / (W * t); endpoints are de-duped within
// 1 um. Pads on the net attach to the nearest track node within ~1 mm.
IrMesh build_track_mesh(const circuitcore::board::Board& board, const MeshConfig& cfg,
                         int layer_ord) {
    IrMesh mesh;
    // Endpoint dedup. Snap to a 1-micrometer grid to merge KiCad-format
    // 6-decimal coordinate noise.
    constexpr double kSnap = 1.0e-6;
    std::map<std::pair<long long, long long>, int> point_to_id;
    auto key_of = [&](const circuitcore::board::Point2& p) {
        return std::pair<long long, long long>{
            static_cast<long long>(std::lround(p.x / kSnap)),
            static_cast<long long>(std::lround(p.y / kSnap))
        };
    };
    auto get_or_create = [&](const circuitcore::board::Point2& p) {
        auto k = key_of(p);
        auto it = point_to_id.find(k);
        if (it != point_to_id.end()) return it->second;
        Node n;
        n.id = static_cast<int>(mesh.nodes.size());
        n.x = p.x;
        n.y = p.y;
        n.layer_ordinal = layer_ord;
        mesh.nodes.push_back(n);
        point_to_id[k] = n.id;
        return n.id;
    };

    for (const auto& seg : board.segments) {
        if (seg.net_id != cfg.net_id) continue;
        if (seg.layer_ordinal != layer_ord) continue;
        if (seg.width <= 0.0) continue;
        const double length = std::hypot(seg.end.x - seg.start.x,
                                          seg.end.y - seg.start.y);
        if (length <= 0.0) continue;
        // Per-layer thickness if the stackup supplied one; else cfg fallback.
        double thickness = cfg.copper_thickness;
        if (const circuitcore::board::Layer* L = board.find_layer(layer_ord)) {
            if (L->thickness > 0.0) thickness = L->thickness;
        }
        const double R = cfg.copper_rho * length / (seg.width * thickness);
        if (R <= 0.0) continue;
        const int a = get_or_create(seg.start);
        const int b = get_or_create(seg.end);
        if (a == b) continue;
        mesh.resistors.push_back({a, b, 1.0 / R});
    }

    if (mesh.nodes.empty()) return mesh;

    // Bbox.
    double lo_x = mesh.nodes[0].x, hi_x = lo_x;
    double lo_y = mesh.nodes[0].y, hi_y = lo_y;
    for (const auto& n : mesh.nodes) {
        lo_x = std::min(lo_x, n.x); hi_x = std::max(hi_x, n.x);
        lo_y = std::min(lo_y, n.y); hi_y = std::max(hi_y, n.y);
    }
    mesh.bbox_lo_x = lo_x; mesh.bbox_lo_y = lo_y;
    mesh.bbox_hi_x = hi_x; mesh.bbox_hi_y = hi_y;

    // Pad -> nearest track node within ~1mm tolerance. Drive source/sink
    // with the auto-pick (leftmost/rightmost pad on net).
    auto nearest = [&](double px, double py) {
        int best = -1;
        double best_d2 = std::numeric_limits<double>::infinity();
        for (const auto& n : mesh.nodes) {
            const double dx = n.x - px;
            const double dy = n.y - py;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best = n.id; }
        }
        return std::pair<int, double>{best, best_d2};
    };

    const circuitcore::board::Pad* src = nullptr;
    const circuitcore::board::Pad* snk = nullptr;
    for (const auto& pad : board.pads) {
        if (pad.net_id != cfg.net_id) continue;
        bool on_layer = false;
        for (int o : pad.layer_ordinals) if (o == layer_ord) { on_layer = true; break; }
        if (!on_layer) continue;
        if (!src || pad.at.x < src->at.x) src = &pad;
        if (!snk || pad.at.x > snk->at.x) snk = &pad;
    }
    constexpr double kPadTol = 1.0e-3;     // 1 mm
    constexpr double kPadTol2 = kPadTol * kPadTol;
    if (src) {
        auto [nid, d2] = nearest(src->at.x, src->at.y);
        if (nid >= 0 && d2 <= kPadTol2) mesh.source_node_ids.push_back(nid);
    }
    if (snk && snk != src) {
        auto [nid, d2] = nearest(snk->at.x, snk->at.y);
        if (nid >= 0 && d2 <= kPadTol2) mesh.sink_node_ids.push_back(nid);
    }
    return mesh;
}

// Drop nodes in components that lack BOTH a source AND a sink, then
// renumber the survivors so node IDs stay contiguous. Without this the
// solver gets isolated copper islands and CHOLMOD hits an indefinite
// matrix.
void prune_disconnected(IrMesh& mesh) {
    const int N = static_cast<int>(mesh.nodes.size());
    if (N == 0 || mesh.resistors.empty()) return;
    // If the mesher had nothing to inject (no source / sink / explicit
    // currents) there is nothing to prune against -- the user is probably
    // just inspecting the mesh structure. Skip silently.
    if (mesh.source_node_ids.empty() && mesh.sink_node_ids.empty() &&
        mesh.node_currents.empty()) {
        return;
    }

    // Union-find over the resistor graph.
    std::vector<int> parent(N);
    std::iota(parent.begin(), parent.end(), 0);
    auto find_root = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](int a, int b) {
        const int ra = find_root(a);
        const int rb = find_root(b);
        if (ra != rb) parent[ra] = rb;
    };
    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= N) continue;
        if (r.to_node   < 0 || r.to_node   >= N) continue;
        unite(r.from_node, r.to_node);
    }

    // Which components contain at least one source / sink?
    std::set<int> source_components;
    std::set<int> sink_components;
    auto note_source = [&](int id) {
        if (id >= 0 && id < N) source_components.insert(find_root(id));
    };
    auto note_sink = [&](int id) {
        if (id >= 0 && id < N) sink_components.insert(find_root(id));
    };
    for (int id : mesh.source_node_ids) note_source(id);
    for (int id : mesh.sink_node_ids)   note_sink(id);
    for (const auto& [id, cur] : mesh.node_currents) {
        if (cur > 0.0) note_source(id);
        else if (cur < 0.0) note_sink(id);
    }

    // Components with both. Keep nodes whose root is in this set.
    std::set<int> valid;
    std::set_intersection(source_components.begin(), source_components.end(),
                          sink_components.begin(),  sink_components.end(),
                          std::inserter(valid, valid.begin()));
    if (valid.empty()) {
        // Nothing solvable -- clear everything.
        mesh.nodes.clear();
        mesh.resistors.clear();
        mesh.source_node_ids.clear();
        mesh.sink_node_ids.clear();
        mesh.node_currents.clear();
        return;
    }

    // Renumber survivors densely.
    std::vector<int> old_to_new(N, -1);
    std::vector<Node> new_nodes;
    new_nodes.reserve(N);
    for (int i = 0; i < N; ++i) {
        if (!valid.count(find_root(i))) continue;
        const int new_id = static_cast<int>(new_nodes.size());
        old_to_new[i] = new_id;
        Node nn = mesh.nodes[i];
        nn.id = new_id;
        new_nodes.push_back(nn);
    }
    mesh.nodes = std::move(new_nodes);

    std::vector<Resistor> new_resistors;
    new_resistors.reserve(mesh.resistors.size());
    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= N) continue;
        if (r.to_node   < 0 || r.to_node   >= N) continue;
        const int a = old_to_new[r.from_node];
        const int b = old_to_new[r.to_node];
        if (a < 0 || b < 0) continue;
        new_resistors.push_back({a, b, r.conductance});
    }
    mesh.resistors = std::move(new_resistors);

    auto remap_list = [&](std::vector<int>& v) {
        std::vector<int> out;
        out.reserve(v.size());
        for (int id : v) {
            if (id >= 0 && id < N && old_to_new[id] >= 0) {
                out.push_back(old_to_new[id]);
            }
        }
        v = std::move(out);
    };
    remap_list(mesh.source_node_ids);
    remap_list(mesh.sink_node_ids);

    std::vector<std::pair<int, double>> new_currents;
    new_currents.reserve(mesh.node_currents.size());
    for (const auto& [id, cur] : mesh.node_currents) {
        if (id >= 0 && id < N && old_to_new[id] >= 0) {
            new_currents.emplace_back(old_to_new[id], cur);
        }
    }
    mesh.node_currents = std::move(new_currents);
}

// Nearest node on a specific layer to a world point, or -1 if none.
int nearest_node_on_layer(const IrMesh& mesh, double px, double py, int layer) {
    int best = -1;
    double best_d2 = std::numeric_limits<double>::infinity();
    for (const auto& n : mesh.nodes) {
        if (n.layer_ordinal != layer) continue;
        const double dx = n.x - px;
        const double dy = n.y - py;
        const double d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = n.id;
        }
    }
    return best;
}

}  // namespace

IrMesh IrMesher::build(const circuitcore::board::Board& board, const MeshConfig& cfg) {
    IrMesh mesh;
    if (cfg.cell_size <= 0.0 || cfg.copper_thickness <= 0.0 ||
        cfg.copper_rho <= 0.0) {
        return mesh;
    }

    // Smart layer auto-pick: if the user-requested layer has no copper for
    // this net, find the copper layer with the most filled-zone area and
    // switch to it. Keep cfg const externally by working with a local copy.
    int primary_layer = cfg.layer_ordinal;
    if (cfg.auto_select_layer) {
        if (zone_area_on(board, cfg.net_id, primary_layer) <= 0.0) {
            int best_layer = primary_layer;
            double best_area = 0.0;
            for (const auto& L : board.stackup.layers) {
                if (!L.is_copper()) continue;
                const double a = zone_area_on(board, cfg.net_id, L.ordinal);
                if (a > best_area) {
                    best_area = a;
                    best_layer = L.ordinal;
                }
            }
            if (best_area > 0.0) primary_layer = best_layer;
        }
    }

    // Build the ordered list of layers (primary first, then extras).
    std::vector<int> layers = {primary_layer};
    for (int l : cfg.extra_layer_ordinals) {
        bool seen = false;
        for (int e : layers) if (e == l) { seen = true; break; }
        if (!seen) layers.push_back(l);
    }

    const double g_per_square = cfg.copper_thickness / cfg.copper_rho;

    std::vector<LayerSubmesh> submeshes;
    for (int layer : layers) {
        submeshes.push_back(mesh_one_layer(board, cfg, layer, mesh, g_per_square));
    }

    // Overall bbox = union of per-layer submeshes.
    bool any_bbox = false;
    for (const auto& sm : submeshes) {
        if (sm.nx == 0 || sm.ny == 0) continue;
        const double hi_x = sm.lo_x + sm.nx * sm.cell_size;
        const double hi_y = sm.lo_y + sm.ny * sm.cell_size;
        if (!any_bbox) {
            mesh.bbox_lo_x = sm.lo_x;
            mesh.bbox_lo_y = sm.lo_y;
            mesh.bbox_hi_x = hi_x;
            mesh.bbox_hi_y = hi_y;
            any_bbox = true;
        } else {
            mesh.bbox_lo_x = std::min(mesh.bbox_lo_x, sm.lo_x);
            mesh.bbox_lo_y = std::min(mesh.bbox_lo_y, sm.lo_y);
            mesh.bbox_hi_x = std::max(mesh.bbox_hi_x, hi_x);
            mesh.bbox_hi_y = std::max(mesh.bbox_hi_y, hi_y);
        }
    }

    // Do not early-return when the zone mesh is empty; the track-based
    // fallback at the end may still produce a usable network. Skip just
    // the via-wiring + pad-attachment block instead.
    const bool zone_mesh_empty = mesh.nodes.empty();

    if (!zone_mesh_empty) {

    // Via wiring: for each via on the target net whose from/to layers are
    // both in our meshed set, add a via-resistor between nearest nodes on
    // each side. Via barrel resistance R = rho * L / A where L is the board
    // thickness between the two layers (approximated as total_thickness for
    // through-vias) and A = pi * (drill/2)^2.
    if (submeshes.size() >= 2) {
        for (const auto& via : board.vias) {
            if (via.net_id != cfg.net_id) continue;
            if (via.drill <= 0.0) continue;

            const LayerSubmesh* from_sm = nullptr;
            const LayerSubmesh* to_sm = nullptr;
            for (const auto& sm : submeshes) {
                if (sm.layer_ordinal == via.from_layer) from_sm = &sm;
                if (sm.layer_ordinal == via.to_layer)   to_sm   = &sm;
            }
            if (!from_sm || !to_sm || from_sm == to_sm) continue;

            const int a = nearest_node_on_layer(mesh, via.at.x, via.at.y,
                                                from_sm->layer_ordinal);
            const int b = nearest_node_on_layer(mesh, via.at.x, via.at.y,
                                                to_sm->layer_ordinal);
            if (a < 0 || b < 0) continue;

            const double L = board.stackup.total_thickness;
            const double r = 0.5 * via.drill;
            const double area = std::numbers::pi * r * r;
            if (area <= 0.0 || L <= 0.0) continue;
            const double R = cfg.copper_rho * L / area;
            const double G = 1.0 / R;
            mesh.resistors.push_back({a, b, G});
        }
    }

    // Source/sink: leftmost vs rightmost pad on (net, layer). Match the pad to
    // the closest in-copper node by Euclidean distance.
    auto nearest_node = [&mesh](double px, double py) -> int {
        int best = -1;
        double best_d2 = std::numeric_limits<double>::infinity();
        for (const auto& n : mesh.nodes) {
            const double dx = n.x - px;
            const double dy = n.y - py;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best = n.id;
            }
        }
        return best;
    };

    auto pad_on_target = [&](const circuitcore::board::Pad& p) {
        if (p.net_id != cfg.net_id) return false;
        for (int o : p.layer_ordinals) {
            if (o == cfg.layer_ordinal) return true;
        }
        return false;
    };
    auto name_in = [](const std::vector<std::string>& names,
                       const std::string& n) {
        for (const auto& s : names) {
            if (s == n) return true;
        }
        return false;
    };

    // Resolve a pad to the set of node IDs it attaches to. Edge-contact:
    // every mesh node inside the pad footprint receives an equal share of
    // the pad's current. Falls back to nearest_node when the pad is smaller
    // than the cell or covers no cells at all.
    auto pad_nodes = [&](const circuitcore::board::Pad& pad) -> std::vector<int> {
        auto nodes = nodes_under_pad(mesh, pad, primary_layer);
        if (!nodes.empty()) return nodes;
        const int nid = nearest_node(pad.at.x, pad.at.y);
        if (nid >= 0) return {nid};
        return {};
    };

    const bool explicit_src = !cfg.source_pad_names.empty();
    const bool explicit_snk = !cfg.sink_pad_names.empty();
    const bool explicit_src_idx = !cfg.source_pad_indices.empty();
    const bool explicit_snk_idx = !cfg.sink_pad_indices.empty();

    auto idx_in = [](const std::vector<int>& v, int i) {
        return std::find(v.begin(), v.end(), i) != v.end();
    };

    if (explicit_src || explicit_snk ||
        explicit_src_idx || explicit_snk_idx) {
        for (std::size_t i = 0; i < board.pads.size(); ++i) {
            const auto& pad = board.pads[i];
            if (!pad_on_target(pad)) continue;
            auto nodes = pad_nodes(pad);
            if (nodes.empty()) continue;
            const int ii = static_cast<int>(i);
            if ((explicit_src && name_in(cfg.source_pad_names, pad.name)) ||
                (explicit_src_idx && idx_in(cfg.source_pad_indices, ii))) {
                for (int nid : nodes) mesh.source_node_ids.push_back(nid);
            }
            if ((explicit_snk && name_in(cfg.sink_pad_names, pad.name)) ||
                (explicit_snk_idx && idx_in(cfg.sink_pad_indices, ii))) {
                for (int nid : nodes) mesh.sink_node_ids.push_back(nid);
            }
        }
    }

    // If per-pad currents are specified, distribute each pad's current
    // equally across the nodes it covers (edge-contact). Keyed by index
    // into board.pads so duplicated pad names (every footprint has a
    // pin "1") don't collapse into one bucket.
    if (!cfg.pad_currents.empty()) {
        for (std::size_t pi = 0; pi < board.pads.size(); ++pi) {
            const auto& pad = board.pads[pi];
            if (!pad_on_target(pad)) continue;
            auto it = cfg.pad_currents.find(static_cast<int>(pi));
            if (it == cfg.pad_currents.end()) continue;
            auto nodes = pad_nodes(pad);
            if (nodes.empty()) continue;
            const double per_node = it->second / static_cast<double>(nodes.size());
            for (int nid : nodes) mesh.node_currents.emplace_back(nid, per_node);
        }
    }

    // Auto-fill anything still missing with leftmost / rightmost pad
    // (edge-contact node lists).
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        const circuitcore::board::Pad* src = nullptr;
        const circuitcore::board::Pad* snk = nullptr;
        for (const auto& pad : board.pads) {
            if (!pad_on_target(pad)) continue;
            if (!src || pad.at.x < src->at.x) src = &pad;
            if (!snk || pad.at.x > snk->at.x) snk = &pad;
        }
        if (mesh.source_node_ids.empty() && src && !mesh.nodes.empty()) {
            for (int nid : pad_nodes(*src)) mesh.source_node_ids.push_back(nid);
        }
        if (mesh.sink_node_ids.empty() && snk && snk != src && !mesh.nodes.empty()) {
            for (int nid : pad_nodes(*snk)) mesh.sink_node_ids.push_back(nid);
        }
    }

    }  // end of if (!zone_mesh_empty) {

    if (!mesh.nodes.empty()) mesh.primary_layer_used = primary_layer;

    // Track-based fallback: if no zone-based geometry came back, try
    // building a 1D resistor graph from track segments on the chosen layer.
    // Common on boards that route power via traces instead of pours.
    if (mesh.nodes.empty()) {
        mesh = build_track_mesh(board, cfg, primary_layer);
        if (!mesh.nodes.empty()) mesh.primary_layer_used = primary_layer;
    }

    prune_disconnected(mesh);
    if (mesh.nodes.empty()) mesh.primary_layer_used = -1;
    return mesh;
}

}  // namespace pdnkit::pi
