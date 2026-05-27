// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "pi/Dielectric.h"

using pdnkit::pi::DjordjevicSarkar;
using pdnkit::pi::dj_sarkar_at;
using Catch::Approx;

// Default FR-4 fit: eps_inf=3.8, delta_eps=1.0, f1=1 kHz, f2=1 GHz.
// At DC (or far below f1) eps_r should be close to eps_inf + delta_eps = 4.8.
TEST_CASE("dj-sarkar: well below f1 -> eps_inf + delta_eps",
          "[dielectric][validation]") {
    DjordjevicSarkar m{};
    auto s = dj_sarkar_at(m, 1.0);     // 1 Hz, three decades below f1
    INFO("eps_r' at 1 Hz = " << s.eps_r_real);
    REQUIRE(s.eps_r_real == Approx(m.eps_inf + m.delta_eps).margin(0.05));
    REQUIRE(s.eps_r_imag < 0.01);      // tiny loss in this band
}

// Far above f2 -> eps_inf alone.
TEST_CASE("dj-sarkar: well above f2 -> eps_inf",
          "[dielectric][validation]") {
    DjordjevicSarkar m{};
    auto s = dj_sarkar_at(m, 1.0e12);  // 1 THz
    INFO("eps_r' at 1 THz = " << s.eps_r_real);
    REQUIRE(s.eps_r_real == Approx(m.eps_inf).margin(0.05));
}

// At f = f2 (the upper corner) the model has relaxed most of the way
// down toward eps_inf. With defaults (f1=1 kHz, f2=1 GHz) we sit right
// at the corner -- eps_r' just barely above eps_inf, and loss is small
// because we are leaving the dispersion band.
TEST_CASE("dj-sarkar: FR-4 default fit at f2 lands near eps_inf",
          "[dielectric][validation]") {
    DjordjevicSarkar m{};
    auto s = dj_sarkar_at(m, 1.0e9);
    INFO("eps_r' = " << s.eps_r_real << ", tan(delta) = " << s.tan_delta);
    REQUIRE(s.eps_r_real > 3.8);
    REQUIRE(s.eps_r_real < 4.0);
    REQUIRE(s.tan_delta  > 0.0);
    REQUIRE(s.tan_delta  < 0.03);
}

// Mid-band loss should peak somewhere between f1 and f2. Verify the
// imaginary part is larger at the geometric mean (sqrt(f1*f2)) than near
// the edges.
TEST_CASE("dj-sarkar: loss peaks mid-band", "[dielectric]") {
    DjordjevicSarkar m{};
    const double f_mid = std::sqrt(m.f1_hz * m.f2_hz);
    auto mid  = dj_sarkar_at(m, f_mid);
    auto low  = dj_sarkar_at(m, m.f1_hz * 0.01);
    auto high = dj_sarkar_at(m, m.f2_hz * 100.0);
    REQUIRE(mid.eps_r_imag > low.eps_r_imag);
    REQUIRE(mid.eps_r_imag > high.eps_r_imag);
}

// Monotonic: eps_r'(f) must decrease as f increases (causal dispersion).
TEST_CASE("dj-sarkar: eps_r real part decreases with frequency",
          "[dielectric]") {
    DjordjevicSarkar m{};
    auto a = dj_sarkar_at(m, 1.0e6);    // 1 MHz
    auto b = dj_sarkar_at(m, 1.0e9);    // 1 GHz
    auto c = dj_sarkar_at(m, 1.0e10);   // 10 GHz
    REQUIRE(a.eps_r_real > b.eps_r_real);
    REQUIRE(b.eps_r_real > c.eps_r_real);
}

// Edge: zero frequency, invalid corners -> safe fallback to eps_inf.
TEST_CASE("dj-sarkar: invalid inputs return eps_inf", "[dielectric]") {
    DjordjevicSarkar m{};
    REQUIRE(dj_sarkar_at(m, 0.0).eps_r_real == Approx(m.eps_inf));
    DjordjevicSarkar bad{4.0, 1.0, 0.0, 1.0e9};
    REQUIRE(dj_sarkar_at(bad, 1.0e6).eps_r_real == Approx(bad.eps_inf));
}
