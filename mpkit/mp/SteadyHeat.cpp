// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "mp/SteadyHeat.h"

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace mpkit {

namespace {

// Look up thermal conductivity for the material at (i, j, k). Returns
// NaN if the material id is out of range or its k is unset; the caller
// turns NaN into an error.
double k_at(const SteadyHeatConfig& cfg, int i, int j, int k_idx) {
    const auto& g = cfg.material_field.grid;
    const std::size_t idx =
        static_cast<std::size_t>(i)
        + static_cast<std::size_t>(j) * g.nx()
        + static_cast<std::size_t>(k_idx) * g.nx() * g.ny();
    const MaterialId id = cfg.material_field.ids[idx];
    if (id >= cfg.material_table.size())
        return std::numeric_limits<double>::quiet_NaN();
    return cfg.material_table[id].thermal_conductivity;
}

// Harmonic mean -- correct face conductivity when the two adjacent
// cells have different k. Degenerates to k when they are equal.
double k_face(double k1, double k2) {
    if (k1 <= 0.0 || k2 <= 0.0) return 0.0;
    return 2.0 * k1 * k2 / (k1 + k2);
}

// Linear unknown index for a voxel.
inline std::size_t lin(int i, int j, int k, int nx, int ny) {
    return static_cast<std::size_t>(i)
         + static_cast<std::size_t>(j) * nx
         + static_cast<std::size_t>(k) * nx * ny;
}

// Iterate over all (i, j) cells that sit on a given outer face. The
// caller's lambda receives the boundary-cell index (i, j, k) and the
// inward-pointing axis identifier so it can dispatch its BC stencil.
template <class F>
void for_each_face_cell(BcTarget tgt, int nx, int ny, int nz, F&& fn) {
    switch (tgt) {
        case BcTarget::FaceXmin:
            for (int k = 0; k < nz; ++k)
                for (int j = 0; j < ny; ++j) fn(0, j, k);
            break;
        case BcTarget::FaceXmax:
            for (int k = 0; k < nz; ++k)
                for (int j = 0; j < ny; ++j) fn(nx - 1, j, k);
            break;
        case BcTarget::FaceYmin:
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i) fn(i, 0, k);
            break;
        case BcTarget::FaceYmax:
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i) fn(i, ny - 1, k);
            break;
        case BcTarget::FaceZmin:
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) fn(i, j, 0);
            break;
        case BcTarget::FaceZmax:
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) fn(i, j, nz - 1);
            break;
        case BcTarget::VoxelRange:
            // Caller-supplied range. Not a face -- handled separately.
            break;
    }
}

}  // namespace

