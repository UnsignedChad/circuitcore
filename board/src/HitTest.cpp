// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/board/HitTest.h"

#include <cmath>
#include <limits>

namespace circuitcore::board::hittest {

const char* name(Hit::Kind k) noexcept {
    switch (k) {
        case Hit::Kind::Zone:    return "zone";
        case Hit::Kind::Segment: return "segment";
        case Hit::Kind::Via:     return "via";
        case Hit::Kind::Pad:     return "pad";
        case Hit::Kind::None:    break;
    }
    return "";
}

namespace {

double dist_squared(circuitcore::board::Point2 a, circuitcore::board::Point2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Shortest distance from point p to line segment ab.
double dist_to_segment(circuitcore::board::Point2 p, circuitcore::board::Point2 a, circuitcore::board::Point2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) return std::sqrt(dist_squared(p, a));
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const circuitcore::board::Point2 proj{a.x + t * dx, a.y + t * dy};
    return std::sqrt(dist_squared(p, proj));
}

// Standard ray-casting point-in-polygon. Treats holes as additive: a point
// is inside the polygon iff it is in the outline AND not in any hole.
bool point_in_ring(const std::vector<circuitcore::board::Point2>& ring, circuitcore::board::Point2 p) {
    bool inside = false;
    const std::size_t n = ring.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& pi = ring[i];
        const auto& pj = ring[j];
        if (((pi.y > p.y) != (pj.y > p.y)) &&
            (p.x < (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_in_polygon(const circuitcore::board::Polygon& poly, circuitcore::board::Point2 p) {
    if (!point_in_ring(poly.outline, p)) return false;
    for (const auto& h : poly.holes) {
        if (point_in_ring(h, p)) return false;
    }
    return true;
}

}  // namespace

Hit at_point(const circuitcore::board::Board& board, circuitcore::board::Point2 world,
             double pick_radius) {
    // Default pad radius matches PadMesher's visual size.
    constexpr double kVisualPadRadius = 0.50e-3;

    // 1. Pads (highest priority — small, on top).
    for (std::size_t i = 0; i < board.pads.size(); ++i) {
        const auto& p = board.pads[i];
        const int layer = p.layer_ordinals.empty() ? 0 : p.layer_ordinals.front();
        bool hit = false;
        if (p.shape == circuitcore::board::PadShape::Circle || p.size.x <= 0.0 || p.size.y <= 0.0) {
            const double r = (p.size.x > 0.0) ? 0.5 * p.size.x : kVisualPadRadius;
            const double tol = r + pick_radius;
            if (dist_squared(world, p.at) <= tol * tol) hit = true;
        } else {
            // Transform world into pad-local axes (undo translation + rotation),
            // then check against the axis-aligned half-extents + pick_radius.
            const double dx = world.x - p.at.x;
            const double dy = world.y - p.at.y;
            const double cs = std::cos(-p.rotation);
            const double sn = std::sin(-p.rotation);
            const double lx = cs * dx - sn * dy;
            const double ly = sn * dx + cs * dy;
            const double hw = 0.5 * p.size.x + pick_radius;
            const double hh = 0.5 * p.size.y + pick_radius;
            if (std::abs(lx) <= hw && std::abs(ly) <= hh) hit = true;
        }
        if (hit) return {Hit::Kind::Pad, p.net_id, layer, static_cast<int>(i)};
    }

    // 2. Vias.
    for (std::size_t i = 0; i < board.vias.size(); ++i) {
        const auto& v = board.vias[i];
        const double r = 0.5 * v.outer_diameter + pick_radius;
        if (dist_squared(world, v.at) <= r * r) {
            return {Hit::Kind::Via, v.net_id, v.from_layer,
                    static_cast<int>(i)};
        }
    }

    // 3. Segments.
    for (std::size_t i = 0; i < board.segments.size(); ++i) {
        const auto& seg = board.segments[i];
        const double tol = 0.5 * seg.width + pick_radius;
        if (dist_to_segment(world, seg.start, seg.end) <= tol) {
            return {Hit::Kind::Segment, seg.net_id, seg.layer_ordinal,
                    static_cast<int>(i)};
        }
    }

    // 4. Zones (largest, lowest priority).
    for (std::size_t i = 0; i < board.zones.size(); ++i) {
        const auto& z = board.zones[i];
        for (const auto& fp : z.filled) {
            if (point_in_polygon(fp, world)) {
                return {Hit::Kind::Zone, z.net_id, z.layer_ordinal,
                        static_cast<int>(i)};
            }
        }
    }

    return {};
}

}  // namespace circuitcore::board::hittest
