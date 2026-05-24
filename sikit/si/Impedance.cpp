#include "si/Impedance.h"

#include <cmath>
#include <format>
#include <numbers>

namespace sikit::impedance {

namespace {

void require_positive(const char* name, double v) {
    if (!(v > 0.0)) {
        throw ImpedanceError(std::format("{} must be > 0 (got {})", name, v));
    }
}

void require_at_least(const char* name, double v, double minv) {
    if (!(v >= minv)) {
        throw ImpedanceError(
            std::format("{} must be >= {} (got {})", name, minv, v));
    }
}

}  // namespace

double microstrip_z0(const MicrostripParams& p) {
    require_positive("W (trace_width)", p.trace_width);
    require_positive("H (dielectric_height)", p.dielectric_height);
    require_at_least("T (trace_thickness)", p.trace_thickness, 0.0);
    require_at_least("εr (epsilon_r)", p.epsilon_r, 1.0);

    const double prefactor = 87.0 / std::sqrt(p.epsilon_r + 1.41);
    const double inside = 5.98 * p.dielectric_height /
                          (0.8 * p.trace_width + p.trace_thickness);
    return prefactor * std::log(inside);
}

double stripline_z0(const StriplineParams& p) {
    require_positive("W (trace_width)", p.trace_width);
    require_positive("B (plane_separation)", p.plane_separation);
    require_at_least("T (trace_thickness)", p.trace_thickness, 0.0);
    require_at_least("εr (epsilon_r)", p.epsilon_r, 1.0);

    const double prefactor = 60.0 / std::sqrt(p.epsilon_r);
    const double inside = 4.0 * p.plane_separation /
                          (0.67 * std::numbers::pi *
                           (0.8 * p.trace_width + p.trace_thickness));
    return prefactor * std::log(inside);
}

double edge_coupled_microstrip_diff(double single_z0,
                                    double spacing,
                                    double dielectric_height) {
    require_positive("single_z0", single_z0);
    require_at_least("spacing", spacing, 0.0);
    require_positive("dielectric_height", dielectric_height);
    return 2.0 * single_z0 *
           (1.0 - 0.48 * std::exp(-0.96 * spacing / dielectric_height));
}

double edge_coupled_stripline_diff(double single_z0,
                                   double spacing,
                                   double plane_separation) {
    require_positive("single_z0", single_z0);
    require_at_least("spacing", spacing, 0.0);
    require_positive("plane_separation", plane_separation);
    return 2.0 * single_z0 *
           (1.0 - 0.347 * std::exp(-2.9 * spacing / plane_separation));
}

bool microstrip_in_valid_range(const MicrostripParams& p) {
    if (p.dielectric_height <= 0.0) return false;
    const double wh = p.trace_width / p.dielectric_height;
    return wh > 0.1 && wh < 2.0 && p.epsilon_r > 1.0 && p.epsilon_r < 15.0;
}

bool stripline_in_valid_range(const StriplineParams& p) {
    if (p.plane_separation <= 0.0) return false;
    const double w_over_bt = p.trace_width /
                             (p.plane_separation - p.trace_thickness);
    const double t_over_b = p.trace_thickness / p.plane_separation;
    return w_over_bt < 0.35 && t_over_b < 0.25 && p.epsilon_r > 1.0;
}

}  // namespace sikit::impedance
