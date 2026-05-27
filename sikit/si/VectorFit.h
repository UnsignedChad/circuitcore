// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Vector fitting: fit a frequency-domain response H(f) to a real-pole
// rational function so it can be exported as a SPICE behavioural model.
//
// Why we need this
//
//   Sikit can compute a Touchstone S-parameter file from a board net
//   end-to-end (DiffSynth, ChannelSynthesis), but the time-domain
//   simulation that hardware engineers actually run lives in
//   LTspice/HSPICE/ngspice, driving a vendor-supplied transistor-level
//   SerDes model. Touchstone files do not load directly into a SPICE-3
//   transient run; the bridge is to fit the S-parameter to a pole-
//   residue rational function and emit a SPICE subcircuit that realises
//   the same frequency response. Once that subcircuit is in the
//   netlist, the channel is a first-class circuit element and the
//   vendor model can drive it in time domain.
//
// Algorithm: Gustavsen-Semlyen Vector Fitting (1999), restricted to
// real poles. Real-only is enough for the monotonic-loss SI channels
// what this currently ships for; complex-pole pairs (needed for stub-resonance
// notches) are deferred along with the matching extension to ViaModel
// that surfaces those features.
//
// Iterative Sanathanan-Koerner reformulation. Given samples (s_k, H_k)
// with s_k = j*2*pi*f_k and N real poles {a_n}:
//
//     sigma(s) = 1 + sum_n c_n / (s - a_n)
//     H_fit(s) = (sum_n r_n / (s - a_n) + d) / sigma(s)
//
// At each iteration we solve the linear least-squares
//
//     sigma(s_k) * H_k  ==  sum_n r_n / (s_k - a_n) + d
//
// for the 2N+1 unknowns {r_n, d, c_n}. New poles are the roots of
// sigma(s), which (Gustavsen 1999, eq. 5) equal the eigenvalues of
//
//     A - 1 * c^T
//
// where A = diag(a_n) and 1 is the all-ones column vector. We then
// flip any eigenvalue with positive real part across the imaginary
// axis to enforce stability (the "pole-flipping" step), and iterate.
//
// Reference: B. Gustavsen and A. Semlyen, "Rational approximation of
// frequency domain responses by vector fitting," IEEE Trans. Power
// Delivery 14(3):1052-1061, 1999.

#pragma once

#include <complex>
#include <stdexcept>
#include <vector>

namespace sikit::si {

struct VectorFitOptions {
    // Number of real poles. 8-16 is the typical sweet spot for SI
    // channels covering DC-30 GHz; more poles risk overfitting noise.
    int n_poles = 12;
    // Hard cap on SK iterations. Convergence usually happens in 3-6.
    int max_iter = 12;
    // Stop when ||c||_2 drops by less than this factor compared to the
    // previous iteration. Smaller = fit harder, slower.
    double convergence_tol = 1e-3;
    // Include the d (constant) direct term in the fit. Almost always
    // wanted; the response of a real channel approaches a non-zero
    // value at infinity.
    bool include_constant = true;
};

struct VectorFitResult {
    // Real poles (each a_n < 0 for stability). Size = n_poles.
    std::vector<double> poles;
    // Real residues, paired with poles. Size = n_poles.
    std::vector<double> residues;
    // Constant direct term d (the "infinity" gain). 0 if
    // include_constant was false.
    double d = 0.0;
    // Sum_k |H_fit(s_k) - H_k|^2  / sum_k |H_k|^2, after the last
    // iteration. Useful as a sanity check on whether the fit is any
    // good. < 1e-3 is very good for SI; > 1e-1 is poor.
    double rms_error = 0.0;
    int iterations = 0;
    bool converged = false;
};

struct VectorFitError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Run vector fitting on a frequency response. freq_hz and response
// must be the same length and have at least 4 * n_poles points (more
// is better; 100-500 is typical). Frequencies must be positive and
// strictly increasing.
VectorFitResult vector_fit(
    const std::vector<double>& freq_hz,
    const std::vector<std::complex<double>>& response,
    const VectorFitOptions& opts = {});

// Evaluate H_fit(j*2*pi*f) for a given frequency. Used to verify the
// fit, and by the SPICE exporter to validate the netlist before
// emitting it.
std::complex<double> evaluate_fit(const VectorFitResult& fit, double freq_hz);

}  // namespace sikit::si
