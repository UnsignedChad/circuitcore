// Absolute-value calibration against the textbook closed form.
//
// loop_e_field implements the small magnetic-dipole far-field formula
// from Henry Ott, "Electromagnetic Compatibility Engineering" (Wiley
// 2009) Eq 11-2 -- also Clayton Paul "Introduction to Electromagnetic
// Compatibility" 2nd ed. Eq 8.62 with identical leading constants:
//
//     E (V/m) = (eta0 * pi * I * A * f^2) / (c^2 * r)
//
// The other emikit tests verify scaling (f^2, 1/r, linear in I and A).
// This file pins down the absolute value -- if someone fat-fingers
// eta0 or drops the pi, scaling tests still pass but these don't.
//
// Reference points were computed by hand against
//     eta0 = 376.730313668  ohm
//     c    = 2.99792458e8   m/s
// to ~5 sig figs.  Tolerance below is 0.05 dB which is well under the
// +/- 6 dB pre-compliance accuracy floor the tool claims.
//
// NOT validated here: the connection from a real PCB layout to the
// (I, A) inputs.  That is the small-loop approximation, which is
// honest only when loop perimeter << wavelength and the trace has a
// nearby reference plane.  Above ~1 GHz on a typical FR-4 board the
// approximation degrades; see Ott 11.3 and Paul 8.4 for the bounds.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "emi/LoopEmissions.h"

using emikit::emi::loop_e_field;
using emikit::emi::loop_e_field_dbuv;
using Catch::Approx;

TEST_CASE("calibration: 1mA, 1 cm^2, 100 MHz, 3 m matches Ott Eq 11-2",
          "[calibration]") {
    // I = 1 mA, A = 1 cm^2 = 1e-4 m^2, f = 100 MHz, r = 3 m
    // E = 376.73 * pi * 1e-3 * 1e-4 * 1e16 / (8.988e16 * 3)
    //   = 4.390 uV/m  ->  12.85 dBuV/m
    const double e_db = loop_e_field_dbuv(1.0e-4, 1.0e-3, 100.0e6, 3.0);
    REQUIRE(e_db == Approx(12.85).margin(0.05));
}

TEST_CASE("calibration: f=300 MHz scales by 9x vs 100 MHz reference",
          "[calibration]") {
    // Same inputs, freq tripled.  E scales as f^2 -> 9x linear,
    // +19.08 dB in dBuV/m. 12.85 + 19.08 = 31.93 dBuV/m.
    const double e_db = loop_e_field_dbuv(1.0e-4, 1.0e-3, 300.0e6, 3.0);
    REQUIRE(e_db == Approx(31.93).margin(0.05));
}

TEST_CASE("calibration: r=10 m drops by 10/3x vs 3 m reference",
          "[calibration]") {
    // 1/r scaling -> linear factor 0.3, -10.46 dB.
    // 12.85 - 10.46 = 2.39 dBuV/m.
    const double e_db = loop_e_field_dbuv(1.0e-4, 1.0e-3, 100.0e6, 10.0);
    REQUIRE(e_db == Approx(2.39).margin(0.05));
}

TEST_CASE("calibration: I=10 mA gives +20 dB over reference",
          "[calibration]") {
    // Linear in current -> factor 10, +20 dB.
    // 12.85 + 20.00 = 32.85 dBuV/m.
    const double e_db = loop_e_field_dbuv(1.0e-4, 1.0e-2, 100.0e6, 3.0);
    REQUIRE(e_db == Approx(32.85).margin(0.05));
}

TEST_CASE("calibration: V/m result matches hand calc to 3 sig figs",
          "[calibration]") {
    // 4.390 uV/m at the reference point.  Tolerance 0.5% of reading
    // -- there is no measurement uncertainty here, the only thing
    // being checked is that the constants didn't drift.
    const double e_v = loop_e_field(1.0e-4, 1.0e-3, 100.0e6, 3.0);
    REQUIRE(e_v == Approx(4.390e-6).epsilon(0.005));
}
