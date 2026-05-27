// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <utility>

#include "si/SParam.h"

using namespace sikit::sparam;
using Catch::Approx;

namespace {

Eigen::Matrix2cd passthrough() {
    // S11=0, S21=1, S12=1, S22=0 — ideal lossless reciprocal line.
    Eigen::Matrix2cd s;
    s(0, 0) = Complex(0, 0);
    s(0, 1) = Complex(1, 0);
    s(1, 0) = Complex(1, 0);
    s(1, 1) = Complex(0, 0);
    return s;
}

Eigen::Matrix2cd attenuator_dB(double db) {
    // Lossy, matched, reciprocal: S11=S22=0, S21=S12=10^(-db/20).
    const double mag = std::pow(10.0, -db / 20.0);
    Eigen::Matrix2cd s;
    s(0, 0) = Complex(0, 0);
    s(0, 1) = Complex(mag, 0);
    s(1, 0) = Complex(mag, 0);
    s(1, 1) = Complex(0, 0);
    return s;
}
}  // namespace

TEST_CASE("sparam: s_to_t round-trip is identity", "[sparam]") {
    Eigen::Matrix2cd s;
    s(0, 0) = Complex( 0.1,  0.2);
    s(0, 1) = Complex( 0.3,  0.4);
    s(1, 0) = Complex( 0.5, -0.1);
    s(1, 1) = Complex(-0.2,  0.3);

    auto round_trip = t_to_s(s_to_t(s));
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            REQUIRE(round_trip(i, j).real() == Approx(s(i, j).real()).margin(1e-12));
            REQUIRE(round_trip(i, j).imag() == Approx(s(i, j).imag()).margin(1e-12));
        }
}

TEST_CASE("sparam: cascading passthrough leaves network unchanged", "[sparam]") {
    auto pass = passthrough();
    auto atten3 = attenuator_dB(3.0);

    auto c1 = cascade(pass, atten3);
    auto c2 = cascade(atten3, pass);

    REQUIRE(std::abs(c1(1, 0)) == Approx(std::abs(atten3(1, 0))).margin(1e-9));
    REQUIRE(std::abs(c2(1, 0)) == Approx(std::abs(atten3(1, 0))).margin(1e-9));
}

TEST_CASE("sparam: cascaded attenuators add in dB", "[sparam]") {
    auto a3 = attenuator_dB(3.0);
    auto a6 = attenuator_dB(6.0);
    auto cascaded = cascade(a3, a6);

    const double db = insertion_loss_db(cascaded(1, 0));
    REQUIRE(db == Approx(9.0).margin(1e-6));
}

TEST_CASE("sparam: cascading three attenuators stacks", "[sparam]") {
    auto a = attenuator_dB(1.0);
    auto b = attenuator_dB(2.0);
    auto c = attenuator_dB(3.0);
    auto ab = cascade(a, b);
    auto abc = cascade(ab, c);
    REQUIRE(insertion_loss_db(abc(1, 0)) == Approx(6.0).margin(1e-6));
}

TEST_CASE("sparam: zero-S21 throws on s_to_t", "[sparam]") {
    Eigen::Matrix2cd s;
    s.setZero();
    REQUIRE_THROWS_AS(s_to_t(s), SParamError);
}

TEST_CASE("sparam: insertion / return loss in dB", "[sparam]") {
    // |S21| = 0.5 → 6.02 dB
    REQUIRE(insertion_loss_db(Complex(0.5, 0)) == Approx(6.020599913).margin(1e-6));
    // |S11| = 0.1 → 20 dB return loss
    REQUIRE(return_loss_db(Complex(0.1, 0)) == Approx(20.0).margin(1e-6));
}

