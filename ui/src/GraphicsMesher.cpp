// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/ui/GraphicsMesher.h"

#include <cmath>
#include <cstdint>

#include <mapbox/earcut.hpp>
#include <utility>

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

#include "circuitcore/ui/CircleHelper.h"

namespace circuitcore::ui {

namespace {

enum class Category { Silk, Mask, Courtyard, Skip };

Category classify(const board::Layer& L) {
    const auto& n = L.name;
    auto ends = [&](std::string_view suffix) {
        return n.size() >= suffix.size() &&
                std::equal(suffix.rbegin(), suffix.rend(), n.rbegin());
    };
    if (ends("SilkS")) return Category::Silk;
    if (ends("Mask"))  return Category::Mask;
    if (ends("CrtYd")) return Category::Courtyard;
    // F.Fab / B.Fab carry the detailed component-body outlines that
    // KiCad uses for the printed-on-board "Fab" view. Many connector
    // footprints (DB9 / VGA / D-Sub family) put most of their body
    // geometry there instead of on SilkS, so without this they read as
    // bare pads with no shape around them. Route them through the silk
    // pipeline so they render in the same opaque colour as silkscreen
    // and respect the same silk-visible toggle.
    if (ends(".Fab"))  return Category::Silk;
    return Category::Skip;
}

// Stroke a single segment (p0->p1) into a rect + two round caps.
// Identical shape SegmentMesher uses for trace segments.
void stroke_segment(LayerMesh& m, double x0, double y0,
                     double x1, double y1, double width) {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0 || width <= 0.0) return;
    const double nx = -dy / len;
    const double ny =  dx / len;
    const double hw = 0.5 * width;
    const float ax1 = static_cast<float>(x0 + nx * hw);
    const float ay1 = static_cast<float>(y0 + ny * hw);
    const float ax2 = static_cast<float>(x0 - nx * hw);
    const float ay2 = static_cast<float>(y0 - ny * hw);
    const float bx1 = static_cast<float>(x1 - nx * hw);
    const float by1 = static_cast<float>(y1 - ny * hw);
    const float bx2 = static_cast<float>(x1 + nx * hw);
    const float by2 = static_cast<float>(y1 + ny * hw);
    const auto base = static_cast<std::uint32_t>(m.vertex_count());
    m.vertices.insert(m.vertices.end(),
                       {ax1, ay1, ax2, ay2, bx1, by1, bx2, by2});
    m.indices.insert(m.indices.end(), {base + 0, base + 1, base + 2,
                                        base + 0, base + 2, base + 3});
    append_disk(m, x0, y0, hw, 16);
    append_disk(m, x1, y1, hw, 16);
}

void stroke_polyline(LayerMesh& m,
                      const std::vector<board::Point2>& pts,
                      double width, bool closed) {
    for (std::size_t i = 1; i < pts.size(); ++i) {
        stroke_segment(m, pts[i - 1].x, pts[i - 1].y,
                          pts[i].x,     pts[i].y, width);
    }
    if (closed && pts.size() >= 2) {
        stroke_segment(m, pts.back().x, pts.back().y,
                          pts.front().x, pts.front().y, width);
    }
}

void fill_polygon(LayerMesh& m,
                   const std::vector<board::Point2>& pts) {
    if (pts.size() < 3) return;
    using Polygon = std::vector<std::vector<board::Point2>>;
    Polygon poly = { pts };
    auto idx = mapbox::earcut<std::uint32_t>(poly);
    if (idx.empty()) return;
    const auto base = static_cast<std::uint32_t>(m.vertex_count());
    for (const auto& p : pts) {
        m.vertices.push_back(static_cast<float>(p.x));
        m.vertices.push_back(static_cast<float>(p.y));
    }
    m.indices.reserve(m.indices.size() + idx.size());
    for (auto i : idx) m.indices.push_back(base + i);
}

}  // namespace

GraphicsBundle GraphicsMesher::build(const board::Board& board) {
    GraphicsBundle out;
    for (const auto& g : board.graphics) {
        const auto* L = board.find_layer(g.layer_ordinal);
        if (!L) continue;
        const auto cat = classify(*L);
        if (cat == Category::Skip) continue;

        const double w = (g.stroke_width > 0.0) ? g.stroke_width
                                                 : kDefaultStrokeWidth;
        LayerMesh* target = nullptr;
        bool fill_only = false;
        switch (cat) {
            case Category::Silk:      target = &out.silk;                 break;
            case Category::Mask:      target = &out.mask; fill_only = true; break;
            case Category::Courtyard: target = &out.courtyard;            break;
            case Category::Skip:      continue;
        }

        using K = board::GraphicItem::Kind;
        switch (g.kind) {
            case K::Line:
                if (g.points.size() == 2 && !fill_only)
                    stroke_segment(*target,
                                    g.points[0].x, g.points[0].y,
                                    g.points[1].x, g.points[1].y, w);
                break;
            case K::Arc:
                if (g.points.size() >= 2 && !fill_only)
                    stroke_polyline(*target, g.points, w, /*closed=*/false);
                break;
            case K::Circle:
                // Tessellated as a closed 48-gon by the parser. Stroke
                // for silk/courtyard, fill for mask.
                if (fill_only) fill_polygon(*target, g.points);
                else           stroke_polyline(*target, g.points, w, /*closed=*/false);
                break;
            case K::Polygon:
                // Filled. Stroke too for silk (KiCad outlines polygons
                // with the same width even when filled), but only fill
                // for mask.
                fill_polygon(*target, g.points);
                if (!fill_only)
                    stroke_polyline(*target, g.points, w, /*closed=*/true);
                break;
            case K::Text:
                if (cat == Category::Silk && !g.text.empty() && !g.points.empty()) {
                    GraphicsBundle::Text t;
                    t.x = g.points.front().x;
                    t.y = g.points.front().y;
                    t.size  = (g.text_size > 0) ? g.text_size : 1.0e-3;
                    t.angle = g.text_angle;
                    t.layer_ordinal = g.layer_ordinal;
                    t.text = g.text;
                    out.silk_text.push_back(std::move(t));
                }
                break;
        }
    }
    return out;
}

}  // namespace circuitcore::ui
