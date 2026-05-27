// 2D axis-aligned bounding-box helpers for board geometry.
//
// Six different kits had their own Bbox / BBox structs and bbox-of-X
// helpers (board, zone, pads-of-component, courtyard, ...). This header
// is the one canonical replacement. Pick the helper that matches the
// question you have:
//
//   bounds_of_board(board)                 -- every segment / pad / via /
//                                              zone / outline / graphic
//   bounds_of_zone(board, net, layer)      -- one zone, filled polys only
//   bounds_of_pads(board, parent_ref)      -- pads belonging to a
//                                              component reference (R7, U3)
//   bounds_of_pads(pads)                   -- generic pad span overload
//
// All helpers return a Bounds2 whose .valid flag is false when there
// was nothing to bound. Coordinates are SI meters, matching the rest of
// the Board model.

#pragma once

#include <span>
#include <string_view>

#include "circuitcore/board/Board.h"

namespace circuitcore::board {

struct Bounds2 {
    bool   valid = false;
    double lo_x  = 0.0;
    double lo_y  = 0.0;
    double hi_x  = 0.0;
    double hi_y  = 0.0;

    void include(double x, double y) noexcept {
        if (!valid) {
            lo_x = hi_x = x;
            lo_y = hi_y = y;
            valid = true;
            return;
        }
        if (x < lo_x) lo_x = x;
        if (y < lo_y) lo_y = y;
        if (x > hi_x) hi_x = x;
        if (y > hi_y) hi_y = y;
    }

    double width()  const noexcept { return valid ? hi_x - lo_x : 0.0; }
    double height() const noexcept { return valid ? hi_y - lo_y : 0.0; }
};

// Every geometric primitive on the board (segments + vias + pads +
// zone outlines + filled polygons + edge-cut outline + graphic items).
Bounds2 bounds_of_board(const Board& board) noexcept;

// Filled polygons of every zone matching (net_id, layer_ordinal).
// Use this for cavity-mode plane bbox, IR mesher region, etc.
Bounds2 bounds_of_zone(const Board& board, int net_id,
                        int layer_ordinal) noexcept;

// Bounding box of every pad whose parent_ref matches. Pad size is
// included (not just the centre), so a single rectangular pad still
// produces a non-degenerate box.
Bounds2 bounds_of_pads(const Board& board,
                        std::string_view parent_ref) noexcept;

// Generic span overload for callers that already have a filtered pad
// list.
Bounds2 bounds_of_pads(std::span<const Pad> pads) noexcept;

}  // namespace circuitcore::board
