// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Walk a board, estimate radiated emissions per net, score vs a mask.
//
// For each routed net the analyzer assumes the trace + its reference
// plane form a current loop. The loop area is
//     A = trace_length * loop_height
// where loop_height is the dielectric distance from the trace to the
// reference plane (caller supplies; defaults to 0.2 mm matching the
// typical microstrip stackup the rest of circuitcore uses).
//
// Each net's loop is excited by the trapezoidal spectrum the caller
// supplies. Worst-case across all evaluated nets at each frequency
// is what gets compared to the mask -- this is pre-compliance, not a
// real chamber sweep.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "emi/LoopEmissions.h"
#include "emi/Masks.h"
#include "emi/Spectrum.h"

namespace emikit::emi {

struct AnalysisConfig {
    // Drive spectrum per net. v1 applies one spectrum to every net the
    // user names -- a future revision will let each net carry its own.
    TrapezoidalSpec drive;

    // Vertical loop height (m). Distance from trace to its reference
    // plane. Defaults to 0.2 mm (canonical 2-layer FR-4 microstrip).
    double loop_height_m = 0.2e-3;

    // CISPR / FCC test distance. 3.0 (residential) or 10.0 (commercial).
    double test_distance_m = 3.0;

    // Frequency grid we evaluate. 30 MHz - 1 GHz covers CISPR/FCC v1
    // range; the upper-end 1-6 GHz CISPR 32 region is included when
    // the user asks for it.
    std::vector<double> freq_hz;

    // Only analyze nets whose names match any of these substrings
    // (case-insensitive). Empty -> all nets with at least one segment.
    std::vector<std::string> net_filter;
};

struct NetEmission {
    int net_id = 0;
    std::string net_name;
    int layer_ordinal = 0;
    double total_length_m = 0.0;
    double loop_area_m2 = 0.0;
    // E-field in dBuV/m, one per AnalysisConfig::freq_hz entry.
    std::vector<double> e_dbuv;
};

struct Verdict {
    // NoData: no routed nets matched the filter, so nothing was scored.
    // Distinct from Pass -- absence of emissions data is not the same
    // as proving compliance.
    enum class Status { Pass, Fail, NoData };
    Status status = Status::Pass;
    double worst_freq_hz = 0.0;
    double worst_margin_db = 0.0;       // positive -> headroom; negative -> over limit
    std::string worst_net;
};

struct AnalysisResult {
    std::vector<NetEmission> nets;
    // For each freq_hz entry: the max E-field across all evaluated
    // nets. This is the curve that gets compared to the mask.
    std::vector<double> worst_case_dbuv;
    Verdict verdict;
};

AnalysisResult analyze_board(
    const circuitcore::board::Board& board,
    const EmissionsMask& mask,
    const AnalysisConfig& config);

// Helper: default 30 MHz - 1 GHz log grid with 100 points -- the
// region every spec mask covers.
std::vector<double> default_cispr_freq_grid(int n_points = 100);

}  // namespace emikit::emi
