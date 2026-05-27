// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Lumped electrical model of a PCB via.
//
// At GHz speeds a single through-hole via is no longer a wire -- the barrel
// adds series inductance, each end-pad couples capacitively to the
// surrounding reference plane through the antipad ring, and the unused
// portion of the barrel (the "stub" -- from the connected signal layer
// down to the bottom pad on a through-hole) creates a quarter-wave
// resonance that wipes out a band of the signal spectrum.
//
// We model the via as a 2-port network at each frequency:
//
//   port 1 -- shunt C_top -- series L_barrel -- shunt C_bot -- port 2
//                                |
//                                +-- open-circuited TL of length = stub
//
// The closed-form parameters (Johnson, "High-Speed Digital Design" 1993,
// ch. 5; Howard Johnson, "High-Speed Signal Propagation" 2003, ch. 7):
//
//   L_barrel  =  (mu_0 / 2*pi) * h * ln(D_antipad / d_drill)        [H]
//   C_pad     =  eps_0 * eps_r * pi * (d_pad/2)^2 / h_diel          [F]
//
// where h is the total via length (top to bottom pad), h_diel is the
// dielectric thickness between pad and adjacent reference plane.
//
// For thru-hole vias with a stub (signal lands on an inner layer, barrel
// continues down to the bottom), the stub is modeled as an open-circuited
// transmission line of length L_stub appended in shunt at the inner-layer
// node. The stub's TL impedance Z_stub is the coaxial-line formula:
//
//   Z_stub  =  (1 / (2*pi)) * sqrt(mu / eps) * ln(D_antipad / d_drill)
//
// At a stub-resonance frequency f_res = c0 / (4 * L_stub * sqrt(eps_r))
// the via becomes a notch filter — every commercial high-speed design
// either backdrills the stub away or routes on the bottom layer to avoid
// this.

#pragma once

#include <vector>

#include "si/Touchstone.h"

namespace sikit::analysis {

struct ViaSpec {
    // Geometry. All meters.
    double drill_diameter = 0.0;     // d_drill -- the hole
    double pad_diameter   = 0.0;     // d_pad   -- top/bottom annular pad
    double antipad_diameter = 0.0;   // D_antipad -- plane clearance ring
    double total_length   = 0.0;     // h -- top pad to bottom pad (through-hole = board thickness)
    double pad_to_plane_h = 0.0;     // h_diel -- dielectric distance pad ↔ closest reference plane

    // Stub: portion of the barrel BELOW the signal connection layer that
    // is electrically connected but not used. Set to 0 for stubless
    // (back-drilled or signal-on-bottom-layer) vias.
    double stub_length = 0.0;

    // Material.
    double epsilon_r   = 4.3;        // dielectric around the barrel
    double tan_delta   = 0.0;        // dielectric loss tangent (used for stub)
};

// Generate a 2-port Touchstone for the via on the given frequency grid.
// Throws std::runtime_error on degenerate geometry (drill <= 0,
// antipad <= drill, length <= 0, etc.).
sikit::touchstone::TouchstoneFile compute_via_s2p(
    const ViaSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance = 50.0);

// Convenience: extract lumped parameters from a spec without running the
// full per-frequency loop. Useful for the UI to show "L=0.5nH C=0.3pF".
struct ViaLumped {
    double L_barrel = 0.0;   // henries
    double C_pad    = 0.0;   // farads, per end
    double Z_stub   = 0.0;   // ohms (coax-line impedance of the stub region)
    double stub_resonance_hz = 0.0;  // c/(4*L*sqrt(er))
};

ViaLumped via_lumped(const ViaSpec& spec);

}  // namespace sikit::analysis
