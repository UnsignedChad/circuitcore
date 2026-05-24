#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/DiffSynth.h"

using namespace sikit::analysis;
using Catch::Approx;

namespace {

DiffChannelSpec basic_diff() {
    DiffChannelSpec s;
    s.trace_width = 0.15e-3;
    s.spacing     = 0.15e-3;
    s.layer_ordinal = 0;
    s.length_m    = 0.05;            // 5 cm
    s.stackup.outer_dielectric_height = 0.2e-3;
    s.stackup.copper_thickness = 35e-6;
    s.stackup.epsilon_r = 4.3;
    return s;
}

}  // namespace

TEST_CASE("diff synth: returns a 4-port file with the requested freq grid",
          "[diffsynth]") {
    auto spec = basic_diff();
    std::vector<double> freqs{1e9, 2e9, 5e9};
    auto t = synthesize_diff_channel(spec, freqs);
    REQUIRE(t.num_ports == 4);
    REQUIRE(t.frequencies.size() == 3);
    REQUIRE(t.s_matrices.size() == 3);
    for (const auto& m : t.s_matrices) {
        REQUIRE(m.size() == 16);
    }
}

TEST_CASE("diff synth: matrix is symmetric (reciprocal lossless network)",
          "[diffsynth]") {
    auto spec = basic_diff();
    auto t = synthesize_diff_channel(spec, {1e9, 3e9});
    for (const auto& m : t.s_matrices) {
        // Reciprocal: S_ij == S_ji for i != j (within numerical tolerance).
        for (int r = 0; r < 4; ++r) {
            for (int c = r + 1; c < 4; ++c) {
                const auto a = m[r + c * 4];
                const auto b = m[c + r * 4];
                REQUIRE(std::abs(a - b) < 1e-9);
            }
        }
    }
}

TEST_CASE("diff synth: zero-length channel is the identity 4-port", "[diffsynth]") {
    auto spec = basic_diff();
    spec.length_m = 1e-12;   // effectively zero
    auto t = synthesize_diff_channel(spec, {1e9});
    const auto& m = t.s_matrices[0];

    // Diagonal blocks: S11, S22, S33, S44 should be ~0 (no reflection).
    REQUIRE(std::abs(m[0 + 0 * 4]) < 0.01);   // S11
    REQUIRE(std::abs(m[1 + 1 * 4]) < 0.01);   // S22
    REQUIRE(std::abs(m[2 + 2 * 4]) < 0.01);   // S33
    REQUIRE(std::abs(m[3 + 3 * 4]) < 0.01);   // S44

    // Same-trace thru: S21, S12, S43, S34 ≈ 1 magnitude.
    REQUIRE(std::abs(m[1 + 0 * 4]) == Approx(1.0).margin(0.01));   // S21
    REQUIRE(std::abs(m[3 + 2 * 4]) == Approx(1.0).margin(0.01));   // S43
}

TEST_CASE("diff synth: cross-trace coupling falls off with spacing", "[diffsynth]") {
    auto close = basic_diff();
    close.spacing = 0.10e-3;
    auto far   = basic_diff();
    far.spacing = 1.0e-3;

    auto t_close = synthesize_diff_channel(close, {2e9});
    auto t_far   = synthesize_diff_channel(far,   {2e9});

    // S13 is the near-end coupling between traces. Closer traces couple
    // more, so |S13_close| > |S13_far|.
    const double xc = std::abs(t_close.s_matrices[0][2 + 0 * 4]);
    const double xf = std::abs(t_far.s_matrices[0][2 + 0 * 4]);
    REQUIRE(xc > xf);
}

TEST_CASE("diff synth: rejects bad geometry", "[diffsynth]") {
    auto spec = basic_diff();
    spec.trace_width = 0.0;
    REQUIRE_THROWS(synthesize_diff_channel(spec, {1e9}));
    spec = basic_diff();
    spec.length_m = -1.0;
    REQUIRE_THROWS(synthesize_diff_channel(spec, {1e9}));
}
