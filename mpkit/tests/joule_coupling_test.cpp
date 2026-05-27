// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// pdnkit -> mpkit Joule heating coupling.
//
// Tests build synthetic IrMesh + Solution by hand so the coupling
// logic is exercised in isolation from the pdnkit mesher and IR
// solver.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

#include "mp/JouleCoupling.h"
#include "mp/Voxelizer.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

using mpkit::ir_solution_to_joule_source;
using mpkit::JouleSourceField;
using mpkit::VoxelMaterialField;

namespace {

// Build a 1-cell-tall grid with copper occupying every cell, and a
// layer_ordinal_to_k entry that puts layer 0 at the only z slice.
VoxelMaterialField flat_copper_grid(int nx, int ny,
                                     double dx, double dy, double dz,
                                     int layer_ordinal = 0) {
    VoxelMaterialField f;
    f.grid.spec = {nx, ny, 1, dx, dy, dz};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(nx) * ny, /*copper*/0);
    f.layer_ordinal_to_k[layer_ordinal] = 0;
    return f;
}

}  // namespace

TEST_CASE("Single resistor across one cell deposits the right total power") {
    // Two nodes at the centres of two adjacent cells, one resistor.
    const double dx = 1.0e-3, dy = 1.0e-3, dz = 35.0e-6;
    auto f = flat_copper_grid(4, 1, dx, dy, dz);

    pdnkit::pi::IrMesh mesh;
    pdnkit::pi::Node n0; n0.id = 0; n0.x = f.grid.cx(0); n0.y = f.grid.cy(0); n0.layer_ordinal = 0;
    pdnkit::pi::Node n1; n1.id = 1; n1.x = f.grid.cx(1); n1.y = f.grid.cy(0); n1.layer_ordinal = 0;
    mesh.nodes = {n0, n1};
    mesh.resistors.push_back({/*from_node=*/0, /*to_node=*/1, /*conductance=*/2.5});

    pdnkit::pi::Solution sol;
    sol.voltages = {1.0, 0.0};
    sol.ok = true;

    JouleSourceField out = ir_solution_to_joule_source(mesh, sol, f);
    REQUIRE(out.ok);
    REQUIRE(out.dropped_nodes == 0);

    const double V = dx * dy * dz;
    const double expected_P = (1.0 - 0.0) * (1.0 - 0.0) * 2.5;  // = 2.5 W
    REQUIRE(std::abs(out.total_power_w - expected_P) < 1e-12);

    // Half the power lands in each of the two endpoint voxels.
    REQUIRE(std::abs(out.source.at(0, 0, 0) - 0.5 * expected_P / V) < 1e-9);
    REQUIRE(std::abs(out.source.at(1, 0, 0) - 0.5 * expected_P / V) < 1e-9);
    // All other voxels stay zero.
    REQUIRE(out.source.at(2, 0, 0) == 0.0);
    REQUIRE(out.source.at(3, 0, 0) == 0.0);
}

TEST_CASE("Resistor whose node sits outside the grid is dropped, not crashed") {
    const double dx = 1.0e-3, dy = 1.0e-3, dz = 35.0e-6;
    auto f = flat_copper_grid(2, 1, dx, dy, dz);

    pdnkit::pi::IrMesh mesh;
    pdnkit::pi::Node n0; n0.id = 0; n0.x = f.grid.cx(0);          n0.y = f.grid.cy(0); n0.layer_ordinal = 0;
    pdnkit::pi::Node n1; n1.id = 1; n1.x = f.grid.cx(0) + 1.0;    n1.y = f.grid.cy(0); n1.layer_ordinal = 0;  // far outside
    mesh.nodes = {n0, n1};
    mesh.resistors.push_back({0, 1, 1.0});

    pdnkit::pi::Solution sol;
    sol.voltages = {1.0, 0.0};
    sol.ok = true;

    JouleSourceField out = ir_solution_to_joule_source(mesh, sol, f);
    REQUIRE(out.ok);
    REQUIRE(out.dropped_nodes == 1);
    // The kept endpoint still receives its half-share of the power.
    const double V = dx * dy * dz;
    REQUIRE(std::abs(out.source.at(0, 0, 0) - 0.5 * 1.0 / V) < 1e-9);
}

TEST_CASE("Node on a layer not in layer_ordinal_to_k is dropped") {
    const double dx = 1.0e-3, dy = 1.0e-3, dz = 35.0e-6;
    auto f = flat_copper_grid(2, 1, dx, dy, dz, /*layer=*/0);

    pdnkit::pi::IrMesh mesh;
    pdnkit::pi::Node n0; n0.id = 0; n0.x = f.grid.cx(0); n0.y = f.grid.cy(0); n0.layer_ordinal = 0;
    pdnkit::pi::Node n1; n1.id = 1; n1.x = f.grid.cx(1); n1.y = f.grid.cy(0); n1.layer_ordinal = 31;  // not in the map
    mesh.nodes = {n0, n1};
    mesh.resistors.push_back({0, 1, 1.0});

    pdnkit::pi::Solution sol;
    sol.voltages = {1.0, 0.0};
    sol.ok = true;

    auto out = ir_solution_to_joule_source(mesh, sol, f);
    REQUIRE(out.ok);
    REQUIRE(out.dropped_nodes == 1);
}

TEST_CASE("Empty mesh yields an all-zero source field, not an error") {
    const double dx = 1.0e-3, dy = 1.0e-3, dz = 35.0e-6;
    auto f = flat_copper_grid(2, 2, dx, dy, dz);
    pdnkit::pi::IrMesh mesh;       // empty
    pdnkit::pi::Solution sol;      // empty

    auto out = ir_solution_to_joule_source(mesh, sol, f);
    REQUIRE(out.ok);
    REQUIRE(out.total_power_w == 0.0);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
            REQUIRE(out.source.at(i, j, 0) == 0.0);
}
