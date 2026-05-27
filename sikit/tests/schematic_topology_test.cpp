// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/formats/kicad/NetlistParser.h"
#include "si/SchematicTopology.h"

using namespace sikit::si;
using circuitcore::formats::kicad::NetlistParser;

namespace {

// Minimal hand-rolled .net string covering the cases the topology
// classifier has to get right: clean driver/receiver, multi-receiver
// (DDR bus stub), no-driver pathology, contention (two drivers),
// passive series element, and a power rail (which should be filtered
// out by derive_all_topologies).
constexpr auto kNet = R"(
(export (version "E")
  (design (source "demo.kicad_sch"))
  (components
    (comp (ref "U1") (value "MCU"))
    (comp (ref "U2") (value "PHY"))
    (comp (ref "U3") (value "DRAM"))
    (comp (ref "U4") (value "DRAM"))
    (comp (ref "R1") (value "33"))
    (comp (ref "C1") (value "100nF")))
  (nets
    (net (code "1") (name "VDD")
      (node (ref "C1") (pin "1") (pintype "passive"))
      (node (ref "U1") (pin "1") (pintype "power_in")))
    (net (code "2") (name "TX_LANE")
      (node (ref "U1") (pin "10") (pinfunction "TX") (pintype "output"))
      (node (ref "R1") (pin "1") (pintype "passive")))
    (net (code "3") (name "TX_LANE_TERM")
      (node (ref "R1") (pin "2") (pintype "passive"))
      (node (ref "U2") (pin "5") (pinfunction "RX") (pintype "input")))
    (net (code "4") (name "DQ0")
      (node (ref "U1") (pin "20") (pintype "bidirectional"))
      (node (ref "U3") (pin "1") (pintype "bidirectional"))
      (node (ref "U4") (pin "1") (pintype "bidirectional")))
    (net (code "5") (name "NO_DRIVER")
      (node (ref "U2") (pin "6") (pintype "input"))
      (node (ref "U3") (pin "2") (pintype "input")))
    (net (code "6") (name "CONTENTION")
      (node (ref "U1") (pin "30") (pintype "output"))
      (node (ref "U2") (pin "7") (pintype "output")))
    (net (code "7") (name "RAW")
      (node (ref "U1") (pin "40"))
      (node (ref "U2") (pin "8")))))
)";

}  // namespace

TEST_CASE("schematic-topology: power-net heuristic catches obvious rails",
          "[schematic-topology]") {
    REQUIRE(looks_like_power_net("VDD"));
    REQUIRE(looks_like_power_net("VCC3V3"));
    REQUIRE(looks_like_power_net("GND"));
    REQUIRE(looks_like_power_net("AGND"));
    REQUIRE(looks_like_power_net("/VBUS"));
    REQUIRE(looks_like_power_net("+3V3"));
    REQUIRE(looks_like_power_net("-12V"));
    REQUIRE_FALSE(looks_like_power_net("USB_DP"));
    REQUIRE_FALSE(looks_like_power_net("DQ0"));
}

TEST_CASE("schematic-topology: driver and receiver classification",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto t = derive_topology(nl, "TX_LANE");
    REQUIRE(t.net_code == 2);
    REQUIRE(t.endpoints.size() == 2);
    REQUIRE(t.drivers().size() == 1);
    REQUIRE(t.drivers().front()->component_ref == "U1");
    REQUIRE(t.passives().size() == 1);
    REQUIRE(t.passives().front()->component_ref == "R1");
    REQUIRE_FALSE(t.has_driver_problem());
}

TEST_CASE("schematic-topology: bidirectional pins count as drivers",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto t = derive_topology(nl, "DQ0");
    REQUIRE(t.drivers().size() == 3);
    REQUIRE(t.receivers().empty());
    // A 3-driver DQ line is "problematic" by the strict heuristic --
    // multi-drop busses naturally trip it, and that's fine.
    REQUIRE(t.has_driver_problem());
}

TEST_CASE("schematic-topology: zero-driver net is flagged",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto t = derive_topology(nl, "NO_DRIVER");
    REQUIRE(t.drivers().empty());
    REQUIRE(t.receivers().size() == 2);
    REQUIRE(t.has_driver_problem());
}

TEST_CASE("schematic-topology: contention (two drivers) is flagged",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto t = derive_topology(nl, "CONTENTION");
    REQUIRE(t.drivers().size() == 2);
    REQUIRE(t.has_driver_problem());
}

TEST_CASE("schematic-topology: missing pintype lands in Unspecified",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto t = derive_topology(nl, "RAW");
    for (const auto& ep : t.endpoints) {
        REQUIRE(ep.role == TopologyRole::Unspecified);
    }
    REQUIRE(t.has_driver_problem());
}

TEST_CASE("schematic-topology: unknown net returns empty",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto t = derive_topology(nl, "DOES_NOT_EXIST");
    REQUIRE(t.endpoints.empty());
    REQUIRE(t.net_name == "DOES_NOT_EXIST");
    REQUIRE(t.net_code == 0);
}

TEST_CASE("schematic-topology: bulk derivation skips power rails",
          "[schematic-topology]") {
    auto nl = NetlistParser::parse_string(kNet).value();
    auto ts = derive_all_topologies(nl);
    // Six non-power nets in the fixture (VDD is filtered).
    REQUIRE(ts.size() == 6);
    for (const auto& t : ts) {
        REQUIRE(t.net_name != "VDD");
    }
}

TEST_CASE("schematic-topology: role_name covers every enum",
          "[schematic-topology]") {
    REQUIRE(std::string(role_name(TopologyRole::Driver))      == "driver");
    REQUIRE(std::string(role_name(TopologyRole::Receiver))    == "receiver");
    REQUIRE(std::string(role_name(TopologyRole::Passive))     == "passive");
    REQUIRE(std::string(role_name(TopologyRole::Power))       == "power");
    REQUIRE(std::string(role_name(TopologyRole::NoConnect))   == "nc");
    REQUIRE(std::string(role_name(TopologyRole::Unspecified)) == "unspecified");
}
