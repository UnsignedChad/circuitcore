// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Thermoelectric effect calculators (Seebeck, Peltier, Thomson).
//
// These are post-processors on top of fields the existing thermal +
// electric solvers already produce. They do NOT couple back into the
// solvers; the full coupled electric-thermal solve (where current
// density depends on temperature gradient through Ohm + Seebeck, and
// the heat equation gains Peltier + Thomson source terms) lands later
// as a Thermoelectric PhysicsKind that calls these primitives.
//
// The three effects, briefly:
//
//   * Seebeck:   a temperature gradient across a conductor produces an
//                EMF: dV/dx = -S * dT/dx, where S is the per-material
//                Seebeck coefficient in V/K. For a homogeneous wire
//                between two temperatures the EMF is S * (T_hi - T_lo).
//                For a thermocouple (two metals joined at the hot end,
//                cold ends both at T_cold) the open-circuit voltage at
//                the cold terminals is (S_a - S_b) * (T_hot - T_cold).
//
//   * Peltier:   when current crosses a junction between two materials,
//                heat is absorbed or released at a rate
//                q_face = (Pi_a - Pi_b) * J_n  [W/m^2 of junction area]
//                where Pi = S * T is the Peltier coefficient (W/A) and
//                J_n is the current density component normal to the
//                junction.
//
//   * Thomson:   distributed volumetric heat that appears when current
//                flows along a temperature gradient in a single
//                material whose S varies with T:
//                q_vol = -tau * J . grad(T)  [W/m^3]
//                where tau = T * dS/dT is the Thomson coefficient. For
//                v1 we treat S as temperature-independent (dS/dT = 0)
//                so the Thomson contribution is exactly zero everywhere
//                that materials are uniform; the API surface stays for
//                forward-compat once temperature-dependent S lands.
//
// For PCB design the magnitudes are usually small (copper Seebeck is
// about +1.83 uV/K, so a 50 K/cm gradient produces a 9 uV/cm field --
// 90 mV/m), but at bimetallic junctions in high-current connectors the
// Peltier flux is real and can shift local hot-spots a few degrees.
// The thermocouple use case is the most familiar -- our test for the
// chromel/alumel pair recovers the textbook Type-K sensitivity.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/Grid.h"
#include "mp/Material.h"
#include "mp/Voxelizer.h"

namespace mpkit::thermo {

// Peltier coefficient Pi = S * T at the given temperature (kelvin).
// Returns 0 if the material has no Seebeck coefficient set.
double peltier_coefficient(const Material& m, double t_kelvin);

// Open-circuit Seebeck EMF accumulated along a 1-D path.
//
//   path_materials[i] -- the material between path nodes i and i+1
//   temperatures[i]   -- temperature at node i, in the same unit on both
//                        ends (kelvin or Celsius -- Seebeck only cares
//                        about differences). length = N+1 for a path of
//                        N segments.
//
// Returns the cumulative EMF in volts, taking the sign convention that
// positive S contributes positive voltage when current would flow from
// hot to cold.
double seebeck_emf_1d(const std::vector<const Material*>& path_materials,
                       const std::vector<double>& temperatures);

// Convenience for the canonical thermocouple geometry: two wires of
// uniform material joined at the hot junction, cold ends both at
// t_cold. Returns (S_a - S_b) * (T_hot - T_cold) in volts.
double thermocouple_emf(const Material& wire_a,
                         const Material& wire_b,
                         double t_hot_kelvin,
                         double t_cold_kelvin);

// Peltier heat-flux density at a single material-A / material-B junction
// when the current density component NORMAL to the junction is j_normal
// (A/m^2). Positive return = heat absorbed (cools the junction) when
// current flows from a to b through positive (Pi_a - Pi_b).
//
//   q_face = (Pi_a - Pi_b) * j_normal   [W/m^2]
double peltier_junction_flux(const Material& a,
                              const Material& b,
                              double t_kelvin,
                              double j_normal);

// Volumetric Thomson heat source field on the same grid as the
// supplied temperature field. Until temperature-dependent Seebeck is
// added, every voxel returns 0 -- the API is here so coupled solvers
// can call it uniformly without #ifdef-ing thermoelectric out.
//
// To populate non-trivial values later, add a dS_dT(T) member to
// Material and finite-difference the gradient of T against the
// supplied current_density components.
circuitcore::field::Field3D thomson_volumetric_heat(
    const VoxelMaterialField&            material_field,
    const std::vector<Material>&         material_table,
    const circuitcore::field::Field3D&   temperature,
    const circuitcore::field::Field3D&   current_density_x,
    const circuitcore::field::Field3D&   current_density_y,
    const circuitcore::field::Field3D&   current_density_z);

}  // namespace mpkit::thermo
