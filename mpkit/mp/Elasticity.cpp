// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "mp/Elasticity.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace mpkit {

namespace {

using Vec3   = Eigen::Vector3d;
using Vec6   = Eigen::Matrix<double, 6,  1>;
using Vec24  = Eigen::Matrix<double, 24, 1>;
using Mat6x6 = Eigen::Matrix<double, 6,  6>;
using Mat3x24= Eigen::Matrix<double, 3,  24>;
using Mat6x24= Eigen::Matrix<double, 6,  24>;
using Mat24x24 = Eigen::Matrix<double, 24, 24>;

// Local coords of the 8 corner nodes of a Q1 hex.
// Order: x fastest, then y, then z (matches the cell-corner indexing
// the assembler uses for scatter).
constexpr double kXi[8]   = {-1, +1, +1, -1, -1, +1, +1, -1};
constexpr double kEta[8]  = {-1, -1, +1, +1, -1, -1, +1, +1};
constexpr double kZeta[8] = {-1, -1, -1, -1, +1, +1, +1, +1};

// Per-corner (i, j, k) offset for cell -> node lookup. Matches the
// (xi, eta, zeta) order above.
constexpr int kDi[8] = {0, 1, 1, 0, 0, 1, 1, 0};
constexpr int kDj[8] = {0, 0, 1, 1, 0, 0, 1, 1};
constexpr int kDk[8] = {0, 0, 0, 0, 1, 1, 1, 1};

// 2x2x2 Gauss-Legendre quadrature, weight 1 per point.
constexpr double kGp = 0.5773502691896258;  // 1 / sqrt(3)

// Shape function values N_i(xi, eta, zeta) for the 8 corners.
inline std::array<double, 8> shape_N(double xi, double eta, double zeta) {
    std::array<double, 8> N{};
    for (int i = 0; i < 8; ++i) {
        N[i] = 0.125
             * (1.0 + xi   * kXi[i])
             * (1.0 + eta  * kEta[i])
             * (1.0 + zeta * kZeta[i]);
    }
    return N;
}

// Shape function derivatives in LOCAL coords. Returns 3 arrays of 8
// (dN/dxi, dN/deta, dN/dzeta).
struct ShapeDLocal {
    std::array<double, 8> dN_dxi;
    std::array<double, 8> dN_deta;
    std::array<double, 8> dN_dzeta;
};
inline ShapeDLocal shape_dN_local(double xi, double eta, double zeta) {
    ShapeDLocal d;
    for (int i = 0; i < 8; ++i) {
        d.dN_dxi[i]   = 0.125 * kXi[i]
                       * (1.0 + eta  * kEta[i])
                       * (1.0 + zeta * kZeta[i]);
        d.dN_deta[i]  = 0.125 * kEta[i]
                       * (1.0 + xi   * kXi[i])
                       * (1.0 + zeta * kZeta[i]);
        d.dN_dzeta[i] = 0.125 * kZeta[i]
                       * (1.0 + xi   * kXi[i])
                       * (1.0 + eta  * kEta[i]);
    }
    return d;
}

// Build the 6x24 strain-displacement matrix B for a regular hex of
// physical size (dx, dy, dz). J is diagonal so dN/dx = (2/dx) dN/dxi
// and similarly. Voigt order: eps_xx eps_yy eps_zz gam_xy gam_yz gam_zx.
Mat6x24 build_B(const ShapeDLocal& d, double dx, double dy, double dz) {
    const double sx = 2.0 / dx;
    const double sy = 2.0 / dy;
    const double sz = 2.0 / dz;
    Mat6x24 B = Mat6x24::Zero();
    for (int i = 0; i < 8; ++i) {
        const double dNx = sx * d.dN_dxi[i];
        const double dNy = sy * d.dN_deta[i];
        const double dNz = sz * d.dN_dzeta[i];
        const int col = 3 * i;
        B(0, col + 0) = dNx;                // d ux / dx
        B(1, col + 1) = dNy;                // d uy / dy
        B(2, col + 2) = dNz;                // d uz / dz
        B(3, col + 0) = dNy;                // gamma_xy
        B(3, col + 1) = dNx;
        B(4, col + 1) = dNz;                // gamma_yz
        B(4, col + 2) = dNy;
        B(5, col + 0) = dNz;                // gamma_zx
        B(5, col + 2) = dNx;
    }
    return B;
}

// 3x24 shape-function matrix used for body-force lumping. Column 3i+c
// holds N_i in row c.
Mat3x24 build_N_matrix(const std::array<double, 8>& N) {
    Mat3x24 NN = Mat3x24::Zero();
    for (int i = 0; i < 8; ++i) {
        NN(0, 3 * i + 0) = N[i];
        NN(1, 3 * i + 1) = N[i];
        NN(2, 3 * i + 2) = N[i];
    }
    return NN;
}

// Isotropic elasticity matrix.
Mat6x6 build_D(double E, double nu) {
    Mat6x6 D = Mat6x6::Zero();
    const double lam = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double mu  = E / (2.0 * (1.0 + nu));
    D(0, 0) = lam + 2.0 * mu;  D(0, 1) = lam;             D(0, 2) = lam;
    D(1, 0) = lam;             D(1, 1) = lam + 2.0 * mu;  D(1, 2) = lam;
    D(2, 0) = lam;             D(2, 1) = lam;             D(2, 2) = lam + 2.0 * mu;
    D(3, 3) = mu;
    D(4, 4) = mu;
    D(5, 5) = mu;
    return D;
}

inline std::size_t node_index(int i, int j, int k, int nx1, int ny1) {
    return static_cast<std::size_t>(i)
         + static_cast<std::size_t>(j) * nx1
         + static_cast<std::size_t>(k) * nx1 * ny1;
}

inline std::size_t cell_index(int i, int j, int k, int nx, int ny) {
    return static_cast<std::size_t>(i)
         + static_cast<std::size_t>(j) * nx
         + static_cast<std::size_t>(k) * nx * ny;
}

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
            break;
    }
}

