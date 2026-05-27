// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "si/Overlay.h"
#include "si/Touchstone.h"

using namespace sikit::analysis;
using sikit::touchstone::Format;
using sikit::touchstone::TouchstoneFile;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {

// Build a flat 2-port with the same |S21| at every frequency point.
TouchstoneFile flat_s21(double mag) {
    TouchstoneFile t;
    t.num_ports = 2;
    t.reference_impedance = 50.0;
    t.format = Format::RealImaginary;
    t.frequency_scale = 1.0;
    for (int i = 0; i < 16; ++i) {
        const double f = (i + 1) * 1e9;
        t.frequencies.push_back(f);
        t.s_matrices.push_back({Complex(0, 0), Complex(mag, 0),
                                  Complex(mag, 0), Complex(0, 0)});
    }
    return t;
}

}  // namespace

TEST_CASE("overlay: identical files give zero delta", "[overlay]") {
    auto a = flat_s21(0.5);
    auto b = flat_s21(0.5);
    auto d = overlay_delta(a, b, 1);
    REQUIRE(d.delta_db.size() == a.frequencies.size());
    for (double x : d.delta_db) REQUIRE(std::abs(x) < 1e-9);
    REQUIRE(d.max_abs_db < 1e-9);
}

TEST_CASE("overlay: 2x amplitude difference is 6 dB", "[overlay]") {
    auto a = flat_s21(0.5);
    auto b = flat_s21(0.25);    // b is half of a -> 20*log10(2) ~ 6.02 dB
    auto d = overlay_delta(a, b, 1);
    for (double x : d.delta_db) REQUIRE(x == Approx(6.0206).margin(1e-3));
    REQUIRE(d.max_abs_db == Approx(6.0206).margin(1e-3));
}

TEST_CASE("overlay: negative delta when a is quieter than b", "[overlay]") {
    auto a = flat_s21(0.25);
    auto b = flat_s21(0.5);
    auto d = overlay_delta(a, b, 1);
    REQUIRE(d.delta_db[0] == Approx(-6.0206).margin(1e-3));
    REQUIRE(d.max_abs_db == Approx(6.0206).margin(1e-3));
}

TEST_CASE("overlay: locates the worst-case frequency index", "[overlay]") {
    auto a = flat_s21(0.5);
    auto b = flat_s21(0.5);
    // Inject a single point of 0.05 mag in b at index 7.
    b.s_matrices[7] = {Complex(0, 0), Complex(0.05, 0),
                        Complex(0.05, 0), Complex(0, 0)};
    auto d = overlay_delta(a, b, 1);
    REQUIRE(d.max_index == 7);
    REQUIRE(d.max_freq_hz == Approx(8e9));
    REQUIRE(d.max_abs_db == Approx(20.0).margin(0.5));
}

TEST_CASE("overlay: rejects port-count mismatch", "[overlay]") {
    auto a = flat_s21(0.5);
    auto b = flat_s21(0.5);
    b.num_ports = 4;
    REQUIRE_THROWS_AS(overlay_delta(a, b, 1), OverlayError);
}

TEST_CASE("overlay: rejects frequency-grid mismatch", "[overlay]") {
    auto a = flat_s21(0.5);
    auto b = flat_s21(0.5);
    b.frequencies[3] += 1e6;   // shift one point off
    REQUIRE_THROWS_AS(overlay_delta(a, b, 1), OverlayError);
}

TEST_CASE("overlay: rejects out-of-range s_param_index", "[overlay]") {
    auto a = flat_s21(0.5);
    auto b = flat_s21(0.5);
    REQUIRE_THROWS_AS(overlay_delta(a, b, /*index=*/9), OverlayError);
    REQUIRE_THROWS_AS(overlay_delta(a, b, /*index=*/-1), OverlayError);
}
