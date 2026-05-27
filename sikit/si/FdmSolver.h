// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// 2D finite-difference Laplace solver for PCB cross-sections.
//
// Solves ∇·(ε ∇V) = 0 with Dirichlet boundary conditions:
//   conductors → fixed V (their `voltage` field)
//   outer box  → V = 0
//
// Uses successive over-relaxation (SOR). The discrete stencil weights cell
// faces by face-averaged ε (geometric mean of the two adjacent cells'
// ε_r), which keeps the operator self-adjoint and conserves flux through
// dielectric interfaces.

#pragma once

#include "si/CrossSection.h"
#include "si/FdmGrid.h"

namespace sikit::em2d {

struct SolveConfig {
    int    max_iterations = 50000;
    double tolerance      = 1.0e-6;
    double omega          = 1.85;  // SOR factor; 1.7–1.9 is the useful range
};

struct SolveResult {
    bool ok = false;
    int  iterations = 0;
    double final_residual = 0.0;
};

// Build an FDM grid covering the entire cross-section (with `air_above`
// of air on top) at the requested cell size. Marks dielectric layers and
// conductors. Outer rows/columns are zero-Dirichlet boundary cells.
FdmGrid build_grid(const CrossSection& cs, double cell_size_m);

// Drive the SOR iteration to convergence (or max iterations).
SolveResult solve(FdmGrid& g, const SolveConfig& cfg = {});

// Total charge per unit length on the named conductor, computed by
// applying Gauss's law (∮ε·E·dl) around a closed loop just outside the
// conductor cells. Returned in coulombs per metre of trace length.
double charge_per_length(const FdmGrid& g, int conductor_id);

struct ImpedanceResult {
    bool   ok = false;
    double c_per_m = 0.0;       // F/m with the real dielectric stack
    double c_air_per_m = 0.0;   // F/m with all dielectric replaced by air
    double z0_ohm = 0.0;        // characteristic impedance Ω
    double eps_eff = 0.0;       // effective relative permittivity
    double v_phase = 0.0;       // m/s
    int    iter_dielectric = 0;
    int    iter_air = 0;
};

// Two-conductor characteristic impedance via the standard
//   Z₀ = 1 / (c · √(C · C_air))
// trick. Runs two FDM solves: one with the real dielectric stack and one
// with everything replaced by air. Caller sets `trace_id` to V=1 and
// `ground_id` to V=0 inside the CrossSection before calling.
ImpedanceResult compute_z0(const CrossSection& cs, int trace_id, int ground_id,
                            double cell_size_m,
                            const SolveConfig& cfg = {});

// Richardson-extrapolated Z₀: runs `compute_z0` at h and 2h and
// linearly combines the per-unit-length capacitances to cancel the
// leading-order O(h) error of the cell-rect classifier. Costs ~5× a
// single solve (the 2h pass is cheap; h is the same as before) but
// brings the canonical 50 Ω microstrip case from ~12 % high to under
// ~5 % at the same h.
//
// Reports the iteration counts of the two underlying solves so callers
// can sanity-check convergence on both grids.
struct RefinedImpedanceResult {
    bool   ok = false;
    double c_per_m = 0.0;
    double c_air_per_m = 0.0;
    double z0_ohm = 0.0;
    double eps_eff = 0.0;
    double v_phase = 0.0;
    int    iter_fine = 0;
    int    iter_coarse = 0;
    double z0_fine = 0.0;     // h-only result (for comparison)
    double z0_coarse = 0.0;   // 2h-only result (for comparison)
};

RefinedImpedanceResult compute_z0_refined(const CrossSection& cs,
                                            int trace_id, int ground_id,
                                            double cell_size_m,
                                            const SolveConfig& cfg = {});

}  // namespace sikit::em2d
