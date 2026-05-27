// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <cmath>
#include <numbers>
#include "circuitcore/field/GridSpec.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/CavityModel.h"

using pdnkit::pi::CavityConfig;
using pdnkit::pi::cavity_impedance;
using pdnkit::pi::cavity_impedance_magnitude_sweep;
using Catch::Approx;

namespace {
constexpr double kPi = std::numbers::pi;
using circuitcore::field::kEps0;
constexpr double kC = 2.99792458e8;
}

TEST_CASE("cavity: DC behavior matches parallel-plate capacitor", "[cavity]") {
    // 100mm x 100mm x 1.6mm FR-4 plate pair.
    // C = eps_r * eps_0 * A / d = 4.3 * 8.854e-12 * 0.01 / 1.6e-3 ~ 238 pF
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.100;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.0;  // no loss for clean comparison
    cfg.max_modes = 50;

    const double f = 100.0;             // 100 Hz (deep DC)
    const double omega = 2.0 * kPi * f;
    // Self-impedance at the plane center.
    auto Z = cavity_impedance(cfg, 0.05, 0.05, 0.05, 0.05, omega);

    const double C_expected = cfg.eps_r * kEps0 * cfg.a * cfg.b / cfg.d;
    const double mag_expected = 1.0 / (omega * C_expected);

    // At DC, the m=n=0 term dominates: Z = j w mu d/(ab) * 1/(-w^2 mu eps) = 1/(jwC).
    REQUIRE(std::abs(Z) == Approx(mag_expected).epsilon(1e-4));
    // Capacitive: imaginary part should be negative (lossless DC).
    REQUIRE(std::imag(Z) < 0.0);
    REQUIRE(std::abs(std::real(Z)) < std::abs(std::imag(Z)) * 1.0e-6);
}

TEST_CASE("cavity: reciprocity Z(p1,p2) == Z(p2,p1)", "[cavity]") {
    CavityConfig cfg;
    cfg.a = 0.080;
    cfg.b = 0.060;
    cfg.d = 0.4e-3;
    cfg.max_modes = 20;

    const double omega = 2.0 * kPi * 1.0e8;  // 100 MHz
    auto Z12 = cavity_impedance(cfg, 0.020, 0.030, 0.070, 0.050, omega);
    auto Z21 = cavity_impedance(cfg, 0.070, 0.050, 0.020, 0.030, omega);

    REQUIRE(std::abs(Z12 - Z21) / std::abs(Z12) < 1e-12);
}

TEST_CASE("cavity: |Z| peaks near first TM10 resonance", "[cavity]") {
    // Resonant frequency of TM_mn = (c / (2 sqrt(eps_r))) * sqrt((m/a)^2 + (n/b)^2)
    // TM10: f10 = c / (2 a sqrt(eps_r))
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.100;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.005;   // small loss so peak is finite
    cfg.max_modes = 40;

    const double f10 = kC / (2.0 * cfg.a * std::sqrt(cfg.eps_r));
    // Self-impedance at corner where TM10 mode has max amplitude.
    const double x = 0.0, y = 0.0;

    auto Z_below = cavity_impedance(cfg, x, y, x, y, 2.0 * kPi * f10 * 0.5);
    auto Z_at    = cavity_impedance(cfg, x, y, x, y, 2.0 * kPi * f10 * 1.0);
    auto Z_above = cavity_impedance(cfg, x, y, x, y, 2.0 * kPi * f10 * 2.0);

    REQUIRE(std::abs(Z_at) > std::abs(Z_below));
    REQUIRE(std::abs(Z_at) > std::abs(Z_above));
}

TEST_CASE("cavity: sweep returns vector of expected length", "[cavity]") {
    CavityConfig cfg;
    cfg.max_modes = 10;
    std::vector<double> freqs;
    for (int i = 0; i < 20; ++i) freqs.push_back(1.0e6 * (i + 1));

    auto mags = cavity_impedance_magnitude_sweep(cfg, 0.01, 0.01, 0.02, 0.02, freqs);
    REQUIRE(mags.size() == freqs.size());
    for (double m : mags) {
        REQUIRE(std::isfinite(m));
        REQUIRE(m >= 0.0);
    }
}


