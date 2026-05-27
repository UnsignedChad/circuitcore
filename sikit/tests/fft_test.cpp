// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "si/Fft.h"

using namespace sikit::dsp;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {
constexpr double kEps = 1e-10;
}

TEST_CASE("fft: next_power_of_2 basics", "[fft]") {
    REQUIRE(next_power_of_2(0) == 1);
    REQUIRE(next_power_of_2(1) == 1);
    REQUIRE(next_power_of_2(2) == 2);
    REQUIRE(next_power_of_2(3) == 4);
    REQUIRE(next_power_of_2(1000) == 1024);
    REQUIRE(next_power_of_2(1024) == 1024);
    REQUIRE(next_power_of_2(1025) == 2048);
}

TEST_CASE("fft: rejects non-power-of-2 size", "[fft]") {
    std::vector<Complex> v(6, Complex(0, 0));
    REQUIRE_THROWS(fft(v));
}

TEST_CASE("fft: trivial sizes are no-ops", "[fft]") {
    std::vector<Complex> v0;
    fft(v0);
    REQUIRE(v0.empty());

    std::vector<Complex> v1{Complex(3.14, 2.7)};
    fft(v1);
    REQUIRE(v1[0].real() == Approx(3.14));
    REQUIRE(v1[0].imag() == Approx(2.7));
}

TEST_CASE("fft: impulse at index 0 maps to flat unit spectrum", "[fft]") {
    std::vector<Complex> v(8, Complex(0, 0));
    v[0] = Complex(1, 0);
    fft(v);
    for (const auto& z : v) {
        REQUIRE(z.real() == Approx(1.0).margin(kEps));
        REQUIRE(z.imag() == Approx(0.0).margin(kEps));
    }
}

TEST_CASE("fft: constant signal maps to spike at DC bin", "[fft]") {
    std::vector<Complex> v(8, Complex(1, 0));
    fft(v);
    // Forward DFT of constant 1 → 8 at DC (no normalization), 0 elsewhere.
    REQUIRE(v[0].real() == Approx(8.0).margin(kEps));
    REQUIRE(v[0].imag() == Approx(0.0).margin(kEps));
    for (std::size_t i = 1; i < v.size(); ++i) {
        REQUIRE(std::abs(v[i]) < kEps);
    }
}

TEST_CASE("fft: forward then inverse round-trips with 1/N normalization", "[fft]") {
    const std::size_t N = 64;
    std::vector<Complex> orig(N);
    for (std::size_t i = 0; i < N; ++i) {
        // Mixed real + imaginary, non-trivial.
        orig[i] = Complex(std::cos(2.0 * std::numbers::pi * 5.0 * i / N),
                          std::sin(2.0 * std::numbers::pi * 3.0 * i / N));
    }
    auto data = orig;
    fft(data, false);
    fft(data, true);
    for (std::size_t i = 0; i < N; ++i) {
        REQUIRE(data[i].real() == Approx(orig[i].real()).margin(1e-10));
        REQUIRE(data[i].imag() == Approx(orig[i].imag()).margin(1e-10));
    }
}

TEST_CASE("fft: pure sinusoid produces a single spectral peak", "[fft]") {
    const std::size_t N = 1024;
    const std::size_t k = 17;  // integer bin → no spectral leakage
    std::vector<Complex> v(N);
    for (std::size_t i = 0; i < N; ++i) {
        v[i] = Complex(std::cos(2.0 * std::numbers::pi * k * i / N), 0.0);
    }
    fft(v);
    // Energy concentrates at bins k and N-k (the negative frequency mirror).
    REQUIRE(std::abs(v[k])     == Approx(N / 2.0).margin(1e-8));
    REQUIRE(std::abs(v[N - k]) == Approx(N / 2.0).margin(1e-8));

    // Other bins are quiet (a few orders of magnitude below).
    for (std::size_t i = 0; i < N; ++i) {
        if (i == k || i == N - k) continue;
        REQUIRE(std::abs(v[i]) < 1e-8);
    }
}

TEST_CASE("fft: linearity holds (sum of inputs → sum of spectra)", "[fft]") {
    const std::size_t N = 32;
    std::vector<Complex> a(N), b(N), sum(N);
    for (std::size_t i = 0; i < N; ++i) {
        a[i] = Complex(std::sin(0.1 * i),  std::cos(0.07 * i));
        b[i] = Complex(std::cos(0.05 * i), std::sin(0.13 * i));
        sum[i] = a[i] + b[i];
    }
    auto fa = a, fb = b, fsum = sum;
    fft(fa);
    fft(fb);
    fft(fsum);
    for (std::size_t i = 0; i < N; ++i) {
        const auto expected = fa[i] + fb[i];
        REQUIRE(fsum[i].real() == Approx(expected.real()).margin(1e-10));
        REQUIRE(fsum[i].imag() == Approx(expected.imag()).margin(1e-10));
    }
}
