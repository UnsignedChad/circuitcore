#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "circuitcore/intent/Checks.h"
#include "circuitcore/netlist/Netlist.h"

using circuitcore::board::Board;
using circuitcore::board::Pad;
using circuitcore::board::Segment;
using circuitcore::intent::check_design_intent;
using circuitcore::intent::CheckOptions;
using circuitcore::intent::Violation;
using circuitcore::netlist::Netlist;

namespace {

// Build a Netlist + Board that match: one cap (C1) decoupling VDD on
// U1, both routed. The matched pair is the "good design" against
// which the violation tests below introduce specific drifts.
struct Setup {
    Netlist nl;
    Board board;
};

Setup matched_setup() {
    Setup s;
    s.nl.components = {
        {"C1", "100nF", "Capacitor_SMD:C_0402"},
        {"U1", "STM32",  "Package_QFP:LQFP-64"},
    };
    s.nl.nets = {
        {1, "VDD", {{"C1", "1", "", ""}, {"U1", "5", "VDD", "power_in"}}},
        {2, "GND", {{"C1", "2", "", ""}, {"U1", "12", "", ""}}},
    };

    s.board.nets = {{1, "VDD"}, {2, "GND"}};
    Pad c1_p1; c1_p1.parent_ref = "C1"; c1_p1.name = "1";
    c1_p1.at = {10e-3, 10e-3}; c1_p1.net_id = 1;
    Pad c1_p2; c1_p2.parent_ref = "C1"; c1_p2.name = "2";
    c1_p2.at = {10.5e-3, 10e-3}; c1_p2.net_id = 2;
    Pad u1_p5; u1_p5.parent_ref = "U1"; u1_p5.name = "5";
    u1_p5.at = {11e-3, 10e-3}; u1_p5.net_id = 1;
    Pad u1_p12; u1_p12.parent_ref = "U1"; u1_p12.name = "12";
    u1_p12.at = {12e-3, 10e-3}; u1_p12.net_id = 2;
    s.board.pads = {c1_p1, c1_p2, u1_p5, u1_p12};

    Segment seg;
    seg.start = {10.5e-3, 10e-3};
    seg.end = {11e-3, 10e-3};
    seg.width = 0.2e-3;
    seg.layer_ordinal = 0;
    seg.net_id = 1;
    s.board.segments = {seg};
    return s;
}

}  // namespace

TEST_CASE("intent: clean design produces no violations", "[intent]") {
    auto s = matched_setup();
    auto v = check_design_intent(s.nl, s.board);
    REQUIRE(v.empty());
}

TEST_CASE("intent: missing footprint flagged", "[intent]") {
    auto s = matched_setup();
    // Add a netlist component that's never placed.
    s.nl.components.push_back({"R7", "10k", "Resistor_SMD:R_0402"});
    auto v = check_design_intent(s.nl, s.board);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].kind == Violation::Kind::MissingFootprint);
    REQUIRE(v[0].subject == "R7");
}

TEST_CASE("intent: unrouted net flagged", "[intent]") {
    auto s = matched_setup();
    // Add a netlist net that has multiple nodes but no board net.
    s.nl.nets.push_back({3, "RESET_N", {
        {"U1", "60", "NRST", "input"},
        {"C1", "1", "", ""},
    }});
    auto v = check_design_intent(s.nl, s.board);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].kind == Violation::Kind::UnroutedNet);
    REQUIRE(v[0].subject == "RESET_N");
}

TEST_CASE("intent: single-node net is not a violation", "[intent]") {
    auto s = matched_setup();
    // Lone no-connect net -- single node, never expected to be routed.
    s.nl.nets.push_back({3, "NC1", {{"U1", "63", "", "no_connect"}}});
    auto v = check_design_intent(s.nl, s.board);
    REQUIRE(v.empty());
}

TEST_CASE("intent: auto-generated Net-() name is not a violation",
          "[intent]") {
    auto s = matched_setup();
    s.nl.nets.push_back({3, "Net-(C1-Pad1)", {
        {"C1", "1", "", ""}, {"U1", "63", "", ""},
    }});
    auto v = check_design_intent(s.nl, s.board);
    REQUIRE(v.empty());
}

TEST_CASE("intent: decoupling cap inside budget passes", "[intent]") {
    auto s = matched_setup();
    // C1 sits at (10.0, 10), U1 pin 5 sits at (11.0, 10). 1 mm apart.
    // Default budget is 2 mm -> pass.
    CheckOptions opts;
    opts.decoupling_max_distance_m = 2.0e-3;
    auto v = check_design_intent(s.nl, s.board, opts);
    REQUIRE(v.empty());
}

TEST_CASE("intent: decoupling cap outside budget flagged", "[intent]") {
    auto s = matched_setup();
    // Tighten the budget so 1 mm fails.
    CheckOptions opts;
    opts.decoupling_max_distance_m = 0.5e-3;
    auto v = check_design_intent(s.nl, s.board, opts);
    REQUIRE_FALSE(v.empty());
    bool found = false;
    for (const auto& x : v) {
        if (x.kind == Violation::Kind::DecouplingCapTooFar &&
            x.subject == "C1") found = true;
    }
    REQUIRE(found);
}

TEST_CASE("intent: cap on non-power net is not checked for decoupling",
          "[intent]") {
    auto s = matched_setup();
    // Move C1 onto a signal net pair instead of VDD/GND so the
    // decoupling check should not engage.
    s.nl.nets[0].name = "SIGNAL_A";   // was VDD
    s.board.nets[0].name = "SIGNAL_A";
    auto v = check_design_intent(s.nl, s.board);
    // No decoupling violation regardless of budget -- this isn't a
    // power-rail cap.
    for (const auto& x : v) {
        REQUIRE(x.kind != Violation::Kind::DecouplingCapTooFar);
    }
}

TEST_CASE("intent: individual check flags can be disabled", "[intent]") {
    auto s = matched_setup();
    s.nl.components.push_back({"R7", "10k", "R_0402"});  // missing footprint
    CheckOptions opts;
    opts.check_missing_footprint = false;
    auto v = check_design_intent(s.nl, s.board, opts);
    for (const auto& x : v) {
        REQUIRE(x.kind != Violation::Kind::MissingFootprint);
    }
}
