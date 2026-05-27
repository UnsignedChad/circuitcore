// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Statistical eye / Peak Distortion Analysis (PDA).
//
// Bathtub curves and PRBS eye diagrams are useful but their statistical
// reach is limited by the bit count you simulate. PCIe Gen5 receiver
// compliance is checked at 1e-12 BER; a 10,000-bit PRBS run can only
// resolve ~1e-4. To bridge that gap, SI tools compute the eye analytically
// from the channels Single-Bit Response (SBR).
//
// Algorithm summary (Casper et al., "An Accurate and Efficient Analysis
// Method for Multi-Gb/s Chip-to-Chip Signaling Schemes", VLSI 2002):
//
//   1. Apply a unit isolated pulse to the channel and capture the time-
//      domain response. Sample it at samples_per_ui per UI; this is the
//      Single-Bit Response (SBR).
//   2. At each sub-UI sampling phase t in [0, UI), identify the "cursor"
//      tap (closest sample to t + n*UI for some integer n that gives the
//      peak SBR) and all other taps at t + k*UI; these are ISI.
//   3. Each ISI tap h_k contributes either +h_k or -h_k to the received
//      voltage, depending on the random data bit, each with probability
//      0.5 (assuming uncorrelated NRZ). Convolve the per-tap PMFs to get
//      the aggregate PMF of v_ISI at this sampling phase.
//   4. The received voltage for a transmitted 1 is h_cursor + v_ISI;
//      for a 0 it is -h_cursor + v_ISI. Both PDFs share the same shape
//      but are shifted by 2*h_cursor.
//   5. BER at a decision threshold v_th and sampling phase t is
//          P(bit=1 received as 0) + P(bit=0 received as 1)
//        = P(h_cursor + v_ISI < v_th) + P(-h_cursor + v_ISI > v_th)
//
// The result is a 2-D BER map over (sampling phase, decision threshold);
// iso-BER contours at 1e-6, 1e-9, 1e-12 etc. give the statistical eye
// opening at each BER level.
//
// Peak Distortion Analysis (PDA) is the deterministic worst-case bound:
//   eye_top(t)    = h_cursor(t) - sum_{k != cursor} |h_k(t)|
//   eye_bottom(t) = -h_cursor(t) + sum_{k != cursor} |h_k(t)|
// This is what the PDA function returns (much faster than the full
// statistical convolution and useful as a sanity check on the
// statistical result).

#pragma once

#include <cstddef>
#include <vector>

#include "si/Touchstone.h"

namespace sikit::eye {

// Compute the channels Single-Bit Response. Pass a single rectangular
// pulse of one UI duration through apply_channel and return the result.
// sbr_length_ui caps how many UIs of history we keep -- anything beyond
// has negligible ISI contribution but adds compute cost.
std::vector<double> compute_sbr(
    const sikit::touchstone::TouchstoneFile& channel,
    double bit_rate_hz,
    int samples_per_ui = 32,
    int sbr_length_ui = 64);

// Peak Distortion (deterministic worst-case) eye envelope.
struct PdaEyeEnvelope {
    int samples_per_ui = 0;
    // One entry per UI phase t in [0, samples_per_ui).
    std::vector<double> top;     // upper boundary at each t  (h_cursor - sum|ISI|)
    std::vector<double> bottom;  // lower boundary at each t  (-h_cursor + sum|ISI|)
    double eye_height = 0.0;     // max(top - bottom) over t (= height at peak)
    double eye_width  = 0.0;     // fraction of UI where top > bottom + zero_margin
};

PdaEyeEnvelope peak_distortion_eye(const std::vector<double>& sbr,
                                    int samples_per_ui);

// Statistical eye BER map. ber_map is a row-major 2-D grid of size
// (volt_bins x samples_per_ui) where ber_map[v * samples_per_ui + t] is
// the BER if the decision threshold sits at v_min + v*(v_max-v_min)/(volt_bins-1)
// and the sampler fires at UI phase t / samples_per_ui.
struct StatEyeResult {
    int samples_per_ui = 0;
    int volt_bins = 0;
    double v_min = 0.0;
    double v_max = 0.0;
    std::vector<double> ber_map;     // size = samples_per_ui * volt_bins
};

StatEyeResult statistical_eye(const std::vector<double>& sbr,
                               int samples_per_ui,
                               int volt_bins = 256);

// Convenience: extract the BER vs UI offset bathtub at the optimum decision
// threshold from a statistical eye. Returns the BER curve along t for the
// threshold midway between the two cursor levels.
std::vector<double> bathtub_from_stat_eye(const StatEyeResult& se);

}  // namespace sikit::eye
