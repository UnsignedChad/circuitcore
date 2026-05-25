#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "pi/Roughness.h"

using pdnkit::pi::hj_roughness_multiplier;
using pdnkit::pi::skin_depth_copper;
using Catch::Approx;

// Skin depth in Cu at 1 GHz is the canonical ~2.06 um.
TEST_CASE("rough: skin depth in Cu at 1 GHz is ~2.06 um",
          "[roughness][validation]") {
    const double w = 2.0 * std::numbers::pi * 1.0e9;
    const double d = skin_depth_copper(w);
    INFO("skin depth = " << d * 1.0e6 << " um");
    REQUIRE(d > 2.0e-6);
    REQUIRE(d < 2.15e-6);
}

// At low frequency where delta >> R_q, multiplier -> 1.
TEST_CASE("rough: low-freq limit gives multiplier ~ 1",
          "[roughness][validation]") {
    const double k = hj_roughness_multiplier(1.0e-6, 1.0e3);  // 1um @ 1kHz
    INFO("K_HJ = " << k);
    REQUIRE(k == Approx(1.0).margin(1.0e-6));
}

// At very high frequency where delta << R_q, multiplier -> 2.
TEST_CASE("rough: high-freq limit gives multiplier ~ 2",
          "[roughness][validation]") {
    const double k = hj_roughness_multiplier(5.0e-6, 1.0e12);  // 5um @ 1THz
    INFO("K_HJ = " << k);
    REQUIRE(k > 1.95);
    REQUIRE(k <= 2.0);
}

// Standard rolled copper (Rq ~ 1 um) at 10 GHz: skin depth ~ 0.65 um,
// ratio ~ 1.54, K ~ 1 + (2/pi)*atan(1.4*2.37) = ~1.83. Published HJ
// tables sit in the 1.7-1.9 band here.
TEST_CASE("rough: 1 um Rq at 10 GHz lands in published HJ band",
          "[roughness][validation]") {
    const double k = hj_roughness_multiplier(1.0e-6, 1.0e10);
    INFO("K_HJ = " << k);
    REQUIRE(k > 1.65);
    REQUIRE(k < 1.95);
}

// Rougher copper has a higher multiplier at any given frequency.
TEST_CASE("rough: rougher copper -> higher multiplier", "[roughness]") {
    const double f = 5.0e9;
    const double k_smooth = hj_roughness_multiplier(0.4e-6, f);
    const double k_rough  = hj_roughness_multiplier(2.0e-6, f);
    REQUIRE(k_rough > k_smooth);
}

// Sanity: zero roughness or zero frequency returns 1.0 (no penalty).
TEST_CASE("rough: zero inputs return 1", "[roughness]") {
    REQUIRE(hj_roughness_multiplier(0.0, 1.0e9) == Approx(1.0));
    REQUIRE(hj_roughness_multiplier(1.0e-6, 0.0) == Approx(1.0));
}
