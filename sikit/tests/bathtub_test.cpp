// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/Bathtub.h"
#include "si/Eye.h"

using namespace sikit::eye;
using Catch::Approx;

TEST_CASE("bathtub: non-trivial waveform produces a non-flat curve", "[bathtub]") {
    // Heavy ISI: the signal spends much of each UI in transit through
    // mid-voltage, so the curve has measurable BER somewhere.
    auto bits = prbs7(2000);
    auto tx = nrz_waveform(bits, 32);
    auto rx = rc_lowpass(tx, 1.0, 0.005);
    auto eye = build_eye(rx, 32, 128, 96, /*warmup=*/8);

    auto bt = compute_bathtub(eye);
    REQUIRE(!bt.ui_offset.empty());
    REQUIRE(bt.ui_offset.size() == bt.ber.size());

    // BER values lie in [0, 1].
    double vmax = 0.0;
    for (double v : bt.ber) {
        REQUIRE(v >= 0.0);
        REQUIRE(v <= 1.0);
        if (v > vmax) vmax = v;
    }
    // Some non-zero BER somewhere — proves the empirical density is
    // actually being measured rather than always returning zeros.
    REQUIRE(vmax > 0.0);

    // ui_offset is monotonically increasing across the curve.
    for (std::size_t i = 1; i < bt.ui_offset.size(); ++i) {
        REQUIRE(bt.ui_offset[i] > bt.ui_offset[i - 1]);
    }
}

TEST_CASE("bathtub: clean NRZ has near-zero BER across the UI", "[bathtub]") {
    // Step NRZ has no samples at the mid voltage at any time bin →
    // BER ~ 0 across the entire UI. Curve is the right length but
    // everywhere flat at zero.
    auto bits = prbs7(500);
    auto y = nrz_waveform(bits, 32);
    auto eye = build_eye(y, 32, 64, 64, /*warmup=*/4);

    auto bt = compute_bathtub(eye);
    REQUIRE(!bt.ber.empty());
    for (double v : bt.ber) {
        REQUIRE(v == Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("bathtub: timing_margin_at returns sensible offsets", "[bathtub]") {
    auto bits = prbs7(2000);
    auto tx = nrz_waveform(bits, 32);
    auto rx = rc_lowpass(tx, 1.0, 0.05);  // moderate filtering
    auto eye = build_eye(rx, 32, 128, 96, /*warmup=*/8);
    auto bt = compute_bathtub(eye);
    REQUIRE(!bt.ber.empty());

    // Margin at a lenient BER threshold (10%) should be larger than at
    // a strict threshold (1%) — wider "good window" tolerance gives
    // more timing room.
    const double m_lenient = timing_margin_at(bt, 0.10);
    const double m_strict  = timing_margin_at(bt, 0.01);
    if (m_lenient > 0 && m_strict > 0) {
        REQUIRE(m_lenient >= m_strict);
    }
}

TEST_CASE("bathtub: fully empty eye returns empty curve", "[bathtub]") {
    // No samples at all → no per-column totals → bathtub stays empty
    // (rather than returning a column of NaNs from divide-by-zero).
    EyeGrid g;
    g.time_bins = 64;
    g.volt_bins = 64;
    g.counts.assign(64 * 64, 0);
    auto bt = compute_bathtub(g);
    REQUIRE(bt.ber.empty());
    REQUIRE(timing_margin_at(bt, 0.5) == -1.0);
}

TEST_CASE("bathtub: ui_offset covers the full UI", "[bathtub]") {
    auto bits = prbs7(1000);
    auto rx = rc_lowpass(nrz_waveform(bits, 32), 1.0, 0.05);
    auto eye = build_eye(rx, 32, 64, 64, /*warmup=*/4);
    auto bt = compute_bathtub(eye);
    if (!bt.ber.empty()) {
        REQUIRE(bt.ui_offset.front() < 0.05);
        REQUIRE(bt.ui_offset.back()  > 0.95);
    }
}
