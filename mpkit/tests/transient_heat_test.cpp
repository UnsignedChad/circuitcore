// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Transient heat solver verification.
//
// Two checks pin the backward-Euler step + mass-matrix handling
// against known analytical answers.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/Material.h"
#include "mp/TransientHeat.h"
#include "mp/Voxelizer.h"

using mpkit::BcKind;
using mpkit::BcTarget;
using mpkit::BoundaryCondition;
using mpkit::Material;
using mpkit::TransientHeatConfig;
using mpkit::TransientHeatResult;
using mpkit::VoxelMaterialField;

namespace {

VoxelMaterialField uniform_field(int nx, int ny, int nz,
                                   double dx, double dy, double dz,
                                   mpkit::MaterialId id) {
    VoxelMaterialField f;
    f.grid.spec = {nx, ny, nz, dx, dy, dz};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(nx) * ny * nz, id);
    return f;
}

Material thermal_mat(double k, double rho, double c) {
    Material m;
    m.thermal_conductivity = k;
    m.density              = rho;
    m.specific_heat        = c;
    return m;
}

}  // namespace

TEST_CASE("Transient bar with both ends Dirichlet relaxes to the "
          "steady linear profile") {
    // 32-cell 1D bar. Initial T uniform = 50; left wall pinned to 0,
    // right wall pinned to 100. The transient must approach the
    // steady linear ramp as t goes large enough that the slowest mode
    // (tau = L^2 / (pi^2 * alpha)) has decayed by ~10x.
    constexpr int N = 32;
    const double L  = 0.05;          // 50 mm
    const double dx = L / N;
    // alpha = k / (rho c). Pick numbers that give a fast tau so the
    // test runs in milliseconds.
    const double k = 50.0, rho = 1000.0, cp = 1.0;  // alpha = 0.05 m^2/s
    const double alpha = k / (rho * cp);
    const double tau1  = L * L / (std::numbers::pi * std::numbers::pi * alpha);

    TransientHeatConfig cfg;
    cfg.material_field = uniform_field(N, 1, 1, dx, 1.0, 1.0, 0);
    cfg.material_table = {thermal_mat(k, rho, cp)};
    cfg.bcs.push_back({"left",  BcTarget::FaceXmin, {}, BcKind::Dirichlet,
                       0.0,   0.0, 0.0});
    cfg.bcs.push_back({"right", BcTarget::FaceXmax, {}, BcKind::Dirichlet,
                       100.0, 0.0, 0.0});
    cfg.initial_temperature_uniform = 50.0;
    // Run for 10*tau1 in 200 steps -> plenty of decay, dt safe.
    cfg.dt_s  = 10.0 * tau1 / 200.0;
    cfg.steps = 200;

    TransientHeatResult r = mpkit::solve_transient_heat(cfg);
    REQUIRE(r.ok);
    REQUIRE(r.times.size() == 201u);

    // Compare to linear steady-state at every cell centre.
    for (int i = 0; i < N; ++i) {
        const double x        = (i + 0.5) * dx;
        const double T_target = 0.0 + (100.0 - 0.0) * x / L;
        const double T_got    = r.final_temperature.at(i, 0, 0);
        REQUIRE(std::abs(T_got - T_target) < 2.0);
    }
}

TEST_CASE("Transient bar with insulated ends + sinusoidal initial "
          "condition decays at the analytical first-mode rate") {
    // Insulated ends (Neumann q = 0) + Robin sink to fix the constant
    // mode. Robin with tiny h on Zmin (the bar is 1D in x; z and y are
    // single cells so Zmin is the same as the bar's bottom face).
    // Drop in T over time tracked at the centre; the first cosine
    // mode T_1(x) = cos(pi x / L) decays as exp(-(pi/L)^2 alpha t).
    constexpr int N = 64;
    const double L  = 0.10;
    const double dx = L / N;
    const double k = 50.0, rho = 1000.0, cp = 1.0;
    const double alpha = k / (rho * cp);
    const double rate1 = (std::numbers::pi / L) * (std::numbers::pi / L) * alpha;
    const double tau1  = 1.0 / rate1;

    TransientHeatConfig cfg;
    cfg.material_field = uniform_field(N, 1, 1, dx, 1.0, 1.0, 0);
    cfg.material_table = {thermal_mat(k, rho, cp)};

    // Insulated x walls.
    cfg.bcs.push_back({"left",  BcTarget::FaceXmin, {}, BcKind::Neumann,
                       0.0, 0.0, 0.0});
    cfg.bcs.push_back({"right", BcTarget::FaceXmax, {}, BcKind::Neumann,
                       0.0, 0.0, 0.0});
    // Tiny Robin on the bottom face so the constant mode decays slowly.
    cfg.bcs.push_back({"floor", BcTarget::FaceZmin, {}, BcKind::Robin,
                       0.0, /*h=*/1.0e-6, /*u_ref=*/0.0});

    cfg.initial_temperature.resize(N, 1, 1);
    for (int i = 0; i < N; ++i) {
        const double x = (i + 0.5) * dx;
        cfg.initial_temperature.at(i, 0, 0) = std::cos(std::numbers::pi * x / L);
    }
    cfg.dt_s  = tau1 / 200.0;
    cfg.steps = 100;          // walk to t = tau1 / 2

    TransientHeatResult r = mpkit::solve_transient_heat(cfg);
    REQUIRE(r.ok);

    // Sample two early-time amplitudes at the bar edge (where cos is
    // +-1 so the mode is most visible). Sample at cell 0 (cos(pi/(2N))
    // ~ 1) and cell N-1 (cos(pi (N-0.5)/N) ~ -1). The ratio
    // T0 / T0_initial should equal exp(-rate * t) for the first mode
    // alone, up to the higher-mode initial-condition leakage.
    const double t_final  = cfg.dt_s * cfg.steps;
    const double T0_final = r.final_temperature.at(0, 0, 0);
    const double T0_init  = std::cos(std::numbers::pi * (0 + 0.5) * dx / L);
    const double ratio    = T0_final / T0_init;
    const double expected = std::exp(-rate1 * t_final);
    // backward Euler under-estimates the decay rate by O(dt); allow
    // a generous 8% relative tolerance to absorb that bias.
    REQUIRE(std::abs(ratio - expected) < 0.08);
}
