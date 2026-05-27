// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// KiCad .net (legacy "E" / "D" format) reader. Pairs with PcbParser:
// PcbParser tells you what's on the board, NetlistParser tells you
// what the schematic said should be on it.
//
// Why parse .net instead of .kicad_sch directly: .kicad_sch records
// wires and labels in geometric form, and computing connectivity
// from that means re-implementing KiCad's connection logic. KiCad
// already does this and exports the result as .net via
// File > Export Netlist. Way simpler to consume the result.
//
// v1 handles flat (single-sheet) netlists. Hierarchical sheets that
// flatten down to a single set of nets at export time work today;
// hierarchical .net with per-sheet sections doesn't.

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "circuitcore/netlist/Netlist.h"

namespace circuitcore::formats::kicad {

struct NetlistParseError {
    std::string message;
    std::size_t line = 0;
    std::size_t col  = 0;
    std::string format() const;
};

class NetlistParser {
public:
    static std::expected<netlist::Netlist, NetlistParseError>
        parse_file(const std::filesystem::path& path);

    static std::expected<netlist::Netlist, NetlistParseError>
        parse_string(std::string_view src);
};

}  // namespace circuitcore::formats::kicad
