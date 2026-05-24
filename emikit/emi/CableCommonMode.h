// Cable common-mode radiated emissions.
//
// On most real digital boards the dominant source of radiated emissions
// between 30 MHz and 1 GHz is not the differential signal loop, it is
// the common-mode current driven onto attached cables (USB, Ethernet,
// power, ribbon) by the noisy ground reference. See:
//   * Henry Ott, "Electromagnetic Compatibility Engineering" Ch 11.6
//   * Clayton Paul, "Introduction to EMC" 2nd ed. Ch 11.3 / Eq 11.5
//   * Hockanson, Drewniak, Hubing, Van Doren et al. "Investigation of
//     fundamental EMI source mechanisms driving common-mode radiation
//     from printed circuit boards with attached cables." IEEE TEMC 1996.
//
// LoopEmissions models differential-mode radiation from the trace+return
// loop. This file adds the common-mode-on-cable contribution. The two
// are independent mechanisms; the chamber sees both summed in power.
//
// V1 takes the CM current as an explicit user input. Estimating it from
// scratch requires the ground-plane impedance at frequency and the
// cable's common-mode termination -- both highly board-specific. A
// future revision can add a heuristic estimator driven by the per-net
// signal current the analyzer already computes.

#pragma once

namespace emikit::emi {

struct CableSpec {
    // Physical length of the cable from the PCB connector outward.
    // Used both for the electrical-length transition and for the
    // short-dipole length factor.
    double length_m = 0.0;

    // Common-mode current on the cable, A. Magnitude only -- this
    // model takes peak-envelope CM current and assumes it is flat
    // across frequency. Per-frequency variation should be supplied
    // through the cm_current_per_freq overload below.
    double cm_current_a = 0.0;
};

// Far-field E magnitude (V/m) from a cable carrying common-mode
// current I_cm at the broadside direction (theta = pi/2). Implements
// the standard short-electric-dipole formula extended for image-in-
// ground-plane:
//
//     |E| = (eta0 / c) * I_cm * L * f / r           for L <= lambda / 4
//
// For longer cables the short-dipole formula overestimates; we cap
// at the half-wave-monopole value (E = 60 * I_cm / r) once the
// cable goes resonant. This is consistent with the Hockanson 1996
// fit used throughout the EMC literature.
double cable_cm_e_field(const CableSpec& cable,
                        double freq_hz,
                        double distance_m);

// Same but expressed in dBuV/m. Returns -1000 for non-positive E.
double cable_cm_e_field_dbuv(const CableSpec& cable,
                             double freq_hz,
                             double distance_m);

}  // namespace emikit::emi
