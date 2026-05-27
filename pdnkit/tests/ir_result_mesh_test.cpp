// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "render/IrResultMesh.h"

using pdnkit::pi::IrMesh;
using pdnkit::pi::Solution;
using pdnkit::render::build_ir_result_mesh;
using Catch::Approx;

TEST_CASE("ir-result-mesh: empty solution → empty mesh", "[result-mesh]") {
    IrMesh m;
    Solution s;
    auto r = build_ir_result_mesh(m, s, 1.0e-3);
    REQUIRE(r.vertices.empty());
}

TEST_CASE("ir-result-mesh: per-node quad with normalized voltage", "[result-mesh]") {
    IrMesh mesh;
    mesh.nodes.push_back({0, 0.0,   0.0, 0, 0});
    mesh.nodes.push_back({1, 0.001, 0.0, 1, 0});

    Solution sol;
    sol.ok = true;
    sol.voltages = {0.002, 0.0};  // 2mV and 0V
    sol.min_v = 0.0;
    sol.max_v = 0.002;

    auto r = build_ir_result_mesh(mesh, sol, 1.0e-3);
    REQUIRE(r.vertex_count() == 8);   // 2 nodes × 4 verts
    REQUIRE(r.indices.size() == 12);  // 2 nodes × 6 indices

    // Node 0 (voltage 2mV) → normalized t = 1.0; vertex stride is x,y,t.
    REQUIRE(r.vertices[2]  == Approx(1.0f));   // first vert of node 0, t
    REQUIRE(r.vertices[5]  == Approx(1.0f));
    REQUIRE(r.vertices[8]  == Approx(1.0f));
    REQUIRE(r.vertices[11] == Approx(1.0f));

    // Node 1 (voltage 0V) → t = 0.0.
    REQUIRE(r.vertices[14] == Approx(0.0f));
    REQUIRE(r.vertices[17] == Approx(0.0f));
}

TEST_CASE("ir-result-mesh: zero-span solution clamps t to 0", "[result-mesh]") {
    IrMesh mesh;
    mesh.nodes.push_back({0, 0, 0, 0, 0});
    Solution sol;
    sol.ok = true;
    sol.voltages = {0.0};
    sol.min_v = 0.0;
    sol.max_v = 0.0;

    auto r = build_ir_result_mesh(mesh, sol, 1.0e-3);
    REQUIRE(r.vertex_count() == 4);
    REQUIRE(r.vertices[2] == Approx(0.0f));
}


// Hotspot field: the voltage builder marks the node with the lowest V,
// the current-density builder marks the node with the largest |J|
// (excluding source/sink boundary nodes whose gradients are inflated
// by the discretized boundary condition).
TEST_CASE("ir-result-mesh: hotspot points at the worst node (voltage)",
          "[result-mesh][hotspot]") {
    IrMesh mesh;
    mesh.nodes.push_back({0, 0.0,   0.0, 0, 0, 0});
    mesh.nodes.push_back({1, 0.001, 0.0, 1, 0, 0});
    mesh.nodes.push_back({2, 0.002, 0.0, 2, 0, 0});

    Solution sol;
    sol.ok = true;
    sol.voltages = {1.0, 0.4, 0.05};  // node 2 is the worst-drop
    sol.min_v = 0.05;
    sol.max_v = 1.0;

    auto r = build_ir_result_mesh(mesh, sol, 1.0e-3);
    REQUIRE(r.hotspot.valid);
    REQUIRE(r.hotspot.x == Approx(0.002));
    REQUIRE(r.hotspot.y == Approx(0.0));
    REQUIRE(r.hotspot.value == Approx(0.05));
    REQUIRE_FALSE(r.hotspot.is_current);
}
