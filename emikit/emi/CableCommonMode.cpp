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

    const double L = cable.length_m;
    const double I = std::abs(cable.cm_current_a);
    const double r = distance_m;

    // Short electric dipole over a ground plane (Hockanson eq 1):
    //     E = (eta0 / c) * I * L * f / r
    // Accurate within a factor of ~2 for L < lambda/2. Beyond that the
    // formula overestimates (real cables develop nodes in the current
    // distribution that the short-dipole approximation does not capture)
    // -- for pre-compliance work overestimation is the safe side.
    return (kEta0 / kC) * I * L * freq_hz / r;
}

double cable_cm_e_field_dbuv(const CableSpec& cable,
                             double freq_hz,
                             double distance_m) {
    const double e = cable_cm_e_field(cable, freq_hz, distance_m);
    if (e <= 0.0) return -1000.0;
    return 20.0 * std::log10(e * 1.0e6);
}

std::vector<double> estimate_cm_current(
    const CableSpec& cable,
    const std::vector<double>& signal_current_a_per_freq) {
    std::vector<double> i_cm(signal_current_a_per_freq.size(), 0.0);

    // Explicit override takes precedence.
    if (cable.cm_current_a > 0.0) {
        for (auto& v : i_cm) v = cable.cm_current_a;
        return i_cm;
    }

    // Estimator needs both inductances to be set.
    if (cable.ground_inductance_h <= 0.0 ||
        cable.cable_cm_inductance_per_m_h <= 0.0 ||
        cable.length_m <= 0.0) {
        return i_cm;
    }

    // Ratio is frequency-independent: I_cm/I_sig = 2*L_gnd / (L_cable*length).
    const double ratio = (2.0 * cable.ground_inductance_h) /
                          (cable.cable_cm_inductance_per_m_h * cable.length_m);
    for (std::size_t k = 0; k < signal_current_a_per_freq.size(); ++k) {
        i_cm[k] = ratio * std::abs(signal_current_a_per_freq[k]);
    }
    return i_cm;
}

}  // namespace emikit::emi
