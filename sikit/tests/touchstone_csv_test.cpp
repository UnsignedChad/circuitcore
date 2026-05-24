#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <sstream>
#include <string>

#include "si/Touchstone.h"
#include "si/TouchstoneCsv.h"

using namespace sikit::touchstone;
using Catch::Approx;
using Complex = std::complex<double>;

namespace {
TouchstoneFile small_2port() {
    TouchstoneFile f;
    f.num_ports = 2;
    f.reference_impedance = 50.0;
    f.frequencies = {1e9, 2e9};
    // Matched line (S11=0, |S21|=1) at 1 GHz, slightly lossy at 2 GHz.
    f.s_matrices = {
        {Complex(0, 0), Complex(1, 0), Complex(1, 0), Complex(0, 0)},
        {Complex(0.1, 0), Complex(0.9, -0.2), Complex(0.9, -0.2), Complex(0.1, 0)},
    };
    return f;
}
}  // namespace

TEST_CASE("ts-csv: header row is present and correct", "[ts-csv]") {
    auto s = TouchstoneCsv::to_string(small_2port());
    REQUIRE(s.starts_with("freq_hz,s11_mag_db,s11_phase_deg,s21_mag_db,"));
}

TEST_CASE("ts-csv: matched line row has S11 = -inf dB", "[ts-csv]") {
    auto s = TouchstoneCsv::to_string(small_2port());
    // First data line is at 1 GHz with S11 = 0 → -inf dB. format() emits "-inf".
    REQUIRE(s.find("1.000000e+09") != std::string::npos);
    REQUIRE(s.find("-inf") != std::string::npos);
}

TEST_CASE("ts-csv: S21 magnitudes drop with the second row's lossy entry",
          "[ts-csv]") {
    auto s = TouchstoneCsv::to_string(small_2port());
    // The 2 GHz row should have S21 mag_db < 0 (|S21| ≈ 0.92 → -0.7 dB).
    // Verify by reading the file back and locating the second freq line.
    std::istringstream is(s);
    std::string line;
    std::getline(is, line);   // header
    std::getline(is, line);   // 1 GHz row
    std::getline(is, line);   // 2 GHz row
    // Tokenise commas
    std::vector<std::string> cols;
    std::stringstream ls(line);
    std::string col;
    while (std::getline(ls, col, ',')) cols.push_back(col);
    REQUIRE(cols.size() >= 4);
    const double s21_db = std::stod(cols[3]);  // s21_mag_db column
    REQUIRE(s21_db < 0.0);
    REQUIRE(s21_db > -3.0);
}

TEST_CASE("ts-csv: rejects 1-port files", "[ts-csv]") {
    TouchstoneFile f;
    f.num_ports = 1;
    f.frequencies = {1e9};
    f.s_matrices = {{Complex(0, 0)}};
    REQUIRE_THROWS_AS(TouchstoneCsv::to_string(f), TouchstoneParseError);
}

TEST_CASE("ts-csv: rejects malformed s_matrices", "[ts-csv]") {
    TouchstoneFile f;
    f.num_ports = 2;
    f.frequencies = {1e9};
    f.s_matrices = {{Complex(0, 0)}};   // expected 4 entries, got 1
    REQUIRE_THROWS_AS(TouchstoneCsv::to_string(f), TouchstoneParseError);
}

TEST_CASE("ts-csv: z_in equals Zref for perfectly matched port", "[ts-csv]") {
    // At the 1 GHz row S11 = 0 → Z_in = Zref · (1 + 0) / (1 − 0) = Zref.
    auto s = TouchstoneCsv::to_string(small_2port());
    std::istringstream is(s);
    std::string line;
    std::getline(is, line);          // header
    std::getline(is, line);          // 1 GHz row
    std::vector<std::string> cols;
    std::stringstream ls(line);
    std::string col;
    while (std::getline(ls, col, ',')) cols.push_back(col);
    REQUIRE(cols.size() >= 7);
    const double z_in_real = std::stod(cols[5]);
    const double z_in_imag = std::stod(cols[6]);
    REQUIRE(z_in_real == Approx(50.0).margin(1e-6));
    REQUIRE(z_in_imag == Approx(0.0).margin(1e-6));
}
