// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/ViaMesher.h"

using circuitcore::ui::ViaMesher;
using circuitcore::ui::PadMesher;
using namespace circuitcore::board;

TEST_CASE("via: 2-layer through-via produces a disk on each copper layer", "[via]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});

    Via v;
    v.at = {0.005, 0.005};
    v.outer_diameter = 0.8e-3;
    v.drill = 0.4e-3;
    v.from_layer = 0;
    v.to_layer = 31;
    b.vias.push_back(v);

    auto m = ViaMesher::build(b);
    REQUIRE(m.size() == 3);  // 2 copper + 1 drill pseudo-layer
    for (const auto& lm : m) {
        REQUIRE(lm.vertex_count() == 33);   // 1 center + 32 rim
        REQUIRE(lm.triangle_count() == 32); // 32-sided fan
    }
}

TEST_CASE("via: 4-layer through-via covers all intermediate copper", "[via]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu",  "signal"});
    b.stackup.layers.push_back({1,  "In1.Cu","power"});
    b.stackup.layers.push_back({2,  "In2.Cu","mixed"});
    b.stackup.layers.push_back({31, "B.Cu",  "signal"});

    Via v;
    v.at = {0, 0};
    v.outer_diameter = 0.6e-3;
    v.from_layer = 0;
    v.to_layer = 31;
    b.vias.push_back(v);

    auto m = ViaMesher::build(b);
    REQUIRE(m.size() == 4);  // F.Cu, In1, In2, B.Cu (no drill since v.drill = 0)
}

TEST_CASE("via: blind via covers only its endpoint range", "[via]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu",  "signal"});
    b.stackup.layers.push_back({1,  "In1.Cu","power"});
    b.stackup.layers.push_back({2,  "In2.Cu","mixed"});
    b.stackup.layers.push_back({31, "B.Cu",  "signal"});

    Via v;
    v.from_layer = 0;
    v.to_layer = 1;
    v.outer_diameter = 0.4e-3;
    b.vias.push_back(v);

    auto m = ViaMesher::build(b);
    REQUIRE(m.size() == 2);  // F.Cu + In1 only
}

TEST_CASE("via: zero outer_diameter is skipped", "[via]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    Via v;
    v.from_layer = 0;
    v.to_layer = 0;
    v.outer_diameter = 0.0;
    b.vias.push_back(v);

    REQUIRE(ViaMesher::build(b).empty());
}

TEST_CASE("pad: pads draw a disk on each listed copper layer", "[pad]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({32, "F.SilkS", "user"});

    Pad p;
    p.at = {0.01, 0.02};
    p.layer_ordinals = {0, 32};
    b.pads.push_back(p);

    auto m = PadMesher::build(b);
    // Only the copper layer (0); silkscreen (32) is skipped.
    REQUIRE(m.size() == 1);
    REQUIRE(m[0].layer_ordinal == 0);
    REQUIRE(m[0].vertex_count() == 33);
}

TEST_CASE("pad: multiple pads on same layer aggregate", "[pad]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});

    for (int i = 0; i < 3; ++i) {
        Pad p;
        p.at = {0.001 * i, 0.0};
        p.layer_ordinals = {0};
        b.pads.push_back(p);
    }

    auto m = PadMesher::build(b);
    REQUIRE(m.size() == 1);
    REQUIRE(m[0].vertex_count() == 99);   // 3 * 33
    REQUIRE(m[0].triangle_count() == 96); // 3 * 32
}
