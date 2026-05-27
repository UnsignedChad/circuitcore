// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/Ibis.h"

using namespace sikit::ibis;
using Catch::Approx;

constexpr auto kSample = R"(
[IBIS Ver]   5.0
[File Name]  sample.ibs
[Date]       2024/01/01

[Component] DRV01
[Manufacturer] Acme

[Model] DRAM_OUT
Model_type   Output
C_comp       3.0pF   2.5pF   3.5pF
Voltage_range  3.3   3.0   3.6

[Pulldown]
| Voltage  I(typ)   I(min)   I(max)
  -1.0    -0.020   -0.018   -0.022
   0.0     0.000    0.000    0.000
   1.0     0.050    0.045    0.055
   2.0     0.100    0.090    0.110

[Pullup]
  0.0    -0.100   -0.090   -0.110
  1.0    -0.050   -0.045   -0.055
  2.0     0.000    0.000    0.000

[Ramp]
dV/dt_r      2.0/200p   1.5/250p   2.5/150p
dV/dt_f      2.0/200p   1.5/250p   2.5/150p

[Model] DRAM_IN
Model_type   Input
C_comp       1.5pF   1.2pF   1.8pF

[End]
)";

TEST_CASE("ibis: top-level fields parsed", "[ibis]") {
    auto f = IbisReader::read_string(kSample);
    REQUIRE(f.version == "5.0");
    REQUIRE(f.component == "DRV01");
    REQUIRE(f.manufacturer == "Acme");
    REQUIRE(f.models.size() == 2);
}

TEST_CASE("ibis: model type and C_comp parsed", "[ibis]") {
    auto f = IbisReader::read_string(kSample);
    REQUIRE(f.models[0].name == "DRAM_OUT");
    REQUIRE(f.models[0].type == ModelType::Output);
    REQUIRE(f.models[0].c_comp.typ == Approx(3.0e-12));
    REQUIRE(f.models[0].c_comp.min == Approx(2.5e-12));
    REQUIRE(f.models[0].c_comp.max == Approx(3.5e-12));

    REQUIRE(f.models[1].name == "DRAM_IN");
    REQUIRE(f.models[1].type == ModelType::Input);
    REQUIRE(f.models[1].c_comp.typ == Approx(1.5e-12));
}

TEST_CASE("ibis: pulldown V/I table parsed", "[ibis]") {
    auto f = IbisReader::read_string(kSample);
    const auto& pd = f.models[0].pulldown;
    REQUIRE(pd.size() == 4);
    REQUIRE(pd[0].voltage == Approx(-1.0));
    REQUIRE(pd[0].i_typ   == Approx(-0.020));
    REQUIRE(pd[2].voltage == Approx(1.0));
    REQUIRE(pd[2].i_min   == Approx(0.045));
    REQUIRE(pd[2].i_max   == Approx(0.055));
}

TEST_CASE("ibis: pullup V/I table parsed", "[ibis]") {
    auto f = IbisReader::read_string(kSample);
    const auto& pu = f.models[0].pullup;
    REQUIRE(pu.size() == 3);
    REQUIRE(pu[0].voltage == Approx(0.0));
    REQUIRE(pu[0].i_typ   == Approx(-0.100));
}

TEST_CASE("ibis: ramp dV/dt parsed", "[ibis]") {
    auto f = IbisReader::read_string(kSample);
    const auto& r = f.models[0].ramp;
    REQUIRE(r.dv_rise.typ == Approx(2.0));
    REQUIRE(r.dt_rise.typ == Approx(200e-12));
    REQUIRE(r.dv_rise.min == Approx(1.5));
    REQUIRE(r.dt_rise.min == Approx(250e-12));
    REQUIRE(r.dv_rise.max == Approx(2.5));
    REQUIRE(r.dt_rise.max == Approx(150e-12));
    REQUIRE(r.dv_fall.typ == Approx(2.0));
}

TEST_CASE("ibis: NA tokens parse as NaN", "[ibis]") {
    constexpr auto src = R"(
[Component] x
[Model] M1
Model_type Output
C_comp  1.0pF  NA  NA
[Pulldown]
  0.0  0.0  NA  NA
  1.0  0.05 NA  NA
[End]
)";
    auto f = IbisReader::read_string(src);
    REQUIRE(std::isnan(f.models[0].c_comp.min));
    REQUIRE(std::isnan(f.models[0].c_comp.max));
    REQUIRE(std::isnan(f.models[0].pulldown[0].i_min));
}

TEST_CASE("ibis: unit suffixes (p/n/u/m/k/M/G)", "[ibis]") {
    // Spot-check the engineering-prefix scaling. Hand-crafted minimal
    // input that puts these in a real keyword context.
    constexpr auto src = R"(
[Component] x
[Model] M1
Model_type Output
C_comp  5.0p   3n   2u
[End]
)";
    auto f = IbisReader::read_string(src);
    REQUIRE(f.models[0].c_comp.typ == Approx(5.0e-12));
    REQUIRE(f.models[0].c_comp.min == Approx(3.0e-9));
    REQUIRE(f.models[0].c_comp.max == Approx(2.0e-6));
}

TEST_CASE("ibis: model_type aliases recognized", "[ibis]") {
    constexpr auto src = R"(
[Component] x
[Model] tri
Model_type  3-state
[Model] io_pin
Model_type  I/O
[Model] od
Model_type  Open_drain
[Model] term
Model_type  Terminator
[End]
)";
    auto f = IbisReader::read_string(src);
    REQUIRE(f.models.size() == 4);
    REQUIRE(f.models[0].type == ModelType::Tristate);
    REQUIRE(f.models[1].type == ModelType::IO);
    REQUIRE(f.models[2].type == ModelType::OpenDrain);
    REQUIRE(f.models[3].type == ModelType::Terminator);
}

TEST_CASE("ibis: pipe comments stripped", "[ibis]") {
    constexpr auto src = R"(
| File preamble comment
[Component] x   | inline comment after value
[Model] M1
Model_type Output
| comment line
C_comp 1pF | trailing
[End]
)";
    auto f = IbisReader::read_string(src);
    REQUIRE(f.component == "x");
    REQUIRE(f.models[0].c_comp.typ == Approx(1e-12));
}

TEST_CASE("ibis: missing file throws", "[ibis]") {
    REQUIRE_THROWS_AS(IbisReader::read_file("/nonexistent/path.ibs"),
                      IbisParseError);
}
