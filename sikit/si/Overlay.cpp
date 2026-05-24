#include "si/Overlay.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace sikit::analysis {

OverlayDelta overlay_delta(const sikit::touchstone::TouchstoneFile& primary,
                           const sikit::touchstone::TouchstoneFile& overlay,
                           int s_param_index) {
    if (primary.num_ports != overlay.num_ports) {
        throw OverlayError(std::format(
            "overlay_delta: port-count mismatch ({} vs {})",
            primary.num_ports, overlay.num_ports));
    }
    if (primary.frequencies.size() != overlay.frequencies.size()) {
        throw OverlayError(std::format(
            "overlay_delta: frequency-count mismatch ({} vs {})",
            primary.frequencies.size(), overlay.frequencies.size()));
    }
    if (s_param_index < 0 ||
        s_param_index >= primary.num_ports * primary.num_ports) {
        throw OverlayError(std::format(
            "overlay_delta: s_param_index {} out of range for {}-port",
            s_param_index, primary.num_ports));
    }
    // Allow a tiny relative drift on the freq grid -- the same 1e-9
    // tolerance the cascade / deembed checks use.
    for (std::size_t i = 0; i < primary.frequencies.size(); ++i) {
        const double ref = std::max(std::abs(primary.frequencies[i]),
                                      std::abs(overlay.frequencies[i]));
        if (std::abs(primary.frequencies[i] - overlay.frequencies[i]) >
            1e-9 * ref) {
            throw OverlayError(std::format(
                "overlay_delta: freq mismatch at point {}: {} vs {} Hz",
                i, primary.frequencies[i], overlay.frequencies[i]));
        }
    }

    OverlayDelta out;
    out.delta_db.reserve(primary.frequencies.size());
    out.max_abs_db = 0.0;
    out.max_index = 0;
    out.max_freq_hz = primary.frequencies.empty() ? 0.0
                                                   : primary.frequencies[0];

    for (std::size_t k = 0; k < primary.frequencies.size(); ++k) {
        const auto p = std::abs(primary.s_matrices[k][s_param_index]);
        const auto o = std::abs(overlay.s_matrices[k][s_param_index]);
        // Clamp tiny magnitudes so the log doesn't blow up. -120 dB is
        // below most VNA noise floors so this floor is harmless.
        const double clamp = 1e-6;
        const double db = 20.0 * std::log10(
            std::max(p, clamp) / std::max(o, clamp));
        out.delta_db.push_back(db);
        if (std::abs(db) > out.max_abs_db) {
            out.max_abs_db = std::abs(db);
            out.max_index = k;
            out.max_freq_hz = primary.frequencies[k];
        }
    }
    return out;
}

}  // namespace sikit::analysis