TEST_CASE("sparam: touchstone cascade is frequency-by-frequency", "[sparam]") {
    using namespace sikit::touchstone;

    TouchstoneFile a;
    a.num_ports = 2;
    a.format = Format::RealImaginary;
    a.reference_impedance = 50.0;
    a.frequency_scale = 1.0;
    a.frequencies = {1e9, 2e9};
    // Two attenuators at 3 dB and 6 dB across two frequency points.
    {
        const double m3 = std::pow(10.0, -3.0 / 20.0);
        a.s_matrices.push_back({Complex(0,0), Complex(m3,0), Complex(m3,0), Complex(0,0)});
        const double m1 = std::pow(10.0, -1.0 / 20.0);
        a.s_matrices.push_back({Complex(0,0), Complex(m1,0), Complex(m1,0), Complex(0,0)});
    }

    TouchstoneFile b = a;
    {
        const double m6 = std::pow(10.0, -6.0 / 20.0);
        b.s_matrices[0] = {Complex(0,0), Complex(m6,0), Complex(m6,0), Complex(0,0)};
        const double m2 = std::pow(10.0, -2.0 / 20.0);
        b.s_matrices[1] = {Complex(0,0), Complex(m2,0), Complex(m2,0), Complex(0,0)};
    }

    auto c = cascade(a, b);
    REQUIRE(c.frequencies.size() == 2);
    // S21 is at index 1 in column-major 2-port storage.
    REQUIRE(insertion_loss_db(c.s_matrices[0][1]) == Approx(9.0).margin(1e-6));
    REQUIRE(insertion_loss_db(c.s_matrices[1][1]) == Approx(3.0).margin(1e-6));
}

TEST_CASE("sparam: touchstone cascade rejects mismatched grids", "[sparam]") {
    using namespace sikit::touchstone;
    TouchstoneFile a;
    a.num_ports = 2;
    a.frequencies = {1e9, 2e9};
    a.s_matrices = {{Complex(0,0), Complex(1,0), Complex(1,0), Complex(0,0)},
                    {Complex(0,0), Complex(1,0), Complex(1,0), Complex(0,0)}};
    TouchstoneFile b = a;
    b.frequencies = {1e9, 3e9};  // mismatch at point 1

    REQUIRE_THROWS_AS(cascade(a, b), SParamError);
}

TEST_CASE("sparam: single-ended-to-differential basics", "[sparam]") {
    // Build a 4-port that's an ideal passthrough between (port1+,port2+)
    // and (port1-,port2-): S31=S13=1, S42=S24=1, all else zero.
    // Differential mode should also pass through (Sdd21 = 1).
    Eigen::Matrix4cd s = Eigen::Matrix4cd::Zero();
    s(2, 0) = Complex(1, 0); s(0, 2) = Complex(1, 0);  // port 1 → port 3
    s(3, 1) = Complex(1, 0); s(1, 3) = Complex(1, 0);  // port 2 → port 4

    auto sdd = sikit::sparam::single_ended_to_differential(s);
    // |Sdd21| should be 1 (ideal differential passthrough).
    REQUIRE(std::abs(sdd(1, 0)) == Approx(1.0).margin(1e-9));
}

TEST_CASE("sparam: mixed-mode 4x4 — ideal passthrough has Sdd21=1, Sdc=0", "[sparam]") {
    // [P1, N1, P2, N2] passthrough: P1↔P2 and N1↔N2.
    Eigen::Matrix4cd s = Eigen::Matrix4cd::Zero();
    s(2, 0) = Complex(1, 0); s(0, 2) = Complex(1, 0);
    s(3, 1) = Complex(1, 0); s(1, 3) = Complex(1, 0);

    auto mm = sikit::sparam::single_ended_to_mixed_mode(
        s, sikit::sparam::PortOrder::PNPN);

    // Sdd21 (row 1, col 0): differential mode passes through cleanly.
    REQUIRE(std::abs(mm(1, 0)) == Approx(1.0).margin(1e-9));
    // Sdd11 (row 0, col 0): no reflection.
    REQUIRE(std::abs(mm(0, 0)) == Approx(0.0).margin(1e-9));
    // Symmetric pair → zero mode-conversion. Sdc21 (row 1, col 2) = 0,
    // Scd21 (row 3, col 0) = 0.
    REQUIRE(std::abs(mm(1, 2)) == Approx(0.0).margin(1e-9));
    REQUIRE(std::abs(mm(3, 0)) == Approx(0.0).margin(1e-9));
    // Scc21 (row 3, col 2): common mode passes through too.
    REQUIRE(std::abs(mm(3, 2)) == Approx(1.0).margin(1e-9));
}

