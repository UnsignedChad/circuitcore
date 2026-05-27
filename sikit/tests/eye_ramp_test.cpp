// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/Eye.h"
#include "si/Ibis.h"

using namespace sikit::eye;
using Catch::Approx;

TEST_CASE("ramp: zero fraction reproduces step NRZ", "[ramp]") {
    auto bits = std::vector<int>{0, 1, 0, 1};
    auto step = nrz_waveform(bits, 8);
    auto ramp = nrz_with_ramp(bits, 8, 0.0);
    REQUIRE(step == ramp);
}

TEST_CASE("ramp: transitions are linear across the ramp window", "[ramp]") {
    auto bits = std::vector<int>{0, 1};
    auto y = nrz_with_ramp(bits, 10, 0.4);  // 40% of UI = 4 samples
    // First bit (0): all samples should be -1 (no preceding transition).
    for (int i = 0; i < 10; ++i) {
        REQUIRE(y[i] == Approx(-1.0));
    }
    // Second bit (1): ramp from -1 to +1 over first 4 samples.
    REQUIRE(y[10] == Approx(-1.0 + 2.0 * 0.25));   // -0.5
    REQUIRE(y[11] == Approx(-1.0 + 2.0 * 0.50));   //  0.0
    REQUIRE(y[12] == Approx(-1.0 + 2.0 * 0.75));   // +0.5
    REQUIRE(y[13] == Approx(+1.0));
    // Remaining flat at +1.
    for (int i = 14; i < 20; ++i) {
        REQUIRE(y[i] == Approx(1.0));
    }
}

TEST_CASE("ramp: identical bits produce a flat trace", "[ramp]") {
    auto bits = std::vector<int>{1, 1, 1};
    auto y = nrz_with_ramp(bits, 6, 0.3);
    for (double v : y) REQUIRE(v == Approx(1.0));
}

TEST_CASE("ramp: fraction > 1 is clamped to 1", "[ramp]") {
    auto bits = std::vector<int>{0, 1};
    auto y = nrz_with_ramp(bits, 4, 5.0);   // requests 500% — clamped
    REQUIRE(y.size() == 8);
    // No assertion violation; just succeeded.
}

TEST_CASE("ramp_fraction_from_ibis: dt_rise / UI", "[ramp]") {
    sikit::ibis::Model m;
    m.ramp.dt_rise = {100e-12, 80e-12, 120e-12};
    // baud = 1 GHz → UI = 1 ns; fraction = 0.1
    REQUIRE(ramp_fraction_from_ibis(m, 1e9) == Approx(0.1));
    // baud = 10 GHz → UI = 100 ps; fraction = 1.0 (clamped)
    REQUIRE(ramp_fraction_from_ibis(m, 10e9) == Approx(1.0));
}

TEST_CASE("ramp_fraction_from_ibis: empty model returns 0", "[ramp]") {
    sikit::ibis::Model m;
    REQUIRE(ramp_fraction_from_ibis(m, 1e9) == 0.0);
}

TEST_CASE("ramp_fraction_from_ibis: zero baud returns 0", "[ramp]") {
    sikit::ibis::Model m;
    m.ramp.dt_rise = {100e-12, 100e-12, 100e-12};
    REQUIRE(ramp_fraction_from_ibis(m, 0.0) == 0.0);
}
