// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"

using namespace circuitcore::board;

TEST_CASE("model: empty board defaults", "[model]") {
    Board b;
    REQUIRE(b.nets.empty());
    REQUIRE(b.segments.empty());
    REQUIRE(b.vias.empty());
    REQUIRE(b.zones.empty());
    REQUIRE(b.stackup.layers.empty());
    REQUIRE(b.stackup.total_thickness == 1.6e-3);
}

TEST_CASE("model: find_net by id and name", "[model]") {
    Board b;
    b.nets.push_back({0, ""});
    b.nets.push_back({1, "GND"});
    b.nets.push_back({2, "+3V3"});

    REQUIRE(b.find_net(1)->name == "GND");
    REQUIRE(b.find_net(99) == nullptr);
    REQUIRE(b.find_net_by_name("+3V3")->id == 2);
    REQUIRE(b.find_net_by_name("missing") == nullptr);
}

TEST_CASE("model: find_layer and is_copper", "[model]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu",      "signal"});
    b.stackup.layers.push_back({31, "B.Cu",      "signal"});
    b.stackup.layers.push_back({32, "F.SilkS",   "user"});

    REQUIRE(b.find_layer(0)->name == "F.Cu");
    REQUIRE(b.find_layer(31)->name == "B.Cu");
    REQUIRE(b.find_layer(99) == nullptr);
    REQUIRE(b.find_layer(0)->is_copper());
    REQUIRE(b.find_layer(31)->is_copper());
    REQUIRE_FALSE(b.find_layer(32)->is_copper());
}

TEST_CASE("model: polygon with holes", "[model]") {
    Zone z;
    z.net_id = 1;
    z.net_name = "GND";
    z.layer_ordinal = 0;

    Polygon p;
    p.outline = {{0, 0}, {0.1, 0}, {0.1, 0.1}, {0, 0.1}};
    p.holes.push_back({{0.04, 0.04}, {0.06, 0.04}, {0.06, 0.06}, {0.04, 0.06}});
    z.filled.push_back(p);

    REQUIRE(z.filled.size() == 1);
    REQUIRE(z.filled[0].outline.size() == 4);
    REQUIRE(z.filled[0].holes.size() == 1);
    REQUIRE(z.filled[0].holes[0].size() == 4);
}
