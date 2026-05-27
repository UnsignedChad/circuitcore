// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <string>

#include "si/Touchstone.h"
#include "si/TouchstoneWriter.h"

using namespace sikit::touchstone;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

}  // namespace

// ----- Reader v2 path ---------------------------------------------------

TEST_CASE("touchstone v2: minimal v2 file parses with version=2 set",
          "[touchstone][v2]") {
    constexpr auto src = R"(
! Synthetic two-port v2 example
[Version] 2.0
# Hz S RI R 50
[Number of Ports] 2
[Two-Port Order] 12_21
[Network Data]
1.0e9   0.1 0.0   0.9 0.0   0.9 0.0   0.2 0.0
2.0e9   0.15 0.0  0.85 0.0  0.85 0.0  0.18 0.0
[End]
)";
    auto t = TouchstoneReader::read_string(src, /*num_ports=*/2);
    REQUIRE(t.version == 2);
    REQUIRE(t.num_ports == 2);
    REQUIRE(t.two_port_order == TwoPortOrder::RowMajor);
    REQUIRE(t.frequencies.size() == 2);
    REQUIRE(t.reference_impedance == Approx(50.0));
    // 12_21 row-major: file row is S11 S12 S21 S22. Storage flips to
    // column-major [S11, S21, S12, S22]. So storage[1] (= S21) should
    // come from the THIRD complex pair on the line.
    REQUIRE(t.s_matrices[0][0].real() == Approx(0.1));   // S11
    REQUIRE(t.s_matrices[0][1].real() == Approx(0.9));   // S21
    REQUIRE(t.s_matrices[0][2].real() == Approx(0.9));   // S12
    REQUIRE(t.s_matrices[0][3].real() == Approx(0.2));   // S22
}

TEST_CASE("touchstone v2: [Number of Ports] overrides filename hint",
          "[touchstone][v2]") {
    // Caller passes num_ports=1; the [Number of Ports] keyword should win.
    constexpr auto src = R"(
[Version] 2.0
# Hz S RI R 50
[Number of Ports] 2
[Network Data]
1.0e9 0.0 0.0 1.0 0.0 1.0 0.0 0.0 0.0
[End]
)";
    auto t = TouchstoneReader::read_string(src, /*num_ports=*/1);
    REQUIRE(t.num_ports == 2);
    REQUIRE(t.s_matrices.size() == 1);
    REQUIRE(t.s_matrices[0].size() == 4);
}

TEST_CASE("touchstone v2: [Reference] populates per-port impedance vector",
          "[touchstone][v2]") {
    constexpr auto src = R"(
[Version] 2.0
# Hz S RI R 50
[Number of Ports] 4
[Reference] 50 50 100 100
[Network Data]
1.0e9   0 0 1 0 0 0 0 0
        1 0 0 0 0 0 0 0
        0 0 0 0 0 0 1 0
        0 0 0 0 1 0 0 0
[End]
)";
    auto t = TouchstoneReader::read_string(src, /*num_ports=*/4);
    REQUIRE(t.port_impedances.size() == 4);
    REQUIRE(t.port_impedances[0] == Approx(50.0));
    REQUIRE(t.port_impedances[2] == Approx(100.0));
    // Scalar reference_impedance picks up the first port's value for
    // backward compat with v1 callers.
    REQUIRE(t.reference_impedance == Approx(50.0));
}

TEST_CASE("touchstone v2: unknown keywords are skipped", "[touchstone][v2]") {
    // [Information], [Comments], and [Noise Data] are spec extensions
    // we don't model yet. The reader should silently skip them and
    // still parse the rest.
    constexpr auto src = R"(
[Version] 2.0
# Hz S RI R 50
[Number of Ports] 1
[Information] vendor stuff goes here
[Comments] this is fine
[Network Data]
1.0e9 0.1 0.2
[End]
)";
    auto t = TouchstoneReader::read_string(src, /*num_ports=*/1);
    REQUIRE(t.num_ports == 1);
    REQUIRE(t.frequencies.size() == 1);
    REQUIRE(t.s_matrices[0][0].real() == Approx(0.1));
    REQUIRE(t.s_matrices[0][0].imag() == Approx(0.2));
}

TEST_CASE("touchstone v2: v1 files still parse via the v1 path",
          "[touchstone][v2]") {
    // No [Version] keyword -> classic v1 path, untouched.
    constexpr auto src = R"(
! v1 file
# GHz S RI R 50
1.0   0.1 0.0   0.9 0.0   0.9 0.0   0.2 0.0
)";
    auto t = TouchstoneReader::read_string(src, /*num_ports=*/2);
    REQUIRE(t.version == 1);
    REQUIRE(t.num_ports == 2);
    REQUIRE(t.s_matrices[0][0].real() == Approx(0.1));
}

