// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "si/Crosstalk.h"
#include "si/Touchstone.h"
#include "si/Eye.h"
#include "si/EyeMetrics.h"

using namespace sikit::analysis;
using sikit::touchstone::TouchstoneFile;
using sikit::touchstone::Format;
using Complex = std::complex<double>;
using Catch::Approx;

namespace {

// Build an "ideal passthrough" 2-port: S21 = 1 across the band, no
// reflections. Frequency grid starts near DC (1 Hz) and extends past
// the eye-pipeline's Nyquist so apply_channel never has to extrapolate
// outside the supplied data -- which matters here because the existing
// dsp::interpolate_s21 returns 1.0 below the grid floor and clamps to
// the last value above the ceiling. Test fixtures that don't cover the
// full TX spectrum see "magic gain" at low frequencies that obscures
// what the model under test actually does.
TouchstoneFile passthrough_2port() {
    TouchstoneFile t;
    t.num_ports = 2;
    t.reference_impedance = 50.0;
    t.format = Format::RealImaginary;
    t.frequency_scale = 1.0;
    t.frequencies.push_back(1.0);
    t.s_matrices.push_back({Complex(0,0), Complex(1,0),
                              Complex(1,0), Complex(0,0)});
    for (int i = 0; i < 64; ++i) {
        const double f = (i + 1) * 1.5e9;  // 1.5 GHz .. 96 GHz
        t.frequencies.push_back(f);
        // S11=0, S21=1, S12=1, S22=0 in column-major [S11,S21,S12,S22].
        t.s_matrices.push_back({Complex(0,0), Complex(1,0),
                                  Complex(1,0), Complex(0,0)});
    }
    return t;
}

// Build a "uniform-attenuation" 2-port: S21 = `mag` across the band, no
// reflections. Used to model an aggressor->victim coupling whose
// frequency-flat magnitude we can crank up or down to test the dose-
// response of crosstalk noise on the victim eye.
TouchstoneFile flat_coupling_2port(double mag) {
    TouchstoneFile t = passthrough_2port();
    for (auto& m : t.s_matrices) {
        m[1] = Complex(mag, 0);   // S21
        m[2] = Complex(mag, 0);   // S12 (reciprocal)
    }
    return t;
}

}  // namespace

TEST_CASE("crosstalk: zero-aggressor scenario reproduces clean victim eye",
          "[crosstalk]") {
    CrosstalkScenario sc;
    sc.victim_thru = passthrough_2port();
    // No aggressors.
    auto eye = simulate_crosstalk_eye(sc,
        /*baud_hz=*/5e9, /*n_bits=*/512, /*samples_per_ui=*/32,
        /*eye_t_bins=*/64, /*eye_v_bins=*/64, /*warmup_ui=*/4);
    REQUIRE(eye.time_bins == 64);
    REQUIRE(eye.volt_bins == 64);
    // A clean passthrough eye is wide open. Height should be close to the
    // full 2 V (signal levels are +/- 1 V, NRZ).
    const auto m = sikit::eye::measure_eye(eye);
    REQUIRE(m.height_v > 1.5);
    REQUIRE(m.width_ui > 0.5);
}

TEST_CASE("crosstalk: a single coupled aggressor closes the eye more than no "
          "aggressor", "[crosstalk]") {
    CrosstalkScenario sc_clean;
    sc_clean.victim_thru = passthrough_2port();

    CrosstalkScenario sc_noisy;
    sc_noisy.victim_thru = passthrough_2port();
    sc_noisy.aggressor_to_victim_coupling.push_back(flat_coupling_2port(0.35));

    auto eye_clean = simulate_crosstalk_eye(sc_clean, 5e9, 512, 32, 64, 64, 4);
    auto eye_noisy = simulate_crosstalk_eye(sc_noisy, 5e9, 512, 32, 64, 64, 4);
    const auto m_clean = sikit::eye::measure_eye(eye_clean);
    const auto m_noisy = sikit::eye::measure_eye(eye_noisy);
    REQUIRE(m_noisy.height_v < m_clean.height_v);
}

TEST_CASE("crosstalk: stronger coupling closes the eye further", "[crosstalk]") {
    CrosstalkScenario light;
    light.victim_thru = passthrough_2port();
    light.aggressor_to_victim_coupling.push_back(flat_coupling_2port(0.10));

    CrosstalkScenario heavy;
    heavy.victim_thru = passthrough_2port();
    heavy.aggressor_to_victim_coupling.push_back(flat_coupling_2port(0.45));

    auto eye_light = simulate_crosstalk_eye(light, 5e9, 1024, 32, 64, 64, 4);
    auto eye_heavy = simulate_crosstalk_eye(heavy, 5e9, 1024, 32, 64, 64, 4);
    const auto m_light = sikit::eye::measure_eye(eye_light);
    const auto m_heavy = sikit::eye::measure_eye(eye_heavy);
    REQUIRE(m_heavy.height_v < m_light.height_v);
}

