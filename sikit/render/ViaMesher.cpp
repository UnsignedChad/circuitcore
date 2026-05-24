#include "render/ViaMesher.h"

#include <algorithm>
#include <unordered_map>

#include "render/CircleHelper.h"

namespace sikit::render {

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

std::vector<LayerMesh> PadMesher::build(const circuitcore::board::Board& board) {
    std::unordered_map<int, std::size_t> idx;
    std::vector<LayerMesh> meshes;

    for (const auto& p : board.pads) {
        for (int ord : p.layer_ordinals) {
            const circuitcore::board::Layer* L = board.find_layer(ord);
            if (!L || !L->is_copper()) continue;
            append_disk(mesh_for(meshes, idx, ord), p.at.x, p.at.y,
                        kDefaultPadRadius);
        }
    }
    return meshes;
}

}  // namespace sikit::render
