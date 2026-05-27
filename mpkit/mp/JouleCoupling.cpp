// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "mp/JouleCoupling.h"

#include <cstddef>

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

namespace mpkit {

JouleSourceField ir_solution_to_joule_source(
    const pdnkit::pi::IrMesh& mesh,
    const pdnkit::pi::Solution& solution,
    const VoxelMaterialField& material_field) {

    JouleSourceField out;
    const auto& g  = material_field.grid;
    const int    nx = g.nx(), ny = g.ny(), nz = g.nz();
    const std::size_t N =
        static_cast<std::size_t>(nx) * ny * nz;
    if (N == 0) {
        out.error = "ir_solution_to_joule_source: empty grid";
        return out;
    }
    if (mesh.nodes.empty()) {
        // No nodes -> source field is just zeros. Treat as ok rather
        // than an error so the caller can chain mp solves uniformly.
        out.source.resize(nx, ny, nz);
        out.source.fill(0.0);
        out.ok = true;
        return out;
    }
    if (solution.voltages.size() != mesh.nodes.size()) {
        out.error =
            "ir_solution_to_joule_source: voltages size != nodes size";
        return out;
    }
    if (g.dx() <= 0 || g.dy() <= 0 || g.dz() <= 0) {
        out.error = "ir_solution_to_joule_source: non-positive grid spacing";
        return out;
    }

    // Pre-compute the (i, j, k) of every node up-front. nullopt-style
    // sentinel: any axis < 0 marks the node as out-of-grid (or its
    // layer absent from the layer_ordinal_to_k map).
    struct VoxIdx { int i = -1, j = -1, k = -1; };
    std::vector<VoxIdx> node_vox(mesh.nodes.size());
    for (std::size_t n = 0; n < mesh.nodes.size(); ++n) {
        const auto& nd = mesh.nodes[n];
        auto it = material_field.layer_ordinal_to_k.find(nd.layer_ordinal);
        if (it == material_field.layer_ordinal_to_k.end()) {
            ++out.dropped_nodes;
            continue;
        }
        const int k = it->second;
        auto ij = g.world_to_index(nd.x, nd.y, /*z=*/g.cz(k));
        if (ij[0] < 0 || ij[1] < 0) {
            ++out.dropped_nodes;
            continue;
        }
        node_vox[n] = {ij[0], ij[1], k};
    }

    // Accumulate Watts per voxel.
    std::vector<double> watts(N, 0.0);
    for (const auto& r : mesh.resistors) {
        if (r.from_node < 0 ||
            static_cast<std::size_t>(r.from_node) >= mesh.nodes.size()) continue;
        if (r.to_node < 0 ||
            static_cast<std::size_t>(r.to_node) >= mesh.nodes.size()) continue;
        const double dv = solution.voltages[r.from_node]
                         - solution.voltages[r.to_node];
        const double P  = dv * dv * r.conductance;          // Watts
        const VoxIdx& a = node_vox[r.from_node];
        const VoxIdx& b = node_vox[r.to_node];
        const double half = 0.5 * P;
        if (a.i >= 0) {
            const std::size_t idx =
                static_cast<std::size_t>(a.i)
                + static_cast<std::size_t>(a.j) * nx
                + static_cast<std::size_t>(a.k) * nx * ny;
            watts[idx] += half;
        }
        if (b.i >= 0) {
            const std::size_t idx =
                static_cast<std::size_t>(b.i)
                + static_cast<std::size_t>(b.j) * nx
                + static_cast<std::size_t>(b.k) * nx * ny;
            watts[idx] += half;
        }
        out.total_power_w += P;
    }

    const double V = g.dx() * g.dy() * g.dz();
    const double inv_V = 1.0 / V;
    out.source.resize(nx, ny, nz);
    for (std::size_t r = 0; r < N; ++r)
        out.source.data()[r] = watts[r] * inv_V;
    out.ok = true;
    return out;
}

}  // namespace mpkit
