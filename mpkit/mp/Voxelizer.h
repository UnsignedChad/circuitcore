// Rasterize a parsed Board into a per-voxel material-id field.
//
// Walks the board's layer stackup + copper geometry (segments, pads,
// vias, zones) and tags each voxel with the index of the dominant
// material at that location. The result is a uniform-extent vector
// the same shape as the Grid -- ready to hand to a thermal /
// mechanical solver as its material field.
//
// Material IDs are indices into the MaterialMap the caller supplies;
// voxels that don't match any material default to a sentinel id
// (usually pointing at air or substrate). The mapping rule is
// pluggable: the caller decides whether a copper trace voxel becomes
// "copper", "copper at 25 C", or "annealed copper" -- mpkit just
// rasterizes geometry to indices.

#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>

#include "circuitcore/board/Board.h"
#include "mp/Grid.h"

namespace mpkit {

// Material IDs are stored as 16-bit so a million-voxel grid fits in
// 2 MB. 65k distinct materials is far more than any real PCB needs.
using MaterialId = std::uint16_t;
inline constexpr MaterialId kAirMaterialId       = 0;
inline constexpr MaterialId kSubstrateMaterialId = 1;
inline constexpr MaterialId kCopperMaterialId    = 2;

struct VoxelMaterialField {
    Grid grid;
    // Row-major in (i, j, k) with i fastest.
    std::vector<MaterialId> ids;
    // Map pdnkit/board layer ordinal -> the k-slice the voxelizer
    // assigned that copper layer to. Lets downstream couplings
    // (Joule heating from pdnkit IR solutions, current-density
    // vectors from emikit emissions, etc.) project layer-resolved
    // data into 3D. Only copper layers are populated.
    std::map<int, int> layer_ordinal_to_k;
};

struct VoxelizerConfig {
    // Cell size in metres. Square cells in plan view; z resolution can
    // differ when the stackup has thin copper and thick substrate.
    double cell_xy_m = 5.0e-4;   // 0.5 mm default -- coarse but quick
    double cell_z_m  = 1.75e-5;  // 17.5 um, half-ounce copper foil

    // Pad the board bbox by this many cells of air on each side so
    // convective BCs at the outer faces have somewhere to live.
    int air_padding_cells = 2;
};

// Build the grid + material-id field for a parsed board.
//
// The returned grid is aligned to the board bbox plus the requested
// air padding. Substrate fills the dielectric layers; copper segments,
// pads, vias and zones get rasterized into their respective copper
// layers; everywhere else stays air.
VoxelMaterialField voxelize_board(const circuitcore::board::Board& board,
                                   const VoxelizerConfig& cfg = {});

}  // namespace mpkit
