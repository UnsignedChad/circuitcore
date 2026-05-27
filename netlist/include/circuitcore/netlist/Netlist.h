// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Schematic-derived netlist. Format-neutral: a NetlistParser takes a
// CAD-specific export (today, KiCad's .net file) and produces this
// shape.
//
// Used to cross-check a Board against the schematic the engineer
// actually drew. Catches the family of bugs where the .kicad_pcb
// drifts from the .kicad_sch (a footprint never placed, a net not
// pushed back to the layout, a deleted decoupling cap that should
// have been routed).

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace circuitcore::netlist {

struct Component {
    std::string ref;        // "C1", "U7"
    std::string value;      // "100nF", "STM32F405RGT6"
    std::string footprint;  // "Capacitor_SMD:C_0402_1005Metric"
};

struct Node {
    std::string component_ref;  // matches Component.ref
    std::string pin;            // "1", "VDD", "+", ...
    std::string pin_function;   // optional; from KiCad pinfunction
    std::string pin_type;       // "input", "output", "passive", "power_in", ...
};

struct Net {
    int code = 0;           // KiCad's monotonic net id
    std::string name;       // "VDD", "/USB_DP", "Net-(C1-Pad1)"
    std::vector<Node> nodes;
};

struct Netlist {
    std::string source_sheet;   // path to the .kicad_sch the export came from
    std::vector<Component> components;
    std::vector<Net> nets;

    const Component* find_component(std::string_view ref) const;
    const Net* find_net(std::string_view name) const;
};

}  // namespace circuitcore::netlist