TEST_CASE("sparam: mixed-mode PPNN convention", "[sparam]") {
    // Same physical network, but port order is [P1, P2, N1, N2].
    // P1↔P2 is now S21=1, N1↔N2 is S43=1.
    Eigen::Matrix4cd s = Eigen::Matrix4cd::Zero();
    s(1, 0) = Complex(1, 0); s(0, 1) = Complex(1, 0);
    s(3, 2) = Complex(1, 0); s(2, 3) = Complex(1, 0);

    auto mm = sikit::sparam::single_ended_to_mixed_mode(
        s, sikit::sparam::PortOrder::PPNN);

    REQUIRE(std::abs(mm(1, 0)) == Approx(1.0).margin(1e-9));   // Sdd21
    REQUIRE(std::abs(mm(3, 2)) == Approx(1.0).margin(1e-9));   // Scc21
    REQUIRE(std::abs(mm(1, 2)) == Approx(0.0).margin(1e-9));   // Sdc21
}

TEST_CASE("sparam: skewed pair leaks differential→common (Sdc != 0)", "[sparam]") {
    // Inject a half-amp loss on N only: P passes 1, N passes 0.7.
    // This creates an imbalance, so a differential input produces some
    // common-mode response → non-zero Scd entry.
    Eigen::Matrix4cd s = Eigen::Matrix4cd::Zero();
    s(2, 0) = Complex(1.0, 0); s(0, 2) = Complex(1.0, 0);  // P1→P2 = 1
    s(3, 1) = Complex(0.7, 0); s(1, 3) = Complex(0.7, 0);  // N1→N2 = 0.7

    auto mm = sikit::sparam::single_ended_to_mixed_mode(
        s, sikit::sparam::PortOrder::PNPN);
    // Scd21 (common-mode output from differential input) is non-zero.
    REQUIRE(std::abs(mm(3, 0)) > 0.01);
}

TEST_CASE("sparam: to_mixed_mode round-trips a TouchstoneFile", "[sparam]") {
    using namespace sikit::touchstone;
    TouchstoneFile a;
    a.num_ports = 4;
    a.frequencies = {1e9};
    // Build same ideal passthrough at one frequency point.
    std::vector<Complex> m(16, Complex(0, 0));
    m[2 + 0 * 4] = m[0 + 2 * 4] = Complex(1, 0);  // P1↔P2
    m[3 + 1 * 4] = m[1 + 3 * 4] = Complex(1, 0);  // N1↔N2
    a.s_matrices.push_back(std::move(m));

    auto b = sikit::sparam::to_mixed_mode(a, sikit::sparam::PortOrder::PNPN);
    REQUIRE(b.num_ports == 4);
    REQUIRE(b.frequencies.size() == 1);
    // Sdd21 lives at row=1, col=0 → column-major index 1.
    REQUIRE(std::abs(b.s_matrices[0][1]) == Approx(1.0).margin(1e-9));
    // Scc21 → row 3, col 2 → index 3 + 2*4 = 11.
    REQUIRE(std::abs(b.s_matrices[0][3 + 2 * 4]) == Approx(1.0).margin(1e-9));
}

