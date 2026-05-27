// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Return-path discontinuity detector.
//
// Every signal trace has a return current that flows on the closest
// adjacent copper plane. When that plane has a gap, a cutout, or simply
// doesn't exist under the trace, the return current detours -- loop
// inductance spikes, impedance discontinuity appears in TDR, common-mode
// noise radiates. It is the #1 cause of "the simulator said pass, the
// board failed" SI escapes.
//
// This v1 detector walks each signal segment, picks its reference plane
// (the nearest copper layer in the stackup), samples the path, and
// flags segments where any sample point falls outside every copper zone
// on the reference layer. Severity is reported as the off-plane fraction
// times the segment length -- a proxy for the loop-inductance penalty.
//
// Two limitations baked into v1, documented as future work:
//   * No quantitative loop-inductance computation. The pdnkit cavity
//     solver knows the actual plane Z(f) at every (x, y, f); a future
//     revision wires that lookup in to replace the on/off heuristic.
//   * Reference-plane choice is "closest copper layer by ordinal" --
//     doesn't yet honour KiCad netclass conventions (where a board
//     designer can declare "GND is on In1.Cu" explicitly).

#pragma once

#include <cstddef>
#include <vector>

#include "circuitcore/board/Board.h"

namespace sikit::si {

struct ReturnPathViolation {
    std::size_t segment_index = 0;   // index into board.segments
    int net_id = 0;
    int signal_layer = 0;            // ordinal of the trace's own layer
    int reference_layer = -1;        // ordinal of the chosen reference plane,
                                       // or -1 if no copper layer is available
    double segment_length_m = 0.0;
    // Fraction of sample points along the segment where the reference
    // plane copper was ABSENT under the trace. 1.0 means no reference
    // anywhere (e.g. board has no copper pour at all on that layer).
    double off_plane_fraction = 0.0;
    // Severity score: off_plane_fraction * segment_length, in meters.
    // Used to sort the worst-case offenders to the top of a report.
    double severity_m = 0.0;
};

// Walk every signal segment, identify its closest reference plane,
// sample N points along the segment's path, and check each against the
// zones on the reference layer. Flag any segment whose off-plane
// fraction exceeds threshold (default 0.05 = 5% off the plane).
std::vector<ReturnPathViolation> detect_return_path_violations(
    const circuitcore::board::Board& board,
    int samples_per_segment = 20,
    double off_plane_threshold = 0.05);

}  // namespace sikit::si
