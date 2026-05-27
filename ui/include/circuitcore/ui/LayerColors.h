// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Shared per-layer color palette. Returns RGBA in [0,1] so both the Qt
// (UI) side and the GL (shader) side can consume it without converting
// through Qt-specific types in circuitcore_ui.
//
// Palette is loosely calibrated to KiCad's PCB Editor default theme so
// the studio canvas reads as a real board at a glance: warm red on F.Cu,
// olive / amber inner layers, cool blue on B.Cu. Plus a sentinel ordinal
// (kDrillOrdinal = -1) for drill-hole punches that render in the canvas
// background color so vias look drilled-through.

#pragma once

#include <array>

namespace circuitcore::ui {

// Reserved ordinal used by ViaMesher (and future PadMesher work) for
// drill-hole geometry. Picked outside KiCad's 0..63 layer ordinal range
// so it never collides with a real copper layer.
inline constexpr int kDrillOrdinal = -1;

inline std::array<float, 4> layer_color(int ordinal) {
    // Drill holes: punch to background. Studio + sikit + pdnkit canvases
    // all clear to roughly (0.10, 0.10, 0.12) -- match it so the hole
    // reads as "drilled through".
    if (ordinal == kDrillOrdinal) return {0.10f, 0.10f, 0.12f, 1.00f};

    switch (ordinal) {
        case 0:  return {0.78f, 0.20f, 0.20f, 0.85f};  // F.Cu  warm copper red
        case 31: return {0.25f, 0.50f, 0.78f, 0.85f};  // B.Cu  cool blue-grey
        default: break;
    }
    // Inner layers cycle through KiCad's default inner palette: olive,
    // amber, mustard, jade, slate -- all warm-to-cool tones that read as
    // copper-ish without colliding with F.Cu / B.Cu.
    static constexpr std::array<std::array<float, 4>, 5> palette{{
        {0.62f, 0.62f, 0.20f, 0.80f},  // In1.Cu  olive
        {0.78f, 0.55f, 0.20f, 0.80f},  // In2.Cu  amber
        {0.80f, 0.72f, 0.25f, 0.80f},  // In3.Cu  mustard
        {0.32f, 0.65f, 0.55f, 0.80f},  // In4.Cu  jade
        {0.55f, 0.55f, 0.65f, 0.80f},  // In5.Cu  slate
    }};
    const int n = static_cast<int>(palette.size());
    return palette[((ordinal - 1) % n + n) % n];
}

}  // namespace circuitcore::ui
