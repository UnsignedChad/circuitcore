// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// PackageDefaults lookup sanity. The numbers themselves are guesses
// rounded from JEDEC tables, so we just check the lookup picks the
// right family (and falls through to the generic default when no
// family token is present).

#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/PackageDefaults.h"

using circuitcore::board::default_body_height_m;
using circuitcore::board::default_mass_kg;

TEST_CASE("package defaults: chip resistors / caps recognised",
          "[package-defaults]") {
    // 0402 should land in the 0.30..0.60 mm range.
    REQUIRE(default_body_height_m("Resistor_SMD:R_0402_1005Metric") < 0.6e-3);
    REQUIRE(default_body_height_m("Resistor_SMD:R_0402_1005Metric") > 0.3e-3);

    // 0805 should be taller.
    REQUIRE(default_body_height_m("Capacitor_SMD:C_0805_2012Metric")
            > default_body_height_m("Capacitor_SMD:C_0402_1005Metric"));
}

TEST_CASE("package defaults: ICs differentiated by family",
          "[package-defaults]") {
    const auto h_qfn  = default_body_height_m("Package_DFN_QFN:QFN-32_5x5mm_P0.5mm");
    const auto h_soic = default_body_height_m("Package_SO:SOIC-8_3.9x4.9mm_P1.27mm");
    const auto h_dip  = default_body_height_m("Package_DIP:DIP-14_W7.62mm");
    REQUIRE(h_qfn  > 0.0);
    REQUIRE(h_soic > h_qfn);
    REQUIRE(h_dip  > h_soic);
}

TEST_CASE("package defaults: connectors get a tall body",
          "[package-defaults]") {
    const auto h = default_body_height_m("Connector_PinHeader_2.54mm:"
                                         "PinHeader_1x02_P2.54mm_Vertical");
    REQUIRE(h > 5.0e-3);
}

TEST_CASE("package defaults: unknown footprint returns generic default",
          "[package-defaults]") {
    const auto h = default_body_height_m("WeirdLib:TotallyUnknownThing");
    REQUIRE(h == 1.0e-3);
    const auto m = default_mass_kg("WeirdLib:TotallyUnknownThing");
    REQUIRE(m == 0.1e-3);
}

TEST_CASE("package defaults: mass scales roughly with size",
          "[package-defaults]") {
    REQUIRE(default_mass_kg("R_0402") < default_mass_kg("R_1206"));
    REQUIRE(default_mass_kg("SOIC-8") < default_mass_kg("DIP-14"));
}
