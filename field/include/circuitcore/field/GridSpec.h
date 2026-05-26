// Shape of a uniform Cartesian grid.
//
// Cell counts in each axis plus the cell size in metres. The grid sits
// at the origin (0, 0, 0) -- if you need to place the grid somewhere in
// world space, wrap GridSpec in a richer Grid type that adds an origin
// (mpkit::Grid does exactly this for board voxelization).
//
// Free-space EM constants live here because the FDTD solver reads them
// from this header and exporting them keeps the include graph short.

#pragma once

namespace circuitcore::field {

inline constexpr double kEps0 = 8.8541878128e-12;
inline constexpr double kMu0  = 1.25663706212e-6;
inline constexpr double kC0   = 2.99792458e8;

struct GridSpec {
    int nx = 0;
    int ny = 0;
    int nz = 0;
    double dx = 0.0;  // metres
    double dy = 0.0;
    double dz = 0.0;
};

// CFL-stable time step for a uniform-mesh free-space FDTD solver.
//   dt <= 1 / (c * sqrt(1/dx^2 + 1/dy^2 + 1/dz^2))
// Caller usually multiplies by a safety factor (0.99 is conventional).
// Throws std::invalid_argument on non-positive cell size.
double cfl_dt(const GridSpec& g, double safety = 0.99);

}  // namespace circuitcore::field