// Pin every NODE belonging to the boundary face of the matching cells.
// Each boundary face of one cell is a 4-node patch; FaceXmin of cell
// (0, j, k) covers nodes (0, j, k), (0, j+1, k), (0, j, k+1),
// (0, j+1, k+1). Calling this on every face-cell visits each shared
// node multiple times -- the assembler is idempotent (sets bool).
template <class F>
void for_each_face_node(BcTarget tgt, int nx1, int ny1, int nz1, F&& fn) {
    switch (tgt) {
        case BcTarget::FaceXmin:
            for (int k = 0; k < nz1; ++k)
                for (int j = 0; j < ny1; ++j) fn(0, j, k);
            break;
        case BcTarget::FaceXmax:
            for (int k = 0; k < nz1; ++k)
                for (int j = 0; j < ny1; ++j) fn(nx1 - 1, j, k);
            break;
        case BcTarget::FaceYmin:
            for (int k = 0; k < nz1; ++k)
                for (int i = 0; i < nx1; ++i) fn(i, 0, k);
            break;
        case BcTarget::FaceYmax:
            for (int k = 0; k < nz1; ++k)
                for (int i = 0; i < nx1; ++i) fn(i, ny1 - 1, k);
            break;
        case BcTarget::FaceZmin:
            for (int j = 0; j < ny1; ++j)
                for (int i = 0; i < nx1; ++i) fn(i, j, 0);
            break;
        case BcTarget::FaceZmax:
            for (int j = 0; j < ny1; ++j)
                for (int i = 0; i < nx1; ++i) fn(i, j, nz1 - 1);
            break;
        case BcTarget::VoxelRange:
            break;
    }
}

}  // namespace

