// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "render/Camera3D.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sikit::render {

namespace {

constexpr double kPi = std::numbers::pi;

struct Vec3 { double x, y, z; };

Vec3 sub(Vec3 a, Vec3 b)         { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot(Vec3 a, Vec3 b)       { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
Vec3 normalize(Vec3 v) {
    const double n = std::sqrt(dot(v, v));
    if (n <= 0.0) return {0, 0, 0};
    return {v.x / n, v.y / n, v.z / n};
}

// Column-major 4x4 view from a right-handed look-at. Maps world points into
// camera space (camera at origin, +X right, +Y up, looking down -Z).
std::array<double, 16> lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(sub(target, eye));
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    return {
        s.x,            u.x,           -f.x,           0.0,
        s.y,            u.y,           -f.y,           0.0,
        s.z,            u.z,           -f.z,           0.0,
       -dot(s, eye),   -dot(u, eye),    dot(f, eye),   1.0,
    };
}

// Column-major perspective projection. Standard OpenGL form, +Z toward
// viewer, depth range [-1, 1].
std::array<double, 16> perspective(double fov_y, double aspect,
                                    double z_near, double z_far) {
    const double f = 1.0 / std::tan(0.5 * fov_y);
    const double rd = 1.0 / (z_near - z_far);
    return {
        f / aspect, 0.0,  0.0,                              0.0,
        0.0,        f,    0.0,                              0.0,
        0.0,        0.0, (z_far + z_near) * rd,            -1.0,
        0.0,        0.0, (2.0 * z_far * z_near) * rd,       0.0,
    };
}

// Column-major 4x4 multiply: c = a * b.
std::array<double, 16> mul(const std::array<double, 16>& a,
                            const std::array<double, 16>& b) {
    std::array<double, 16> c{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + row] * b[col * 4 + k];
            }
            c[col * 4 + row] = s;
        }
    }
    return c;
}

std::array<float, 16> toFloat(const std::array<double, 16>& m) {
    std::array<float, 16> r{};
    for (int i = 0; i < 16; ++i) r[i] = static_cast<float>(m[i]);
    return r;
}

}  // namespace

void Camera3D::eye_position(double& ex, double& ey, double& ez) const noexcept {
    const double ce = std::cos(elevation);
    const double se = std::sin(elevation);
    const double ca = std::cos(azimuth);
    const double sa = std::sin(azimuth);
    ex = target_x + distance * ce * ca;
    ey = target_y + distance * ce * sa;
    ez = target_z + distance * se;
}

void Camera3D::orbit_pixels(double dx_px, double dy_px, int widget_h) {
    if (widget_h <= 0) return;
    // One full screen height ≈ 180° of elevation; same scale on azimuth.
    const double rad_per_px = kPi / widget_h;
    azimuth   -= dx_px * rad_per_px;
    elevation += dy_px * rad_per_px;
    // Clamp elevation just inside the poles to avoid gimbal-lock at exactly ±π/2.
    constexpr double kCap = 0.49 * kPi;
    elevation = std::clamp(elevation, -kCap, kCap);
    // Wrap azimuth for numerical stability.
    while (azimuth >  kPi) azimuth -= 2.0 * kPi;
    while (azimuth < -kPi) azimuth += 2.0 * kPi;
}

void Camera3D::pan_pixels(double dx_px, double dy_px, int widget_h) {
    if (widget_h <= 0) return;
    // Pixels-to-world at the target plane: a vertical screen-height spans
    // (2 * distance * tan(fov_y / 2)) world units.
    const double world_per_px = 2.0 * distance * std::tan(0.5 * fov_y) / widget_h;

    double ex, ey, ez;
    eye_position(ex, ey, ez);
    const Vec3 fwd = normalize({target_x - ex, target_y - ey, target_z - ez});
    const Vec3 up_world{0.0, 0.0, 1.0};
    const Vec3 right = normalize(cross(fwd, up_world));
    const Vec3 up    = cross(right, fwd);

    // Dragging right should slide the world right under the cursor →
    // move target in the opposite direction.
    target_x -= (right.x * dx_px - up.x * dy_px) * world_per_px;
    target_y -= (right.y * dx_px - up.y * dy_px) * world_per_px;
    target_z -= (right.z * dx_px - up.z * dy_px) * world_per_px;
}

void Camera3D::zoom(double factor) {
    if (factor <= 0.0) return;
    distance *= factor;
    distance = std::clamp(distance, 1.0e-4, 10.0);
}

void Camera3D::fit_to_bounds(circuitcore::board::Point2 lo_xy, circuitcore::board::Point2 hi_xy,
                              double z_mid, int widget_w, int widget_h,
                              double margin) {
    const double dx = (hi_xy.x - lo_xy.x) * (1.0 + 2.0 * margin);
    const double dy = (hi_xy.y - lo_xy.y) * (1.0 + 2.0 * margin);
    if (dx <= 0.0 || dy <= 0.0 || widget_w <= 0 || widget_h <= 0) return;

    target_x = 0.5 * (lo_xy.x + hi_xy.x);
    target_y = 0.5 * (lo_xy.y + hi_xy.y);
    target_z = z_mid;

    // Distance needed so half-fov vertically spans dy, and the horizontal
    // half-fov spans dx. Pick the larger of the two so both fit.
    const double aspect = static_cast<double>(widget_w) / widget_h;
    const double dist_for_h = 0.5 * dy / std::tan(0.5 * fov_y);
    const double fov_x = 2.0 * std::atan(std::tan(0.5 * fov_y) * aspect);
    const double dist_for_w = 0.5 * dx / std::tan(0.5 * fov_x);
    distance = std::max(dist_for_h, dist_for_w);
    distance = std::clamp(distance, 1.0e-4, 10.0);
}

std::array<float, 16> Camera3D::view_matrix() const {
    double ex, ey, ez;
    eye_position(ex, ey, ez);
    return toFloat(lookAt({ex, ey, ez},
                          {target_x, target_y, target_z},
                          {0.0, 0.0, 1.0}));
}

std::array<float, 16> Camera3D::view_projection(int widget_w, int widget_h) const {
    if (widget_w <= 0 || widget_h <= 0) {
        return toFloat({1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1});
    }
    double ex, ey, ez;
    eye_position(ex, ey, ez);
    const auto v = lookAt({ex, ey, ez},
                          {target_x, target_y, target_z},
                          {0.0, 0.0, 1.0});
    const double aspect = static_cast<double>(widget_w) / widget_h;
    const auto p = perspective(fov_y, aspect, z_near, z_far);
    return toFloat(mul(p, v));
}

}  // namespace sikit::render
