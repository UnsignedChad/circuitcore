// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/ChannelSynthesis.h"
#include "circuitcore/field/GridSpec.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace sikit::analysis {

using Complex = std::complex<double>;

namespace {

constexpr double kC0  = 2.99792458e8;
using circuitcore::field::kMu0;

// Effective permittivity for microstrip via Hammerstad. Used when we
// need to re-derive eps_eff at each frequency with a freshly dispersed
// εr from the DS model.
double microstrip_eps_eff(double w, double h, double eps_r) {
    if (h <= 0.0 || w <= 0.0) return eps_r;
    const double wh = w / h;
    return 0.5 * (eps_r + 1.0) +
           0.5 * (eps_r - 1.0) / std::sqrt(1.0 + 12.0 / wh);
}

double per_freq_alpha(double f, double trace_width, double Z0, double eps_eff,
                      double tan_delta, double sigma_copper,
                      const RoughnessSpec& roughness) {
    if (trace_width <= 0.0 || Z0 <= 0.0) return 0.0;
    const double Rs_smooth = std::sqrt(std::numbers::pi * f * kMu0 / sigma_copper);
    const double K = roughness_factor(roughness, f, sigma_copper);
    const double Rs = Rs_smooth * K;
    const double R_per_m = Rs / trace_width;
    const double alpha_c = R_per_m / (2.0 * Z0);
    const double alpha_d = std::numbers::pi * f * std::sqrt(eps_eff) /
                           kC0 * tan_delta;
    return alpha_c + alpha_d;
}

bool is_outer_copper(int ord) { return ord == 0 || ord == 31; }

}  // namespace

sikit::touchstone::TouchstoneFile synthesize_channel(
    const ChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance) {

    const SegmentImpedance imp =
        (spec.engine == Engine::Fdm)
            ? compute_one_fdm(spec.trace_width, spec.layer_ordinal, spec.stackup)
            : compute_one(spec.trace_width, spec.layer_ordinal, spec.stackup);
    if (imp.z0 <= 0.0 || imp.v_phase <= 0.0) {
        throw std::runtime_error(
            "synthesize_channel: impedance engine returned invalid Z₀ or v_phase");
    }

    const double Z0_dc = imp.z0;
    const double Zr    = reference_impedance;
    const double l     = spec.length_m;
    const double two_pi = 2.0 * std::numbers::pi;
    const bool dispersive = spec.dispersion_model.has_value();

    sikit::touchstone::TouchstoneFile out;
    out.num_ports = 2;
    out.format = sikit::touchstone::Format::RealImaginary;
    out.reference_impedance = Zr;
    out.frequency_scale = 1.0;
    out.frequencies = freq_hz;
    out.s_matrices.reserve(freq_hz.size());

    for (double f : freq_hz) {
        // Pick eps_eff(f) and tan_δ(f) per the dispersion model if set,
        // otherwise fall back to the constant-εr stackup data.
        double eps_eff = imp.eps_eff;
        double tan_d   = spec.stackup.tan_delta;
        if (dispersive) {
            const double eps_r_f = spec.dispersion_model->epsilon_r(f);
            tan_d = spec.dispersion_model->tan_delta(f);
            if (is_outer_copper(spec.layer_ordinal)) {
                eps_eff = microstrip_eps_eff(
                    spec.trace_width, spec.stackup.outer_dielectric_height,
                    eps_r_f);
            } else {
                eps_eff = eps_r_f;  // stripline → fully embedded
            }
        }
        const double v_phase = kC0 / std::sqrt(eps_eff);

        const double alpha = per_freq_alpha(f, spec.trace_width, Z0_dc,
                                             eps_eff, tan_d,
                                             spec.stackup.sigma_copper,
                                             spec.stackup.roughness);
        const double beta = two_pi * f / v_phase;
        const Complex gamma(alpha, beta);
        const Complex gl = gamma * l;
        const Complex A = std::cosh(gl);
        const Complex B = Z0_dc * std::sinh(gl);
        const Complex C = std::sinh(gl) / Z0_dc;
        const Complex D = std::cosh(gl);

        const Complex denom = A + B / Zr + C * Zr + D;
        const Complex S11 = (A + B / Zr - C * Zr - D) / denom;
        const Complex S12 = Complex(2.0, 0) * (A * D - B * C) / denom;
        const Complex S21 = Complex(2.0, 0) / denom;
        const Complex S22 = (-A + B / Zr - C * Zr + D) / denom;

        std::vector<Complex> flat{S11, S21, S12, S22};
        out.s_matrices.push_back(std::move(flat));
    }
    return out;
}

}  // namespace sikit::analysis
