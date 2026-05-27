// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// mpkit Cartesian voxel grid placed in world space.
//
// Wraps circuitcore::field::GridSpec (shape + cell size) with an origin
// in metres so a grid can be aligned to the bounding box of a parsed
// Board. Every mpkit solver reads fields living on a Grid; the world
// placement is what lets the orchestrator map between physics and
// between the board geometry the user sees in the 3D viewer.

#pragma once

#include <array>
#include <cstddef>

#include "circuitcore/field/GridSpec.h"

namespace mpkit {

struct Grid {
    circuitcore::field::GridSpec spec;
    // World-space coordinates (metres) of the (i=0, j=0, k=0) corner.
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;

    int nx() const { return spec.nx; }
    int ny() const { return spec.ny; }
    int nz() const { return spec.nz; }
    double dx() const { return spec.dx; }
    double dy() const { return spec.dy; }
    double dz() const { return spec.dz; }
    std::size_t voxel_count() const {
        return static_cast<std::size_t>(spec.nx) * spec.ny * spec.nz;
    }

    // Cell-centre coordinates of (i, j, k) in world space.
    double cx(int i) const { return x0 + (i + 0.5) * spec.dx; }
    double cy(int j) const { return y0 + (j + 0.5) * spec.dy; }
    double cz(int k) const { return z0 + (k + 0.5) * spec.dz; }

    // Inverse mapping. Out-of-grid points return -1 in the failing axis.
    std::array<int, 3> world_to_index(double x, double y, double z) const {
        std::array<int, 3> out{-1, -1, -1};
        if (spec.dx <= 0 || spec.dy <= 0 || spec.dz <= 0) return out;
        const int i = static_cast<int>((x - x0) / spec.dx);
        const int j = static_cast<int>((y - y0) / spec.dy);
        const int k = static_cast<int>((z - z0) / spec.dz);
        if (i >= 0 && i < spec.nx) out[0] = i;
        if (j >= 0 && j < spec.ny) out[1] = j;
        if (k >= 0 && k < spec.nz) out[2] = k;
        return out;
    }
};

}  // namespace mpkit
