// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Physical-cross-section ("items[]") stackup model used by sikit's SI math.
//
// The canonical circuitcore::board::Stackup only captures copper layers plus
// per-copper-layer thickness/eps_r/loss_tangent fields. Cross-section
// solvers (stripline RLGC, asymmetric stackup microstrip, plane-pair
// dielectric for diff pairs) need the dielectric items between copper
// layers as first-class records. This sikit-side parser pulls those out of
// the .kicad_pcb (setup (stackup ...)) section directly using
// circuitcore::sexpr, sitting alongside the canonical Board.
//
// pdnkit doesn't use this -- its cavity solver only needs the bulk plane-
// pair epsilon_r per layer, which the canonical Layer table covers.

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sikit::si {

struct SiStackupItem {
    enum class Kind { Copper, Dielectric, SolderMask, Silkscreen, Paste, Other };

    Kind kind = Kind::Other;
    std::string name;          // copper: matches Layer.name; dielectric: e.g. "dielectric 1"
    double thickness = 0.0;    // meters

    // Dielectric-only fields. Default 0 -> "not specified" (caller uses
    // an FR-4 fallback). Copper items leave these zero.
    double epsilon_r = 0.0;
    double loss_tangent = 0.0;
    std::string material;
};

struct SiStackup {
    // Physical cross-section items in top-to-bottom order.
    std::vector<SiStackupItem> items;

    // Find the first dielectric immediately adjacent to a copper-layer
    // item with the given name. side = -1 -> look above (earlier in
    // items[]); side = +1 -> look below (later). Returns nullptr if none.
    const SiStackupItem* adjacent_dielectric(std::string_view copper_name,
                                              int side) const noexcept;

    // Convenience: any dielectric anywhere in the stack (first one found).
    // Useful as a fallback when adjacent_dielectric returns nothing.
    const SiStackupItem* any_dielectric() const noexcept;
};

struct SiStackupError {
    std::string message;
};

// Parse the (setup (stackup ...)) block out of a .kicad_pcb file and
// build an SiStackup. Returns empty stackup (no error) if the file has
// no stackup block.
std::expected<SiStackup, SiStackupError> load_si_stackup(
    const std::filesystem::path& kicad_pcb_path);

// Same but from an in-memory document.
std::expected<SiStackup, SiStackupError> parse_si_stackup(std::string_view src);

}  // namespace sikit::si
