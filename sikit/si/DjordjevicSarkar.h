// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Djordjevic-Sarkar wideband Debye dispersion model.
//
// Real PCB dielectrics aren't constant. FR-4's εr drops from ~4.5 at 1 GHz
// to ~4.0 at 20 GHz; tan δ creeps up over the same span. The DS model
// gives a Kramers-Kronig-consistent (causal) εr(f) and tan_δ(f) shape
// from a single reference measurement, valid across many decades.
//
// Form:
//   ε(ω) = ε_∞ + Δε / ln(ω2/ω1) · ln((ω2 + jω) / (ω1 + jω))
//
// Two parameters (ε_∞ and Δε) plus a fixed (f1, f2) validity band. We
// solve for (ε_∞, Δε) at construction time from a single (εr, tan_δ) data
// point at frequency f0 — exactly the data sheets give you for FR-4 /
// Rogers / etc.
//
// Reference: Djordjevic, Biljic, Likar-Smiljanic, Sarkar, "Wideband
// frequency-domain characterization of FR-4 and time-domain causality"
// (2001).

#pragma once

#include <complex>

namespace sikit::dispersion {

class DjordjevicSarkar {
public:
    // Direct construction (advanced).
    DjordjevicSarkar(double eps_inf, double delta_eps,
                      double f1_hz, double f2_hz);

    // Typical use: build from a single reference data point. f1 / f2
    // bracket the dispersion's validity band; the defaults (1 kHz to
    // 1 THz) cover every realistic PCB use case.
    static DjordjevicSarkar from_reference(double eps_r,
                                            double tan_delta,
                                            double f0_hz,
                                            double f1_hz = 1.0e3,
                                            double f2_hz = 1.0e12);

    // Complex relative permittivity at the given frequency.
    //   ε(f) = ε' - j ε''     with ε'' ≥ 0 for lossy materials.
    std::complex<double> epsilon_complex(double f_hz) const;

    // Real part of ε(f).
    double epsilon_r(double f_hz) const;

    // Loss tangent at frequency f: tan_δ = ε'' / ε'.
    double tan_delta(double f_hz) const;

    double eps_inf_value()   const { return eps_inf_; }
    double delta_eps_value() const { return delta_eps_; }
    double f1_value()        const { return f1_; }
    double f2_value()        const { return f2_; }

private:
    double eps_inf_;
    double delta_eps_;
    double f1_;
    double f2_;
};

}  // namespace sikit::dispersion
