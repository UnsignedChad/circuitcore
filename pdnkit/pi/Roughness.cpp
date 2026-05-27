#include "pi/Roughness.h"
#include "circuitcore/field/GridSpec.h"

#include <cmath>
#include <numbers>

namespace pdnkit::pi {

namespace {
constexpr double kSigmaCu = 5.96e7;                 // S/m
using circuitcore::field::kMu0;
}

double skin_depth_copper(double omega) {
    if (omega <= 0.0) return 0.0;
    return std::sqrt(2.0 / (omega * kMu0 * kSigmaCu));
}

double hj_roughness_multiplier(double r_q_m, double f_hz) {
    if (r_q_m <= 0.0 || f_hz <= 0.0) return 1.0;
    const double omega = 2.0 * std::numbers::pi * f_hz;
    const double delta = skin_depth_copper(omega);
    if (delta <= 0.0) return 1.0;
    const double ratio = r_q_m / delta;
    return 1.0 + (2.0 / std::numbers::pi) *
                 std::atan(1.4 * ratio * ratio);
}

}  // namespace pdnkit::pi
