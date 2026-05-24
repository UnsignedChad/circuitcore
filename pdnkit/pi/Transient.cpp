#include "pi/CavityModel.h"
#include "pi/Transient.h"

#include <algorithm>
#include <limits>
#include <cmath>

#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

#ifdef PDNKIT_HAVE_CHOLMOD
#include <Eigen/CholmodSupport>
#endif

namespace pdnkit::pi {

TransientResult solve_step_transient(const IrMesh& mesh,
                                      const TransientConfig& cfg) {
    TransientResult out;
    const int N = static_cast<int>(mesh.nodes.size());

    if (N == 0) { out.error = "empty mesh"; return out; }
    if (cfg.n_steps < 1 || cfg.dt <= 0.0) {
        out.error = "invalid TransientConfig (need n_steps>=1, dt>0)";
        return out;
    }
    const bool have_vec = !cfg.per_node_capacitances.empty();
    if (have_vec && static_cast<int>(cfg.per_node_capacitances.size()) != N) {
        out.error = "per_node_capacitances size != mesh node count";
        return out;
    }
    if (!have_vec && cfg.per_node_capacitance <= 0.0) {
        out.error = "no per-node capacitance supplied (need vector or scalar > 0)";
        return out;
    }
    if (mesh.source_node_ids.empty()) {
        out.error = "no source nodes -- nothing to step";
        return out;
    }
    if (mesh.sink_node_ids.empty()) {
        out.error = "no sink nodes -- matrix would be singular";
        return out;
    }

    // Build G (sparse conductance) + C/dt diagonal + sink pin diagonal.
    constexpr double kPinStiffness = 1.0e15;
    // Per-node C/dt vector. Uniform fallback when no vector was supplied.
    std::vector<double> c_over_dt(N);
    for (int i = 0; i < N; ++i) {
        const double ci = have_vec ? cfg.per_node_capacitances[i]
                                    : cfg.per_node_capacitance;
        c_over_dt[i] = ci / cfg.dt;
    }

    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(4 * mesh.resistors.size() + 2 * N);

    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 || r.from_node >= N) continue;
        if (r.to_node   < 0 || r.to_node   >= N) continue;
        triplets.emplace_back(r.from_node, r.from_node,  r.conductance);
        triplets.emplace_back(r.to_node,   r.to_node,    r.conductance);
        triplets.emplace_back(r.from_node, r.to_node,   -r.conductance);
        triplets.emplace_back(r.to_node,   r.from_node, -r.conductance);
    }
    for (int i = 0; i < N; ++i) triplets.emplace_back(i, i, c_over_dt[i]);
    for (int s : mesh.sink_node_ids) {
        if (s >= 0 && s < N) triplets.emplace_back(s, s, kPinStiffness);
    }

    Eigen::SparseMatrix<double> A(N, N);
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

#ifdef PDNKIT_HAVE_CHOLMOD
    Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>> solver;
#else
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> solver;
#endif
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        out.error = "Cholesky factorization failed (matrix may be singular)";
        return out;
    }

    // Per-step source-current vector (constant after t_zero_step).
    Eigen::VectorXd i_step = Eigen::VectorXd::Zero(N);
    const double per_source = cfg.step_current /
        static_cast<double>(mesh.source_node_ids.size());
    for (int s : mesh.source_node_ids) {
        if (s >= 0 && s < N) i_step[s] += per_source;
    }

    Eigen::VectorXd v = Eigen::VectorXd::Zero(N);

    const int obs = (cfg.obs_node_id >= 0 && cfg.obs_node_id < N)
        ? cfg.obs_node_id : mesh.source_node_ids.front();

    out.times.reserve(cfg.n_steps);
    out.max_v.reserve(cfg.n_steps);
    out.obs_v.reserve(cfg.n_steps);

    for (int k = 0; k < cfg.n_steps; ++k) {
        const double t = (k + 1) * cfg.dt;
        // RHS = C/dt * v_prev + (step on if t >= t_zero_step). Element-wise
        // multiply by the per-node C/dt vector.
        Eigen::VectorXd rhs(N);
        for (int i = 0; i < N; ++i) rhs[i] = c_over_dt[i] * v[i];
        if (t >= cfg.t_zero_step) rhs += i_step;

        v = solver.solve(rhs);
        if (solver.info() != Eigen::Success) {
            out.error = "transient back-substitution failed";
            return out;
        }
        out.times.push_back(t);
        out.obs_v.push_back(v[obs]);
        out.max_v.push_back(v.cwiseAbs().maxCoeff());
    }

    out.ok = true;
    return out;
}



std::vector<double> build_distributed_capacitance(
    const IrMesh& mesh,
    double cell_size,
    double eps_r,
    double substrate_thickness_m,
    const std::vector<Decap>& decaps) {
    constexpr double kEps0 = 8.854187817e-12;
    const int N = static_cast<int>(mesh.nodes.size());
    std::vector<double> c(N, 0.0);
    if (N == 0 || cell_size <= 0.0 || eps_r <= 0.0 || substrate_thickness_m <= 0.0) {
        return c;
    }
    const double cell_area = cell_size * cell_size;
    const double c_cell = eps_r * kEps0 * cell_area / substrate_thickness_m;
    for (int i = 0; i < N; ++i) c[i] = c_cell;

    // Lump each decap onto the nearest node.
    for (const auto& d : decaps) {
        if (d.C <= 0.0) continue;
        int best = -1;
        double best_d2 = std::numeric_limits<double>::infinity();
        for (int i = 0; i < N; ++i) {
            const double dx = mesh.nodes[i].x - d.x;
            const double dy = mesh.nodes[i].y - d.y;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best = i; }
        }
        if (best >= 0) c[best] += d.C;
    }
    return c;
}

}  // namespace pdnkit::pi
