#include "pi/Thermal.h"

#include <cmath>
#include <utility>

namespace pdnkit::pi {

namespace {

double sum_dissipated_power(const IrMesh& mesh, const Solution& sol) {
    if (!sol.ok || sol.voltages.size() != mesh.nodes.size()) return 0.0;
    double p = 0.0;
    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 ||
            r.from_node >= static_cast<int>(sol.voltages.size()) ||
            r.to_node   < 0 ||
            r.to_node   >= static_cast<int>(sol.voltages.size())) continue;
        const double dv = sol.voltages[r.from_node] - sol.voltages[r.to_node];
        // G = 1/R; P_R = dv^2 / R = dv^2 * G
        p += dv * dv * r.conductance;
    }
    return p;
}

}  // namespace

ThermalResult solve_ir_with_thermal(
    const circuitcore::board::Board& board,
    const MeshConfig& mc_in,
    const SolveConfig& sc,
    const ThermalConfig& tc) {
    ThermalResult res;
    MeshConfig mc = mc_in;
    const double rho_20 = mc.copper_rho;
    double t_now = tc.t_ambient_c;

    for (int i = 1; i <= tc.max_iterations; ++i) {
        mc.copper_rho = rho_20 * (1.0 + tc.alpha_per_c *
                                       (t_now - 20.0));
        auto mesh = IrMesher::build(board, mc);
        auto sol = IrSolver::solve(mesh, sc);
        const double p = sum_dissipated_power(mesh, sol);
        const double dt_new = p * tc.r_theta_total_kw;
        const double t_new = tc.t_ambient_c + dt_new;
        const double change = std::abs(t_new - t_now);

        res.mesh = std::move(mesh);
        res.solution = std::move(sol);
        res.final_rho = mc.copper_rho;
        res.final_delta_t_c = t_new - tc.t_ambient_c;
        res.final_power_w = p;
        res.iterations = i;

        if (change < tc.convergence_tolerance_c) {
            res.converged = true;
            return res;
        }
        t_now = t_new;
    }
    return res;
}

}  // namespace pdnkit::pi
