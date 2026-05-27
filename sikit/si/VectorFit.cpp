// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/VectorFit.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <utility>

namespace sikit::si {

namespace {

using Complex = std::complex<double>;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Initial real-pole guess: log-spaced over the measured frequency band,
// each pole at -2*pi*f to put it on the negative-real axis. Logarithmic
// spacing gives the iterative refinement a head start because real
// channels respond at exponentially distinct time-scales (skin-effect
// loss, dielectric loss, etc.) rather than uniformly spaced ones.
std::vector<double> initial_poles(int n_poles, double f_min, double f_max) {
    std::vector<double> poles(n_poles);
    if (n_poles == 1) {
        poles[0] = -kTwoPi * std::sqrt(f_min * f_max);
        return poles;
    }
    const double log_lo = std::log(f_min);
    const double log_hi = std::log(f_max);
    for (int n = 0; n < n_poles; ++n) {
        const double t = static_cast<double>(n) / (n_poles - 1);
        const double f = std::exp(log_lo + t * (log_hi - log_lo));
        poles[n] = -kTwoPi * f;
    }
    return poles;
}

// Build the LS system for one SK iteration.
//
// Unknown vector x has size 2*N + (1 if d else 0). Layout:
//     [r_0..r_{N-1}, (d), c_0..c_{N-1}]
// At each sample s_k = j*2*pi*f_k the model equation is
//     sum_n r_n/(s_k - a_n) + d
//   - H_k * sum_n c_n/(s_k - a_n)  == H_k
// which is linear in x. Stack real and imaginary parts as separate
// rows so the LS lives entirely in the reals (Eigen's complex LS is
// fine too, but real is faster and the answer is real-valued anyway).
//
// Returns A (2M x (2N+optional)) and b (2M).
struct LsSystem {
    Eigen::MatrixXd A;
    Eigen::VectorXd b;
};

LsSystem build_ls_system(const std::vector<double>& freq_hz,
                          const std::vector<Complex>& H,
                          const std::vector<double>& poles,
                          bool include_d) {
    const int M = static_cast<int>(freq_hz.size());
    const int N = static_cast<int>(poles.size());
    const int cols = 2 * N + (include_d ? 1 : 0);
    LsSystem ls;
    ls.A.setZero(2 * M, cols);
    ls.b.setZero(2 * M);

    for (int k = 0; k < M; ++k) {
        const Complex s_k(0.0, kTwoPi * freq_hz[k]);
        // sum_n r_n * basis_n - H_k * sum_n c_n * basis_n + (d if any) = H_k
        for (int n = 0; n < N; ++n) {
            const Complex basis = Complex(1.0, 0.0) / (s_k - poles[n]);
            // r_n column
            ls.A(2 * k,     n) = basis.real();
            ls.A(2 * k + 1, n) = basis.imag();
            // c_n column (negated and multiplied by H_k)
            const Complex c_basis = -H[k] * basis;
            ls.A(2 * k,     N + (include_d ? 1 : 0) + n) = c_basis.real();
            ls.A(2 * k + 1, N + (include_d ? 1 : 0) + n) = c_basis.imag();
        }
        if (include_d) {
            ls.A(2 * k,     N) = 1.0;
            ls.A(2 * k + 1, N) = 0.0;
        }
        ls.b(2 * k)     = H[k].real();
        ls.b(2 * k + 1) = H[k].imag();
    }
    return ls;
}

// Solve A x = b in the least-squares sense via Householder QR. Returns
// x. Falls back to JacobiSVD if QR is rank-deficient.
Eigen::VectorXd ls_solve(const Eigen::MatrixXd& A, const Eigen::VectorXd& b) {
    auto qr = A.colPivHouseholderQr();
    if (qr.rank() < A.cols()) {
        return A.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);
    }
    return qr.solve(b);
}

// Update poles to the roots of sigma(s) = 1 + sum c_n/(s - a_n) = 0,
// which equal the eigenvalues of (diag(a) - 1 * c^T). Flip any pole
// with positive real part back to the LHP for passivity.
std::vector<double> flip_to_lhp(const std::vector<double>& old_poles,
                                  const Eigen::VectorXd& c) {
    const int N = static_cast<int>(old_poles.size());
    Eigen::MatrixXd H(N, N);
    H.setZero();
    for (int i = 0; i < N; ++i) {
        H(i, i) = old_poles[i];
        for (int j = 0; j < N; ++j) {
            H(i, j) -= c(j);
        }
    }
    Eigen::EigenSolver<Eigen::MatrixXd> es(H);
    const auto eig = es.eigenvalues();

    // Read all N eigenvalues. For each: take the real part, sign-flip
    // any positive-real onto the negative axis (causality). Complex
    // eigenvalues from the real-only model come in conjugate pairs
    // whose real parts are identical; taking the real part of each
    // would produce a degenerate near-repeated pole that collapses the
    // LS at the next iteration. We detect that case (two consecutive
    // eigenvalues with identical real part and opposite imaginary
    // parts) and reject the update for that pair, keeping the old
    // poles instead. The fit converges more slowly when this rejection
    // fires often, but it does not diverge.
    std::vector<double> new_poles;
    new_poles.reserve(N);
    int i = 0;
    while (i < static_cast<int>(eig.size()) &&
           static_cast<int>(new_poles.size()) < N) {
        const Complex e = eig(i);
        const bool is_complex = std::abs(e.imag()) >
                                  1e-6 * std::abs(e.real()) + 1e-12;
        if (is_complex && i + 1 < static_cast<int>(eig.size())) {
            const Complex e2 = eig(i + 1);
            const bool is_pair =
                std::abs(e.real() - e2.real()) <
                    1e-9 * std::abs(e.real()) + 1e-12 &&
                std::abs(e.imag() + e2.imag()) <
                    1e-6 * std::abs(e.imag()) + 1e-12;
            if (is_pair) {
                // Complex-conjugate pair from a fundamentally complex
                // root. Keep the previous two old_poles unchanged.
                const int idx = static_cast<int>(new_poles.size());
                new_poles.push_back(old_poles[idx]);
                if (static_cast<int>(new_poles.size()) < N) {
                    new_poles.push_back(old_poles[idx + 1]);
                }
                i += 2;
                continue;
            }
        }
        double p = e.real();
        if (p > 0.0) p = -p;
        // Don't push extremely-near-zero poles; they cause numerical
        // grief in the next LS.
        if (std::abs(p) < 1e-3) p = -1e-3;
        new_poles.push_back(p);
        ++i;
    }
    while (static_cast<int>(new_poles.size()) < N) {
        const int idx = static_cast<int>(new_poles.size());
        new_poles.push_back(old_poles[idx]);
    }
    return new_poles;
}

double rms_fit_error(const std::vector<double>& freq_hz,
                      const std::vector<Complex>& H,
                      const VectorFitResult& fit) {
    double num = 0.0, den = 0.0;
    for (std::size_t k = 0; k < freq_hz.size(); ++k) {
        const Complex h = evaluate_fit(fit, freq_hz[k]);
        const Complex diff = h - H[k];
        num += std::norm(diff);
        den += std::norm(H[k]);
    }
    if (den <= 0.0) return 0.0;
    return std::sqrt(num / den);
}

}  // namespace

