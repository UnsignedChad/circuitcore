#include "pi/ViaInductance.h"
#include "circuitcore/field/GridSpec.h"

#include <cmath>
#include <numbers>

namespace pdnkit::pi {

namespace {
using circuitcore::field::kMu0;
}

double via_self_inductance(double radius_m, double length_m) {
    if (radius_m <= 0.0 || length_m <= 0.0) return 0.0;
    const double prefactor = kMu0 * length_m / (2.0 * std::numbers::pi);
    return prefactor * (std::log(2.0 * length_m / radius_m) - 1.0);
}

double via_mutual_inductance(double radius_m, double length_m,
                              double spacing_m) {
    if (radius_m <= 0.0 || length_m <= 0.0 || spacing_m <= 0.0) return 0.0;
    const double h = length_m;
    const double d = spacing_m;
    const double h_over_d = h / d;
    const double d_over_h = d / h;
    const double prefactor = kMu0 * h / (2.0 * std::numbers::pi);
    const double bracket = std::log(h_over_d + std::sqrt(1.0 + h_over_d * h_over_d))
                         - std::sqrt(1.0 + d_over_h * d_over_h)
                         + d_over_h;
    return prefactor * bracket;
}

double via_pair_loop_inductance(double radius_m, double length_m,
                                 double spacing_m) {
    const double L = via_self_inductance(radius_m, length_m);
    const double M = via_mutual_inductance(radius_m, length_m, spacing_m);
    return L - M;
}

}  // namespace pdnkit::pi
