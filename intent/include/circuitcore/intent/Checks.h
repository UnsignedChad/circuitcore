// Cross-check a Board against the netlist the schematic exported.
//
// The drift this catches: footprint placed in schematic but never
// dropped on the board, net routed in schematic but not pushed to
// layout, decoupling cap two inches from the pin it's supposed to
// be decoupling. Standard layout-vs-schematic verification that
// every commercial CAD tool ships but KiCad's stock workflow
// leaves to the eyeball.
//
// pdnkit and sikit both call this before running expensive
// analyses -- garbage in, garbage out is much cheaper to fix when
// the tool says "your schematic and PCB disagree" than when the
// numbers come out wrong an hour later.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "circuitcore/netlist/Netlist.h"

namespace circuitcore::intent {

struct Violation {
    enum class Kind {
        MissingFootprint,       // netlist has a component, board has no pads with that ref
        UnroutedNet,            // netlist has a net, board has no segments / pads on it
        DecouplingCapTooFar,    // decoupling cap sits further than the budget from its IC pin
    };
    Kind kind;
    std::string subject;        // component ref or net name
    std::string detail;         // human-readable explanation including measured values
};

struct CheckOptions {
    // Decoupling cap is "too far" when its center sits beyond this radius
    // from any pad of the IC it decouples. 2 mm matches the rule-of-thumb
    // for typical SoC packages; tighten for high-current rails.
    double decoupling_max_distance_m = 2.0e-3;

    bool check_missing_footprint = true;
    bool check_unrouted_net = true;
    bool check_decoupling_proximity = true;

    // Power-net detection. A "power net" is one whose name contains any
    // of these substrings (case-insensitive). Used by the decoupling
    // check to find the rail a cap belongs to. Defaults cover the
    // common cases; project-specific rails get added per-call.
    std::vector<std::string> power_net_keywords = {
        "VCC", "VDD", "VBAT", "VBUS", "VSYS", "3V3", "5V", "1V8", "1V2",
    };
};

std::vector<Violation> check_design_intent(
    const netlist::Netlist& netlist,
    const board::Board& board,
    const CheckOptions& opts = {});

const char* kind_name(Violation::Kind k);

}  // namespace circuitcore::intent
