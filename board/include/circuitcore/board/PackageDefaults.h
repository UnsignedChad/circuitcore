// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Per-footprint-family default body height + mass.
//
// KiCad's .kicad_pcb does not record body height, mass, or dissipated
// power. For the multiphysics tab to give a useful read on a board the
// user just dropped on the window we need *some* extruded body per
// component, even if the user never told us the package class. This
// header is the lookup -- footprint library prefix → reasonable default.
//
// All values are SI (meters, kilograms). They are deliberately rounded
// to the typical centre of the EIA size; a 0402 chip resistor body is
// usually 0.40-0.50 mm tall, so we return 0.45 mm. Anything we cannot
// classify falls through to a generic "small SMD" default of 1.0 mm /
// 0.1 g so the body still appears in the viewer rather than being
// silently invisible.

#pragma once

#include <string_view>

namespace circuitcore::board {

// Returns a default body height in meters for the given footprint library
// id (eg "Resistor_SMD:R_0402_1005Metric"). Always returns a positive
// number; falls back to 1.0 mm for unknown footprints.
double default_body_height_m(std::string_view footprint_name) noexcept;

// Returns a default mass in kg for the same input. Used by mpkit's
// elasticity solver and any board-level mass / centre-of-gravity
// summaries. Falls back to 0.1 g.
double default_mass_kg(std::string_view footprint_name) noexcept;

}  // namespace circuitcore::board
