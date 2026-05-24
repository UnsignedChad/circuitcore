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

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "circuitcore/board/Board.h"

namespace circuitcore::formats::kicad {

struct ParseError : std::runtime_error {
    ParseError(const std::string& msg, std::size_t line, std::size_t col);
    std::size_t line;
    std::size_t col;
};

class PcbParser {
public:
    // Parse a .kicad_pcb file from disk. Throws ParseError on failure
    // (file I/O failure surfaces as a runtime_error with errno-style detail).
    static circuitcore::board::Board parse_file(const std::filesystem::path& path);

    // Parse a .kicad_pcb document held in memory.
    static circuitcore::board::Board parse_string(std::string_view src);
};

}  // namespace circuitcore::formats::kicad
