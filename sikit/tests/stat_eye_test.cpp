#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "si/StatEye.h"
#include "si/Touchstone.h"

using namespace sikit::eye;
using Catch::Approx;

namespace {

// Build a synthetic SBR that's a clean rectangular pulse one UI wide.
// No ISI -> peak distortion eye should match the cursor amplitude.
std::vector<double> clean_pulse_sbr(int samples_per_ui) {
    const int total = samples_per_ui * 8;
    std::vector<double> sbr(total, 0.0);
    // Cursor UI is at UI 1 (so UI 0 is pre-cursor; UIs 2..7 are post-cursor zeros).
    for (int t = 0; t < samples_per_ui; ++t) {
        sbr[samples_per_ui + t] = 1.0;
    }
    return sbr;
}

// Build an SBR with one cursor + one trailing post-cursor (ISI = 0.3).
// PDA eye top should be 1.0 - 0.3 = 0.7 at the cursor sample.
std::vector<double> isi_sbr(int samples_per_ui) {
    const int total = samples_per_ui * 8;
    std::vector<double> sbr(total, 0.0);
    for (int t = 0; t < samples_per_ui; ++t) {
        sbr[samples_per_ui + t] = 1.0;
        sbr[2 * samples_per_ui + t] = 0.3;
    }
    return sbr;
}

}  // namespace

TEST_CASE("stat-eye: clean pulse opens to cursor amplitude", "[stateye][pda]") {
    constexpr int spui = 16;
    auto sbr = clean_pulse_sbr(spui);
    auto pda = peak_distortion_eye(sbr, spui);
    REQUIRE(pda.samples_per_ui == spui);
    REQUIRE(pda.eye_height == Approx(2.0).margin(1e-9));  // top - bottom = 1 - (-1) = 2
    REQUIRE(pda.eye_width  == Approx(1.0));               // open at every phase
}

TEST_CASE("stat-eye: ISI tap closes the eye proportionally", "[stateye][pda]") {
    constexpr int spui = 16;
    auto sbr = isi_sbr(spui);
    auto pda = peak_distortion_eye(sbr, spui);
    // Cursor = 1.0; ISI = 0.3; PDA eye height = 2 * (1.0 - 0.3) = 1.4
    REQUIRE(pda.eye_height == Approx(1.4).margin(1e-9));
    REQUIRE(pda.eye_width  == Approx(1.0));
}

TEST_CASE("stat-eye: empty SBR yields empty envelope", "[stateye]") {
    auto pda = peak_distortion_eye({}, 16);
    REQUIRE(pda.top.empty());
    auto se = statistical_eye({}, 16, 128);
    REQUIRE(se.ber_map.empty());
}

TEST_CASE("stat-eye: statistical eye on clean pulse has 0 BER inside eye",
          "[stateye][stat]") {
    constexpr int spui = 16;
    auto sbr = clean_pulse_sbr(spui);
    auto se = statistical_eye(sbr, spui, 64);
    REQUIRE(se.samples_per_ui == spui);
    REQUIRE(se.volt_bins == 64);
    // With no ISI: at decision threshold v_th = 0 (middle) the BER should
    // be exactly 0 for all UI phases (cursor = +/- 1.0, all ISI taps = 0).
    const int v_mid = se.volt_bins / 2;
    for (int t = 0; t < spui; ++t) {
        const double ber = se.ber_map[static_cast<std::size_t>(v_mid) * spui + t];
        REQUIRE(ber == Approx(0.0).margin(0.01));
    }
}

TEST_CASE("stat-eye: BER is highest at extreme thresholds", "[stateye][stat]") {
    constexpr int spui = 16;
    auto sbr = isi_sbr(spui);
    auto se = statistical_eye(sbr, spui, 128);
    // BER at v_th = v_min (always below the signal) -> bit=1 never seen as
    // 0 (h_cursor + v_ISI is always > v_min); bit=0 always seen as 1.
    // So BER -> 0.5 (50% of bit=0s flipped, 0% of bit=1s).
    const int last_t = spui - 1;
    const double ber_top = se.ber_map[
        static_cast<std::size_t>(se.volt_bins - 1) * spui + last_t];
    const double ber_bot = se.ber_map[
        static_cast<std::size_t>(0) * spui + last_t];
    // Both extremes should give ~0.5 BER.
    REQUIRE(ber_top == Approx(0.5).margin(0.05));
    REQUIRE(ber_bot == Approx(0.5).margin(0.05));
}

TEST_CASE("stat-eye: bathtub_from_stat_eye returns per-UI BER", "[stateye][stat]") {
    constexpr int spui = 16;
    auto sbr = isi_sbr(spui);
    auto se = statistical_eye(sbr, spui, 128);
    auto bt = bathtub_from_stat_eye(se);
    REQUIRE(static_cast<int>(bt.size()) == spui);
    for (double v : bt) REQUIRE(v >= 0.0);
    for (double v : bt) REQUIRE(v <= 0.5);
}

TEST_CASE("stat-eye: compute_sbr returns nonempty for a valid channel",
          "[stateye][sbr]") {
    // Build a tiny passthrough channel: |S21| = 1 across band.
    using namespace sikit::touchstone;
    TouchstoneFile c;
    c.num_ports = 2;
    c.reference_impedance = 50.0;
    c.frequencies = {1e9, 5e9, 10e9, 20e9};
    for (std::size_t i = 0; i < c.frequencies.size(); ++i) {
        c.s_matrices.push_back({std::complex<double>(0, 0),
                                  std::complex<double>(1, 0),
                                  std::complex<double>(1, 0),
                                  std::complex<double>(0, 0)});
    }
    auto sbr = compute_sbr(c, 5e9, 16, 16);
    REQUIRE(!sbr.empty());
    // Should have some nonzero content.
    double max_abs = 0;
    for (double v : sbr) max_abs = std::max(max_abs, std::abs(v));
    REQUIRE(max_abs > 0.01);
}
