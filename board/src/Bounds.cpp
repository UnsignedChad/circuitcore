// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Canonical 2D bounding-box helpers. See Bounds.h for rationale.

#include "circuitcore/board/Bounds.h"

namespace circuitcore::board {

Bounds2 bounds_of_board(const Board& board) noexcept {
    Bounds2 b;
    for (const auto& s : board.segments) {
        b.include(s.start.x, s.start.y);
        b.include(s.end.x,   s.end.y);
    }
    for (const auto& v : board.vias) {
        const double r = 0.5 * v.outer_diameter;
        b.include(v.at.x - r, v.at.y - r);
        b.include(v.at.x + r, v.at.y + r);
    }
    for (const auto& p : board.pads) {
        const double hw = 0.5 * p.size.x;
        const double hh = 0.5 * p.size.y;
        b.include(p.at.x - hw, p.at.y - hh);
        b.include(p.at.x + hw, p.at.y + hh);
    }
    for (const auto& seg : board.outline) {
        b.include(seg.start.x, seg.start.y);
        b.include(seg.end.x,   seg.end.y);
    }
    for (const auto& g : board.graphics) {
        for (const auto& pt : g.points) b.include(pt.x, pt.y);
    }
    for (const auto& z : board.zones) {
        for (const auto& pt : z.outline.outline) b.include(pt.x, pt.y);
        for (const auto& fp : z.filled)
            for (const auto& pt : fp.outline) b.include(pt.x, pt.y);
    }
    return b;
}

Bounds2 bounds_of_zone(const Board& board, int net_id,
                        int layer_ordinal) noexcept {
    Bounds2 b;
    for (const auto& z : board.zones) {
        if (z.net_id != net_id) continue;
        if (z.layer_ordinal != layer_ordinal) continue;
        for (const auto& fp : z.filled)
            for (const auto& pt : fp.outline) b.include(pt.x, pt.y);
    }
    return b;
}

Bounds2 bounds_of_pads(const Board& board,
                        std::string_view parent_ref) noexcept {
    Bounds2 b;
    for (const auto& pd : board.pads) {
        if (pd.parent_ref != parent_ref) continue;
        const double hw = 0.5 * pd.size.x;
        const double hh = 0.5 * pd.size.y;
        b.include(pd.at.x - hw, pd.at.y - hh);
        b.include(pd.at.x + hw, pd.at.y + hh);
    }
    return b;
}

Bounds2 bounds_of_pads(std::span<const Pad> pads) noexcept {
    Bounds2 b;
    for (const auto& pd : pads) {
        const double hw = 0.5 * pd.size.x;
        const double hh = 0.5 * pd.size.y;
        b.include(pd.at.x - hw, pd.at.y - hh);
        b.include(pd.at.x + hw, pd.at.y + hh);
    }
    return b;
}

}  // namespace circuitcore::board
