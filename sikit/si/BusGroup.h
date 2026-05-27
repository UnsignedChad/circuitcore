// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Multi-bit bus grouping by net-name heuristic.
//
// DDR4 byte lanes (DQ0..DQ7), PCIe lane groups (LANE0..LANE3), and any
// other set of nets that share a common prefix and differ by a trailing
// integer index form a bus. SI engineers care about the per-bus length
// variation -- the worst bit on a DDR4 byte lane sets the read-eye
// margin for the whole lane, so even a single 0.5 mm-long net stands
// out as the bottleneck.
//
// This module surfaces those groups + reports length min / max / skew
// per bus. Diff-pair-aware: each detected pair is collapsed to one
// logical entry (so PCIE_TX_LANE0_P / _N becomes a single
// "PCIE_TX_LANE0" member of the LANE bus, length = P leg).

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"
#include "si/TraceImpedance.h"

namespace sikit::si {

struct BusMember {
    std::string name;        // representative net name (P leg for pairs)
    int net_id = -1;         // ditto
    bool is_diff_pair = false;
    int partner_net_id = -1; // N leg's net_id when is_diff_pair
    int index = 0;           // trailing integer index extracted from the name
    double length_m = 0.0;
};

struct BusGroup {
    std::string base_name;
    std::vector<BusMember> members;     // sorted by index ascending
    double min_length_m = 0.0;
    double max_length_m = 0.0;
    double skew_m = 0.0;                // max_length - min_length
    double skew_ps = 0.0;                // via stackup v_phase
    bool exceeds_budget = false;
    double v_phase = 0.0;
};

// Detect bus groups. A "bus" requires >= 2 members sharing the same base
// name after stripping a trailing integer. Diff pairs collapse to a
// single member. budget_ps flags bus.exceeds_budget when the within-bus
// skew exceeds the threshold; default 10 ps is a typical DDR write-eye
// budget on a 1.6 GT/s lane.
std::vector<BusGroup> compute_bus_groups(
    const circuitcore::board::Board& board,
    const sikit::analysis::AnalysisStackup& stackup,
    double budget_ps = 10.0);

}  // namespace sikit::si
