// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/Eye.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "si/Ibis.h"

namespace sikit::eye {

int EyeGrid::max_count() const {
    int m = 0;
    for (int c : counts) {
        if (c > m) m = c;
    }
    return m;
}

std::vector<double> nrz_waveform(const std::vector<int>& bits,
                                  int samples_per_ui) {
    std::vector<double> out;
    out.reserve(bits.size() * static_cast<std::size_t>(samples_per_ui));
    for (int bit : bits) {
        const double level = bit ? 1.0 : -1.0;
        for (int s = 0; s < samples_per_ui; ++s) out.push_back(level);
    }
    return out;
}

std::vector<double> nrz_with_ramp(const std::vector<int>& bits,
                                   int samples_per_ui,
                                   double ramp_fraction) {
    if (ramp_fraction <= 0.0) return nrz_waveform(bits, samples_per_ui);
    if (ramp_fraction > 1.0) ramp_fraction = 1.0;

    const std::size_t N = bits.size() * static_cast<std::size_t>(samples_per_ui);
    std::vector<double> out(N, 0.0);
    const int ramp_samples =
        std::max(1, static_cast<int>(ramp_fraction * samples_per_ui));

    // Walk bit by bit; at each bit boundary, if the bit changed, ramp
    // linearly over `ramp_samples` into the new level. Outside the ramp
    // window we sit at the new level (flat top/bottom).
    double current_level = bits.empty() ? 0.0 : (bits[0] ? 1.0 : -1.0);
    double prev_level = current_level;
    for (std::size_t b = 0; b < bits.size(); ++b) {
        const double target = bits[b] ? 1.0 : -1.0;
        const std::size_t base =
            b * static_cast<std::size_t>(samples_per_ui);
        for (int s = 0; s < samples_per_ui; ++s) {
            double v;
            if (s < ramp_samples && target != prev_level) {
                // Linear ramp from prev to target across the first
                // `ramp_samples` samples of the UI.
                const double t = (s + 1.0) / static_cast<double>(ramp_samples);
                v = prev_level + (target - prev_level) * t;
            } else {
                v = target;
            }
            out[base + s] = v;
        }
        prev_level = target;
        current_level = target;
    }
    return out;
}

double ramp_fraction_from_ibis(const sikit::ibis::Model& m, double baud_hz) {
    if (baud_hz <= 0.0) return 0.0;
    const double ui = 1.0 / baud_hz;

    // Prefer rise time; fall back to fall time. dt is the time portion
    // of dV/dt = dV / dt, expressed as seconds.
    double dt = m.ramp.dt_rise.typ;
    if (!(dt > 0.0)) dt = m.ramp.dt_fall.typ;
    if (!(dt > 0.0)) return 0.0;
    return std::clamp(dt / ui, 0.0, 1.0);
}

std::vector<int> prbs7(int num_bits) {
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(num_bits));
    unsigned int lfsr = 0x7F;
    for (int i = 0; i < num_bits; ++i) {
        const unsigned int bit = ((lfsr >> 6) ^ (lfsr >> 5)) & 1u;
        lfsr = ((lfsr << 1) | bit) & 0x7Fu;
        out.push_back(static_cast<int>(bit));
    }
    return out;
}

std::vector<double> rc_lowpass(const std::vector<double>& x,
                                double dt,
                                double cutoff_hz) {
    std::vector<double> y(x.size(), 0.0);
    if (x.empty()) return y;
    const double alpha = std::exp(-2.0 * std::numbers::pi * cutoff_hz * dt);
    const double beta = 1.0 - alpha;
    y[0] = beta * x[0];
    for (std::size_t i = 1; i < x.size(); ++i) {
        y[i] = alpha * y[i - 1] + beta * x[i];
    }
    return y;
}

EyeGrid build_eye(const std::vector<double>& y,
                  int samples_per_ui,
                  int time_bins,
                  int volt_bins,
                  int warmup_uis) {
    EyeGrid g;
    g.time_bins = time_bins;
    g.volt_bins = volt_bins;
    g.counts.assign(static_cast<std::size_t>(time_bins) *
                    static_cast<std::size_t>(volt_bins), 0);

    if (y.empty() || samples_per_ui <= 0 ||
        time_bins <= 0 || volt_bins <= 0) {
        return g;
    }

    double y_min = *std::min_element(y.begin(), y.end());
    double y_max = *std::max_element(y.begin(), y.end());
    if (y_min == y_max) {
        y_min -= 0.5;
        y_max += 0.5;
    } else {
        const double margin = 0.05 * (y_max - y_min);
        y_min -= margin;
        y_max += margin;
    }
    g.v_min = y_min;
    g.v_max = y_max;

    const std::size_t start = static_cast<std::size_t>(warmup_uis) *
                              static_cast<std::size_t>(samples_per_ui);
    if (start >= y.size()) return g;

    const double v_span = y_max - y_min;

    for (std::size_t i = start; i < y.size(); ++i) {
        const int t_in_ui = static_cast<int>(i % static_cast<std::size_t>(samples_per_ui));
        int t_bin = (t_in_ui * time_bins) / samples_per_ui;
        if (t_bin < 0) t_bin = 0;
        if (t_bin >= time_bins) t_bin = time_bins - 1;

        int v_bin = static_cast<int>((y[i] - y_min) / v_span * volt_bins);
        if (v_bin < 0) v_bin = 0;
        if (v_bin >= volt_bins) v_bin = volt_bins - 1;

        ++g.counts[static_cast<std::size_t>(t_bin) +
                   static_cast<std::size_t>(v_bin) *
                       static_cast<std::size_t>(time_bins)];
    }
    return g;
}

}  // namespace sikit::eye
