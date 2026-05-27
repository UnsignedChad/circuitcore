// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/Skew.h"
#include "si/SiStackup.h"
#include "si/TraceImpedance.h"
#include "circuitcore/board/Board.h"

using Catch::Approx;
using sikit::si::compute_diff_pair_skews;
using sikit::si::DiffPairSkew;

namespace {

// Build a synthetic board with two diff pairs:
//   USB_DP / USB_DN -- both 50 mm exactly (zero skew)
//   PCIE_RX_P / PCIE_RX_N -- 50 mm vs 50.4 mm (0.4 mm of skew)
// Both routed on F.Cu.
circuitcore::board::Board make_two_pair_board() {
    circuitcore::board::Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6,
                                  "copper", 0.0, 0.0});
    b.stackup.total_thickness = 1.6e-3;

    int next_id = 1;
    auto add_net = [&](const std::string& name) {
        b.nets.push_back({next_id, name});
        return next_id++;
    };
    auto add_trace = [&](int net_id, double x0, double x1) {
        circuitcore::board::Segment s;
        s.start = {x0, 0};
        s.end   = {x1, 0};
        s.width = 0.20e-3;
        s.layer_ordinal = 0;
        s.net_id = net_id;
        b.segments.push_back(s);
    };

    const int usb_dp = add_net("USB_DP");
    const int usb_dn = add_net("USB_DN");
    add_trace(usb_dp, 0, 50e-3);
    add_trace(usb_dn, 0, 50e-3);

    const int pcie_p = add_net("PCIE_RX_P");
    const int pcie_n = add_net("PCIE_RX_N");
    add_trace(pcie_p, 0, 50e-3);
    add_trace(pcie_n, 0, 50.4e-3);
    return b;
}

sikit::analysis::AnalysisStackup canonical_microstrip() {
    sikit::analysis::AnalysisStackup s;
    s.outer_dielectric_height = 0.2e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.3;
    s.tan_delta = 0.02;
    return s;
}

}  // namespace

TEST_CASE("skew: matched pair reports zero skew", "[skew]") {
    auto b = make_two_pair_board();
    auto rows = compute_diff_pair_skews(b, canonical_microstrip(), 5.0);
    REQUIRE_FALSE(rows.empty());
    // The USB pair (matched 50 mm) should report sub-micron skew.
    const DiffPairSkew* usb = nullptr;
    for (const auto& r : rows) {
        if (r.base_name.find("USB") != std::string::npos) { usb = &r; break; }
    }
    REQUIRE(usb != nullptr);
    REQUIRE(std::abs(usb->skew_m) < 1e-9);
    REQUIRE(std::abs(usb->skew_ps) < 1e-3);
    REQUIRE_FALSE(usb->exceeds_budget);
}

TEST_CASE("skew: mismatched pair reports correct sign and magnitude",
          "[skew]") {
    auto b = make_two_pair_board();
    auto rows = compute_diff_pair_skews(b, canonical_microstrip(), 5.0);
    const DiffPairSkew* pcie = nullptr;
    for (const auto& r : rows) {
        if (r.base_name.find("PCIE") != std::string::npos) { pcie = &r; break; }
    }
    REQUIRE(pcie != nullptr);
    // P is 50.0 mm, N is 50.4 mm, so skew_m = -0.4 mm (P shorter).
    REQUIRE(pcie->skew_m == Approx(-0.4e-3).margin(1e-6));
    // Microstrip on FR-4: v_phase ~ 1.6e8 m/s, so 0.4 mm / 1.6e8 ~ 2.5 ps.
    REQUIRE(std::abs(pcie->skew_ps) > 1.5);
    REQUIRE(std::abs(pcie->skew_ps) < 5.0);
    REQUIRE_FALSE(pcie->exceeds_budget);   // under 5 ps
}

TEST_CASE("skew: budget threshold flips the FAIL flag", "[skew]") {
    auto b = make_two_pair_board();
    // PCIe pair skew is ~2.5 ps. Set budget at 1.0 ps -> should fail.
    auto rows = compute_diff_pair_skews(b, canonical_microstrip(), 1.0);
    const DiffPairSkew* pcie = nullptr;
    for (const auto& r : rows) {
        if (r.base_name.find("PCIE") != std::string::npos) { pcie = &r; break; }
    }
    REQUIRE(pcie != nullptr);
    REQUIRE(pcie->exceeds_budget);
}

TEST_CASE("skew: empty board returns empty result", "[skew]") {
    circuitcore::board::Board empty;
    auto rows = compute_diff_pair_skews(empty, canonical_microstrip(), 5.0);
    REQUIRE(rows.empty());
}

TEST_CASE("skew: board without diff pairs returns empty result", "[skew]") {
    circuitcore::board::Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6,
                                  "copper", 0.0, 0.0});
    b.nets.push_back({1, "SCK"});   // single-ended, no pair partner
    circuitcore::board::Segment s;
    s.start = {0, 0}; s.end = {0.01, 0}; s.width = 0.2e-3;
    s.layer_ordinal = 0; s.net_id = 1;
    b.segments.push_back(s);
    auto rows = compute_diff_pair_skews(b, canonical_microstrip(), 5.0);
    REQUIRE(rows.empty());
}

TEST_CASE("skew: positive v_phase is reported back", "[skew]") {
    auto b = make_two_pair_board();
    auto rows = compute_diff_pair_skews(b, canonical_microstrip(), 5.0);
    REQUIRE_FALSE(rows.empty());
    for (const auto& r : rows) {
        REQUIRE(r.v_phase > 1e8);   // FR-4 microstrip ~ 1.5e8 .. 2e8 m/s
        REQUIRE(r.v_phase < 3e8);
    }
}

TEST_CASE("skew: zero-length pair is skipped (no segments anywhere)",
          "[skew]") {
    circuitcore::board::Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6, "copper", 0, 0});
    b.nets.push_back({1, "DATA_P"});
    b.nets.push_back({2, "DATA_N"});
    // No segments at all on either net.
    auto rows = compute_diff_pair_skews(b, canonical_microstrip(), 5.0);
    REQUIRE(rows.empty());
}
