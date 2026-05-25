// Triangulates filled-zone polygons into GL-ready meshes, grouped by layer.
//
// For each copper layer that has zone fill, we produce a single LayerMesh
// with concatenated triangle vertices and indices ready for glDrawElements.
// Holes inside zones are handled (earcut.hpp supports them natively).

#pragma once

#include <cstdint>
#include <vector>

#include "circuitcore/board/Board.h"

namespace circuitcore::ui {

struct LayerMesh {
    int layer_ordinal = 0;
    // Interleaved 2D positions: [x0, y0, x1, y1, ...]. Units: meters.
    std::vector<float> vertices;
    // Triangle list (3 indices per triangle).
    std::vector<std::uint32_t> indices;

    std::size_t vertex_count() const noexcept { return vertices.size() / 2; }
    std::size_t triangle_count() const noexcept { return indices.size() / 3; }
};

class ZoneMesher {
public:
    // Build one LayerMesh per copper layer that has at least one filled
    // polygon. Layers with no zone fill are omitted.
    static std::vector<LayerMesh> build(const circuitcore::board::Board& board);
};

}  // namespace circuitcore::ui
