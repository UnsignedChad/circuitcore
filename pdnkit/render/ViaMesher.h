// Tessellates vias and pads into disk-shaped triangle meshes.
//
// Vias span from_layer..to_layer; we draw a disk on every copper layer in
// that ordinal range that exists in the stackup (matches through-via
// behavior on multi-layer boards).
//
// Pads draw on each of their listed copper layers as a disk sized to the
// via outer diameter or a fallback fixed size. v0 ignores actual pad shape
// (round/rect/oval) — refine when the renderer needs to distinguish them.

#pragma once

#include "circuitcore/board/Board.h"
#include "render/ZoneMesher.h"  // for LayerMesh

namespace pdnkit::render {

class ViaMesher {
public:
    static std::vector<LayerMesh> build(const circuitcore::board::Board& board);
};

class PadMesher {
public:
    // Pads have no size in the v0 model; render with this default radius (m).
    static constexpr double kDefaultPadRadius = 0.50e-3;  // 0.5 mm

    static std::vector<LayerMesh> build(const circuitcore::board::Board& board);
};

}  // namespace pdnkit::render
