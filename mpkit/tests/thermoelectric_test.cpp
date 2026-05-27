// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Thermoelectric primitives sanity tests.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "mp/MaterialLibrary.h"
#include "mp/Thermoelectric.h"

using mpkit::Material;
namespace tt = mpkit::thermo;

TEST_CASE("Copper has the expected ~+1.8 uV/K room-temperature Seebeck") {
    auto cu = mpkit::copper();
    REQUIRE(!std::isnan(cu.seebeck_coefficient));
    REQUIRE(cu.seebeck_coefficient > 1.0e-6);
    REQUIRE(cu.seebeck_coefficient < 3.0e-6);
}

TEST_CASE("Type-K thermocouple (chromel / alumel) recovers a sensitivity "
          "near the published ~41 uV/K average") {
    auto chromel = mpkit::chromel();
    auto alumel  = mpkit::alumel();
    const double t_hot  = 100.0;        // any reference, only delta matters
    const double t_cold = 0.0;
    const double v = tt::thermocouple_emf(chromel, alumel, t_hot, t_cold);
    // (S_chromel - S_alumel) * 100 K -> 43.7 mV theoretical from our
    // constant-S model. Published Type-K is 4.10 mV at 100 C (linearised
    // 41 uV/K), our value is 4.37 mV -- within the ~7% systematic error
    // a constant-S model carries against the true temperature-dependent
    // table.
    REQUIRE(std::abs(v - 4.37e-3) < 0.1e-3);
}

TEST_CASE("Seebeck EMF along a path is independent of intermediate "
          "uniform segments (Magnus principle)") {
    // A homogeneous wire of length 10 between T=0 and T=100 has the same
    // EMF as a single segment spanning the same endpoints.
    auto cu = mpkit::copper();
    const Material* M = &cu;
    std::vector<const Material*> path_one  = {M};
    std::vector<double>          temps_one = {0.0, 100.0};
    std::vector<const Material*> path_many = {M, M, M, M, M};
    std::vector<double>          temps_many = {0.0, 25.0, 50.0, 75.0, 90.0, 100.0};
    const double v1 = tt::seebeck_emf_1d(path_one,  temps_one);
    const double v2 = tt::seebeck_emf_1d(path_many, temps_many);
    REQUIRE(std::abs(v1 - v2) < 1.0e-12);
    // And both equal S * 100.
    REQUIRE(std::abs(v1 - cu.seebeck_coefficient * 100.0) < 1.0e-12);
}

TEST_CASE("Closed thermocouple loop -- hot junction at T_h, cold ends "
          "at T_c -- gives (S_a - S_b)*(T_h - T_c)") {
    auto a = mpkit::chromel();
    auto b = mpkit::alumel();
    // Wire-A from cold to hot, then wire-B from hot back to cold.
    std::vector<const Material*> path = {&a, &a, &b, &b};
    std::vector<double> temps = {0.0, 50.0, 100.0, 50.0, 0.0};
    const double v_loop = tt::seebeck_emf_1d(path, temps);
    const double v_th   = tt::thermocouple_emf(a, b, 100.0, 0.0);
    REQUIRE(std::abs(v_loop - v_th) < 1.0e-12);
}

TEST_CASE("Peltier coefficient Pi = S * T at the given absolute temperature") {
    auto cu = mpkit::copper();
    const double T = 300.0;
    REQUIRE(std::abs(tt::peltier_coefficient(cu, T)
                      - cu.seebeck_coefficient * T) < 1.0e-15);
}

TEST_CASE("Peltier junction flux uses Pi_a - Pi_b times the normal current "
          "density and flips sign with current direction") {
    auto cu = mpkit::copper();
    auto bi = mpkit::bismuth();
    const double T  = 300.0;
    const double j  = 1.0e5;             // A/m^2
    const double q  = tt::peltier_junction_flux(cu, bi, T, +j);
    const double q_neg = tt::peltier_junction_flux(cu, bi, T, -j);
    REQUIRE(std::abs(q + q_neg) < 1.0e-15);
    // Magnitude: (Pi_cu - Pi_bi) * J. With Pi_cu = +1.83e-6 * 300 ~ 549 uV
    // and Pi_bi = -72e-6 * 300 = -21.6 mV, the difference is ~22.15 mV.
    // Times 1e5 A/m^2 -> ~2215 W/m^2.
    REQUIRE(q > 2.0e3);
    REQUIRE(q < 2.5e3);
}

TEST_CASE("Thomson volumetric heat returns zero everywhere under v1's "
          "constant-Seebeck assumption (placeholder API)") {
    mpkit::VoxelMaterialField vmf;
    vmf.grid.spec = {2, 2, 1, 1.0e-3, 1.0e-3, 1.0e-3};
    vmf.grid.x0 = vmf.grid.y0 = vmf.grid.z0 = 0.0;
    vmf.ids.assign(4, 0);
    std::vector<Material> mt = {mpkit::copper()};

    circuitcore::field::Field3D T(2, 2, 1); T.fill(310.0);
    circuitcore::field::Field3D jx(2, 2, 1); jx.fill(1.0e5);
    circuitcore::field::Field3D jy(2, 2, 1); jy.fill(0.0);
    circuitcore::field::Field3D jz(2, 2, 1); jz.fill(0.0);
    auto q = tt::thomson_volumetric_heat(vmf, mt, T, jx, jy, jz);
    REQUIRE(q.nx() == 2);
    REQUIRE(q.ny() == 2);
    REQUIRE(q.nz() == 1);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
            REQUIRE(q.at(i, j, 0) == 0.0);
}

TEST_CASE("New library materials are reachable through material_by_name") {
    for (const char* name : {"iron", "chromel", "alumel", "constantan", "bismuth"}) {
        auto m = mpkit::material_by_name(name);
        REQUIRE(m.name == name);
        REQUIRE(!std::isnan(m.seebeck_coefficient));
    }
}
