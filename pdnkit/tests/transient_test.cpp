// Validate the transient solver against the textbook RC step response.
//
// For a single resistor R between source and sink, with capacitance C at the
// source node, a step current I at t=0 produces:
//
//     V_src(t) = I * R * (1 - exp(-t / (R * C)))
//
// The pdnkit Backward Euler discretization should converge to this curve as
// the timestep shrinks. We assert <5% error at dt small compared to RC.

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/IrMesher.h"
#include "pi/Transient.h"

using pdnkit::pi::IrMesh;
using pdnkit::pi::TransientConfig;
using pdnkit::pi::solve_step_transient;
using Catch::Approx;

namespace {

IrMesh two_node_rc(double conductance) {
    IrMesh m;
    m.nodes.push_back({0, 0.0, 0.0, 0, 0, 0});
    m.nodes.push_back({1, 0.001, 0.0, 1, 0, 0});
    m.resistors.push_back({0, 1, conductance});
    m.source_node_ids.push_back(0);
    m.sink_node_ids.push_back(1);
    return m;
}

}  // namespace

TEST_CASE("transient: empty mesh returns error", "[transient]") {
    TransientConfig cfg;
    auto r = solve_step_transient(IrMesh{}, cfg);
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("transient: missing sink returns error", "[transient]") {
    IrMesh m;
    m.nodes.push_back({0, 0, 0, 0, 0, 0});
    m.source_node_ids.push_back(0);
    auto r = solve_step_transient(m, {});
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("transient: two-node RC matches analytical step response", "[transient]") {
    const double G = 1000.0;     // 1 mOhm resistor (G = 1/R)
    const double R = 1.0 / G;    // 0.001 Ohm
    const double C = 1.0e-6;     // 1 uF per node
    const double I = 1.0;         // 1 A step
    const double tau = R * C;     // ~1 us

    auto mesh = two_node_rc(G);

    TransientConfig cfg;
    cfg.per_node_capacitance = C;
    cfg.dt = tau / 100.0;        // 100 steps per time constant
    cfg.n_steps = 600;            // ~6 tau total
    cfg.t_zero_step = 0.0;
    cfg.step_current = I;
    cfg.obs_node_id = 0;          // observe at source

    auto res = solve_step_transient(mesh, cfg);
    REQUIRE(res.ok);
    REQUIRE(res.times.size() == 600);

    // Check four sample points along the curve.
    auto v_analytic = [&](double t) { return I * R * (1.0 - std::exp(-t / tau)); };

    for (double check_t : {0.5 * tau, 1.0 * tau, 3.0 * tau, 5.0 * tau}) {
        // Find the timestep nearest the check time.
        std::size_t idx = 0;
        double best = 1e9;
        for (std::size_t i = 0; i < res.times.size(); ++i) {
            const double d = std::abs(res.times[i] - check_t);
            if (d < best) { best = d; idx = i; }
        }
        const double v_solver = res.obs_v[idx];
        const double v_theory = v_analytic(res.times[idx]);
        INFO("at t=" << check_t << ", solver=" << v_solver
                     << " theory=" << v_theory);
        // Backward Euler is first-order accurate; with dt = tau/100 we expect
        // <5% error throughout the transient.
        REQUIRE(std::abs(v_solver - v_theory) / std::abs(v_theory + 1e-12) < 0.05);
    }
}

TEST_CASE("transient: asymptotic value matches static IR drop", "[transient]") {
    const double G = 1000.0;
    const double R = 1.0 / G;
    auto mesh = two_node_rc(G);

    TransientConfig cfg;
    cfg.per_node_capacitance = 1.0e-9;
    cfg.dt = 1.0e-9;
    cfg.n_steps = 2000;
    cfg.step_current = 1.0;

    auto res = solve_step_transient(mesh, cfg);
    REQUIRE(res.ok);
    const double v_final = res.obs_v.back();
    // Should converge to I*R = 1mV.
    REQUIRE(v_final == Approx(R).epsilon(0.01));
}

TEST_CASE("transient: per-node C vector overrides scalar", "[transient]") {
    const double G = 1000.0;
    auto mesh = two_node_rc(G);

    TransientConfig cfg;
    cfg.per_node_capacitance = 1.0e-15;  // tiny scalar (ignored)
    cfg.per_node_capacitances = {1.0e-6, 1.0e-6};
    cfg.dt = 10.0e-9;
    cfg.n_steps = 600;
    cfg.step_current = 1.0;

    auto res = solve_step_transient(mesh, cfg);
    REQUIRE(res.ok);
    REQUIRE(res.obs_v.back() == Approx(0.001).epsilon(0.01));
}

TEST_CASE("transient: per-node vector wrong length is an error", "[transient]") {
    auto mesh = two_node_rc(1000.0);
    TransientConfig cfg;
    cfg.per_node_capacitances = {1.0e-6};  // wrong size (mesh has 2 nodes)
    auto res = solve_step_transient(mesh, cfg);
    REQUIRE_FALSE(res.ok);
}

TEST_CASE("transient: distributed C builder gives plane-pair value per cell", "[transient]") {
    auto mesh = two_node_rc(1000.0);
    // 0.5mm cells, FR-4 eps_r 4.3, 1.6mm board:
    // C_cell = 4.3 * 8.854e-12 * (0.5e-3)^2 / 1.6e-3
    //        = 4.3 * 8.854e-12 * 2.5e-7 / 1.6e-3
    //        ~= 5.95e-15 F per cell.
    auto c = pdnkit::pi::build_distributed_capacitance(
        mesh, 0.5e-3, 4.3, 1.6e-3, {});
    REQUIRE(c.size() == 2);
    REQUIRE(c[0] == Approx(5.95e-15).epsilon(0.05));
    REQUIRE(c[1] == Approx(5.95e-15).epsilon(0.05));
}

TEST_CASE("transient: decap C lumps onto nearest node", "[transient]") {
    auto mesh = two_node_rc(1000.0);
    pdnkit::pi::Decap d{0.001, 0.0, 1.0e-6, 0.005, 0.5e-9};  // near node 1
    auto c = pdnkit::pi::build_distributed_capacitance(
        mesh, 0.5e-3, 4.3, 1.6e-3, {d});
    // Node 1 picks up the lumped decap C.
    REQUIRE(c[1] > 0.5e-6);
    REQUIRE(c[0] < 1e-9);
}
