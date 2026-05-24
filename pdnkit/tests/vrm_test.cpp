#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "pi/Vrm.h"

using pdnkit::pi::VrmModel;
using pdnkit::pi::vrm_impedance;
using Catch::Approx;

// At DC, Z = R_droop alone, purely real.
TEST_CASE("vrm: at DC Z = R_droop", "[vrm][validation]") {
    VrmModel m{};   // 5 mOhm, 1 uH defaults
    auto z = vrm_impedance(m, 0.0);
    REQUIRE(z.real() == Approx(m.r_droop_ohm));
    REQUIRE(z.imag() == Approx(0.0));
}

// At the L/R corner (omega = R/L), |Z| = R*sqrt(2).
TEST_CASE("vrm: at R/L corner |Z| = R*sqrt(2)", "[vrm][validation]") {
    VrmModel m{};
    const double w_corner = m.r_droop_ohm / m.l_out_h;
    auto z = vrm_impedance(m, w_corner);
    const double mag = std::abs(z);
    INFO("|Z| at corner = " << mag * 1000.0 << " mOhm");
    REQUIRE(mag == Approx(m.r_droop_ohm * std::sqrt(2.0)).margin(1e-9));
}

// Well above corner: |Z| ~ omega*L, phase ~ 90 deg.
TEST_CASE("vrm: well above corner Z is inductive", "[vrm]") {
    VrmModel m{};   // corner at R/L = 5e-3/1e-6 = 5000 rad/s -> ~800 Hz
    const double f = 1.0e7;   // 10 MHz, 4 decades above corner
    const double w = 2.0 * std::numbers::pi * f;
    auto z = vrm_impedance(m, w);
    REQUIRE(std::abs(z.imag()) > 100.0 * z.real());
    REQUIRE(z.imag() == Approx(w * m.l_out_h));
    const double phase_deg = std::atan2(z.imag(), z.real()) * 180.0
                              / std::numbers::pi;
    INFO("phase = " << phase_deg << " deg");
    REQUIRE(phase_deg > 89.0);
}

// Custom parameters: a tightly-controlled VRM with very low droop and
// big output inductor.
TEST_CASE("vrm: custom parameters scale as expected", "[vrm]") {
    VrmModel m{0.5e-3, 4.7e-6};
    auto z_dc  = vrm_impedance(m, 0.0);
    auto z_1mhz = vrm_impedance(m, 2.0 * std::numbers::pi * 1.0e6);
    REQUIRE(z_dc.real() == Approx(0.5e-3));
    REQUIRE(z_1mhz.imag() == Approx(2.0 * std::numbers::pi * 1.0e6 * 4.7e-6));
}
