#include "emi/LoopEmissions.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace emikit::emi {

namespace {
constexpr double kEta0 = 376.730313668;  // ohm, free-space impedance
constexpr double kC0   = 2.99792458e8;
}

double loop_e_field(double A, double I, double f, double r) {
    if (A <= 0.0 || f <= 0.0 || r <= 0.0) return 0.0;
    // E_max = eta_0 * pi * I * A * f^2 / (c0^2 * r)
    return kEta0 * std::numbers::pi * std::abs(I) * A * f * f
           / (kC0 * kC0 * r);
}

double loop_e_field_dbuv(double A, double I, double f, double r) {
    const double e = loop_e_field(A, I, f, r);
    if (e <= 0.0) return -1000.0;   // sentinel, effectively -inf
    // dBuV/m = 20 * log10(E [V/m] * 1e6)
    return 20.0 * std::log10(e * 1e6);
}

std::vector<double> loop_e_field_dbuv_sweep(
    double A, const std::vector<double>& freq_hz,
    const std::vector<double>& current_a, double r) {
    if (freq_hz.size() != current_a.size()) {
        throw std::runtime_error(
            "loop_e_field_dbuv_sweep: freq and current size mismatch");
    }
    std::vector<double> out;
    out.reserve(freq_hz.size());
    for (std::size_t k = 0; k < freq_hz.size(); ++k) {
        out.push_back(loop_e_field_dbuv(A, current_a[k], freq_hz[k], r));
    }
    return out;
}

}  // namespace emikit::emi
