// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Per-component metadata coupling. See header for the design.

#include "mp/ComponentCoupling.h"
#include "circuitcore/board/Bounds.h"

#include <algorithm>
#include <cmath>

#include "circuitcore/board/PackageDefaults.h"

namespace mpkit {

namespace {

// Walk pads belonging to this component and return the inflated XY
// bbox. Returns false if the component has no pads.
struct PadBbox {
    double x_lo, y_lo, x_hi, y_hi;
    bool   has_back_only;   // any pad on B.Cu and not F.Cu -> bottom side
};

bool pad_bbox(const circuitcore::board::Board& board,
              const circuitcore::board::Component& c,
              PadBbox& out) noexcept {
    out.has_back_only = false;
    // Bounds via the canonical helper; side detection still walks pads
    // here since Bounds2 deliberately doesnt carry layer info.
    const auto bb = circuitcore::board::bounds_of_pads(board, c.reference);
    if (!bb.valid || !(bb.lo_x < bb.hi_x) || !(bb.lo_y < bb.hi_y))
        return false;
    out.x_lo = bb.lo_x; out.y_lo = bb.lo_y;
    out.x_hi = bb.hi_x; out.y_hi = bb.hi_y;
    bool back_seen = false, front_seen = false;
    for (const auto& pd : board.pads) {
        if (pd.parent_ref != c.reference) continue;
        for (int o : pd.layer_ordinals) {
            if (o == 0)  front_seen = true;
            if (o == 31) back_seen  = true;
        }
    }
    out.has_back_only = back_seen && !front_seen;
    return true;
}

// Final xy bbox for a component: courtyard if present, else pad-derived
// inflated by 0.2 mm.
struct Footprint {
    double x_lo, y_lo, x_hi, y_hi;
    bool   bottom_side;
};

bool component_footprint(const circuitcore::board::Board& board,
                          const circuitcore::board::Component& c,
                          Footprint& out) noexcept {
    const bool have_crtyd =
        !(c.courtyard_lo.x == 0.0 && c.courtyard_lo.y == 0.0 &&
          c.courtyard_hi.x == 0.0 && c.courtyard_hi.y == 0.0);
    PadBbox pb{};
    const bool have_pads = pad_bbox(board, c, pb);

    if (have_crtyd) {
        out.x_lo = c.courtyard_lo.x;
        out.y_lo = c.courtyard_lo.y;
        out.x_hi = c.courtyard_hi.x;
        out.y_hi = c.courtyard_hi.y;
        out.bottom_side = have_pads && pb.has_back_only;
        return true;
    }
    if (!have_pads) return false;
    constexpr double inflate = 0.20e-3;
    out.x_lo = pb.x_lo - inflate;
    out.y_lo = pb.y_lo - inflate;
    out.x_hi = pb.x_hi + inflate;
    out.y_hi = pb.y_hi + inflate;
    out.bottom_side = pb.has_back_only;
    return true;
}

}  // namespace

// ---- 1. fill in defaults ---------------------------------------------

int apply_default_metadata(circuitcore::board::Board& board) noexcept {
    int n_touched = 0;
    for (auto& c : board.components) {
        bool touched = false;
        if (c.body_height_m <= 0.0) {
            c.body_height_m =
                circuitcore::board::default_body_height_m(c.name);
            touched = true;
        }
        if (c.mass_kg <= 0.0) {
            c.mass_kg = circuitcore::board::default_mass_kg(c.name);
            touched = true;
        }
        if (touched) ++n_touched;
    }
    return n_touched;
}

// ---- 2. dissipated power -> Joule source field -----------------------

JouleSourceField component_power_to_joule_source(
    const circuitcore::board::Board& board,
    const VoxelMaterialField& material_field) {

    JouleSourceField out;
    const auto& g  = material_field.grid;
    const int    nx = g.nx(), ny = g.ny(), nz = g.nz();
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        out.error = "component_power_to_joule_source: empty grid";
        return out;
    }
    if (g.dx() <= 0 || g.dy() <= 0 || g.dz() <= 0) {
        out.error =
            "component_power_to_joule_source: non-positive grid spacing";
        return out;
    }
    out.source.resize(nx, ny, nz);
    out.source.fill(0.0);

