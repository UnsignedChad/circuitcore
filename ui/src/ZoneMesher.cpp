#include "circuitcore/ui/ZoneMesher.h"

#include <unordered_map>

#include <mapbox/earcut.hpp>

// Teach earcut how to read x/y from circuitcore::board::Point2.
namespace mapbox {
namespace util {

template <>
struct nth<0, circuitcore::board::Point2> {
    static auto get(const circuitcore::board::Point2& p) { return p.x; }
};
template <>
struct nth<1, circuitcore::board::Point2> {
    static auto get(const circuitcore::board::Point2& p) { return p.y; }
};

}  // namespace util
}  // namespace mapbox

namespace circuitcore::ui {

std::vector<LayerMesh> ZoneMesher::build(const circuitcore::board::Board& board) {
    // Layer ordinal → index into result vector. Lets us append per zone.
    std::unordered_map<int, std::size_t> layer_to_mesh;
    std::vector<LayerMesh> meshes;

    auto mesh_for = [&](int layer) -> LayerMesh& {
        auto it = layer_to_mesh.find(layer);
        if (it == layer_to_mesh.end()) {
            layer_to_mesh[layer] = meshes.size();
            meshes.push_back(LayerMesh{layer, {}, {}});
            return meshes.back();
        }
        return meshes[it->second];
    };

    for (const auto& z : board.zones) {
        const circuitcore::board::Layer* L = board.find_layer(z.layer_ordinal);
        if (!L || !L->is_copper()) continue;

        for (const auto& fp : z.filled) {
            if (fp.outline.size() < 3) continue;  // not a polygon

            // Build earcut input: outline ring + hole rings.
            std::vector<std::vector<circuitcore::board::Point2>> rings;
            rings.reserve(1 + fp.holes.size());
            rings.push_back(fp.outline);
            for (const auto& h : fp.holes) {
                if (h.size() >= 3) rings.push_back(h);
            }

            // earcut returns indices into the flattened ring vertex list
            // (outline first, then each hole), 3 indices per triangle.
            std::vector<std::uint32_t> tri = mapbox::earcut<std::uint32_t>(rings);
            if (tri.empty()) continue;  // degenerate / collinear

            LayerMesh& m = mesh_for(z.layer_ordinal);
            const auto base = static_cast<std::uint32_t>(m.vertex_count());

            // Append vertices in earcut's ring order.
            for (const auto& ring : rings) {
                for (const auto& p : ring) {
                    m.vertices.push_back(static_cast<float>(p.x));
                    m.vertices.push_back(static_cast<float>(p.y));
                }
            }
            // Shift indices to point into the appended block.
            m.indices.reserve(m.indices.size() + tri.size());
            for (auto idx : tri) m.indices.push_back(base + idx);
        }
    }

    return meshes;
}

}  // namespace circuitcore::ui
