// VRM (voltage regulator module) output impedance.
//
// Treating the VRM as an ideal voltage source (Z = 0 at DC) makes the
// Z(f) plot look better than reality. Real switching regulators have
// a characteristic output impedance shape:
//
//   * At DC: R_droop, set by the regulator's load-line and any droop
//            resistance the designer added on purpose (typically a few mOhm).
//   * Below loop bandwidth (~100 kHz typical): closed-loop control keeps
//            Z flat near R_droop.
//   * Above loop bandwidth: the loop gain falls off; output impedance
//            rises as +20 dB/decade following the output inductor:
//            Z ~ j*omega*L_out (typically 1-10 uH for a buck converter).
//   * Far above: bulk output capacitance starts to dominate again, but
//            by then board PDN caps usually carry the rail.
//
// This MVP models the dominant first-two regions: Z(f) = R_droop + j*omega*L.
// Good enough to overlay on the cavity Z(f) plot and show where the VRM
// dominates vs the cap network vs the cavity itself. Loop-bandwidth and
// bulk-cap corners are future refinements.

#pragma once

#include <complex>

namespace pdnkit::pi {

struct VrmModel {
    double r_droop_ohm = 5.0e-3;     // 5 mOhm typical
    double l_out_h    = 1.0e-6;      // 1 uH typical buck output inductor
};

// Z_VRM(omega) = R_droop + j * omega * L_out.
std::complex<double> vrm_impedance(const VrmModel& m, double omega);

}  // namespace pdnkit::pi
