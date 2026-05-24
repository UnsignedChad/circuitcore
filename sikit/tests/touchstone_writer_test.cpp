#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>

#include "si/Touchstone.h"
#include "si/TouchstoneWriter.h"

using namespace sikit::touchstone;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {

TouchstoneFile small_2port() {
    TouchstoneFile f;
    f.num_ports = 2;
    f.format = Format::RealImaginary;
    f.reference_impedance = 50.0;
    f.frequency_scale = 1.0;
    f.frequencies = {1e9, 2e9, 3e9};
    // Storage is column-major: [S11, S21, S12, S22]
    f.s_matrices = {
        {Complex(0.1, 0.2), Complex(0.3, 0.4), Complex(0.5, 0.6), Complex(0.7, 0.8)},
        {Complex(0.11, 0.21), Complex(0.31, 0.41), Complex(0.51, 0.61), Complex(0.71, 0.81)},
        {Complex(0.12, 0.22), Complex(0.32, 0.42), Complex(0.52, 0.62), Complex(0.72, 0.82)},
    };
    return f;
}

TouchstoneFile small_4port() {
    TouchstoneFile f;
    f.num_ports = 4;
    f.format = Format::RealImaginary;
    f.reference_impedance = 50.0;
    f.frequency_scale = 1.0;
    f.frequencies = {1e9};
    std::vector<Complex> mat(16);
    for (int i = 0; i < 16; ++i) mat[i] = Complex(i + 1.0, 0.0);
    f.s_matrices = {mat};
    return f;
}

}  // namespace

TEST_CASE("ts-write: round-trip 2-port preserves complex S values", "[ts-write]") {
    auto orig = small_2port();
    auto txt = TouchstoneWriter::to_string(orig);
    auto loaded = TouchstoneReader::read_string(txt, 2);

    REQUIRE(loaded.num_ports == 2);
    REQUIRE(loaded.frequencies.size() == orig.frequencies.size());
    for (std::size_t k = 0; k < orig.frequencies.size(); ++k) {
        REQUIRE(loaded.frequencies[k] == Approx(orig.frequencies[k]));
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(loaded.s_matrices[k][i].real() ==
                    Approx(orig.s_matrices[k][i].real()).margin(1e-9));
            REQUIRE(loaded.s_matrices[k][i].imag() ==
                    Approx(orig.s_matrices[k][i].imag()).margin(1e-9));
        }
    }
}

TEST_CASE("ts-write: round-trip 4-port preserves row-major <-> column-major", "[ts-write]") {
    auto orig = small_4port();
    auto txt = TouchstoneWriter::to_string(orig);
    auto loaded = TouchstoneReader::read_string(txt, 4);

    REQUIRE(loaded.num_ports == 4);
    REQUIRE(loaded.s_matrices.size() == 1);
    for (std::size_t i = 0; i < 16; ++i) {
        REQUIRE(loaded.s_matrices[0][i].real() ==
                Approx(orig.s_matrices[0][i].real()).margin(1e-9));
    }
}

TEST_CASE("ts-write: emits Hz units and RI format", "[ts-write]") {
    auto f = small_2port();
    auto txt = TouchstoneWriter::to_string(f);
    REQUIRE(txt.find("# Hz S RI R") != std::string::npos);
}

TEST_CASE("ts-write: reference impedance preserved", "[ts-write]") {
    auto f = small_2port();
    f.reference_impedance = 75.0;
    auto txt = TouchstoneWriter::to_string(f);
    auto loaded = TouchstoneReader::read_string(txt, 2);
    REQUIRE(loaded.reference_impedance == Approx(75.0));
}

TEST_CASE("ts-write: rejects port-count mismatch", "[ts-write]") {
    TouchstoneFile f;
    f.num_ports = 2;
    f.frequencies = {1e9};
    f.s_matrices = {{Complex(0, 0)}};  // only 1 entry, expected 4
    REQUIRE_THROWS_AS(TouchstoneWriter::to_string(f), TouchstoneParseError);
}

TEST_CASE("ts-write: rejects freq vs matrix length mismatch", "[ts-write]") {
    auto f = small_2port();
    f.frequencies.push_back(4e9);  // 4 freqs, only 3 matrices
    REQUIRE_THROWS_AS(TouchstoneWriter::to_string(f), TouchstoneParseError);
}
