#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/Eye.h"
#include "si/EyeMetrics.h"

using namespace sikit::eye;
using Catch::Approx;

TEST_CASE("metrics: clean unfiltered NRZ has a wide-open eye", "[metrics]") {
    auto bits = prbs7(500);
    auto y = nrz_waveform(bits, 32);
    auto eye = build_eye(y, 32, 64, 64, /*warmup=*/4);
    auto m = measure_eye(eye);

    // Step NRZ: traces only at ±1 V → entire middle voltage band is empty.
    // Height should be very close to the full ±1 range (after the small
    // margin build_eye adds).
    REQUIRE(m.height_v > 1.5);
    // No transitions populate the middle voltage row → eye width = 1 UI.
    REQUIRE(m.width_ui == Approx(1.0));
    // No jitter under perfect step transitions.
    REQUIRE(m.jitter_pp_ui == Approx(0.0));
}

TEST_CASE("metrics: heavy ISI closes the eye", "[metrics]") {
    auto bits = prbs7(2000);
    auto tx = nrz_waveform(bits, 32);
    auto rx = rc_lowpass(tx, 1.0, 0.005);
    auto eye = build_eye(rx, 32, 128, 96, /*warmup=*/8);
    auto m = measure_eye(eye);

    // Severely band-limited channel → middle bins fill in → eye height
    // collapses (anywhere from 0 to small positive value).
    REQUIRE(m.height_v < 1.0);
}

TEST_CASE("metrics: bandwidth-limited ramped NRZ shows measurable jitter", "[metrics]") {
    auto bits = prbs7(500);
    auto y = nrz_with_ramp(bits, 32, 0.20);   // 20% UI rise time
    auto eye = build_eye(y, 32, 64, 64, /*warmup=*/4);
    auto m = measure_eye(eye);

    // Eye should still be open vertically.
    REQUIRE(m.height_v > 0.5);
    // Ramp traces populate the centre voltage row near the transition
    // edges → some jitter, some inner empty width.
    REQUIRE(m.jitter_pp_ui > 0.0);
    REQUIRE(m.width_ui > 0.0);
    REQUIRE(m.width_ui < 1.0);
}

TEST_CASE("metrics: empty eye returns zeros", "[metrics]") {
    EyeGrid g;
    g.time_bins = 32;
    g.volt_bins = 32;
    g.counts.assign(32 * 32, 0);
    auto m = measure_eye(g);
    REQUIRE(m.height_v == 0.0);
    REQUIRE(m.width_ui == 0.0);
    REQUIRE(m.jitter_pp_ui == 0.0);
}

TEST_CASE("metrics: threshold is midpoint of v_min/v_max", "[metrics]") {
    auto bits = std::vector<int>{0, 1, 0, 1};
    auto y = nrz_waveform(bits, 8);
    auto eye = build_eye(y, 8, 16, 16);
    auto m = measure_eye(eye);
    REQUIRE(m.v_threshold == Approx(0.5 * (eye.v_max + eye.v_min)));
}
