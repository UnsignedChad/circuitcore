// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
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
