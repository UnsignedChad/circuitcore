// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Partial inductance of PCB vias.
//
// Above ~100 MHz the via barrel's series inductance dominates the path
// from one copper layer to another. For a typical 0.3 mm diameter via
// through a 1.6 mm board this is ~700 pH self / ~300 pH loop with a
// nearby return via -- enough to matter for any rail with a switching
// load past a few hundred MHz.
//
// Formulas: Grover, "Inductance Calculations" (Dover reissue 2004),
// chapters 2-3. Same closed forms used in Ruehli's "Inductance
// Calculations in a Complex Integrated Circuit Environment" (IBM
// J. R&D 16(5), 1972), which is where the PEEC method came from.
//
// Self-inductance of a cylindrical conductor (h >> r):
//   L_self = (mu0 * h / 2pi) * [ ln(2h/r) - 1 ]
//
// Mutual inductance between two parallel cylindrical conductors of
// length h, separated by center-to-center distance d:
//   M = (mu0 * h / 2pi) * [ ln(h/d + sqrt(1 + (h/d)^2))
//                            - sqrt(1 + (d/h)^2) + d/h ]
//
// Loop inductance of a signal-and-return via pair (currents
// anti-correlated):
//   L_loop = L_self - M
//
// Validity: the closed forms above assume the conductor is long
// compared to its radius (h/r > 10 keeps error under 1%). For very
// short stubs (h/r ~ 1) use a full PEEC solver instead.

#pragma once

namespace pdnkit::pi {

// Self partial inductance of one cylindrical via barrel. Returns
// inductance in henries. radius_m and length_m in meters.
double via_self_inductance(double radius_m, double length_m);

// Mutual partial inductance between two parallel cylindrical barrels
// of the same length and radius, with center-to-center spacing d.
double via_mutual_inductance(double radius_m, double length_m,
                              double spacing_m);

// Loop inductance of a signal-via + return-via pair (currents flow in
// opposite directions). Equals self - mutual.
double via_pair_loop_inductance(double radius_m, double length_m,
                                 double spacing_m);

}  // namespace pdnkit::pi
