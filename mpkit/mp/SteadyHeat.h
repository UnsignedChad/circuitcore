// Steady-state heat-equation solver on the mpkit voxel grid.
//
// Solves
//
//     -nabla . (k(x) nabla T(x)) = Q(x)        in the grid interior
//                                T = T_d        on Dirichlet faces
//          -k(x) dT/dn = q                       on Neumann faces
//          -k(x) dT/dn = h (T - T_ref)           on Robin (convective) faces
//
// using a cell-centred finite-volume discretization with harmonic-mean
// conductivities at faces. The harmonic mean is the correct face value
// when adjacent cells have different conductivities (handbook FVM; see
// Patankar "Numerical Heat Transfer and Fluid Flow", section 4.2-4) --
// it correctly degenerates to a series of thermal resistances across a
// copper / FR-4 boundary.
//
// The discretization assembles a symmetric positive-definite sparse
// matrix which we factor with Eigen::SimplicialLLT (the same fallback
// pdnkit uses when SuiteSparse is unavailable). N x N where
// N = nx * ny * nz; works fine to a few million unknowns on a desktop.
//
// What this v1 does NOT do:
//   * Temperature-dependent k (the loop pdnkit's lumped solver runs).
//     Add by wrapping solve_steady_heat in a fixed-point iteration.
//   * Anisotropic k (kx != ky != kz). FR-4 is mildly anisotropic; for
//     v1 we treat it isotropic at the in-plane value.
//   * Radiation. Add as a Robin BC with linearised h_rad.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/BoundaryCondition.h"
#include "mp/Material.h"
#include "mp/Voxelizer.h"

namespace mpkit {

struct SteadyHeatConfig {
    // Per-voxel material tag, indexed into material_table below. Must
    // match the grid the solver builds its system on.
    VoxelMaterialField material_field;

    // material_table[id] supplies thermal_conductivity (W/(m*K)) for
    // every MaterialId that appears in material_field.ids. Missing or
    // NaN conductivities produce an error.
    std::vector<Material> material_table;

    // Volumetric heat source in W/m^3 per voxel. Empty (zero-size)
    // means no source -- pure conduction with BC-driven temperature.
    circuitcore::field::Field3D volumetric_source;

    // Boundary conditions. At least one Dirichlet BC must be present
    // somewhere -- otherwise the discrete system is singular (pure
    // Neumann is rank-deficient by a constant offset).
    std::vector<BoundaryCondition> bcs;
};

struct SteadyHeatResult {
    circuitcore::field::Field3D temperature;  // K or whatever unit BCs use
    bool        ok = false;
    std::string error;
    std::size_t n_unknowns = 0;
    std::size_t nnz        = 0;
};

SteadyHeatResult solve_steady_heat(const SteadyHeatConfig& cfg);

}  // namespace mpkit
