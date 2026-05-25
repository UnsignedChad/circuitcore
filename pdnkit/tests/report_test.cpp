#include <catch2/catch_test_macros.hpp>

#include "Report.h"
#include "circuitcore/board/Board.h"

using namespace pdnkit;

namespace {
bool has(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}
}

TEST_CASE("report: minimal board renders board section", "[report]") {
    circuitcore::board::Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal", 35.0e-6});
    b.stackup.layers.push_back({31, "B.Cu", "signal", 35.0e-6});
    b.nets.push_back({1, "+3V3"});

    SignoffReport r;
    r.board = &b;
    r.board_path = "test.kicad_pcb";

    auto html = render_signoff_html(r);
    REQUIRE(has(html, "pdnkit PDN signoff"));
    REQUIRE(has(html, "test.kicad_pcb"));
    REQUIRE(has(html, "<title>"));
}

TEST_CASE("report: IR drop section appears when solution given",
          "[report]") {
    circuitcore::board::Board b;
    pi::IrMesh m;
    m.nodes.push_back({0, 0.0, 0.0, 0, 0, 0});
    m.nodes.push_back({1, 0.001, 0.0, 1, 0, 0});
    pi::Solution sol;
    sol.ok = true;
    sol.voltages = {1.0, 0.95};
    sol.min_v = 0.95;
    sol.max_v = 1.0;
    SignoffReport r;
    r.board = &b;
    r.ir_mesh = &m;
    r.ir_solution = &sol;
    auto html = render_signoff_html(r);
    REQUIRE(has(html, "Static IR drop"));
    REQUIRE(has(html, "50.000 mV"));  // 1.0 - 0.95 = 50 mV drop
}

TEST_CASE("report: DRC section shows violations or all-clear",
          "[report]") {
    circuitcore::board::Board b;
    SignoffReport r;
    r.board = &b;
    pi::DrcReport drc;
    drc.segments_checked = 5;
    drc.nets_checked = 1;
    r.drc = drc;
    auto html = render_signoff_html(r);
    REQUIRE(has(html, "No violations"));
    // Add a violation.
    pi::DrcViolation v;
    v.segment_index = 7;
    v.net_id = 2;
    v.layer_ordinal = 0;
    v.width_actual_m = 0.3e-3;
    v.width_required_m = 0.8e-3;
    drc.violations.push_back(v);
    r.drc = drc;
    html = render_signoff_html(r);
    REQUIRE(has(html, "violation"));
    REQUIRE(has(html, "0.300"));
}

TEST_CASE("report: Z(f) section embeds an SVG", "[report]") {
    circuitcore::board::Board b;
    SignoffReport r;
    r.board = &b;
    r.zf_freqs = {1.0e6, 1.0e7, 1.0e8};
    r.zf_z    = {{0.01, 0.0}, {0.05, 0.0}, {0.1, 0.0}};
    r.zf_target_ohm = 0.025;
    auto html = render_signoff_html(r);
    REQUIRE(has(html, "<svg"));
    REQUIRE(has(html, "Peak |Z|"));
    REQUIRE(has(html, "exceeds target"));  // peak 0.1 > target 0.025
}
