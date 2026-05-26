#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

#include "pi/CavityModel.h"

using namespace pdnkit::pi;
using Catch::Approx;

namespace {
CavityConfig fr4() {
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.080;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.020;
    cfg.max_modes = 15;
    return cfg;
}
}  // namespace

TEST_CASE("cavity-hj: zero roughness is identical to default",
          "[cavity-hj][validation]") {
    CavityConfig a = fr4();
    a.conductor_roughness_rq_m = 0.0;
    CavityConfig b = fr4();
    const double f = 1.0e8;
    const double w = 2.0 * std::numbers::pi * f;
    auto za = cavity_impedance(a, 0.020, 0.020, 0.080, 0.060, w);
    auto zb = cavity_impedance(b, 0.020, 0.020, 0.080, 0.060, w);
    REQUIRE(za.real() == Approx(zb.real()));
    REQUIRE(za.imag() == Approx(zb.imag()));
}

TEST_CASE("cavity-hj: roughness damps the resonance peak",
          "[cavity-hj][validation]") {
    const double f = 723.0e6;
    const double w = 2.0 * std::numbers::pi * f;
    CavityConfig smooth = fr4();
    CavityConfig rough  = fr4();
    rough.conductor_roughness_rq_m = 5.0e-6;
    auto z_smooth = cavity_impedance(smooth, 0.020, 0.020, 0.080, 0.060, w);
    auto z_rough  = cavity_impedance(rough,  0.020, 0.020, 0.080, 0.060, w);
    INFO("|Z_smooth| = " << std::abs(z_smooth)
         << "  |Z_rough| = " << std::abs(z_rough));
    REQUIRE(std::abs(z_rough) < std::abs(z_smooth));
}

TEST_CASE("cavity-hj: off-resonance change is small", "[cavity-hj]") {
    const double f = 50.0e6;
    const double w = 2.0 * std::numbers::pi * f;
    CavityConfig smooth = fr4();
    CavityConfig rough  = fr4();
    rough.conductor_roughness_rq_m = 5.0e-6;
    auto z_smooth = cavity_impedance(smooth, 0.020, 0.020, 0.080, 0.060, w);
    auto z_rough  = cavity_impedance(rough,  0.020, 0.020, 0.080, 0.060, w);
    const double rel = std::abs(std::abs(z_rough) - std::abs(z_smooth))
                       / std::abs(z_smooth);
    REQUIRE(rel < 0.05);
}
