// cable common-mode radiated emissions.
//
// on most digital boards the dominant emission between 30 MHz and 1 GHz
// is common-mode current pushed onto attached cables by ground bounce,
// not the differential signal loop.
//
// refs:
//   * Ott, EMC Engineering, Ch 11.6
//   * Paul, Intro to EMC 2nd ed., Ch 11.3 / Eq 11.5
//   * Hockanson et al., IEEE Trans EMC 38(4), 1996
//
// LoopEmissions covers the differential-mode loop. CableSpec carries
// either an explicit cm_current_a or a ground_inductance_h that
// estimate_cm_current() uses to derive I_cm from the signal spectrum.

#pragma once

#include <vector>

namespace emikit::emi {

struct CableSpec {
    // Physical length of the cable from the PCB connector outward.
    double length_m = 0.0;

    // -------- Explicit CM current path --------
    // If > 0, this value is used as I_cm at every frequency and the
    // estimator below is ignored.
    double cm_current_a = 0.0;

    // -------- Ground-bounce estimator path --------
    // Partial inductance of the ground return between the noisy
    // signal source and the cable shield connection point. Typical
    // values:
    //   * solid plane, short return:           0.5 - 2 nH
    //   * one via penalty:                     ~5 nH
    //   * crossing a slot in the plane:        10 - 30 nH
    //   * connector pin without dedicated GND: 20 - 50 nH
    double ground_inductance_h = 0.0;

    // Per-unit-length common-mode inductance of the cable. Defaults to
    // 1 uH/m which is representative of unshielded ribbon or zip cord;
    // shielded coax / twisted-pair runs lower (~0.3 uH/m).
    double cable_cm_inductance_per_m_h = 1.0e-6;
};

// Far-field E magnitude (V/m) from a cable carrying common-mode
// current I_cm at the broadside direction (theta = pi/2). Implements
// the standard short-electric-dipole formula extended for image-in-
// ground-plane (Hockanson 1996 eq 1, Paul Eq 11.5):
//
//     |E| = (eta0 / c) * I_cm * L * f / r
//
// Accurate within a factor of ~2 for L < lambda/2; overestimates at
// longer lengths (conservative for pre-compliance).
double cable_cm_e_field(const CableSpec& cable,
                        double freq_hz,
                        double distance_m);

// Same but expressed in dBuV/m. Returns -1000 for non-positive E.
double cable_cm_e_field_dbuv(const CableSpec& cable,
                             double freq_hz,
                             double distance_m);

// Estimate CM current spectrum from the signal current spectrum,
// using the Hockanson ground-bounce model:
//
//     I_cm(f) = (2 * L_gnd / (L_cable_per_m * cable_length)) * I_signal(f)
//
// Derivation: V_gb = j*omega*L_gnd * I_signal, and the cable's CM
// impedance is dominated by j*omega*L_cable/2 -- the frequency
// dependence cancels, leaving a flat ratio. See Hockanson 1996
// Section III for the full derivation.
//
// If cable.cm_current_a > 0 the explicit value is returned at every
// frequency and the estimator is bypassed. If neither cm_current_a nor
// ground_inductance_h is set, returns all zeros.
std::vector<double> estimate_cm_current(
    const CableSpec& cable,
    const std::vector<double>& signal_current_a_per_freq);

}  // namespace emikit::emi
