// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// KiCad .kicad_pcb parser.
//
// Walks an S-expression tree produced by circuitcore::sexpr::parse() and produces
// a typed circuitcore::board::Board. All coordinates are converted from KiCad
// millimeters to SI meters at parse time.
//
// v0 scope: layers, nets, segments, vias, zones (outline + filled),
//           footprint pads (position + net only).
// Skipped:  silkscreen, text, fab-layers, 3D models, schematic refs.
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
