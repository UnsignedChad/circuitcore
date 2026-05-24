// Tessellates track segments into per-layer triangle meshes.
//
// Each segment becomes a rectangle (4 vertices, 2 triangles) of the segment
// width, oriented along the (start → end) direction. Butt end caps for v0;
// round caps can be added later if visual rendering of trace ends matters.

#pragma once

#include "circuitcore/board/Board.h"
#include "render/ZoneMesher.h"  // for LayerMesh

namespace sikit::render {

class SegmentMesher {
public:
    static std::vector<LayerMesh> build(const circuitcore::board::Board& board);
};

// Convenience: build zone fills + segment quads in one pass, merging results
// per copper layer. This is what the renderer consumes.
std::vector<LayerMesh> build_all_meshes(const circuitcore::board::Board& board);

}  // namespace sikit::render
