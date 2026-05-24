#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

using pdnkit::pi::IrMesher;
using pdnkit::pi::IrMesh;
using pdnkit::pi::IrSolver;
using pdnkit::pi::MeshConfig;
using pdnkit::pi::SolveConfig;
using namespace circuitcore::board;
using Catch::Approx;

namespace {

// Hand-build a tiny mesh for unit-level solver tests (no parser needed).
IrMesh make_two_node(double conductance) {
    IrMesh m;
    m.nodes.push_back({0, 0.0, 0.0, 0, 0});
    m.nodes.push_back({1, 0.001, 0.0, 1, 0});
    m.resistors.push_back({0, 1, conductance});
    m.source_node_ids.push_back(0);
    m.sink_node_ids.push_back(1);
    return m;
}

IrMesh make_three_node_series(double conductance) {
    IrMesh m;
    m.nodes.push_back({0, 0.0,   0.0, 0, 0});
    m.nodes.push_back({1, 0.001, 0.0, 1, 0});
    m.nodes.push_back({2, 0.002, 0.0, 2, 0});
    m.resistors.push_back({0, 1, conductance});
    m.resistors.push_back({1, 2, conductance});
    m.source_node_ids.push_back(0);
    m.sink_node_ids.push_back(2);
    return m;
}

}  // namespace

TEST_CASE("solver: empty mesh returns error", "[solver]") {
    auto s = IrSolver::solve({});
    REQUIRE_FALSE(s.ok);
    REQUIRE(s.error == "empty mesh");
}

TEST_CASE("solver: missing sources fails cleanly", "[solver]") {
    IrMesh m;
    m.nodes.push_back({0, 0, 0, 0, 0});
    auto s = IrSolver::solve(m);
    REQUIRE_FALSE(s.ok);
}

TEST_CASE("solver: missing sinks fails cleanly", "[solver]") {
    IrMesh m;
    m.nodes.push_back({0, 0, 0, 0, 0});
    m.source_node_ids.push_back(0);
    auto s = IrSolver::solve(m);
    REQUIRE_FALSE(s.ok);
}

TEST_CASE("solver: 2-node, 1A through G=1000S gives 1mV drop", "[solver]") {
    auto m = make_two_node(1000.0);  // 1 mΩ between source and sink
    auto s = IrSolver::solve(m, {1.0});  // 1A
    REQUIRE(s.ok);
    REQUIRE(s.voltages.size() == 2);
    REQUIRE(s.voltages[0] == Approx(1.0e-3).epsilon(1e-6));  // V = I/G = 1mV
    REQUIRE(s.voltages[1] == Approx(0.0).margin(1e-10));     // sink pinned ~0
}

TEST_CASE("solver: 3-node series, middle is halfway", "[solver]") {
    auto m = make_three_node_series(1000.0);
    auto s = IrSolver::solve(m, {1.0});
    REQUIRE(s.ok);
    // V_src = I * (R + R) = 1A * 2 * 1mΩ = 2mV
    // V_mid = 1mV (halfway)
    REQUIRE(s.voltages[0] == Approx(2.0e-3).epsilon(1e-6));
    REQUIRE(s.voltages[1] == Approx(1.0e-3).epsilon(1e-6));
    REQUIRE(s.voltages[2] == Approx(0.0).margin(1e-10));
}

TEST_CASE("solver: total_current scales linearly", "[solver]") {
    auto m = make_two_node(1000.0);
    auto s1 = IrSolver::solve(m, {1.0});
    auto s5 = IrSolver::solve(m, {5.0});
    REQUIRE(s1.ok);
    REQUIRE(s5.ok);
    REQUIRE(s5.voltages[0] / s1.voltages[0] == Approx(5.0).epsilon(1e-6));
}

TEST_CASE("solver: parallel paths cut effective resistance", "[solver]") {
    // 4 nodes in a square. Source = 0, sink = 3. Two parallel paths.
    IrMesh m;
    for (int i = 0; i < 4; ++i) m.nodes.push_back({i, 0, 0, i, 0});
    const double G = 1000.0;
    m.resistors.push_back({0, 1, G});  // path A: 0-1-3
    m.resistors.push_back({1, 3, G});
    m.resistors.push_back({0, 2, G});  // path B: 0-2-3
    m.resistors.push_back({2, 3, G});
    m.source_node_ids.push_back(0);
    m.sink_node_ids.push_back(3);

    auto s = IrSolver::solve(m, {1.0});
    REQUIRE(s.ok);
    // Each path: 2 series resistors → R = 2 * 1mΩ = 2mΩ.
    // Two paths in parallel: R_eq = 1mΩ. V_src = I * R_eq = 1mV.
    REQUIRE(s.voltages[0] == Approx(1.0e-3).epsilon(1e-3));
}

