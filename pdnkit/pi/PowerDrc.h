// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// IPC-2152 / IPC-2221 power-aware design rule check.
//
// Given a board and a set of per-net DRC rules (each: current carried,
// allowable temperature rise), reports which segments are too narrow to
// safely carry that current per the published current-vs-cross-section
// curves.
//
// The math is the IPC-2221 closed form (which underlies IPC-2152's
// reference curves; the newer standard adds more nuance for board
// thickness, copper weight, and via heat-sinking, but the IPC-2221
// equation is the practical baseline that every commercial PI tool
// implements):
//
//   I_amps = k * (dT_C)^0.44 * (A_mil2)^0.725
//
// where k = 0.048 for external layers (top/bottom) and k = 0.024 for
// internal layers, dT is the allowable temperature rise above ambient
// in degrees C, and A is the conductor cross-sectional area in mil^2.
//
// Conservative model: every segment on the target net is required to
// carry the full net current. Real PDNs branch, so this over-flags
// segments that share current with parallel paths -- but for an MVP
// "is my routing thick enough at all" check, conservative is the
// right answer. A future revision can use IrSolver output to compute
// per-segment actual current and refine.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"

namespace pdnkit::pi {

// Closed-form IPC-2221: max safe current for a given trace cross-section
// at the given allowable temperature rise, on an external or internal layer.
//   area_m2 -- conductor cross-section in square meters (width * thickness)
//   temp_rise_c -- allowable rise above ambient in degrees C
//   external -- true for F.Cu / B.Cu, false for inner copper layers
double ipc2221_max_current(double area_m2, double temp_rise_c, bool external);

// Inverse: minimum cross-section needed to safely carry the given current.
double ipc2221_min_area(double current_amps, double temp_rise_c, bool external);

// One rule per net the user cares about. nets they don't list are skipped.
struct DrcRule {
    int net_id = 0;
    double current_amps = 0.0;  // expected current through the net
    double temp_rise_c = 10.0;  // allowable rise (10/20/30 typical)
};

struct DrcViolation {
    int segment_index = 0;      // index into board.segments
    int net_id = 0;
    int layer_ordinal = 0;
    bool external = true;       // F.Cu or B.Cu
    double current_amps = 0.0;
    double temp_rise_c = 0.0;
    double width_actual_m = 0.0;
    double width_required_m = 0.0;
    double cu_thickness_m = 0.0;
    // human-readable violation summary
    std::string message;
};

struct DrcReport {
    std::vector<DrcViolation> violations;
    int segments_checked = 0;
    int nets_checked = 0;
};

// Run the DRC. If a Layer in the stackup has no parsed thickness, falls
// back to fallback_cu_thickness_m (default 1 oz = 35 um).
DrcReport check_ipc2152(const circuitcore::board::Board& board,
                        const std::vector<DrcRule>& rules,
                        double fallback_cu_thickness_m = 35.0e-6);

}  // namespace pdnkit::pi