ElasticityResult solve_elasticity(const ElasticityConfig& cfg) {
    using Sparse  = Eigen::SparseMatrix<double>;
    using Triplet = Eigen::Triplet<double>;
    ElasticityResult out;

    const auto& g  = cfg.material_field.grid;
    const int   nx = g.nx(), ny = g.ny(), nz = g.nz();
    const double dx = g.dx(), dy = g.dy(), dz = g.dz();
    const std::size_t n_cells =
        static_cast<std::size_t>(nx) * ny * nz;
    if (n_cells == 0 || dx <= 0 || dy <= 0 || dz <= 0) {
        out.error = "solve_elasticity: empty or degenerate grid";
        return out;
    }
    if (cfg.material_field.ids.size() != n_cells) {
        out.error = "solve_elasticity: material_field.ids size mismatch";
        return out;
    }

    const int nx1 = nx + 1, ny1 = ny + 1, nz1 = nz + 1;
    const std::size_t n_nodes =
        static_cast<std::size_t>(nx1) * ny1 * nz1;
    const std::size_t N = 3 * n_nodes;  // total dofs

    const double detJ = (dx * dy * dz) / 8.0;

    // Pre-compute the per-Gauss-point B matrices and N vectors. They
    // depend only on the 8 GP local coords + the cell size, both of
    // which are constant across the grid (regular spacing).
    std::array<Mat6x24, 8> B_gp;
    std::array<Mat3x24, 8> N_gp;
    std::array<double,  8> w_gp;
    constexpr double s[2] = {-kGp, +kGp};
    int gp = 0;
    for (int kk = 0; kk < 2; ++kk) {
        for (int jj = 0; jj < 2; ++jj) {
            for (int ii = 0; ii < 2; ++ii) {
                auto d = shape_dN_local(s[ii], s[jj], s[kk]);
                auto Nv = shape_N    (s[ii], s[jj], s[kk]);
                B_gp[gp] = build_B(d, dx, dy, dz);
                N_gp[gp] = build_N_matrix(Nv);
                w_gp[gp] = 1.0;
                ++gp;
            }
        }
    }

    // -- assembly ---------------------------------------------------
    std::vector<Triplet> trips;
    trips.reserve(24 * 24 * n_cells);
    Eigen::VectorXd b = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(N));

    const bool have_bx = cfg.body_force_x.size() == n_cells;
    const bool have_by = cfg.body_force_y.size() == n_cells;
    const bool have_bz = cfg.body_force_z.size() == n_cells;
    const bool have_dT = cfg.temperature_change.size() == n_cells;

    for (int kc = 0; kc < nz; ++kc) {
        for (int jc = 0; jc < ny; ++jc) {
            for (int ic = 0; ic < nx; ++ic) {
                const std::size_t c_idx = cell_index(ic, jc, kc, nx, ny);
                const MaterialId id = cfg.material_field.ids[c_idx];
                if (id >= cfg.material_table.size()) {
                    out.error = "solve_elasticity: material id out of range";
                    return out;
                }
                const auto& m = cfg.material_table[id];
                if (std::isnan(m.youngs_modulus) || m.youngs_modulus <= 0 ||
                    std::isnan(m.poissons_ratio)) {
                    out.error =
                        "solve_elasticity: missing youngs_modulus or "
                        "poissons_ratio for some material";
                    return out;
                }
                const Mat6x6 D = build_D(m.youngs_modulus, m.poissons_ratio);

                Mat24x24 Ke = Mat24x24::Zero();
                Vec24    fe = Vec24::Zero();

                Vec3 body{
                    have_bx ? cfg.body_force_x.data()[c_idx] : 0.0,
                    have_by ? cfg.body_force_y.data()[c_idx] : 0.0,
                    have_bz ? cfg.body_force_z.data()[c_idx] : 0.0
                };

                double dT = 0.0;
                if (have_dT) dT = cfg.temperature_change.data()[c_idx];
                const double alpha =
                    std::isnan(m.thermal_expansion) ? 0.0 : m.thermal_expansion;
                Vec6 eps_th = Vec6::Zero();
                if (have_dT && alpha != 0.0 && dT != 0.0) {
                    const double e = alpha * dT;
                    eps_th(0) = e; eps_th(1) = e; eps_th(2) = e;
                }

                for (int g_i = 0; g_i < 8; ++g_i) {
                    const Mat6x24& B  = B_gp[g_i];
                    const Mat3x24& NN = N_gp[g_i];
                    const double   w  = w_gp[g_i] * detJ;
                    Ke.noalias() += w * (B.transpose() * D * B);
                    if (eps_th.norm() > 0.0) {
                        fe.noalias() += w * (B.transpose() * D * eps_th);
                    }
                    if (body.norm() > 0.0) {
                        fe.noalias() += w * (NN.transpose() * body);
                    }
                }

                // Scatter Ke / fe into the global matrix using the 8
                // corner-node dof indices for this cell.
                std::array<int, 24> dof;
                for (int n = 0; n < 8; ++n) {
                    const int ni = ic + kDi[n];
                    const int nj = jc + kDj[n];
                    const int nk = kc + kDk[n];
                    const std::size_t base =
                        3 * node_index(ni, nj, nk, nx1, ny1);
                    dof[3 * n + 0] = static_cast<int>(base + 0);
                    dof[3 * n + 1] = static_cast<int>(base + 1);
                    dof[3 * n + 2] = static_cast<int>(base + 2);
                }
                for (int r = 0; r < 24; ++r) {
                    b[dof[r]] += fe(r);
                    for (int c = 0; c < 24; ++c) {
                        trips.emplace_back(dof[r], dof[c], Ke(r, c));
                    }
                }
            }
        }
    }

    // -- boundary conditions ----------------------------------------
    std::vector<bool>   pinned(N, false);
    std::vector<double> pin_value(N, 0.0);

    for (const auto& bc : cfg.bcs) {
        const auto apply_at_node = [&](int ni, int nj, int nk) {
            if (ni < 0 || nj < 0 || nk < 0 ||
                ni >= nx1 || nj >= ny1 || nk >= nz1) return;
            const std::size_t base = 3 * node_index(ni, nj, nk, nx1, ny1);
            if (bc.pin_axes & DofX) {
                pinned[base + 0] = true; pin_value[base + 0] = bc.pin_ux;
            }
            if (bc.pin_axes & DofY) {
                pinned[base + 1] = true; pin_value[base + 1] = bc.pin_uy;
            }
            if (bc.pin_axes & DofZ) {
                pinned[base + 2] = true; pin_value[base + 2] = bc.pin_uz;
            }
        };
        if (bc.target == BcTarget::VoxelRange) {
            const auto& r = bc.range;
            for (int k = r.k_lo; k <= r.k_hi + 1; ++k)
                for (int j = r.j_lo; j <= r.j_hi + 1; ++j)
                    for (int i = r.i_lo; i <= r.i_hi + 1; ++i)
                        apply_at_node(i, j, k);
        } else {
            for_each_face_node(bc.target, nx1, ny1, nz1, apply_at_node);
        }
    }

    bool any_pin = false;
    for (std::size_t d = 0; d < N; ++d) {
        if (pinned[d]) { any_pin = true; break; }
    }
    if (!any_pin) {
        out.error =
            "solve_elasticity: at least one Dirichlet pin is required "
            "(rigid body modes are otherwise unconstrained)";
        return out;
    }

    Sparse A(static_cast<int>(N), static_cast<int>(N));
    A.setFromTriplets(trips.begin(), trips.end());
    A.makeCompressed();

    // Standard symmetric pinning: for each pinned column c with value
    // v, push v * A(:, c) over to the RHS, then zero column c. Then
    // for each pinned row r, zero the row and set diag to 1, RHS to v.
    for (int col = 0; col < A.outerSize(); ++col) {
        if (!pinned[static_cast<std::size_t>(col)]) continue;
        for (Sparse::InnerIterator it(A, col); it; ++it) {
            const int r = it.row();
            if (r != col) b[r] -= it.value() * pin_value[col];
            it.valueRef() = 0.0;
        }
    }
    for (std::size_t r = 0; r < N; ++r) {
        if (!pinned[r]) continue;
        for (int col = 0; col < A.outerSize(); ++col) {
            for (Sparse::InnerIterator it(A, col); it; ++it) {
                if (static_cast<std::size_t>(it.row()) == r) {
                    it.valueRef() = (it.row() == it.col()) ? 1.0 : 0.0;
                }
            }
        }
        b[static_cast<Eigen::Index>(r)] = pin_value[r];
    }
    A.prune(0.0);

    // -- solve ------------------------------------------------------
    Eigen::SimplicialLLT<Sparse> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        out.error = "solve_elasticity: Cholesky factorization failed";
        return out;
    }
    Eigen::VectorXd x = solver.solve(b);
    if (solver.info() != Eigen::Success) {
        out.error = "solve_elasticity: solver back-substitution failed";
        return out;
    }

    out.displacements.assign(N, 0.0);
    for (std::size_t d = 0; d < N; ++d)
        out.displacements[d] = x[static_cast<Eigen::Index>(d)];

    // -- post-process stress + von Mises at cell centres ------------
    out.stress.assign(6 * n_cells, 0.0);
    out.von_mises.resize(nx, ny, nz);
    auto d_centre = shape_dN_local(0.0, 0.0, 0.0);
    Mat6x24 B_centre = build_B(d_centre, dx, dy, dz);
    for (int kc = 0; kc < nz; ++kc) {
        for (int jc = 0; jc < ny; ++jc) {
            for (int ic = 0; ic < nx; ++ic) {
                const std::size_t c_idx = cell_index(ic, jc, kc, nx, ny);
                const MaterialId id = cfg.material_field.ids[c_idx];
                const auto& m = cfg.material_table[id];
                const Mat6x6 D = build_D(m.youngs_modulus, m.poissons_ratio);
                Vec24 u_e;
                for (int n = 0; n < 8; ++n) {
                    const int ni = ic + kDi[n];
                    const int nj = jc + kDj[n];
                    const int nk = kc + kDk[n];
                    const std::size_t base =
                        3 * node_index(ni, nj, nk, nx1, ny1);
                    u_e(3 * n + 0) = out.displacements[base + 0];
                    u_e(3 * n + 1) = out.displacements[base + 1];
                    u_e(3 * n + 2) = out.displacements[base + 2];
                }
                Vec6 eps = B_centre * u_e;
                if (cfg.temperature_change.size() == n_cells) {
                    const double dT_c =
                        cfg.temperature_change.data()[c_idx];
                    const double alpha =
                        std::isnan(m.thermal_expansion) ? 0.0
                                                        : m.thermal_expansion;
                    const double e_th = alpha * dT_c;
                    eps(0) -= e_th; eps(1) -= e_th; eps(2) -= e_th;
                }
                Vec6 sig = D * eps;
                for (int v = 0; v < 6; ++v) out.stress[6 * c_idx + v] = sig(v);
                const double s11 = sig(0), s22 = sig(1), s33 = sig(2);
                const double s12 = sig(3), s23 = sig(4), s13 = sig(5);
                const double vm = std::sqrt(0.5 * (
                    (s11 - s22) * (s11 - s22) +
                    (s22 - s33) * (s22 - s33) +
                    (s33 - s11) * (s33 - s11) +
                    6.0 * (s12 * s12 + s23 * s23 + s13 * s13)));
                out.von_mises.data()[c_idx] = vm;
            }
        }
    }

    out.n_dofs = N;
    out.ok = true;
    return out;
}

}  // namespace mpkit
