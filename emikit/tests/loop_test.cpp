// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "emi/LoopEmissions.h"

using emikit::emi::loop_e_field;
using emikit::emi::loop_e_field_dbuv;
using emikit::emi::loop_e_field_dbuv_sweep;
using Catch::Approx;

TEST_CASE("loop: zero area -> zero E", "[loop]") {
    REQUIRE(loop_e_field(0.0, 0.020, 1.0e9, 3.0) == Approx(0.0));
}

TEST_CASE("loop: zero current -> zero E", "[loop]") {
    REQUIRE(loop_e_field(1e-5, 0.0, 1.0e9, 3.0) == Approx(0.0));
}

TEST_CASE("loop: E scales as f^2", "[loop]") {
    const double e1 = loop_e_field(1e-5, 0.020, 100e6, 3.0);
    const double e2 = loop_e_field(1e-5, 0.020, 200e6, 3.0);
    REQUIRE(e2 / e1 == Approx(4.0).margin(1e-9));
}

TEST_CASE("loop: E scales linearly in area and current", "[loop]") {
    const double base = loop_e_field(1e-5, 0.020, 300e6, 3.0);
    REQUIRE(loop_e_field(2e-5, 0.020, 300e6, 3.0) == Approx(2 * base));
    REQUIRE(loop_e_field(1e-5, 0.040, 300e6, 3.0) == Approx(2 * base));
}

TEST_CASE("loop: E falls as 1/r", "[loop]") {
    const double e3  = loop_e_field(1e-5, 0.020, 300e6, 3.0);
    const double e10 = loop_e_field(1e-5, 0.020, 300e6, 10.0);
    REQUIRE(e3 / e10 == Approx(10.0 / 3.0).margin(1e-6));
}

TEST_CASE("loop: dBuV conversion is consistent with V/m result",
          "[loop]") {
    const double e_v  = loop_e_field(1e-5, 0.020, 300e6, 3.0);
    const double e_db = loop_e_field_dbuv(1e-5, 0.020, 300e6, 3.0);
    // dBuV = 20*log10(V/m * 1e6)
    const double expect = 20.0 * std::log10(e_v * 1e6);
    REQUIRE(e_db == Approx(expect));
}

TEST_CASE("loop: sweep returns one value per freq", "[loop]") {
    auto e = loop_e_field_dbuv_sweep(
        1e-5, {30e6, 100e6, 300e6, 1e9},
        {0.010, 0.010, 0.010, 0.005}, 3.0);
    REQUIRE(e.size() == 4);
}

TEST_CASE("loop: sweep size mismatch throws", "[loop]") {
    REQUIRE_THROWS(loop_e_field_dbuv_sweep(
        1e-5, {30e6, 100e6}, {0.010}, 3.0));
}
