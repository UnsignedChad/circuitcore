#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "emi/Spectrum.h"

using emikit::emi::envelope_corners;
using emikit::emi::harmonic_magnitude;
using emikit::emi::spectrum_sweep;
using emikit::emi::TrapezoidalSpec;
using Catch::Approx;

TEST_CASE("spectrum: fundamental of a 100 MHz / 50% / 1 ns rise is "
          "well-behaved", "[spectrum]") {
    TrapezoidalSpec s;
    s.i_peak_a = 0.020;       // 20 mA peak
    s.period_s = 1.0 / 100e6;
    s.duty_cycle = 0.5;
    s.rise_time_s = 1.0e-9;
    // n=1: sinc(0.5)*sinc(0.1) -> about (2/pi) * 0.984. With
    // 2*Ipeak*duty = 20 mA, the fundamental is ~12.5 mA.
    REQUIRE(harmonic_magnitude(s, 1) > 0.010);
    REQUIRE(harmonic_magnitude(s, 1) < 0.015);
}

TEST_CASE("spectrum: even harmonics of a 50% duty cycle vanish",
          "[spectrum]") {
    TrapezoidalSpec s;
    s.duty_cycle = 0.5;
    REQUIRE(harmonic_magnitude(s, 2) < 1e-12);
    REQUIRE(harmonic_magnitude(s, 4) < 1e-12);
}

TEST_CASE("spectrum: corner frequencies match the closed form",
          "[spectrum]") {
    TrapezoidalSpec s;
    s.duty_cycle = 0.5;
    s.period_s = 1.0e-8;            // 10 ns
    s.rise_time_s = 1.0e-9;         // 1 ns
    auto c = envelope_corners(s);
    // f_tau = 1/(pi * 5 ns) = 63.66 MHz
    // f_tr  = 1/(pi * 1 ns) = 318.3 MHz
    REQUIRE(c.f_tau_hz == Approx(63.66e6).margin(0.5e6));
    REQUIRE(c.f_tr_hz  == Approx(318.3e6).margin(2.0e6));
}

TEST_CASE("spectrum: sweep returns one value per freq", "[spectrum]") {
    TrapezoidalSpec s;
    auto v = spectrum_sweep(s, {1e6, 100e6, 500e6, 1e9});
    REQUIRE(v.size() == 4);
}

TEST_CASE("spectrum: zero period -> zero output", "[spectrum]") {
    TrapezoidalSpec s;
    s.period_s = 0.0;
    REQUIRE(harmonic_magnitude(s, 1) == Approx(0.0));
    auto v = spectrum_sweep(s, {30e6, 100e6});
    for (double x : v) REQUIRE(x == Approx(0.0));
}
