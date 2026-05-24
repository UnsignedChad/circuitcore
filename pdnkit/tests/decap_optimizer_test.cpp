#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "pi/CavityModel.h"
#include "pi/DecapOptimizer.h"

using pdnkit::pi::CavityConfig;
using pdnkit::pi::DecapOptimizerConfig;
using pdnkit::pi::optimize_decaps;

namespace {

CavityConfig small_plane() {
    CavityConfig c;
    c.a = 0.080;
    c.b = 0.060;
    c.d = 1.6e-3;
    c.eps_r = 4.3;
    c.tan_delta = 0.020;
    c.max_modes = 15;  // small for test speed
    return c;
}

}  // namespace

TEST_CASE("optimizer: easy target requires no caps", "[optimizer]") {
    auto cfg = small_plane();
    DecapOptimizerConfig opt;
    opt.target_z = 1.0e6;  // absurdly easy
    opt.f_min = 1.0e6;
    opt.f_max = 1.0e7;
    opt.n_points = 10;
    opt.max_caps = 5;
    opt.cap_x = 0.020;
    opt.cap_y = 0.020;

    auto result = optimize_decaps(cfg, 0.040, 0.030, opt);
    REQUIRE(result.target_met);
    REQUIRE(result.decaps.empty());
}

TEST_CASE("optimizer: meaningful target picks some decaps and reduces |Z|", "[optimizer]") {
    auto cfg = small_plane();
    DecapOptimizerConfig opt;
    opt.target_z = 0.050;   // 50 mOhm
    opt.f_min = 1.0e6;
    opt.f_max = 1.0e8;
    opt.n_points = 15;
    opt.max_caps = 10;
    opt.cap_x = 0.020;
    opt.cap_y = 0.020;

    auto result = optimize_decaps(cfg, 0.040, 0.030, opt);
    // Should have picked at least one cap (bare 80x60mm FR-4 plate has
    // |Z| in the 10s of ohms range at 1 MHz, well above 50 mOhm).
    REQUIRE_FALSE(result.decaps.empty());
    REQUIRE(result.final_max_z < 50.0);  // big improvement vs bare plate

    // Chosen caps cluster around the configured cap position (the optimizer
    // spreads them on a small grid so port positions stay distinct -- cavity
    // Z-matrix would be singular with co-located ports).
    for (const auto& d : result.decaps) {
        REQUIRE(std::abs(d.x - opt.cap_x) < 0.010);  // within 10mm of base
        REQUIRE(std::abs(d.y - opt.cap_y) < 0.010);
        REQUIRE(d.C > 0.0);
    }
}

TEST_CASE("optimizer: max_caps cap is respected", "[optimizer]") {
    auto cfg = small_plane();
    DecapOptimizerConfig opt;
    opt.target_z = 1.0e-6;  // absurdly tight — impossible to meet
    opt.f_min = 1.0e6;
    opt.f_max = 1.0e9;
    opt.n_points = 10;
    opt.max_caps = 4;       // hard cap
    opt.cap_x = 0.020;
    opt.cap_y = 0.020;

    auto result = optimize_decaps(cfg, 0.040, 0.030, opt);
    REQUIRE(result.decaps.size() <= 4);
    REQUIRE_FALSE(result.target_met);
}

TEST_CASE("optimizer: invalid config returns no caps", "[optimizer]") {
    auto cfg = small_plane();
    DecapOptimizerConfig opt;
    opt.target_z = -1.0;  // invalid
    auto r = optimize_decaps(cfg, 0.04, 0.03, opt);
    REQUIRE(r.decaps.empty());
    REQUIRE_FALSE(r.target_met);
}
