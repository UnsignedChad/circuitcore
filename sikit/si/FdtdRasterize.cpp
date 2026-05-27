// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/FdtdRasterize.h"
#include "circuitcore/board/Bounds.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sikit::fdtd {

namespace {

// Use the canonical board::Bounds2 throughout; alias the old name so
// the surrounding code keeps reading naturally. board_bbox here returns
// the union over segments / vias / outline zones only (skips pads etc)
// since this rasteriser centres on PDN copper, not pad lands.
using BBox = circuitcore::board::Bounds2;
inline BBox board_bbox(const circuitcore::board::Board& b) {
    BBox bb;
    for (const auto& s : b.segments) {
        bb.include(s.start.x, s.start.y);
        bb.include(s.end.x,   s.end.y);
    }
    for (const auto& v : b.vias) bb.include(v.at.x, v.at.y);
    for (const auto& z : b.zones) {
        for (const auto& p : z.outline.outline) bb.include(p.x, p.y);
    }
    return bb;
}

int to_idx(double v, double origin, double d) {
    return static_cast<int>(std::floor((v - origin) / d + 0.5));
}

// Standard ray-cast point-in-polygon. Duplicated from ReturnPath /
// EyeMask so the rasteriser doesn't pull a dep on either module.
bool point_in_polygon(double x, double y,
                       const std::vector<circuitcore::board::Point2>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].x, yi = poly[i].y;
        const double xj = poly[j].x, yj = poly[j].y;
        const bool cross = ((yi > y) != (yj > y)) &&
                            (x < (xj - xi) * (y - yi) / (yj - yi + 1e-30) + xi);
        if (cross) inside = !inside;
    }
    return inside;
}

}  // namespace

RasterMapping make_default_mapping(
    const circuitcore::board::Board& board) {
    RasterMapping m;
    const auto bb = board_bbox(board);
    m.origin_x = !bb.valid ? 0.0 : bb.lo_x;
    m.origin_y = !bb.valid ? 0.0 : bb.lo_y;
    m.origin_z = 0.0;

    // Walk the stackup top-down and stash each copper layer's top-z.
    // Non-copper items advance z; we don't store their position because
    // the substrate cells get filled implicitly between two copper
    // layers in rasterize_board.
    double z = 0.0;
    for (const auto& L : board.stackup.layers) {
        if (L.is_copper()) {
            m.layer_z[L.ordinal] = z;
        }
        // Move down by the layer's own thickness (0 for layers with no
        // recorded thickness; not great but pragmatic given KiCad's
        // historic lack of detail on missing stackup info).
        z += L.thickness;
    }
    return m;
}

std::size_t rasterize_segment(FDTD3D& s,
                                const circuitcore::board::Segment& seg,
                                const RasterMapping& m) {
    auto it = m.layer_z.find(seg.layer_ordinal);
    if (it == m.layer_z.end()) return 0;  // not a copper layer we know
    const double dx = s.grid().dx;
    const double dy = s.grid().dy;
    const double dz = s.grid().dz;
    const double z  = it->second;
    const int kz    = to_idx(z, m.origin_z, dz);
    if (kz < 0 || kz >= s.grid().nz) return 0;

    // Walk the segment in tiny steps (half a cell). For each sample
    // point, mark a square footprint of "width" around the centreline,
    // perpendicular to the segment direction, as PEC.
    const double sx = seg.end.x - seg.start.x;
    const double sy = seg.end.y - seg.start.y;
    const double L = std::sqrt(sx * sx + sy * sy);
    if (L <= 0.0) return 0;
    const double tx = sx / L, ty = sy / L;
    const double nx = -ty, ny = tx;  // perpendicular unit vector
    const double step = 0.5 * std::min(dx, dy);
    const int n_steps = static_cast<int>(std::ceil(L / step)) + 1;
    const int half_w = static_cast<int>(
        std::ceil(0.5 * seg.width / std::min(dx, dy)));

    std::size_t marked = 0;
    for (int n = 0; n <= n_steps; ++n) {
        const double t = static_cast<double>(n) / n_steps;
        const double cx = seg.start.x + t * sx;
        const double cy = seg.start.y + t * sy;
        for (int u = -half_w; u <= half_w; ++u) {
            const double px = cx + u * dx * nx;
            const double py = cy + u * dy * ny;
            const int i = to_idx(px, m.origin_x, dx);
            const int j = to_idx(py, m.origin_y, dy);
            if (i < 0 || i >= s.grid().nx) continue;
            if (j < 0 || j >= s.grid().ny) continue;
            // Mark the single Yee cell as PEC for all three E
            // components. The mask is per-component so the rasteriser
            // could be cleverer about which components to mark; the current
            // marks all three for simplicity.
            s.mark_pec_box(i, j, kz, i, j, kz);
            ++marked;
        }
    }
    return marked;
}

