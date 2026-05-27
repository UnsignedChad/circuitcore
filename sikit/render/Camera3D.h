// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// 3D orbit camera for stackup-aware PCB viewing.
//
// Spherical orbit around a world-space target point: drag-LMB rotates
// (azimuth + elevation), drag-MMB pans the target along the view plane,
// wheel changes orbit distance. Produces a column-major view-projection
// matrix suitable for glUniformMatrix4fv.
//
// Coordinate convention (right-handed): world X right, Y "into" the
// board's footprint plane (matches KiCad layout XY), Z up out of the
// board's top copper. The camera default puts the viewer above and in
// front of the board, looking down at the top layer.
//
// Sits in sikit_core, so no Qt dependency — all math is hand-rolled.

#pragma once

#include <array>

#include "circuitcore/board/Board.h"

namespace sikit::render {

class Camera3D {
public:
    // Spherical orbit parameters.
    double target_x = 0.0;       // world-space orbit center
    double target_y = 0.0;
    double target_z = 0.0;
    double azimuth = 0.5;        // radians, rotation about world Z
    double elevation = 0.9;      // radians, tilt above XY plane (π/2 = top-down)
    double distance = 0.20;      // meters from target to eye
    double fov_y = 0.70;         // vertical field of view (radians, ~40°)
    double z_near = 1.0e-4;
    double z_far  = 10.0;

    // Returns the eye position derived from target + spherical orbit.
    void eye_position(double& ex, double& ey, double& ez) const noexcept;

    // Mouse-drag orbit. dx/dy in screen pixels. Standard "drag the world" feel.
    void orbit_pixels(double dx_px, double dy_px, int widget_h);

    // Mouse-drag pan: moves the target perpendicular to the view direction,
    // scaled so one screen pixel ≈ one pixel of motion at the target plane.
    void pan_pixels(double dx_px, double dy_px, int widget_h);

    // Multiplicative zoom — clamped so we can't pass through the target or
    // wander off to infinity.
    void zoom(double factor);

    // Frame a world AABB so the whole board (in XY) is visible from the
    // current orbit angle. Leaves Z target on the stackup mid-plane.
    void fit_to_bounds(circuitcore::board::Point2 lo_xy, circuitcore::board::Point2 hi_xy,
                       double z_mid, int widget_w, int widget_h,
                       double margin = 0.15);

    // Column-major view-projection (proj * view) for OpenGL.
    std::array<float, 16> view_projection(int widget_w, int widget_h) const;

    // Column-major view matrix alone — useful for lighting that lives in
    // view space.
    std::array<float, 16> view_matrix() const;
};

}  // namespace sikit::render
