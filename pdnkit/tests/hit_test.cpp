// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "circuitcore/board/HitTest.h"

using circuitcore::board::hittest::Hit;
using circuitcore::board::hittest::at_point;
using namespace circuitcore::board;

namespace {
Board with_copper(int ord = 0) {
    Board b;
    b.stackup.layers.push_back({ord, "F.Cu", "signal"});
    return b;
}
}

TEST_CASE("hittest: empty board misses", "[hittest]") {
    Board b;
    auto h = at_point(b, {0, 0}, 0.001);
    REQUIRE(h.kind == Hit::Kind::None);
}

TEST_CASE("hittest: zone polygon hit", "[hittest]") {
    Board b = with_copper(0);
    Zone z;
    z.net_id = 5;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {0.1, 0}, {0.1, 0.1}, {0, 0.1}};
    z.filled.push_back(p);
    b.zones.push_back(z);

    auto h = at_point(b, {0.05, 0.05}, 0);
    REQUIRE(h.kind == Hit::Kind::Zone);
    REQUIRE(h.net_id == 5);
    REQUIRE(h.layer_ordinal == 0);

    // Outside: miss
    REQUIRE(at_point(b, {0.2, 0.2}, 0).kind == Hit::Kind::None);
}

TEST_CASE("hittest: zone hole is treated as not-inside", "[hittest]") {
    Board b = with_copper(0);
    Zone z;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {0.1, 0}, {0.1, 0.1}, {0, 0.1}};
    p.holes.push_back({{0.04, 0.04}, {0.06, 0.04}, {0.06, 0.06}, {0.04, 0.06}});
    z.filled.push_back(p);
    b.zones.push_back(z);

    // Point in outline but in hole: miss
    REQUIRE(at_point(b, {0.05, 0.05}, 0).kind == Hit::Kind::None);
    // Point in outline but outside hole: zone hit
    REQUIRE(at_point(b, {0.02, 0.02}, 0).kind == Hit::Kind::Zone);
}

TEST_CASE("hittest: segment hit within width/2 + pick radius", "[hittest]") {
    Board b = with_copper(0);
    Segment s;
    s.start = {0.0, 0.0};
    s.end = {0.020, 0.0};
    s.width = 0.001;  // 1mm
    s.net_id = 7;
    s.layer_ordinal = 0;
    b.segments.push_back(s);

    // On centerline mid-segment: hit
    REQUIRE(at_point(b, {0.010, 0.0}, 0).kind == Hit::Kind::Segment);
    // 0.6mm above centerline (> width/2 = 0.5mm), no pick radius: miss
    REQUIRE(at_point(b, {0.010, 0.0006}, 0).kind == Hit::Kind::None);
    // Same point with 0.2mm pick radius (0.5 + 0.2 = 0.7mm tolerance): hit
    auto h = at_point(b, {0.010, 0.0006}, 0.0002);
    REQUIRE(h.kind == Hit::Kind::Segment);
    REQUIRE(h.net_id == 7);
}

TEST_CASE("hittest: via hit within outer radius", "[hittest]") {
    Board b = with_copper(0);
    Via v;
    v.at = {0.005, 0.005};
    v.outer_diameter = 0.0008;  // 0.8mm → radius 0.4mm
    v.from_layer = 0;
    v.to_layer = 31;
    v.net_id = 11;
    b.vias.push_back(v);

    // Center: hit
    REQUIRE(at_point(b, {0.005, 0.005}, 0).kind == Hit::Kind::Via);
    // 0.3mm away: still in radius, hit
    REQUIRE(at_point(b, {0.0053, 0.005}, 0).kind == Hit::Kind::Via);
    // 0.5mm away: miss
    REQUIRE(at_point(b, {0.0055, 0.005}, 0).kind == Hit::Kind::None);
}

TEST_CASE("hittest: pad takes priority over zone", "[hittest]") {
    Board b = with_copper(0);

    // Big zone covering origin
    Zone z;
    z.net_id = 1;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    z.filled.push_back(p);
    b.zones.push_back(z);

    // Pad at origin
    Pad pad;
    pad.at = {0, 0};
    pad.layer_ordinals = {0};
    pad.net_id = 42;
    b.pads.push_back(pad);

    auto h = at_point(b, {0, 0}, 0);
    REQUIRE(h.kind == Hit::Kind::Pad);
    REQUIRE(h.net_id == 42);  // pad's net, not zone's
}

TEST_CASE("hittest: name() handles all kinds", "[hittest]") {
    REQUIRE(std::string(circuitcore::board::hittest::name(Hit::Kind::Pad)) == "pad");
    REQUIRE(std::string(circuitcore::board::hittest::name(Hit::Kind::Via)) == "via");
    REQUIRE(std::string(circuitcore::board::hittest::name(Hit::Kind::Segment)) == "segment");
    REQUIRE(std::string(circuitcore::board::hittest::name(Hit::Kind::Zone)) == "zone");
    REQUIRE(std::string(circuitcore::board::hittest::name(Hit::Kind::None)) == "");
}

TEST_CASE("hittest: rect pad respects width/height + rotation", "[hittest]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    Pad p;
    p.at = {0.0, 0.0};
    p.size = {0.004, 0.001};  // 4mm wide, 1mm tall
    p.shape = circuitcore::board::PadShape::Rect;
    p.rotation = 0.0;
    p.layer_ordinals = {0};
    p.net_id = 9;
    b.pads.push_back(p);

    // Inside the wide direction: hit.
    REQUIRE(at_point(b, {0.0015, 0.0}, 0).kind == Hit::Kind::Pad);
    // Beyond width: miss.
    REQUIRE(at_point(b, {0.0025, 0.0}, 0).kind == Hit::Kind::None);
    // Inside height: hit.
    REQUIRE(at_point(b, {0.0, 0.0004}, 0).kind == Hit::Kind::Pad);
    // Beyond height: miss.
    REQUIRE(at_point(b, {0.0, 0.0008}, 0).kind == Hit::Kind::None);
}

TEST_CASE("hittest: rect pad rotated 90deg swaps W/H sensitivity", "[hittest]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    Pad p;
    p.at = {0.0, 0.0};
    p.size = {0.004, 0.001};
    p.shape = circuitcore::board::PadShape::Rect;
    p.rotation = 1.5707963267948966;  // 90deg
    p.layer_ordinals = {0};
    b.pads.push_back(p);

    // After 90deg rotation, the 4mm direction is now Y. So (0, 0.0015) should hit.
    REQUIRE(at_point(b, {0.0, 0.0015}, 0).kind == Hit::Kind::Pad);
    // And (0.0008, 0) should miss because the rotated tall direction (now X)
    // is only 1mm tall, so 0.8mm is outside half-extent 0.5mm.
    REQUIRE(at_point(b, {0.0008, 0.0}, 0).kind == Hit::Kind::None);
}
