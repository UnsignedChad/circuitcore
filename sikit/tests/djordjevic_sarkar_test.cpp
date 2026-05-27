// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/DjordjevicSarkar.h"

using namespace sikit::dispersion;
using Catch::Approx;

TEST_CASE("DS: from_reference reproduces inputs at f0", "[ds]") {
    // Build a model from a known data point (FR-4-style: εr=4.5 tan_δ=0.02
    // at 1 GHz) and verify it returns exactly that at the same f0.
    const double eps_r = 4.5;
    const double tand  = 0.02;
    const double f0    = 1.0e9;

    auto m = DjordjevicSarkar::from_reference(eps_r, tand, f0);
    REQUIRE(m.epsilon_r(f0) == Approx(eps_r).margin(1e-9));
    REQUIRE(m.tan_delta(f0) == Approx(tand).margin(1e-9));
}

TEST_CASE("DS: εr decreases monotonically with frequency in-band", "[ds]") {
    // Inside (f1, f2) the real part of ε is monotonically decreasing —
    // canonical "high-frequency dielectric relaxation" behavior.
    auto m = DjordjevicSarkar::from_reference(4.5, 0.02, 1.0e9);
    double prev = m.epsilon_r(1.0e6);
    for (double f : {1.0e7, 1.0e8, 1.0e9, 1.0e10, 1.0e11}) {
        const double v = m.epsilon_r(f);
        REQUIRE(v < prev);
        prev = v;
    }
}

TEST_CASE("DS: ε'' is positive (lossy material) across band", "[ds]") {
    auto m = DjordjevicSarkar::from_reference(4.5, 0.02, 1.0e9);
    for (double f : {1.0e7, 1.0e8, 1.0e9, 1.0e10, 1.0e11}) {
        const auto e = m.epsilon_complex(f);
        REQUIRE(e.imag() < 0.0);   // convention: ε = ε' - j ε''
    }
}

TEST_CASE("DS: tan δ stays in a physically plausible range", "[ds]") {
    auto m = DjordjevicSarkar::from_reference(4.5, 0.02, 1.0e9);
    for (double f : {1.0e7, 1.0e8, 1.0e9, 1.0e10, 1.0e11}) {
        const double td = m.tan_delta(f);
        REQUIRE(td > 0.0);
        REQUIRE(td < 0.5);   // anything above this would be exotic / wrong
    }
}

TEST_CASE("DS: high tan_delta gives larger dispersion magnitude", "[ds]") {
    // tan δ = 0.02 (FR-4) vs 0.001 (Rogers-style low-loss): the lossier
    // material has a bigger εr swing across the same f1..f2 band.
    auto fr4    = DjordjevicSarkar::from_reference(4.5, 0.02, 1.0e9);
    auto rogers = DjordjevicSarkar::from_reference(4.5, 0.001, 1.0e9);

    const double swing_fr4    = fr4.epsilon_r(1.0e7)    - fr4.epsilon_r(1.0e11);
    const double swing_rogers = rogers.epsilon_r(1.0e7) - rogers.epsilon_r(1.0e11);
    REQUIRE(swing_fr4 > swing_rogers);
    REQUIRE(swing_fr4 > 0.0);
}

TEST_CASE("DS: rejects bad construction parameters", "[ds]") {
    REQUIRE_THROWS(DjordjevicSarkar(4.4, 0.5, /*f1=*/0.0, /*f2=*/1e12));
    REQUIRE_THROWS(DjordjevicSarkar(4.4, 0.5, /*f1=*/1e12, /*f2=*/1e3));   // f1 ≥ f2
    REQUIRE_THROWS(DjordjevicSarkar::from_reference(-1.0, 0.02, 1e9));   // εr ≤ 0
    REQUIRE_THROWS(DjordjevicSarkar::from_reference(4.5, 0.02, -1.0));  // f0 ≤ 0
}

TEST_CASE("DS: very low loss approaches lossless behavior", "[ds]") {
    // tan δ = 0 → Δε = 0 → ε(ω) = ε_∞ everywhere, no dispersion at all.
    auto lossless = DjordjevicSarkar::from_reference(4.4, 0.0, 1.0e9);
    REQUIRE(lossless.epsilon_r(1.0e6)  == Approx(4.4));
    REQUIRE(lossless.epsilon_r(1.0e10) == Approx(4.4));
    REQUIRE(lossless.tan_delta(1.0e9)  == Approx(0.0));
}
