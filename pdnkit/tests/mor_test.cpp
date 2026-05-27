// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/Mor.h"

using namespace pdnkit::pi;
using Catch::Approx;

namespace {

// Build a simple three-node mesh:
//   N0 --R01--+--R12-- N2
//             |
//             N1
//
// With R01 = R12 = 10 ohm. Keeping ports {N0, N2} and eliminating N1
// gives a single series-combined R of 20 ohm, so G_red = [[0.05, -0.05],
// [-0.05, 0.05]].
IrMesh chain_3() {
    IrMesh m;
    m.nodes.push_back({0, 0.0, 0.0, 0, 0, 0});
    m.nodes.push_back({1, 0.0, 0.0, 1, 0, 0});
    m.nodes.push_back({2, 0.0, 0.0, 2, 0, 0});
    m.resistors.push_back({0, 1, 0.1});   // 10 ohm
    m.resistors.push_back({1, 2, 0.1});   // 10 ohm
    return m;
}
}  // namespace

TEST_CASE("mor: empty inputs return empty", "[mor]") {
    IrMesh m;
    auto r = reduce_to_ports(m, {0, 1});
    REQUIRE(r.port_node_ids.empty());
}

TEST_CASE("mor: chain mesh reduces correctly", "[mor][validation]") {
    auto m = chain_3();
    auto r = reduce_to_ports(m, {0, 2});
    REQUIRE(r.port_node_ids.size() == 2);
    REQUIRE(r.G_reduced.rows() == 2);
    REQUIRE(r.G_reduced.cols() == 2);
    // Series 10 + 10 = 20 ohm -> G = 0.05.
    REQUIRE(r.G_reduced(0, 0) == Approx( 0.05));
    REQUIRE(r.G_reduced(1, 1) == Approx( 0.05));
    REQUIRE(r.G_reduced(0, 1) == Approx(-0.05));
    REQUIRE(r.G_reduced(1, 0) == Approx(-0.05));
}

// Parallel: N0 --R01-- N2 directly and N0 --R02-- N2 (via N1).
TEST_CASE("mor: parallel paths combine correctly", "[mor][validation]") {
    IrMesh m;
    m.nodes.push_back({0, 0.0, 0.0, 0, 0, 0});
    m.nodes.push_back({1, 0.0, 0.0, 1, 0, 0});
    m.nodes.push_back({2, 0.0, 0.0, 2, 0, 0});
    m.resistors.push_back({0, 2, 0.1});   // direct 10 ohm
    m.resistors.push_back({0, 1, 0.1});   // 10 ohm
    m.resistors.push_back({1, 2, 0.1});   // 10 ohm
    auto r = reduce_to_ports(m, {0, 2});
    // 10 ohm in parallel with 20 ohm: 1 / (0.1 + 0.05) = 1/0.15 = 6.67.
    // G = 0.15.
    REQUIRE(r.G_reduced(0, 1) == Approx(-0.15));
    REQUIRE(r.G_reduced(0, 0) == Approx( 0.15));
}

// All nodes are kept -> no reduction, returns the full G unchanged.
TEST_CASE("mor: keeping all nodes is identity", "[mor]") {
    auto m = chain_3();
    auto r = reduce_to_ports(m, {0, 1, 2});
    REQUIRE(r.G_reduced.rows() == 3);
    // Diagonal: node 0 sees only the 10 ohm to N1 -> G_diag = 0.1.
    REQUIRE(r.G_reduced(0, 0) == Approx(0.1));
    REQUIRE(r.G_reduced(1, 1) == Approx(0.2));   // 0.1 to each side
    REQUIRE(r.G_reduced(2, 2) == Approx(0.1));
}

TEST_CASE("mor: SPICE export of reduced network", "[mor]") {
    auto m = chain_3();
    auto r = reduce_to_ports(m, {0, 2});
    auto s = export_reduced_spice(r, "test");
    // Series 20 ohm between the two ports.
    REQUIRE(s.find("R0 NP0 NP1 2.000000e+01") != std::string::npos);
    REQUIRE(s.find(".end") != std::string::npos);
}
