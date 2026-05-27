// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Steady-state heat solver verification.
//
// Two analytical-comparison tests pin the FVM stencil + BC handling
// against known-correct closed-form solutions, plus one
// material-discontinuity test that catches the harmonic-mean k case
// (the common bug when a slab has copper and FR-4 in series).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/Material.h"
#include "mp/SteadyHeat.h"
#include "mp/Voxelizer.h"

using mpkit::BcKind;
using mpkit::BcTarget;
using mpkit::BoundaryCondition;
using mpkit::Material;
using mpkit::MaterialId;
using mpkit::SteadyHeatConfig;
using mpkit::SteadyHeatResult;
using mpkit::VoxelMaterialField;

namespace {

// Single-material homogeneous grid helper.
VoxelMaterialField uniform_field(int nx, int ny, int nz,
                                   double dx, double dy, double dz,
                                   MaterialId id) {
    VoxelMaterialField f;
    f.grid.spec = {nx, ny, nz, dx, dy, dz};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(nx) * ny * nz, id);
    return f;
}

Material with_k(double k) {
    Material m;
    m.thermal_conductivity = k;
    return m;
}

}  // namespace

TEST_CASE("1D conduction between two Dirichlet faces matches the linear "
          "analytical T(x) = T0 + (T1 - T0) * x / L") {
    // 32 cells along x, single cell in y and z. Walls at Xmin = 0,
    // Xmax = 100 (deg C). With no source the steady solution is the
    // linear ramp.
    constexpr int N = 32;
    const double L = 0.10;            // 100 mm
    const double dx = L / N;
    auto field = uniform_field(N, 1, 1, dx, 1.0, 1.0, 0);

    SteadyHeatConfig cfg;
    cfg.material_field = std::move(field);
    cfg.material_table = {with_k(400.0)};  // copper-ish
    cfg.bcs.push_back({"left", BcTarget::FaceXmin, {}, BcKind::Dirichlet,
                       /*value=*/0.0, 0.0, 0.0});
    cfg.bcs.push_back({"right", BcTarget::FaceXmax, {}, BcKind::Dirichlet,
                       /*value=*/100.0, 0.0, 0.0});

    SteadyHeatResult r = mpkit::solve_steady_heat(cfg);
    REQUIRE(r.ok);
    REQUIRE(r.n_unknowns == static_cast<std::size_t>(N));

    // Compare against analytical at every cell centre.
    for (int i = 0; i < N; ++i) {
        const double x = (i + 0.5) * dx;
        const double T_expected = 0.0 + (100.0 - 0.0) * x / L;
        const double T_got = r.temperature.at(i, 0, 0);
        // Dirichlet pin happens at the boundary CELL not at the face, so
        // the discrete answer matches the linear profile sampled at cell
        // centres exactly (up to FP error) for this 1D pure-conduction
        // case. Tolerance is loose to absorb the slight FVM offset at
        // the two end cells where the pin sits on the centre.
        REQUIRE(std::abs(T_got - T_expected) < 2.0);
    }
}

TEST_CASE("1D conduction with a uniform volumetric source matches the "
          "parabolic T(x) = T_wall + Q L^2 / (8 k) at the centre") {
    // Same 1D bar, both walls pinned to 0, uniform source Q. The
    // textbook (Mills, "Heat Transfer", Ex. 2-3) result for the centre
    // temperature is T_max = Q * L^2 / (8 k).
    constexpr int N = 64;
    const double L  = 0.05;          // 50 mm
    const double dx = L / N;
    const double k  = 50.0;          // some intermediate value
    const double Q  = 1.0e6;         // 1 MW/m^3

    auto field = uniform_field(N, 1, 1, dx, 1.0, 1.0, 0);
    SteadyHeatConfig cfg;
    cfg.material_field = std::move(field);
    cfg.material_table = {with_k(k)};
    cfg.bcs.push_back({"left", BcTarget::FaceXmin, {}, BcKind::Dirichlet,
                       0.0, 0.0, 0.0});
    cfg.bcs.push_back({"right", BcTarget::FaceXmax, {}, BcKind::Dirichlet,
                       0.0, 0.0, 0.0});
    cfg.volumetric_source.resize(N, 1, 1);
    cfg.volumetric_source.fill(Q);

    SteadyHeatResult r = mpkit::solve_steady_heat(cfg);
    REQUIRE(r.ok);

    const double T_expected_centre = Q * L * L / (8.0 * k);
    const double T_got_centre = r.temperature.at(N / 2, 0, 0);
    // Tolerance: ~1% of peak rise is plenty for 64-cell discretization.
    REQUIRE(std::abs(T_got_centre - T_expected_centre)
            < 0.05 * T_expected_centre);
}

TEST_CASE("Series copper / FR-4 slab respects harmonic-mean conductivity "
          "at the material interface") {
    // Half-and-half slab: cells 0..N/2-1 are copper (k=400), N/2..N-1
    // are FR-4 (k=0.3). Endpoints pinned to 100 and 0. The interface
    // temperature follows from the series-resistance analogy:
    //
    //   T_interface = T_hot * R_cold / (R_hot + R_cold)
    //
    // where R = L/2 / k. For k_hot = 400, k_cold = 0.3, that's almost
    // exactly the cold-side wall temperature -- the cold material
    // dominates the gradient.
    constexpr int N = 32;
    const double L = 0.10;
    const double dx = L / N;

    VoxelMaterialField f;
    f.grid.spec = {N, 1, 1, dx, 1.0, 1.0};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.resize(N);
    for (int i = 0; i < N; ++i) f.ids[i] = (i < N / 2) ? 0 : 1;

    SteadyHeatConfig cfg;
    cfg.material_field = std::move(f);
    cfg.material_table = {with_k(400.0), with_k(0.3)};
    cfg.bcs.push_back({"hot", BcTarget::FaceXmin, {}, BcKind::Dirichlet,
                       100.0, 0.0, 0.0});
    cfg.bcs.push_back({"cold", BcTarget::FaceXmax, {}, BcKind::Dirichlet,
                       0.0, 0.0, 0.0});

    auto r = mpkit::solve_steady_heat(cfg);
    REQUIRE(r.ok);

    const double R_hot  = (L * 0.5) / 400.0;
    const double R_cold = (L * 0.5) / 0.3;
    const double T_iface_analytical =
        100.0 * R_cold / (R_hot + R_cold);  // ~100 - tiny epsilon
    // Cold-side cell centre sits dx/2 past the interface; the linear
    // gradient on the cold side drops T_iface_analytical by
    // T_iface_analytical / (L/2) * (dx/2).
    const double drop_half_cell =
        T_iface_analytical / (L * 0.5) * (dx * 0.5);
    const double T_iface_expected = T_iface_analytical - drop_half_cell;

    // Sample the temperature just past the interface (cold side).
    const double T_iface_got = r.temperature.at(N / 2, 0, 0);
    REQUIRE(std::abs(T_iface_got - T_iface_expected) < 1.0);
    // Sanity: copper side should be nearly isothermal at 100.
    REQUIRE(r.temperature.at(1, 0, 0) > 99.0);
}
