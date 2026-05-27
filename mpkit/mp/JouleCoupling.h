// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// pdnkit IR-drop solution -> mpkit volumetric heat source.
//
// pdnkit's IrSolver returns one voltage per mesh node and a list of
// resistors (conductance G in Siemens) between adjacent nodes. The
// power dissipated by resistor (a, b) is
//
//     P = (V_a - V_b)^2 * G        [Watts]
//
// To turn that into the volumetric source Q(x, y, z) the heat solver
// needs, we split each resistor's power evenly between its two end
// nodes (i.e. each node receives the sum of half its incident-edge
// powers) then drop the per-node Watts into the voxel containing the
// node's (x, y, z) world position, divided by that voxel's volume to
// get W/m^3.
//
// Nodes whose layer is absent from material_field.layer_ordinal_to_k
// or whose (x, y) falls outside the grid bbox are skipped and counted
// in dropped_nodes -- usually a sign the caller voxelized a sub-bbox
// of the board and the IR mesh extends past it.
//
// What this v1 does NOT do:
//   * Spread power along the resistor segment (it lands at the two
//     endpoint cells). Refine by walking interior voxels of the
//     segment if the grid resolution is much finer than the mesh.
//   * Treat via resistors specially. A via that connects two layers
//     deposits half its power at each layer's k-slice -- physically
//     reasonable for a vertical resistor sized like a copper-foil
//     cell, slightly under-counts hot vias where the heat capacity
//     is concentrated.

#pragma once

#include <string>

#include "circuitcore/field/Field3D.h"
#include "mp/Voxelizer.h"

namespace pdnkit::pi {
struct IrMesh;
struct Solution;
}  // namespace pdnkit::pi

namespace mpkit {

struct JouleSourceField {
    circuitcore::field::Field3D source;     // W/m^3 per voxel
    double      total_power_w = 0.0;        // sum (V * Q) for sanity
    int         dropped_nodes = 0;          // count of out-of-grid mesh nodes
    bool        ok = false;
    std::string error;
};

// Build a volumetric heat-source field from an IR-drop solution and a
// previously-voxelized board. The returned source's shape exactly
// matches material_field.grid so the caller can drop it straight into
// SteadyHeatConfig::volumetric_source or TransientHeatConfig::
// volumetric_source.
JouleSourceField ir_solution_to_joule_source(
    const pdnkit::pi::IrMesh& mesh,
    const pdnkit::pi::Solution& solution,
    const VoxelMaterialField& material_field);

}  // namespace mpkit
