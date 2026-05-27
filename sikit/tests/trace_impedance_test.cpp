// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "si/TraceImpedance.h"
#include "si/SiStackup.h"

using namespace sikit::analysis;
using namespace circuitcore::board;
using Catch::Approx;

TEST_CASE("trace impedance: F.Cu uses microstrip, inner uses stripline", "[trace]") {
    AnalysisStackup s;
    const double W = 0.2e-3;

    auto outer = compute_one(W, 0, s);          // F.Cu
    auto inner = compute_one(W, 1, s);          // inner copper
    auto bottom = compute_one(W, 31, s);        // B.Cu

    REQUIRE(outer.z0 > 0.0);
    REQUIRE(inner.z0 > 0.0);
    REQUIRE(bottom.z0 > 0.0);

    // Outer and bottom should use the same formula (microstrip).
    REQUIRE(outer.z0 == Approx(bottom.z0));

    // For the typical defaults (H=0.2mm outer, B=0.4mm inner, εr=4.4),
    // both come out near 50–60Ω — they shouldn't be identical though,
    // since the formulas and the geometry differ.
    REQUIRE(outer.z0 != Approx(inner.z0));
}

TEST_CASE("trace impedance: zero width returns invalid", "[trace]") {
    AnalysisStackup s;
    auto r = compute_one(0.0, 0, s);
    REQUIRE(r.z0 == 0.0);
    REQUIRE_FALSE(r.in_valid_range);
}

TEST_CASE("trace impedance: compute_all skips non-copper segments", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0,  "F.Cu",    "signal"});
    b.stackup.layers.push_back({32, "F.SilkS", "user"});

    Segment cu;
    cu.start = {0, 0};
    cu.end   = {1e-3, 0};
    cu.width = 0.2e-3;
    cu.layer_ordinal = 0;
    cu.net_id = 1;
    b.segments.push_back(cu);

    Segment silk = cu;
    silk.layer_ordinal = 32;
    b.segments.push_back(silk);

    auto rs = compute_all(b, {});
    REQUIRE(rs.size() == 1);
    REQUIRE(rs[0].layer_ordinal == 0);
    REQUIRE(rs[0].net_id == 1);
}

TEST_CASE("trace impedance: per-segment index lines up with input order", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});

    for (int i = 0; i < 3; ++i) {
        Segment s;
        s.width = (0.1 + 0.05 * i) * 1e-3;
        s.layer_ordinal = 0;
        s.net_id = i + 1;
        b.segments.push_back(s);
    }

    auto rs = compute_all(b, {});
    REQUIRE(rs.size() == 3);
    REQUIRE(rs[0].segment_index == 0);
    REQUIRE(rs[1].segment_index == 1);
    REQUIRE(rs[2].segment_index == 2);
    // Narrower trace → higher impedance.
    REQUIRE(rs[0].z0 > rs[1].z0);
    REQUIRE(rs[1].z0 > rs[2].z0);
}

TEST_CASE("color_for_error: tolerance bands", "[trace]") {
    auto green  = color_for_error(50.0, 50.0);
    auto yellow = color_for_error(53.5, 50.0);   // 7% off
    auto red    = color_for_error(60.0, 50.0);   // 20% off

    REQUIRE(green.g  > green.r);
    REQUIRE(yellow.r > yellow.b);
    REQUIRE(yellow.g > yellow.b);
    REQUIRE(red.r    > red.g);
}

TEST_CASE("color_for_error: zero target returns gray", "[trace]") {
    auto c = color_for_error(50.0, 0.0);
    REQUIRE(c.r == Approx(0.5f));
    REQUIRE(c.g == Approx(0.5f));
    REQUIRE(c.b == Approx(0.5f));
}

TEST_CASE("from_board: empty stackup → generic FR-4 defaults", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;  // no stackup items
    auto s = AnalysisStackup::from_board(b, sis);
    REQUIRE_FALSE(s.from_real_stackup);
    REQUIRE(s.epsilon_r == 4.4);
    REQUIRE(s.outer_dielectric_height == 0.2e-3);
}

TEST_CASE("from_board: picks up dielectric below F.Cu", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    sikit::si::SiStackupItem cu{sikit::si::SiStackupItem::Kind::Copper, "F.Cu", 35e-6, 0, 0, ""};
    sikit::si::SiStackupItem die;
    die.kind = sikit::si::SiStackupItem::Kind::Dielectric;
    die.name = "dielectric 1";
    die.thickness = 0.150e-3;
    die.epsilon_r = 3.8;
    sis.items.push_back(cu);
    sis.items.push_back(die);

    auto s = AnalysisStackup::from_board(b, sis);
    REQUIRE(s.from_real_stackup);
    REQUIRE(s.outer_dielectric_height == Approx(0.150e-3));
    REQUIRE(s.epsilon_r == Approx(3.8));
    REQUIRE(s.copper_thickness == Approx(35e-6));
}

