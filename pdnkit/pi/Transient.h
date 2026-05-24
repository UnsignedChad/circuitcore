// Time-domain step-response transient solver.
//
// Backward Euler on the implicit system  (G + C/dt) * v_{k+1} = (C/dt) * v_k + i_k
// where G is the existing conductance matrix from IrMesh, C is a diagonal
// per-node capacitance (uniform across all nodes in v0), and i_k is the
// current-injection vector at timestep k (zero before t_zero_step, then
// the static IrMesh source/sink pattern).
//
// Sink nodes are pinned to ~0 V via the same large-diagonal trick used in
// the static IrSolver so the matrix stays SPD and we can keep sparse
// Cholesky. The (G + C/dt) matrix is factored ONCE and re-used for every
// timestep; each step is just one back-substitution.
//
// This is intentionally a v0:
//   * Uniform capacitance per node (real plane has distributed C from
//     plane-pair epsilon * area / d; decap C adds at decap locations).
//   * Step-only excitation (per-node arbitrary I(t) lands in a follow-up).
//   * No UI yet -- callable from tests / CLI.

#pragma once

#include <string>
#include <vector>

#include "pi/CavityModel.h"
#include "pi/IrMesher.h"

namespace pdnkit::pi {

struct TransientConfig {
    // Per-node capacitance in Farads. Two ways to supply it:
    //   * per_node_capacitances (preferred) -- one entry per mesh node,
    //     letting the caller bake in plane-pair distributed C + lumped decap C.
    //   * per_node_capacitance (fallback)   -- uniform value applied to every
    //     node if the vector is empty. Useful for quick tests / hand-built
    //     meshes without a real stackup.
    std::vector<double> per_node_capacitances;
    double per_node_capacitance = 1.0e-12;
    double dt = 1.0e-9;          // timestep (s); pick smaller for fast transients
    int    n_steps = 1000;       // length of the simulation
    double t_zero_step = 0.0;    // time at which the step turns on
    double step_current = 1.0;   // Amperes injected at t >= t_zero_step

    // Index of the mesh node whose voltage gets recorded as obs_v(t).
    // Defaults to -1 meaning "use the first source node" (the usual case).
    int obs_node_id = -1;
};

struct TransientResult {
    std::vector<double> times;  // seconds
    std::vector<double> max_v;  // max absolute voltage across the whole mesh
    std::vector<double> obs_v;  // voltage at the observation node
    bool ok = false;
    std::string error;
};

TransientResult solve_step_transient(const IrMesh& mesh,
                                      const TransientConfig& cfg);

// Build a per-node capacitance vector from physical inputs:
//   * plane-pair distributed C:  eps_r * eps_0 * cell_area / substrate_thickness
//     contributed to every copper mesh node.
//   * lumped decap C:            each decap's C added to the single mesh node
//     nearest to its (x, y).
// Returns one C value per node, ready to drop into TransientConfig.
std::vector<double> build_distributed_capacitance(
    const IrMesh& mesh,
    double cell_size,
    double eps_r,
    double substrate_thickness_m,
    const std::vector<Decap>& decaps);

}  // namespace pdnkit::pi
