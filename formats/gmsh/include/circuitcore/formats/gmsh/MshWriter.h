// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// gmsh .msh exporter for mpkit's voxel hex grid.
//
// Why: mpkit has its own thermal / mechanical / EM solvers, but the
// honest way to gain confidence in those numbers is to feed the same
// geometry into an independent solver and check the answers line up.
// ElmerFEM is the natural choice: open source, mature, reads gmsh
// format directly via ElmerGrid. This writer emits ASCII gmsh v2.2
// since that is the format Elmer's tools handle out of the box.
//
// What gets written:
//
//   * One linear hex8 element per *non-air* voxel. Air voxels are
//     skipped because Elmer doesnt need an explicit air region for
//     conductive heat transfer or static elasticity, and emitting
//     them inflates the file by 10x without changing the answer.
//
//   * Shared corner nodes between adjacent cells: a 100x100x100 grid
//     produces ~10^6 elements but ~10^6 nodes, not 8 * 10^6.
//
//   * One $PhysicalNames entry per distinct material id in the field,
//     so the Elmer .sif can reference "copper" / "substrate" / etc.
//     by name rather than by numeric tag.
//
// Out of scope for v1: gmsh v4 binary, periodicity, boundary
// elements (quads on the outer faces). ElmerGrid synthesises the
// boundary quads automatically; add them here if a use case shows up.

#pragma once

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>

#include "mp/Voxelizer.h"

namespace circuitcore::formats::gmsh {

struct WriteError {
    std::string message;
};

// Map a material id to the name written to $PhysicalNames. Default
// mapping knows the three sentinel ids mpkit ships with; pass your own
// table if you want material_3 to read as "copper-at-25C" instead.
struct MaterialNameMap {
    std::string operator()(mpkit::MaterialId id) const;
};

// Write a voxel field to an ASCII gmsh v2.2 .msh stream. Returns void
// on success; on failure (stream bad bit, empty grid) the error string
// is filled. Air voxels are omitted from the element list.
std::expected<void, WriteError> write_voxel_field_msh(
    const mpkit::VoxelMaterialField& field,
    std::ostream& out,
    MaterialNameMap names = MaterialNameMap{});

// File-path convenience: opens path for writing then delegates.
std::expected<void, WriteError> write_voxel_field_msh(
    const mpkit::VoxelMaterialField& field,
    const std::filesystem::path& path,
    MaterialNameMap names = MaterialNameMap{});

}  // namespace circuitcore::formats::gmsh
