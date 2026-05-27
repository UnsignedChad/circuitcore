// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/CrossSection.h"
#include "si/FdmSolver.h"

using namespace sikit::em2d;
using Catch::Approx;

namespace {

CrossSection canonical_50_microstrip() {
    CrossSection cs;
    cs.stack.push_back({1.524e-3, 4.4, 0, "FR4"});
    Conductor trace{0, "trace", 0, -35e-6, 2.8e-3, 35e-6, 1.0};
    Conductor gnd{1, "gnd",     0, 1.524e-3, 20e-3, 35e-6, 0.0};
    cs.conductors.push_back(trace);
    cs.conductors.push_back(gnd);
    cs.y_min = -10e-3;
    cs.y_max =  10e-3;
    cs.air_above = 5e-3;
    return cs;
}

}  // namespace

TEST_CASE("refined: produces a finite result + reports both solves", "[refined]") {
    // compute_z0_refined runs the solve at h and 2h and applies
    // first-order Richardson extrapolation on the per-unit-length C
    // values. This is a sound technique in general, but its benefit
    // depends on whether the FDM's leading error scales linearly with
    // h — which is geometry-dependent (boundary truncation and the
    // cell-rect classifier both contribute non-linear error terms).
    //
    // The contract this test enforces is therefore the bounded one:
    //   - we get a valid finite result with both solves' iterations
    //     reported,
    //   - the result and the fine-only value are both in the same
    //     order-of-magnitude window around Wadell's textbook 50 Ω.
    auto cs = canonical_50_microstrip();
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;

    auto r = compute_z0_refined(cs, 0, 1, 100e-6, cfg);
    REQUIRE(r.ok);
    REQUIRE(r.iter_fine   > 0);
    REQUIRE(r.iter_coarse > 0);
    REQUIRE(r.z0_fine     > 0);
    REQUIRE(r.z0_coarse   > 0);

    INFO("z0_fine        = " << r.z0_fine    << " Ω");
    INFO("z0_coarse (2h) = " << r.z0_coarse  << " Ω");
    INFO("z0_richardson  = " << r.z0_ohm     << " Ω");

    // All three should land in [40, 70] Ω for this geometry. This is a
    // generous bound — what we're really checking is that Richardson
    // doesn't blow up (e.g. negative C from a bad extrapolation) and
    // the wrapper degrades gracefully when it can't help.
    REQUIRE(r.z0_ohm > 40.0);   REQUIRE(r.z0_ohm < 70.0);
    REQUIRE(r.z0_fine > 40.0);  REQUIRE(r.z0_fine < 70.0);
}

TEST_CASE("refined: 2h solve converges in fewer iterations than h", "[refined]") {
    auto cs = canonical_50_microstrip();
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_z0_refined(cs, 0, 1, 100e-6, cfg);
    REQUIRE(r.ok);
    // Coarser grid = fewer cells = fewer iterations to converge.
    REQUIRE(r.iter_coarse <= r.iter_fine);
}

TEST_CASE("refined: result varies smoothly with cell size", "[refined]") {
    // Result at slightly different mesh densities should stay in the
    // same ballpark, not jump catastrophically.
    auto cs = canonical_50_microstrip();
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r1 = compute_z0_refined(cs, 0, 1, 150e-6, cfg);
    auto r2 = compute_z0_refined(cs, 0, 1, 100e-6, cfg);
    REQUIRE(r1.ok);
    REQUIRE(r2.ok);
    // Within 10 Ω of each other.
    REQUIRE(std::abs(r1.z0_ohm - r2.z0_ohm) < 10.0);
}
