// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// 2D orthographic camera for KiCad-style PCB viewing.
//
// World units: SI meters. Y-axis grows downward to match KiCad's PCB editor
// convention. The ortho projection matrix flips Y to map world → OpenGL NDC.

#pragma once

#include <array>

#include "circuitcore/board/Board.h"

namespace circuitcore::ui {

class Camera2D {
public:
    circuitcore::board::Point2 center{0.0, 0.0};   // world point at the screen center
    double pixels_per_meter = 1000.0; // zoom; default 1 px = 1 mm

    circuitcore::board::Point2 screen_to_world(double sx, double sy,
                                   int widget_w, int widget_h) const;

    void world_to_screen(circuitcore::board::Point2 wp,
                         int widget_w, int widget_h,
                         double& sx, double& sy) const;

    // Mouse drag pan. dx/dy in screen pixels. Standard "grab the world" feel:
    // dragging right moves the world right (and the camera left in world).
    void pan_pixels(double dx_px, double dy_px);

    // Zoom by `factor` while keeping the world point currently under
    // (anchor_sx, anchor_sy) fixed under that pixel.
    void zoom_at(double anchor_sx, double anchor_sy, double factor,
                 int widget_w, int widget_h);

    // Fit world bounding box [lo, hi] into the widget with a fractional
    // margin on each side. No-op if the box is degenerate.
    void fit_to_bounds(circuitcore::board::Point2 lo, circuitcore::board::Point2 hi,
                       int widget_w, int widget_h, double margin = 0.05);

    // Column-major 4x4 ortho projection (world → NDC), suitable for
    // glUniformMatrix4fv. Includes the Y-flip for KiCad screen convention.
    std::array<float, 16> ortho_matrix(int widget_w, int widget_h) const;
};

}  // namespace circuitcore::ui
