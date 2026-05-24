#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "render/Camera2D.h"

using sikit::render::Camera2D;
using circuitcore::board::Point2;
using Catch::Approx;

namespace {
constexpr double kEps = 1e-9;
}

TEST_CASE("camera: defaults centered at origin, 1px=1mm", "[camera]") {
    Camera2D c;
    REQUIRE(c.center.x == 0.0);
    REQUIRE(c.center.y == 0.0);
    REQUIRE(c.pixels_per_meter == 1000.0);
}

TEST_CASE("camera: screen center maps to world center", "[camera]") {
    Camera2D c;
    c.center = {0.05, 0.07};
    double sx = 0, sy = 0;
    c.world_to_screen({0.05, 0.07}, 800, 600, sx, sy);
    REQUIRE(sx == Approx(400.0));
    REQUIRE(sy == Approx(300.0));

    Point2 w = c.screen_to_world(400, 300, 800, 600);
    REQUIRE(w.x == Approx(0.05));
    REQUIRE(w.y == Approx(0.07));
}

TEST_CASE("camera: world↔screen round-trip", "[camera]") {
    Camera2D c;
    c.center = {0.01, -0.02};
    c.pixels_per_meter = 5000.0;
    for (double sx : {0.0, 137.5, 799.99}) {
        for (double sy : {0.0, 250.0, 599.0}) {
            Point2 w = c.screen_to_world(sx, sy, 800, 600);
            double rx = 0, ry = 0;
            c.world_to_screen(w, 800, 600, rx, ry);
            REQUIRE(rx == Approx(sx).margin(kEps));
            REQUIRE(ry == Approx(sy).margin(kEps));
        }
    }
}

TEST_CASE("camera: pan moves center by inverse of drag", "[camera]") {
    Camera2D c;
    c.center = {0.0, 0.0};
    c.pixels_per_meter = 1000.0;
    c.pan_pixels(100, 50);
    // Dragging the world +100px right at 1000 px/m means the camera moved
    // -0.1m left in world; new center.x = -0.1m.
    REQUIRE(c.center.x == Approx(-0.1));
    REQUIRE(c.center.y == Approx(-0.05));
}

TEST_CASE("camera: zoom_at keeps anchor pixel pointing at same world", "[camera]") {
    Camera2D c;
    c.center = {0.1, 0.1};
    c.pixels_per_meter = 1000.0;
    const int W = 800, H = 600;

    const double ax = 250, ay = 175;
    Point2 before = c.screen_to_world(ax, ay, W, H);

    c.zoom_at(ax, ay, 2.5, W, H);
    Point2 after = c.screen_to_world(ax, ay, W, H);

    REQUIRE(after.x == Approx(before.x).margin(kEps));
    REQUIRE(after.y == Approx(before.y).margin(kEps));
    REQUIRE(c.pixels_per_meter == Approx(2500.0));
}

TEST_CASE("camera: fit_to_bounds centers and zooms to fit", "[camera]") {
    Camera2D c;
    c.fit_to_bounds({0.0, 0.0}, {0.10, 0.05}, 800, 600, 0.0);
    REQUIRE(c.center.x == Approx(0.05));
    REQUIRE(c.center.y == Approx(0.025));
    // Width-limited: 800px / 0.1m = 8000 px/m, vs 600/0.05=12000. Take the smaller.
    REQUIRE(c.pixels_per_meter == Approx(8000.0));
}

TEST_CASE("camera: ortho matrix has Y-flip and centers world", "[camera]") {
    Camera2D c;
    c.center = {0.0, 0.0};
    c.pixels_per_meter = 1000.0;
    auto m = c.ortho_matrix(800, 600);

    // Column-major: m[1] is column 0 row 1 (should be 0); m[5] is column 1 row 1.
    // World (0,0,0,1) → NDC must be (tx, ty, 0, 1).
    // Center is at world origin, so tx = ty = 0.
    REQUIRE(m[12] == Approx(0.0).margin(kEps));  // tx
    REQUIRE(m[13] == Approx(0.0).margin(kEps));  // ty

    // World y > 0 should map to NDC y < 0 (Y flip for KiCad-down convention).
    // Multiply m * (0, 0.001, 0, 1) where 0.001m is below center.
    // ny = m[5]*0.001 + m[13]
    double ny = m[5] * 0.001 + m[13];
    REQUIRE(ny < 0.0);
}
