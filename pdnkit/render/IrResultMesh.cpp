// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "render/IrResultMesh.h"

namespace pdnkit::render {

IrResultMesh build_ir_result_mesh(const pi::IrMesh& mesh,
                                   const pi::Solution& solution,
                                   double cell_size) {
    IrResultMesh out;
    if (!solution.ok || mesh.nodes.empty() ||
        solution.voltages.size() != mesh.nodes.size()) {
        return out;
    }

    out.v_min = solution.min_v;
    out.v_max = solution.max_v;
    const double span = (out.v_max - out.v_min);
    const double inv_span = (span > 0.0) ? (1.0 / span) : 0.0;

    // Group node indices by layer ordinal, preserving first-seen layer order.
    std::vector<int> layer_order;
    std::unordered_map<int, std::vector<std::size_t>> by_layer;
    by_layer.reserve(8);
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const int ord = mesh.nodes[i].layer_ordinal;
        auto [it, inserted] = by_layer.try_emplace(ord);
        if (inserted) layer_order.push_back(ord);
        it->second.push_back(i);
    }

    out.vertices.reserve(mesh.nodes.size() * 12);  // 4 verts × 3 floats
    out.indices.reserve(mesh.nodes.size() * 6);    // 2 tris × 3 indices

    const float hs = static_cast<float>(0.5 * cell_size);

    for (int ord : layer_order) {
        IrResultMesh::LayerRange range;
        range.ordinal = ord;
        range.index_start = static_cast<int>(out.indices.size());

        for (std::size_t i : by_layer[ord]) {
            const auto& n = mesh.nodes[i];
            const float cx = static_cast<float>(n.x);
            const float cy = static_cast<float>(n.y);
            const double v = solution.voltages[i];
            const float t = static_cast<float>((v - out.v_min) * inv_span);
            const auto base = static_cast<std::uint32_t>(out.vertex_count());

            out.vertices.insert(out.vertices.end(),
                                {cx - hs, cy - hs, t,
                                 cx + hs, cy - hs, t,
                                 cx + hs, cy + hs, t,
                                 cx - hs, cy + hs, t});

            out.indices.insert(out.indices.end(),
                               {base + 0, base + 1, base + 2,
                                base + 0, base + 2, base + 3});
        }

        range.index_count = static_cast<int>(out.indices.size()) - range.index_start;
        out.layer_ranges.push_back(range);
    }

    // Markers: prefer the per-node currents (multi-pad case); fall back to
    // source/sink lists with synthetic +/-1 (the v0 split-current case).
    if (!mesh.node_currents.empty()) {
        for (const auto& [nid, cur] : mesh.node_currents) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, cur});
        }
    } else {
        for (int nid : mesh.source_node_ids) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, +1.0});
        }
        for (int nid : mesh.sink_node_ids) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, -1.0});
        }
    }

    // Hotspot: node with the lowest voltage (= worst drop from source).
    {
        int worst = -1;
        double v_worst = 0.0;
        for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
            const double v = solution.voltages[i];
            if (worst < 0 || v < v_worst) { worst = static_cast<int>(i); v_worst = v; }
        }
        if (worst >= 0) {
            out.hotspot.valid = true;
            out.hotspot.x = mesh.nodes[worst].x;
            out.hotspot.y = mesh.nodes[worst].y;
            out.hotspot.value = v_worst;
            out.hotspot.is_current = false;
        }
    }
    return out;
}


