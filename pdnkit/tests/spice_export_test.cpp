#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "pi/SpiceExport.h"

using namespace pdnkit::pi;
using namespace circuitcore::board;

namespace {

// Tiny hand-built mesh: 3 nodes in a row, 2 resistors, source on N0,
// sink on N2. Enough surface area to verify every line type.
IrMesh make_chain_mesh() {
    IrMesh m;
    m.nodes.push_back({0, 0.000, 0.0, 0, 0, 0});
    m.nodes.push_back({1, 0.001, 0.0, 1, 0, 0});
    m.nodes.push_back({2, 0.002, 0.0, 2, 0, 0});
    // 10 ohm each (G = 0.1)
    m.resistors.push_back({0, 1, 0.1});
    m.resistors.push_back({1, 2, 0.1});
    m.source_node_ids = {0};
    m.sink_node_ids = {2};
    return m;
}

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int count_lines(const std::string& s, const std::string& prefix) {
    int n = 0;
    std::istringstream is(s);
    for (std::string line; std::getline(is, line); ) {
        if (line.rfind(prefix, 0) == 0) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("spice: empty mesh -> minimal netlist", "[spice]") {
    IrMesh m;
    auto out = export_spice(m);
    REQUIRE(contains(out, ".end"));
    REQUIRE(count_lines(out, "R") == 0);
    REQUIRE(count_lines(out, "I") == 0);
    REQUIRE(count_lines(out, "Vsink") == 0);
}

TEST_CASE("spice: chain mesh has expected R / I / V lines", "[spice]") {
    auto m = make_chain_mesh();
    SpiceExportConfig cfg;
    cfg.include_position_comments = false;  // tighter assertions
    cfg.default_total_current = 1.0;
    auto out = export_spice(m, cfg);

    // 2 resistors, both 10 ohm.
    REQUIRE(count_lines(out, "R") == 2);
    REQUIRE(contains(out, "R0 N0 N1 1.000000e+01"));
    REQUIRE(contains(out, "R1 N1 N2 1.000000e+01"));

    // 1 source injector of 1.0 A on N0.
    REQUIRE(count_lines(out, "I") == 1);
    REQUIRE(contains(out, "I0 0 N0 DC 1.000000e+00"));

    // 1 sink tying N2 to ground.
    REQUIRE(count_lines(out, "Vsink") == 1);
    REQUIRE(contains(out, "Vsink0 N2 0 DC 0"));

    REQUIRE(contains(out, ".op"));
    REQUIRE(contains(out, ".end"));
}

TEST_CASE("spice: explicit node_currents override source list", "[spice]") {
    auto m = make_chain_mesh();
    m.node_currents.push_back({0, 0.5});
    m.node_currents.push_back({1, 0.25});
    SpiceExportConfig cfg;
    cfg.include_position_comments = false;
    auto out = export_spice(m, cfg);

    REQUIRE(count_lines(out, "I") == 2);
    REQUIRE(contains(out, "I0 0 N0 DC 5.000000e-01"));
    REQUIRE(contains(out, "I1 0 N1 DC 2.500000e-01"));
    // source_node_ids fallback should not have fired.
    REQUIRE_FALSE(contains(out, "I0 0 N0 DC 1.000000e+00"));
}

TEST_CASE("spice: multiple sources split the total current", "[spice]") {
    IrMesh m;
    m.nodes.push_back({0, 0.0, 0.0, 0, 0, 0});
    m.nodes.push_back({1, 0.001, 0.0, 1, 0, 0});
    m.nodes.push_back({2, 0.002, 0.0, 2, 0, 0});
    m.resistors.push_back({0, 1, 1.0});
    m.resistors.push_back({1, 2, 1.0});
    m.source_node_ids = {0, 1};   // two sources, share total
    m.sink_node_ids = {2};

    SpiceExportConfig cfg;
    cfg.include_position_comments = false;
    cfg.default_total_current = 2.0;   // 2A total, 1A each
    auto out = export_spice(m, cfg);

    REQUIRE(contains(out, "I0 0 N0 DC 1.000000e+00"));
    REQUIRE(contains(out, "I1 0 N1 DC 1.000000e+00"));
}

TEST_CASE("spice: title and position comments configurable", "[spice]") {
    auto m = make_chain_mesh();
    SpiceExportConfig cfg;
    cfg.title = "my_board +3V3 IR drop";
    cfg.include_position_comments = true;
    auto out = export_spice(m, cfg);

    REQUIRE(contains(out, "* my_board +3V3 IR drop"));
    REQUIRE(contains(out, "; ("));   // at least one position comment
}
