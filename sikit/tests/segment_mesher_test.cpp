// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/SegmentMesher.h"

using circuitcore::ui::SegmentMesher;
using circuitcore::ui::build_all_meshes;
using namespace circuitcore::board;
using Catch::Approx;

namespace {
Board with_layer(int ord) {
    Board b;
    b.stackup.layers.push_back({ord, "F.Cu", "signal"});
    return b;
}
}

TEST_CASE("segments: horizontal segment yields 4 vertices, 2 triangles", "[segments]") {
    Board b = with_layer(0);
    Segment s;
    s.start = {0.0, 0.0};
    s.end   = {0.010, 0.0};   // 10mm right
    s.width = 0.001;          // 1mm wide
    s.layer_ordinal = 0;
    b.segments.push_back(s);

    auto m = SegmentMesher::build(b);
    REQUIRE(m.size() == 1);
    REQUIRE(m[0].vertex_count() == 54);  // 4 rect + 2 * (1 + 24-side cap)
    REQUIRE(m[0].triangle_count() == 50);  // 2 rect + 2 * 24 cap
    // Width is along Y; first vertex should be at (0, +0.5mm).
    REQUIRE(m[0].vertices[0] == Approx(0.0f));
    REQUIRE(m[0].vertices[1] == Approx(0.0005f));   // a + perp*hw
    REQUIRE(m[0].vertices[2] == Approx(0.0f));
    REQUIRE(m[0].vertices[3] == Approx(-0.0005f));  // a - perp*hw
}

TEST_CASE("segments: vertical segment width offset along X", "[segments]") {
    Board b = with_layer(0);
    Segment s;
    s.start = {0.005, 0.0};
    s.end   = {0.005, 0.010};
    s.width = 0.002;
    s.layer_ordinal = 0;
    b.segments.push_back(s);

    auto m = SegmentMesher::build(b);
    REQUIRE(m.size() == 1);
    REQUIRE(m[0].vertex_count() == 54);  // 4 rect + 2 * (1 + 24-side cap)
    // perp of (0, +1) is (-1, 0). First vertex = (5mm - 1mm, 0) = (4mm, 0).
    REQUIRE(m[0].vertices[0] == Approx(0.004f));
    REQUIRE(m[0].vertices[1] == Approx(0.0f));
}

TEST_CASE("segments: zero-length and zero-width are skipped", "[segments]") {
    Board b = with_layer(0);
    Segment zero_len{};
    zero_len.start = {0.0, 0.0};
    zero_len.end   = {0.0, 0.0};
    zero_len.width = 0.001;
    zero_len.layer_ordinal = 0;
    b.segments.push_back(zero_len);

    Segment zero_w{};
    zero_w.start = {0, 0};
    zero_w.end = {1, 0};
    zero_w.width = 0.0;
    zero_w.layer_ordinal = 0;
    b.segments.push_back(zero_w);

    auto m = SegmentMesher::build(b);
    REQUIRE(m.empty());
}

TEST_CASE("segments: non-copper layer is skipped", "[segments]") {
    Board b;
    b.stackup.layers.push_back({32, "F.SilkS", "user"});
    Segment s;
    s.start = {0, 0};
    s.end = {1, 0};
    s.width = 0.001;
    s.layer_ordinal = 32;
    b.segments.push_back(s);

    REQUIRE(SegmentMesher::build(b).empty());
}

TEST_CASE("segments: many segments on same layer aggregate", "[segments]") {
    Board b = with_layer(0);
    for (int i = 0; i < 5; ++i) {
        Segment s;
        s.start = {0.001 * i, 0.0};
        s.end   = {0.001 * i, 0.010};
        s.width = 0.0005;
        s.layer_ordinal = 0;
        b.segments.push_back(s);
    }

    auto m = SegmentMesher::build(b);
    REQUIRE(m.size() == 1);
    REQUIRE(m[0].vertex_count() == 270);   // 5 * (4 rect + 2*(1+24) cap)
    REQUIRE(m[0].triangle_count() == 250); // 5 * (2 rect + 2*24 cap)
}

TEST_CASE("build_all_meshes: combines zones and segments on same layer", "[segments]") {
    Board b = with_layer(0);

    // One square zone on F.Cu (4 verts, 2 triangles after earcut).
    Zone z;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    z.filled.push_back(p);
    b.zones.push_back(z);

    // One segment on F.Cu (4 verts, 2 triangles).
    Segment s;
    s.start = {0.0, 2.0};
    s.end   = {1.0, 2.0};
    s.width = 0.1;
    s.layer_ordinal = 0;
    b.segments.push_back(s);

    auto m = build_all_meshes(b);
    REQUIRE(m.size() == 1);
    REQUIRE(m[0].layer_ordinal == 0);
    REQUIRE(m[0].vertex_count() == 58);   // 4 zone + 1 seg (4 rect + 2*(1+24) cap)
    REQUIRE(m[0].triangle_count() == 52); // 2 zone + 1 seg (2 rect + 2*24 cap)
}

TEST_CASE("build_all_meshes: zones-only and segments-only layers stay separate", "[segments]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});

    // Zone on F.Cu only
    Zone z;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    z.filled.push_back(p);
    b.zones.push_back(z);

    // Segment on B.Cu only
    Segment s;
    s.start = {0, 0};
    s.end = {1, 0};
    s.width = 0.1;
    s.layer_ordinal = 31;
    b.segments.push_back(s);

    auto m = build_all_meshes(b);
    REQUIRE(m.size() == 2);
}
