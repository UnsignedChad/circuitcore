// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "emi/CableCommonMode.h"

#include <cmath>

namespace emikit::emi {

namespace {
constexpr double kEta0 = 376.730313668;       // ohm, vacuum impedance
constexpr double kC    = 2.99792458e8;        // m/s
}  // namespace

double cable_cm_e_field(const CableSpec& cable,
                        double freq_hz,
                        double distance_m) {
    if (cable.length_m <= 0.0 ||
        cable.cm_current_a == 0.0 ||
        freq_hz <= 0.0 ||
        distance_m <= 0.0) {
        return 0.0;
    }

    const double wavelength = kC / freq_hz;
    const double L          = cable.length_m;
    const double I          = std::abs(cable.cm_current_a);
    const double r          = distance_m;

    // Short electric dipole over a ground plane (Hockanson eq 1):
    //     E = (eta0 / c) * I * L * f / r
    // Accurate within a factor of ~2 for L < lambda/2. Beyond that the
    // formula overestimates (real cables develop nodes in the current
    // distribution that the short-dipole approximation does not capture)
    // -- for pre-compliance work overestimation is the safe side.
    (void)wavelength;
    return (kEta0 / kC) * I * L * freq_hz / r;
}

double cable_cm_e_field_dbuv(const CableSpec& cable,
                             double freq_hz,
                             double distance_m) {
    const double e = cable_cm_e_field(cable, freq_hz, distance_m);
    if (e <= 0.0) return -1000.0;
    return 20.0 * std::log10(e * 1.0e6);
}

}  // namespace emikit::emi