TEST_CASE("cavity: empty decap list equals plain self-impedance", "[cavity][decap]") {
    CavityConfig cfg;
    cfg.max_modes = 20;
    const double omega = 2.0 * kPi * 1.0e7;
    auto z_plain = pdnkit::pi::cavity_impedance(cfg, 0.05, 0.05, 0.05, 0.05, omega);
    auto z_zero  = pdnkit::pi::cavity_impedance_with_decaps(cfg, 0.05, 0.05, {}, omega);
    REQUIRE(std::abs(z_plain - z_zero) / std::abs(z_plain) < 1e-12);
}

TEST_CASE("cavity: nearby decap pulls Z toward the parallel combination", "[cavity][decap]") {
    // Co-located ports make the cavity Z-matrix singular (rank-deficient);
    // real PCB layouts never have a decap at the exact observation pin --
    // there is always at least a few mm of trace. We offset the decap by
    // ~1 cell and verify the result is closer to (plane || cap) than to
    // plane alone.
    CavityConfig cfg;
    cfg.max_modes = 30;
    cfg.tan_delta = 0.005;
    const double omega = 2.0 * kPi * 1.0e7;
    const double xo = 0.05, yo = 0.05;

    auto z_plain = pdnkit::pi::cavity_impedance(cfg, xo, yo, xo, yo, omega);

    pdnkit::pi::Decap d{xo + 0.005, yo, 100.0e-9, 0.0, 0.0};  // 5mm offset
    auto z_dec = pdnkit::pi::cavity_impedance_with_decaps(cfg, xo, yo, {d}, omega);

    const std::complex<double> j(0, 1);
    const auto y_parallel = 1.0 / z_plain + j * omega * d.C;
    const auto z_parallel = 1.0 / y_parallel;

    REQUIRE(std::abs(z_dec - z_parallel) < std::abs(z_dec - z_plain));
}

TEST_CASE("cavity: adding a decap reduces |Z| in the cap-dominant band", "[cavity][decap]") {
    CavityConfig cfg;
    cfg.max_modes = 20;
    cfg.tan_delta = 0.005;

    // In the 10 MHz range a 1 uF decap with low ESR/ESL has |Z_cap| << plane |Z|,
    // so adding it must drop |Z_total| below the bare-plane value.
    const double omega = 2.0 * kPi * 1.0e7;
    const double xo = 0.05, yo = 0.05;

    auto z_plain = std::abs(pdnkit::pi::cavity_impedance(cfg, xo, yo, xo, yo, omega));

    pdnkit::pi::Decap d{xo + 0.005, yo, 1.0e-6, 0.005, 0.5e-9};  // 5mm offset
    auto z_with  = std::abs(pdnkit::pi::cavity_impedance_with_decaps(cfg, xo, yo, {d}, omega));

    REQUIRE(z_with < z_plain);
}

TEST_CASE("cavity: decap sweep magnitudes finite + non-negative", "[cavity][decap]") {
    CavityConfig cfg;
    cfg.max_modes = 10;
    std::vector<pdnkit::pi::Decap> dec{
        {0.04, 0.04, 1.0e-6, 0.005, 0.5e-9},
        {0.06, 0.06, 0.1e-6, 0.020, 0.3e-9},
    };
    std::vector<double> freqs;
    for (int i = 0; i < 25; ++i) freqs.push_back(1.0e6 * (i + 1));
    auto mags = pdnkit::pi::cavity_impedance_with_decaps_magnitude_sweep(
        cfg, 0.05, 0.05, dec, freqs);
    REQUIRE(mags.size() == freqs.size());
    for (double m : mags) {
        REQUIRE(std::isfinite(m));
        REQUIRE(m >= 0.0);
    }
}

