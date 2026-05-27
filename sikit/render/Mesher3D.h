// 3D mesh builder for the PCB stackup view.
//
// Produces a single interleaved buffer per category (dielectric slabs,
// copper, vias) ready for upload as VBO/IBO pairs. Vertices carry
// position + normal + RGBA so a single lit shader can draw the whole
// scene with one VAO binding per draw call.
//
// Coordinate convention matches Camera3D: world X/Y from the KiCad
// layout, world Z grows upward out of the board's top copper. The board
// stackup is walked from its first item (top) downward; layer Z's are
// derived so a board with total_thickness 1.6 mm runs Z from 0 to 1.6e-3.

#pragma once

#include <cstdint>
#include <vector>

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"

namespace sikit::render {

struct Mesh3D {
    // Interleaved (x, y, z, nx, ny, nz, r, g, b, a) per vertex.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    bool empty() const noexcept { return indices.empty(); }
};

struct BoardMesh3D {
    // Translucent dielectric slabs (FR-4 + any explicit dielectric items).
    Mesh3D dielectric;
    // Copper traces extruded to the per-layer thickness.
    Mesh3D copper;
    // Via barrels — solid cylinders spanning each via's layer range.
    Mesh3D vias;
    // Component bodies extruded from the courtyard bbox up by the
    // per-package default body height. One AABB per Component the
    // parser produced; placed on the side the footprint sits on (F.Cu
    // -> upward, B.Cu -> downward). Empty if the board has no
    // (footprint ...) blocks.
    Mesh3D components;
};

// Walks the stackup, computes per-layer Z positions, then meshes every
// segment + via in the board into the three category meshes above. Falls
// back to a synthesised 2-layer stackup when the board has no explicit
// (setup (stackup ...)) block.
BoardMesh3D build_board_mesh_3d(const circuitcore::board::Board& board,
                                 const sikit::si::SiStackup& si_stackup = {});

}  // namespace sikit::render
