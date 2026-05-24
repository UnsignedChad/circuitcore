#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/Touchstone.h"

using namespace sikit::touchstone;
using Catch::Approx;

// Small synthetic 2-port file. Touchstone 2-port order is S11 S21 S12 S22.
// Real/imag here exercise straightforward parsing and column-major storage.
constexpr auto kS2pRI = R"(
! Sample 2-port S-parameter file in RI format
# Hz S RI R 50
1.0e9   0.10  0.20   0.30  0.40   0.50  0.60   0.70  0.80
2.0e9  -0.10 -0.20  -0.30 -0.40  -0.50 -0.60  -0.70 -0.80
)";

// Same values, MA format. mag = sqrt(re² + im²), angle in degrees.
// For (re=0.1, im=0.2): mag = sqrt(0.05) ≈ 0.22360679, ang = atan2(0.2,0.1) ≈ 63.43494882°
constexpr auto kS2pMA = R"(
# Hz S MA R 50
1.0e9   0.22360679774997896  63.43494882292201   0.5  53.13010235415598   0.7810249675906655  50.19442890773481   1.0630145812734649  48.81407483429036
)";

TEST_CASE("touchstone: parse RI 2-port", "[touchstone]") {
    auto f = TouchstoneReader::read_string(kS2pRI, 2);
    REQUIRE(f.num_ports == 2);
    REQUIRE(f.format == Format::RealImaginary);
    REQUIRE(f.reference_impedance == Approx(50.0));
    REQUIRE(f.frequencies.size() == 2);
    REQUIRE(f.frequencies[0] == Approx(1.0e9));
    REQUIRE(f.frequencies[1] == Approx(2.0e9));

    // First frequency point: S11=0.1+0.2j, S21=0.3+0.4j, S12=0.5+0.6j, S22=0.7+0.8j
    const auto& m = f.s_matrices[0];
    // Column-major: mat[r + c*2]; for 2-port: [S11, S21, S12, S22]
    REQUIRE(m[0].real() == Approx(0.1));  // S11
    REQUIRE(m[0].imag() == Approx(0.2));
    REQUIRE(m[1].real() == Approx(0.3));  // S21
    REQUIRE(m[1].imag() == Approx(0.4));
    REQUIRE(m[2].real() == Approx(0.5));  // S12
    REQUIRE(m[2].imag() == Approx(0.6));
    REQUIRE(m[3].real() == Approx(0.7));  // S22
    REQUIRE(m[3].imag() == Approx(0.8));
}

TEST_CASE("touchstone: MA format converts to same complex values as RI", "[touchstone]") {
    auto ri = TouchstoneReader::read_string(kS2pRI, 2);
    auto ma = TouchstoneReader::read_string(kS2pMA, 2);

    REQUIRE(ma.format == Format::MagnitudeAngle);
    REQUIRE(ma.s_matrices.size() == 1);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(ma.s_matrices[0][i].real() == Approx(ri.s_matrices[0][i].real()).margin(1e-6));
        REQUIRE(ma.s_matrices[0][i].imag() == Approx(ri.s_matrices[0][i].imag()).margin(1e-6));
    }
}

TEST_CASE("touchstone: DB format converts magnitude correctly", "[touchstone]") {
    // mag 0.5 in dB = 20*log10(0.5) ≈ -6.0206 dB
    constexpr auto kDb = R"(
# Hz S DB R 50
1.0e9  -6.0206  0   -6.0206 0   -6.0206 0   -6.0206 0
)";
    auto f = TouchstoneReader::read_string(kDb, 2);
    for (const auto& s : f.s_matrices[0]) {
        REQUIRE(std::abs(s) == Approx(0.5).margin(1e-4));
        REQUIRE(s.imag() == Approx(0.0).margin(1e-6));
    }
}

TEST_CASE("touchstone: frequency unit MHz scales to Hz", "[touchstone]") {
    constexpr auto src = R"(
# MHz S RI R 50
1000  0 0   0 0   0 0   0 0
)";
    auto f = TouchstoneReader::read_string(src, 2);
    REQUIRE(f.frequency_scale == Approx(1e6));
    REQUIRE(f.frequencies[0] == Approx(1.0e9));  // 1000 MHz → 1 GHz
}

