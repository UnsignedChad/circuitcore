// Boundary condition primitives for mpkit solvers.
//
// Every physics interface (thermal, elasticity, etc) accepts a list of
// BoundaryConditions. The solver walks the list and applies whichever
// kind is set on the matching grid face / region. Multiple conditions
// can target the same face; the solver resolves by precedence
// (Dirichlet > Robin > Neumann) which matches the COMSOL convention.

#pragma once

#include <string>
#include <vector>

namespace mpkit {

// Geometric target -- which voxels does this BC apply to?
//
//   FaceXmin .. FaceZmax : the six outer faces of the grid box.
//   VoxelRange           : an axis-aligned sub-block in index space.
//
// Future kinds (sphere, mesh selection, named domain) can extend the
// enum without breaking serialisation -- the kind is enumerated and
// unrecognised entries are skipped by the solver.
enum class BcTarget {
    FaceXmin, FaceXmax,
    FaceYmin, FaceYmax,
    FaceZmin, FaceZmax,
    VoxelRange,
};

struct VoxelRange {
    int i_lo = 0, i_hi = 0;
    int j_lo = 0, j_hi = 0;
    int k_lo = 0, k_hi = 0;
};

// Physical kind -- what does the BC do to the unknown?
enum class BcKind {
    // u = value          (Dirichlet, e.g. fixed temperature, fixed
    //                     displacement)
    Dirichlet,
    // -k * du/dn = flux  (Neumann; e.g. heat flux in W/m^2 outward)
    Neumann,
    // -k * du/dn = h * (u - u_ref)  (Robin / convection; e.g. heat
    // transfer with film coefficient h and ambient T u_ref)
    Robin,
};

struct BoundaryCondition {
    std::string name;     // human label, optional; for tree-view display
    BcTarget    target = BcTarget::FaceXmin;
    VoxelRange  range;    // only consulted when target == VoxelRange

    BcKind      kind = BcKind::Dirichlet;
    double      value = 0.0;     // Dirichlet u or Neumann flux
    double      h     = 0.0;     // Robin convection coefficient
    double      u_ref = 0.0;     // Robin ambient value
};

}  // namespace mpkit
