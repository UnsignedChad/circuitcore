// Linear elasticity FEM solver on the mpkit voxel grid.
//
// Each voxel cell is one trilinear Q1 hexahedral element with 8 nodes
// at the cell corners. Nodal unknowns are the three displacement
// components (u_x, u_y, u_z); total dof count is 3 * (nx+1) * (ny+1) *
// (nz+1). Element stiffness K_e is integrated with 2*2*2 Gauss-
// Legendre quadrature. The global K is symmetric positive-definite
// (after Dirichlet pinning removes rigid-body modes) so the same
// Eigen::SimplicialLLT path the thermal solver uses applies here.
//
// Per-cell isotropic material from MaterialMap:
//
//     lambda = E nu / ((1 + nu)(1 - 2 nu))
//     mu     = E / (2 (1 + nu))
//
// Source terms:
//
//   * volumetric body force f (N/m^3) per voxel -- gravity, magnetic
//     forces, etc. Lumped into nodal forces via the shape-function
//     mass integral.
//
//   * thermal strain epsilon_th = alpha * (T - T_ref) * I per voxel.
//     The equivalent nodal force is integral over element of
//     B^T D epsilon_th dV. Drop the per-voxel temperature_change field
//     from a mpkit thermal solve straight in.
//
// Boundary conditions act on nodal dofs by face or by an explicit
// voxel range that selects the cells whose corner nodes get pinned.
//
// What this v1 does NOT do:
//   * Anisotropic materials (orthotropic E_x, E_y, E_z). Real FR-4
//     is mildly anisotropic; FRD-grade analyses want it eventually.
//   * Contact, friction, plasticity, large strain. Linear small-strain
//     only.
//   * Surface tractions in N/m^2. Use a volumetric body force on the
//     surface layer of cells as a workaround until v2.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/BoundaryCondition.h"
#include "mp/Material.h"
#include "mp/Voxelizer.h"

namespace mpkit {

// Per-dof pin mask bits. OR together to pin multiple axes.
enum DofMask : unsigned {
    DofX = 1u,
    DofY = 2u,
    DofZ = 4u,
    DofAll = 7u,
};

struct ElasticityBC {
    // Same target semantics as the thermal BC: a grid face or an
    // explicit voxel range. For elasticity the face-or-range selects
    // the NODES at the corners of the matching cells.
    BcTarget   target = BcTarget::FaceXmin;
    VoxelRange range;

    // Which axes to pin. v1 only does Dirichlet (fixed displacement);
    // traction comes in a follow-up. Use body forces meanwhile.
    unsigned pin_axes = DofAll;
    double   pin_ux   = 0.0;
    double   pin_uy   = 0.0;
    double   pin_uz   = 0.0;
};

struct ElasticityConfig {
    VoxelMaterialField    material_field;
    std::vector<Material> material_table;

    // Optional per-voxel body force (N/m^3). Each is independent; an
    // empty (zero-size) field means that component is zero everywhere.
    circuitcore::field::Field3D body_force_x;
    circuitcore::field::Field3D body_force_y;
    circuitcore::field::Field3D body_force_z;

    // Optional per-voxel temperature change relative to the stress-free
    // reference (K). Multiplied by the per-cell material's
    // thermal_expansion to get the isotropic thermal strain that
    // sources the elastic equations.
    circuitcore::field::Field3D temperature_change;

    std::vector<ElasticityBC> bcs;
};

struct ElasticityResult {
    // Nodal displacements, interleaved (ux, uy, uz) per node. Node
    // count is (nx+1) * (ny+1) * (nz+1); fastest index i, then j, k.
    std::vector<double> displacements;

    // Per-cell stress in Voigt order
    //     [sigma_xx, sigma_yy, sigma_zz, sigma_xy, sigma_yz, sigma_zx]
    // evaluated at the cell centre (xi = eta = zeta = 0). Interleaved
    // per voxel.
    std::vector<double> stress;

    // Per-cell von Mises scalar derived from stress (for plotting).
    circuitcore::field::Field3D von_mises;

    bool        ok     = false;
    std::string error;
    std::size_t n_dofs = 0;
};

ElasticityResult solve_elasticity(const ElasticityConfig& cfg);

}  // namespace mpkit
