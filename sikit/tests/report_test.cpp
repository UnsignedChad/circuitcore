#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "circuitcore/formats/kicad/PcbParser.h"
#include "si/Report.h"
#include "si/SiStackup.h"

using Catch::Approx;
using sikit::report::build_board_report;
using sikit::report::render_html;

namespace {

std::filesystem::path demo_path() {
    return std::filesystem::path(SIKIT_EXAMPLES_DIR) / "demo_board.kicad_pcb";
}

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("report: build_board_report on the demo board",
          "[report]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_path()).value();
    auto sis = sikit::si::load_si_stackup(demo_path()).value();
    auto r = build_board_report(board, sis);
    REQUIRE(r.net_count >= 10);
    REQUIRE(r.copper_layer_count == 4);
    REQUIRE_FALSE(r.nets.empty());
    REQUIRE(r.diff_pairs.size() == 1);
    REQUIRE(r.buses.size() == 1);
    REQUIRE_FALSE(r.return_path_violations.empty());
}

TEST_CASE("report: overall_pass reflects the per-section flags",
          "[report]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_path()).value();
    auto sis = sikit::si::load_si_stackup(demo_path()).value();
    auto r = build_board_report(board, sis);
    // DDR_DQ bus + BAD_NET return-path → overall must fail.
    REQUIRE_FALSE(r.overall_pass());
    REQUIRE(r.bus_fails > 0);
    REQUIRE(r.return_path_fails > 0);
}

TEST_CASE("report: render_html emits valid HTML scaffolding",
          "[report]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_path()).value();
    auto sis = sikit::si::load_si_stackup(demo_path()).value();
    auto r = build_board_report(board, sis);
    r.board_path = "demo_board.kicad_pcb";
    const auto html = render_html(r);
    REQUIRE(contains(html, "<!DOCTYPE html>"));
    REQUIRE(contains(html, "<title>sikit report"));
    REQUIRE(contains(html, "Trace impedance"));
    REQUIRE(contains(html, "Diff-pair skew"));
    REQUIRE(contains(html, "Multi-bit bus skew"));
    REQUIRE(contains(html, "Return-path violations"));
    REQUIRE(contains(html, "Issues found"));     // fail banner
}

TEST_CASE("report: HTML-escapes net names that contain markup chars",
          "[report]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_path()).value();
    // Inject a net name with an angle bracket to verify escaping.
    if (!board.nets.empty()) {
        board.nets.back().name = "EVIL<NET>&";
    }
    auto sis = sikit::si::load_si_stackup(demo_path()).value();
    auto r = build_board_report(board, sis);
    const auto html = render_html(r);
    // The angle brackets must be escaped, not present raw.
    REQUIRE(contains(html, "EVIL&lt;NET&gt;&amp;"));
    REQUIRE_FALSE(contains(html, "EVIL<NET>"));
}

TEST_CASE("report: empty board produces a valid pass-banner report",
          "[report]") {
    circuitcore::board::Board empty;
    empty.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6,
                                       "copper", 0, 0});
    sikit::si::SiStackup sis;
    auto r = build_board_report(empty, sis);
    REQUIRE(r.overall_pass());
    const auto html = render_html(r);
    REQUIRE(contains(html, "All checks PASS"));
}
