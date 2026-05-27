// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Reference-data validation suite.
//
// Each case below pairs a canonical PCB cross-section with a published or
// widely-accepted reference Z₀ value, and verifies sikit's closed-form
// engine reproduces it within a tolerance band. Tolerances account for
// the fact that "ground truth" itself is model-dependent — Polar Si9000
// uses Hammerstad-Jensen, sikit uses IPC-2141A, and the two formulas
// disagree by 2–5% even on the textbook microstrip geometry.
//
// Sources are cited per case. Where possible we picked geometries with
// independent attestations from at least two of:
//   * IPC-2141A "Design Guide for High-Speed Controlled-Impedance Circuit
//     Boards", appendix tables
//   * Polar Si9000 controlled-impedance calculator (industry default)
//   * Wadell, "Transmission Line Design Handbook", worked examples
//
// Expected impedances are noted to 3 significant figures because the
// upstream tools rarely publish more than that.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/TraceImpedance.h"
#include "si/Impedance.h"

using namespace sikit::analysis;
using namespace sikit::impedance;
using Catch::Approx;

namespace {

struct MicrostripCase {
    const char* name;
    const char* source;
    double W;    // m
    double H;    // m
    double T;    // m
    double Er;
    double expected_z0;  // Ω
    double tolerance;    // fraction (0.10 = ±10%)
};

struct StriplineCase {
    const char* name;
    const char* source;
    double W;
    double B;
    double T;
    double Er;
    double expected_z0;
    double tolerance;
};

constexpr double mm(double v) { return v * 1e-3; }
constexpr double mil_to_mm(double m) { return m * 0.0254; }

}  // namespace

TEST_CASE("ref-data: microstrip canonical geometries (IPC-2141A / Polar)", "[ref]") {
    const MicrostripCase cases[] = {
        // 1oz Cu = 35µm = ~1.4mil
        { "50Ω microstrip on 1.524mm FR-4 (textbook)",
          "IPC-2141A appendix; Wadell §3.5",
          mm(2.80), mm(1.524), mm(0.035), 4.4,
          50.0, 0.10 },

        { "75Ω microstrip on 1.524mm FR-4",
          "IPC-2141A hand-derived; W = 1.40 mm at H = 1.524 mm gives 75 Ω",
          mm(1.40), mm(1.524), mm(0.035), 4.4,
          75.0, 0.10 },

        { "50Ω microstrip on 0.2mm FR-4 (4-layer outer)",
          "IPC-2141A; typical 4-layer outer-prepreg geometry",
          mm(0.36), mm(0.20), mm(0.035), 4.3,
          50.0, 0.12 },

        { "50Ω microstrip on 0.1mm FR-4 (HDI)",
          "IPC-2141A; typical HDI outer",
          mm(0.18), mm(0.10), mm(0.018), 4.0,
          50.0, 0.12 },
    };

    for (const auto& c : cases) {
        SECTION(c.name) {
            MicrostripParams p{c.W, c.H, c.T, c.Er};
            const double z = microstrip_z0(p);
            INFO("source: " << c.source);
            INFO("computed Z0 = " << z << " Ω (expected " << c.expected_z0 << ")");
            REQUIRE(std::abs(z - c.expected_z0) / c.expected_z0 < c.tolerance);
            REQUIRE(microstrip_in_valid_range(p));
        }
    }
}

TEST_CASE("ref-data: stripline canonical geometries", "[ref]") {
    const StriplineCase cases[] = {
        { "50Ω stripline B=0.6mm",
          "Polar Si9000; common 6-layer inner",
          mm(0.20), mm(0.60), mm(0.035), 4.2,
          50.0, 0.10 },

        { "50Ω stripline B=0.4mm narrower trace",
          "Polar Si9000; typical 4-layer inner",
          mm(0.13), mm(0.40), mm(0.018), 4.2,
          50.0, 0.15 },
    };

    for (const auto& c : cases) {
        SECTION(c.name) {
            StriplineParams p{c.W, c.B, c.T, c.Er};
            const double z = stripline_z0(p);
            INFO("source: " << c.source);
            INFO("computed Z0 = " << z << " Ω (expected " << c.expected_z0 << ")");
            REQUIRE(std::abs(z - c.expected_z0) / c.expected_z0 < c.tolerance);
        }
    }
}

