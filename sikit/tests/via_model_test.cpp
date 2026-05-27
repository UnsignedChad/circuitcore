// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/ViaModel.h"

using namespace sikit::analysis;
using Catch::Approx;

namespace {

// Realistic 1.6mm board, 0.3mm drill, 0.6mm pad, 1.0mm antipad.
ViaSpec stubless_via() {
    ViaSpec v;
    v.drill_diameter   = 0.30e-3;
    v.pad_diameter     = 0.60e-3;
    v.antipad_diameter = 1.00e-3;
    v.total_length     = 1.6e-3;
    v.pad_to_plane_h   = 0.20e-3;
    v.stub_length      = 0.0;
    v.epsilon_r        = 4.3;
    v.tan_delta        = 0.02;
    return v;
}

}  // namespace

TEST_CASE("via_lumped: sensible L and C for canonical PCB via", "[via]") {
    auto v = stubless_via();
    auto p = via_lumped(v);

    // Order-of-magnitude sanity: L should be sub-nH, C should be sub-pF.
    REQUIRE(p.L_barrel > 0.1e-9);   // > 0.1 nH
    REQUIRE(p.L_barrel < 5.0e-9);   // < 5 nH
    REQUIRE(p.C_pad    > 0.01e-12); // > 10 fF
    REQUIRE(p.C_pad    < 5.0e-12);  // < 5 pF
    REQUIRE(p.Z_stub   > 10.0);     // coax-line Z typical 30-80 Ω
    REQUIRE(p.Z_stub   < 200.0);
}

TEST_CASE("via_lumped: L scales with ln(antipad/drill)", "[via]") {
    auto small = stubless_via();
    auto big = stubless_via();
    big.antipad_diameter = 2.0e-3;       // bigger antipad → bigger L
    REQUIRE(via_lumped(big).L_barrel > via_lumped(small).L_barrel);
}

TEST_CASE("via_lumped: stub resonance at expected frequency", "[via]") {
    auto v = stubless_via();
    v.stub_length = 1.0e-3;              // 1 mm stub
    auto p = via_lumped(v);
    // f_res = c0 / (4 * L * sqrt(eps_r))
    // = 3e8 / (4 * 1e-3 * sqrt(4.3)) ≈ 36 GHz
    REQUIRE(p.stub_resonance_hz == Approx(36.2e9).margin(2e9));
}

TEST_CASE("compute_via_s2p: rejects bad geometry", "[via]") {
    std::vector<double> f{1e9};
    auto v = stubless_via();
    v.drill_diameter = 0.0;
    REQUIRE_THROWS(compute_via_s2p(v, f));
    v = stubless_via();
    v.antipad_diameter = v.drill_diameter;   // not larger
    REQUIRE_THROWS(compute_via_s2p(v, f));
    v = stubless_via();
    v.total_length = 0.0;
    REQUIRE_THROWS(compute_via_s2p(v, f));
}

TEST_CASE("compute_via_s2p: stubless via passes through at low freq", "[via]") {
    auto v = stubless_via();
    auto ts = compute_via_s2p(v, {1e6, 10e6, 100e6}, 50.0);
    // |S21| should be very close to 1 in the MHz range.
    for (const auto& m : ts.s_matrices) {
        REQUIRE(std::abs(m[1]) == Approx(1.0).margin(0.005));
    }
}

TEST_CASE("compute_via_s2p: insertion loss grows with frequency", "[via]") {
    auto v = stubless_via();
    auto ts = compute_via_s2p(v, {100e6, 1e9, 5e9, 10e9, 20e9}, 50.0);
    double prev = std::abs(ts.s_matrices.front()[1]);
    int dropped = 0;
    for (std::size_t k = 1; k < ts.s_matrices.size(); ++k) {
        const double cur = std::abs(ts.s_matrices[k][1]);
        if (cur < prev) ++dropped;
        prev = cur;
    }
    // Most steps should show a drop (some early steps may be flat).
    REQUIRE(dropped >= 3);
}

TEST_CASE("compute_via_s2p: stub creates a deep |S21| notch near resonance",
          "[via]") {
    auto v = stubless_via();
    v.stub_length = 1.0e-3;              // 1 mm stub → ~36 GHz resonance

    // Sweep across the resonance.
    std::vector<double> freqs;
    for (double f = 5e9; f <= 60e9; f += 1e9) freqs.push_back(f);
    auto ts = compute_via_s2p(v, freqs, 50.0);

    // Find min |S21|. Should be substantially below the off-resonance level.
    double min_s21 = 1.0;
    double off_resonance_s21 = std::abs(ts.s_matrices.front()[1]);
    for (const auto& m : ts.s_matrices) {
        const double mag = std::abs(m[1]);
        if (mag < min_s21) min_s21 = mag;
    }
    REQUIRE(min_s21 < off_resonance_s21 * 0.5);
}

TEST_CASE("compute_via_s2p: 2-port file shape matches grid", "[via]") {
    auto v = stubless_via();
    std::vector<double> freqs{1e9, 2e9, 3e9, 4e9};
    auto ts = compute_via_s2p(v, freqs, 50.0);
    REQUIRE(ts.num_ports == 2);
    REQUIRE(ts.frequencies.size() == freqs.size());
    REQUIRE(ts.s_matrices.size() == freqs.size());
    for (const auto& m : ts.s_matrices) {
        REQUIRE(m.size() == 4);   // 2-port = 2x2
    }
}