TEST_CASE("touchstone v2: rejects [Matrix Format] other than Full",
          "[touchstone][v2]") {
    constexpr auto src = R"(
[Version] 2.0
# Hz S RI R 50
[Number of Ports] 2
[Matrix Format] Lower
[Network Data]
1.0e9 0 0 1 0 1 0 0 0
[End]
)";
    REQUIRE_THROWS_AS(TouchstoneReader::read_string(src, 2), TouchstoneParseError);
}

// ----- Writer v2 path ---------------------------------------------------

TEST_CASE("touchstone v2: writer emits the [Version] keyword block",
          "[touchstone][v2]") {
    TouchstoneFile f;
    f.version = 2;
    f.num_ports = 2;
    f.reference_impedance = 50.0;
    f.format = Format::RealImaginary;
    f.frequencies = {1e9};
    f.s_matrices = {{Complex(0, 0), Complex(1, 0), Complex(1, 0), Complex(0, 0)}};
    const std::string out = TouchstoneWriter::to_string(f);
    REQUIRE(contains(out, "[Version] 2.0"));
    REQUIRE(contains(out, "[Number of Ports] 2"));
    REQUIRE(contains(out, "[Network Data]"));
    REQUIRE(contains(out, "[End]"));
}

TEST_CASE("touchstone v2: writer emits [Reference] when port_impedances set",
          "[touchstone][v2]") {
    TouchstoneFile f;
    f.version = 2;
    f.num_ports = 4;
    f.reference_impedance = 50.0;
    f.port_impedances = {50, 50, 100, 100};
    f.format = Format::RealImaginary;
    f.frequencies = {1e9};
    f.s_matrices.assign(1, std::vector<Complex>(16, Complex(0, 0)));
    const std::string out = TouchstoneWriter::to_string(f);
    REQUIRE(contains(out, "[Reference]"));
    REQUIRE(contains(out, " 50"));
    REQUIRE(contains(out, " 100"));
}

TEST_CASE("touchstone v2: v1 writer output omits the v2 keyword block",
          "[touchstone][v2]") {
    TouchstoneFile f;
    f.version = 1;
    f.num_ports = 2;
    f.reference_impedance = 50.0;
    f.format = Format::RealImaginary;
    f.frequencies = {1e9};
    f.s_matrices = {{Complex(0, 0), Complex(1, 0), Complex(1, 0), Complex(0, 0)}};
    const std::string out = TouchstoneWriter::to_string(f);
    REQUIRE_FALSE(contains(out, "[Version]"));
    REQUIRE_FALSE(contains(out, "[Network Data]"));
    REQUIRE(contains(out, "# Hz S RI R"));
}

TEST_CASE("touchstone v2: writer-then-reader round-trip preserves S-matrices",
          "[touchstone][v2]") {
    TouchstoneFile src;
    src.version = 2;
    src.num_ports = 2;
    src.two_port_order = TwoPortOrder::RowMajor;
    src.reference_impedance = 50.0;
    src.format = Format::RealImaginary;
    src.frequencies = {1e9, 2e9};
    src.s_matrices = {
        {Complex(0.1, 0), Complex(0.9, -0.05),
         Complex(0.9, -0.05), Complex(0.2, 0)},
        {Complex(0.15, 0), Complex(0.85, -0.10),
         Complex(0.85, -0.10), Complex(0.18, 0)},
    };
    const std::string emitted = TouchstoneWriter::to_string(src);
    auto round = TouchstoneReader::read_string(emitted, /*num_ports=*/2);
    REQUIRE(round.version == 2);
    REQUIRE(round.frequencies.size() == src.frequencies.size());
    for (std::size_t k = 0; k < src.frequencies.size(); ++k) {
        REQUIRE(round.frequencies[k] == Approx(src.frequencies[k]));
        for (int i = 0; i < 4; ++i) {
            REQUIRE(std::abs(round.s_matrices[k][i] -
                              src.s_matrices[k][i]) < 1e-9);
        }
    }
}

TEST_CASE("touchstone v2: writer round-trip carries port_impedances back",
          "[touchstone][v2]") {
    TouchstoneFile src;
    src.version = 2;
    src.num_ports = 4;
    src.format = Format::RealImaginary;
    src.reference_impedance = 50.0;
    src.port_impedances = {50, 50, 100, 100};
    src.frequencies = {1e9};
    src.s_matrices.assign(1, std::vector<Complex>(16, Complex(0, 0)));
    const std::string emitted = TouchstoneWriter::to_string(src);
    auto round = TouchstoneReader::read_string(emitted, /*num_ports=*/4);
    REQUIRE(round.port_impedances.size() == 4);
    REQUIRE(round.port_impedances[2] == Approx(100.0));
}
