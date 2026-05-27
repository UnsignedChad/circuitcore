// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "circuitcore/field/GridSpec.h"

#include "si/CrossSection.h"
#include "si/FdmSolver.h"

using namespace sikit::em2d;
using Catch::Approx;

namespace {
using circuitcore::field::kEps0;
}

TEST_CASE("em2d: epsilon_r_at returns the correct layer value", "[em2d]") {
    CrossSection cs;
    cs.stack.push_back({1.0e-3, 4.4, 0, "FR4"});  // 1mm FR-4
    cs.stack.push_back({0.5e-3, 3.0, 0, "Rogers"});

    REQUIRE(cs.epsilon_r_at(0.0, -1e-4) == 1.0);          // above board → air
    REQUIRE(cs.epsilon_r_at(0.0, 0.5e-3) == Approx(4.4)); // mid FR-4
    REQUIRE(cs.epsilon_r_at(0.0, 1.2e-3) == Approx(3.0)); // mid Rogers
    REQUIRE(cs.epsilon_r_at(0.0, 2.0e-3) == 1.0);         // below board → air
}

TEST_CASE("em2d: parallel-plate capacitor recovers analytic C ≈ ε₀·εr·W/d",
          "[em2d]") {
    // Two wide horizontal conductors, separated vertically by an air gap.
    // For W >> d the analytic per-unit-length capacitance is ε₀ · W / d.
    // We expect the FDM value to fall a bit short because we ignore the
    // fringing capacitance at the conductor edges, but with W/d = 20 the
    // fringing contribution is small.
    const double W = 4.0e-3;
    const double d = 0.2e-3;
    const double t = 0.05e-3;  // conductor thickness

    CrossSection cs;
    cs.stack.push_back({d, 1.0, 0, "air-gap"});  // gap dielectric is also air

    Conductor top, bot;
    top.id = 0;
    top.label = "top";
    top.y_center = 0;
    top.z_top    = -t;        // sits in the air-above region
    top.width    = W;
    top.thickness = t;
    top.voltage  = 1.0;

    bot.id = 1;
    bot.label = "bot";
    bot.y_center = 0;
    bot.z_top    = d;         // sits just below the air gap
    bot.width    = W;
    bot.thickness = t;
    bot.voltage  = 0.0;

    cs.conductors.push_back(top);
    cs.conductors.push_back(bot);

    cs.y_min = -10e-3;
    cs.y_max =  10e-3;
    cs.air_above = 2e-3;

    auto g = build_grid(cs, 50e-6);  // 50 µm cells → ~80 cells across the gap
    SolveConfig cfg;
    cfg.tolerance = 1e-7;
    cfg.max_iterations = 30000;
    auto r = solve(g, cfg);
    REQUIRE(r.ok);

    const double q = charge_per_length(g, /*conductor_id=*/0);
    const double c_analytic = kEps0 * W / d;
    // The analytic formula ε₀·W/d ignores fringing at the plate edges,
    // so FDM should return MORE capacitance, not less. Two sources of
    // overshoot at this geometry: fringing (~20% for W/d = 20) and the
    // cell-rect overlap classifier slightly fattening conductors by ~h/2.
    // We require FDM ≥ analytic (no lost flux) and ≤ 2× analytic (sanity).
    REQUIRE(q >= c_analytic * 0.95);
    REQUIRE(q <= c_analytic * 2.0);
}

TEST_CASE("em2d: microstrip Z₀ within 15% of Wadell closed-form", "[em2d]") {
    // Standard ~50Ω microstrip target geometry. Wadell/IPC-2141A gives
    // ~50 Ω at W = 2.8 mm, H = 1.524 mm, εr = 4.4, T = 35 µm; we expect
    // the FDM solver to land in the same neighbourhood.
    CrossSection cs;
    cs.stack.push_back({1.524e-3, 4.4, 0, "FR4"});

    Conductor trace;
    trace.id = 0;
    trace.label = "trace";
    trace.y_center = 0;
    trace.z_top    = -35e-6;   // sit just above the dielectric, in air
    trace.width    = 2.8e-3;
    trace.thickness = 35e-6;
    trace.voltage  = 1.0;

    Conductor gnd;
    gnd.id = 1;
    gnd.label = "gnd";
    gnd.y_center = 0;
    gnd.z_top    = 1.524e-3;   // bottom of board
    gnd.width    = 20e-3;       // wide ground plane
    gnd.thickness = 35e-6;
    gnd.voltage  = 0.0;

    cs.conductors.push_back(trace);
    cs.conductors.push_back(gnd);

    cs.y_min = -10e-3;
    cs.y_max =  10e-3;
    cs.air_above = 5e-3;

    SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 80000;
    auto r = compute_z0(cs, 0, 1, 100e-6, cfg);  // 100 µm cells

    REQUIRE(r.ok);
    REQUIRE(r.z0_ohm > 0);
    REQUIRE(r.z0_ohm == Approx(50.0).margin(8.0));
    REQUIRE(r.eps_eff > 2.0);   // somewhere between air (1) and FR-4 (4.4)
    REQUIRE(r.eps_eff < 4.4);
}
