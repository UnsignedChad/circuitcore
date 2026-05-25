#include "circuitcore/ui/SegmentMesher.h"

#include <algorithm>
#include "circuitcore/ui/CircleHelper.h"
#include <cmath>
#include <unordered_map>

#include "circuitcore/ui/ViaMesher.h"

namespace circuitcore::ui {

std::vector<LayerMesh> SegmentMesher::build(const circuitcore::board::Board& board) {
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

    for (const auto& s : board.segments) {
        const circuitcore::board::Layer* L = board.find_layer(s.layer_ordinal);
        if (!L || !L->is_copper()) continue;
        if (s.width <= 0.0) continue;

        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0) continue;  // zero-length segment

        // Perpendicular unit vector (rotate (dx,dy) by 90° → (-dy, dx)).
        const double nx = -dy / len;
        const double ny =  dx / len;
        const double hw = 0.5 * s.width;

        // Four corners: (a+perp, a-perp, b-perp, b+perp). Counter-clockwise.
        const float ax1 = static_cast<float>(s.start.x + nx * hw);
        const float ay1 = static_cast<float>(s.start.y + ny * hw);
        const float ax2 = static_cast<float>(s.start.x - nx * hw);
        const float ay2 = static_cast<float>(s.start.y - ny * hw);
        const float bx1 = static_cast<float>(s.end.x   - nx * hw);
        const float by1 = static_cast<float>(s.end.y   - ny * hw);
        const float bx2 = static_cast<float>(s.end.x   + nx * hw);
        const float by2 = static_cast<float>(s.end.y   + ny * hw);

        LayerMesh& m = mesh_for(s.layer_ordinal);
        const auto base = static_cast<std::uint32_t>(m.vertex_count());

        m.vertices.insert(m.vertices.end(),
                          {ax1, ay1, ax2, ay2, bx1, by1, bx2, by2});

        // Triangles: (0,1,2) and (0,2,3) relative to base.
        m.indices.insert(m.indices.end(), {base + 0, base + 1, base + 2,
                                           base + 0, base + 2, base + 3});

        // Round caps at each endpoint so adjacent segments join smoothly
        // and standalone segments render as pill shapes (matching how
        // KiCad draws trace ends). The disks overlap the rectangle on
        // the inside; the half that extends past each endpoint is the
        // visible cap.
        append_disk(m, s.start.x, s.start.y, hw, 24);
        append_disk(m, s.end.x,   s.end.y,   hw, 24);
    }

    return meshes;
}

namespace {

// Merge meshes from multiple sources by layer ordinal. Index ranges from
// later meshes get shifted to refer into the consolidated vertex block.
void merge_into(std::vector<LayerMesh>& dst,
                const std::vector<LayerMesh>& src) {
    for (const auto& s : src) {
        // Find matching layer in dst, or append.
        auto it = std::find_if(dst.begin(), dst.end(),
                               [&](const LayerMesh& m) { return m.layer_ordinal == s.layer_ordinal; });
        if (it == dst.end()) {
            dst.push_back(s);
        } else {
            const auto vbase = static_cast<std::uint32_t>(it->vertex_count());
            it->vertices.insert(it->vertices.end(), s.vertices.begin(), s.vertices.end());
            it->indices.reserve(it->indices.size() + s.indices.size());
            for (auto idx : s.indices) it->indices.push_back(vbase + idx);
        }
    }
}

}  // namespace

std::vector<LayerMesh> build_all_meshes(const circuitcore::board::Board& board) {
    std::vector<LayerMesh> result = ZoneMesher::build(board);
    merge_into(result, SegmentMesher::build(board));
    merge_into(result, ViaMesher::build(board));
    merge_into(result, PadMesher::build(board));
    return result;
}

}  // namespace circuitcore::ui
