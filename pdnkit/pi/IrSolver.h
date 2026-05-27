// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Static IR-drop solver.
//
// Assembles the conductance matrix G from an IrMesh and solves G·v = i for
// the per-node voltage vector. Sink nodes are pinned to 0 V via a
// large-diagonal stiffness (preserves SPD so Cholesky still applies).
//
// Solver: Eigen::SimplicialLLT — pure-Eigen sparse Cholesky. Switch to
// Eigen::CholmodSupernodalLLT (SuiteSparse) once we have boards big enough
// to care.

#pragma once

#include <string>
#include <vector>

#include "pi/IrMesher.h"

namespace pdnkit::pi {

struct SolveConfig {
    // Total current injected at sources (split evenly across source nodes).
    // Default 1 A makes the resulting voltage map equal to the network's
    // effective resistance — a useful unit-current view.
    double total_current = 1.0;
};

struct Solution {
    std::vector<double> voltages;  // one entry per mesh node (volts)
    double max_v = 0.0;
    double min_v = 0.0;
    bool ok = false;
    std::string error;
};

class IrSolver {
public:
    static Solution solve(const IrMesh& mesh, const SolveConfig& cfg = {});
};

}  // namespace pdnkit::pi
