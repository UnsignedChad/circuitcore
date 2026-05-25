// Shared per-layer color palette. Returns RGBA in [0,1] so both the Qt
// (UI) side and the GL (shader) side can consume it without converting
// through Qt-specific types in circuitcore_ui.

#pragma once

#include <array>

namespace circuitcore::ui {

// RGBA in [0,1]. Front copper (0) is warm red; back copper (31) is cool
// blue; inner copper cycles through a small palette by ordinal.
inline std::array<float, 4> layer_color(int ordinal) {
    switch (ordinal) {
        case 0:  return {0.82f, 0.20f, 0.20f, 0.80f};  // F.Cu
        case 31: return {0.20f, 0.55f, 0.85f, 0.80f};  // B.Cu
        default: break;
    }
    static constexpr std::array<std::array<float, 4>, 5> palette{{
        {0.30f, 0.75f, 0.30f, 0.70f},  // green
        {0.78f, 0.55f, 0.20f, 0.70f},  // orange
        {0.65f, 0.30f, 0.78f, 0.70f},  // purple
        {0.30f, 0.72f, 0.78f, 0.70f},  // cyan
        {0.78f, 0.78f, 0.30f, 0.70f},  // yellow
    }};
    const int n = static_cast<int>(palette.size());
    return palette[((ordinal - 1) % n + n) % n];
}

}  // namespace circuitcore::ui
