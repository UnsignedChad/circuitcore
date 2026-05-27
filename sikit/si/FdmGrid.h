// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Cell-centred 2D finite-difference grid for the cross-section Laplace solve.
//
// Indexing: cell (i, j) covers the rectangle
//   y ∈ [y_min + i·h, y_min + (i+1)·h]
//   z ∈ [z_min + j·h, z_min + (j+1)·h]
// with i ∈ [0, ny), j ∈ [0, nz). All cells are square with side `h`.
//
// We store one ε_r and one V per cell, plus a conductor mask and per-cell
// conductor id (for charge extraction). Outer rows/columns are Dirichlet
// boundary cells held at 0.

#pragma once

#include <cstddef>
#include <vector>

namespace sikit::em2d {

struct FdmGrid {
    double y_min = 0.0;
    double z_min = 0.0;
    double h = 0.0;          // cell size (m)
    int ny = 0;
    int nz = 0;

    std::vector<double> V;             // potential per cell
    std::vector<double> epsilon_r;     // relative permittivity per cell
    std::vector<unsigned char> is_conductor;  // 0/1 mask
    std::vector<int> conductor_id;     // id when is_conductor==1, -1 otherwise

    std::size_t idx(int i, int j) const noexcept {
        return static_cast<std::size_t>(i) +
               static_cast<std::size_t>(j) * static_cast<std::size_t>(ny);
    }

    // Cell centre in world coordinates.
    double y_center(int i) const noexcept { return y_min + (i + 0.5) * h; }
    double z_center(int j) const noexcept { return z_min + (j + 0.5) * h; }
};

}  // namespace sikit::em2d
