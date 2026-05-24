#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "emi/BoardAnalysis.h"
#include "emi/Masks.h"

using Catch::Approx;
using emikit::emi::AnalysisConfig;
using emikit::emi::analyze_board;
using emikit::emi::cispr32_class_b;
using emikit::emi::default_cispr_freq_grid;
using emikit::emi::Verdict;

namespace {

// Build a board with one routed net at a controllable length so we can
// observe how trace length drives the emission estimate.
circuitcore::board::Board one_trace_board(double length_m) {
    circuitcore::board::Board b;
    b.nets.push_back({1, "CLK"});
    circuitcore::board::Segment s;
    s.start = {0, 0};
    s.end   = {length_m, 0};
    s.width = 0.2e-3;
    s.layer_ordinal = 0;
    s.net_id = 1;
    b.segments.push_back(s);
    return b;
}

}  // namespace

TEST_CASE("analysis: empty board returns NoData, not PASS",
          "[emi]") {
    circuitcore::board::Board b;
    AnalysisConfig cfg;
    auto R = analyze_board(b, cispr32_class_b(), cfg);
    REQUIRE(R.nets.empty());
    REQUIRE(R.verdict.status == Verdict::Status::NoData);
}

TEST_CASE("analysis: filter that matches nothing returns NoData",
          "[emi]") {
    auto b = one_trace_board(50e-3);
    AnalysisConfig cfg;
    cfg.net_filter = {"DOES_NOT_EXIST"};
    auto R = analyze_board(b, cispr32_class_b(), cfg);
    REQUIRE(R.nets.empty());
    REQUIRE(R.verdict.status == Verdict::Status::NoData);
}

TEST_CASE("analysis: one trace produces one NetEmission record",
          "[emi]") {
    auto b = one_trace_board(50e-3);   // 50 mm
    AnalysisConfig cfg;
    auto R = analyze_board(b, cispr32_class_b(), cfg);
    REQUIRE(R.nets.size() == 1);
    REQUIRE(R.nets[0].net_name == "CLK");
    REQUIRE(R.nets[0].total_length_m == Approx(50e-3));
    REQUIRE(R.nets[0].loop_area_m2 > 0.0);
    const std::size_t expected_freqs = cfg.freq_hz.empty()
                                            ? default_cispr_freq_grid().size()
                                            : cfg.freq_hz.size();
    REQUIRE(R.nets[0].e_dbuv.size() == expected_freqs);
}

TEST_CASE("analysis: longer trace radiates more", "[emi]") {
    AnalysisConfig cfg;
    auto small  = analyze_board(one_trace_board(20e-3), cispr32_class_b(), cfg);
    auto large  = analyze_board(one_trace_board(200e-3), cispr32_class_b(), cfg);
    // Worst-case dBuV/m at any single frequency should be higher.
    double s_max = -1000.0, l_max = -1000.0;
    for (double v : small.worst_case_dbuv) s_max = std::max(s_max, v);
    for (double v : large.worst_case_dbuv) l_max = std::max(l_max, v);
    REQUIRE(l_max > s_max);
}

TEST_CASE("analysis: net filter restricts evaluated nets", "[emi]") {
    circuitcore::board::Board b;
    b.nets.push_back({1, "CLK"});
    b.nets.push_back({2, "DATA"});
    circuitcore::board::Segment s; s.width = 0.2e-3; s.layer_ordinal = 0;
    s.start = {0, 0}; s.end = {0.05, 0}; s.net_id = 1; b.segments.push_back(s);
    s.net_id = 2;                       b.segments.push_back(s);

    AnalysisConfig cfg;
    cfg.net_filter = {"CLK"};
    auto R = analyze_board(b, cispr32_class_b(), cfg);
    REQUIRE(R.nets.size() == 1);
    REQUIRE(R.nets[0].net_name == "CLK");
}

TEST_CASE("analysis: tight drive + giant loop fails the mask",
          "[emi]") {
    // 1 m of trace, 5 mm loop height, 1 A drive, 1 ns rise -> emissions
    // well over CISPR Class B.
    auto b = one_trace_board(1.0);
    AnalysisConfig cfg;
    cfg.drive.i_peak_a = 1.0;
    cfg.drive.rise_time_s = 1.0e-9;
    cfg.drive.period_s = 1.0e-8;        // 100 MHz
    cfg.loop_height_m = 5.0e-3;
    auto R = analyze_board(b, cispr32_class_b(), cfg);
    REQUIRE(R.verdict.status == Verdict::Status::Fail);
    REQUIRE(R.verdict.worst_margin_db < 0.0);
}

TEST_CASE("analysis: default freq grid covers 30 MHz to 1 GHz", "[emi]") {
    auto g = default_cispr_freq_grid(50);
    REQUIRE(g.size() == 50);
    REQUIRE(g.front() == Approx(30.0e6));
    REQUIRE(g.back()  == Approx(1.0e9));
}
