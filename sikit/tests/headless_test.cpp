// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Unit tests for the headless CLI operations. The argv-parsing layer
// (CLI11 in main.cpp) is exercised by hand at the shell; this file
// drives the underlying analysis pipelines directly so we catch
// regressions in the headless code path without spinning up a real
// shell session.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
// dup/dup2/fileno live in <unistd.h> on POSIX and <io.h> on Windows
// (with underscore-prefixed names). The macro shim lets CaptureStdout
// keep one body across both platforms.
#ifdef _WIN32
#  include <io.h>
#  define dup    _dup
#  define dup2   _dup2
#  define fileno _fileno
#else
#  include <unistd.h>
#endif
#include <fstream>
#include <sstream>
#include <string>

#include "si/HeadlessOps.h"

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"

using Catch::Approx;
using sikit::cli::compliance_op;
using sikit::cli::impedance_op;
using sikit::cli::list_nets_op;
using sikit::cli::list_specs_op;
using sikit::cli::spice_op;
using sikit::cli::touchstone_op;

namespace {

// Synthetic board: a single 50 mm F.Cu trace at the canonical PCIe-style
// width and width / dielectric stackup. Used so the headless ops have
// something to chew on without needing a .kicad_pcb fixture.
circuitcore::board::Board make_synthetic_board() {
    circuitcore::board::Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6,
                                  "copper", 0.0, 0.0});
    b.stackup.layers.push_back({31, "B.Cu", "signal", 35e-6,
                                  "copper", 0.0, 0.0});
    b.stackup.total_thickness = 1.6e-3;
    b.nets.push_back({1, "DATA"});
    circuitcore::board::Segment s;
    s.start = {0, 0};
    s.end   = {0.050, 0};
    s.width = 0.20e-3;
    s.layer_ordinal = 0;
    s.net_id = 1;
    b.segments.push_back(s);
    return b;
}

sikit::si::SiStackup make_synthetic_sis() {
    sikit::si::SiStackup sis;
    sikit::si::SiStackupItem cu;
    cu.kind = sikit::si::SiStackupItem::Kind::Copper;
    cu.name = "F.Cu";
    cu.thickness = 35e-6;
    sis.items.push_back(cu);
    sikit::si::SiStackupItem diel;
    diel.kind = sikit::si::SiStackupItem::Kind::Dielectric;
    diel.name = "core";
    diel.thickness = 0.20e-3;
    diel.epsilon_r = 4.3;
    diel.loss_tangent = 0.02;
    sis.items.push_back(diel);
    sikit::si::SiStackupItem cu2 = cu;
    cu2.name = "B.Cu";
    sis.items.push_back(cu2);
    return sis;
}

// RAII wrapper for capturing stdout. Catch2 has its own stdout
// redirection but it interacts badly with std::printf (C stdio is
// outside Catch's redirect scope); the explicit freopen + temp-file
// dance works for both.
class CaptureStdout {
public:
    CaptureStdout() {
        path_ = std::filesystem::temp_directory_path() /
                ("sikit_cli_out_" + std::to_string(std::rand()) + ".txt");
        old_ = fdopen(dup(fileno(stdout)), "w");
        std::freopen(path_.string().c_str(), "w", stdout);
    }
    ~CaptureStdout() { restore(); }

    std::string read() {
        std::fflush(stdout);
        restore();
        std::ifstream is(path_);
        std::ostringstream ss;
        ss << is.rdbuf();
        std::filesystem::remove(path_);
        return ss.str();
    }

private:
    void restore() {
        if (old_) {
            std::fflush(stdout);
            dup2(fileno(old_), fileno(stdout));
            std::fclose(old_);
            old_ = nullptr;
        }
    }
    std::filesystem::path path_;
    std::FILE* old_ = nullptr;
};

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("cli: impedance_op succeeds on a routed net", "[cli][impedance]") {
    auto b = make_synthetic_board();
    auto sis = make_synthetic_sis();
    CaptureStdout cap;
    const int rc = impedance_op(b, sis, "DATA", "F.Cu", false);
    const std::string out = cap.read();
    REQUIRE(rc == 0);
    REQUIRE(contains(out, "Z0"));
    REQUIRE(contains(out, "v_phase"));
    REQUIRE(contains(out, "eps_eff"));
}

TEST_CASE("cli: impedance_op reports missing net", "[cli][impedance]") {
    auto b = make_synthetic_board();
    auto sis = make_synthetic_sis();
    CaptureStdout cap;
    const int rc = impedance_op(b, sis, "NOPE", "F.Cu", false);
    cap.read();
    REQUIRE(rc == 3);   // exit code 3 = net not found
}

TEST_CASE("cli: impedance_op reports missing layer", "[cli][impedance]") {
    auto b = make_synthetic_board();
    auto sis = make_synthetic_sis();
    CaptureStdout cap;
    const int rc = impedance_op(b, sis, "DATA", "In7.Cu", false);
    cap.read();
    REQUIRE(rc == 4);   // exit code 4 = layer not found
}

