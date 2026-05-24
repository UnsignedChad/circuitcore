// Small-loop magnetic-dipole radiation in the far field.
//
// A trace driven against its reference plane forms a current loop.
// At frequencies where the loop is electrically small (loop diameter
// << wavelength, i.e. well under 1 GHz for typical PCB geometries),
// the radiation pattern is the textbook small-loop magnetic dipole:
//
//     E_max(r, f) = (eta_0 * pi * I * A * f^2) / (c0^2 * r)
//
// where I is the current, A is the loop area, r is the observation
// distance, eta_0 = 377 ohm, c0 = 2.998e8 m/s. This is the peak
// broadside value; a real trace is rarely oriented for that maximum,
// but pre-compliance always assumes worst case.
//
// We don't model the resonant transition (loop perimeter ~= lambda)
// because by that point the device is well into "hire a test house"
// territory anyway. v1 is honest about being pre-compliance.

#pragma once

#include <vector>

namespace emikit::emi {

// Far-field E-field strength in V/m. inputs in SI.
double loop_e_field(double loop_area_m2,
                     double current_a,
                     double freq_hz,
                     double distance_m);

// Same as above, returned in dBuV/m (the unit CISPR / FCC masks use).
double loop_e_field_dbuv(double loop_area_m2,
                           double current_a,
                           double freq_hz,
                           double distance_m);

// Sweep E(f) for a loop excited by the supplied per-frequency current
// spectrum (e.g. from emi::spectrum_sweep). Returns dBuV/m at each
// frequency point.
std::vector<double> loop_e_field_dbuv_sweep(
    double loop_area_m2,
    const std::vector<double>& freq_hz,
    const std::vector<double>& current_a,
    double distance_m);

}  // namespace emikit::emi