IrResultMesh build_grid_mesh(const std::vector<double>& mags,
                              int nx, int ny,
                              double dx, double dy,
                              double origin_x, double origin_y) {
    IrResultMesh out;
    if (mags.empty() || nx < 1 || ny < 1 ||
        mags.size() != static_cast<std::size_t>(nx) * ny) {
        return out;
    }
    out.v_min = *std::min_element(mags.begin(), mags.end());
    out.v_max = *std::max_element(mags.begin(), mags.end());
    const double span = out.v_max - out.v_min;
    const double inv_span = (span > 0.0) ? 1.0 / span : 0.0;

    const float hx = static_cast<float>(0.5 * dx);
    const float hy = static_cast<float>(0.5 * dy);

    out.vertices.reserve(static_cast<std::size_t>(nx) * ny * 12);
    out.indices.reserve(static_cast<std::size_t>(nx) * ny * 6);

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const float cx = static_cast<float>(origin_x + (i + 0.5) * dx);
            const float cy = static_cast<float>(origin_y + (j + 0.5) * dy);
            const double m = mags[j * nx + i];
            const float t = static_cast<float>((m - out.v_min) * inv_span);
            const auto base = static_cast<std::uint32_t>(out.vertex_count());
            out.vertices.insert(out.vertices.end(),
                                {cx - hx, cy - hy, t,
                                 cx + hx, cy - hy, t,
                                 cx + hx, cy + hy, t,
                                 cx - hx, cy + hy, t});
            out.indices.insert(out.indices.end(),
                               {base + 0, base + 1, base + 2,
                                base + 0, base + 2, base + 3});
        }
    }
    // Single layer-range covering the whole grid; ordinal doesn't matter for
    // the heat-map shader, just put it on layer 0.
    out.layer_ranges.push_back({0, 0, static_cast<int>(out.indices.size())});
    return out;
}



