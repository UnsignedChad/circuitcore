// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <algorithm>

#include "si/ChannelResponse.h"
#include "si/Touchstone.h"

using namespace sikit::dsp;
using namespace sikit::touchstone;
using Complex = std::complex<double>;
using Catch::Approx;

namespace {

// Build a 2-port Touchstone in-memory with the given S21(f) values.
// S11 = S12 = S22 = 0 (lossless reflectionless idealization).
TouchstoneFile make_channel(const std::vector<double>& freqs,
                             const std::vector<Complex>& s21) {
    TouchstoneFile t;
    t.num_ports = 2;
    t.format = Format::RealImaginary;
    t.reference_impedance = 50.0;
    t.frequency_scale = 1.0;
    t.frequencies = freqs;
    for (auto s : s21) {
        // Column-major: [S11, S21, S12, S22].
        t.s_matrices.push_back({Complex(0,0), s, Complex(0,0), Complex(0,0)});
    }
    return t;
}

}  // namespace

TEST_CASE("channel: interpolate_s21 at exact grid points", "[channel]") {
    auto ch = make_channel({1e9, 2e9, 3e9},
                            {Complex(0.9, 0), Complex(0.7, 0), Complex(0.4, 0)});
    REQUIRE(interpolate_s21(ch, 1e9).real() == Approx(0.9));
    REQUIRE(interpolate_s21(ch, 2e9).real() == Approx(0.7));
    REQUIRE(interpolate_s21(ch, 3e9).real() == Approx(0.4));
}

TEST_CASE("channel: interpolate_s21 linear between points", "[channel]") {
    auto ch = make_channel({1e9, 2e9}, {Complex(1.0, 0), Complex(0.0, 0)});
    REQUIRE(interpolate_s21(ch, 1.5e9).real() == Approx(0.5));
}

TEST_CASE("channel: interpolate_s21 below range is passthrough", "[channel]") {
    auto ch = make_channel({1e9, 2e9}, {Complex(0.5, 0), Complex(0.5, 0)});
    auto z = interpolate_s21(ch, 0.0);
    REQUIRE(z.real() == Approx(1.0));
    REQUIRE(z.imag() == Approx(0.0));
}

TEST_CASE("channel: interpolate_s21 above range clamps to last value", "[channel]") {
    auto ch = make_channel({1e9, 2e9}, {Complex(1.0, 0), Complex(0.2, 0)});
    REQUIRE(interpolate_s21(ch, 10e9).real() == Approx(0.2));
}

TEST_CASE("channel: identity S21 reproduces TX waveform", "[channel]") {
    // S21 = 1 across the relevant band → output = input (round-trip via FFT).
    const double fs = 32e9;  // 32 GS/s
    std::vector<double> freqs;
    std::vector<Complex> s21;
    for (double f = 0.1e9; f <= 20e9; f += 0.1e9) {
        freqs.push_back(f);
        s21.push_back(Complex(1.0, 0));
    }
    auto ch = make_channel(freqs, s21);

    std::vector<double> tx(256);
    for (std::size_t i = 0; i < tx.size(); ++i) {
        tx[i] = std::sin(2.0 * std::numbers::pi * 1.0e9 * i / fs);
    }
    auto rx = apply_channel(tx, fs, ch);

    // Identity channel should give back the input up to FFT round-off.
    REQUIRE(rx.size() == tx.size());
    for (std::size_t i = 0; i < tx.size(); ++i) {
        REQUIRE(rx[i] == Approx(tx[i]).margin(1e-8));
    }
}

TEST_CASE("channel: low-pass S21 attenuates a high-frequency tone", "[channel]") {
    // S21 drops linearly from 1 at 1 GHz to 0.05 at 10 GHz.
    std::vector<double> freqs;
    std::vector<Complex> s21;
    for (double f = 0.1e9; f <= 20e9; f += 0.1e9) {
        const double mag = std::max(0.05, 1.0 - (f - 1e9) / 9e9 * 0.95);
        freqs.push_back(f);
        s21.push_back(Complex(mag, 0));
    }
    auto ch = make_channel(freqs, s21);

    const double fs = 32e9;
    const std::size_t N = 1024;
    std::vector<double> tx_lo(N), tx_hi(N);
    for (std::size_t i = 0; i < N; ++i) {
        tx_lo[i] = std::sin(2.0 * std::numbers::pi * 0.5e9 * i / fs);   // 500 MHz, in passband
        tx_hi[i] = std::sin(2.0 * std::numbers::pi * 8.0e9 * i / fs);   // 8 GHz, attenuated
    }

    auto rx_lo = apply_channel(tx_lo, fs, ch);
    auto rx_hi = apply_channel(tx_hi, fs, ch);

    // Peak amplitude after steady-state (skip first 200 samples for transient).
    auto peak = [](const std::vector<double>& v) {
        double m = 0;
        for (std::size_t i = 200; i < v.size(); ++i) m = std::max(m, std::abs(v[i]));
        return m;
    };

    REQUIRE(peak(rx_lo) > 0.8);   // low-band passes mostly intact
    REQUIRE(peak(rx_hi) < 0.3);   // high-band heavily attenuated
}

TEST_CASE("channel: empty input returns empty", "[channel]") {
    auto ch = make_channel({1e9}, {Complex(1, 0)});
    REQUIRE(apply_channel({}, 1e9, ch).empty());
}

TEST_CASE("channel: rejects non-2-port file", "[channel]") {
    TouchstoneFile t;
    t.num_ports = 1;
    t.frequencies = {1e9};
    t.s_matrices = {{Complex(0, 0)}};
    REQUIRE_THROWS(apply_channel({1.0, 2.0}, 1e9, t));
}
