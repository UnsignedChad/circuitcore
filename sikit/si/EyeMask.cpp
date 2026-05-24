#include "si/EyeMask.h"

#include <algorithm>
#include <cstddef>

namespace sikit::specs {

const EyeMask& generic_centered_opening() {
    static const EyeMask kMask{
        "Generic centered opening",
        // Hexagonal "keep-out" region in the center of the eye:
        // requires the trace to stay outside |v| < 0.4 over t ∈ [0.3, 0.7].
        {{0.3,  -0.4},
         {0.4,  -0.4},
         {0.5,   0.0},
         {0.4,   0.4},
         {0.3,   0.4},
         {0.5,   0.0}},  // shaped like a stretched diamond
        "sikit v0 generic test (not a real spec)",
    };
    return kMask;
}

const EyeMask& usb20_hs_template1() {
    // Approximation of USB 2.0 high-speed transmit eye Template 1.
    // Real spec is hexagonal in (UI, normalized V) space with vertices at
    // roughly (0.275, ±0), (0.5, ±0.4), (0.725, ±0). This is a rectilinear
    // simplification that gives the same qualitative pass/fail.
    static const EyeMask kMask{
        "USB 2.0 HS Template 1 (approx)",
        // Diamond shape: forbidden region around the eye center.
        {{0.275,  0.0},
         {0.5,    0.4},
         {0.725,  0.0},
         {0.5,   -0.4}},
        "USB 2.0 spec, rectilinear approximation",
    };
    return kMask;
}

std::vector<std::string> available_mask_names() {
    return {
        std::string(generic_centered_opening().name),
        std::string(usb20_hs_template1().name),
    };
}

const EyeMask* mask_by_name(std::string_view name) {
    if (name == generic_centered_opening().name) return &generic_centered_opening();
    if (name == usb20_hs_template1().name)       return &usb20_hs_template1();
    return nullptr;
}

bool point_in_polygon(double t, double v,
                      const std::vector<std::pair<double, double>>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& [xi, yi] = poly[i];
        const auto& [xj, yj] = poly[j];
        // Edge from (xj,yj) → (xi,yi). Standard half-line crossing test.
        const bool intersects =
            ((yi > v) != (yj > v)) &&
            (t < (xj - xi) * (v - yi) / (yj - yi) + xi);
        if (intersects) inside = !inside;
    }
    return inside;
}

int count_violations(const eye::EyeGrid& g, const EyeMask& mask) {
    if (g.time_bins <= 0 || g.volt_bins <= 0) return 0;
    if (g.v_max <= g.v_min) return 0;

    const double v_span  = g.v_max - g.v_min;
    const double v_mid   = 0.5 * (g.v_min + g.v_max);
    const double v_half  = 0.5 * v_span;

    int violating = 0;
    for (int v = 0; v < g.volt_bins; ++v) {
        // Center voltage of this bin in absolute units…
        const double v_abs = g.v_min + (v + 0.5) / g.volt_bins * v_span;
        // …then normalize to [-1, 1] around the center.
        const double v_norm = (v_abs - v_mid) / v_half;
        for (int t = 0; t < g.time_bins; ++t) {
            if (g.at(t, v) == 0) continue;
            const double t_norm = (t + 0.5) / g.time_bins;
            if (point_in_polygon(t_norm, v_norm, mask.polygon)) {
                ++violating;
            }
        }
    }
    return violating;
}

}  // namespace sikit::specs
