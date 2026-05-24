#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/TargetZ.h"

using pdnkit::pi::TargetZSpec;
using pdnkit::pi::target_impedance_flat;
using Catch::Approx;

// "3.3V +/-5 percent, 1A step" -> 165 mOhm. This is the canonical
// worked example from Larry Smith's PDN tutorials.
TEST_CASE("target-z: 3.3V 5pct 1A -> 165 mOhm", "[target-z][validation]") {
    TargetZSpec s{3.3, 0.05, 1.0};
    REQUIRE(target_impedance_flat(s) == Approx(0.165).epsilon(1e-9));
}

// Doubling the step current halves the target.
TEST_CASE("target-z: tighter step -> tighter target", "[target-z]") {
    TargetZSpec a{1.0, 0.03, 0.5};   // 60 mOhm
    TargetZSpec b{1.0, 0.03, 5.0};   // 6 mOhm
    REQUIRE(target_impedance_flat(a) == Approx(0.060));
    REQUIRE(target_impedance_flat(b) == Approx(0.006));
    REQUIRE(target_impedance_flat(b) ==
            Approx(target_impedance_flat(a) / 10.0));
}

// A modern core rail: 0.9V +/-3 percent, 50A step -> 0.54 mOhm.
TEST_CASE("target-z: 0.9V core rail at high current", "[target-z]") {
    TargetZSpec s{0.9, 0.03, 50.0};
    REQUIRE(target_impedance_flat(s) == Approx(0.00054).epsilon(1e-9));
}

// Edge case: zero current -> no constraint -> 0 returned.
TEST_CASE("target-z: zero current step returns 0", "[target-z]") {
    TargetZSpec s{3.3, 0.05, 0.0};
    REQUIRE(target_impedance_flat(s) == 0.0);
}
