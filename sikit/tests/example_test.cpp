// Regression test that runs the example board through every analysis
// pipeline and verifies the numbers match what sikit/examples/
// WALKTHROUGH.md tells the user to expect. If anything here breaks,
// either the analysis output drifted or the WALKTHROUGH needs an update;
// fixing both in lockstep keeps the example honest.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "circuitcore/formats/kicad/PcbParser.h"
#include "si/BusGroup.h"
#include "si/ReturnPath.h"
#include "si/SiStackup.h"
#include "si/Skew.h"
#include "si/TraceImpedance.h"

using Catch::Approx;

namespace {

std::filesystem::path demo_board_path() {
    // The path comes from a compile-time define so the test runs
    // independent of cwd. Defined in tests/CMakeLists.txt.
    return std::filesystem::path(SIKIT_EXAMPLES_DIR) / "demo_board.kicad_pcb";
}

}  // namespace

TEST_CASE("example: demo_board.kicad_pcb parses cleanly", "[example]") {
    auto result = circuitcore::formats::kicad::PcbParser::parse_file(
        demo_board_path());
    REQUIRE(result.has_value());
    const auto& b = result.value();
    REQUIRE(b.nets.size() >= 10);
    REQUIRE(b.stackup.layers.size() == 5);   // F.Cu, In1.Cu, In2.Cu, B.Cu, Edge.Cuts
}

TEST_CASE("example: demo_board carries the expected nets", "[example]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_board_path()).value();
    for (const auto& name :
         {"GND", "VCC", "USB_DP", "USB_DN",
          "DDR_DQ0", "DDR_DQ1", "DDR_DQ2", "DDR_DQ3", "BAD_NET"}) {
        REQUIRE(board.find_net_by_name(name) != nullptr);
    }
}

TEST_CASE("example: USB diff pair skew matches the walkthrough", "[example]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_board_path()).value();
    auto sis = sikit::si::load_si_stackup(demo_board_path()).value();
    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    auto rows = sikit::si::compute_diff_pair_skews(board, stackup, 5.0);
    REQUIRE(rows.size() == 1);
    // P = 40 mm, N = 40.4 mm, so skew_m = -0.4 mm.
    REQUIRE(rows[0].skew_m == Approx(-0.4e-3).margin(1e-6));
    REQUIRE_FALSE(rows[0].exceeds_budget);  // under 5 ps default
}

TEST_CASE("example: DDR_DQ bus skew fails the 10 ps default budget",
          "[example]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_board_path()).value();
    auto sis = sikit::si::load_si_stackup(demo_board_path()).value();
    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    auto groups = sikit::si::compute_bus_groups(board, stackup, 10.0);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].base_name == "DDR_DQ");
    REQUIRE(groups[0].members.size() == 4);
    // min = 39 mm, max = 43 mm, spread = 4 mm.
    REQUIRE(groups[0].min_length_m == Approx(39e-3).margin(1e-6));
    REQUIRE(groups[0].max_length_m == Approx(43e-3).margin(1e-6));
    REQUIRE(groups[0].exceeds_budget);
}

TEST_CASE("example: BAD_NET is the only return-path violation",
          "[example]") {
    auto board = circuitcore::formats::kicad::PcbParser::parse_file(
                      demo_board_path()).value();
    auto v = sikit::si::detect_return_path_violations(board);
    REQUIRE_FALSE(v.empty());
    // Worst offender at the top of the sorted list should be BAD_NET.
    const auto* bad = board.find_net_by_name("BAD_NET");
    REQUIRE(bad != nullptr);
    REQUIRE(v.front().net_id == bad->id);
    // 100% of its sample points are over the plane gap.
    REQUIRE(v.front().off_plane_fraction == Approx(1.0));
    REQUIRE(v.front().reference_layer == 1);   // In1.Cu was the candidate
}
