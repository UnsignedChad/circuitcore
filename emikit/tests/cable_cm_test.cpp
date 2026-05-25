// Calibration tests for the cable common-mode model.
//
// Reference: Paul, "Introduction to EMC" 2nd ed. Eq 11.5; Hockanson
// 1996 IEEE TEMC. Closed form for short (L < lambda/4) cable carrying
// common-mode current I:
//     |E_max| = (eta0 / c) * I * L * f / r
// Hand-computed reference points use
//     eta0 / c = 1.25663706e-6

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "emi/CableCommonMode.h"

using Catch::Approx;
using emikit::emi::CableSpec;
using emikit::emi::cable_cm_e_field;
using emikit::emi::cable_cm_e_field_dbuv;

TEST_CASE("cable: zero length -> zero field", "[cable]") {
    REQUIRE(cable_cm_e_field({0.0, 1.0e-3}, 100e6, 3.0) == 0.0);
}

TEST_CASE("cable: zero current -> zero field", "[cable]") {
    REQUIRE(cable_cm_e_field({0.3, 0.0}, 100e6, 3.0) == 0.0);
}

TEST_CASE("cable: 20uA, 30 cm, 100 MHz, 10 m matches Paul Eq 11.5",
          "[cable][calibration]") {
    // |E| = 1.25664e-6 * 20e-6 * 0.3 * 1e8 / 10
    //     = 7.540e-5 V/m
    //     = 75.4 uV/m
    //     = 37.55 dBuV/m
    CableSpec c{0.3, 20.0e-6};
    REQUIRE(cable_cm_e_field(c, 100e6, 10.0) == Approx(7.540e-5).epsilon(0.005));
    REQUIRE(cable_cm_e_field_dbuv(c, 100e6, 10.0) == Approx(37.55).margin(0.05));
}

TEST_CASE("cable: short-dipole regime is linear in f, I, and L",
          "[cable]") {
    CableSpec c{0.3, 20.0e-6};
    const double base = cable_cm_e_field(c, 100e6, 10.0);

    SECTION("doubling freq doubles field (short dipole)") {
        REQUIRE(cable_cm_e_field(c, 200e6, 10.0) ==
                Approx(2.0 * base).epsilon(1e-9));
    }
    SECTION("doubling current doubles field") {
        CableSpec c2 = c;
        c2.cm_current_a *= 2.0;
        REQUIRE(cable_cm_e_field(c2, 100e6, 10.0) ==
                Approx(2.0 * base).epsilon(1e-9));
    }
    SECTION("doubling length doubles field") {
        CableSpec c2 = c;
        c2.length_m *= 2.0;
        // Still well under lambda/4 = 0.75m at 100 MHz.
        REQUIRE(cable_cm_e_field(c2, 100e6, 10.0) ==
                Approx(2.0 * base).epsilon(1e-9));
    }
    SECTION("doubling distance halves field") {
        REQUIRE(cable_cm_e_field(c, 100e6, 20.0) ==
                Approx(0.5 * base).epsilon(1e-9));
    }
}

TEST_CASE("cable: TI ADS8686S working point sanity check",
          "[cable][calibration]") {
    // From the SBAA548A comparison: 30 cm USB-style cable, ~30 uA of
    // ground-bounce-driven CM at 480 MHz, 10 m chamber distance.
    // |E| = 1.25664e-6 * 30e-6 * 0.3 * 4.8e8 / 10
    //     = 5.43e-4 V/m -> 54.7 dBuV/m
    // This is what closes the gap to TI's measured peak.
    CableSpec c{0.3, 30.0e-6};
    REQUIRE(cable_cm_e_field_dbuv(c, 480e6, 10.0) ==
              Approx(54.71).margin(0.1));
}
// Append to cable_cm_test.cpp -- tests for estimate_cm_current and the
// BoardAnalysis cable integration.

TEST_CASE("estimator: explicit I_cm overrides L_gnd path", "[cable]") {
    CableSpec c;
    c.length_m = 0.3;
    c.cm_current_a = 5.0e-6;
    c.ground_inductance_h = 100.0e-9;   // would estimate much higher

    auto out = estimate_cm_current(c, {1.0e-3, 2.0e-3, 3.0e-3});
    REQUIRE(out.size() == 3);
    for (auto v : out) REQUIRE(v == Approx(5.0e-6));
}

TEST_CASE("estimator: ground-bounce ratio matches hand calc", "[cable]") {
    // L_gnd = 5 nH, cable 30 cm of 1 uH/m -> total cable L = 0.3 uH
    // ratio = 2 * 5e-9 / 0.3e-6 = 3.33e-2
    CableSpec c;
    c.length_m = 0.3;
    c.ground_inductance_h = 5.0e-9;
    c.cable_cm_inductance_per_m_h = 1.0e-6;

    auto out = estimate_cm_current(c, {1.0e-3, 5.0e-3, 10.0e-3});
    REQUIRE(out[0] == Approx(3.333e-5).epsilon(0.001));
    REQUIRE(out[1] == Approx(1.667e-4).epsilon(0.001));
    REQUIRE(out[2] == Approx(3.333e-4).epsilon(0.001));
}

TEST_CASE("estimator: returns zeros when no model fields set", "[cable]") {
    CableSpec c;
    c.length_m = 0.3;  // length alone is not enough
    auto out = estimate_cm_current(c, {1.0e-3, 2.0e-3});
    for (auto v : out) REQUIRE(v == 0.0);
}

TEST_CASE("estimator: hand-computed cable contribution for TI working point",
          "[cable][calibration]") {
    // 30 cm USB cable, signal current 1 mA at 480 MHz, 5 nH ground bounce
    // I_cm = (2 * 5e-9 / (1e-6 * 0.3)) * 1e-3 = 3.33e-2 * 1e-3 = 33.3 uA
    // E = (eta0/c) * 33.3e-6 * 0.3 * 4.8e8 / 10 = 6.03e-4 V/m -> 55.6 dBuV/m
    CableSpec c;
    c.length_m = 0.3;
    c.ground_inductance_h = 5.0e-9;
    c.cable_cm_inductance_per_m_h = 1.0e-6;

    auto i_cm = estimate_cm_current(c, {1.0e-3});
    REQUIRE(i_cm[0] == Approx(33.33e-6).epsilon(0.001));

    CableSpec instant = c;
    instant.cm_current_a = i_cm[0];
    REQUIRE(cable_cm_e_field_dbuv(instant, 480e6, 10.0) ==
              Approx(55.59).margin(0.1));
}
