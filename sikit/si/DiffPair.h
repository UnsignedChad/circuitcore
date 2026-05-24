// High-speed net + differential pair detection by KiCad naming convention.
//
// KiCad doesn't tag nets as "diff pair" in the .kicad_pcb at the schematic
// level — the convention is to name the two halves with matching base
// names and opposite polarity suffixes (FOO_P/FOO_N, FOO+/FOO-, etc.).
// This module surfaces the candidates so the impedance overlay knows
// which traces to compute Zdiff for.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "circuitcore/board/Board.h"

namespace sikit::highspeed {

struct DiffPair {
    int net_p_id = -1;
    int net_n_id = -1;
    std::string base_name;     // shared stem (e.g. "USB_DP/USB_DM" → "USB_D")
    std::string suffix_style;  // e.g. "_P/_N", "+/-", "DP/DM"
};

// Find all diff-pair candidates by net-name matching. Recognizes:
//     _P/_N      _p/_n      _POS/_NEG    _pos/_neg
//     +/-                   _DP/_DM      DP/DM
std::vector<DiffPair> find_diff_pairs(const circuitcore::board::Board& board);

// Cheap keyword heuristic: returns true if the name looks like a high-speed
// protocol signal (USB, PCIe, DDR, HDMI, MIPI, SerDes, SATA, LVDS, ...).
// Case-insensitive substring match.
bool looks_high_speed(std::string_view net_name);

// Returns net IDs that either appear in a diff pair or pass looks_high_speed().
std::vector<int> find_high_speed_nets(const circuitcore::board::Board& board);

}  // namespace sikit::highspeed
