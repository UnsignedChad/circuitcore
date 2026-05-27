// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// KiCad .kicad_pcb parser.
//
// Walks an S-expression tree produced by circuitcore::sexpr::parse() and produces
// a typed circuitcore::board::Board. All coordinates are converted from KiCad
// millimeters to SI meters at parse time.
//
// Scope: layers, nets, stackup, segments, vias, zones (outline +
// filled), footprint pads (position + size + shape + net), graphics
// (lines / arcs / circles / polygons / text on non-copper layers),
// components (per-footprint identifier + courtyard bbox).
// Skipped:  fab-layers, 3D models, schematic refs.
//
// Error handling: the parser is exception-free at the API boundary.
// All failures come back as std::expected<Board, ParseError>; any
// std::exception that escapes a helper is caught and wrapped. The
// internal Walker uses throw for compact control flow but those throws
// never escape parse_string / parse_file.
//
// Coordinate system note: KiCad's PCB Y-axis grows downward (screen
// convention). This parser preserves the raw mm-to-m conversion; downstream
// code (renderer/mesher) is responsible for any axis flip it needs.

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "circuitcore/board/Board.h"

namespace circuitcore::formats::kicad {

// Detail about why a parse failed. Not an exception type -- the parser
// returns this in a std::expected. Use .format() for a human-readable
// "kicad_pcb parse error at line N, col M: message".
struct ParseError {
    std::string message;
    std::size_t line = 0;
    std::size_t col  = 0;

    // "kicad_pcb parse error at line N, col M: <message>"
    std::string format() const;
};

class PcbParser {
public:
    // Parse a .kicad_pcb file from disk. Returns the Board on success, or
    // a ParseError describing where parsing failed. File-I/O failures
    // (path missing, permission denied) surface as a ParseError with
    // line/col == 0.
    static std::expected<circuitcore::board::Board, ParseError> parse_file(
        const std::filesystem::path& path);

    // Parse a .kicad_pcb document held in memory.
    static std::expected<circuitcore::board::Board, ParseError> parse_string(
        std::string_view src);
};

}  // namespace circuitcore::formats::kicad
