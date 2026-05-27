#include "mp/Voxelizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace mpkit {

namespace {

// Sum of all known layer thicknesses (m). Falls back to the default
// 1.6 mm if the parser did not populate thickness fields.
double total_stack_thickness(const circuitcore::board::Stackup& s) {
    double t = 0.0;
    for (const auto& L : s.layers) t += L.thickness;
    if (t <= 0.0) return s.total_thickness > 0.0 ? s.total_thickness : 1.6e-3;
    return t;
}

// World-space bbox of every copper feature on the board. Falls back to
// the outline if no copper is present. Returns false if neither yields
// finite extents.
bool board_bbox(const circuitcore::board::Board& b,
                double& xmin, double& ymin, double& xmax, double& ymax) {
    xmin = ymin =  std::numeric_limits<double>::infinity();
    xmax = ymax = -std::numeric_limits<double>::infinity();
    auto take = [&](double x, double y) {
        xmin = std::min(xmin, x); ymin = std::min(ymin, y);
        xmax = std::max(xmax, x); ymax = std::max(ymax, y);
    };
    for (const auto& s : b.segments) {
        const double r = 0.5 * s.width;
        take(s.start.x - r, s.start.y - r);
        take(s.start.x + r, s.start.y + r);
        take(s.end.x   - r, s.end.y   - r);
        take(s.end.x   + r, s.end.y   + r);
    }
    for (const auto& v : b.vias) {
        const double r = 0.5 * v.outer_diameter;
        take(v.at.x - r, v.at.y - r);
        take(v.at.x + r, v.at.y + r);
    }
    for (const auto& seg : b.outline) {
        take(seg.start.x, seg.start.y);
        take(seg.end.x, seg.end.y);
    }
    return std::isfinite(xmin) && std::isfinite(xmax);
}

// Rasterize the swept disk left by a segment of width w from a to b
// into the (i, j) cells of `out` on the supplied k layer.
void stamp_segment(VoxelMaterialField& out, int k,
                   double ax, double ay, double bx, double by, double w,
                   MaterialId id) {
    const auto& g = out.grid;
    const double r = 0.5 * w;
    const double xlo = std::min(ax, bx) - r;
    const double xhi = std::max(ax, bx) + r;
    const double ylo = std::min(ay, by) - r;
    const double yhi = std::max(ay, by) + r;
    const int ilo = std::max(0, static_cast<int>((xlo - g.x0) / g.dx()));
    const int ihi = std::min(g.nx() - 1, static_cast<int>((xhi - g.x0) / g.dx()));
    const int jlo = std::max(0, static_cast<int>((ylo - g.y0) / g.dy()));
    const int jhi = std::min(g.ny() - 1, static_cast<int>((yhi - g.y0) / g.dy()));
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    for (int j = jlo; j <= jhi; ++j) {
        const double cy = g.cy(j);
        for (int i = ilo; i <= ihi; ++i) {
            const double cx = g.cx(i);
            // Distance from (cx, cy) to segment ab.
            double d2;
            if (len2 <= 0.0) {
                const double ex = cx - ax, ey = cy - ay;
                d2 = ex * ex + ey * ey;
            } else {
                const double t = std::clamp(
                    ((cx - ax) * dx + (cy - ay) * dy) / len2, 0.0, 1.0);
                const double px = ax + t * dx, py = ay + t * dy;
                const double ex = cx - px, ey = cy - py;
                d2 = ex * ex + ey * ey;
            }
            if (d2 <= r * r) {
                const std::size_t idx =
                    static_cast<std::size_t>(i)
                    + static_cast<std::size_t>(j) * g.nx()
                    + static_cast<std::size_t>(k) * g.nx() * g.ny();
                out.ids[idx] = id;
            }
        }
    }
}

void stamp_disk(VoxelMaterialField& out, int k,
                double cx0, double cy0, double r, MaterialId id) {
    const auto& g = out.grid;
    const double xlo = cx0 - r, xhi = cx0 + r, ylo = cy0 - r, yhi = cy0 + r;
    const int ilo = std::max(0, static_cast<int>((xlo - g.x0) / g.dx()));
    const int ihi = std::min(g.nx() - 1, static_cast<int>((xhi - g.x0) / g.dx()));
    const int jlo = std::max(0, static_cast<int>((ylo - g.y0) / g.dy()));
    const int jhi = std::min(g.ny() - 1, static_cast<int>((yhi - g.y0) / g.dy()));
    const double r2 = r * r;
    for (int j = jlo; j <= jhi; ++j) {
        const double cy = g.cy(j);
        for (int i = ilo; i <= ihi; ++i) {
            const double cx = g.cx(i);
            const double ex = cx - cx0, ey = cy - cy0;
            if (ex * ex + ey * ey <= r2) {
                const std::size_t idx =
                    static_cast<std::size_t>(i)
                    + static_cast<std::size_t>(j) * g.nx()
                    + static_cast<std::size_t>(k) * g.nx() * g.ny();
                out.ids[idx] = id;
            }
        }
    }
}

}  // namespace