IrResultMesh build_current_density_mesh(const pi::IrMesh& mesh,
                                         const pi::Solution& solution,
                                         double cell_size,
                                         double copper_thickness,
                                         double rho_cu) {
    IrResultMesh out;
    if (!solution.ok || mesh.nodes.empty() ||
        solution.voltages.size() != mesh.nodes.size() ||
        cell_size <= 0.0 || copper_thickness <= 0.0 || rho_cu <= 0.0) {
        return out;
    }

    // Build a (grid_i, grid_j, layer) -> node-index map so we can look up
    // neighbors in O(1).
    struct Key { int i, j, layer; };
    auto hashKey = [](const Key& k) {
        return (static_cast<std::size_t>(k.i) * 73856093u) ^
               (static_cast<std::size_t>(k.j) * 19349663u) ^
               (static_cast<std::size_t>(k.layer) * 83492791u);
    };
    auto eqKey = [](const Key& a, const Key& b) {
        return a.i == b.i && a.j == b.j && a.layer == b.layer;
    };
    std::unordered_map<Key, int, decltype(hashKey), decltype(eqKey)>
        lookup(mesh.nodes.size() * 2, hashKey, eqKey);
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        lookup.emplace(Key{n.grid_i, n.grid_j, n.layer_ordinal},
                       static_cast<int>(i));
    }

    // Per-node sheet current density magnitude (A/m).
    std::vector<double> jmag(mesh.nodes.size(), 0.0);
    const double sigma_sheet = copper_thickness / rho_cu;  // S/sq * t = S*m
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const auto& n = mesh.nodes[i];
        const double v = solution.voltages[i];

        auto lookup_v = [&](int di, int dj, double& dv, double& dist,
                            bool& ok) {
            ok = false;
            auto it = lookup.find(Key{n.grid_i + di, n.grid_j + dj,
                                       n.layer_ordinal});
            if (it == lookup.end()) return;
            dv = solution.voltages[it->second] - v;
            dist = cell_size * std::sqrt(static_cast<double>(di * di +
                                                              dj * dj));
            ok = true;
        };

        double dvx_p = 0, dvx_m = 0, dvy_p = 0, dvy_m = 0;
        double dx_p = 0, dx_m = 0, dy_p = 0, dy_m = 0;
        bool xp = false, xm = false, yp = false, ym = false;
        lookup_v(+1, 0, dvx_p, dx_p, xp);
        lookup_v(-1, 0, dvx_m, dx_m, xm);
        lookup_v( 0,+1, dvy_p, dy_p, yp);
        lookup_v( 0,-1, dvy_m, dy_m, ym);

        double gx = 0.0, gy = 0.0;
        if (xp && xm) {
            gx = (dvx_p - dvx_m) / (dx_p + dx_m);
        } else if (xp) {
            gx = dvx_p / dx_p;
        } else if (xm) {
            gx = -dvx_m / dx_m;
        }
        if (yp && ym) {
            gy = (dvy_p - dvy_m) / (dy_p + dy_m);
        } else if (yp) {
            gy = dvy_p / dy_p;
        } else if (ym) {
            gy = -dvy_m / dy_m;
        }
        const double grad = std::sqrt(gx * gx + gy * gy);
        // K = sigma_sheet * |E| = (t / rho) * |grad V|. Units A/m.
        jmag[i] = sigma_sheet * grad;
    }

    // Now build the same quad-per-node mesh as build_ir_result_mesh, but
    // color t = (|J| - min) / (max - min).
    out.v_min = *std::min_element(jmag.begin(), jmag.end());
    out.v_max = *std::max_element(jmag.begin(), jmag.end());

    // Hotspot: node with the largest |J| -- the bottleneck where current
    // is crowding. Skip source/sink nodes themselves (they always show
    // the steepest gradient because of the boundary, which is a
    // discretization artifact, not a real bottleneck).
    {
        std::vector<bool> is_terminal(mesh.nodes.size(), false);
        for (int nid : mesh.source_node_ids)
            if (nid >= 0 && nid < static_cast<int>(is_terminal.size()))
                is_terminal[nid] = true;
        for (int nid : mesh.sink_node_ids)
            if (nid >= 0 && nid < static_cast<int>(is_terminal.size()))
                is_terminal[nid] = true;
        int worst = -1;
        double j_worst = -1.0;
        for (std::size_t i = 0; i < jmag.size(); ++i) {
            if (is_terminal[i]) continue;
            if (jmag[i] > j_worst) { j_worst = jmag[i]; worst = static_cast<int>(i); }
        }
        // If every node was a terminal (degenerate fixture), fall back
        // to the absolute max.
        if (worst < 0) {
            for (std::size_t i = 0; i < jmag.size(); ++i) {
                if (jmag[i] > j_worst) { j_worst = jmag[i]; worst = static_cast<int>(i); }
            }
        }
        if (worst >= 0) {
            out.hotspot.valid = true;
            out.hotspot.x = mesh.nodes[worst].x;
            out.hotspot.y = mesh.nodes[worst].y;
            out.hotspot.value = j_worst;
            out.hotspot.is_current = true;
        }
    }
    const double span = out.v_max - out.v_min;
    const double inv_span = (span > 0.0) ? 1.0 / span : 0.0;

    std::vector<int> layer_order;
    std::unordered_map<int, std::vector<std::size_t>> by_layer;
    by_layer.reserve(8);
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const int ord = mesh.nodes[i].layer_ordinal;
        auto [it, inserted] = by_layer.try_emplace(ord);
        if (inserted) layer_order.push_back(ord);
        it->second.push_back(i);
    }

    out.vertices.reserve(mesh.nodes.size() * 12);
    out.indices.reserve(mesh.nodes.size() * 6);
    const float hs = static_cast<float>(0.5 * cell_size);

    for (int ord : layer_order) {
        IrResultMesh::LayerRange range;
        range.ordinal = ord;
        range.index_start = static_cast<int>(out.indices.size());
        for (std::size_t i : by_layer[ord]) {
            const auto& n = mesh.nodes[i];
            const float cx = static_cast<float>(n.x);
            const float cy = static_cast<float>(n.y);
            const float t = static_cast<float>((jmag[i] - out.v_min) *
                                                inv_span);
            const auto base = static_cast<std::uint32_t>(out.vertex_count());
            out.vertices.insert(out.vertices.end(),
                                {cx - hs, cy - hs, t,
                                 cx + hs, cy - hs, t,
                                 cx + hs, cy + hs, t,
                                 cx - hs, cy + hs, t});
            out.indices.insert(out.indices.end(),
                               {base + 0, base + 1, base + 2,
                                base + 0, base + 2, base + 3});
        }
        range.index_count = static_cast<int>(out.indices.size()) -
                            range.index_start;
        out.layer_ranges.push_back(range);
    }

    // Re-use the source/sink markers from voltage view so the user still
    // sees where the current is being driven.
    if (!mesh.node_currents.empty()) {
        for (const auto& [nid, cur] : mesh.node_currents) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, cur});
        }
    } else {
        for (int nid : mesh.source_node_ids) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, +1.0});
        }
        for (int nid : mesh.sink_node_ids) {
            if (nid < 0 || nid >= static_cast<int>(mesh.nodes.size())) continue;
            const auto& n = mesh.nodes[nid];
            out.markers.push_back({n.x, n.y, -1.0});
        }
    }

    return out;
}

}  // namespace pdnkit::render