std::complex<double> evaluate_fit(const VectorFitResult& fit, double freq_hz) {
    const Complex s(0.0, kTwoPi * freq_hz);
    Complex y(fit.d, 0.0);
    for (std::size_t n = 0; n < fit.poles.size(); ++n) {
        y += fit.residues[n] / (s - fit.poles[n]);
    }
    return y;
}

VectorFitResult vector_fit(
    const std::vector<double>& freq_hz,
    const std::vector<Complex>& H,
    const VectorFitOptions& opts) {
    if (freq_hz.size() != H.size()) {
        throw VectorFitError("vector_fit: freq_hz and response size mismatch");
    }
    const int M = static_cast<int>(freq_hz.size());
    if (M < 4 * opts.n_poles) {
        throw VectorFitError("vector_fit: too few samples for requested pole count");
    }
    if (opts.n_poles < 1) {
        throw VectorFitError("vector_fit: n_poles must be >= 1");
    }
    if (!std::is_sorted(freq_hz.begin(), freq_hz.end())) {
        throw VectorFitError("vector_fit: frequencies must be sorted ascending");
    }
    if (freq_hz.front() <= 0.0) {
        throw VectorFitError("vector_fit: frequencies must be positive");
    }

    std::vector<double> poles = initial_poles(
        opts.n_poles, freq_hz.front(), freq_hz.back());

    // Sanathanan-Koerner iteration. Each pass: solve LS for residues
    // and direction-of-update c, then update poles to roots of sigma.
    // The flip_to_lhp helper rejects complex-conjugate eigenvalue
    // pairs (which would collapse to degenerate real poles) and
    // preserves the old pole instead. That trades convergence speed
    // for stability on responses (like exp(-alpha*sqrt(f))) whose
    // natural rational model uses complex poles.
    double last_c_norm = 0.0;
    bool converged = false;
    Eigen::VectorXd x;
    int iter = 0;
    for (; iter < opts.max_iter; ++iter) {
        auto ls = build_ls_system(freq_hz, H, poles, opts.include_constant);
        x = ls_solve(ls.A, ls.b);

        const int N = opts.n_poles;
        const int d_offset = opts.include_constant ? 1 : 0;
        Eigen::VectorXd c = x.segment(N + d_offset, N);
        const double c_norm = c.norm();

        if (iter > 0 &&
            std::abs(c_norm - last_c_norm) <
                opts.convergence_tol * std::abs(last_c_norm)) {
            converged = true;
            ++iter;
            break;
        }
        last_c_norm = c_norm;
        poles = flip_to_lhp(poles, c);
    }
    auto final_ls = build_ls_system(freq_hz, H, poles, opts.include_constant);
    x = ls_solve(final_ls.A, final_ls.b);

    VectorFitResult result;
    result.poles = std::move(poles);
    result.residues.assign(x.data(), x.data() + opts.n_poles);
    result.d = opts.include_constant ? x(opts.n_poles) : 0.0;
    result.iterations = iter;
    result.converged = converged;
    result.rms_error = rms_fit_error(freq_hz, H, result);
    return result;
}

}  // namespace sikit::si
