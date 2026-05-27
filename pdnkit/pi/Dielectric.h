// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Wideband causal dielectric model (Djordjevic-Sarkar).
//
// Treating dielectric constant as a single number is fine below ~100 MHz,
// but the cavity Z(f) plot loses accuracy by 1-2 GHz where eps_r has
// already ramped down a few percent and tan(delta) is sweeping.
//
// The Djordjevic-Sarkar model is the standard causal fit:
//
//   eps(w) = eps_inf + (delta_eps / ln(w2/w1)) * ln((1 + j*w*t2) / (1 + j*w*t1))
//
// Equivalently in terms of frequency (f1 = 1/(2*pi*t1), f2 = 1/(2*pi*t2)):
//
//   eps_r'(f) = eps_inf + (delta_eps / (2 * L12)) * ln((1 + (f/f1)^2) / (1 + (f/f2)^2))
//   eps_r"(f) = (delta_eps / L12) * (atan(f/f1) - atan(f/f2))
//
//   where L12 = ln(f2/f1).
//
// Loss tangent: tan(delta) = eps_r" / eps_r'.
//
// For FR-4 a typical fit is:
//   eps_inf = 3.8, delta_eps = 1.0  (so eps at DC is ~4.8, drops to ~3.8 at GHz)
//   f1 = 1 kHz, f2 = 1 GHz
//   tan(delta) peaks around mid-band at ~0.02.

#pragma once

namespace pdnkit::pi {

struct DjordjevicSarkar {
    double eps_inf = 3.8;        // high-frequency limit
    double delta_eps = 1.0;      // dispersion magnitude
    double f1_hz = 1.0e3;        // low-frequency corner
    double f2_hz = 1.0e9;        // high-frequency corner
};

struct DielectricSample {
    double eps_r_real = 0.0;     // eps_r'(f)
    double eps_r_imag = 0.0;     // eps_r"(f), positive
    double tan_delta = 0.0;      // eps_r" / eps_r'
};

// Evaluate the model at one frequency.
DielectricSample dj_sarkar_at(const DjordjevicSarkar& m, double f_hz);

}  // namespace pdnkit::pi
