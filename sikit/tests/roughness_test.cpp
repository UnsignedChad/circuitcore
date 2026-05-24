#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/SurfaceRoughness.h"
#include "si/ChannelSynthesis.h"
#include "si/TraceImpedance.h"

using namespace sikit::analysis;
using Catch::Approx;

TEST_CASE("roughness: None returns factor 1 at any frequency", "[roughness]") {
    RoughnessSpec r;   // model = None
    REQUIRE(roughness_factor(r, 1e9,  5.8e7) == Approx(1.0));
    REQUIRE(roughness_factor(r, 50e9, 5.8e7) == Approx(1.0));
}

TEST_CASE("roughness: skin depth shrinks as sqrt(freq)", "[roughness]") {
    const double d1 = skin_depth(1e9,  5.8e7);
    const double d4 = skin_depth(16e9, 5.8e7);
    // 16x freq → 4x smaller skin depth.
    REQUIRE(d4 == Approx(d1 / 4.0).margin(d1 / 100.0));
}

TEST_CASE("roughness: HJ factor monotonically rises with Delta/delta",
          "[roughness]") {
    RoughnessSpec r;
    r.model = RoughnessModel::HammerstadJensen;
    r.rms_height = 0.5e-6;
    const double k1 = roughness_factor(r, 1e9, 5.8e7);
    r.rms_height = 3.0e-6;
    const double k3 = roughness_factor(r, 1e9, 5.8e7);
    REQUIRE(k3 > k1);
}

TEST_CASE("roughness: HJ factor at high freq approaches the 2x asymptote",
          "[roughness]") {
    // For Delta >> delta, atan(1.4*(Delta/delta)^2) -> pi/2, so K -> 1 + 1 = 2.
    RoughnessSpec r;
    r.model = RoughnessModel::HammerstadJensen;
    r.rms_height = 3.0e-6;
    const double k_high = roughness_factor(r, 100e9, 5.8e7);
    REQUIRE(k_high > 1.9);
    REQUIRE(k_high <= 2.0);
}

TEST_CASE("roughness: HJ factor at low freq approaches 1", "[roughness]") {
    RoughnessSpec r;
    r.model = RoughnessModel::HammerstadJensen;
    r.rms_height = 0.5e-6;
    const double k_low = roughness_factor(r, 1e6, 5.8e7);
    REQUIRE(k_low == Approx(1.0).margin(0.05));
}

TEST_CASE("roughness: Huray factor non-trivial", "[roughness]") {
    RoughnessSpec r;
    r.model = RoughnessModel::Huray;
    r.sphere_radius  = 0.5e-6;
    r.sphere_density = 1e12;
    r.flat_coverage  = 0.5;
    const double k = roughness_factor(r, 25e9, 5.8e7);
    REQUIRE(k > 1.0);
}

TEST_CASE("synthesize: rough copper produces more loss than smooth", "[roughness][synth]") {
    ChannelSpec spec_smooth;
    spec_smooth.trace_width = 0.2e-3;
    spec_smooth.layer_ordinal = 0;
    spec_smooth.length_m = 0.30;
    spec_smooth.stackup.outer_dielectric_height = 0.2e-3;
    spec_smooth.stackup.copper_thickness = 35e-6;
    spec_smooth.stackup.epsilon_r = 4.3;
    spec_smooth.stackup.tan_delta = 0.005;
    spec_smooth.engine = Engine::ClosedForm;

    ChannelSpec spec_rough = spec_smooth;
    spec_rough.stackup.roughness.model = RoughnessModel::HammerstadJensen;
    spec_rough.stackup.roughness.rms_height = 2.0e-6;

    auto ts_smooth = synthesize_channel(spec_smooth, {20e9}, 50.0);
    auto ts_rough  = synthesize_channel(spec_rough,  {20e9}, 50.0);

    const double s21_smooth = std::abs(ts_smooth.s_matrices[0][1]);
    const double s21_rough  = std::abs(ts_rough.s_matrices[0][1]);
    REQUIRE(s21_rough < s21_smooth);
}
