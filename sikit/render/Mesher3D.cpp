#include "render/Mesher3D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>

#include "circuitcore/ui/LayerColors.h"

namespace sikit::render {

namespace {

constexpr double kPi = std::numbers::pi;

struct Color { float r, g, b, a; };

// FR-4-ish amber/green translucent fill for unspecified dielectric.
constexpr Color kDielectricColor{0.55f, 0.50f, 0.18f, 0.28f};

constexpr double kDefaultCopperThickness = 35.0e-6;  // 1 oz copper ≈ 35 μm
constexpr double kDefaultBoardThickness  = 1.6e-3;

void push_vertex(Mesh3D& m,
                 double x, double y, double z,
                 double nx, double ny, double nz,
                 Color c) {
    m.vertices.push_back(static_cast<float>(x));
    m.vertices.push_back(static_cast<float>(y));
    m.vertices.push_back(static_cast<float>(z));
    m.vertices.push_back(static_cast<float>(nx));
    m.vertices.push_back(static_cast<float>(ny));
    m.vertices.push_back(static_cast<float>(nz));
    m.vertices.push_back(c.r);
    m.vertices.push_back(c.g);
    m.vertices.push_back(c.b);
    m.vertices.push_back(c.a);
}

std::uint32_t vert_count(const Mesh3D& m) {
    return static_cast<std::uint32_t>(m.vertices.size() / 10);
}

// Append an axis-aligned box to the mesh. Six faces, four vertices each,
// distinct normals → 24 vertices, 36 indices.
void append_aabb(Mesh3D& m,
                  double x_lo, double x_hi,
                  double y_lo, double y_hi,
                  double z_lo, double z_hi,
                  Color c) {
    struct Face {
        double nx, ny, nz;
        std::array<std::array<int, 3>, 4> corners;  // each (xi, yi, zi) in {0,1}
    };
    // Corner indices: x = (lo, hi)[xi], etc. CCW from outside the box.
    const Face faces[6] = {
        // +Z (top)
        {0, 0,  1, {{ {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} }}},
        // -Z (bottom)
        {0, 0, -1, {{ {0,1,0}, {1,1,0}, {1,0,0}, {0,0,0} }}},
        // +X
        { 1, 0, 0, {{ {1,0,0}, {1,1,0}, {1,1,1}, {1,0,1} }}},
        // -X
        {-1, 0, 0, {{ {0,1,0}, {0,0,0}, {0,0,1}, {0,1,1} }}},
        // +Y
        {0,  1, 0, {{ {1,1,0}, {0,1,0}, {0,1,1}, {1,1,1} }}},
        // -Y
        {0, -1, 0, {{ {0,0,0}, {1,0,0}, {1,0,1}, {0,0,1} }}},
    };
    const double xs[2] = {x_lo, x_hi};
    const double ys[2] = {y_lo, y_hi};
    const double zs[2] = {z_lo, z_hi};

    for (const auto& f : faces) {
        const auto base = vert_count(m);
        for (const auto& cn : f.corners) {
            push_vertex(m, xs[cn[0]], ys[cn[1]], zs[cn[2]],
                        f.nx, f.ny, f.nz, c);
        }
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
    }
}

// Extrude a track segment into a box in Z. The segment defines a rectangle
// in XY (width perpendicular to start→end direction); we lift it to a
// box spanning z_lo..z_hi. Side normals point outward in XY; top/bottom
// normals are ±Z. 24 vertices, 36 indices per segment.
void append_segment_box(Mesh3D& m, const circuitcore::board::Segment& s,
                        double z_lo, double z_hi, Color c) {
    const double dx = s.end.x - s.start.x;
    const double dy = s.end.y - s.start.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0 || s.width <= 0.0) return;
    const double tx = dx / len;
    const double ty = dy / len;
    const double nx = -ty;        // unit perpendicular (left of direction)
    const double ny =  tx;
    const double hw = 0.5 * s.width;

