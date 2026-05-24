// Board -> Yee-grid rasteriser for FDTD3D.
//
// Takes a circuitcore::board::Board plus a SiStackup (for per-layer
// thicknesses + dielectric properties) and writes the geometry into
// an FDTD3D instance:
//   - Trace segments       -> PEC cells on the appropriate z-slab
//   - Filled copper zones  -> PEC cells on the appropriate z-slab
//   - Vias                 -> PEC columns through the stackup
//   - Substrate layers     -> set_material_box for the dielectric
//
// What it does NOT do (yet, on purpose)
//
//   - PMC / open boundaries -- caller chooses FDTD3D::Boundary
//   - Curved geometry -- staircased to the Cartesian grid; arcs in
//     KiCad have already been flattened to polylines at parse time
//   - Surface roughness or sub-cell conductor thickness -- a trace
//     is exactly one cell thick on the copper layer's z-slab
//   - Conductor loss -- we use PEC. A future PR can swap in a
//     finite-conductivity sigma-based wall.
//
// Coordinate mapping
//
//   The board's (x, y) are in metres, with the origin wherever KiCad
//   put it. The Yee grid origin is the user's choice via
//   RasterMapping::origin. The grid extents are determined by
//   GridSpec; cells whose centres fall outside the board bbox are
//   left as vacuum.
//
//   The z axis maps the stackup. RasterMapping::layer_z[ordinal] gives
//   the z-coordinate of the *top* of each copper layer; the substrate
//   between two copper layers fills the cells in between. The top
//   copper layer sits at z=0 by convention; positive z goes down
//   into the board.

#pragma once

#include <map>
#include <vector>

#include "circuitcore/board/Board.h"
#include "si/Fdtd3d.h"

namespace sikit::fdtd {

// Mapping from board (x, y, z) coordinates to Yee grid (i, j, k).
//
// The conversion is linear: i = (x - origin.x) / dx, with rounding to
// nearest integer when the rasteriser writes a cell.
struct RasterMapping {
    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;
    // Per-layer-ordinal z-coordinate of the *top* of each copper layer.
    // Computed from the SiStackup at build time and stored here so the
    // raster code is independent of the stackup type.
    std::map<int, double> layer_z;
};

// Rasterisation summary -- how many cells did each kind of feature
// place into the grid? Useful for the future "fdtd info" CLI and as a
// sanity check in tests.
struct RasterReport {
    std::size_t segment_pec_cells = 0;
    std::size_t zone_pec_cells    = 0;
    std::size_t via_pec_cells     = 0;
    std::size_t substrate_cells   = 0;
    int n_segments_processed      = 0;
    int n_zones_processed         = 0;
    int n_vias_processed          = 0;
};

// Build a default RasterMapping from a board + stackup. Origin is the
// lower-left of the board bounding box on top of the F.Cu plane; layer
// z values come from the SiStackup item thicknesses, cumulated.
RasterMapping make_default_mapping(
    const circuitcore::board::Board& board);

// Rasterise the whole board into the solver. PEC mask gets the
// conductors; set_material_box gets the substrate dielectric.
RasterReport rasterize_board(FDTD3D& s,
                                const circuitcore::board::Board& board,
                                const RasterMapping& m);

// Lower-level helpers (also exposed for tests).
std::size_t rasterize_segment(FDTD3D& s,
                                const circuitcore::board::Segment& seg,
                                const RasterMapping& m);

std::size_t rasterize_zone(FDTD3D& s,
                              const circuitcore::board::Zone& z,
                              const RasterMapping& m);

std::size_t rasterize_via(FDTD3D& s,
                            const circuitcore::board::Via& v,
                            const RasterMapping& m);

}  // namespace sikit::fdtd
