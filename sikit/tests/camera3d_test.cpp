// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "render/Camera3D.h"

using sikit::render::Camera3D;
using Catch::Approx;

TEST_CASE("camera3d: eye_position derives spherical → Cartesian correctly", "[cam3d]") {
    Camera3D c;
    c.target_x = 0.05;
    c.target_y = -0.02;
    c.target_z = 0.001;
    c.azimuth   = 0.0;          // pointing at +X
    c.elevation = 0.0;          // in the XY plane
    c.distance  = 0.10;

    double ex, ey, ez;
    c.eye_position(ex, ey, ez);
    // Eye = target + distance·(cos(elev)·cos(az), cos(elev)·sin(az), sin(elev))
    REQUIRE(ex == Approx(0.05 + 0.10));
    REQUIRE(ey == Approx(-0.02));
    REQUIRE(ez == Approx(0.001));
}

TEST_CASE("camera3d: orbit_pixels clamps elevation away from poles", "[cam3d]") {
    Camera3D c;
    c.elevation = 0.0;
    // 10x the screen height should saturate at the elevation cap.
    c.orbit_pixels(0.0, 8000.0, 800);
    REQUIRE(c.elevation <= 0.5 * std::numbers::pi);
    REQUIRE(c.elevation > 1.4);    // close to the +π/2 cap (~1.539)

    c.orbit_pixels(0.0, -16000.0, 800);
    REQUIRE(c.elevation >= -0.5 * std::numbers::pi);
    REQUIRE(c.elevation < -1.4);
}

TEST_CASE("camera3d: orbit_pixels wraps azimuth into [-π, π]", "[cam3d]") {
    Camera3D c;
    c.azimuth = 0.0;
    // Big azimuth swing — should wrap several times.
    c.orbit_pixels(20000.0, 0.0, 800);
    REQUIRE(c.azimuth >= -std::numbers::pi);
    REQUIRE(c.azimuth <=  std::numbers::pi);
}

TEST_CASE("camera3d: pan_pixels moves target perpendicular to view", "[cam3d]") {
    Camera3D c;
    c.target_x = 0; c.target_y = 0; c.target_z = 0;
    c.azimuth = 0.0; c.elevation = 0.0;   // looking along +X
    c.distance = 0.10;

    // Drag right ⇒ target should slide in −Y (or +Y, depending on
    // right-hand convention) but z and x should be unchanged.
    c.pan_pixels(100.0, 0.0, 800);
    REQUIRE(c.target_x == Approx(0.0).margin(1e-9));
    REQUIRE(std::abs(c.target_y) > 0.0);
}

TEST_CASE("camera3d: zoom clamps distance to a sane band", "[cam3d]") {
    Camera3D c;
    c.distance = 0.10;

    // 1000× shrink should bottom out at the lower clamp.
    for (int i = 0; i < 60; ++i) c.zoom(0.5);
    REQUIRE(c.distance >= 1.0e-4);
    REQUIRE(c.distance <  1.0e-3);

    // 1000× growth should hit the upper clamp.
    for (int i = 0; i < 80; ++i) c.zoom(2.0);
    REQUIRE(c.distance <= 10.0);
    REQUIRE(c.distance > 1.0);
}

TEST_CASE("camera3d: fit_to_bounds centers target on the box", "[cam3d]") {
    Camera3D c;
    c.fit_to_bounds({0.0, 0.0}, {0.10, 0.05},
                    /*z_mid=*/0.0008, /*w=*/800, /*h=*/600,
                    /*margin=*/0.0);
    REQUIRE(c.target_x == Approx(0.05));
    REQUIRE(c.target_y == Approx(0.025));
    REQUIRE(c.target_z == Approx(0.0008));
    // distance should be > 0 (clamped) and roughly proportional to the
    // bigger of the two side lengths.
    REQUIRE(c.distance > 0.0);
    REQUIRE(c.distance < 10.0);
}

TEST_CASE("camera3d: view_projection has 16 entries and is non-degenerate",
          "[cam3d]") {
    Camera3D c;
    c.fit_to_bounds({0.0, 0.0}, {0.05, 0.05}, 0.001, 800, 600);
    auto m = c.view_projection(800, 600);
    REQUIRE(m.size() == 16);
    // Not the identity (the camera has moved away from origin).
    bool any_nonidentity = false;
    const std::array<float, 16> id{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int i = 0; i < 16; ++i) {
        if (std::abs(m[i] - id[i]) > 1e-6f) { any_nonidentity = true; break; }
    }
    REQUIRE(any_nonidentity);
    // Numeric sanity — no NaNs or infs.
    for (float v : m) {
        REQUIRE(std::isfinite(v));
    }
}

TEST_CASE("camera3d: zero widget size returns identity matrix safely",
          "[cam3d]") {
    Camera3D c;
    auto m = c.view_projection(0, 0);
    REQUIRE(m[0] == Approx(1.0f));
    REQUIRE(m[5] == Approx(1.0f));
    REQUIRE(m[10] == Approx(1.0f));
    REQUIRE(m[15] == Approx(1.0f));
}