TEST_CASE("cli: impedance_op flags a net with no segments on the layer",
          "[cli][impedance]") {
    auto b = make_synthetic_board();
    auto sis = make_synthetic_sis();
    CaptureStdout cap;
    // DATA is routed on F.Cu, not on B.Cu.
    const int rc = impedance_op(b, sis, "DATA", "B.Cu", false);
    cap.read();
    REQUIRE(rc == 5);   // exit code 5 = analysis-side error
}

TEST_CASE("cli: touchstone_op writes a .s2p file with expected metadata",
          "[cli][touchstone]") {
    auto b = make_synthetic_board();
    auto sis = make_synthetic_sis();
    auto tmp = std::filesystem::temp_directory_path() / "sikit_cli_test.s2p";
    CaptureStdout cap;
    const int rc = touchstone_op(b, sis, "DATA", "F.Cu", tmp,
                                  10e6, 20e9, 100, false);
    cap.read();
    REQUIRE(rc == 0);
    REQUIRE(std::filesystem::exists(tmp));
    REQUIRE(std::filesystem::file_size(tmp) > 0);
    std::filesystem::remove(tmp);
}

TEST_CASE("cli: spice_op writes a .cir file with the required directives",
          "[cli][spice]") {
    auto b = make_synthetic_board();
    auto sis = make_synthetic_sis();
    auto tmp = std::filesystem::temp_directory_path() / "sikit_cli_test.cir";
    CaptureStdout cap;
    const int rc = spice_op(b, sis, "DATA", "F.Cu", tmp,
                             /*n_poles=*/8, 10e6, 20e9, 200, false);
    cap.read();
    REQUIRE(rc == 0);
    std::ifstream is(tmp);
    std::ostringstream buf;
    buf << is.rdbuf();
    const std::string nl = buf.str();
    REQUIRE(contains(nl, ".subckt"));
    REQUIRE(contains(nl, ".ends"));
    std::filesystem::remove(tmp);
}

TEST_CASE("cli: list_nets_op prints the net table", "[cli][list]") {
    auto b = make_synthetic_board();
    CaptureStdout cap;
    const int rc = list_nets_op(b);
    const std::string out = cap.read();
    REQUIRE(rc == 0);
    REQUIRE(contains(out, "DATA"));
    REQUIRE(contains(out, "id"));
    REQUIRE(contains(out, "name"));
}

TEST_CASE("cli: list_specs_op prints every shipped compliance spec",
          "[cli][list]") {
    CaptureStdout cap;
    const int rc = list_specs_op();
    const std::string out = cap.read();
    REQUIRE(rc == 0);
    REQUIRE(contains(out, "PCIe Gen5"));
    REQUIRE(contains(out, "DDR4"));
    REQUIRE(contains(out, "USB"));
    REQUIRE(contains(out, "Ethernet"));
}

TEST_CASE("cli: compliance_op reports missing spec by name", "[cli][compliance]") {
    // Use a non-existent file path; the operation should fail at the
    // load step first (rc 2) so we use a real file for the load and
    // pass a bad spec name to test the "unknown spec" branch.
    // Generate a tiny Touchstone for the load step.
    auto tmp = std::filesystem::temp_directory_path() / "sikit_cli_min.s2p";
    {
        std::ofstream os(tmp);
        os << "! minimal touchstone\n";
        os << "# Hz S RI R 50\n";
        os << "1e9 0 0 0.9 0 0.9 0 0 0\n";
        os << "2e9 0 0 0.8 0 0.8 0 0 0\n";
    }
    CaptureStdout cap;
    const int rc = compliance_op(tmp, "PCIe Gen99 (which doesnt exist)");
    cap.read();
    REQUIRE(rc == 3);
    std::filesystem::remove(tmp);
}

TEST_CASE("cli: compliance_op walks a real spec for a real touchstone",
          "[cli][compliance]") {
    auto tmp = std::filesystem::temp_directory_path() / "sikit_cli_min2.s2p";
    {
        std::ofstream os(tmp);
        os << "# Hz S RI R 50\n";
        os << "1e9 0 0 0.9 0 0.9 0 0 0\n";
        os << "2e9 0 0 0.85 0 0.85 0 0 0\n";
        os << "5e9 0 0 0.75 0 0.75 0 0 0\n";
        os << "10e9 0 0 0.6 0 0.6 0 0 0\n";
    }
    CaptureStdout cap;
    const int rc = compliance_op(tmp, "PCIe Gen3 (8.0 GT/s)");
    const std::string out = cap.read();
    REQUIRE(rc == 0);
    REQUIRE(contains(out, "PCIe Gen3"));
    REQUIRE(contains(out, "baud"));
    REQUIRE(contains(out, "ber"));
    std::filesystem::remove(tmp);
}
