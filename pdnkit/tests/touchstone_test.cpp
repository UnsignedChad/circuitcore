#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "pi/Touchstone.h"

using pdnkit::pi::TouchstoneSample;
using pdnkit::pi::write_touchstone_z1p;
using Catch::Approx;

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Parse a Touchstone .s1p (Z-form) back into samples. Tolerant: skips
// comment ("!") and option ("#") lines.
std::vector<TouchstoneSample> read_z1p(const std::filesystem::path& p) {
    std::vector<TouchstoneSample> out;
    std::ifstream in(p);
    for (std::string line; std::getline(in, line); ) {
        if (line.empty() || line[0] == '!' || line[0] == '#') continue;
        std::istringstream is(line);
        TouchstoneSample s;
        double re = 0, im = 0;
        if (is >> s.f_hz >> re >> im) {
            s.z = {re, im};
            out.push_back(s);
        }
    }
    return out;
}

std::filesystem::path tmp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST_CASE("touchstone: empty samples -> header only", "[touchstone]") {
    auto p = tmp_path("ts_empty.s1p");
    REQUIRE(write_touchstone_z1p(p, {}, ""));
    auto s = slurp(p);
    REQUIRE(s.find("# Hz Z RI R 50") != std::string::npos);
    std::filesystem::remove(p);
}

TEST_CASE("touchstone: round-trip a few samples", "[touchstone]") {
    std::vector<TouchstoneSample> in = {
        {1.0e6,  {0.05, -0.10}},
        {1.0e7,  {0.10,  0.20}},
        {1.0e8,  {0.50,  1.50}},
        {1.0e9,  {2.00,  0.00}},
    };
    auto p = tmp_path("ts_round.s1p");
    REQUIRE(write_touchstone_z1p(p, in, "pdnkit unit test"));

    auto out = read_z1p(p);
    REQUIRE(out.size() == in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        REQUIRE(out[i].f_hz       == Approx(in[i].f_hz));
        REQUIRE(out[i].z.real()   == Approx(in[i].z.real()));
        REQUIRE(out[i].z.imag()   == Approx(in[i].z.imag()));
    }
    std::filesystem::remove(p);
}

TEST_CASE("touchstone: comment lines emit with ! prefix", "[touchstone]") {
    auto p = tmp_path("ts_comment.s1p");
    REQUIRE(write_touchstone_z1p(p, {{1.0e6, {1.0, 0.0}}},
                                  "line one\nline two"));
    auto s = slurp(p);
    REQUIRE(s.find("! line one") != std::string::npos);
    REQUIRE(s.find("! line two") != std::string::npos);
    std::filesystem::remove(p);
}

TEST_CASE("touchstone: option line is well-formed", "[touchstone]") {
    auto p = tmp_path("ts_opt.s1p");
    REQUIRE(write_touchstone_z1p(p, {{1.0e6, {0.5, 0.5}}}, ""));
    auto s = slurp(p);
    // The format spec: "# <freq-unit> <parameter> <format> R <Z0>".
    // We emit Hz / Z / RI / 50.
    REQUIRE(s.find("# Hz Z RI R 50") != std::string::npos);
    std::filesystem::remove(p);
}
