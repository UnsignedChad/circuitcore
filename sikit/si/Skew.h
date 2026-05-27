// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Diff-pair length and intra-pair skew analysis.
//
// For each diff pair on the board, sum the segment lengths of the P leg
// and the N leg, take the difference, and convert to picoseconds using
// the effective phase velocity from the stackup. Pairs whose absolute
// skew exceeds a configurable budget get flagged.
//
// The picosecond budget engineers use depends on the standard: PCIe
// Gen5 calls for under 5 ps intra-pair skew at the connector boundary,
// USB 3.2 Gen 2x2 allows ~10 ps, HDMI 2.1 FRL is roughly 20 ps. The
// default budget here is 5 ps (the tightest of the common standards);
// callers override per protocol.
//
// v_phase comes from compute_one's eps_eff on each leg's host layer.
// For inner-layer stripline this is essentially c0 / sqrt(eps_r); for
// outer-layer microstrip it's the slightly higher value Hammerstad
// returns (effective eps lower than bulk eps_r because the field is
// partially in air).

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"
#include "si/TraceImpedance.h"

namespace sikit::si {

struct DiffPairSkew {
    std::string base_name;
    int net_p_id = -1;
    int net_n_id = -1;
    int layer_ordinal = 0;       // copper layer the pair routes on (majority)

    double length_p_m = 0.0;     // P leg, total across all segments
    double length_n_m = 0.0;     // N leg

    double skew_m = 0.0;         // length_p - length_n
    double skew_ps = 0.0;        // skew_m / v_phase * 1e12; sign preserved
    double v_phase = 0.0;        // m/s, what the conversion used

    bool exceeds_budget = false;
};

// Compute skew for every detected diff pair on the board. Pairs whose
// nets have no segments on any layer are skipped. budget_ps controls
// the exceeds_budget flag; default 5.0 matches PCIe Gen5.
std::vector<DiffPairSkew> compute_diff_pair_skews(
    const circuitcore::board::Board& board,
    const sikit::analysis::AnalysisStackup& stackup,
    double budget_ps = 5.0);

}  // namespace sikit::si
