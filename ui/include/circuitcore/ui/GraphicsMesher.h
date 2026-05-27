// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Build OpenGL meshes from the silk / mask / courtyard graphic items
// the parser captures into Board::graphics. The renderer treats each
// category separately so it can use a different color + draw order:
//
//   Mask      -- translucent green, drawn over copper (KiCad's classic
//                "green PCB" look comes from this)
//   Silkscreen -- opaque white, lines + filled polygons, on top of mask
//   Courtyard -- thin magenta outlines, optional (debug overlay)
//
// Text items are collected separately because the GL pipeline doesn't
// rasterize text -- the canvas draws them via QPainter on top of the
// GL frame, the same way pdnkit overlays voltage labels at IR-drop
// probe points.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/ZoneMesher.h"  // LayerMesh

namespace circuitcore::ui {

struct GraphicsBundle {
    LayerMesh silk;       // strokes + filled polys
    LayerMesh mask;       // filled polys
    LayerMesh courtyard;  // strokes
    struct Text {
        double x = 0.0, y = 0.0;
        double size = 0.0;     // metres
        double angle = 0.0;    // radians
        int    layer_ordinal = 0;
        std::string text;
    };
    std::vector<Text> silk_text;
};

class GraphicsMesher {
public:
    // Default stroke width for graphic items that don't carry one
    // (rare; KiCad usually writes 0.15 mm or similar). Caller can
    // override.
    static constexpr double kDefaultStrokeWidth = 0.15e-3;

    static GraphicsBundle build(const board::Board& board);
};

}  // namespace circuitcore::ui
