// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Gmsh .geo writer -- emit a CAD-style geometry script for the
// OpenCASCADE-backed Gmsh mesher.
//
// Why this exists alongside MshWriter:
//
//   MshWriter takes our voxel grid and dumps it as hex8 elements --
//   trivially robust, but inherits the staircase artifacts and Z-
//   uniform resolution that voxels force on you. Plenty of users want
//   a "real" conformal tet mesh: refine around vias, follow trace
//   edges exactly, no aliasing on diagonals. Doing that ourselves
//   means dragging in CGAL or Netgen (heavy + licensing headaches);
//   the smarter move is to describe the geometry in Gmsh's own .geo
//   language and let Gmsh's OpenCASCADE backend do the tetrahedral-
//   isation -- it's already excellent at this.
//
// Output dialect: ASCII Gmsh .geo with `SetFactory("OpenCASCADE")`.
// Two kinds of entities get emitted:
//
//   * Substrate: the Edge.Cuts polygon extruded by the board thickness
//     (default 1.6 mm). Physical Volume "substrate".
//   * Copper:    each filled zone polygon (per copper layer) extruded
//     by the copper thickness (default 35 µm) at the layer's z plane.
//     Physical Volume "copper_F_Cu" / "copper_B_Cu" / etc.
//
// To produce a tet mesh from the output:
//
//   $ gmsh -3 board.geo -o board.msh
//
// Elmer's ElmerGrid then converts board.msh into Elmer's own mesh
// directory. Out of scope for v0:
//
//   * Individual track segments + pads + vias (zones cover most of the
//     copper area for power/ground; signal traces are usually a small
//     fraction by volume, so the thermal answer is close even when we
//     omit them). Adding them is a matter of writing more extruded
//     polygons + a final BooleanFragments to clean overlaps.
//   * Per-region characteristic-length hints. Gmsh will use a uniform
//     default unless the caller overrides via the WriteOptions.

#pragma once

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <string>

#include "circuitcore/board/Board.h"

namespace circuitcore::formats::gmsh {

struct GeoWriteError {
    std::string message;
};

struct GeoWriteOptions {
    // Characteristic length (target tet edge), metres. Smaller -> more
    // elements + slower mesher. 0.5 mm is a usable default for a board
    // a few cm on a side; tune up for big boards.
    double characteristic_length_m = 0.5e-3;

    // Copper layer physical thickness (metres). KiCad usually leaves
    // this unset in the .kicad_pcb so we default to 35 µm (1 oz).
    double copper_thickness_m = 35.0e-6;

    // Substrate total thickness fallback when board.stackup.total_thickness
    // is zero / unset (metres). 1.6 mm = standard FR-4 PCB.
    double substrate_thickness_fallback_m = 1.6e-3;

    // Emit BooleanFragments at the end so overlapping copper /
    // substrate volumes get split into a single coherent topology.
    // Off by default because (a) it rewrites every Volume tag, which
    // invalidates the Physical Volume directives the writer emits for
    // material tagging, and (b) it's expensive on big boards.
    // For two-layer designs the top/bottom copper sits *outside* the
    // substrate so no fragments are needed; turn this on if inner
    // layers (which overlap the substrate) need a clean topology and
    // you don't mind losing per-region material names.
    bool boolean_fragments = false;
};

// Write the board geometry as a Gmsh .geo script. On success returns
// void; on failure the error message describes the first thing that
// went wrong (empty outline, no zones, write failure).
std::expected<void, GeoWriteError> write_board_geo(
    const board::Board& board,
    std::ostream& out,
    const GeoWriteOptions& opts = {});

// File-path convenience overload.
std::expected<void, GeoWriteError> write_board_geo(
    const board::Board& board,
    const std::filesystem::path& path,
    const GeoWriteOptions& opts = {});

}  // namespace circuitcore::formats::gmsh
