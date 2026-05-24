#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "emi/Masks.h"

using namespace emikit::emi;
using Catch::Approx;

TEST_CASE("masks: CISPR 32 Class B step at 230 MHz", "[masks]") {
    const auto& m = cispr32_class_b();
    // Below 230 MHz: 40 dBuV/m. Above: 47 dBuV/m.
    REQUIRE(limit_at(m, 100e6) == Approx(40.0));
    REQUIRE(limit_at(m, 500e6) == Approx(47.0));
}

TEST_CASE("masks: CISPR 32 Class A is looser than Class B in upper band",
          "[masks]") {
    REQUIRE(limit_at(cispr32_class_a(), 2e9) > limit_at(cispr32_class_b(), 2e9));
}

TEST_CASE("masks: FCC Part 15 Class B has its own breakpoints",
          "[masks]") {
    const auto& m = fcc_part15_class_b();
    REQUIRE(limit_at(m,  50e6) == Approx(40.0));    // 30-88 MHz band
    REQUIRE(limit_at(m, 100e6) == Approx(43.5));    // 88-216
    REQUIRE(limit_at(m, 500e6) == Approx(46.0));    // 216-960
    REQUIRE(limit_at(m, 980e6) == Approx(54.0));    // above 960
}

TEST_CASE("masks: limit_at clamps at endpoints", "[masks]") {
    const auto& m = cispr32_class_b();
    // Below 30 MHz lower bound -> snaps to first point.
    REQUIRE(limit_at(m, 1e6) == limit_at(m, 30e6));
    // Above last point -> snaps to last.
    REQUIRE(limit_at(m, 1e11) == limit_at(m, 6e9));
}

TEST_CASE("masks: margin_db sign convention", "[masks]") {
    const auto& m = cispr32_class_b();   // 40 dBuV at 100 MHz
    REQUIRE(margin_db(m, 100e6, 30.0) == Approx(10.0));   // 10 dB under
    REQUIRE(margin_db(m, 100e6, 50.0) == Approx(-10.0));  // 10 dB over (fail)
}

TEST_CASE("masks: registry lookup", "[masks]") {
    REQUIRE(mask_by_name("CISPR 32 Class B (3 m)") == &cispr32_class_b());
    REQUIRE(mask_by_name("nope") == nullptr);
    REQUIRE(all_masks().size() == 6);
}

TEST_CASE("masks: every mask carries a usable shape", "[masks]") {
    for (const auto* m : all_masks()) {
        REQUIRE_FALSE(m->name.empty());
        REQUIRE_FALSE(m->family.empty());
        REQUIRE(m->test_distance_m > 0.0);
        REQUIRE_FALSE(m->points.empty());
        REQUIRE_FALSE(m->source.empty());
    }
}
