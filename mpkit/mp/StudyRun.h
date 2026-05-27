// Study orchestrator -- walk a Study, run the physics nodes, apply
// couplings between them, optionally sweep one parameter and re-run.
//
// The Study/PhysicsNode/CouplingSpec data structures (Study.h) are
// pure data; this header is what turns them into actual solver calls
// to mpkit::solve_steady_heat, pdnkit::pi::IrSolver, etc. The runner
// owns no state beyond a per-step result table the caller consumes.
//
// Per-physics config schema (the sexpr embedded inside PhysicsNode.config)
//
//   PdnIrDrop:
//     (config
//       (net_id        <int>)        REQUIRED
//       (layer_ordinal <int>)        default 0
//       (cell_size     <metres>)     default 5e-4
//       (total_current <amps>)       default 1.0)
//
//   SteadyHeat:
//     (config
//       (bc (target "FaceZmin") (kind "Robin")     (h 10.0) (u_ref 25.0))
//       (bc (target "FaceZmax") (kind "Robin")     (h 10.0) (u_ref 25.0))
//       (bc (target "FaceXmin") (kind "Dirichlet") (value 25.0)))
//
// Known coupling transforms
//
//   "joule"  (source = PdnIrDrop) -> (target = SteadyHeat,
//                                      input  = volumetric_source).
//            Wraps mpkit::ir_solution_to_joule_source.
//
// v1 supports PdnIrDrop + SteadyHeat + the joule coupling. The other
// PhysicsKinds (TransientHeat, Elasticity, PdnCavityZf, SikitFdtd)
// parse cleanly but return a "not yet dispatched" error when the
// runner reaches them.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "circuitcore/field/Field3D.h"

#include "mp/Material.h"
#include "mp/Study.h"
#include "mp/Voxelizer.h"

namespace mpkit {

struct StudyRunInput {
    Study              study;
    VoxelMaterialField material_field;

    // Required for any PdnIrDrop node (pdnkit::pi::IrMesher needs the
    // parsed Board to walk segments + pads + zones). May be nullptr if
    // the study only contains mpkit-native physics.
    const circuitcore::board::Board* board = nullptr;

    // Material lookup table indexed by MaterialId. Defaults to
    // {air(), fr4(), copper()} when empty.
    std::vector<Material> material_table;
};

struct StudyStepResult {
    std::string node_id;
    // Multi-dim sweep index. One entry per Study.sweeps axis in
    // declaration order. Empty when the study has no sweeps.
    std::vector<int> sweep_index;

    // Steady-heat output.
    circuitcore::field::Field3D temperature;
    // PdnIrDrop output (per-mesh-node voltages).
    std::vector<double> voltages;
    // Joule coupling reporting (when the step's inputs came through
    // the "joule" transform).
    double total_joule_power_w = 0.0;

    bool        ok = false;
    std::string error;
};

struct StudyRunResult {
    // Per-step results in the order the runner evaluated them. With
    // a sweep of N values, this contains N * |solve_order| entries.
    std::vector<StudyStepResult> steps;
    bool        ok = false;
    std::string error;
};

// Run the study end-to-end. Returns a result whose `ok` is true only
// if every step succeeded.
StudyRunResult run_study(const StudyRunInput& input);

}  // namespace mpkit
