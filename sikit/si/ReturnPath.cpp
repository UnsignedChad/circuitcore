// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/ReturnPath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace sikit::si {

namespace {

// Standard ray-casting point-in-polygon. Same algorithm specs::EyeMask
// uses internally; we duplicate it here to keep the dependency surface
// small (return-path detection doesn't otherwise touch the eye-mask
// module).
bool point_in_polygon(double x, double y,
                       const std::vector<circuitcore::board::Point2>& poly) {
    if (poly.size() < 3) return false;
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].x, yi = poly[i].y;
        const double xj = poly[j].x, yj = poly[j].y;
        const bool cross = ((yi > y) != (yj > y)) &&
                            (x < (xj - xi) * (y - yi) / (yj - yi + 1e-30) + xi);
        if (cross) inside = !inside;
    }
    return inside;
}

// Is (x, y) inside any filled polygon of any zone on this layer?
// We check `filled` (post-thermal-relief processed copper) when present,
// otherwise the user-drawn `outline`.
bool any_zone_covers(const circuitcore::board::Board& board,
                      int layer_ordinal, double x, double y) {
    for (const auto& z : board.zones) {
        if (z.layer_ordinal != layer_ordinal) continue;
        if (!z.filled.empty()) {
            for (const auto& poly : z.filled) {
                if (point_in_polygon(x, y, poly.outline)) return true;
            }
        } else {
            if (point_in_polygon(x, y, z.outline.outline)) return true;
        }
    }
    return false;
}

// Pick the closest copper layer in the stackup to use as the segment's
// return-path reference. Closeness is measured by ordinal distance; ties
// are broken in favour of higher ordinals (B.Cu side) so a 4-layer
// board with F.Cu signal naturally picks In1.Cu rather than skipping to
// In2.Cu. Returns -1 if no other copper layer exists.
int pick_reference_layer(const circuitcore::board::Board& board,
                          int signal_layer) {
    int best = -1;
    int best_dist = std::numeric_limits<int>::max();
    for (const auto& L : board.stackup.layers) {
        if (L.ordinal == signal_layer) continue;
        if (!L.is_copper()) continue;
        const int d = std::abs(L.ordinal - signal_layer);
        if (d < best_dist || (d == best_dist && L.ordinal > best)) {
            best = L.ordinal;
            best_dist = d;
        }
    }
    return best;
}

double segment_length(const circuitcore::board::Segment& s) {
    const double dx = s.end.x - s.start.x;
    const double dy = s.end.y - s.start.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

std::vector<ReturnPathViolation> detect_return_path_violations(
    const circuitcore::board::Board& board, int samples_per_segment,
    double off_plane_threshold) {
    std::vector<ReturnPathViolation> out;
    if (samples_per_segment < 2) samples_per_segment = 2;

    // Cache reference-layer choice per signal layer ordinal so we don't
    // re-walk the stackup for every segment.
    std::unordered_map<int, int> ref_layer_cache;

    for (std::size_t i = 0; i < board.segments.size(); ++i) {
        const auto& seg = board.segments[i];

        // Only look at segments on copper-signal layers. Edge.Cuts and
        // similar non-routing layers get skipped silently.
        const auto* L = board.find_layer(seg.layer_ordinal);
        if (!L || !L->is_copper()) continue;

        // Skip nets we can't return (e.g. nets 0 with no name).
        if (seg.net_id <= 0) continue;

        int ref = -1;
        auto it = ref_layer_cache.find(seg.layer_ordinal);
        if (it != ref_layer_cache.end()) {
            ref = it->second;
        } else {
            ref = pick_reference_layer(board, seg.layer_ordinal);
            ref_layer_cache[seg.layer_ordinal] = ref;
        }

        const double seg_L = segment_length(seg);
        if (seg_L <= 0.0) continue;

        int off_count = 0;
        if (ref < 0) {
            // No reference layer at all -- whole segment is in trouble.
            off_count = samples_per_segment;
        } else {
            for (int k = 0; k < samples_per_segment; ++k) {
                const double t = static_cast<double>(k) /
                                  (samples_per_segment - 1);
                const double x = seg.start.x + t * (seg.end.x - seg.start.x);
                const double y = seg.start.y + t * (seg.end.y - seg.start.y);
                if (!any_zone_covers(board, ref, x, y)) ++off_count;
            }
        }
        const double frac =
            static_cast<double>(off_count) / samples_per_segment;
        if (frac <= off_plane_threshold) continue;

        ReturnPathViolation v;
        v.segment_index = i;
        v.net_id = seg.net_id;
        v.signal_layer = seg.layer_ordinal;
        v.reference_layer = ref;
        v.segment_length_m = seg_L;
        v.off_plane_fraction = frac;
        v.severity_m = frac * seg_L;
        out.push_back(v);
    }

    // Sort worst-first so the report leads with the offending segment.
    std::sort(out.begin(), out.end(),
               [](const ReturnPathViolation& a, const ReturnPathViolation& b) {
                   return a.severity_m > b.severity_m;
               });
    return out;
}

}  // namespace sikit::si
