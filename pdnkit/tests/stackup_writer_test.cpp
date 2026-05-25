#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "StackupWriter.h"
#include "circuitcore/board/Board.h"
#include "circuitcore/sexpr/SExpr.h"

using circuitcore::sexpr::parse;

namespace {

std::filesystem::path tmp(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Construct a minimal kicad_pcb file with a stackup block we can edit.
void write_fixture(const std::filesystem::path& p) {
    std::ofstream out(p);
    out << "(kicad_pcb (version 20240108) (generator pcbnew)\n"
           "  (setup\n"
           "    (stackup\n"
           "      (layer \"F.Cu\"          (type \"copper\") (thickness 0.035))\n"
           "      (layer \"dielectric 1\" (type \"core\")  (thickness 0.51) (material \"FR4\"))\n"
           "      (layer \"B.Cu\"          (type \"copper\") (thickness 0.035))\n"
           "    )\n"
           "  )\n"
           ")\n";
}

}  // namespace

TEST_CASE("stackup-save: same src/dst path is rejected",
          "[stackup-save]") {
    auto p = tmp("ss_same.kicad_pcb");
    write_fixture(p);
    circuitcore::board::Board b;
    auto r = pdnkit::save_modified_stackup(p, p, b);
    REQUIRE_FALSE(r.ok);
    std::filesystem::remove(p);
}

TEST_CASE("stackup-save: updates thickness of matching layers",
          "[stackup-save][validation]") {
    auto src = tmp("ss_src.kicad_pcb");
    auto dst = tmp("ss_dst.kicad_pcb");
    write_fixture(src);

    circuitcore::board::Board b;
    // 2 oz copper instead of 1 oz (35 um -> 70 um = 0.070 mm).
    b.stackup.layers.push_back({0,  "F.Cu", "signal", 70.0e-6});
    b.stackup.layers.push_back({31, "B.Cu", "signal", 70.0e-6});

    auto r = pdnkit::save_modified_stackup(src, dst, b);
    REQUIRE(r.ok);
    REQUIRE(r.layers_updated == 2);

    // Re-parse the destination and verify the F.Cu thickness was rewritten.
    auto root = parse(slurp(dst));
    REQUIRE(slurp(dst).find("0.07") != std::string::npos);
    REQUIRE(slurp(dst).find("0.035") == std::string::npos);
    // Dielectric thickness (0.51) is untouched since the board has no
    // matching dielectric layer.
    REQUIRE(slurp(dst).find("0.51") != std::string::npos);

    std::filesystem::remove(src);
    std::filesystem::remove(dst);
}

TEST_CASE("stackup-save: missing stackup block fails cleanly",
          "[stackup-save]") {
    auto src = tmp("ss_nostack.kicad_pcb");
    {
        std::ofstream out(src);
        out << "(kicad_pcb (version 20240108) (generator pcbnew))\n";
    }
    auto dst = tmp("ss_nostack_out.kicad_pcb");
    circuitcore::board::Board b;
    auto r = pdnkit::save_modified_stackup(src, dst, b);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.error.find("stackup") != std::string::npos);
    std::filesystem::remove(src);
}