    // The board "top" / "bottom" z is whichever copper layer's center
    // is highest / lowest in the voxel grid.
    int k_top    = 0;
    int k_bottom = nz - 1;
    if (!material_field.layer_ordinal_to_k.empty()) {
        int hi_k = -1, lo_k = nz;
        for (const auto& [ord, kk] : material_field.layer_ordinal_to_k) {
            (void)ord;
            if (kk > hi_k) hi_k = kk;
            if (kk < lo_k) lo_k = kk;
        }
        if (hi_k >= 0) k_top    = hi_k;
        if (lo_k < nz) k_bottom = lo_k;
    }

    for (const auto& c : board.components) {
        const double P = c.dissipated_power_w;
        if (!(P > 0.0)) continue;

        Footprint fp{};
        if (!component_footprint(board, c, fp)) {
            ++out.dropped_nodes;  // reuse the counter for "skipped components"
            continue;
        }
        // Body height: respect explicit, else look up.
        double h = c.body_height_m;
        if (h <= 0.0)
            h = circuitcore::board::default_body_height_m(c.name);
        if (h <= 0.0) {
            ++out.dropped_nodes;
            continue;
        }

        // Voxel bounds in xy.
        const int i_lo = std::max(0, static_cast<int>(
            (fp.x_lo - g.x0) / g.dx()));
        const int i_hi = std::min(nx - 1, static_cast<int>(
            (fp.x_hi - g.x0) / g.dx()));
        const int j_lo = std::max(0, static_cast<int>(
            (fp.y_lo - g.y0) / g.dy()));
        const int j_hi = std::min(ny - 1, static_cast<int>(
            (fp.y_hi - g.y0) / g.dy()));
        if (i_hi < i_lo || j_hi < j_lo) {
            ++out.dropped_nodes;
            continue;
        }

        // Voxel bounds in z. Start at the surface (top or bottom of the
        // board) and walk outward by the body height; clamp to grid.
        const int n_zk = std::max(1, static_cast<int>(std::ceil(
            h / g.dz())));
        int k_lo, k_hi;
        if (fp.bottom_side) {
            k_hi = k_bottom;
            k_lo = std::max(0, k_hi - (n_zk - 1));
        } else {
            k_lo = k_top;
            k_hi = std::min(nz - 1, k_lo + (n_zk - 1));
        }
        if (k_hi < k_lo) {
            ++out.dropped_nodes;
            continue;
        }

        // W/m^3 = P / volume(cells * dx*dy*dz).
        const std::size_t n_cells =
            static_cast<std::size_t>(i_hi - i_lo + 1) *
            static_cast<std::size_t>(j_hi - j_lo + 1) *
            static_cast<std::size_t>(k_hi - k_lo + 1);
        if (n_cells == 0) {
            ++out.dropped_nodes;
            continue;
        }
        const double cell_volume = g.dx() * g.dy() * g.dz();
        const double q = P / (cell_volume * static_cast<double>(n_cells));

        for (int kk = k_lo; kk <= k_hi; ++kk) {
            for (int jj = j_lo; jj <= j_hi; ++jj) {
                for (int ii = i_lo; ii <= i_hi; ++ii) {
                    out.source.at(ii, jj, kk) += q;
                }
            }
        }
        out.total_power_w += P;
    }

    out.ok = true;
    return out;
}

// ---- 3. summary ------------------------------------------------------

ComponentSummary compute_component_summary(
    const circuitcore::board::Board& board) noexcept {

    ComponentSummary s;
    s.n_components = static_cast<int>(board.components.size());
    for (const auto& c : board.components) {
        // Side detection mirrors component_footprint -- if all pads of
        // this component live on B.Cu only, count it bottom-side.
        PadBbox pb{};
        if (pad_bbox(board, c, pb) && pb.has_back_only) ++s.n_bottom_side;
        else                                            ++s.n_top_side;

        const double mass = (c.mass_kg > 0.0)
            ? c.mass_kg
            : circuitcore::board::default_mass_kg(c.name);
        s.total_mass_kg += mass;
        s.total_power_w += c.dissipated_power_w;
        if (c.dissipated_power_w > s.hottest_power_w) {
            s.hottest_power_w   = c.dissipated_power_w;
            s.hottest_reference = c.reference;
        }
    }
    return s;
}

}  // namespace mpkit
