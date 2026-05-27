// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Iterative IR + thermal coupling.
//
// At sign-off-grade currents, Joule heating raises copper resistivity:
//
//   rho(T) = rho_20 * (1 + alpha * (T - 20 C))    alpha_Cu = 0.00393 /C
//
// A 30 C rise means ~12% higher rho, which means a tighter solve
// predicts a worse drop. Real signoff tools iterate:
//
//   1. Solve IR at ambient T.
//   2. Compute dissipated power P = sum (V_from - V_to)^2 / R_resistor.
//   3. delta_T = P * R_theta   (R_theta is K/W from copper to ambient).
//   4. Update rho with the new average T; re-mesh; re-solve.
//   5. Repeat until delta_T converges (usually 3-5 iterations).
//
// MVP: uniform-temperature approximation. The whole net rises by the
// same delta_T, computed from total power and an aggregate R_theta.
// Per-cell coupling (hotspot rises faster than average) is a future
// refinement -- needs a 2D thermal solver next to the IR solver.

#pragma once

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "circuitcore/board/Board.h"

namespace pdnkit::pi {

struct ThermalConfig {
    double t_ambient_c = 25.0;
    double alpha_per_c = 0.00393;        // copper TCR
    // Aggregate thermal resistance from the meshed copper to ambient
    // (K/W). 50-200 typical depending on board size, copper area, and
    // forced vs natural convection. 100 is a reasonable default.
    double r_theta_total_kw = 100.0;
    int    max_iterations = 10;
    double convergence_tolerance_c = 0.1;
};

struct ThermalResult {
    IrMesh   mesh;
    Solution solution;
    double   final_rho;
    double   final_delta_t_c;   // steady-state rise above ambient
    double   final_power_w;     // dissipated by the net at convergence
    int      iterations;
    bool     converged = false;
};

// Iterate IR + copper resistivity to steady-state. mc.copper_rho is the
// 20 C reference; the iteration updates it internally each pass.
ThermalResult solve_ir_with_thermal(
    const circuitcore::board::Board& board,
    const MeshConfig& mc,
    const SolveConfig& sc,
    const ThermalConfig& tc);

}  // namespace pdnkit::pi
