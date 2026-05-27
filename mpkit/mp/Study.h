// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Persistent representation of a multiphysics study.
//
// A Study is the COMSOL-style "model tree" data layer: a list of
// physics nodes (each parameterised by a config blob), couplings that
// chain one node's output into another's input, a solve order, and an
// optional parameter sweep. The interpreter that actually invokes the
// solvers reads this same struct -- see StudyRun.h (follow-up PR).
//
// Persistence is sexpr (the same KiCad-aesthetic format the rest of
// circuitcore uses) for the model tree, with field outputs written as
// binary sidecar files next to the study (FieldIO.h). One .mpstudy
// file references zero or more .mpfield files.
//
// v1 keeps physics-config payloads as opaque sexpr Nodes so adding a
// new physics doesn't force a Study schema bump -- the orchestrator
// understands kind-specific configs, the persistence layer just
// round-trips them.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/sexpr/SExpr.h"

namespace mpkit {

// All physics interfaces the orchestrator can dispatch to. Each is
// either a mpkit-native solver or a bridge into another kit.
enum class PhysicsKind {
    SteadyHeat,        // mpkit::solve_steady_heat
    TransientHeat,     // mpkit::solve_transient_heat
    Elasticity,        // mpkit::solve_elasticity
    PdnIrDrop,         // pdnkit::pi::IrSolver
    PdnCavityZf,       // pdnkit::pi::cavity_impedance
    SikitFdtd,         // sikit::fdtd
};

const char* to_string(PhysicsKind k);
PhysicsKind  physics_kind_from_string(const std::string& s);

struct PhysicsNode {
    std::string id;             // unique within a Study
    std::string label;          // user-friendly display name
    PhysicsKind kind = PhysicsKind::SteadyHeat;
    // Opaque per-physics config in sexpr form. The orchestrator
    // converts this to a typed config (e.g. SteadyHeatConfig) when
    // running the node. v1 stores the raw tree so adding new physics
    // doesn't require touching this header.
    circuitcore::sexpr::Node config;
};

struct CouplingSpec {
    std::string source_node_id;
    std::string source_output;     // e.g. "voltages", "temperature"
    std::string target_node_id;
    std::string target_input;      // e.g. "temperature_change",
                                   //      "volumetric_source"
    // Optional transform that adapts source -> target. Empty means a
    // straight field assignment (shapes must match). Known transform
    // names so far: "joule" (pdnkit IR Solution -> heat W/m^3).
    std::string transform;
};

struct SweepSpec {
    // Path of the parameter to vary, using "node:<id>/config/<key>"
    // syntax. The orchestrator walks the sexpr config tree by key.
    std::string parameter_path;
    std::vector<double> values;
};

// Sidecar reference: which solver-output field lives at which file.
struct StoredField {
    std::string node_id;         // which PhysicsNode produced it
    std::string output_name;     // which output of that node
    int         sweep_index = 0; // 0 for studies without sweeps
    std::string path;            // relative to the study file
};

struct Study {
    int                       version = 1;
    std::string               name    = "Untitled study";
    std::vector<PhysicsNode>  nodes;
    std::vector<CouplingSpec> couplings;
    // Topological order in which the orchestrator runs the nodes.
    // Empty means "evaluate in declaration order".
    std::vector<std::string>  solve_order;
    std::vector<SweepSpec>    sweeps;
    std::vector<StoredField>  result_files;
};

}  // namespace mpkit
