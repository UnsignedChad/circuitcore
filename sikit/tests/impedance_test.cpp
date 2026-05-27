// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/Impedance.h"

using namespace sikit::impedance;
using Catch::Approx;

TEST_CASE("microstrip: ~50 ohm at standard FR-4 geometry", "[impedance]") {
    // Industry-standard ~50Ω microstrip on 1.524mm FR-4 (1/2 of a 1.6mm board)
    // with 1oz copper and W ≈ 2.8mm.
    MicrostripParams p{
        .trace_width = 2.8e-3,
        .dielectric_height = 1.524e-3,
        .trace_thickness = 35e-6,
        .epsilon_r = 4.4,
    };
    REQUIRE(microstrip_z0(p) == Approx(50.0).margin(2.0));
    REQUIRE(microstrip_in_valid_range(p));
}

TEST_CASE("microstrip: narrower trace → higher impedance", "[impedance]") {
    MicrostripParams a{1.5e-3, 1.524e-3, 35e-6, 4.4};
    MicrostripParams b{3.0e-3, 1.524e-3, 35e-6, 4.4};
    REQUIRE(microstrip_z0(a) > microstrip_z0(b));
}

TEST_CASE("microstrip: higher εr → lower impedance", "[impedance]") {
    MicrostripParams p_fr4{2.8e-3, 1.524e-3, 35e-6, 4.4};
    MicrostripParams p_rogers{2.8e-3, 1.524e-3, 35e-6, 3.0};
    REQUIRE(microstrip_z0(p_rogers) > microstrip_z0(p_fr4));
}

TEST_CASE("stripline: known geometry produces ~50 ohm", "[impedance]") {
    // 6-layer stackup, ~50Ω stripline: W=0.2mm, B=0.6mm, T=35um, εr=4.2.
    // Hand-computed via the IPC formula gives ≈51.7Ω.
    StriplineParams p{
        .trace_width = 0.2e-3,
        .plane_separation = 0.6e-3,
        .trace_thickness = 35e-6,
        .epsilon_r = 4.2,
    };
    REQUIRE(stripline_z0(p) == Approx(50.0).margin(5.0));
}

TEST_CASE("stripline: validity check rejects too-wide trace", "[impedance]") {
    StriplineParams p{1.0e-3, 0.4e-3, 35e-6, 4.2};  // W/B-T > 0.35
    REQUIRE_FALSE(stripline_in_valid_range(p));
}

TEST_CASE("diff microstrip: 100 ohm at typical USB-style geometry", "[impedance]") {
    // ~50Ω single-ended traces edge-coupled with spacing close to height
    // gives roughly 80-90Ω diff. Verify the formula's monotonicity.
    const double s = 0.4e-3;
    const double h = 0.4e-3;
    const double z = edge_coupled_microstrip_diff(50.0, s, h);
    REQUIRE(z > 60.0);
    REQUIRE(z < 100.0);

    // Closer spacing → lower diff impedance.
    const double z_closer = edge_coupled_microstrip_diff(50.0, 0.1e-3, h);
    REQUIRE(z_closer < z);

    // Far spacing → approaches 2·Z₀ = 100 Ω.
    const double z_far = edge_coupled_microstrip_diff(50.0, 10.0 * h, h);
    REQUIRE(z_far == Approx(100.0).margin(1.0));
}

TEST_CASE("diff stripline: monotonicity vs spacing", "[impedance]") {
    const double b = 0.4e-3;
    const double z_close = edge_coupled_stripline_diff(50.0, 0.05e-3, b);
    const double z_med   = edge_coupled_stripline_diff(50.0, 0.20e-3, b);
    const double z_far   = edge_coupled_stripline_diff(50.0, 2.00e-3, b);
    REQUIRE(z_close < z_med);
    REQUIRE(z_med   < z_far);
    REQUIRE(z_far == Approx(100.0).margin(1.0));
}

TEST_CASE("microstrip: zero/negative inputs throw", "[impedance]") {
    REQUIRE_THROWS_AS(microstrip_z0({0.0,  1.524e-3, 35e-6, 4.4}), ImpedanceError);
    REQUIRE_THROWS_AS(microstrip_z0({2.8e-3, 0.0,    35e-6, 4.4}), ImpedanceError);
    REQUIRE_THROWS_AS(microstrip_z0({2.8e-3, 1.524e-3, -1.0, 4.4}), ImpedanceError);
    REQUIRE_THROWS_AS(microstrip_z0({2.8e-3, 1.524e-3, 35e-6, 0.5}), ImpedanceError);
}

TEST_CASE("stripline: zero/negative inputs throw", "[impedance]") {
    REQUIRE_THROWS_AS(stripline_z0({0.0,    0.4e-3, 35e-6, 4.2}), ImpedanceError);
    REQUIRE_THROWS_AS(stripline_z0({0.2e-3, 0.0,    35e-6, 4.2}), ImpedanceError);
}

TEST_CASE("diff: zero-spacing limit is below 2·Z0", "[impedance]") {
    // S=0 in the microstrip formula: Zdiff = 2·Z0 · (1 - 0.48 · 1) = 2·Z0 · 0.52
    REQUIRE(edge_coupled_microstrip_diff(50.0, 0.0, 1e-3) == Approx(52.0));
    // S=0 stripline: 2·Z0 · (1 - 0.347) = 2·Z0 · 0.653 → 65.3 for Z0=50
    REQUIRE(edge_coupled_stripline_diff(50.0, 0.0, 1e-3) == Approx(65.3).margin(0.1));
}