    // Four XY corners of the trace rectangle, in CCW order viewed from +Z.
    // P1 = start + n*hw, P2 = end + n*hw, P3 = end - n*hw, P4 = start - n*hw.
    const double p1x = s.start.x + nx * hw, p1y = s.start.y + ny * hw;
    const double p2x = s.end.x   + nx * hw, p2y = s.end.y   + ny * hw;
    const double p3x = s.end.x   - nx * hw, p3y = s.end.y   - ny * hw;
    const double p4x = s.start.x - nx * hw, p4y = s.start.y - ny * hw;

    // Top face (+Z): corners CCW viewed from above.
    {
        const auto base = vert_count(m);
        push_vertex(m, p1x, p1y, z_hi, 0, 0,  1, c);
        push_vertex(m, p2x, p2y, z_hi, 0, 0,  1, c);
        push_vertex(m, p3x, p3y, z_hi, 0, 0,  1, c);
        push_vertex(m, p4x, p4y, z_hi, 0, 0,  1, c);
        m.indices.insert(m.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    // Bottom face (-Z): reversed winding.
    {
        const auto base = vert_count(m);
        push_vertex(m, p4x, p4y, z_lo, 0, 0, -1, c);
        push_vertex(m, p3x, p3y, z_lo, 0, 0, -1, c);
        push_vertex(m, p2x, p2y, z_lo, 0, 0, -1, c);
        push_vertex(m, p1x, p1y, z_lo, 0, 0, -1, c);
        m.indices.insert(m.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    // Four side walls. Outward normals point perpendicular to the edge.
    auto side = [&](double ax, double ay, double bx, double by,
                    double nxv, double nyv) {
        const auto base = vert_count(m);
        push_vertex(m, ax, ay, z_lo, nxv, nyv, 0, c);
        push_vertex(m, bx, by, z_lo, nxv, nyv, 0, c);
        push_vertex(m, bx, by, z_hi, nxv, nyv, 0, c);
        push_vertex(m, ax, ay, z_hi, nxv, nyv, 0, c);
        m.indices.insert(m.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    side(p1x, p1y, p2x, p2y,  nx,  ny);   // +n side
    side(p3x, p3y, p4x, p4y, -nx, -ny);   // -n side
    side(p2x, p2y, p3x, p3y,  tx,  ty);   // +t (end) side
    side(p4x, p4y, p1x, p1y, -tx, -ty);   // -t (start) side
}

// Closed cylinder (top + bottom caps + side wall) along Z.
// sides ≥ 6; default 24 gives a smooth-looking barrel at via scale.
void append_cylinder(Mesh3D& m, double cx, double cy, double radius,
                      double z_lo, double z_hi, Color c, int sides = 24) {
    if (radius <= 0.0 || sides < 3) return;
    if (z_hi <= z_lo) return;

    // Side wall: 2 triangles per face, distinct normals per face.
    for (int i = 0; i < sides; ++i) {
        const double a1 = 2.0 * kPi * i / sides;
        const double a2 = 2.0 * kPi * (i + 1) / sides;
        const double n1x = std::cos(a1), n1y = std::sin(a1);
        const double n2x = std::cos(a2), n2y = std::sin(a2);
        const double x1 = cx + radius * n1x, y1 = cy + radius * n1y;
        const double x2 = cx + radius * n2x, y2 = cy + radius * n2y;

        const auto base = vert_count(m);
        push_vertex(m, x1, y1, z_lo, n1x, n1y, 0, c);
        push_vertex(m, x2, y2, z_lo, n2x, n2y, 0, c);
        push_vertex(m, x2, y2, z_hi, n2x, n2y, 0, c);
        push_vertex(m, x1, y1, z_hi, n1x, n1y, 0, c);
        m.indices.insert(m.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    // Top cap fan.
    {
        const auto base = vert_count(m);
        push_vertex(m, cx, cy, z_hi, 0, 0, 1, c);
        for (int i = 0; i < sides; ++i) {
            const double a = 2.0 * kPi * i / sides;
            push_vertex(m, cx + radius * std::cos(a), cy + radius * std::sin(a),
                        z_hi, 0, 0, 1, c);
        }
        for (int i = 0; i < sides; ++i) {
            const int next = (i + 1) % sides;
            m.indices.push_back(base);
            m.indices.push_back(base + 1 + static_cast<std::uint32_t>(i));
            m.indices.push_back(base + 1 + static_cast<std::uint32_t>(next));
        }
    }
    // Bottom cap fan (reverse winding so the normal points -Z).
    {
        const auto base = vert_count(m);
        push_vertex(m, cx, cy, z_lo, 0, 0, -1, c);
        for (int i = 0; i < sides; ++i) {
            const double a = 2.0 * kPi * i / sides;
            push_vertex(m, cx + radius * std::cos(a), cy + radius * std::sin(a),
                        z_lo, 0, 0, -1, c);
        }
        for (int i = 0; i < sides; ++i) {
            const int next = (i + 1) % sides;
            m.indices.push_back(base);
            m.indices.push_back(base + 1 + static_cast<std::uint32_t>(next));
            m.indices.push_back(base + 1 + static_cast<std::uint32_t>(i));
        }
    }
}

struct LayerZ {
    double z_center = 0.0;
    double thickness = kDefaultCopperThickness;
};

struct StackupZMap {
    // copper-layer-name → (z_center, thickness)
    std::unordered_map<std::string, LayerZ> by_name;
    // Dielectric slabs as (z_lo, z_hi) ranges.
    std::vector<std::pair<double, double>> dielectric_slabs;
    double board_z_lo = 0.0;
    double board_z_hi = kDefaultBoardThickness;
};

// Build a Z map from the board's explicit stackup, or synthesize one when
// the board file didn't carry a (setup (stackup ...)) block.
StackupZMap compute_stackup_z(const circuitcore::board::Board& board, const sikit::si::SiStackup& si_stackup) {
    StackupZMap out;
    const auto& stk = board.stackup;

    if (!si_stackup.items.empty()) {
        // Items are top-to-bottom. Walk from z_top downward.
        double z_top = stk.total_thickness;
        if (z_top <= 0.0) {
            double sum = 0.0;
            for (const auto& it : si_stackup.items) sum += it.thickness;
            z_top = sum > 0.0 ? sum : kDefaultBoardThickness;
        }
        out.board_z_hi = z_top;
        out.board_z_lo = 0.0;

        double z = z_top;
        for (const auto& it : si_stackup.items) {
            const double t = it.thickness > 0.0
                ? it.thickness
                : (it.kind == sikit::si::SiStackupItem::Kind::Copper
                       ? kDefaultCopperThickness
                       : 0.0);
            if (it.kind == sikit::si::SiStackupItem::Kind::Copper) {
                LayerZ lz;
                lz.z_center = z - 0.5 * t;
                lz.thickness = t;
                out.by_name[it.name] = lz;
            } else if (it.kind == sikit::si::SiStackupItem::Kind::Dielectric) {
                out.dielectric_slabs.emplace_back(z - t, z);
            }
            z -= t;
        }
        return out;
    }

    // No stackup items — synthesise one. Use total_thickness (or default)
    // for the board height, and lay all copper layers evenly between top
    // and bottom by their stackup-table position.
    const double total = stk.total_thickness > 0.0
        ? stk.total_thickness
        : kDefaultBoardThickness;
    out.board_z_lo = 0.0;
    out.board_z_hi = total;
    out.dielectric_slabs.emplace_back(0.0, total);

    std::vector<int> copper_ords;
    for (const auto& l : stk.layers) {
        if (l.is_copper()) copper_ords.push_back(l.ordinal);
    }
    // Heuristic order: F.Cu (0) on top, B.Cu (31) on bottom, others between
    // by ordinal. Ordinal-sorted descending so 0 → top, 31 → bottom.
    std::sort(copper_ords.begin(), copper_ords.end(),
              [](int a, int b) { return a < b; });
    // For a 2-layer board: F.Cu just under z=total, B.Cu just above z=0.
    // For more layers: distribute centres evenly between two thin offsets.
    const std::size_t n = copper_ords.size();
    if (n == 0) return out;
    const double inset = 0.5 * kDefaultCopperThickness;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.5
                                  : 1.0 - static_cast<double>(i) / (n - 1);
        const double z_center = inset + t * (total - 2 * inset);
        // Need to find the layer by ordinal → name; fall back to ordinal-keyed
        // string if no layer entry exists.
        const auto* layer = board.find_layer(copper_ords[i]);
        std::string key = layer ? layer->name : std::to_string(copper_ords[i]);
        LayerZ lz;
        lz.z_center = z_center;
        lz.thickness = kDefaultCopperThickness;
        out.by_name[key] = lz;
    }
    return out;
}

// Find Z for a copper segment / via layer ordinal. Returns nullopt if the
// layer ordinal doesn't resolve to a known copper layer.
std::optional<LayerZ> z_for_layer(const circuitcore::board::Board& board,
                                    const StackupZMap& zmap,
                                    int ordinal) {
    const auto* layer = board.find_layer(ordinal);
    if (layer) {
        auto it = zmap.by_name.find(layer->name);
        if (it != zmap.by_name.end()) return it->second;
    }
    // Fall back to numeric key (used by the synthesised path when no layer
    // name was available).
    auto it = zmap.by_name.find(std::to_string(ordinal));
    if (it != zmap.by_name.end()) return it->second;
    return std::nullopt;
}

// Board XY bounding box: union of all segment endpoints, pads, vias, and
// zone outlines. Returns ((0,0),(0,0)) if the board is empty.
struct XYBounds { double x_lo, y_lo, x_hi, y_hi; bool any; };

XYBounds board_xy_bounds(const circuitcore::board::Board& board) {
    XYBounds b{0, 0, 0, 0, false};
    auto include = [&](double x, double y) {
        if (!b.any) { b.x_lo = b.x_hi = x; b.y_lo = b.y_hi = y; b.any = true; }
        else {
            b.x_lo = std::min(b.x_lo, x); b.x_hi = std::max(b.x_hi, x);
            b.y_lo = std::min(b.y_lo, y); b.y_hi = std::max(b.y_hi, y);
        }
    };
    for (const auto& s : board.segments) {
        include(s.start.x, s.start.y);
        include(s.end.x,   s.end.y);
    }
    for (const auto& p : board.pads) include(p.at.x, p.at.y);
    for (const auto& v : board.vias) include(v.at.x, v.at.y);
    for (const auto& z : board.zones) {
        for (const auto& pt : z.outline.outline) include(pt.x, pt.y);
    }
    return b;
}

}  // namespace

BoardMesh3D build_board_mesh_3d(const circuitcore::board::Board& board,
                                 const sikit::si::SiStackup& si_stackup) {
    BoardMesh3D out;
    const auto zmap = compute_stackup_z(board, si_stackup);
    const auto bb = board_xy_bounds(board);
    if (!bb.any) return out;

    // Dielectric slabs span the full board XY bounding box (with a small
    // outward margin so copper sitting flush at the board edge doesn't
    // z-fight with the slab face).
    const double margin = 0.0005;  // 0.5 mm
    const double x_lo = bb.x_lo - margin, x_hi = bb.x_hi + margin;
    const double y_lo = bb.y_lo - margin, y_hi = bb.y_hi + margin;
    for (const auto& [zl, zh] : zmap.dielectric_slabs) {
        if (zh <= zl) continue;
        append_aabb(out.dielectric, x_lo, x_hi, y_lo, y_hi, zl, zh,
                    kDielectricColor);
    }

    // Copper traces. Per-layer color via the existing LayerColors palette,
    // with alpha forced to 1 so traces read as solid metal under the
    // translucent dielectric.
    for (const auto& s : board.segments) {
        const auto lz = z_for_layer(board, zmap, s.layer_ordinal);
        if (!lz) continue;
        const double z_lo = lz->z_center - 0.5 * lz->thickness;
        const double z_hi = lz->z_center + 0.5 * lz->thickness;
        const auto rgba = circuitcore::ui::layer_color(s.layer_ordinal);
        const Color c{rgba[0], rgba[1], rgba[2], 1.0f};
        append_segment_box(out.copper, s, z_lo, z_hi, c);
    }

    // Vias: barrel spans from the highest copper Z in [from..to] down to
    // the lowest one, using drill diameter (the copper-plated barrel) so
    // the cylinder reads as the conductive path, not the larger annular
    // pad. Falls back to outer_diameter * 0.6 if drill wasn't recorded.
    for (const auto& v : board.vias) {
        double z_hi = -1e30, z_lo = 1e30;
        bool found = false;
        const int o_lo = std::min(v.from_layer, v.to_layer);
        const int o_hi = std::max(v.from_layer, v.to_layer);
        for (int ord = o_lo; ord <= o_hi; ++ord) {
            const auto lz = z_for_layer(board, zmap, ord);
            if (!lz) continue;
            z_hi = std::max(z_hi, lz->z_center + 0.5 * lz->thickness);
            z_lo = std::min(z_lo, lz->z_center - 0.5 * lz->thickness);
            found = true;
        }
        if (!found || z_hi <= z_lo) continue;
        const double radius = v.drill > 0.0
            ? 0.5 * v.drill
            : 0.30 * v.outer_diameter;
        if (radius <= 0.0) continue;
        // Bright copper-ish color so vias pop against dielectric.
        constexpr Color kViaColor{0.85f, 0.55f, 0.20f, 1.0f};
        append_cylinder(out.vias, v.at.x, v.at.y, radius,
                         z_lo, z_hi, kViaColor);
    }


    // Pads: walk every pad and stamp its shape onto each copper layer
    // it sits on. Through-hole pads (multiple copper-layer ordinals)
    // get stamped on each so they visibly span top->bottom; v0 doesn't
    // synthesise an explicit barrel for them.
    for (const auto& pd : board.pads) {
        if (pd.layer_ordinals.empty()) continue;
        const double hw = 0.5 * pd.size.x;
        const double hh = 0.5 * pd.size.y;
        const bool have_size = (pd.size.x > 0.0 && pd.size.y > 0.0);
        for (int ord : pd.layer_ordinals) {
            const auto* L = board.find_layer(ord);
            if (!L || !L->is_copper()) continue;
            const auto lz = z_for_layer(board, zmap, ord);
            if (!lz) continue;
            const double z_lo = lz->z_center - 0.5 * lz->thickness;
            const double z_hi = lz->z_center + 0.5 * lz->thickness;
            const auto rgba = circuitcore::ui::layer_color(ord);
            const Color c{rgba[0], rgba[1], rgba[2], 1.0f};

            switch (pd.shape) {
                case circuitcore::board::PadShape::Circle: {
                    const double r = have_size
                        ? std::max(hw, hh)  // size.x ~= size.y for round
                        : 0.50e-3;
                    append_cylinder(out.copper, pd.at.x, pd.at.y, r,
                                     z_lo, z_hi, c);
                    break;
                }
                case circuitcore::board::PadShape::Rect:
                case circuitcore::board::PadShape::Oval:
                case circuitcore::board::PadShape::RoundRect:
                case circuitcore::board::PadShape::Custom: {
                    // v1: render every non-circular pad as a flat box.
                    // Oval / round-rect corner radii are a future
                    // refinement -- the volumetric shape is close enough
                    // for the thermal + EM read.
                    if (have_size) {
                        append_aabb(out.copper,
                                     pd.at.x - hw, pd.at.x + hw,
                                     pd.at.y - hh, pd.at.y + hh,
                                     z_lo, z_hi, c);
                    } else {
                        append_cylinder(out.copper, pd.at.x, pd.at.y,
                                         0.50e-3, z_lo, z_hi, c);
                    }
                    break;
                }
            }
        }
    }

    return out;
}

}  // namespace sikit::render