TEST_CASE("touchstone: GHz unit", "[touchstone]") {
    constexpr auto src = R"(
# GHz S RI R 75
2.5  0 0   0 0   0 0   0 0
)";
    auto f = TouchstoneReader::read_string(src, 2);
    REQUIRE(f.reference_impedance == Approx(75.0));
    REQUIRE(f.frequencies[0] == Approx(2.5e9));
}

TEST_CASE("touchstone: 4-port file is row-major in file, column-major in storage", "[touchstone]") {
    // 4-port: 16 complex S-params per row, row-major in file order.
    // We test that S[r=0,c=1] in storage equals the second complex pair on the line.
    constexpr auto src = R"(
# Hz S RI R 50
1.0e9
   0 0    1 0    2 0    3 0
   4 0    5 0    6 0    7 0
   8 0    9 0   10 0   11 0
  12 0   13 0   14 0   15 0
)";
    auto f = TouchstoneReader::read_string(src, 4);
    REQUIRE(f.s_matrices.size() == 1);
    const auto& m = f.s_matrices[0];
    // file value at row 0 col 1 is "1" → storage mat[0 + 1*4] = m[4]
    REQUIRE(m[0 + 1 * 4].real() == Approx(1.0));
    // file value at row 2 col 3 is "11" → storage mat[2 + 3*4] = m[14]
    REQUIRE(m[2 + 3 * 4].real() == Approx(11.0));
    // file value at row 3 col 0 is "12" → storage mat[3 + 0*4] = m[3]
    REQUIRE(m[3 + 0 * 4].real() == Approx(12.0));
}

TEST_CASE("touchstone: multi-line data record spans line breaks", "[touchstone]") {
    // Touchstone allows whitespace between any two values, including line breaks.
    constexpr auto src = R"(
# Hz S RI R 50
1.0e9
   0.1 0.2
   0.3 0.4
   0.5 0.6
   0.7 0.8
)";
    auto f = TouchstoneReader::read_string(src, 2);
    REQUIRE(f.s_matrices.size() == 1);
    REQUIRE(f.s_matrices[0][0].real() == Approx(0.1));
    REQUIRE(f.s_matrices[0][3].imag() == Approx(0.8));
}

TEST_CASE("touchstone: comments and blank lines ignored", "[touchstone]") {
    constexpr auto src = R"(
! lots of comments
! more comments
# Hz S RI R 50
! comment in middle
1e9  0 0  0 0  0 0  0 0

! trailing comment
2e9  0 0  0 0  0 0  0 0
)";
    auto f = TouchstoneReader::read_string(src, 2);
    REQUIRE(f.frequencies.size() == 2);
}

TEST_CASE("touchstone: missing option line throws", "[touchstone]") {
    constexpr auto src = "1e9 0 0 0 0 0 0 0 0";
    REQUIRE_THROWS_AS(TouchstoneReader::read_string(src, 2), TouchstoneParseError);
}

TEST_CASE("touchstone: wrong data count for num_ports throws", "[touchstone]") {
    // 2-port needs 1 + 2*4 = 9 floats per record; this has 8 (one missing).
    constexpr auto src = R"(
# Hz S RI R 50
1e9  0 0  0 0  0 0  0
)";
    REQUIRE_THROWS_AS(TouchstoneReader::read_string(src, 2), TouchstoneParseError);
}

TEST_CASE("touchstone: unknown format throws", "[touchstone]") {
    constexpr auto src = R"(
# Hz S XX R 50
1e9 0 0 0 0 0 0 0 0
)";
    REQUIRE_THROWS_AS(TouchstoneReader::read_string(src, 2), TouchstoneParseError);
}

TEST_CASE("touchstone: non-S parameter type throws (Y/Z not supported v0)", "[touchstone]") {
    constexpr auto src = R"(
# Hz Y RI R 50
1e9 0 0 0 0 0 0 0 0
)";
    REQUIRE_THROWS_AS(TouchstoneReader::read_string(src, 2), TouchstoneParseError);
}
