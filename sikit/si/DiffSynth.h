// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Synthesize a 4-port Touchstone for an edge-coupled diff pair.
//
// Uses modal decomposition on RLGC matrices from the FDM solver:
// even / odd modes are propagated independently, then recombined into
// a 4-port S-matrix per frequency using the textbook formulas.
//
// Port convention (matches scikit-rf, HFSS, ADS):
//     port 1   positive trace, near end
//     port 2   positive trace, far end
//     port 3   negative trace, near end
//     port 4   negative trace, far end

#pragma once

#include <vector>

#include "si/TraceImpedance.h"     // for AnalysisStackup
#include "si/Touchstone.h"

namespace sikit::analysis {

struct DiffChannelSpec {
    double trace_width = 0.0;       // m
    double spacing     = 0.0;       // m, edge-to-edge gap
    int    layer_ordinal = 0;
    double length_m    = 0.0;
    AnalysisStackup stackup;
};

// Generate a 4-port .s4p representing length_m of a coupled diff pair.
// Reference impedance defaults to 50 Ω per port (standard for raw S4P;
// tools convert to differential 100 Ω / common 25 Ω as needed).
sikit::touchstone::TouchstoneFile synthesize_diff_channel(
    const DiffChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance = 50.0);

}  // namespace sikit::analysis
