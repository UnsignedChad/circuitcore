// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/Bathtub.h"

#include <algorithm>
#include <cmath>

namespace sikit::eye {

BathtubCurve compute_bathtub(const EyeGrid& g) {
    BathtubCurve b;
    if (g.time_bins <= 0 || g.volt_bins <= 0 || g.counts.empty()) return b;
    if (g.v_max <= g.v_min) return b;

    // Locate the mid-voltage row.
    const double v_mid = 0.5 * (g.v_max + g.v_min);
    const int v_mid_bin = std::clamp(
        static_cast<int>((v_mid - g.v_min) / (g.v_max - g.v_min) * g.volt_bins),
        0, g.volt_bins - 1);

    // Width of the "ambiguous voltage" window (±2 bins around mid).
    const int v_lo = std::max(0, v_mid_bin - 2);
    const int v_hi = std::min(g.volt_bins - 1, v_mid_bin + 2);

    // For each time bin: BER(t) ≈ (samples whose voltage was within the
    // ambiguous window) / (total samples landing in column t). This is
    // the empirical probability that a sample taken at offset t would
    // be interpreted incorrectly because the signal was mid-transition.
    bool has_any = false;
    b.ui_offset.reserve(g.time_bins);
    b.ber.reserve(g.time_bins);
    for (int t = 0; t < g.time_bins; ++t) {
        double col_total = 0.0;
        double cross = 0.0;
        for (int v = 0; v < g.volt_bins; ++v) {
            const double c = g.at(t, v);
            col_total += c;
            if (v >= v_lo && v <= v_hi) cross += c;
        }
        const double ui = (t + 0.5) / g.time_bins;
        double ber = 0.0;
        if (col_total > 0.0) {
            ber = cross / col_total;
            has_any = true;
        }
        b.ui_offset.push_back(ui);
        b.ber.push_back(ber);
    }
    if (!has_any) {
        b.ui_offset.clear();
        b.ber.clear();
    }
    return b;
}

double timing_margin_at(const BathtubCurve& bt, double target_ber) {
    if (bt.ber.empty()) return -1.0;
    // Walk outward from the centre. At each step, the smaller of the two
    // bath walls' BER controls — that's the limiting side.
    const std::size_t N = bt.ber.size();
    const std::size_t centre = N / 2;
    for (std::size_t step = 0; step <= centre; ++step) {
        const std::size_t left  = (centre >= step) ? centre - step : 0;
        const std::size_t right = std::min(centre + step, N - 1);
        if (bt.ber[left]  >= target_ber) return std::abs(bt.ui_offset[left]  - 0.5);
        if (bt.ber[right] >= target_ber) return std::abs(bt.ui_offset[right] - 0.5);
    }
    return -1.0;  // bathtub never exceeds target → comfortably open
}

}  // namespace sikit::eye