TEST_CASE("crosstalk: zero-coupling aggressor contributes nothing", "[crosstalk]") {
    CrosstalkScenario sc;
    sc.victim_thru = passthrough_2port();
    sc.aggressor_to_victim_coupling.push_back(flat_coupling_2port(0.0));

    auto eye_zero = simulate_crosstalk_eye(sc, 5e9, 512, 32, 64, 64, 4);
    CrosstalkScenario clean;
    clean.victim_thru = passthrough_2port();
    auto eye_clean = simulate_crosstalk_eye(clean, 5e9, 512, 32, 64, 64, 4);
    const auto m_zero  = sikit::eye::measure_eye(eye_zero);
    const auto m_clean = sikit::eye::measure_eye(eye_clean);
    // Within a tiny tolerance: the two heights should match (within
    // sub-bin-resolution noise of the eye-folding pass).
    REQUIRE(std::abs(m_zero.height_v - m_clean.height_v) < 0.05);
}

TEST_CASE("crosstalk: three aggressors close the eye more than one",
          "[crosstalk]") {
    CrosstalkScenario one_agg;
    one_agg.victim_thru = passthrough_2port();
    one_agg.aggressor_to_victim_coupling.push_back(flat_coupling_2port(0.20));

    CrosstalkScenario three_agg;
    three_agg.victim_thru = passthrough_2port();
    for (int i = 0; i < 3; ++i) {
        three_agg.aggressor_to_victim_coupling.push_back(
            flat_coupling_2port(0.20));
    }

    auto eye_1 = simulate_crosstalk_eye(one_agg,   5e9, 1024, 32, 64, 64, 4);
    auto eye_3 = simulate_crosstalk_eye(three_agg, 5e9, 1024, 32, 64, 64, 4);
    const auto m_1 = sikit::eye::measure_eye(eye_1);
    const auto m_3 = sikit::eye::measure_eye(eye_3);
    REQUIRE(m_3.height_v < m_1.height_v);
}

TEST_CASE("crosstalk: rejects invalid input", "[crosstalk]") {
    CrosstalkScenario sc;
    sc.victim_thru = passthrough_2port();
    REQUIRE_THROWS(simulate_crosstalk_eye(sc, 0.0, 100, 32, 64, 64));
    REQUIRE_THROWS(simulate_crosstalk_eye(sc, 5e9, 0,   32, 64, 64));
    REQUIRE_THROWS(simulate_crosstalk_eye(sc, 5e9, 100,  1, 64, 64));

    // Empty victim Touchstone.
    CrosstalkScenario empty;
    REQUIRE_THROWS(simulate_crosstalk_eye(empty, 5e9, 100, 32, 64, 64));

    // Wrong-port-count aggressor.
    CrosstalkScenario bad;
    bad.victim_thru = passthrough_2port();
    TouchstoneFile bad_agg = passthrough_2port();
    bad_agg.num_ports = 4;
    bad.aggressor_to_victim_coupling.push_back(bad_agg);
    REQUIRE_THROWS(simulate_crosstalk_eye(bad, 5e9, 100, 32, 64, 64));
}

TEST_CASE("crosstalk: diff_pair_to_scenario extracts the right slices for PNPN",
          "[crosstalk]") {
    // Build a 4-port with known, distinct entries so we can verify which
    // column is being pulled.
    TouchstoneFile s4p;
    s4p.num_ports = 4;
    s4p.reference_impedance = 50.0;
    s4p.format = Format::RealImaginary;
    s4p.frequencies = {1e9};
    // Column-major: m[row + col*4]. Set every entry to (col + 10*row, 0)
    // so we can read off which (row, col) survives.
    std::vector<Complex> m(16, Complex(0, 0));
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            m[r + c * 4] = Complex(c + 10.0 * r, 0);
        }
    }
    s4p.s_matrices.push_back(m);

    auto sc = diff_pair_to_scenario(s4p, S4pPortOrder::PNPN);
    // PNPN: p_near=0, n_near=1, p_far=2, n_far=3.
    // Victim_thru is the P_near -> P_far slice = column 0 (src),
    // row 2 (dst); so S21 of the 2-port == m[2 + 0*4] = (0 + 20, 0) = 20.
    REQUIRE(sc.victim_thru.s_matrices[0][1].real() == Approx(20.0));
    // Aggressor coupling: N_near -> P_far = column 1, row 2 = m[2 + 1*4]
    // = (1 + 20, 0) = 21.
    REQUIRE(sc.aggressor_to_victim_coupling[0].s_matrices[0][1].real()
            == Approx(21.0));
}

TEST_CASE("crosstalk: diff_pair_to_scenario rejects non-4-port", "[crosstalk]") {
    TouchstoneFile two_port;
    two_port.num_ports = 2;
    REQUIRE_THROWS(diff_pair_to_scenario(two_port));
}
