// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Per-unit-length RLGC matrix extraction for N-conductor 2D cross-sections.
//
// For an N-signal-conductor system over a ground reference, the Maxwell
// capacitance matrix C is N×N with:
//   C[i][i] > 0    (self-capacitance to ground)
//   C[i][j] < 0    (negative of mutual coupling, i ≠ j)
// Extracted by N FDM solves, one per excited conductor.
//
// The inductance matrix L is recovered from the all-air capacitance
// matrix via L = (1/c²) · C_air⁻¹ — the standard TEM identity, exact
// for non-magnetic dielectrics (μ_r = 1).
//
// From {C, L} we derive near-end / far-end crosstalk coefficients for
// any aggressor/victim pair via the textbook modal-mismatch formula.

#pragma once

#include <vector>

#include <Eigen/Dense>

#include "si/CrossSection.h"
#include "si/FdmSolver.h"

namespace sikit::em2d {

struct RlgcMatrices {
    int n = 0;                  // number of signal conductors
    Eigen::MatrixXd C;          // F/m — Maxwell capacitance with real dielectric
    Eigen::MatrixXd C_air;      // F/m — same problem in all-air
    Eigen::MatrixXd L;          // H/m — recovered from C_air via (1/c²)·C_air⁻¹
    bool ok = false;
};

// Compute C, C_air, L for an N-signal-conductor cross-section.
//
//   signal_ids: conductors (by id) to treat as signals; matrix indices
//               follow the order of this vector.
//   ground_id:  conductor (by id) held at V = 0 in every excitation. Must
//               exist in cs.conductors.
//
// One FDM solve per signal × 2 (dielectric + air), so N=3 conductors
// means 6 solves. At the v0 cell size (W/25) this is seconds for
// realistic geometries.
RlgcMatrices compute_rlgc(const CrossSection& cs,
                           const std::vector<int>& signal_ids,
                           int ground_id,
                           double cell_size_m,
                           const SolveConfig& cfg = {});

// Per-aggressor / per-victim coupling derived from an RLGC matrix.
// Both are returned for a single (aggressor, victim) pair.
struct CrosstalkCoefficients {
    // Near-end crosstalk coefficient: steady-state amplitude on the
    // victim's near terminal as a fraction of the aggressor's voltage,
    // assuming matched terminations. Standard textbook formula:
    //     K_NE = (1/4) · (L_m / L_self − C_m / C_self)
    double k_near_end = 0.0;

    // Modal mismatch κ = L_m / L_self − C_m / C_self. Drives FEXT;
    // K_FE = −(length / (2 · t_rise · v)) · κ requires the user to
    // supply length, rise time, and propagation velocity for any given
    // signal — we hand back κ and let the caller plug in the rest.
    double modal_mismatch = 0.0;
};

// Compute crosstalk coefficients for the (aggressor, victim) pair from a
// given RLGC matrix. The indices are positions into the signal_ids
// vector the matrix was built with.
CrosstalkCoefficients crosstalk_for_pair(const RlgcMatrices& rlgc,
                                          int aggressor_idx,
                                          int victim_idx);

}  // namespace sikit::em2d