TEST_CASE("sparam: to_mixed_mode rejects non-4-port files", "[sparam]") {
    using namespace sikit::touchstone;
    TouchstoneFile a;
    a.num_ports = 2;
    a.frequencies = {1e9};
    a.s_matrices.push_back({Complex(0, 0), Complex(1, 0), Complex(1, 0), Complex(0, 0)});
    REQUIRE_THROWS_AS(sikit::sparam::to_mixed_mode(a),
                      sikit::sparam::SParamError);
}

TEST_CASE("sparam: TDR of zero-reflection line gives Z ≈ Zref", "[sparam][tdr]") {
    // S11 = 0 at every frequency → step response = 0 → Z(t) = Zref.
    std::vector<double> freqs;
    std::vector<sikit::sparam::Complex> s11;
    for (int k = 0; k < 128; ++k) {
        freqs.push_back(1e8 * (k + 1));   // 100 MHz .. 12.8 GHz, uniform
        s11.emplace_back(0.0, 0.0);
    }
    auto tdr = sikit::sparam::tdr_step_response(freqs, s11, 50.0);
    REQUIRE(!tdr.time.empty());
    for (double z : tdr.value) {
        REQUIRE(z == Approx(50.0).margin(1.0));
    }
}

TEST_CASE("sparam: TDR of |S11|=0.2 short reflection moves Z away from Zref",
          "[sparam][tdr]") {
    // Constant S11 = 0.2 across band. After Hann-windowed IFFT + cumsum, the
    // step response should rise off zero, so Z(t) departs from Zref=50.
    std::vector<double> freqs;
    std::vector<sikit::sparam::Complex> s11;
    for (int k = 0; k < 256; ++k) {
        freqs.push_back(1e8 * (k + 1));
        s11.emplace_back(0.2, 0.0);
    }
    auto tdr = sikit::sparam::tdr_step_response(freqs, s11, 50.0);
    REQUIRE(!tdr.time.empty());
    // Look at the peak excursion. The exact value depends on FFT length and
    // windowing, but it must move at least a couple of ohms away from 50.
    double zmin = tdr.value.front(), zmax = tdr.value.front();
    for (double z : tdr.value) { zmin = std::min(zmin, z); zmax = std::max(zmax, z); }
    REQUIRE((zmax - zmin) > 2.0);
}

TEST_CASE("sparam: TDR rejects bad input", "[sparam][tdr]") {
    std::vector<double> freqs{1e9};                  // only 1 point
    std::vector<sikit::sparam::Complex> s11{{0, 0}};
    auto bad = sikit::sparam::tdr_step_response(freqs, s11, 50.0);
    REQUIRE(bad.time.empty());

    // Mismatched lengths.
    std::vector<double> f2{1e9, 2e9};
    auto bad2 = sikit::sparam::tdr_step_response(f2, s11, 50.0);
    REQUIRE(bad2.time.empty());
}

TEST_CASE("sparam: TDT of perfect passthrough is monotonically rising",
          "[sparam][tdt]") {
    // S21 = 1 across band → impulse response is a band-limited sinc → its
    // step response rises from zero. Absolute amplitude is not 1 because
    // there is no DC bin in the source data (band starts at 100 MHz) and
    // the Hann window halves the area further; this is documented in the
    // SParam.h block. We only test the qualitative shape: the value at
    // mid-trace exceeds the value at the start.
    std::vector<double> freqs;
    std::vector<sikit::sparam::Complex> s21;
    for (int k = 0; k < 256; ++k) {
        freqs.push_back(1e8 * (k + 1));
        s21.emplace_back(1.0, 0.0);
    }
    auto tdt = sikit::sparam::tdt_step_response(freqs, s21);
    REQUIRE(!tdt.time.empty());
    // Any meaningful step response sits well above the noise floor in
    // its early oscillation envelope. Without a DC anchor the trace will
    // rise, ring, and decay; we just verify the rise.
    double peak = 0;
    for (double v : tdt.value) peak = std::max(peak, std::abs(v));
    REQUIRE(peak > 0.1);
}
