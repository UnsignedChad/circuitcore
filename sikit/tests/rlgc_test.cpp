// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/CrossSection.h"
#include "si/FdmSolver.h"
#include "si/Rlgc.h"

using namespace sikit::em2d;
using Catch::Approx;

namespace {

// Two parallel signal traces over a wide ground plane. Single dielectric
// layer beneath. Coarse cell size to keep the test under a second.
CrossSection two_coupled_microstrip(double spacing) {
    CrossSection cs;
    cs.stack.push_back({1.0e-3, 4.4, 0, "FR4"});

    Conductor t_a{0, "agg",    -spacing,         -35e-6, 0.5e-3, 35e-6, 0.0};
    Conductor t_b{1, "victim", +spacing,         -35e-6, 0.5e-3, 35e-6, 0.0};
    Conductor gnd{2, "gnd",     0,                1.0e-3, 10e-3,  35e-6, 0.0};

    cs.conductors.push_back(t_a);
    cs.conductors.push_back(t_b);
    cs.conductors.push_back(gnd);

    cs.y_min = -8e-3;
    cs.y_max =  8e-3;
    cs.air_above = 3e-3;
    return cs;
}

}  // namespace

TEST_CASE("rlgc: 2-conductor microstrip — C is positive on diagonal, negative off",
          "[rlgc]") {
    auto cs = two_coupled_microstrip(0.6e-3);  // 0.6mm centre-to-centre
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_rlgc(cs, /*signals=*/{0, 1}, /*gnd=*/2, /*h=*/200e-6, cfg);

    REQUIRE(r.ok);
    REQUIRE(r.n == 2);
    REQUIRE(r.C(0, 0) > 0.0);
    REQUIRE(r.C(1, 1) > 0.0);
    REQUIRE(r.C(0, 1) < 0.0);
    REQUIRE(r.C(1, 0) < 0.0);
    // |off-diagonal| < diagonal (mutual cap is smaller than self cap).
    REQUIRE(std::abs(r.C(0, 1)) < r.C(0, 0));
}

TEST_CASE("rlgc: C matrix is approximately symmetric", "[rlgc]") {
    auto cs = two_coupled_microstrip(0.6e-3);
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_rlgc(cs, {0, 1}, 2, 200e-6, cfg);
    REQUIRE(r.ok);
    // FDM with cell-rect classifier introduces a few % asymmetry; loose.
    const double avg = 0.5 * (std::abs(r.C(0, 1)) + std::abs(r.C(1, 0)));
    REQUIRE(std::abs(r.C(0, 1) - r.C(1, 0)) / avg < 0.10);
}

TEST_CASE("rlgc: L self-inductance is positive", "[rlgc]") {
    auto cs = two_coupled_microstrip(0.6e-3);
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_rlgc(cs, {0, 1}, 2, 200e-6, cfg);
    REQUIRE(r.ok);
    REQUIRE(r.L(0, 0) > 0.0);
    REQUIRE(r.L(1, 1) > 0.0);
}

TEST_CASE("rlgc: closer spacing → larger NEXT", "[rlgc]") {
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;

    auto cs_far  = two_coupled_microstrip(1.2e-3);
    auto cs_near = two_coupled_microstrip(0.4e-3);

    auto r_far  = compute_rlgc(cs_far,  {0, 1}, 2, 200e-6, cfg);
    auto r_near = compute_rlgc(cs_near, {0, 1}, 2, 200e-6, cfg);
    REQUIRE(r_far.ok);
    REQUIRE(r_near.ok);

    auto ct_far  = crosstalk_for_pair(r_far,  0, 1);
    auto ct_near = crosstalk_for_pair(r_near, 0, 1);

    // For inhomogeneous TEM (microstrip), modal mismatch κ > 0 and
    // grows as conductors get closer (mutual L grows faster than mutual C).
    REQUIRE(std::abs(ct_near.k_near_end) > std::abs(ct_far.k_near_end));
}

TEST_CASE("rlgc: same conductor crosstalk returns zero", "[rlgc]") {
    auto cs = two_coupled_microstrip(0.6e-3);
    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_rlgc(cs, {0, 1}, 2, 200e-6, cfg);
    REQUIRE(r.ok);
    auto ct = crosstalk_for_pair(r, 0, 0);  // aggressor == victim
    REQUIRE(ct.k_near_end == 0.0);
    REQUIRE(ct.modal_mismatch == 0.0);
}

TEST_CASE("rlgc: 3-signal-conductor matrix is 3×3", "[rlgc]") {
    // Just a structural test — three traces side by side, verify the
    // returned matrices have the right shape.
    CrossSection cs;
    cs.stack.push_back({1.0e-3, 4.4, 0, ""});
    cs.conductors.push_back({0, "a",  -1.0e-3, -35e-6, 0.4e-3, 35e-6, 0.0});
    cs.conductors.push_back({1, "b",   0.0,    -35e-6, 0.4e-3, 35e-6, 0.0});
    cs.conductors.push_back({2, "c",   1.0e-3, -35e-6, 0.4e-3, 35e-6, 0.0});
    cs.conductors.push_back({3, "gnd", 0,       1.0e-3, 12e-3,  35e-6, 0.0});
    cs.y_min = -8e-3;
    cs.y_max =  8e-3;
    cs.air_above = 3e-3;

    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_rlgc(cs, {0, 1, 2}, 3, 250e-6, cfg);
    REQUIRE(r.ok);
    REQUIRE(r.C.rows() == 3);
    REQUIRE(r.C.cols() == 3);
    REQUIRE(r.L.rows() == 3);
    REQUIRE(r.L.cols() == 3);
}