VoxelMaterialField voxelize_board(const circuitcore::board::Board& board,
                                   const VoxelizerConfig& cfg) {
    VoxelMaterialField out;

    double xmin, ymin, xmax, ymax;
    if (!board_bbox(board, xmin, ymin, xmax, ymax)) {
        // Empty board -- one-voxel grid of air so callers don't crash.
        out.grid.spec = {1, 1, 1, cfg.cell_xy_m, cfg.cell_xy_m, cfg.cell_z_m};
        out.ids.assign(1, kAirMaterialId);
        return out;
    }

    const double pad_xy = cfg.air_padding_cells * cfg.cell_xy_m;
    const double pad_z  = cfg.air_padding_cells * cfg.cell_z_m;
    const double zspan  = total_stack_thickness(board.stackup) + 2.0 * pad_z;
    out.grid.x0 = xmin - pad_xy;
    out.grid.y0 = ymin - pad_xy;
    out.grid.z0 = -pad_z;
    const int nx = std::max(1, static_cast<int>(
        std::ceil((xmax - xmin + 2.0 * pad_xy) / cfg.cell_xy_m)));
    const int ny = std::max(1, static_cast<int>(
        std::ceil((ymax - ymin + 2.0 * pad_xy) / cfg.cell_xy_m)));
    const int nz = std::max(1, static_cast<int>(
        std::ceil(zspan / cfg.cell_z_m)));
    out.grid.spec = {nx, ny, nz, cfg.cell_xy_m, cfg.cell_xy_m, cfg.cell_z_m};
    out.ids.assign(static_cast<std::size_t>(nx) * ny * nz, kAirMaterialId);

    // Lay down substrate everywhere except the air padding above + below.
    const int k_substrate_lo = cfg.air_padding_cells;
    const int k_substrate_hi = nz - cfg.air_padding_cells - 1;
    for (int k = std::max(0, k_substrate_lo);
         k <= std::min(nz - 1, k_substrate_hi); ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const std::size_t idx =
                    static_cast<std::size_t>(i)
                    + static_cast<std::size_t>(j) * nx
                    + static_cast<std::size_t>(k) * nx * ny;
                out.ids[idx] = kSubstrateMaterialId;
            }
        }
    }

    // Walk the stackup top-down assigning each copper layer to one k
    // slice. This is intentionally coarse for v1; multi-cell-thick
    // copper waits on the per-layer thickness handling in a follow-up.
    int next_k = k_substrate_lo;
    for (const auto& L : board.stackup.layers) {
        if (next_k > k_substrate_hi) break;
        const int k_here = next_k++;
        if (!L.is_copper()) continue;
        out.layer_ordinal_to_k[L.ordinal] = k_here;
        for (const auto& s : board.segments) {
            if (s.layer_ordinal != L.ordinal) continue;
            stamp_segment(out, k_here, s.start.x, s.start.y,
                           s.end.x, s.end.y, s.width, kCopperMaterialId);
        }
        for (const auto& v : board.vias) {
            // Through-hole vias touch every copper layer.
            stamp_disk(out, k_here, v.at.x, v.at.y,
                        0.5 * v.outer_diameter, kCopperMaterialId);
        }
        // Pads belonging to this layer. Through-hole pads list every
        // copper layer in layer_ordinals, so they get stamped on each.
        for (const auto& pd : board.pads) {
            const auto& los = pd.layer_ordinals;
            if (std::find(los.begin(), los.end(), L.ordinal) == los.end())
                continue;
            const double hw = 0.5 * pd.size.x;
            const double hh = 0.5 * pd.size.y;
            const bool have_size = (pd.size.x > 0.0 && pd.size.y > 0.0);
            if (pd.shape == circuitcore::board::PadShape::Circle) {
                const double r = have_size
                    ? std::max(hw, hh)
                    : 0.50e-3;
                stamp_disk(out, k_here, pd.at.x, pd.at.y, r,
                            kCopperMaterialId);
            } else if (have_size) {
                // Rect / oval / roundrect / custom -- treat as box.
                // (Capsule / corner radius are visual niceties only,
                // the thermal mass is the bounding-box area.)
                const auto& g = out.grid;
                const int ilo = std::max(0, static_cast<int>(
                    (pd.at.x - hw - g.x0) / g.dx()));
                const int ihi = std::min(g.nx() - 1, static_cast<int>(
                    (pd.at.x + hw - g.x0) / g.dx()));
                const int jlo = std::max(0, static_cast<int>(
                    (pd.at.y - hh - g.y0) / g.dy()));
                const int jhi = std::min(g.ny() - 1, static_cast<int>(
                    (pd.at.y + hh - g.y0) / g.dy()));
                for (int j = jlo; j <= jhi; ++j) {
                    for (int i = ilo; i <= ihi; ++i) {
                        const std::size_t idx =
                            static_cast<std::size_t>(i)
                            + static_cast<std::size_t>(j) * g.nx()
                            + static_cast<std::size_t>(k_here)
                              * g.nx() * g.ny();
                        out.ids[idx] = kCopperMaterialId;
                    }
                }
            } else {
                // Pad size unknown -- fall back to the default radius.
                stamp_disk(out, k_here, pd.at.x, pd.at.y, 0.50e-3,
                            kCopperMaterialId);
            }
        }
    }

    return out;
}

}  // namespace mpkit
