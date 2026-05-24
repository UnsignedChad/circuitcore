#include "render/ViaMesher.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "render/CircleHelper.h"

namespace pdnkit::render {

namespace {

// Get-or-create a mesh for `layer` inside `meshes` and return reference.
LayerMesh& mesh_for(std::vector<LayerMesh>& meshes,
                    std::unordered_map<int, std::size_t>& index,
                    int layer) {
    auto it = index.find(layer);
    if (it == index.end()) {
        index[layer] = meshes.size();
        meshes.push_back(LayerMesh{layer, {}, {}});
        return meshes.back();
    }
    return meshes[it->second];
}

}  // namespace

std::vector<LayerMesh> ViaMesher::build(const circuitcore::board::Board& board) {
    std::unordered_map<int, std::size_t> idx;
    std::vector<LayerMesh> meshes;

    for (const auto& v : board.vias) {
        if (v.outer_diameter <= 0.0) continue;
        const int lo = std::min(v.from_layer, v.to_layer);
        const int hi = std::max(v.from_layer, v.to_layer);
        const double r = 0.5 * v.outer_diameter;

        for (const auto& L : board.stackup.layers) {
            if (!L.is_copper()) continue;
            if (L.ordinal < lo || L.ordinal > hi) continue;
            append_disk(mesh_for(meshes, idx, L.ordinal), v.at.x, v.at.y, r);
        }
    }
    return meshes;
}

namespace {

// Append a rotated rectangle (4 verts, 2 tris) centered at (cx, cy) with
// half-width hw, half-height hh, rotation angle (radians).
void append_rect(LayerMesh& mesh, double cx, double cy, double hw, double hh,
                 double angle) {
    if (hw <= 0.0 || hh <= 0.0) return;
    const double cs = std::cos(angle);
    const double sn = std::sin(angle);
    auto rot = [&](double lx, double ly, float& x, float& y) {
        x = static_cast<float>(cx + cs * lx - sn * ly);
        y = static_cast<float>(cy + sn * lx + cs * ly);
    };
    const auto base = static_cast<std::uint32_t>(mesh.vertex_count());
    float x, y;
    rot(-hw, -hh, x, y); mesh.vertices.push_back(x); mesh.vertices.push_back(y);
    rot( hw, -hh, x, y); mesh.vertices.push_back(x); mesh.vertices.push_back(y);
    rot( hw,  hh, x, y); mesh.vertices.push_back(x); mesh.vertices.push_back(y);
    rot(-hw,  hh, x, y); mesh.vertices.push_back(x); mesh.vertices.push_back(y);
    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2,
                                              base + 0, base + 2, base + 3});
}

}  // namespace

std::vector<LayerMesh> PadMesher::build(const circuitcore::board::Board& board) {
    std::unordered_map<int, std::size_t> idx;
    std::vector<LayerMesh> meshes;

    for (const auto& p : board.pads) {
        for (int ord : p.layer_ordinals) {
            const circuitcore::board::Layer* L = board.find_layer(ord);
            if (!L || !L->is_copper()) continue;
            LayerMesh& m = mesh_for(meshes, idx, ord);
            switch (p.shape) {
                case circuitcore::board::PadShape::Circle: {
                    // size.x is the diameter for SMD round pads. Fallback to
                    // kDefaultPadRadius when size is unknown.
                    const double r = (p.size.x > 0.0)
                        ? 0.5 * p.size.x : kDefaultPadRadius;
                    append_disk(m, p.at.x, p.at.y, r);
                    break;
                }
                case circuitcore::board::PadShape::Rect:
                case circuitcore::board::PadShape::RoundRect:
                case circuitcore::board::PadShape::Oval:    // approximated as Rect for v0
                case circuitcore::board::PadShape::Custom: {
                    if (p.size.x > 0.0 && p.size.y > 0.0) {
                        append_rect(m, p.at.x, p.at.y, 0.5 * p.size.x,
                                    0.5 * p.size.y, p.rotation);
                    } else {
                        append_disk(m, p.at.x, p.at.y, kDefaultPadRadius);
                    }
                    break;
                }
            }
        }
    }
    return meshes;
}

}  // namespace pdnkit::render
