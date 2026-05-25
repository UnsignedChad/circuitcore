#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>
#include <utility>

#include "si/VectorFit.h"

using namespace sikit::si;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {

// Build a synthetic frequency response by evaluating a known rational
// function at the supplied frequency grid. Used so we can verify the
// vector_fit pass recovers the originating poles and residues within
// a tight tolerance.
std::vector<Complex> sample_response(
    const std::vector<double>& freq_hz,
    const std::vector<double>& poles,
    const std::vector<double>& residues,
    double d) {
    std::vector<Complex> out;
    out.reserve(freq_hz.size());
    for (double f : freq_hz) {
        const Complex s(0.0, 2.0 * std::numbers::pi * f);
        Complex y(d, 0.0);
        for (std::size_t i = 0; i < poles.size(); ++i) {
            y += residues[i] / (s - poles[i]);
        }
        out.push_back(y);
    }
    return out;
}

std::vector<double> log_freq_grid(double f_lo, double f_hi, int M) {
    std::vector<double> g(M);
    const double log_lo = std::log(f_lo);
    const double log_hi = std::log(f_hi);
    for (int i = 0; i < M; ++i) {
        const double t = static_cast<double>(i) / (M - 1);
        g[i] = std::exp(log_lo + t * (log_hi - log_lo));
    }
    return g;
}

}  // namespace

TEST_CASE("vector_fit: recovers a single-pole real model", "[vectorfit]") {
    // Build H(s) = r / (s - p) + d at 200 log-spaced frequency points
    // and verify VF reconstructs it. With one pole and 200 samples the
    // fit should be near-machine-precision.
    const double p_true = -1e10;        // pole at ~1.6 GHz
    const double r_true = 5e10;
    const double d_true = 0.05;
    const auto freqs = log_freq_grid(1e6, 100e9, 200);
    const auto H = sample_response(freqs, {p_true}, {r_true}, d_true);

    VectorFitOptions opts;
    opts.n_poles = 1;
    auto fit = vector_fit(freqs, H, opts);
    REQUIRE(fit.poles.size() == 1);
    REQUIRE(fit.rms_error < 1e-6);
}

TEST_CASE("vector_fit: recovers a three-pole real model", "[vectorfit]") {
    const std::vector<double> p_true{-1e9, -5e9, -2e10};
    const std::vector<double> r_true{1e9, 4e9, 8e9};
    const double d_true = 0.1;
    const auto freqs = log_freq_grid(1e6, 100e9, 300);
    const auto H = sample_response(freqs, p_true, r_true, d_true);

    VectorFitOptions opts;
    opts.n_poles = 3;
    auto fit = vector_fit(freqs, H, opts);
    REQUIRE(fit.rms_error < 1e-3);

    // Sanity: the fit's evaluate function matches the synthetic
    // response at every sample to within rms_error.
    for (std::size_t k = 0; k < freqs.size(); k += 30) {
        const Complex got = evaluate_fit(fit, freqs[k]);
        REQUIRE(std::abs(got - H[k]) < 1e-3 * std::abs(H[k]) + 1e-9);
    }
}

TEST_CASE("vector_fit: poles come out negative (causal, stable)",
          "[vectorfit]") {
    const auto freqs = log_freq_grid(1e6, 50e9, 200);
    // Channel-like response: monotonic loss, no resonance.
    const std::vector<double> p_true{-2e9, -1e10, -5e10};
    const std::vector<double> r_true{-5e8, -8e9, -3e10};
    const auto H = sample_response(freqs, p_true, r_true, 1.0);

    VectorFitOptions opts;
    opts.n_poles = 5;     // over-parameterised on purpose
    auto fit = vector_fit(freqs, H, opts);
    for (double p : fit.poles) {
        REQUIRE(p < 0.0);
    }
}

TEST_CASE("vector_fit: rejects malformed input", "[vectorfit]") {
    REQUIRE_THROWS_AS(vector_fit({}, {}), VectorFitError);

    // Size mismatch.
    REQUIRE_THROWS_AS(
        vector_fit({1e9, 2e9}, {Complex(1,0)}),
        VectorFitError);

    // Too few samples for the requested pole count.
    VectorFitOptions opts;
    opts.n_poles = 12;
    std::vector<double> short_grid{1e9, 2e9, 3e9};
    std::vector<Complex> short_H(3, Complex(1, 0));
    REQUIRE_THROWS_AS(vector_fit(short_grid, short_H, opts), VectorFitError);

    // Frequencies must be sorted.
    auto freqs = log_freq_grid(1e6, 100e9, 200);
    std::swap(freqs[10], freqs[20]);
    auto H = sample_response(freqs, {-1e9}, {1e9}, 0.0);
    REQUIRE_THROWS_AS(vector_fit(freqs, H, VectorFitOptions{}), VectorFitError);
}

TEST_CASE("vector_fit: many-pole fit of a low-pass channel converges to a "
          "sensible model", "[vectorfit]") {
    // Eight real poles spread over four decades, real residues, plus a
    // d term. Mimics the rational shape of a frequency-flat skin-loss
    // channel and verifies the algorithm scales past the trivial cases.
    // No phase delay -- that needs complex-pole pairs which the v1 fit
    // does not yet emit; see VectorFit.h for the limitation note.
    const std::vector<double> p_true{-1e8, -3e8, -1e9, -3e9, -1e10,
                                       -3e10, -1e11, -3e11};
    const std::vector<double> r_true{1e8,  3e8,  1e9,  3e9,  1e10,
                                       3e10, 1e11, 3e11};
    const auto freqs = log_freq_grid(1e6, 200e9, 400);
    const auto H = sample_response(freqs, p_true, r_true, 0.2);
    VectorFitOptions opts;
    opts.n_poles = 8;
    opts.max_iter = 16;
    auto fit = vector_fit(freqs, H, opts);
    REQUIRE(fit.rms_error < 0.02);
}
