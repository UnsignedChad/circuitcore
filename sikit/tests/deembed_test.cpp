#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <vector>
#include <utility>

#include "si/SParam.h"
#include "si/Touchstone.h"

using namespace sikit::sparam;
using sikit::touchstone::Format;
using sikit::touchstone::TouchstoneFile;
using Complex = std::complex<double>;
using Catch::Approx;

namespace {

// A simple frequency grid the fixture / DUT / measured files all share.
const std::vector<double> kFreqs{1e9, 2e9, 5e9, 10e9};

// Build a 2-port TouchstoneFile from an Eigen S-matrix repeated across
// the kFreqs grid -- used to make test fixtures and DUTs frequency-flat
// so we can verify the de-embedding math in isolation from any
// frequency-dependent shape.
TouchstoneFile flat_two_port(const Eigen::Matrix2cd& s) {
    TouchstoneFile ts;
    ts.num_ports = 2;
    ts.reference_impedance = 50.0;
    ts.format = Format::RealImaginary;
    ts.frequency_scale = 1.0;
    for (double f : kFreqs) {
        (void)f;
        std::vector<Complex> flat{s(0, 0), s(1, 0), s(0, 1), s(1, 1)};
        ts.s_matrices.push_back(std::move(flat));
    }
    ts.frequencies = kFreqs;
    return ts;
}

bool approx_equal(const Eigen::Matrix2cd& A, const Eigen::Matrix2cd& B,
                   double tol = 1e-9) {
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            if (std::abs(A(r, c) - B(r, c)) > tol) return false;
        }
    }
    return true;
}

Eigen::Matrix2cd unpack_flat(const std::vector<Complex>& flat) {
    Eigen::Matrix2cd M;
    M(0, 0) = flat[0]; M(1, 0) = flat[1];
    M(0, 1) = flat[2]; M(1, 1) = flat[3];
    return M;
}

}  // namespace

TEST_CASE("deembed: passthrough fixture leaves the measurement unchanged",
          "[deembed]") {
    // Fixture S11 = S22 = 0, S21 = S12 = 1: zero-length zero-loss
    // through.  Both sides of the cascade are this fixture, so the
    // measured == DUT and de-embedding should reproduce it.
    Eigen::Matrix2cd fix;
    fix << Complex(0, 0), Complex(1, 0),
           Complex(1, 0), Complex(0, 0);

    // An arbitrary DUT that we want to recover.
    Eigen::Matrix2cd dut;
    dut << Complex(0.1, 0.2), Complex(0.7, -0.1),
           Complex(0.7, -0.1), Complex(0.2, 0.05);

    auto fix_ts = flat_two_port(fix);
    auto meas_ts = flat_two_port(dut);   // since fixture is identity
    auto recovered = deembed(meas_ts, fix_ts, fix_ts);

    REQUIRE(approx_equal(unpack_flat(recovered.s_matrices[0]), dut));
}

TEST_CASE("deembed: embed-then-deembed round-trip recovers the DUT",
          "[deembed]") {
    // Two distinct fixtures cascaded around a third DUT. Build the
    // measurement = T_L * T_DUT * T_R via cascade(), then deembed.
    Eigen::Matrix2cd L, R, dut;
    L << Complex(0.05, 0),    Complex(0.95,  0),
         Complex(0.95,  0),   Complex(0.05, 0);
    R << Complex(0.08, 0.02), Complex(0.92, -0.05),
         Complex(0.92,-0.05), Complex(0.06, 0.01);
    dut << Complex(0.2, 0.1),  Complex(0.6, -0.3),
           Complex(0.6, -0.3), Complex(0.15, 0.08);

    const auto meas_S = cascade(cascade(L, dut), R);

    auto L_ts = flat_two_port(L);
    auto R_ts = flat_two_port(R);
    auto meas_ts = flat_two_port(meas_S);

    auto recovered = deembed(meas_ts, L_ts, R_ts);
    REQUIRE(approx_equal(unpack_flat(recovered.s_matrices[0]), dut, 1e-6));
}

TEST_CASE("deembed_symmetric: same fixture on both sides round-trips",
          "[deembed]") {
    Eigen::Matrix2cd F, dut;
    F << Complex(0.04, 0.01), Complex(0.96, -0.02),
         Complex(0.96, -0.02), Complex(0.05, 0.01);
    dut << Complex(0.18, 0.07), Complex(0.55, -0.25),
           Complex(0.55, -0.25), Complex(0.20, 0.05);

    const auto meas_S = cascade(cascade(F, dut), F);
    auto fix_ts = flat_two_port(F);
    auto meas_ts = flat_two_port(meas_S);

    auto recovered = deembed_symmetric(meas_ts, fix_ts);
    REQUIRE(approx_equal(unpack_flat(recovered.s_matrices[0]), dut, 1e-6));
}

TEST_CASE("deembed: frequency grid mismatch is rejected", "[deembed]") {
    Eigen::Matrix2cd I;
    I << Complex(0, 0), Complex(1, 0),
         Complex(1, 0), Complex(0, 0);
    auto fix_a = flat_two_port(I);
    auto fix_b = flat_two_port(I);
    fix_b.frequencies = {1e9, 2e9, 5e9, 11e9};  // last point shifted

    REQUIRE_THROWS_AS(deembed(fix_a, fix_b, fix_a), SParamError);
}

TEST_CASE("deembed: non-2-port input is rejected", "[deembed]") {
    Eigen::Matrix2cd I;
    I << Complex(0, 0), Complex(1, 0),
         Complex(1, 0), Complex(0, 0);
    auto fix = flat_two_port(I);
    TouchstoneFile bad;
    bad.num_ports = 4;
    bad.frequencies = kFreqs;
    bad.s_matrices.assign(kFreqs.size(),
                           std::vector<Complex>(16, Complex(0, 0)));
    REQUIRE_THROWS_AS(deembed(bad, fix, fix), SParamError);
}

TEST_CASE("invert_t: T * T^-1 ~= identity for a passthrough", "[deembed][t]") {
    Eigen::Matrix2cd S;
    S << Complex(0.1, 0.05), Complex(0.9, -0.1),
         Complex(0.9, -0.1), Complex(0.08, 0.02);
    const Eigen::Matrix2cd T = s_to_t(S);
    const Eigen::Matrix2cd Tinv = invert_t(T);
    const Eigen::Matrix2cd I = T * Tinv;
    REQUIRE(approx_equal(I, Eigen::Matrix2cd::Identity(), 1e-9));
}

TEST_CASE("invert_t: singular T throws", "[deembed][t]") {
    // Construct a singular T explicitly (zero column).
    Eigen::Matrix2cd T;
    T << Complex(1, 0), Complex(0, 0),
         Complex(0, 0), Complex(0, 0);
    REQUIRE_THROWS_AS(invert_t(T), SParamError);
}
