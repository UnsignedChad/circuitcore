// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Point-in-board hit testing.
//
// Given a world-space point and a board, returns the highest-priority piece
// of geometry under the point: Pad > Via > Segment > Zone. Used by the canvas
// for hover info.
//
// Coordinates and tolerance are in SI meters (matching the rest of pdnkit).

#pragma once

#include <string>

#include "circuitcore/board/Board.h"

namespace circuitcore::board::hittest {

struct Hit {
    enum class Kind { None, Zone, Segment, Via, Pad };
    Kind kind = Kind::None;
    int net_id = 0;
    int layer_ordinal = 0;
    // Index into board.{pads,vias,segments,zones} depending on Kind.
    // -1 means "no specific element," for backward compatibility with
    // callers that only care about Kind/net/layer.
    int element_index = -1;
};

// Returns the first Hit found in priority order. `pick_radius` is added to
// each geometry's intrinsic tolerance (segment width/2, via radius, pad
// radius) — set it to about a few screen pixels in world units.
Hit at_point(const circuitcore::board::Board& board, circuitcore::board::Point2 world,
             double pick_radius);

// Human-readable name for a hit kind ("zone", "segment", "via", "pad", "").
const char* name(Hit::Kind k) noexcept;

}  // namespace circuitcore::board::hittest
