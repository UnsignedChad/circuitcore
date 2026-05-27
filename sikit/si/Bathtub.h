// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Bathtub curve — the canonical SerDes timing-margin plot.
//
// For each UI offset, compute the empirical probability that a sample
// clock aligned at that offset would land in a "wrong" zero-crossing
// region of the eye. The result has the characteristic bathtub shape:
// flat and low near the eye centre, rising steeply toward the UI
// edges where ISI / jitter pushes the trace across the threshold.
//
// We derive it from the populated EyeGrid by:
//   1. Locating the mid-voltage row (v ≈ ½·(v_min + v_max)).
//   2. Treating each populated bin in that row as a crossing event.
//   3. Building cumulative-density curves from both UI edges inward.
//   4. The probability at offset t is the minimum of the left and
//      right cumulatives — the chance a sample landed beyond t in
//      whichever direction is closer.
//
// This is a single-tap statistical eye, not a full PAM-N / equalizer-
// aware bathtub. Good enough to answer "what's my 10⁻³ timing margin?"
// for the synthesized eyes sikit produces.

#pragma once

#include <vector>

#include "si/Eye.h"

namespace sikit::eye {

struct BathtubCurve {
    std::vector<double> ui_offset;   // [0, 1] across one UI
    std::vector<double> ber;          // probability of timing error at that offset
};

// Build a bathtub curve from an eye grid. Returns an empty curve if the
// eye has no populated mid-voltage bins (i.e. clean step NRZ with no
// crossings to measure — eye fully open).
BathtubCurve compute_bathtub(const EyeGrid& g);

// Find the smallest UI offset (from the centre, 0.5) at which the BER
// exceeds `target_ber`. Returns -1 if the bathtub never rises above
// target_ber (eye is wide enough). The conventional reporting form for
// SI compliance is "eye opening at BER = 10⁻¹²" — for small synthetic
// eyes we usually report it at 10⁻³ or 10⁻⁴ instead.
double timing_margin_at(const BathtubCurve& bt, double target_ber);

}  // namespace sikit::eye
