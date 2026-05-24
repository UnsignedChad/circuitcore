#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "si/SpiceExport.h"
#include "si/Touchstone.h"
#include "si/VectorFit.h"

using namespace sikit::si;
using Complex = std::complex<double>;

namespace {

VectorFitResult make_two_pole_fit() {
    VectorFitResult r;
    r.poles    = {-1e9, -2e10};
    r.residues = { 3e9,  5e10};
    r.d = 0.5;
    r.iterations = 4;
    r.converged = true;
    r.rms_error = 1e-5;
    return r;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int count_lines_starting_with(const std::string& s, const std::string& prefix) {
    int count = 0;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind(prefix, 0) == 0) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("spice_export: subckt skeleton has the required directives",
          "[spice]") {
    auto fit = make_two_pole_fit();
    SpiceExportOptions opts;
    opts.subckt_name = "CHAN";
    auto netlist = spice_subckt_from_fit(fit, opts);

    REQUIRE(contains(netlist, ".subckt CHAN in out"));
    REQUIRE(contains(netlist, ".ends CHAN"));
    REQUIRE(contains(netlist, "B_out out 0 V="));
}

TEST_CASE("spice_export: emits one R and one C per pole", "[spice]") {
    auto fit = make_two_pole_fit();
    auto netlist = spice_subckt_from_fit(fit, {});
    REQUIRE(count_lines_starting_with(netlist, "R") == 2);
    REQUIRE(count_lines_starting_with(netlist, "C") == 2);
}

TEST_CASE("spice_export: capacitor values are C_n = -1/p_n",
          "[spice]") {
    auto fit = make_two_pole_fit();
    auto netlist = spice_subckt_from_fit(fit, {});
    // p_0 = -1e9, so C_0 = 1e-9.
    REQUIRE(contains(netlist, "C0 n0 0 1.000000e-09"));
    // p_1 = -2e10, so C_1 = 5e-11.
    REQUIRE(contains(netlist, "C1 n1 0 5.000000e-11"));
}

TEST_CASE("spice_export: summing source includes residues and d-term",
          "[spice]") {
    auto fit = make_two_pole_fit();
    auto netlist = spice_subckt_from_fit(fit, {});
    REQUIRE(contains(netlist, "*V(in)"));
    REQUIRE(contains(netlist, "*V(n0)"));
    REQUIRE(contains(netlist, "*V(n1)"));
}

TEST_CASE("spice_export: header carries the fit summary when enabled",
          "[spice]") {
    auto fit = make_two_pole_fit();
    SpiceExportOptions opts;
    opts.include_header = true;
    auto netlist = spice_subckt_from_fit(fit, opts);
    REQUIRE(contains(netlist, "poles      = 2"));
    REQUIRE(contains(netlist, "converged  = yes"));
    REQUIRE(contains(netlist, "rms error  ="));
}

TEST_CASE("spice_export: header suppressed when disabled", "[spice]") {
    auto fit = make_two_pole_fit();
    SpiceExportOptions opts;
    opts.include_header = false;
    auto netlist = spice_subckt_from_fit(fit, opts);
    REQUIRE_FALSE(contains(netlist, "Vector-fit summary"));
}

TEST_CASE("spice_export: rejects invalid subckt names", "[spice]") {
    auto fit = make_two_pole_fit();
    SpiceExportOptions opts;
    opts.subckt_name = "1Channel";   // leading digit not allowed
    REQUIRE_THROWS(spice_subckt_from_fit(fit, opts));
    opts.subckt_name = "Has Space";
    REQUIRE_THROWS(spice_subckt_from_fit(fit, opts));
    opts.subckt_name = "";
    REQUIRE_THROWS(spice_subckt_from_fit(fit, opts));
}

TEST_CASE("spice_export: roundtrip through a synthetic Touchstone",
          "[spice]") {
    // Build a small Touchstone whose S21 follows a 2-pole model. Then
    // run the end-to-end exporter and check the resulting .subckt is
    // structurally sound.
    using namespace sikit::touchstone;
    TouchstoneFile ts;
    ts.num_ports = 2;
    ts.reference_impedance = 50.0;
    ts.format = Format::RealImaginary;
    ts.frequency_scale = 1.0;

    auto fit = make_two_pole_fit();
    // 200 log-spaced freq points across DC-30 GHz.
    for (int k = 0; k < 200; ++k) {
        const double t = static_cast<double>(k) / 199.0;
        const double f = 1e6 * std::pow(30e3, t);   // 1 MHz .. 30 GHz
        ts.frequencies.push_back(f);
        const Complex s(0.0, 2.0 * 3.14159265358979323846 * f);
        Complex y(fit.d, 0.0);
        for (std::size_t n = 0; n < fit.poles.size(); ++n) {
            y += fit.residues[n] / (s - fit.poles[n]);
        }
        // Touchstone storage: column-major [S11, S21, S12, S22].
        ts.s_matrices.push_back({Complex(0,0), y, y, Complex(0,0)});
    }

    SpiceExportOptions opts;
    opts.subckt_name = "TEST";
    opts.fit.n_poles = 2;
    auto netlist = spice_subckt_from_touchstone(ts, opts);

    // Same structural checks as the from-fit path.
    REQUIRE(contains(netlist, ".subckt TEST in out"));
    REQUIRE(count_lines_starting_with(netlist, "R") == 2);
    REQUIRE(count_lines_starting_with(netlist, "C") == 2);
}

TEST_CASE("spice_export: write_spice_subckt produces a readable file",
          "[spice]") {
    using namespace sikit::touchstone;
    TouchstoneFile ts;
    ts.num_ports = 2;
    ts.reference_impedance = 50.0;
    ts.format = Format::RealImaginary;
    ts.frequency_scale = 1.0;

    auto fit = make_two_pole_fit();
    for (int k = 0; k < 200; ++k) {
        const double t = static_cast<double>(k) / 199.0;
        const double f = 1e6 * std::pow(30e3, t);
        ts.frequencies.push_back(f);
        const Complex s(0.0, 2.0 * 3.14159265358979323846 * f);
        Complex y(fit.d, 0.0);
        for (std::size_t n = 0; n < fit.poles.size(); ++n) {
            y += fit.residues[n] / (s - fit.poles[n]);
        }
        ts.s_matrices.push_back({Complex(0,0), y, y, Complex(0,0)});
    }

    auto tmp = std::filesystem::temp_directory_path()
               / "sikit_spice_test.cir";
    SpiceExportOptions opts;
    opts.subckt_name = "TEST_FILE";
    opts.fit.n_poles = 2;
    REQUIRE(write_spice_subckt(ts, tmp, opts));

    std::ifstream is(tmp);
    REQUIRE(is.good());
    std::ostringstream buf;
    buf << is.rdbuf();
    REQUIRE(contains(buf.str(), ".subckt TEST_FILE"));
    std::filesystem::remove(tmp);
}

TEST_CASE("spice_export: throws on empty Touchstone", "[spice]") {
    using namespace sikit::touchstone;
    TouchstoneFile ts;
    ts.num_ports = 2;
    REQUIRE_THROWS(spice_subckt_from_touchstone(ts));
}
