// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>

#include "si/ChannelSynthesis.h"
#include "si/DjordjevicSarkar.h"

using namespace sikit::analysis;
using namespace sikit::dispersion;
using Catch::Approx;

namespace {

ChannelSpec basic_spec() {
    ChannelSpec s;
    s.trace_width = 2.8e-3;
    s.layer_ordinal = 0;
    s.length_m = 0.3;                  // 30 cm so loss is visible
    s.stackup.outer_dielectric_height = 1.524e-3;
    s.stackup.copper_thickness = 35e-6;
    s.stackup.epsilon_r = 4.4;
    s.stackup.tan_delta = 0.02;
    s.engine = Engine::ClosedForm;
    return s;
}

}  // namespace

TEST_CASE("dispersion: ChannelSpec without model matches default behavior", "[disp]") {
    // Round-trip sanity: when dispersion_model is not set, the output
    // should be identical to the lossy-but-non-dispersive synthesis.
    auto a = basic_spec();
    auto b = a;
    b.dispersion_model = std::nullopt;

    auto ta = synthesize_channel(a, {1e9, 5e9, 10e9});
    auto tb = synthesize_channel(b, {1e9, 5e9, 10e9});
    for (std::size_t k = 0; k < ta.s_matrices.size(); ++k) {
        for (int i = 0; i < 4; ++i) {
            REQUIRE(ta.s_matrices[k][i].real() ==
                    Approx(tb.s_matrices[k][i].real()).margin(1e-12));
            REQUIRE(ta.s_matrices[k][i].imag() ==
                    Approx(tb.s_matrices[k][i].imag()).margin(1e-12));
        }
    }
}

TEST_CASE("dispersion: model attached → results differ from constant εr", "[disp]") {
    auto with_model = basic_spec();
    with_model.dispersion_model = DjordjevicSarkar::from_reference(4.4, 0.02, 1e9);

    auto without = basic_spec();
    without.dispersion_model = std::nullopt;

    // Pick a frequency far from the f0 reference where εr should have
    // measurably drifted.
    auto t_disp  = synthesize_channel(with_model, {10e9});
    auto t_const = synthesize_channel(without,    {10e9});

    // S21 magnitudes differ — that's the whole point of the model.
    const double m_disp  = std::abs(t_disp.s_matrices[0][1]);
    const double m_const = std::abs(t_const.s_matrices[0][1]);
    REQUIRE(m_disp != Approx(m_const).margin(1e-4));
}

TEST_CASE("dispersion: at f0 the two paths agree closely", "[disp]") {
    // At the reference frequency the dispersion model returns the same εr
    // and tan_δ as the constant model — so the channel output should
    // match within numerical noise.
    auto with_model = basic_spec();
    with_model.dispersion_model = DjordjevicSarkar::from_reference(4.4, 0.02, 1e9);

    auto without = basic_spec();

    auto t_disp  = synthesize_channel(with_model, {1e9});
    auto t_const = synthesize_channel(without,    {1e9});

    // Magnitudes should agree to a fraction of a percent (small residual
    // from the Hammerstad recompute vs cached eps_eff).
    const double m_disp  = std::abs(t_disp.s_matrices[0][1]);
    const double m_const = std::abs(t_const.s_matrices[0][1]);
    REQUIRE(std::abs(m_disp - m_const) / m_const < 0.02);
}

TEST_CASE("dispersion: |S21| still drops monotonically at higher freq", "[disp]") {
    auto spec = basic_spec();
    spec.dispersion_model = DjordjevicSarkar::from_reference(4.4, 0.02, 1e9);
    auto t = synthesize_channel(spec, {1e9, 3e9, 7e9, 15e9});
    double prev = std::abs(t.s_matrices[0][1]);
    for (std::size_t k = 1; k < t.s_matrices.size(); ++k) {
        const double m = std::abs(t.s_matrices[k][1]);
        REQUIRE(m < prev);
        prev = m;
    }
}
