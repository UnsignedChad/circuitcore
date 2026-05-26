// Time-domain transient heat solver on the mpkit voxel grid.
//
//     rho c dT/dt = nabla . (k nabla T) + Q              (interior)
//                                T = T_d                  (Dirichlet)
//                      -k dT/dn = q                       (Neumann)
//                      -k dT/dn = h (T - T_ref)           (Robin)
//
// Backward Euler -- unconditionally stable, no CFL constraint. The
// implicit step
//
//     (M/dt + K) T^{n+1} = M/dt T^n + b
//
// has the same K as the steady solver plus a diagonal mass M whose
// (i,i) entry is rho_i c_i V_i. We factor (M/dt + K) once and re-use
// the Cholesky decomposition for every subsequent step.
//
// What this v1 does NOT do:
//   * Adaptive dt -- the user picks a fixed step. RC time-scale of an
//     FR-4 board is seconds; pick dt ~ 0.1 s for thermal transients.
//   * Time-varying BCs -- BCs are evaluated once at t=0 and reused. To
//     drive a time profile, call the solver in segments with updated
//     BCs and seed T^0 from the previous segment's final state.
//   * Adaptive K(T) coupling -- if k depends on T, wrap in fixed-point.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/BoundaryCondition.h"
#include "mp/Material.h"
#include "mp/Voxelizer.h"

namespace mpkit {

struct TransientHeatConfig {
    VoxelMaterialField        material_field;
    std::vector<Material>     material_table;
    circuitcore::field::Field3D volumetric_source;        // W/m^3 per voxel; empty = zero
    std::vector<BoundaryCondition> bcs;

    // Initial condition. Same shape as the grid. If empty, defaults to
    // initial_temperature_uniform on every voxel.
    circuitcore::field::Field3D initial_temperature;
    double initial_temperature_uniform = 0.0;

    double dt_s   = 0.1;     // timestep, seconds
    int    steps  = 100;     // number of timesteps to walk

    // Cell indices to record at every step (post-step temperature).
    // Empty means record nothing -- the caller can still read the
    // final field from TransientHeatResult.final_temperature.
    struct ObsPoint { int i = 0, j = 0, k = 0; };
    std::vector<ObsPoint> observation_points;
};

struct TransientHeatResult {
    circuitcore::field::Field3D final_temperature;        // T at t = dt * steps
    std::vector<double> times;                            // length = steps + 1
    // One row per observation point, length = steps + 1.
    std::vector<std::vector<double>> obs_history;
    bool        ok = false;
    std::string error;
};

TransientHeatResult solve_transient_heat(const TransientHeatConfig& cfg);

}  // namespace mpkit