TEST_CASE("solver: end-to-end with mesher on a 10mm square zone", "[solver][e2e]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VCC"});
    Zone z;
    z.net_id = 1;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
    z.filled.push_back(p);
    b.zones.push_back(z);

    // Two pads at opposite ends to set source/sink.
    Pad p_left;  p_left.at  = {0.0005, 0.005}; p_left.net_id  = 1; p_left.layer_ordinals  = {0};
    Pad p_right; p_right.at = {0.0095, 0.005}; p_right.net_id = 1; p_right.layer_ordinals = {0};
    b.pads.push_back(p_left);
    b.pads.push_back(p_right);

    MeshConfig mc;
    mc.cell_size = 1.0e-3;
    mc.net_id = 1;
    mc.layer_ordinal = 0;
    auto mesh = IrMesher::build(b, mc);
    REQUIRE(mesh.nodes.size() == 100);
    REQUIRE(mesh.source_node_ids.size() == 1);
    REQUIRE(mesh.sink_node_ids.size() == 1);

    auto sol = IrSolver::solve(mesh, {1.0});
    REQUIRE(sol.ok);

    // Sanity bounds for sheet R of 35um copper over a 10×10 square ~ 0.48mΩ.
    // Source voltage with 1A injection should be on the order of a fraction
    // of a mV (it depends on point-to-point sheet resistance which is geometry
    // dependent; spreading is significant, so V_src < pure sheet R).
    REQUIRE(sol.max_v > 0.0);
    REQUIRE(sol.max_v < 5.0e-3);   // < 5 mV — well below the bulk sheet-R bound
    REQUIRE(sol.min_v == Approx(0.0).margin(1e-9));
}

TEST_CASE("solver: explicit node_currents (multi-source/sink balance)", "[solver]") {
    // 5-node line: 0 - 1 - 2 - 3 - 4, conductance G between each pair.
    IrMesh m;
    for (int i = 0; i < 5; ++i) m.nodes.push_back({i, double(i), 0, i, 0});
    const double G = 1000.0;
    for (int i = 0; i < 4; ++i) m.resistors.push_back({i, i + 1, G});
    // Two sources (0 and 4 each injecting 0.5A), one sink (2 drawing 1.0A).
    m.node_currents = {{0, 0.5}, {4, 0.5}, {2, -1.0}};

    auto s = IrSolver::solve(m, {});
    REQUIRE(s.ok);
    // Center node (pinned sink) ~0. Ends symmetric.
    REQUIRE(s.voltages[2] == Approx(0.0).margin(1e-9));
    REQUIRE(s.voltages[0] == Approx(s.voltages[4]).margin(1e-6));
    REQUIRE(s.voltages[0] > 0.0);
}

TEST_CASE("solver: explicit currents must sum to zero", "[solver]") {
    IrMesh m;
    for (int i = 0; i < 3; ++i) m.nodes.push_back({i, double(i), 0, i, 0});
    m.resistors.push_back({0, 1, 1000});
    m.resistors.push_back({1, 2, 1000});
    // Sum = 0.5 (charge accumulates) -- solver should refuse.
    m.node_currents = {{0, 1.0}, {2, -0.5}};

    auto s = IrSolver::solve(m, {});
    REQUIRE_FALSE(s.ok);
}

TEST_CASE("solver: explicit currents need at least one sink", "[solver]") {
    IrMesh m;
    for (int i = 0; i < 3; ++i) m.nodes.push_back({i, double(i), 0, i, 0});
    m.resistors.push_back({0, 1, 1000});
    m.resistors.push_back({1, 2, 1000});
    // All positive -- no ground reference.
    m.node_currents = {{0, 1.0}, {2, 1.0}};

    auto s = IrSolver::solve(m, {});
    REQUIRE_FALSE(s.ok);
}
