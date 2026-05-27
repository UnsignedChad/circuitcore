// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "pi/IrSolver.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

#ifdef PDNKIT_HAVE_CHOLMOD
#include <Eigen/CholmodSupport>
#endif

namespace pdnkit::pi {

Solution IrSolver::solve(const IrMesh& mesh, const SolveConfig& cfg) {
    Solution sol;

    const std::size_t N = mesh.nodes.size();
    if (N == 0) {
        sol.error = "empty mesh";
        return sol;
    }
    const bool explicit_currents = !mesh.node_currents.empty();
    if (!explicit_currents) {
        if (mesh.source_node_ids.empty()) {
            sol.error = "no source nodes";
            return sol;
        }
        if (mesh.sink_node_ids.empty()) {
            sol.error = "no sink nodes (the matrix would be singular without "
                        "a pinned reference)";
            return sol;
        }
    } else {
        // Explicit currents: must sum to ~0 (current conservation) and contain
        // at least one negative entry to pin as the reference.
        double sum = 0.0;
        bool has_negative = false;
        for (const auto& [nid, cur] : mesh.node_currents) {
            (void)nid;
            sum += cur;
            if (cur < 0.0) has_negative = true;
        }
        if (std::abs(sum) > 1.0e-9 * (1.0 + std::abs(sum))) {
            sol.error = "per-node currents do not sum to zero (charge would "
                        "accumulate); injected and drawn currents must balance";
            return sol;
        }
        if (!has_negative) {
            sol.error = "per-node currents have no sink (all currents >= 0); "
                        "need at least one negative current as ground reference";
            return sol;
        }
    }

    // 1) Conductance matrix G (N×N, SPSD). Each resistor between (a, b) with
    //    conductance g contributes:
    //        G[a][a] += g, G[b][b] += g, G[a][b] -= g, G[b][a] -= g
    //    setFromTriplets sums duplicate (i, j) entries, so we just emit four
    //    triplets per resistor and let Eigen do the accumulation.
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(4 * mesh.resistors.size() + mesh.sink_node_ids.size());

    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= static_cast<int>(N)) continue;
        if (r.to_node   < 0 || r.to_node   >= static_cast<int>(N)) continue;
        triplets.emplace_back(r.from_node, r.from_node,  r.conductance);
        triplets.emplace_back(r.to_node,   r.to_node,    r.conductance);
        triplets.emplace_back(r.from_node, r.to_node,   -r.conductance);
        triplets.emplace_back(r.to_node,   r.from_node, -r.conductance);
    }

    // 2) Pin a reference node to 0 V via large-diagonal stiffness. With G
    //    typical entries around a few thousand S, 1e15 dominates by ~11
    //    orders of magnitude while staying clear of double-precision overflow.
    constexpr double kPinStiffness = 1.0e15;

    if (!explicit_currents) {
        for (int s : mesh.sink_node_ids) {
            if (s >= 0 && s < static_cast<int>(N)) {
                triplets.emplace_back(s, s, kPinStiffness);
            }
        }
    } else {
        // Pin the most-negative-current node (the deepest sink).
        int pin_node = -1;
        double pin_value = 0.0;
        for (const auto& [nid, cur] : mesh.node_currents) {
            if (cur < pin_value && nid >= 0 && nid < static_cast<int>(N)) {
                pin_value = cur;
                pin_node = nid;
            }
        }
        if (pin_node >= 0) {
            triplets.emplace_back(pin_node, pin_node, kPinStiffness);
        }
    }

    Eigen::SparseMatrix<double> G(static_cast<Eigen::Index>(N),
                                   static_cast<Eigen::Index>(N));
    G.setFromTriplets(triplets.begin(), triplets.end());
    G.makeCompressed();

    // 3) RHS construction.
    Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(N));
    if (!explicit_currents) {
        // Default: total_current split evenly across source nodes.
        const double per_source = cfg.total_current /
            static_cast<double>(mesh.source_node_ids.size());
        for (int s : mesh.source_node_ids) {
            if (s >= 0 && s < static_cast<int>(N)) b[s] += per_source;
        }
    } else {
        for (const auto& [nid, cur] : mesh.node_currents) {
            if (nid >= 0 && nid < static_cast<int>(N)) b[nid] += cur;
        }
    }

    // 4) Solve with sparse Cholesky.
#ifdef PDNKIT_HAVE_CHOLMOD
    Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>> solver;
#else
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> solver;
#endif
    solver.compute(G);
    if (solver.info() != Eigen::Success) {
        sol.error = "Cholesky factorization failed (matrix may be singular)";
        return sol;
    }

    Eigen::VectorXd v = solver.solve(b);
    if (solver.info() != Eigen::Success) {
        sol.error = "back-substitution failed";
        return sol;
    }

    sol.voltages.assign(v.data(), v.data() + N);
    sol.max_v = v.maxCoeff();
    sol.min_v = v.minCoeff();
    sol.ok = true;
    return sol;
}

}  // namespace pdnkit::pi
