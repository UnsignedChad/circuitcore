#include <catch2/catch_test_macros.hpp>

#include "si/Eye.h"
#include "si/EyeMask.h"

using namespace sikit::specs;
using namespace sikit::eye;

TEST_CASE("eye_mask: point-in-polygon ray cast basics", "[mask]") {
    // Unit square.
    std::vector<std::pair<double, double>> square = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1}};
    REQUIRE(point_in_polygon(0.5, 0.5, square));
    REQUIRE_FALSE(point_in_polygon(1.5, 0.5, square));
    REQUIRE_FALSE(point_in_polygon(-0.5, 0.5, square));
    REQUIRE_FALSE(point_in_polygon(0.5, -0.5, square));
}

TEST_CASE("eye_mask: available masks listed", "[mask]") {
    auto names = available_mask_names();
    REQUIRE(names.size() >= 2);
    REQUIRE(mask_by_name(names[0]) != nullptr);
    REQUIRE(mask_by_name("does not exist") == nullptr);
}

TEST_CASE("eye_mask: clean NRZ passes the USB-style mask", "[mask]") {
    // Unfiltered NRZ has counts only at ±1, with the eye middle empty.
    // No bins fall inside the diamond at (0.5, 0), so no violations.
    auto bits = prbs7(500);
    auto y = nrz_waveform(bits, 32);
    auto eye = build_eye(y, 32, 128, 96, /*warmup=*/4);

    const int v = count_violations(eye, usb20_hs_template1());
    REQUIRE(v == 0);
    REQUIRE(passes(eye, usb20_hs_template1()));
}

TEST_CASE("eye_mask: heavy ISI fails the centered-opening mask", "[mask]") {
    // Severely band-limited channel → eye middle fills in → violations.
    auto bits = prbs7(2000);
    auto tx = nrz_waveform(bits, 32);
    auto rx = rc_lowpass(tx, 1.0, 0.005);  // fc << baud
    auto eye = build_eye(rx, 32, 128, 96, /*warmup=*/8);

    const int v = count_violations(eye, generic_centered_opening());
    REQUIRE(v > 0);
    REQUIRE_FALSE(passes(eye, generic_centered_opening()));
}

TEST_CASE("eye_mask: empty eye reports zero violations", "[mask]") {
    EyeGrid empty;
    empty.time_bins = 128;
    empty.volt_bins = 96;
    empty.counts.assign(128 * 96, 0);
    REQUIRE(count_violations(empty, usb20_hs_template1()) == 0);
    REQUIRE(passes(empty, usb20_hs_template1()));
}

TEST_CASE("eye_mask: degenerate polygon never reports inside", "[mask]") {
    std::vector<std::pair<double, double>> line = {{0, 0}, {1, 1}};
    REQUIRE_FALSE(point_in_polygon(0.5, 0.5, line));
}