TEST_CASE("ref-data: differential microstrip Z_diff", "[ref]") {
    // Edge-coupled diff microstrip — typical USB / HDMI / DDR geometries.
    // Polar Si9000 ranges:
    //   * 4mil/4mil/4mil (W/S/H) on εr=4.3 → ~100Ω
    //   * USB 2.0 HS spec target = 90Ω ± 15%
    AnalysisStackup stack;
    stack.outer_dielectric_height = mil_to_mm(4.0);
    stack.copper_thickness = mil_to_mm(1.4);
    stack.epsilon_r = 4.3;

    SECTION("4/4/4 → ~100Ω") {
        const double z_d = compute_diff_z0_closed_form(
            mil_to_mm(4.0), mil_to_mm(4.0), 0, stack);
        INFO("Z_diff = " << z_d << " Ω");
        REQUIRE(std::abs(z_d - 100.0) / 100.0 < 0.15);
    }

    SECTION("USB-style 4/6/4 → ~85-95Ω") {
        const double z_d = compute_diff_z0_closed_form(
            mil_to_mm(4.0), mil_to_mm(6.0), 0, stack);
        REQUIRE(z_d > 80.0);
        REQUIRE(z_d < 110.0);
    }
}

TEST_CASE("ref-data: FDM agrees with closed-form within budget", "[ref]") {
    // Independent cross-validation: two engines with different math
    // (closed-form vs 2D Laplace finite-difference) should converge to
    // the same answer within their joint error budget. Use the textbook
    // 50Ω case so any disagreement is easy to interpret.
    AnalysisStackup s;
    s.outer_dielectric_height = mm(1.524);
    s.copper_thickness = mm(0.035);
    s.epsilon_r = 4.4;

    const double W = mm(2.80);
    auto cf  = compute_one(W, 0, s);
    auto fdm = compute_one_fdm(W, 0, s);

    INFO("closed-form Z0 = " << cf.z0  << " Ω");
    INFO("FDM Z0         = " << fdm.z0 << " Ω");

    // Both should land in [42, 62] Ω for this geometry.
    REQUIRE(cf.z0  > 42.0);  REQUIRE(cf.z0  < 62.0);
    REQUIRE(fdm.z0 > 42.0);  REQUIRE(fdm.z0 < 62.0);

    // And agree with each other to within 20% (FDM at v0 mesh density
    // tends to over-estimate by 10-15% from the cell-rect classifier
    // fattening the conductor edges).
    REQUIRE(std::abs(fdm.z0 - cf.z0) / cf.z0 < 0.20);
}

TEST_CASE("ref-data: trace impedance monotonicity sanity", "[ref]") {
    // Independent of any reference value: physics dictates that
    //   - wider trace → lower Z₀
    //   - thicker dielectric → higher Z₀
    //   - higher εr → lower Z₀
    AnalysisStackup s;
    s.outer_dielectric_height = mm(0.2);
    s.inner_plane_separation  = mm(0.4);
    s.copper_thickness        = mm(0.035);
    s.epsilon_r               = 4.4;
    s.tan_delta               = 0.02;
    s.sigma_copper            = 5.8e7;
    s.from_real_stackup       = false;

    auto narrow = compute_one(mm(0.10), 0, s);
    auto wide   = compute_one(mm(0.50), 0, s);
    REQUIRE(narrow.z0 > wide.z0);

    AnalysisStackup s_thick = s;
    s_thick.outer_dielectric_height = mm(1.0);
    auto thick = compute_one(mm(0.25), 0, s_thick);
    auto thin  = compute_one(mm(0.25), 0, s);
    REQUIRE(thick.z0 > thin.z0);

    AnalysisStackup s_lowK = s;
    s_lowK.epsilon_r = 2.5;   // PTFE-like
    auto low  = compute_one(mm(0.25), 0, s_lowK);
    auto high = compute_one(mm(0.25), 0, s);
    REQUIRE(low.z0 > high.z0);
}
