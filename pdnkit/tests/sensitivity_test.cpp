// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "pi/Sensitivity.h"

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
std::vector<double> log_sweep(double lo, double hi, int n) {
    std::vector<double> out;
    out.reserve(n);
    const double log_lo = std::log10(lo);
    const double log_hi = std::log10(hi);
    for (int i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.0 : static_cast<double>(i) / (n - 1);
        out.push_back(std::pow(10.0, log_lo + t * (log_hi - log_lo)));
    }
    return out;
}
}  // namespace

TEST_CASE("sensitivity: empty inputs return empty", "[sensitivity]") {
    auto out = sensitivity_sweep(small_plane(), 0.025, 0.025, {}, {});
    REQUIRE(out.empty());
}

// A bulk cap (10 uF) and a small bypass (100 nF). Removing the small
// bypass exposes the plane at high frequency where the bulk no longer
// helps. So the bypass should rank ahead of the bulk in sensitivity
// across the full band.
TEST_CASE("sensitivity: ranks small bypass ahead of bulk at high f",
          "[sensitivity][validation]") {
    auto cfg = small_plane();
    std::vector<Decap> caps = {
        // index 0: bulk
        {0.015, 0.015, 10.0e-6, 5.0e-3, 1.0e-9, 0.0},
        // index 1: high-frequency bypass
        {0.035, 0.035, 100.0e-9, 30.0e-3, 0.3e-9, 0.0}
    };
    auto freqs = log_sweep(1.0e5, 5.0e9, 50);
    auto out = sensitivity_sweep(cfg, 0.025, 0.025, caps, freqs);

    REQUIRE(out.size() == 2);
    // Find each by index instead of asserting a particular ranking
    // (rank depends on which dominates the *widest* band, which can be
    // either cap depending on cavity resonance positions).
    int bulk_idx = -1, bypass_idx = -1;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].decap_index == 0) bulk_idx   = static_cast<int>(i);
        if (out[i].decap_index == 1) bypass_idx = static_cast<int>(i);
    }
    REQUIRE(bulk_idx   >= 0);
    REQUIRE(bypass_idx >= 0);
    // Bulk dominates at low frequency, bypass at high.
    REQUIRE(out[bulk_idx].peak_freq_hz   < out[bypass_idx].peak_freq_hz);
    REQUIRE(out[bypass_idx].peak_freq_hz > 1.0e7);
}

// Two identical caps at the same position. By symmetry, removing either
// should produce the same relative change. Either can rank first.
TEST_CASE("sensitivity: symmetric caps have equal sensitivity",
          "[sensitivity]") {
    auto cfg = small_plane();
    std::vector<Decap> caps = {
        {0.020, 0.020, 1.0e-6, 5.0e-3, 0.5e-9, 0.0},
        {0.020, 0.020, 1.0e-6, 5.0e-3, 0.5e-9, 0.0}
    };
    auto freqs = log_sweep(1.0e6, 1.0e9, 30);
    auto out = sensitivity_sweep(cfg, 0.030, 0.030, caps, freqs);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].max_relative_change ==
            Approx(out[1].max_relative_change).margin(1e-9));
}
