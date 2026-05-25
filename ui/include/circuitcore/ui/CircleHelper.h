// Shared geometry helper: append a triangle-fan disk to a LayerMesh.
// Used by ViaMesher and PadMesher (vias and round pads both render as disks).

#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

#include "circuitcore/ui/ZoneMesher.h"  // for LayerMesh

namespace circuitcore::ui {

// Append `sides` triangles forming a filled disk centered at (cx, cy) with
// radius `r` into the given mesh. Default 24 sides → visually smooth at all
// realistic via sizes without bloating vertex counts.
inline void append_disk(LayerMesh& mesh, double cx, double cy, double r,
                        int sides = 32) {
    if (r <= 0.0 || sides < 3) return;
    const auto base = static_cast<std::uint32_t>(mesh.vertex_count());

    mesh.vertices.push_back(static_cast<float>(cx));
    mesh.vertices.push_back(static_cast<float>(cy));

    const double two_pi = 2.0 * std::numbers::pi;
    for (int i = 0; i < sides; ++i) {
        const double a = two_pi * i / sides;
        mesh.vertices.push_back(static_cast<float>(cx + r * std::cos(a)));
        mesh.vertices.push_back(static_cast<float>(cy + r * std::sin(a)));
    }

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1 + static_cast<std::uint32_t>(i));
        mesh.indices.push_back(base + 1 + static_cast<std::uint32_t>(next));
    }
}

}  // namespace circuitcore::ui
