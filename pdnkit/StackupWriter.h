// Write back a .kicad_pcb with the user's in-memory stackup edits applied.
//
// Re-parses the source file, walks for (setup (stackup ...)), overwrites
// the (thickness ...) inside each (layer ...) entry whose name matches an
// in-memory layer, then emits the modified tree to the destination file
// via circuitcore::sexpr::emit.
//
// KiCad's stackup block stores thickness in millimeters. Board::Layer
// stores it in meters. Conversion happens here.
//
// Canonical formatting (per circuitcore::sexpr::emit) -- whitespace and
// comments from the original source are not preserved.

#pragma once

#include <filesystem>
#include <string>

#include "circuitcore/board/Board.h"

namespace pdnkit {

struct StackupSaveResult {
    bool ok = false;
    std::string error;
    int layers_updated = 0;
};

// Re-parse src, splice in board's stackup layer thicknesses, emit to dst.
// Never overwrites src directly -- caller passes the destination path.
StackupSaveResult save_modified_stackup(
    const std::filesystem::path& src_path,
    const std::filesystem::path& dst_path,
    const circuitcore::board::Board& board);

}  // namespace pdnkit
