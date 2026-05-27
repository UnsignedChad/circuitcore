#include "si/SurfaceRoughness.h"
#include "circuitcore/field/GridSpec.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sikit::analysis {

namespace {

using circuitcore::field::kMu0;

}  // namespace

double skin_depth(double frequency_hz, double sigma_copper) {
    if (frequency_hz <= 0.0 || sigma_copper <= 0.0) return 0.0;
    return std::sqrt(1.0 / (std::numbers::pi * frequency_hz * kMu0 * sigma_copper));
}

namespace {

double hammerstad_jensen(double Delta, double delta) {
    if (delta <= 0.0 || Delta <= 0.0) return 1.0;
    const double ratio = Delta / delta;
    return 1.0 + (2.0 / std::numbers::pi) *
                  std::atan(1.4 * ratio * ratio);
}

double huray(const RoughnessSpec& spec, double delta) {
    if (delta <= 0.0 || spec.sphere_radius <= 0.0 ||
        spec.sphere_density <= 0.0) {
        return 1.0;
    }
    // K_Huray = A_flat / A_matte + (sum over sphere ensembles of
    //           (N * 4*pi*a^2) * (3/2) * 1 / (1 + delta/a + delta^2/(2 a^2)))
    // For a single sphere distribution this collapses to:
    const double a = spec.sphere_radius;
    const double N = spec.sphere_density;
    const double ratio = delta / a;
    const double inv_term = 1.0 + ratio + 0.5 * ratio * ratio;
    const double K_sphere = (3.0 / 2.0) * (N * 4.0 * std::numbers::pi * a * a)
                            / inv_term;
    const double K_flat = std::clamp(spec.flat_coverage, 0.0, 1.0);
    // K_total = K_flat + K_sphere (Huray formulation). Always >= 1.
    return std::max(1.0, K_flat + K_sphere);
}

}  // namespace

double roughness_factor(const RoughnessSpec& spec, double frequency_hz,
                         double sigma_copper) {
    if (spec.model == RoughnessModel::None) return 1.0;
    const double delta = skin_depth(frequency_hz, sigma_copper);
    if (delta <= 0.0) return 1.0;

    switch (spec.model) {
        case RoughnessModel::HammerstadJensen:
            return hammerstad_jensen(spec.rms_height, delta);
        case RoughnessModel::Huray:
            return huray(spec, delta);
        case RoughnessModel::None:
        default:
            return 1.0;
    }
}

}  // namespace sikit::analysis
