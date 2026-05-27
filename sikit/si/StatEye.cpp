// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/StatEye.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "si/ChannelResponse.h"

namespace sikit::eye {

std::vector<double> compute_sbr(
    const sikit::touchstone::TouchstoneFile& channel,
    double bit_rate_hz, int samples_per_ui, int sbr_length_ui) {
    if (bit_rate_hz <= 0.0 || samples_per_ui <= 1 || sbr_length_ui <= 1) {
        return {};
    }
    // Single rectangular pulse of one UI, surrounded by zero history.
    const int n = sbr_length_ui * samples_per_ui;
    std::vector<double> pulse(n, 0.0);
    for (int i = 0; i < samples_per_ui; ++i) pulse[i] = 1.0;
    const double fs = bit_rate_hz * samples_per_ui;
    try {
        return sikit::dsp::apply_channel(pulse, fs, channel);
    } catch (...) {
        return {};
    }
}

PdaEyeEnvelope peak_distortion_eye(const std::vector<double>& sbr,
                                    int samples_per_ui) {
    PdaEyeEnvelope out;
    if (sbr.empty() || samples_per_ui <= 1) return out;
    out.samples_per_ui = samples_per_ui;
    out.top.assign(samples_per_ui, 0.0);
    out.bottom.assign(samples_per_ui, 0.0);

    // Slice the SBR into UI rows. cursor_ui_idx is the UI index whose
    // peak sample is largest -- thats the main cursor UI.
    const int n_ui = static_cast<int>(sbr.size()) / samples_per_ui;
    if (n_ui < 2) return out;

    // Build a 2-D grid h[ui_index][t_phase] = sbr value at that phase.
    std::vector<std::vector<double>> h(n_ui,
        std::vector<double>(samples_per_ui, 0.0));
    for (int u = 0; u < n_ui; ++u) {
        for (int t = 0; t < samples_per_ui; ++t) {
            const int idx = u * samples_per_ui + t;
            if (idx < static_cast<int>(sbr.size())) h[u][t] = sbr[idx];
        }
    }

    // At each UI phase t, the cursor tap is the UI whose value at phase t
    // is maximum in magnitude.
    for (int t = 0; t < samples_per_ui; ++t) {
        int cursor = 0;
        double cursor_val = 0.0;
        for (int u = 0; u < n_ui; ++u) {
            if (std::abs(h[u][t]) > std::abs(cursor_val)) {
                cursor = u;
                cursor_val = h[u][t];
            }
        }
        double isi_sum = 0.0;
        for (int u = 0; u < n_ui; ++u) {
            if (u == cursor) continue;
            isi_sum += std::abs(h[u][t]);
        }
        out.top[t]    =  cursor_val - isi_sum;
        out.bottom[t] = -cursor_val + isi_sum;
    }

    // Eye height = max(top - bottom). Width = fraction of t where eye is open.
    double max_h = 0.0;
    int open_count = 0;
    for (int t = 0; t < samples_per_ui; ++t) {
        const double h_t = out.top[t] - out.bottom[t];
        if (h_t > max_h) max_h = h_t;
        if (h_t > 0.0)   ++open_count;
    }
    out.eye_height = max_h;
    out.eye_width  = static_cast<double>(open_count) / samples_per_ui;
    return out;
}

namespace {

// Convolve two PMFs defined on the same uniform voltage grid.
std::vector<double> convolve_pmf(const std::vector<double>& a,
                                  const std::vector<double>& b,
                                  double dv, double v_min) {
    // Both PMFs share the same grid spacing dv. The convolution is:
    //   c[i] = sum_j a[j] * b[i - j]    on the shifted index
    // We re-center to keep the same grid; values beyond the grid get clipped
    // (their mass becomes a tail at the extremes).
    const int n = static_cast<int>(a.size());
    std::vector<double> c(n, 0.0);
    // For PMF convolution of v_a + v_b, output index k corresponds to
    // voltage v_a[i] + v_b[j], where i = (v_a - v_min)/dv and similar for j.
    // So c[i + j - i_zero] += a[i] * b[j] where i_zero = index of v=0.
    // Easier: shift each multiplication by (j - n/2) since both PMFs are
    // already centered the same way.
    const int center = n / 2;
    (void)dv; (void)v_min;
    for (int j = 0; j < n; ++j) {
        if (b[j] == 0.0) continue;
        const double bw = b[j];
        const int shift = j - center;
        for (int i = 0; i < n; ++i) {
            if (a[i] == 0.0) continue;
            int dst = i + shift;
            if (dst < 0)  dst = 0;
            if (dst >= n) dst = n - 1;
            c[dst] += a[i] * bw;
        }
    }
    return c;
}

}  // namespace

StatEyeResult statistical_eye(const std::vector<double>& sbr,
                               int samples_per_ui, int volt_bins) {
    StatEyeResult R;
    if (sbr.empty() || samples_per_ui <= 1 || volt_bins < 32) return R;
    R.samples_per_ui = samples_per_ui;
    R.volt_bins = volt_bins;

    const int n_ui = static_cast<int>(sbr.size()) / samples_per_ui;
    if (n_ui < 2) return R;

    // Build the per-UI tap matrix as before.
    std::vector<std::vector<double>> h(n_ui,
        std::vector<double>(samples_per_ui, 0.0));
    for (int u = 0; u < n_ui; ++u) {
        for (int t = 0; t < samples_per_ui; ++t) {
            const int idx = u * samples_per_ui + t;
            if (idx < static_cast<int>(sbr.size())) h[u][t] = sbr[idx];
        }
    }

    // Establish a global voltage span large enough to hold +/- (h_cursor +
    // total |ISI|) at the worst phase, with some padding.
    double v_max_signed = 0.0;
    for (int t = 0; t < samples_per_ui; ++t) {
        double cursor_val = 0.0;
        for (int u = 0; u < n_ui; ++u) {
            if (std::abs(h[u][t]) > std::abs(cursor_val)) cursor_val = h[u][t];
        }
        double sum_isi = 0.0;
        for (int u = 0; u < n_ui; ++u) sum_isi += std::abs(h[u][t]);
        v_max_signed = std::max(v_max_signed, sum_isi);
        (void)cursor_val;
    }
    R.v_max =  v_max_signed * 1.05;
    R.v_min = -v_max_signed * 1.05;
    if (R.v_max <= R.v_min) { R.v_max = 1.0; R.v_min = -1.0; }
    const double dv = (R.v_max - R.v_min) / (volt_bins - 1);
    auto v_index = [&](double v) {
        int i = static_cast<int>(std::round((v - R.v_min) / dv));
        return std::clamp(i, 0, volt_bins - 1);
    };

    R.ber_map.assign(static_cast<std::size_t>(samples_per_ui) * volt_bins, 0.5);

    // For each sub-UI phase, build the aggregate PMF of v_ISI by convolving
    // each non-cursor taps two-spike PMF (1/2 at +h_k, 1/2 at -h_k). For
    // efficiency we cap the number of ISI taps to keep convolution time
    // bounded; the omitted taps (small magnitude) contribute negligible ISI.
    constexpr int kMaxIsiTaps = 32;

    for (int t = 0; t < samples_per_ui; ++t) {
        // Cursor.
        int cursor = 0;
        double cursor_val = 0.0;
        for (int u = 0; u < n_ui; ++u) {
            if (std::abs(h[u][t]) > std::abs(cursor_val)) {
                cursor = u; cursor_val = h[u][t];
            }
        }
        // Collect ISI taps, sorted by descending magnitude.
        std::vector<double> taps;
        taps.reserve(n_ui);
        for (int u = 0; u < n_ui; ++u) {
            if (u == cursor) continue;
            if (std::abs(h[u][t]) > 1e-9) taps.push_back(h[u][t]);
        }
        std::sort(taps.begin(), taps.end(),
                  [](double a, double b) { return std::abs(a) > std::abs(b); });
        if (static_cast<int>(taps.size()) > kMaxIsiTaps) {
            taps.resize(kMaxIsiTaps);
        }

        // Start PMF = delta at v=0.
        std::vector<double> pmf(volt_bins, 0.0);
        pmf[v_index(0.0)] = 1.0;

        for (double hk : taps) {
            std::vector<double> tap_pmf(volt_bins, 0.0);
            tap_pmf[v_index( hk)] += 0.5;
            tap_pmf[v_index(-hk)] += 0.5;
            pmf = convolve_pmf(pmf, tap_pmf, dv, R.v_min);
        }

        // pmf now = P(v_ISI = v). For each decision threshold v_th, compute:
        //   BER = 0.5 * P(h_cursor + v_ISI < v_th)
        //       + 0.5 * P(-h_cursor + v_ISI > v_th)
        // The shifted PMFs cdf are simple cumsum / reverse cumsum.

        // CDF: cdf[i] = P(v_ISI <= bin i).
        std::vector<double> cdf(volt_bins, 0.0);
        cdf[0] = pmf[0];
        for (int i = 1; i < volt_bins; ++i) cdf[i] = cdf[i - 1] + pmf[i];

        // Loop decision threshold over voltage bins.
        for (int vi = 0; vi < volt_bins; ++vi) {
            const double v_th = R.v_min + vi * dv;
            // P(h_cursor + v_ISI < v_th) = P(v_ISI < v_th - h_cursor)
            //   = cdf at index of (v_th - h_cursor) - 1
            int ai = v_index(v_th - cursor_val) - 1;
            double p_bit1_err = (ai < 0) ? 0.0 : cdf[std::min(ai, volt_bins - 1)];
            // P(-h_cursor + v_ISI > v_th) = 1 - P(v_ISI <= v_th + h_cursor)
            int bi = v_index(v_th + cursor_val);
            double p_bit0_err = 1.0 - cdf[std::min(std::max(bi, 0), volt_bins - 1)];
            double ber = 0.5 * (p_bit1_err + p_bit0_err);
            ber = std::clamp(ber, 0.0, 0.5);
            R.ber_map[static_cast<std::size_t>(vi) * samples_per_ui + t] = ber;
        }
    }
    return R;
}

std::vector<double> bathtub_from_stat_eye(const StatEyeResult& se) {
    std::vector<double> bt;
    if (se.samples_per_ui <= 0 || se.volt_bins <= 0 || se.ber_map.empty()) {
        return bt;
    }
    bt.assign(se.samples_per_ui, 0.5);
    const int v_mid = se.volt_bins / 2;
    for (int t = 0; t < se.samples_per_ui; ++t) {
        bt[t] = se.ber_map[static_cast<std::size_t>(v_mid) * se.samples_per_ui + t];
    }
    return bt;
}

}  // namespace sikit::eye
