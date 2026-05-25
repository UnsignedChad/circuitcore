#include "pi/Sensitivity.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace pdnkit::pi {

namespace {
constexpr double k2pi = 2.0 * std::numbers::pi;

std::vector<double> mag_sweep(const CavityConfig& cfg,
                              double xo, double yo,
                              const std::vector<Decap>& decaps,
                              const std::vector<double>& freqs) {
    std::vector<double> mags;
    mags.reserve(freqs.size());
    if (decaps.empty()) {
        // Bare plane: cavity_impedance against the observation port at
        // both ends gives self-impedance.
        for (double f : freqs) {
            const double w = k2pi * f;
            mags.push_back(std::abs(cavity_impedance(cfg, xo, yo, xo, yo, w)));
        }
    } else {
        for (double f : freqs) {
            const double w = k2pi * f;
            mags.push_back(std::abs(
                cavity_impedance_with_decaps(cfg, xo, yo, decaps, w)));
        }
    }
    return mags;
}
}  // namespace

std::vector<SensitivitySample> sensitivity_sweep(
    const CavityConfig& cfg,
    double xo, double yo,
    const std::vector<Decap>& decaps,
    const std::vector<double>& freqs_hz) {
    std::vector<SensitivitySample> out;
    if (decaps.empty() || freqs_hz.empty()) return out;

    // Baseline.
    auto baseline = mag_sweep(cfg, xo, yo, decaps, freqs_hz);
    const double peak_base = *std::max_element(baseline.begin(),
                                                baseline.end());

    for (std::size_t k = 0; k < decaps.size(); ++k) {
        std::vector<Decap> minus_k;
        minus_k.reserve(decaps.size() - 1);
        for (std::size_t i = 0; i < decaps.size(); ++i) {
            if (i != k) minus_k.push_back(decaps[i]);
        }
        auto wo = mag_sweep(cfg, xo, yo, minus_k, freqs_hz);

        double max_rel = 0.0;
        double peak_f  = freqs_hz.front();
        for (std::size_t i = 0; i < freqs_hz.size(); ++i) {
            const double rel = (baseline[i] > 0.0)
                ? std::abs(wo[i] - baseline[i]) / baseline[i]
                : 0.0;
            if (rel > max_rel) { max_rel = rel; peak_f = freqs_hz[i]; }
        }
        const double peak_wo = *std::max_element(wo.begin(), wo.end());

        SensitivitySample s;
        s.decap_index = static_cast<int>(k);
        s.peak_z_with_caps_ohm    = peak_base;
        s.peak_z_without_cap_ohm  = peak_wo;
        s.peak_freq_hz            = peak_f;
        s.max_relative_change     = max_rel;
        out.push_back(s);
    }

    std::sort(out.begin(), out.end(),
              [](const SensitivitySample& a, const SensitivitySample& b) {
                  return a.max_relative_change > b.max_relative_change;
              });
    return out;
}

}  // namespace pdnkit::pi