SteadyHeatResult solve_steady_heat(const SteadyHeatConfig& cfg) {
    using Sparse = Eigen::SparseMatrix<double>;
    using Triplet = Eigen::Triplet<double>;
    SteadyHeatResult out;

    const auto& g  = cfg.material_field.grid;
    const int    nx = g.nx(), ny = g.ny(), nz = g.nz();
    const double dx = g.dx(), dy = g.dy(), dz = g.dz();
    const std::size_t N =
        static_cast<std::size_t>(nx) * ny * nz;
    if (N == 0 || dx <= 0 || dy <= 0 || dz <= 0) {
        out.error = "solve_steady_heat: empty or degenerate grid";
        return out;
    }
    if (cfg.material_field.ids.size() != N) {
        out.error = "solve_steady_heat: material_field.ids size mismatch";
        return out;
    }
    if (cfg.material_table.empty()) {
        out.error = "solve_steady_heat: material_table is empty";
        return out;
    }
    // Face areas (m^2) and one-sided distance to neighbour centre (m).
    const double Ax = dy * dz, Ay = dx * dz, Az = dx * dy;
    const double V  = dx * dy * dz;

    // -- assembly ----------------------------------------------------
    std::vector<Triplet> trips;
    trips.reserve(7 * N);
    Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(N));

    // Volumetric source contribution (Q * V) per voxel.
    const bool have_src =
        cfg.volumetric_source.size() ==
        static_cast<std::size_t>(nx) * ny * nz;
    for (int kk = 0; kk < nz; ++kk) {
        for (int jj = 0; jj < ny; ++jj) {
            for (int ii = 0; ii < nx; ++ii) {
                const std::size_t row = lin(ii, jj, kk, nx, ny);
                double diag = 0.0;
                const double k_ijk = k_at(cfg, ii, jj, kk);
                if (std::isnan(k_ijk) || k_ijk <= 0.0) {
                    out.error =
                        "solve_steady_heat: missing or non-positive "
                        "thermal_conductivity for some material";
                    return out;
                }
                // Add the six interior-face stencils. Faces on the
                // outer boundary contribute nothing here; BCs add their
                // own stencil below.
                auto add_face = [&](int ni, int nj, int nk,
                                     double area, double distance) {
                    if (ni < 0 || nj < 0 || nk < 0 ||
                        ni >= nx || nj >= ny || nk >= nz) return;
                    const double kn = k_at(cfg, ni, nj, nk);
                    if (std::isnan(kn) || kn <= 0.0) return;
                    const double kf = k_face(k_ijk, kn);
                    const double c  = kf * area / distance;
                    diag += c;
                    trips.emplace_back(
                        static_cast<int>(row),
                        static_cast<int>(lin(ni, nj, nk, nx, ny)),
                        -c);
                };
                add_face(ii - 1, jj, kk, Ax, dx);
                add_face(ii + 1, jj, kk, Ax, dx);
                add_face(ii, jj - 1, kk, Ay, dy);
                add_face(ii, jj + 1, kk, Ay, dy);
                add_face(ii, jj, kk - 1, Az, dz);
                add_face(ii, jj, kk + 1, Az, dz);

                trips.emplace_back(static_cast<int>(row),
                                    static_cast<int>(row), diag);
                if (have_src) {
                    const std::size_t sidx = lin(ii, jj, kk, nx, ny);
                    b[static_cast<Eigen::Index>(row)] +=
                        cfg.volumetric_source.data()[sidx] * V;
                }
            }
        }
    }

    // -- boundary conditions ----------------------------------------
    // Track which rows have been "Dirichlet pinned" so we can keep the
    // matrix symmetric: pinning is implemented as (replace row with
    // identity, set RHS to value, AND set the column to zero with the
    // corresponding RHS adjustments) via the standard symmetric-pin
    // trick. For simplicity v1 does a non-symmetric pin (row only) and
    // uses BiCGSTAB if Cholesky fails -- but we use the standard
    // technique below.

    std::vector<bool> dirichlet_pinned(N, false);
    std::vector<double> dirichlet_value(N, 0.0);

    for (const auto& bc : cfg.bcs) {
        switch (bc.kind) {
            case BcKind::Dirichlet: {
                // Ghost-cell flavour: pin the boundary voxel to value.
                auto pin = [&](int i, int j, int k) {
                    const std::size_t row = lin(i, j, k, nx, ny);
                    dirichlet_pinned[row] = true;
                    dirichlet_value[row]  = bc.value;
                };
                if (bc.target == BcTarget::VoxelRange) {
                    const auto& r = bc.range;
                    for (int k = r.k_lo; k <= r.k_hi; ++k)
                        for (int j = r.j_lo; j <= r.j_hi; ++j)
                            for (int i = r.i_lo; i <= r.i_hi; ++i) {
                                if (i < 0 || j < 0 || k < 0 ||
                                    i >= nx || j >= ny || k >= nz) continue;
                                pin(i, j, k);
                            }
                } else {
                    for_each_face_cell(bc.target, nx, ny, nz, pin);
                }
                break;
            }
            case BcKind::Neumann: {
                // q (W/m^2) flowing inward through the face. Add q*A to
                // the RHS of the boundary voxel.
                auto add_q = [&](int i, int j, int k, double area) {
                    const std::size_t row = lin(i, j, k, nx, ny);
                    b[static_cast<Eigen::Index>(row)] += bc.value * area;
                };
                switch (bc.target) {
                    case BcTarget::FaceXmin: case BcTarget::FaceXmax:
                        for_each_face_cell(bc.target, nx, ny, nz,
                            [&](int i, int j, int k) { add_q(i, j, k, Ax); });
                        break;
                    case BcTarget::FaceYmin: case BcTarget::FaceYmax:
                        for_each_face_cell(bc.target, nx, ny, nz,
                            [&](int i, int j, int k) { add_q(i, j, k, Ay); });
                        break;
                    case BcTarget::FaceZmin: case BcTarget::FaceZmax:
                        for_each_face_cell(bc.target, nx, ny, nz,
                            [&](int i, int j, int k) { add_q(i, j, k, Az); });
                        break;
                    case BcTarget::VoxelRange:
                        // Neumann inside the domain is unusual; ignore for v1.
                        break;
                }
                break;
            }
            case BcKind::Robin: {
                // -k dT/dn = h (T - u_ref). For a face of area A this
                // adds h*A to the diagonal and h*A*u_ref to the RHS of
                // the adjacent boundary voxel.
                auto add_robin = [&](int i, int j, int k, double area) {
                    const std::size_t row = lin(i, j, k, nx, ny);
                    trips.emplace_back(static_cast<int>(row),
                                        static_cast<int>(row),
                                        bc.h * area);
                    b[static_cast<Eigen::Index>(row)] += bc.h * area * bc.u_ref;
                };
                switch (bc.target) {
                    case BcTarget::FaceXmin: case BcTarget::FaceXmax:
                        for_each_face_cell(bc.target, nx, ny, nz,
                            [&](int i, int j, int k) { add_robin(i, j, k, Ax); });
                        break;
                    case BcTarget::FaceYmin: case BcTarget::FaceYmax:
                        for_each_face_cell(bc.target, nx, ny, nz,
                            [&](int i, int j, int k) { add_robin(i, j, k, Ay); });
                        break;
                    case BcTarget::FaceZmin: case BcTarget::FaceZmax:
                        for_each_face_cell(bc.target, nx, ny, nz,
                            [&](int i, int j, int k) { add_robin(i, j, k, Az); });
                        break;
                    case BcTarget::VoxelRange:
                        break;
                }
                break;
            }
        }
    }

    // -- build the sparse matrix, apply Dirichlet pins symmetrically -
    Sparse A(static_cast<int>(N), static_cast<int>(N));
    A.setFromTriplets(trips.begin(), trips.end());
    A.makeCompressed();

    bool any_dirichlet = false;
    for (std::size_t r = 0; r < N; ++r) {
        if (dirichlet_pinned[r]) { any_dirichlet = true; break; }
    }
    if (!any_dirichlet) {
        out.error =
            "solve_steady_heat: at least one Dirichlet boundary condition "
            "is required (pure-Neumann + Robin systems are singular up "
            "to a constant)";
        return out;
    }

    // Standard symmetric pinning: for each pinned row r with value v,
    // for every off-diagonal entry A(j, r) move -A(j, r)*v to b[j] then
    // zero A(j, r). Then zero row r and set A(r, r) = 1, b[r] = v.
    // This preserves SPD so SimplicialLLT continues to work.
    for (int col = 0; col < A.outerSize(); ++col) {
        for (Sparse::InnerIterator it(A, col); it; ++it) {
            const int r = it.row();
            const int c = it.col();
            if (dirichlet_pinned[c] && r != c) {
                b[r] -= it.value() * dirichlet_value[c];
                it.valueRef() = 0.0;
            }
        }
    }
    for (std::size_t r = 0; r < N; ++r) {
        if (!dirichlet_pinned[r]) continue;
        // Zero the entire row r, then set diagonal to 1 and RHS to value.
        for (Sparse::InnerIterator it(A, static_cast<int>(r)); it; ++it) {
            it.valueRef() = (it.row() == it.col()) ? 1.0 : 0.0;
        }
        // Some non-zeros in row r are stored in OTHER columns; scrub
        // them too. Cheapest correct way: iterate every column once.
        for (int col = 0; col < A.outerSize(); ++col) {
            for (Sparse::InnerIterator it(A, col); it; ++it) {
                if (static_cast<std::size_t>(it.row()) == r &&
                    it.row() != it.col()) {
                    it.valueRef() = 0.0;
                }
            }
        }
        b[static_cast<Eigen::Index>(r)] = dirichlet_value[r];
    }
    A.prune(0.0);  // drop the zeroed-out entries

    // -- solve ------------------------------------------------------
    Eigen::SimplicialLLT<Sparse> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        out.error = "solve_steady_heat: Cholesky factorization failed";
        return out;
    }
    Eigen::VectorXd x = solver.solve(b);
    if (solver.info() != Eigen::Success) {
        out.error = "solve_steady_heat: solver back-substitution failed";
        return out;
    }

    out.temperature.resize(nx, ny, nz);
    for (std::size_t r = 0; r < N; ++r) {
        out.temperature.data()[r] = x[static_cast<Eigen::Index>(r)];
    }
    out.n_unknowns = N;
    out.nnz        = static_cast<std::size_t>(A.nonZeros());
    out.ok = true;
    return out;
}

}  // namespace mpkit
