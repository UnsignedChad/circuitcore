// End-to-end test: real .kicad_pcb file → parse → mesh → solve.
// Locks in the full pipeline against changes in any one stage.

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "circuitcore/formats/kicad/PcbParser.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

#ifndef PDNKIT_TEST_FIXTURES_DIR
#error "PDNKIT_TEST_FIXTURES_DIR must be defined by CMake."
#endif

using circuitcore::formats::kicad::PcbParser;
using pdnkit::pi::IrMesher;
using pdnkit::pi::IrSolver;
using pdnkit::pi::MeshConfig;

namespace {
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(PDNKIT_TEST_FIXTURES_DIR) / name;
}
}

TEST_CASE("e2e: tiny_pdn fixture parses with expected counts", "[e2e]") {
    auto b = PcbParser::parse_file(fixture("tiny_pdn.kicad_pcb"));

    REQUIRE(b.stackup.layers.size() == 11);
    REQUIRE(b.stackup.total_thickness == 1.6e-3);
    REQUIRE(b.nets.size() == 3);  // "", GND, +3V3

    REQUIRE(b.find_net_by_name("GND")->id == 1);
    REQUIRE(b.find_net_by_name("+3V3")->id == 2);

    // 4 pads total: 2 from each footprint
    REQUIRE(b.pads.size() == 4);
    REQUIRE(b.segments.size() == 1);
    REQUIRE(b.vias.size() == 1);
    REQUIRE(b.zones.size() == 2);  // +3V3 on F.Cu, GND on B.Cu
}

TEST_CASE("e2e: through-hole pads on *.Cu expand to all copper layers", "[e2e]") {
    auto b = PcbParser::parse_file(fixture("tiny_pdn.kicad_pcb"));

    // Find the PinHeader pads — they use "*.Cu" so should hit both F.Cu (0)
    // and B.Cu (31). Their footprint origin is (5, 20), pad "1" at (0,0)
    // local → world (5, 20).
    int through_pads = 0;
    for (const auto& p : b.pads) {
        if (p.layer_ordinals.size() == 2) {
            // Sanity: contains both 0 and 31.
            const bool has_f = std::find(p.layer_ordinals.begin(),
                                          p.layer_ordinals.end(), 0)
                                != p.layer_ordinals.end();
            const bool has_b = std::find(p.layer_ordinals.begin(),
                                          p.layer_ordinals.end(), 31)
                                != p.layer_ordinals.end();
            if (has_f && has_b) ++through_pads;
        }
    }
    REQUIRE(through_pads == 2);
}

TEST_CASE("e2e: IR drop on +3V3 F.Cu produces sane voltage map", "[e2e]") {
    auto b = PcbParser::parse_file(fixture("tiny_pdn.kicad_pcb"));

    MeshConfig mc;
    mc.cell_size = 0.5e-3;  // 0.5mm grid
    mc.net_id = b.find_net_by_name("+3V3")->id;
    mc.layer_ordinal = 0;

    auto mesh = IrMesher::build(b, mc);
    REQUIRE(mesh.nodes.size() > 100);             // 19mm × 9mm copper at 0.5mm
    REQUIRE_FALSE(mesh.resistors.empty());
    REQUIRE_FALSE(mesh.source_node_ids.empty());  // 2 pads on +3V3,F.Cu
    REQUIRE_FALSE(mesh.sink_node_ids.empty());

    auto sol = IrSolver::solve(mesh, {1.0});
    REQUIRE(sol.ok);

    // Conservation: every voltage finite, non-negative, sink at ~0V, source at peak.
    for (double v : sol.voltages) {
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= -1e-9);          // tiny slop from large-diagonal pin
        REQUIRE(v < 1.0);             // 1A through a few mm of copper << 1 V
    }
    REQUIRE(sol.max_v > 0.0);
    REQUIRE(sol.min_v < 1e-6);        // sink pinned ~0
    REQUIRE(sol.max_v - sol.min_v > 0.0);
}

TEST_CASE("e2e: board outline parsed from Edge.Cuts", "[e2e]") {
    auto b = PcbParser::parse_file(fixture("tiny_pdn.kicad_pcb"));
    // 4 rectangle lines + 24 arc segments + 48 circle segments = 76 outline lines.
    REQUIRE(b.outline.size() == 76);
    // First segment is the bottom rectangle edge from (0,15) to (20,15).
    REQUIRE(b.outline[0].start.x == 0.0);
    REQUIRE(b.outline[0].end.x == 0.020);
}
