#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/PowerDrc.h"

using namespace pdnkit::pi;
using namespace circuitcore::board;
using Catch::Approx;

// IPC-2221 reference: for an external 1 oz (35 um) trace at 10 C rise,
// 1 A nominally needs about 0.30 mm width. The published curve gives
// ~0.30-0.35 mm (small spread depending on how you round the source
// tables). We check the IPC-2221 closed form lands inside that band.
TEST_CASE("ipc2221: 1A external 1oz 10C rise -> ~0.30 mm", "[drc][validation]") {
    constexpr double oz1 = 35.0e-6;     // 35 um
    const double area = ipc2221_min_area(1.0, 10.0, /*external=*/true);
    const double width = area / oz1;

    INFO("required width = " << width * 1000.0 << " mm");
    REQUIRE(width >= 0.25e-3);    // 0.25 mm lower bound
    REQUIRE(width <= 0.40e-3);    // 0.40 mm upper bound

    // Inverse direction: that same area should carry close to 1 A.
    const double I_back = ipc2221_max_current(area, 10.0, true);
    REQUIRE(I_back == Approx(1.0).epsilon(0.001));
}

// Internal trace needs more copper than external for the same current,
// because heat dissipates less efficiently. k drops from 0.048 to 0.024.
TEST_CASE("ipc2221: internal needs more copper than external", "[drc]") {
    const double area_ext = ipc2221_min_area(2.0, 10.0, true);
    const double area_int = ipc2221_min_area(2.0, 10.0, false);
    INFO("ext = " << area_ext << " m^2,  int = " << area_int << " m^2");
    REQUIRE(area_int > area_ext);
    // The ratio should be (k_ext/k_int)^(1/0.725) = 2^1.379 = ~2.6x.
    REQUIRE(area_int / area_ext == Approx(2.6).epsilon(0.05));
}

// Higher temp rise allowance lets the trace be narrower.
TEST_CASE("ipc2221: 30C rise needs less width than 10C rise", "[drc]") {
    const double area_10 = ipc2221_min_area(3.0, 10.0, true);
    const double area_30 = ipc2221_min_area(3.0, 30.0, true);
    REQUIRE(area_30 < area_10);
    // Ratio (10/30)^(0.44/0.725) = (1/3)^0.607 = ~0.51
    REQUIRE(area_30 / area_10 == Approx(0.51).epsilon(0.02));
}

// End-to-end check: hand-built board with one undersized trace, the DRC
// must flag exactly that trace.
TEST_CASE("drc: flags undersized trace on the target net", "[drc]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal", 35.0e-6});
    b.stackup.layers.push_back({31, "B.Cu", "signal", 35.0e-6});

    Net n;
    n.id = 1;
    n.name = "+3V3";
    b.nets.push_back(n);

    // Segment too narrow for 3A: 0.5 mm wide on F.Cu.
    Segment narrow;
    narrow.net_id = 1;
    narrow.layer_ordinal = 0;
    narrow.start = {0.0, 0.0};
    narrow.end   = {10.0e-3, 0.0};
    narrow.width = 0.5e-3;
    b.segments.push_back(narrow);

    // Segment plenty wide: 2.0 mm.
    Segment wide;
    wide.net_id = 1;
    wide.layer_ordinal = 0;
    wide.start = {0.0, 1.0e-3};
    wide.end   = {10.0e-3, 1.0e-3};
    wide.width = 2.0e-3;
    b.segments.push_back(wide);

    DrcRule rule{1, /*amps=*/3.0, /*dT=*/10.0};
    auto report = check_ipc2152(b, {rule});

    REQUIRE(report.segments_checked == 2);
    REQUIRE(report.violations.size() == 1);
    REQUIRE(report.violations[0].segment_index == 0);
    REQUIRE(report.violations[0].width_actual_m == Approx(0.5e-3));
    REQUIRE(report.violations[0].width_required_m > 0.5e-3);
    REQUIRE(report.violations[0].external);
}

// Internal layer flag: a trace on an inner copper layer must be classified
// as internal even if it's plenty wide on F.Cu.
TEST_CASE("drc: distinguishes internal from external layers", "[drc]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu",   "signal", 35.0e-6});
    b.stackup.layers.push_back({1,  "In1.Cu", "signal", 35.0e-6});
    b.stackup.layers.push_back({31, "B.Cu",   "signal", 35.0e-6});

    Net n; n.id = 1; n.name = "VCC"; b.nets.push_back(n);

    Segment seg;
    seg.net_id = 1;
    seg.layer_ordinal = 1;          // inner layer
    seg.start = {0.0, 0.0};
    seg.end   = {10.0e-3, 0.0};
    seg.width = 0.3e-3;             // 0.3 mm on internal -- too narrow for 1A
    b.segments.push_back(seg);

    DrcRule rule{1, 1.0, 10.0};
    auto report = check_ipc2152(b, {rule});

    // 0.3 mm internal is too narrow for 1A at 10C -- should flag.
    REQUIRE(report.violations.size() == 1);
    REQUIRE_FALSE(report.violations[0].external);
}
