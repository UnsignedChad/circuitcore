// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Conductor surface roughness multiplier.
//
// At GHz frequencies the skin depth in copper drops to the same order
// as the surface roughness left by oxide treatments, the
// PCB-laminate-to-copper bond. The current can no longer flow as if
// the surface were smooth; effective resistance climbs.
//
// Hammerstad-Jensen (1980) closed form:
//
//   K_HJ = 1 + (2/pi) * atan( 1.4 * (R_q / delta_s)^2 )
//
// where R_q is the RMS surface roughness (m) and delta_s is the
// skin depth at the frequency of interest:
//
//   delta_s = sqrt( 2 / (omega * mu * sigma) )
//
// K_HJ -> 1 at low frequency (skin depth wide compared to roughness).
// K_HJ -> 1 + 2/pi*atan(infty) = 2 at high frequency: roughness can
// at most double the effective sheet resistance.
//
// Typical values:
//   * Smooth ED foil:           R_q ~ 0.4 um
//   * Standard rolled copper:   R_q ~ 1.0 um
//   * "Reverse-treat" foil:     R_q ~ 0.5 um
//   * Black-oxide-treated:      R_q ~ 2.0-5.0 um (deliberately roughened)

#pragma once

namespace pdnkit::pi {

// Skin depth in copper at angular frequency omega (rad/s).
// Defaults: sigma_Cu = 5.96e7 S/m, mu = mu_0 (non-magnetic Cu).
double skin_depth_copper(double omega);

// Hammerstad-Jensen multiplier. r_q_m is RMS roughness in meters,
// f_hz the frequency. Returns 1.0 for f <= 0 or r_q_m <= 0.
double hj_roughness_multiplier(double r_q_m, double f_hz);

}  // namespace pdnkit::pi