std::size_t rasterize_zone(FDTD3D& s,
                              const circuitcore::board::Zone& z,
                              const RasterMapping& m) {
    auto it = m.layer_z.find(z.layer_ordinal);
    if (it == m.layer_z.end()) return 0;
    const double dx = s.grid().dx;
    const double dy = s.grid().dy;
    const double dz = s.grid().dz;
    const double zc = it->second;
    const int kz    = to_idx(zc, m.origin_z, dz);
    if (kz < 0 || kz >= s.grid().nz) return 0;

    // Use filled polygons if the parser populated them; else fall back
    // to the user-drawn outline.
    const auto& polys = !z.filled.empty()
                         ? z.filled
                         : std::vector{z.outline};

    std::size_t marked = 0;
    for (const auto& poly : polys) {
        if (poly.outline.size() < 3) continue;
        // Bounding box of the polygon to bound the scan.
        BBox bb;
        for (const auto& p : poly.outline) bb.include(p.x, p.y);
        const int i0 = std::max(0,
            to_idx(bb.lo_x, m.origin_x, dx));
        const int i1 = std::min(s.grid().nx - 1,
            to_idx(bb.hi_x, m.origin_x, dx));
        const int j0 = std::max(0,
            to_idx(bb.lo_y, m.origin_y, dy));
        const int j1 = std::min(s.grid().ny - 1,
            to_idx(bb.hi_y, m.origin_y, dy));
        for (int j = j0; j <= j1; ++j) {
            const double py = m.origin_y + j * dy;
            for (int i = i0; i <= i1; ++i) {
                const double px = m.origin_x + i * dx;
                if (point_in_polygon(px, py, poly.outline)) {
                    s.mark_pec_box(i, j, kz, i, j, kz);
                    ++marked;
                }
            }
        }
    }
    return marked;
}

std::size_t rasterize_via(FDTD3D& s,
                            const circuitcore::board::Via& v,
                            const RasterMapping& m) {
    const double dx = s.grid().dx;
    const double dy = s.grid().dy;
    const double dz = s.grid().dz;
    auto it_from = m.layer_z.find(v.from_layer);
    auto it_to   = m.layer_z.find(v.to_layer);
    if (it_from == m.layer_z.end() || it_to == m.layer_z.end()) return 0;
    const double z0 = std::min(it_from->second, it_to->second);
    const double z1 = std::max(it_from->second, it_to->second);
    const int k0    = std::max(0, to_idx(z0, m.origin_z, dz));
    const int k1    = std::min(s.grid().nz - 1,
                                  to_idx(z1, m.origin_z, dz));
    if (k0 > k1) return 0;
    // Use the pad outer diameter as the cross-section; fall back to
    // drill if outer wasn't recorded.
    const double d = v.outer_diameter > 0 ? v.outer_diameter : v.drill;
    if (d <= 0.0) return 0;
    const int half = static_cast<int>(
        std::ceil(0.5 * d / std::min(dx, dy)));
    const int ic = to_idx(v.at.x, m.origin_x, dx);
    const int jc = to_idx(v.at.y, m.origin_y, dy);
    const double r2 = (0.5 * d) * (0.5 * d);

    std::size_t marked = 0;
    for (int dj = -half; dj <= half; ++dj) {
        for (int di = -half; di <= half; ++di) {
            const double px = (ic + di) * dx + m.origin_x;
            const double py = (jc + dj) * dy + m.origin_y;
            const double rx = px - v.at.x;
            const double ry = py - v.at.y;
            if (rx * rx + ry * ry > r2) continue;
            const int ii = ic + di;
            const int jj = jc + dj;
            if (ii < 0 || ii >= s.grid().nx) continue;
            if (jj < 0 || jj >= s.grid().ny) continue;
            s.mark_pec_box(ii, jj, k0, ii, jj, k1);
            marked += (k1 - k0 + 1);
        }
    }
    return marked;
}

RasterReport rasterize_board(FDTD3D& s,
                                const circuitcore::board::Board& board,
                                const RasterMapping& m) {
    RasterReport r;
    for (const auto& seg : board.segments) {
        r.segment_pec_cells += rasterize_segment(s, seg, m);
        ++r.n_segments_processed;
    }
    for (const auto& z : board.zones) {
        r.zone_pec_cells += rasterize_zone(s, z, m);
        ++r.n_zones_processed;
    }
    for (const auto& v : board.vias) {
        r.via_pec_cells += rasterize_via(s, v, m);
        ++r.n_vias_processed;
    }

    // Substrate: between every pair of adjacent copper layers in the
    // mapping, set the substrate eps_r from whatever non-copper layer
    // we find between them in the stackup. If no eps_r is recorded,
    // default to FR-4 (4.3).
    if (m.layer_z.size() < 2) return r;
    auto it = m.layer_z.begin();
    auto prev = it++;
    for (; it != m.layer_z.end(); ++it) {
        const double z_top    = prev->second;
        const double z_bottom = it->second;
        const int k_top = std::max(0,
            to_idx(z_top, m.origin_z, s.grid().dz));
        const int k_bot = std::min(s.grid().nz - 1,
            to_idx(z_bottom, m.origin_z, s.grid().dz));
        if (k_bot < k_top) { prev = it; continue; }

        // Find the dielectric eps_r between these two copper layers.
        double er = 4.3;
        bool found = false;
        for (const auto& L : board.stackup.layers) {
            if (L.is_copper()) continue;
            if (L.ordinal > prev->first && L.ordinal < it->first &&
                L.epsilon_r > 0) {
                er = L.epsilon_r;
                found = true;
                break;
            }
        }
        (void)found;  // 4.3 default is fine when not declared
        s.set_material_box(0, 0, k_top + 1,
                            s.grid().nx - 1, s.grid().ny - 1, k_bot - 1,
                            er, 0.0);
        r.substrate_cells += static_cast<std::size_t>(s.grid().nx)
                            * s.grid().ny
                            * std::max(0, k_bot - k_top - 1);
        prev = it;
    }
    return r;
}

}  // namespace sikit::fdtd
