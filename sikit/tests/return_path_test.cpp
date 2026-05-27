// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "si/ReturnPath.h"
#include "circuitcore/board/Board.h"

using sikit::si::detect_return_path_violations;
using sikit::si::ReturnPathViolation;
using Catch::Approx;

namespace {

using namespace circuitcore::board;

// Build a board with F.Cu + In1.Cu copper layers, optionally a copper
// pour on In1.Cu covering a chosen rectangle, and one signal segment on
// F.Cu running along Y = 0 from x_start to x_end.
struct Toy {
    Board board;
    int signal_net_id = 1;
    int ground_net_id = 2;
};

Toy make_toy(double trace_x0, double trace_x1,
              double ref_x0 = -1, double ref_x1 = -1,
              double ref_y0 = -1, double ref_y1 = -1) {
    Toy t;
    t.board.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6, "copper", 0, 0});
    t.board.stackup.layers.push_back({1, "In1.Cu", "power", 35e-6, "copper", 0, 0});
    t.board.nets.push_back({1, "DATA"});
    t.board.nets.push_back({2, "GND"});

    Segment s;
    s.start = {trace_x0, 0};
    s.end   = {trace_x1, 0};
    s.width = 0.20e-3;
    s.layer_ordinal = 0;
    s.net_id = 1;
    t.board.segments.push_back(s);

    if (ref_x0 < ref_x1 && ref_y0 < ref_y1) {
        Zone z;
        z.net_id = 2;
        z.net_name = "GND";
        z.layer_ordinal = 1;
        Polygon filled;
        filled.outline = {
            {ref_x0, ref_y0}, {ref_x1, ref_y0},
            {ref_x1, ref_y1}, {ref_x0, ref_y1},
        };
        z.filled.push_back(filled);
        t.board.zones.push_back(z);
    }
    return t;
}

}  // namespace

TEST_CASE("return-path: trace fully over a covering plane has no violations",
          "[returnpath]") {
    // 50 mm trace, plane covers 100 x 20 mm region centred on origin.
    auto t = make_toy(/*trace*/ -25e-3, 25e-3,
                       /*ref*/   -50e-3, 50e-3, -10e-3, 10e-3);
    auto v = detect_return_path_violations(t.board);
    REQUIRE(v.empty());
}

TEST_CASE("return-path: board with no reference plane flags the segment",
          "[returnpath]") {
    // No zone on In1.Cu at all -> every sample point is off-plane.
    auto t = make_toy(0, 50e-3);
    auto v = detect_return_path_violations(t.board);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].off_plane_fraction == Approx(1.0));
    REQUIRE(v[0].reference_layer == 1);   // In1.Cu was the candidate
}

TEST_CASE("return-path: trace half off the plane reports ~0.5 fraction",
          "[returnpath]") {
    // Trace from x=0 to x=100 mm. Plane covers x=0..50 mm at y=0..10mm.
    // Half the samples are on the plane, half are off.
    auto t = make_toy(0, 100e-3, 0, 50e-3, -10e-3, 10e-3);
    auto v = detect_return_path_violations(t.board);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].off_plane_fraction > 0.3);
    REQUIRE(v[0].off_plane_fraction < 0.7);
    // severity == off_plane_fraction * length
    REQUIRE(v[0].severity_m ==
            Approx(v[0].off_plane_fraction * 100e-3).margin(1e-9));
}

TEST_CASE("return-path: threshold filters out near-clean traces",
          "[returnpath]") {
    // Plane covers x=0..49.9 mm so only the very last sample is off.
    auto t = make_toy(0, 50e-3, 0, 49.9e-3, -10e-3, 10e-3);
    // Default threshold is 5%. With 20 samples, 1/20 = 5% exactly.
    auto v_default = detect_return_path_violations(t.board);
    // Should be borderline; with a slightly looser threshold the
    // violation goes away entirely.
    auto v_loose = detect_return_path_violations(t.board, 20, 0.10);
    REQUIRE(v_loose.empty());
}

TEST_CASE("return-path: violations sorted worst-first", "[returnpath]") {
    // Two traces: trace A is half off the plane (50%), trace B is
    // entirely off the plane (100%). Violation B should sort first.
    auto t = make_toy(0, 100e-3, 0, 50e-3, -10e-3, 10e-3);  // half off
    // Add trace B that's entirely off the plane (very far away).
    Segment b;
    b.start = {500e-3, 500e-3};
    b.end   = {600e-3, 500e-3};
    b.width = 0.20e-3;
    b.layer_ordinal = 0;
    b.net_id = 1;
    t.board.segments.push_back(b);
    auto v = detect_return_path_violations(t.board);
    REQUIRE(v.size() == 2);
    REQUIRE(v[0].off_plane_fraction > v[1].off_plane_fraction);
}

TEST_CASE("return-path: non-routable nets are skipped", "[returnpath]") {
    auto t = make_toy(0, 50e-3);
    t.board.segments[0].net_id = 0;   // unrouted
    auto v = detect_return_path_violations(t.board);
    REQUIRE(v.empty());
}

TEST_CASE("return-path: single-copper-layer board reports the segment as "
          "lacking any reference", "[returnpath]") {
    // Strip the In1.Cu layer entirely.
    auto t = make_toy(0, 50e-3);
    t.board.stackup.layers.erase(t.board.stackup.layers.begin() + 1);
    auto v = detect_return_path_violations(t.board);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].reference_layer == -1);
    REQUIRE(v[0].off_plane_fraction == Approx(1.0));
}

TEST_CASE("return-path: zero-length segment is skipped", "[returnpath]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6, "copper", 0, 0});
    b.stackup.layers.push_back({1, "In1.Cu", "power", 35e-6, "copper", 0, 0});
    b.nets.push_back({1, "DATA"});
    Segment s;
    s.start = {0, 0};
    s.end   = {0, 0};
    s.width = 0.20e-3;
    s.layer_ordinal = 0;
    s.net_id = 1;
    b.segments.push_back(s);
    auto v = detect_return_path_violations(b);
    REQUIRE(v.empty());
}
