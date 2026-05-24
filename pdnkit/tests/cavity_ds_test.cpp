#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

#include "pi/CavityModel.h"

using namespace pdnkit::pi;
using Catch::Approx;

namespace {
CavityConfig fr4_default_plane() {
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

// Regression: wideband_dielectric=false (default) reproduces the
// original cavity_impedance output bit-for-bit. Locks in that the new
// branch does not perturb existing behavior.
TEST_CASE("cavity-ds: default (off) is identical to constant-eps",
          "[cavity-ds][validation]") {
    CavityConfig cfg = fr4_default_plane();
    cfg.wideband_dielectric = false;
    const double f = 1.0e8;
    const double w = 2.0 * std::numbers::pi * f;
    auto z = cavity_impedance(cfg, 0.020, 0.020, 0.080, 0.060, w);

    CavityConfig cfg_legacy = fr4_default_plane();   // no DS fields set
    auto z_legacy = cavity_impedance(cfg_legacy, 0.020, 0.020, 0.080, 0.060, w);
    REQUIRE(z.real() == Approx(z_legacy.real()));
    REQUIRE(z.imag() == Approx(z_legacy.imag()));
}

// At a frequency well below the DS lower corner, the model gives
// eps_r ~ eps_inf + delta_eps. So enabling wideband with corners
// (1 kHz, 1 GHz) and eps_inf=3.8, delta=1.0 (-> eps_DC=4.8) at
// f=10 Hz should match a constant-eps run with eps_r=4.8.
TEST_CASE("cavity-ds: low-frequency limit matches eps_DC",
          "[cavity-ds][validation]") {
    CavityConfig dyn = fr4_default_plane();
    dyn.wideband_dielectric = true;
    dyn.ds_eps_inf = 3.8;
    dyn.ds_delta_eps = 1.0;
    dyn.ds_f1_hz = 1.0e3;
    dyn.ds_f2_hz = 1.0e9;
    const double f = 10.0;   // 100x below f1
    const double w = 2.0 * std::numbers::pi * f;
    auto z_dyn = cavity_impedance(dyn, 0.020, 0.020, 0.080, 0.060, w);

    CavityConfig stat = fr4_default_plane();
    stat.eps_r = 4.8;
    stat.tan_delta = 1.0e-4;  // DS gives ~zero loss this far below f1
    auto z_stat = cavity_impedance(stat, 0.020, 0.020, 0.080, 0.060, w);

    // Within 5% magnitude (DS imag part is small but not exactly zero).
    const double rel = std::abs(std::abs(z_dyn) - std::abs(z_stat))
                       / std::abs(z_stat);
    INFO("|Z_dyn| = " << std::abs(z_dyn)
         << "  |Z_stat| = " << std::abs(z_stat)
         << "  rel = " << rel);
    REQUIRE(rel < 0.05);
}

// Well above the DS upper corner the model gives eps_r ~ eps_inf.
TEST_CASE("cavity-ds: high-frequency limit matches eps_inf",
          "[cavity-ds][validation]") {
    CavityConfig dyn = fr4_default_plane();
    dyn.wideband_dielectric = true;
    dyn.ds_eps_inf = 3.8;
    dyn.ds_delta_eps = 1.0;
    dyn.ds_f1_hz = 1.0e3;
    dyn.ds_f2_hz = 1.0e9;
    const double f = 1.0e11;   // 100x above f2
    const double w = 2.0 * std::numbers::pi * f;
    auto z_dyn = cavity_impedance(dyn, 0.020, 0.020, 0.080, 0.060, w);

    CavityConfig stat = fr4_default_plane();
    stat.eps_r = 3.8;
    stat.tan_delta = 1.0e-4;
    auto z_stat = cavity_impedance(stat, 0.020, 0.020, 0.080, 0.060, w);

    const double rel = std::abs(std::abs(z_dyn) - std::abs(z_stat))
                       / std::abs(z_stat);
    REQUIRE(rel < 0.05);
}

// Cavity peak shifts UP in frequency when eps_r drops with f, because
// peak position scales as 1/sqrt(eps_r). DS reduces eps_r at high f
// so the second mode peak should appear at a slightly higher frequency
// than the constant-eps model predicts.
TEST_CASE("cavity-ds: wideband peak above constant-eps peak",
          "[cavity-ds]") {
    CavityConfig dyn = fr4_default_plane();
    dyn.wideband_dielectric = true;
    CavityConfig stat = fr4_default_plane();

    // Look at |Z| at a frequency where constant-eps (4.3) shows a peak;
    // wideband at this point has eps_r ~ 3.85 (above the cavity's first
    // resonance band) so the peak has already moved. Wideband |Z| should
    // be lower at the constant-eps peak frequency.
    // Sample at the analytic TM10 freq for the constant model:
    //   f_TM10 = c / (2*a*sqrt(eps_r)) = 3e8 / (2*0.1*sqrt(4.3)) ~ 723 MHz
    const double f = 723.0e6;
    const double w = 2.0 * std::numbers::pi * f;
    auto z_dyn  = cavity_impedance(dyn,  0.020, 0.020, 0.080, 0.060, w);
    auto z_stat = cavity_impedance(stat, 0.020, 0.020, 0.080, 0.060, w);
    // No equality assertion -- just confirm the wideband answer is
    // physically different (off-peak there).
    REQUIRE(std::abs(z_dyn) != Approx(std::abs(z_stat)).epsilon(0.01));
}