TEST_CASE("engine: FDM and closed-form agree to within ~25% on microstrip", "[trace]") {
    // Canonical 50Ω microstrip geometry; both engines should land in the
    // same neighbourhood (closed-form ~50, FDM ~50–60 at v0 mesh density).
    AnalysisStackup s;
    s.outer_dielectric_height = 1.524e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.4;

    const double W = 2.8e-3;
    auto cf = compute_one(W, 0, s);
    auto fdm = compute_one_fdm(W, 0, s);

    REQUIRE(cf.z0  > 0);
    REQUIRE(fdm.z0 > 0);
    const double rel = std::abs(fdm.z0 - cf.z0) / cf.z0;
    REQUIRE(rel < 0.25);
}

TEST_CASE("diff FDM: Z_diff in the right neighbourhood for ~100 Ω geometry", "[trace]") {
    // Edge-coupled microstrip diff pair on 1 mm FR-4, ~100 Ω target.
    AnalysisStackup s;
    s.outer_dielectric_height = 1.0e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.4;

    const double W = 1.5e-3;
    const double S = 0.5e-3;
    const double z_diff = compute_diff_z0_fdm(W, S, 0, s);

    // For a target geometry that produces ~80–120 Ω diff in FR-4 we expect
    // FDM to land somewhere in that band. Looser bound than single-ended
    // because the coupling math compounds discretization error.
    REQUIRE(z_diff > 60.0);
    REQUIRE(z_diff < 150.0);

    // Cross-check against the Wadell formula via the same single-ended
    // engine result: diff should be > 1.5 × single-ended (tightly coupled
    // is the only way to get below that).
    auto se = compute_one_fdm(W, 0, s);
    REQUIRE(z_diff > 1.5 * se.z0);
}

TEST_CASE("diff FDM: closer spacing yields lower Z_diff", "[trace]") {
    AnalysisStackup s;
    s.outer_dielectric_height = 1.0e-3;

    const double W = 1.5e-3;
    const double z_far  = compute_diff_z0_fdm(W, 1.0e-3, 0, s);
    const double z_near = compute_diff_z0_fdm(W, 0.2e-3, 0, s);

    REQUIRE(z_far  > 0);
    REQUIRE(z_near > 0);
    REQUIRE(z_near < z_far);
}

TEST_CASE("engine: compute_all caches FDM results per (width, layer)", "[trace]") {
    // Build a board with many segments of the same width and verify that
    // every segment ends up with the SAME z0 (i.e. the cache fired). Use
    // a relatively wide trace so the FDM grid stays small and the test
    // doesn't dominate CI time.
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    AnalysisStackup as;
    as.outer_dielectric_height = 1.0e-3;  // larger H → coarser absolute mesh
    as.copper_thickness = 35e-6;

    for (int i = 0; i < 4; ++i) {
        Segment s;
        s.start = {0, 0};
        s.end   = {1e-3, 0};
        s.width = 2.0e-3;   // identical across all segments
        s.layer_ordinal = 0;
        s.net_id = i + 1;
        b.segments.push_back(s);
    }

    auto rs = compute_all(b, as, Engine::Fdm);
    REQUIRE(rs.size() == 4);
    for (std::size_t i = 1; i < rs.size(); ++i) {
        REQUIRE(rs[i].z0 == rs[0].z0);
    }
    REQUIRE(rs[0].z0 > 0);
}

TEST_CASE("diff closed-form: edge-coupled microstrip Z_diff is in expected band",
          "[trace]") {
    AnalysisStackup s;
    s.outer_dielectric_height = 1.0e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.4;
    const double W = 1.5e-3;
    const double S = 0.5e-3;
    const double z = compute_diff_z0_closed_form(W, S, 0, s);
    REQUIRE(z > 60.0);
    REQUIRE(z < 130.0);

    // Tighter spacing reduces Z_diff (more coupling).
    const double z_tight = compute_diff_z0_closed_form(W, 0.1e-3, 0, s);
    REQUIRE(z_tight < z);
}