TEST_CASE("cavity: peak frequency matches analytical TM10 within 5%", "[cavity][validation]") {
    // Rectangular plane, sweep |Z| over a band around f10, locate the peak,
    // assert it lands within 5% of c / (2 a sqrt(eps_r)). This is a tier-2
    // physical-correctness anchor (analytical mode-frequency reference).
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.060;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.005;
    cfg.max_modes = 30;

    const double f10_theory = kC / (2.0 * cfg.a * std::sqrt(cfg.eps_r));

    // Sweep tightly around f10.
    const double f_lo = 0.6 * f10_theory;
    const double f_hi = 1.4 * f10_theory;
    constexpr int N = 401;
    std::vector<double> freqs;
    for (int i = 0; i < N; ++i) {
        freqs.push_back(f_lo + (f_hi - f_lo) * i / (N - 1));
    }
    auto mags = pdnkit::pi::cavity_impedance_magnitude_sweep(
        cfg, 0.0, 0.0, 0.0, 0.0, freqs);

    std::size_t peak_i = 0;
    for (std::size_t i = 1; i < mags.size(); ++i) {
        if (mags[i] > mags[peak_i]) peak_i = i;
    }
    const double f_peak = freqs[peak_i];
    INFO("f10_theory = " << f10_theory / 1e6 << " MHz");
    INFO("f_peak     = " << f_peak     / 1e6 << " MHz");
    REQUIRE(std::abs(f_peak - f10_theory) / f10_theory < 0.05);
}

TEST_CASE("cavity: TM01 peak resolves on a non-square plane", "[cavity][validation]") {
    CavityConfig cfg;
    cfg.a = 0.100;
    cfg.b = 0.050;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.005;
    cfg.max_modes = 40;

    // TM01: peak in the direction perpendicular to the long axis.
    const double f01_theory = kC / (2.0 * cfg.b * std::sqrt(cfg.eps_r));

    // Excite the TM01 mode by putting the port at mid-x edge-y.
    const double f_lo = 0.7 * f01_theory;
    const double f_hi = 1.3 * f01_theory;
    constexpr int N = 401;
    std::vector<double> freqs;
    for (int i = 0; i < N; ++i) {
        freqs.push_back(f_lo + (f_hi - f_lo) * i / (N - 1));
    }
    auto mags = pdnkit::pi::cavity_impedance_magnitude_sweep(
        cfg, cfg.a / 2.0, 0.0, cfg.a / 2.0, 0.0, freqs);

    std::size_t peak_i = 0;
    for (std::size_t i = 1; i < mags.size(); ++i) {
        if (mags[i] > mags[peak_i]) peak_i = i;
    }
    const double f_peak = freqs[peak_i];
    INFO("f01_theory = " << f01_theory / 1e6 << " MHz");
    INFO("f_peak     = " << f_peak     / 1e6 << " MHz");
    REQUIRE(std::abs(f_peak - f01_theory) / f01_theory < 0.05);
}

TEST_CASE("cavity: peak frequency scales 1/sqrt(eps_r)", "[cavity][validation]") {
    // Same plane geometry, two different dielectrics. Peak frequency should
    // scale by 1/sqrt(eps_r). This catches numerical bugs that subtly couple
    // permittivity into the resonance frequency wrong.
    auto find_peak = [](double eps_r) {
        CavityConfig cfg;
        cfg.a = 0.080;
        cfg.b = 0.080;
        cfg.d = 1.0e-3;
        cfg.eps_r = eps_r;
        cfg.tan_delta = 0.005;
        cfg.max_modes = 30;

        const double f_theory = kC / (2.0 * cfg.a * std::sqrt(eps_r));
        const double f_lo = 0.5 * f_theory;
        const double f_hi = 1.5 * f_theory;
        constexpr int N = 501;
        std::vector<double> freqs;
        for (int i = 0; i < N; ++i) {
            freqs.push_back(f_lo + (f_hi - f_lo) * i / (N - 1));
        }
        auto mags = pdnkit::pi::cavity_impedance_magnitude_sweep(
            cfg, 0.0, 0.0, 0.0, 0.0, freqs);
        std::size_t pi_ = 0;
        for (std::size_t i = 1; i < mags.size(); ++i) {
            if (mags[i] > mags[pi_]) pi_ = i;
        }
        return freqs[pi_];
    };

    const double f1 = find_peak(2.2);  // PTFE
    const double f2 = find_peak(4.3);  // FR-4
    const double ratio = f1 / f2;
    const double ratio_theory = std::sqrt(4.3) / std::sqrt(2.2);
    INFO("f(eps=2.2) / f(eps=4.3) = " << ratio
                                       << "  theory " << ratio_theory);
    REQUIRE(std::abs(ratio - ratio_theory) / ratio_theory < 0.05);
}
