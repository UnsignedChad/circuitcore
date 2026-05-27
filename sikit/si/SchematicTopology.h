// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Schematic-derived topology classifier.
//
// Reads a parsed circuitcore::netlist::Netlist (which the shared
// circuitcore::kicad::NetlistParser produces from a .net export) and,
// for each net of interest, classifies its Nodes into drivers,
// receivers, passives, and "other". The classification uses the
// schematic's pintype field -- "output" -> driver, "input" -> receiver,
// "passive" -> series element, "bidirectional"/"tri_state" -> driver
// (because they CAN drive the line and need a buffer model), and
// anything else lands in `others`.
//
// Why this is useful
//
//   The topology editor (sikit/si/Topology.h) takes a manually
//   constructed list of blocks. In practice the engineer already drew
//   the channel in the schematic: the IC pin labeled "TX" drives, the
//   one labeled "RX" receives, the resistor in between is a series
//   termination. This module reads that intent off the schematic so
//   the topology editor can pre-fill driver/receiver assignments
//   instead of asking the user.
//
//   Also doubles as a sanity check: if a high-speed net has zero
//   drivers (every node is "input") OR two drivers (genuine
//   contention), we want to surface it -- the topology editor can
//   flag it before the user runs eye analysis on nonsense.
//
// What this is NOT
//
//   Not a .kicad_sch parser. KiCad's .kicad_sch stores wires and
//   labels geometrically; recovering the netlist from those means
//   re-implementing KiCad's connection logic, which KiCad already
//   does and exports as .net. The shared circuitcore::netlist module
//   has a parser for that file; this module sits on top of it.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/netlist/Netlist.h"

namespace sikit::si {

enum class TopologyRole {
    Driver,         // output / tri_state / open_collector / bidirectional
    Receiver,       // input
    Passive,        // passive (R, L, C, ferrite bead, ...)
    Power,          // power_in / power_out (irrelevant for SI topology)
    NoConnect,      // pintype = "nc" / "no_connect"
    Unspecified,    // pintype missing or unrecognised
};

const char* role_name(TopologyRole r);

struct TopologyEndpoint {
    std::string component_ref;   // "U7"
    std::string pin;              // "B12"
    std::string pin_function;     // optional, from schematic
    std::string pin_type;         // raw pintype string from .net
    TopologyRole role = TopologyRole::Unspecified;
};

struct DerivedTopology {
    std::string net_name;
    int net_code = 0;
    std::vector<TopologyEndpoint> endpoints;

    // Convenience views: subsets of `endpoints` filtered by role.
    std::vector<const TopologyEndpoint*> drivers()    const;
    std::vector<const TopologyEndpoint*> receivers()  const;
    std::vector<const TopologyEndpoint*> passives()   const;

    // True iff this net looks pathological for a point-to-point SI
    // analysis: zero drivers or more than one driver (driver contention).
    // Multi-drop buses naturally trip the "more than one driver" rule
    // too, which is fine -- the caller decides whether that's a bug or
    // expected.
    bool has_driver_problem() const;
};

// Classify a single net. Net is looked up by name; returns empty
// optional if no such net exists.
//
// header note: the .net format does not always set pintype. KiCad
// only emits one when the schematic symbol declares it (newer
// libraries do, ancient hand-drawn ones don't). Endpoints with no
// pintype land in `Unspecified`; the caller can decide whether to
// treat them as drivers, receivers, or skip the net.
DerivedTopology derive_topology(const circuitcore::netlist::Netlist& nl,
                                  const std::string& net_name);

// Classify every net in the netlist. Ignores power nets ("VDD", "GND",
// any name starting with "VCC") because they aren't SI channels and
// would otherwise dominate the report.
std::vector<DerivedTopology> derive_all_topologies(
    const circuitcore::netlist::Netlist& nl);

// True if `net_name` looks like a power/ground rail and should be
// excluded from SI topology analysis. Exposed so the CLI can filter
// the same way the bulk derivation does.
bool looks_like_power_net(const std::string& net_name);

}  // namespace sikit::si