TEST_CASE("compute_diff_pairs: finds pairs and computes Z_diff per pair", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "USB_DP_P"});
    b.nets.push_back({2, "USB_DP_N"});
    b.nets.push_back({3, "GND"});

    auto push_seg = [&](int net, double y, double w) {
        Segment s;
        s.start = {0, y};
        s.end   = {10e-3, y};
        s.width = w;
        s.layer_ordinal = 0;
        s.net_id = net;
        b.segments.push_back(s);
    };
    push_seg(1, 0,        0.20e-3);
    push_seg(1, 0,        0.20e-3);
    push_seg(2, 0.30e-3,  0.20e-3);
    push_seg(2, 0.30e-3,  0.20e-3);
    push_seg(3, 1.0e-3,   0.30e-3);  // unrelated GND segment, should not match

    AnalysisStackup s;
    auto pairs = compute_diff_pairs(b, s, Engine::ClosedForm);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].base_name == "USB_DP");
    REQUIRE(pairs[0].trace_width == 0.20e-3);
    // Segments at y=0 (P) and y=0.30mm (N) → centre-to-centre 0.30 mm;
    // both 0.20 mm wide → gap = 0.30 − 0.20 = 0.10 mm. Geometry
    // estimator picks that up, so spacing is no longer the S=W default.
    REQUIRE(pairs[0].spacing == Approx(0.10e-3).margin(1e-9));
    REQUIRE(pairs[0].z_diff      > 0);
    REQUIRE(pairs[0].segment_indices.size() == 4);  // 2 + 2 segments
}

TEST_CASE("compute_diff_pairs: spacing derived from parallel geometry", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "USB_DP_P"});
    b.nets.push_back({2, "USB_DP_N"});

    // Two parallel horizontal traces 0.3 mm apart (centre-to-centre),
    // each 0.15 mm wide → expected edge-to-edge gap = 0.3 - 0.15 = 0.15 mm.
    auto add = [&](int net, double y) {
        Segment s;
        s.start = {0, y};
        s.end   = {10e-3, y};
        s.width = 0.15e-3;
        s.layer_ordinal = 0;
        s.net_id = net;
        b.segments.push_back(s);
    };
    add(1, 0.0);
    add(2, 0.3e-3);

    AnalysisStackup as;
    auto pairs = compute_diff_pairs(b, as, Engine::ClosedForm);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].spacing == Approx(0.15e-3).margin(1e-9));
}

TEST_CASE("compute_diff_pairs: spacing falls back to S=W if no parallel pair",
          "[trace]") {
    // Two segments oriented perpendicular to each other → spacing
    // estimator finds nothing, so the result reverts to S = trace_width.
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "X_P"});
    b.nets.push_back({2, "X_N"});

    auto add = [&](int net, Point2 start, Point2 end) {
        Segment s;
        s.start = start;
        s.end = end;
        s.width = 0.20e-3;
        s.layer_ordinal = 0;
        s.net_id = net;
        b.segments.push_back(s);
    };
    add(1, {0, 0}, {10e-3, 0});       // horizontal
    add(2, {5e-3, -5e-3}, {5e-3, 5e-3});  // vertical, crossing

    AnalysisStackup as;
    auto pairs = compute_diff_pairs(b, as, Engine::ClosedForm);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].spacing == Approx(0.20e-3));  // S = W fallback
}

TEST_CASE("compute_diff_pairs: returns empty when no diff pairs exist", "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VCC"});
    auto p = compute_diff_pairs(b, {});
    REQUIRE(p.empty());
}

TEST_CASE("from_board: stripline B = sum of dielectric above + below inner copper",
          "[trace]") {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.layers.push_back({0,  "F.Cu",  "signal"});
    b.stackup.layers.push_back({1,  "In1.Cu","power"});
    b.stackup.layers.push_back({31, "B.Cu",  "signal"});

    auto push_item = [&](sikit::si::SiStackupItem::Kind k, std::string n, double t, double e = 0) {
        sikit::si::SiStackupItem it;
        it.kind = k; it.name = std::move(n); it.thickness = t; it.epsilon_r = e;
        sis.items.push_back(it);
    };
    push_item(sikit::si::SiStackupItem::Kind::Copper,     "F.Cu",   35e-6);
    push_item(sikit::si::SiStackupItem::Kind::Dielectric, "prepreg", 0.10e-3, 4.5);
    push_item(sikit::si::SiStackupItem::Kind::Copper,     "In1.Cu", 18e-6);
    push_item(sikit::si::SiStackupItem::Kind::Dielectric, "core",    0.30e-3, 4.4);
    push_item(sikit::si::SiStackupItem::Kind::Copper,     "B.Cu",   35e-6);

    auto s = AnalysisStackup::from_board(b, sis);
    REQUIRE(s.from_real_stackup);
    REQUIRE(s.inner_plane_separation == Approx(0.40e-3));  // 0.10 + 0.30
}
