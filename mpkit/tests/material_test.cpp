#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "mp/MaterialLibrary.h"

using mpkit::Material;

TEST_CASE("Copper has plausible engineering numbers") {
    Material c = mpkit::copper();
    REQUIRE(c.name == "copper");
    REQUIRE(c.thermal_conductivity > 350.0);
    REQUIRE(c.thermal_conductivity < 450.0);
    REQUIRE(c.density > 8000.0);
    REQUIRE(c.electrical_resistivity > 1.0e-8);
    REQUIRE(c.electrical_resistivity < 3.0e-8);
}

TEST_CASE("Air properties present and not copper") {
    Material a = mpkit::air();
    REQUIRE(a.thermal_conductivity < 0.1);
    REQUIRE(a.density < 2.0);
    REQUIRE(std::isnan(a.youngs_modulus));  // air has no mechanical props
}

TEST_CASE("material_by_name is case-insensitive + recognises aliases") {
    REQUIRE(mpkit::material_by_name("copper").thermal_conductivity > 350.0);
    REQUIRE(mpkit::material_by_name("COPPER").thermal_conductivity > 350.0);
    REQUIRE(mpkit::material_by_name("aluminum").density > 2000.0);
    REQUIRE(mpkit::material_by_name("aluminium").density > 2000.0);
    REQUIRE_THROWS_AS(mpkit::material_by_name("unobtanium"), std::out_of_range);
}

TEST_CASE("material_names lists the canonical set without alias duplicates") {
    auto names = mpkit::material_names();
    REQUIRE(std::find(names.begin(), names.end(), "copper") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "fr4")    != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "air")    != names.end());
    // aluminium should be present but the US alias should not duplicate it.
    REQUIRE(std::count(names.begin(), names.end(), "aluminium") == 1);
    REQUIRE(std::find(names.begin(), names.end(), "aluminum") == names.end());
    REQUIRE(std::is_sorted(names.begin(), names.end()));
}
