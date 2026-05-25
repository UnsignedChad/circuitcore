#include "emi/Spectrum.h"

#include <cmath>
#include <numbers>
#include <algorithm>

namespace emikit::emi {

namespace {

double sinc(double x) {
    // Normalized sinc(x) = sin(pi*x)/(pi*x). x=0 -> 1.
    if (std::abs(x) < 1e-12) return 1.0;
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
}

}  // namespace

double harmonic_magnitude(const TrapezoidalSpec& spec, int n) {
    if (n < 1) return 0.0;
    if (spec.period_s <= 0.0) return 0.0;
    const double tau = spec.duty_cycle * spec.period_s;
    const double tr  = spec.rise_time_s;
    const double a   = sinc(n * tau / spec.period_s);
    const double b   = sinc(n * tr  / spec.period_s);
    return 2.0 * spec.i_peak_a * (tau / spec.period_s) * std::abs(a) * std::abs(b);
}

std::vector<double> spectrum_sweep(const TrapezoidalSpec& spec,
                                    const std::vector<double>& freq_hz) {
    std::vector<double> out;
    out.reserve(freq_hz.size());
    if (spec.period_s <= 0.0) {
        out.assign(freq_hz.size(), 0.0);
        return out;
    }
    const double f_clk = 1.0 / spec.period_s;
    for (double f : freq_hz) {
        if (f <= 0.0) { out.push_back(0.0); continue; }
        const int n = std::max(1, static_cast<int>(std::round(f / f_clk)));
        out.push_back(harmonic_magnitude(spec, n));
    }
    return out;
}

EnvelopeCorners envelope_corners(const TrapezoidalSpec& spec) {
    EnvelopeCorners e;
    const double tau = spec.duty_cycle * spec.period_s;
    e.f_tau_hz = (tau > 0.0) ? 1.0 / (std::numbers::pi * tau) : 0.0;
    e.f_tr_hz  = (spec.rise_time_s > 0.0)
                  ? 1.0 / (std::numbers::pi * spec.rise_time_s) : 0.0;
    return e;
}

}  // namespace emikit::emi
