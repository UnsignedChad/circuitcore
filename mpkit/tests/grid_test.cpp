// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "mp/Grid.h"

using mpkit::Grid;

TEST_CASE("Grid cell-centre + world_to_index round trip") {
    Grid g;
    g.spec = {10, 8, 4, 1.0e-3, 1.0e-3, 0.5e-3};
    g.x0 = -5.0e-3;
    g.y0 = 0.0;
    g.z0 = 0.0;

    REQUIRE(g.voxel_count() == 10u * 8u * 4u);
    const auto close = [](double a, double b) {
        return std::abs(a - b) < 1e-12;
    };
    REQUIRE(close(g.cx(0), -4.5e-3));
    REQUIRE(close(g.cy(0),  0.5e-3));
    REQUIRE(close(g.cz(3),  0.25e-3 + 3.0 * 0.5e-3));

    // World -> index of the cell centre returns the same cell.
    for (int i = 0; i < g.nx(); ++i)
        for (int j = 0; j < g.ny(); ++j)
            for (int k = 0; k < g.nz(); ++k) {
                auto idx = g.world_to_index(g.cx(i), g.cy(j), g.cz(k));
                REQUIRE(idx[0] == i);
                REQUIRE(idx[1] == j);
                REQUIRE(idx[2] == k);
            }
}

TEST_CASE("Grid world_to_index returns -1 for out-of-grid axes") {
    Grid g;
    g.spec = {4, 4, 4, 1.0e-3, 1.0e-3, 1.0e-3};
    g.x0 = 0.0; g.y0 = 0.0; g.z0 = 0.0;

    auto out = g.world_to_index(-1.0, 0.5e-3, 0.5e-3);
    REQUIRE(out[0] == -1);
    REQUIRE(out[1] == 0);
    REQUIRE(out[2] == 0);

    auto out2 = g.world_to_index(0.5e-3, 0.5e-3, 99.0);
    REQUIRE(out2[0] == 0);
    REQUIRE(out2[1] == 0);
    REQUIRE(out2[2] == -1);
}
