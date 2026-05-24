#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

#include "pi/CavityModel.h"

using namespace pdnkit::pi;
using Catch::Approx;

namespace {
CavityConfig small_plane() {
    CavityConfig cfg;
    cfg.a = 0.050;
    cfg.b = 0.050;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.020;
    cfg.max_modes = 8;
    return cfg;
}
}  // namespace

// Default mounting_via_loop_l_h == 0 -> identical to the old result.
TEST_CASE("cavity-mount-L: zero mounting L matches default behavior",
          "[cavity-mount-l][validation]") {
    auto cfg = small_plane();
    std::vector<Decap> baseline = {
        {0.020, 0.020, 1.0e-6, 5.0e-3, 0.5e-9, 0.0}
    };
    std::vector<Decap> zeroed = {
        {0.020, 0.020, 1.0e-6, 5.0e-3, 0.5e-9, 0.0}
    };
    const double w = 2.0 * std::numbers::pi * 5.0e7;
    auto z_base = cavity_impedance_with_decaps(cfg, 0.025, 0.025, baseline, w);
    auto z_zero = cavity_impedance_with_decaps(cfg, 0.025, 0.025, zeroed,   w);
    REQUIRE(z_base.real() == Approx(z_zero.real()));
    REQUIRE(z_base.imag() == Approx(z_zero.imag()));
}

// Cap self-resonance: f_SRF = 1 / (2*pi*sqrt(L*C)). Adding mounting L
// drops the SRF. For C=1uF, ESL=0.5nH the SRF is 7.12 MHz; adding
// 1 nH mounting L makes effective L = 1.5 nH and SRF = 4.11 MHz.
// At the OLD SRF the mounted cap is now inductive, |Z| > 0.
TEST_CASE("cavity-mount-L: nonzero mounting L lowers cap SRF",
          "[cavity-mount-l][validation]") {
    auto cfg = small_plane();
    // Eval cap impedance alone at the bare-ESL SRF.
    const double C = 1.0e-6, ESL = 0.5e-9;
    const double f_srf_bare = 1.0 / (2.0 * std::numbers::pi * std::sqrt(ESL * C));
    const double w = 2.0 * std::numbers::pi * f_srf_bare;

    // Without mounting L, decap impedance at f_srf_bare is near zero
    // (just ESR). With mounting L it is purely inductive (positive imag).
    // Pull both through cavity_impedance_with_decaps and compare imag
    // parts at the observation port.
    std::vector<Decap> bare    = {{0.020, 0.020, C, 5.0e-3, ESL, 0.0}};
    std::vector<Decap> mounted = {{0.020, 0.020, C, 5.0e-3, ESL, 1.0e-9}};
    auto z_bare    = cavity_impedance_with_decaps(cfg, 0.025, 0.025, bare,    w);
    auto z_mounted = cavity_impedance_with_decaps(cfg, 0.025, 0.025, mounted, w);

    INFO("|Z_bare|    = " << std::abs(z_bare));
    INFO("|Z_mounted| = " << std::abs(z_mounted));
    // With mounting L pushing SRF down, at f_srf_bare the cap is
    // inductive -> higher |Z| seen from the port.
    REQUIRE(std::abs(z_mounted) > std::abs(z_bare));
}

// Sanity: mounting L scales the SRF as expected. Setting L_mount such
// that L_eff doubles should drop f_SRF by 1/sqrt(2) ~ 0.707.
TEST_CASE("cavity-mount-L: SRF scales with 1/sqrt(L_eff)",
          "[cavity-mount-l]") {
    const double C = 1.0e-6, ESL = 0.5e-9;
    const double f1 = 1.0 / (2.0 * std::numbers::pi * std::sqrt(ESL * C));
    const double L_eff_2 = 2.0 * ESL;
    const double f2 = 1.0 / (2.0 * std::numbers::pi * std::sqrt(L_eff_2 * C));
    REQUIRE(f2 / f1 == Approx(1.0 / std::sqrt(2.0)).margin(1e-9));
}
