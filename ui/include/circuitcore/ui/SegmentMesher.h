// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Tessellates track segments into per-layer triangle meshes.
//
// Each segment becomes a rectangle (4 vertices, 2 triangles) of the segment
// width, oriented along the (start → end) direction. Butt end caps for v0;
// round caps can be added later if visual rendering of trace ends matters.

#pragma once

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/ZoneMesher.h"  // for LayerMesh

namespace circuitcore::ui {

class SegmentMesher {
public:
    static std::vector<LayerMesh> build(const circuitcore::board::Board& board);
};

// Legacy: build zone fills + segments + vias + pads in one pass, merging
// per copper layer. Tests still consume this. The canvas should prefer
// build_board_meshes() below so zones can be rendered translucent and
// tracks opaque on top.
std::vector<LayerMesh> build_all_meshes(const circuitcore::board::Board& board);

// Split mesh set: zone fills separated from segment / via / pad geometry,
// each grouped per copper layer. The canvas draws zones first with a
// reduced-alpha tint so traces and pads on the same layer stay readable
// even when they run through a poured plane.
struct BoardMeshes {
    std::vector<LayerMesh> zones;
    std::vector<LayerMesh> tracks;
};
BoardMeshes build_board_meshes(const circuitcore::board::Board& board);

}  // namespace circuitcore::ui
