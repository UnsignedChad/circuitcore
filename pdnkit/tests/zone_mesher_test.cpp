// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/ZoneMesher.h"

using circuitcore::ui::ZoneMesher;
using circuitcore::ui::LayerMesh;
using namespace circuitcore::board;

namespace {

Board make_board_with_copper_layer(int layer_ordinal) {
    Board b;
    b.stackup.layers.push_back({layer_ordinal, "F.Cu", "signal"});
    return b;
}

Zone square_zone(int layer_ordinal, double size_m) {
    Zone z;
    z.net_id = 1;
    z.layer_ordinal = layer_ordinal;
    Polygon p;
    p.outline = {{0, 0}, {size_m, 0}, {size_m, size_m}, {0, size_m}};
    z.filled.push_back(p);
    return z;
}

}  // namespace

TEST_CASE("mesher: empty board produces no meshes", "[mesher]") {
    Board b;
    auto m = ZoneMesher::build(b);
    REQUIRE(m.empty());
}

TEST_CASE("mesher: single square zone yields 4 vertices and 2 triangles", "[mesher]") {
    Board b = make_board_with_copper_layer(0);
    b.zones.push_back(square_zone(0, 0.10));
    auto meshes = ZoneMesher::build(b);
    REQUIRE(meshes.size() == 1);
    REQUIRE(meshes[0].layer_ordinal == 0);
    REQUIRE(meshes[0].vertex_count() == 4);
    REQUIRE(meshes[0].triangle_count() == 2);
    // Indices should reference vertices 0..3.
    for (auto i : meshes[0].indices) REQUIRE(i < 4);
}

TEST_CASE("mesher: square with square hole yields 8 vertices, 8 triangles", "[mesher]") {
    Board b = make_board_with_copper_layer(0);
    Zone z;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    p.holes.push_back({{0.4, 0.4}, {0.6, 0.4}, {0.6, 0.6}, {0.4, 0.6}});
    z.filled.push_back(p);
    b.zones.push_back(z);

    auto meshes = ZoneMesher::build(b);
    REQUIRE(meshes.size() == 1);
    REQUIRE(meshes[0].vertex_count() == 8);  // 4 outline + 4 hole
    REQUIRE(meshes[0].triangle_count() == 8);  // earcut: square-with-square-hole → 8
}

TEST_CASE("mesher: two zones on different layers → two meshes", "[mesher]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});
    b.zones.push_back(square_zone(0,  0.10));
    b.zones.push_back(square_zone(31, 0.05));
    auto meshes = ZoneMesher::build(b);
    REQUIRE(meshes.size() == 2);
    // Order is insertion order (encounter order in zones list).
    REQUIRE(meshes[0].layer_ordinal == 0);
    REQUIRE(meshes[1].layer_ordinal == 31);
}

TEST_CASE("mesher: two zones on same layer share one mesh", "[mesher]") {
    Board b = make_board_with_copper_layer(0);
    b.zones.push_back(square_zone(0, 0.10));
    b.zones.push_back(square_zone(0, 0.05));
    auto meshes = ZoneMesher::build(b);
    REQUIRE(meshes.size() == 1);
    REQUIRE(meshes[0].vertex_count() == 8);   // 4 + 4
    REQUIRE(meshes[0].triangle_count() == 4); // 2 + 2
}

TEST_CASE("mesher: non-copper layer is skipped", "[mesher]") {
    Board b;
    b.stackup.layers.push_back({32, "F.SilkS", "user"});
    b.zones.push_back(square_zone(32, 0.10));
    auto meshes = ZoneMesher::build(b);
    REQUIRE(meshes.empty());
}

TEST_CASE("mesher: degenerate (< 3 pts) outline is skipped without crashing", "[mesher]") {
    Board b = make_board_with_copper_layer(0);
    Zone z;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {1, 1}};  // only 2 points
    z.filled.push_back(p);
    b.zones.push_back(z);

    auto meshes = ZoneMesher::build(b);
    REQUIRE(meshes.empty());
}
