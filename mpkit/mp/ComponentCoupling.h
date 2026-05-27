// Per-component metadata -> mpkit physics sources.
//
// Two things this module does:
//
//   1. Fill in defaulted metadata. The parser leaves
//      Component::body_height_m and Component::mass_kg at 0 because it
//      cannot know them from a .kicad_pcb alone. apply_default_metadata
//      walks board.components and, for every zero field, fills in the
//      corresponding PackageDefaults lookup so downstream solvers and
//      summaries see concrete numbers.
//
//   2. Build a volumetric heat-source field from each component's
//      dissipated_power_w. Each component's power is spread evenly over
//      the voxels its courtyard + body_height extent covers (W/m^3 =
//      P / V_box). Mirrors mpkit::ir_solution_to_joule_source so the
//      same Field3D type can be handed to SteadyHeatConfig::
//      volumetric_source / TransientHeatConfig::volumetric_source.
//
// Optional third bit (compute_component_summary): a small dashboard
// helper for the Mp tab that returns total mass + total dissipated
// power so the user can sanity-check their inputs against a BOM.

#pragma once

#include <cstddef>
#include <string>

#include "circuitcore/board/Board.h"
#include "mp/JouleCoupling.h"
#include "mp/Voxelizer.h"

namespace mpkit {

// Fill in zero-valued body_height_m / mass_kg fields on each Component
// from the per-package-family defaults table. Idempotent: only touches
// fields that are still at their default of 0.0. Returns the number of
// components that received at least one fill.
int apply_default_metadata(circuitcore::board::Board& board) noexcept;

// Build a Joule-source field from per-component dissipated_power_w.
// Component without a courtyard fall back to a small pad-derived bbox
// inflated by 0.2 mm so isolated SMD parts still receive a stamp.
// Components with dissipated_power_w == 0 are skipped.
//
// Returns the same JouleSourceField shape as ir_solution_to_joule_source
// so the two can be added voxel-wise if the caller wants both Joule
// heat (from IR drop) and component dissipation (from spec sheets) in
// one heat solve.
JouleSourceField component_power_to_joule_source(
    const circuitcore::board::Board& board,
    const VoxelMaterialField& material_field);

// ---- summary ----------------------------------------------------------

struct ComponentSummary {
    int    n_components       = 0;
    int    n_top_side         = 0;
    int    n_bottom_side      = 0;
    double total_mass_kg      = 0.0;
    double total_power_w      = 0.0;
    // Reference designator of the single most-dissipating component, or
    // empty if no component has dissipated_power_w > 0.
    std::string hottest_reference;
    double hottest_power_w    = 0.0;
};

ComponentSummary compute_component_summary(
    const circuitcore::board::Board& board) noexcept;

}  // namespace mpkit
