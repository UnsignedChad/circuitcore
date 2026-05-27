// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/FdtdPort.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "si/Fft.h"

namespace sikit::fdtd {

double gaussian_modulated_sinusoid(double t, double t0, double spread,
                                    double fc) {
    const double x = (t - t0) / spread;
    return std::exp(-x * x) * std::sin(2.0 * std::numbers::pi * fc * (t - t0));
}

std::vector<double> make_gms_drive(int n_steps, double dt,
                                     double t0, double spread, double fc) {
    std::vector<double> v(n_steps);
    for (int n = 0; n < n_steps; ++n) {
        v[n] = gaussian_modulated_sinusoid(n * dt, t0, spread, fc);
    }
    return v;
}

namespace {

// Linear interpolation of an FFT-bin function at an arbitrary
// frequency. FFT bins span [0, fs/2] for the positive half; we
// look up the two neighbours and interpolate.
std::complex<double> interp_bin(
    const std::vector<std::complex<double>>& X,
    double freq_hz, double dt) {
    const std::size_t N = X.size();
    const double df = 1.0 / (N * dt);
    const double bin_f = freq_hz / df;
    const std::size_t b_lo = static_cast<std::size_t>(std::floor(bin_f));
    const std::size_t b_hi = std::min(N - 1, b_lo + 1);
    if (b_lo >= N) return {0.0, 0.0};
    const double frac = bin_f - b_lo;
    return X[b_lo] * (1.0 - frac) + X[b_hi] * frac;
}

}  // namespace

std::vector<std::complex<double>>
extract_s11_from_histories(const std::vector<double>& v_inc,
                              const std::vector<double>& v_total,
                              double dt,
                              const std::vector<double>& freqs) {
    if (v_inc.size() != v_total.size()) {
        throw std::invalid_argument(
            "extract_s11: v_inc and v_total length mismatch");
    }
    // Reflected = total - incident.
    std::vector<double> v_ref(v_inc.size());
    for (std::size_t n = 0; n < v_inc.size(); ++n) {
        v_ref[n] = v_total[n] - v_inc[n];
    }
    const std::size_t N = sikit::dsp::next_power_of_2(v_inc.size());
    std::vector<std::complex<double>> X_inc(N, {0.0, 0.0});
    std::vector<std::complex<double>> X_ref(N, {0.0, 0.0});
    for (std::size_t n = 0; n < v_inc.size(); ++n) {
        X_inc[n] = v_inc[n];
        X_ref[n] = v_ref[n];
    }
    sikit::dsp::fft(X_inc);
    sikit::dsp::fft(X_ref);

    std::vector<std::complex<double>> s11;
    s11.reserve(freqs.size());
    for (const double f : freqs) {
        const auto a = interp_bin(X_inc, f, dt);
        const auto b = interp_bin(X_ref, f, dt);
        if (std::abs(a) < 1e-20) {
            s11.emplace_back(0.0, 0.0);
        } else {
            s11.push_back(b / a);
        }
    }
    return s11;
}

std::vector<double> run_with_port(FDTD3D& s, const LumpedPort& p) {
    FDTD3D::SoftESource src;
    src.i = p.i; src.j = p.j; src.k = p.k;
    switch (p.comp) {
        case SoftESource::Comp::Ex:
            src.comp = FDTD3D::SoftESource::Comp::Ex; break;
        case SoftESource::Comp::Ey:
            src.comp = FDTD3D::SoftESource::Comp::Ey; break;
        case SoftESource::Comp::Ez:
            src.comp = FDTD3D::SoftESource::Comp::Ez; break;
    }
    src.samples = p.drive;
    s.add_soft_e_source(src);

    std::vector<double> v(p.drive.size(), 0.0);
    for (std::size_t n = 0; n < p.drive.size(); ++n) {
        s.step();
        switch (p.comp) {
            case SoftESource::Comp::Ex: v[n] = s.ex(p.i, p.j, p.k); break;
            case SoftESource::Comp::Ey: v[n] = s.ey(p.i, p.j, p.k); break;
            case SoftESource::Comp::Ez: v[n] = s.ez(p.i, p.j, p.k); break;
        }
    }
    return v;
}

}  // namespace sikit::fdtd
