// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/formats/kicad/NetlistParser.h"

using circuitcore::formats::kicad::NetlistParser;

namespace {

constexpr auto kTinyNet = R"(
(export (version "E")
  (design
    (source "/home/user/proj/board.kicad_sch")
    (date "2024-01-01"))
  (components
    (comp (ref "C1") (value "100nF") (footprint "Capacitor_SMD:C_0402"))
    (comp (ref "U1") (value "STM32F405") (footprint "Package_QFP:LQFP-64_10x10mm_P0.5mm"))
    (comp (ref "R3") (value "10k") (footprint "Resistor_SMD:R_0402")))
  (nets
    (net (code "1") (name "VDD")
      (node (ref "C1") (pin "1") (pinfunction "VDD") (pintype "passive"))
      (node (ref "U1") (pin "5") (pinfunction "VDD") (pintype "power_in")))
    (net (code "2") (name "GND")
      (node (ref "C1") (pin "2") (pintype "passive"))
      (node (ref "U1") (pin "12") (pintype "power_in")))
    (net (code "3") (name "BOOT0")
      (node (ref "R3") (pin "1") (pintype "passive"))
      (node (ref "U1") (pin "60") (pintype "input")))))
)";

}  // namespace

TEST_CASE("netlist: parses components and nets", "[netlist]") {
    auto r = NetlistParser::parse_string(kTinyNet);
    REQUIRE(r.has_value());
    const auto& n = r.value();
    REQUIRE(n.source_sheet == "/home/user/proj/board.kicad_sch");
    REQUIRE(n.components.size() == 3);
    REQUIRE(n.nets.size() == 3);
}

TEST_CASE("netlist: component lookup", "[netlist]") {
    auto n = NetlistParser::parse_string(kTinyNet).value();
    const auto* c1 = n.find_component("C1");
    REQUIRE(c1 != nullptr);
    REQUIRE(c1->value == "100nF");
    REQUIRE(c1->footprint == "Capacitor_SMD:C_0402");
    REQUIRE(n.find_component("nonexistent") == nullptr);
}

TEST_CASE("netlist: net lookup carries node list", "[netlist]") {
    auto n = NetlistParser::parse_string(kTinyNet).value();
    const auto* vdd = n.find_net("VDD");
    REQUIRE(vdd != nullptr);
    REQUIRE(vdd->code == 1);
    REQUIRE(vdd->nodes.size() == 2);
    REQUIRE(vdd->nodes[0].component_ref == "C1");
    REQUIRE(vdd->nodes[0].pin == "1");
    REQUIRE(vdd->nodes[0].pin_function == "VDD");
    REQUIRE(vdd->nodes[0].pin_type == "passive");
    REQUIRE(vdd->nodes[1].component_ref == "U1");
}

TEST_CASE("netlist: rejects non-export root", "[netlist]") {
    auto r = NetlistParser::parse_string("(kicad_pcb)");
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("netlist: handles missing optional sections", "[netlist]") {
    // Empty design + components + nets sections.
    auto r = NetlistParser::parse_string(
        "(export (version \"E\") (design) (components) (nets))");
    REQUIRE(r.has_value());
    REQUIRE(r.value().components.empty());
    REQUIRE(r.value().nets.empty());
}
