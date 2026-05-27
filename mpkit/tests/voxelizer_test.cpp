// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "circuitcore/board/Board.h"
#include "mp/Voxelizer.h"

using namespace circuitcore::board;
using namespace mpkit;

namespace {

Board make_tiny_board() {
    Board b;
    b.stackup.total_thickness = 1.6e-3;
    b.stackup.layers = {
        // F.Cu on top, In1.Cu middle, B.Cu bottom; one dielectric implied.
        {0,  "F.Cu",  "signal", 35e-6, "copper", 0,    0},
        {32, "Diel",  "dielectric", 1.5e-3, "FR4", 4.3, 0.02},
        {31, "B.Cu",  "signal", 35e-6, "copper", 0,    0},
    };
    b.nets.push_back({1, "VCC"});
    // One short segment on F.Cu from (1, 1) to (5, 1) mm, width 0.5 mm.
    Segment s;
    s.start = {1.0e-3, 1.0e-3};
    s.end   = {5.0e-3, 1.0e-3};
    s.width = 0.5e-3;
    s.layer_ordinal = 0;
    s.net_id = 1;
    b.segments.push_back(s);
    return b;
}

bool field_contains(const VoxelMaterialField& f, MaterialId id) {
    return std::find(f.ids.begin(), f.ids.end(), id) != f.ids.end();
}

}  // namespace

TEST_CASE("Empty board produces a single-voxel air grid") {
    Board b;
    auto f = voxelize_board(b);
    REQUIRE(f.grid.voxel_count() == 1u);
    REQUIRE(f.ids[0] == kAirMaterialId);
}

TEST_CASE("Tiny board: copper appears, substrate fills the bulk") {
    Board b = make_tiny_board();
    VoxelizerConfig cfg;
    cfg.cell_xy_m = 1.0e-4;   // 100 um -- fine enough to resolve a 0.5 mm trace
    cfg.cell_z_m  = 5.0e-4;
    auto f = voxelize_board(b, cfg);

    REQUIRE(f.grid.voxel_count() > 0u);
    REQUIRE(field_contains(f, kAirMaterialId));
    REQUIRE(field_contains(f, kSubstrateMaterialId));
    REQUIRE(field_contains(f, kCopperMaterialId));
}

TEST_CASE("Voxelizer grid bbox spans the trace plus the air padding") {
    Board b = make_tiny_board();
    VoxelizerConfig cfg;
    cfg.cell_xy_m = 2.5e-4;
    cfg.cell_z_m  = 5.0e-4;
    cfg.air_padding_cells = 3;
    auto f = voxelize_board(b, cfg);

    // The segment ranges x in [1, 5] mm with half-width 0.25 mm.
    const double pad = 3 * cfg.cell_xy_m;
    REQUIRE(f.grid.x0 <= (1.0e-3 - 0.25e-3 - pad) + 1e-12);
    REQUIRE(f.grid.x0 + f.grid.nx() * f.grid.dx() >=
            5.0e-3 + 0.25e-3 + pad - 1e-12);
}
