// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>

#include "si/Connector.h"
#include "si/Touchstone.h"

using namespace sikit::si;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {

std::vector<double> log_freqs(int n, double f_lo, double f_hi) {
    std::vector<double> g;
    g.reserve(n);
    const double log_lo = std::log(f_lo);
    const double log_hi = std::log(f_hi);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        g.push_back(std::exp(log_lo + t * (log_hi - log_lo)));
    }
    return g;
}

}  // namespace

TEST_CASE("connector: SMA edge launch generates a low-loss 2-port",
          "[connector]") {
    auto spec = preset_sma_edge_launch();
    auto freqs = log_freqs(64, 1e8, 30e9);
    auto t = generate_connector_touchstone(spec, freqs);
    REQUIRE(t.num_ports == 2);
    REQUIRE(t.frequencies.size() == freqs.size());
    // |S21| at 1 GHz should be very close to 1 (low-loss).
    const auto s21_low = std::abs(t.s_matrices.front()[1]);
    REQUIRE(s21_low > 0.95);
    // |S21| at 30 GHz still > 0.7 (sqrt-shape, low slope).
    const auto s21_high = std::abs(t.s_matrices.back()[1]);
    REQUIRE(s21_high > 0.70);
    // Monotonic decrease.
    REQUIRE(s21_high < s21_low);
}

TEST_CASE("connector: 2-port output is reciprocal and symmetric",
          "[connector]") {
    auto spec = preset_sma_panel_mount();
    auto freqs = log_freqs(16, 1e8, 10e9);
    auto t = generate_connector_touchstone(spec, freqs);
    for (const auto& m : t.s_matrices) {
        REQUIRE(m[0] == m[3]);   // S11 == S22
        REQUIRE(m[1] == m[2]);   // S21 == S12 (reciprocal)
    }
}

TEST_CASE("connector: USB-C diff connector emits a 4-port file",
          "[connector]") {
    auto spec = preset_usb_c_diff_pair();
    auto freqs = log_freqs(32, 1e8, 25e9);
    auto t = generate_connector_touchstone(spec, freqs);
    REQUIRE(t.num_ports == 4);
    REQUIRE(t.s_matrices.front().size() == 16);
    // S31 (P_near -> P_far, the through) magnitude near unity at low freq.
    const Complex s31 = t.s_matrices.front()[2 + 0 * 4];
    REQUIRE(std::abs(s31) > 0.9);
}

TEST_CASE("connector: USB-C notch produces a measurable dip near 18 GHz",
          "[connector]") {
    auto spec = preset_usb_c_diff_pair();
    auto freqs = log_freqs(128, 1e9, 30e9);
    auto t = generate_connector_touchstone(spec, freqs);
    // Find min |S31| across the band and the freq it occurs at.
    double min_mag = 1.0;
    double min_freq = 0.0;
    for (std::size_t k = 0; k < freqs.size(); ++k) {
        const auto s31 = t.s_matrices[k][2 + 0 * 4];
        const double m = std::abs(s31);
        if (m < min_mag) { min_mag = m; min_freq = freqs[k]; }
    }
    // The notch sits around 18 GHz; allow generous tolerance because
    // the Lorentzian shape doesn't deepen at the exact resonance for
    // sparse log grids.
    REQUIRE(min_freq > 10e9);
    REQUIRE(min_freq < 25e9);
}

TEST_CASE("connector: preset registry is consistent", "[connector]") {
    for (const auto& n : available_connector_presets()) {
        const auto spec = connector_preset_by_name(n);
        REQUIRE(spec.name == n);
        REQUIRE(spec.num_ports >= 2);
        REQUIRE(spec.il_slope_db_per_sqrt_ghz > 0.0);
    }
}

TEST_CASE("connector: preset_by_name rejects unknown names", "[connector]") {
    REQUIRE_THROWS(connector_preset_by_name("not a real connector"));
}

TEST_CASE("connector: rejects degenerate frequency input", "[connector]") {
    auto spec = preset_sma_edge_launch();
    REQUIRE_THROWS(generate_connector_touchstone(spec, {}));
    REQUIRE_THROWS(generate_connector_touchstone(spec, {0.0}));
    REQUIRE_THROWS(generate_connector_touchstone(spec, {2e9, 1e9}));
}

TEST_CASE("connector: harsher RL preset produces louder |S11|",
          "[connector]") {
    auto lossy = preset_sma_edge_launch();
    auto worse = preset_sma_panel_mount();
    auto freqs = log_freqs(16, 1e9, 10e9);
    auto t_low  = generate_connector_touchstone(lossy, freqs);
    auto t_high = generate_connector_touchstone(worse, freqs);
    // Panel-mount has RL=20 dB; edge-launch has RL=25 dB. Higher RL
    // (in dB) means QUIETER reflection -- so panel-mount's |S11|
    // should be LARGER.
    REQUIRE(std::abs(t_high.s_matrices.front()[0]) >
            std::abs(t_low.s_matrices.front()[0]));
}
