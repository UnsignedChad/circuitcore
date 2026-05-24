#include "render/Camera2D.h"

#include <algorithm>

namespace sikit::render {

using circuitcore::board::Point2;

Point2 Camera2D::screen_to_world(double sx, double sy, int w, int h) const {
    return {center.x + (sx - 0.5 * w) / pixels_per_meter,
            center.y + (sy - 0.5 * h) / pixels_per_meter};
}

void Camera2D::world_to_screen(Point2 wp, int w, int h,
                                double& sx, double& sy) const {
    sx = 0.5 * w + (wp.x - center.x) * pixels_per_meter;
    sy = 0.5 * h + (wp.y - center.y) * pixels_per_meter;
}

void Camera2D::pan_pixels(double dx_px, double dy_px) {
    center.x -= dx_px / pixels_per_meter;
    center.y -= dy_px / pixels_per_meter;
}

void Camera2D::zoom_at(double anchor_sx, double anchor_sy, double factor,
                       int w, int h) {
    if (factor <= 0.0) return;
    Point2 world_anchor = screen_to_world(anchor_sx, anchor_sy, w, h);
    pixels_per_meter *= factor;
    Point2 new_world = screen_to_world(anchor_sx, anchor_sy, w, h);
    center.x += world_anchor.x - new_world.x;
    center.y += world_anchor.y - new_world.y;
}

void Camera2D::fit_to_bounds(Point2 lo, Point2 hi, int w, int h, double margin) {
    const double dx = (hi.x - lo.x) * (1.0 + 2.0 * margin);
    const double dy = (hi.y - lo.y) * (1.0 + 2.0 * margin);
    if (dx <= 0.0 || dy <= 0.0 || w <= 0 || h <= 0) return;
    center.x = 0.5 * (lo.x + hi.x);
    center.y = 0.5 * (lo.y + hi.y);
    pixels_per_meter = std::min(static_cast<double>(w) / dx,
                                static_cast<double>(h) / dy);
}

std::array<float, 16> Camera2D::ortho_matrix(int w, int h) const {
    // Visible world rect, with t < b because KiCad Y grows downward.
    const double half_w = 0.5 * w / pixels_per_meter;
    const double half_h = 0.5 * h / pixels_per_meter;
    const double l = center.x - half_w;
    const double r = center.x + half_w;
    const double t = center.y - half_h;
    const double b = center.y + half_h;

    // Standard ortho with the (t, b) order swapped → Y-flip baked into the
    // matrix. World (x = l) → NDC -1; (x = r) → +1. World (y = t) → +1 (top
    // of screen); (y = b) → -1 (bottom).
    const float A  = static_cast<float>(2.0 / (r - l));
    const float B  = static_cast<float>(2.0 / (t - b));
    const float tx = static_cast<float>(-(r + l) / (r - l));
    const float ty = static_cast<float>(-(t + b) / (t - b));

    // Column-major layout.
    return {
        A,  0,  0, 0,
        0,  B,  0, 0,
        0,  0, -1, 0,
        tx, ty, 0, 1,
    };
}

}  // namespace sikit::render
