// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "mp/TransientHeat.h"

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace mpkit {

namespace {

// Look up thermal conductivity for the material at (i, j, k). Returns
// NaN if the material id is out of range or its k is unset.
double k_at(const TransientHeatConfig& cfg, int i, int j, int kk) {
    const auto& g = cfg.material_field.grid;
    const std::size_t idx =
        static_cast<std::size_t>(i)
        + static_cast<std::size_t>(j) * g.nx()
        + static_cast<std::size_t>(kk) * g.nx() * g.ny();
    const MaterialId id = cfg.material_field.ids[idx];
    if (id >= cfg.material_table.size())
        return std::numeric_limits<double>::quiet_NaN();
    return cfg.material_table[id].thermal_conductivity;
}

// Volumetric heat capacity rho * c (J/(m^3*K)) at (i, j, k). Returns
// NaN if either property is missing.
double rho_c_at(const TransientHeatConfig& cfg, int i, int j, int kk) {
    const auto& g = cfg.material_field.grid;
    const std::size_t idx =
        static_cast<std::size_t>(i)
        + static_cast<std::size_t>(j) * g.nx()
        + static_cast<std::size_t>(kk) * g.nx() * g.ny();
    const MaterialId id = cfg.material_field.ids[idx];
    if (id >= cfg.material_table.size())
        return std::numeric_limits<double>::quiet_NaN();
    const auto& m = cfg.material_table[id];
    return m.density * m.specific_heat;
}

double k_face(double k1, double k2) {
    if (k1 <= 0.0 || k2 <= 0.0) return 0.0;
    return 2.0 * k1 * k2 / (k1 + k2);
}

inline std::size_t lin(int i, int j, int k, int nx, int ny) {
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

}  // namespace

TransientHeatResult solve_transient_heat(const TransientHeatConfig& cfg) {
    using Sparse  = Eigen::SparseMatrix<double>;
    using Triplet = Eigen::Triplet<double>;
    TransientHeatResult out;

    const auto& g  = cfg.material_field.grid;
    const int    nx = g.nx(), ny = g.ny(), nz = g.nz();
    const double dx = g.dx(), dy = g.dy(), dz = g.dz();
    const std::size_t N =
        static_cast<std::size_t>(nx) * ny * nz;
    if (N == 0 || dx <= 0 || dy <= 0 || dz <= 0) {
        out.error = "solve_transient_heat: empty or degenerate grid";
        return out;
    }
    if (cfg.material_field.ids.size() != N) {
        out.error = "solve_transient_heat: material_field.ids size mismatch";
        return out;
    }
    if (cfg.material_table.empty()) {
        out.error = "solve_transient_heat: material_table is empty";
        return out;
    }
    if (cfg.dt_s <= 0.0 || cfg.steps <= 0) {
        out.error = "solve_transient_heat: dt_s and steps must be positive";
        return out;
    }

    const double Ax = dy * dz, Ay = dx * dz, Az = dx * dy;
    const double V  = dx * dy * dz;
    const double inv_dt = 1.0 / cfg.dt_s;

    // -- assemble K + diagonal M/dt -----------------------------------
    std::vector<Triplet> trips;
    trips.reserve(7 * N);
    Eigen::VectorXd b_const = Eigen::VectorXd::Zero(static_cast<Eigen::Index>(N));
    Eigen::VectorXd m_over_dt(static_cast<Eigen::Index>(N));

    const bool have_src =
        cfg.volumetric_source.size() == N;
    for (int kk = 0; kk < nz; ++kk) {
        for (int jj = 0; jj < ny; ++jj) {
            for (int ii = 0; ii < nx; ++ii) {
                const std::size_t row = lin(ii, jj, kk, nx, ny);
                double diag = 0.0;
                const double k_ijk = k_at(cfg, ii, jj, kk);
                const double rc    = rho_c_at(cfg, ii, jj, kk);
                if (std::isnan(k_ijk) || k_ijk <= 0.0 ||
                    std::isnan(rc) || rc <= 0.0) {
                    out.error =
                        "solve_transient_heat: missing thermal_conductivity, "
                        "density, or specific_heat for some material";
                    return out;
                }
                auto add_face = [&](int ni, int nj, int nk,
                                     double area, double dist) {
                    if (ni < 0 || nj < 0 || nk < 0 ||
                        ni >= nx || nj >= ny || nk >= nz) return;
                    const double kn = k_at(cfg, ni, nj, nk);
                    if (std::isnan(kn) || kn <= 0.0) return;
                    const double kf = k_face(k_ijk, kn);
                    const double c  = kf * area / dist;
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

                // M/dt contribution: rho*c*V/dt added to diagonal, and
                // the RHS gets M/dt * T^n at each step (assembled per-
                // step below).
                m_over_dt[static_cast<Eigen::Index>(row)] =
                    rc * V * inv_dt;
                diag += m_over_dt[static_cast<Eigen::Index>(row)];

                trips.emplace_back(static_cast<int>(row),
                                    static_cast<int>(row), diag);
                if (have_src) {
                    b_const[static_cast<Eigen::Index>(row)] +=
                        cfg.volumetric_source.data()[row] * V;
                }
            }
        }
    }

    // -- BCs identical to the steady solver ---------------------------
    std::vector<bool>   dirichlet_pinned(N, false);
    std::vector<double> dirichlet_value(N, 0.0);

    for (const auto& bc : cfg.bcs) {
        switch (bc.kind) {
            case BcKind::Dirichlet: {
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
                auto add_q = [&](int i, int j, int k, double area) {
                    const std::size_t row = lin(i, j, k, nx, ny);
                    b_const[static_cast<Eigen::Index>(row)] += bc.value * area;
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
                    case BcTarget::VoxelRange: break;
                }
                break;
            }
            case BcKind::Robin: {
                auto add_robin = [&](int i, int j, int k, double area) {
                    const std::size_t row = lin(i, j, k, nx, ny);
                    trips.emplace_back(static_cast<int>(row),
                                        static_cast<int>(row),
                                        bc.h * area);
                    b_const[static_cast<Eigen::Index>(row)] +=
                        bc.h * area * bc.u_ref;
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
                    case BcTarget::VoxelRange: break;
                }
                break;
            }
        }
    }

    // Build A = M/dt + K
    Sparse A(static_cast<int>(N), static_cast<int>(N));
    A.setFromTriplets(trips.begin(), trips.end());
    A.makeCompressed();

    // Symmetric Dirichlet pinning (same trick as the steady solver).
    // Note: this also has to live before the per-step iteration so the
    // factored matrix already encodes the pins. Each per-step b vector
    // then keeps the pinned-row RHS at dirichlet_value and the
    // un-pinned rows get -A_orig(j, r)*v moved into b once and re-used.
    // Because we factor A only once we apply the column-zeroing here
    // and accumulate the constant "Dirichlet contribution to b" into
    // b_dirichlet for later addition.
    Eigen::VectorXd b_dirichlet =
        Eigen::VectorXd::Zero(static_cast<Eigen::Index>(N));
    for (int col = 0; col < A.outerSize(); ++col) {
        for (Sparse::InnerIterator it(A, col); it; ++it) {
            const int r = it.row();
            const int c = it.col();
            if (dirichlet_pinned[c] && r != c) {
                b_dirichlet[r] -= it.value() * dirichlet_value[c];
                it.valueRef() = 0.0;
            }
        }
    }
    bool any_dirichlet = false;
    for (std::size_t r = 0; r < N; ++r) {
        if (dirichlet_pinned[r]) {
            any_dirichlet = true;
            for (int col = 0; col < A.outerSize(); ++col) {
                for (Sparse::InnerIterator it(A, col); it; ++it) {
                    if (static_cast<std::size_t>(it.row()) == r) {
                        it.valueRef() =
                            (it.row() == it.col()) ? 1.0 : 0.0;
                    }
                }
            }
            b_dirichlet[static_cast<Eigen::Index>(r)] = dirichlet_value[r];
        }
    }
    if (!any_dirichlet) {
        // Pure Neumann + Robin transient IS solvable (the mass matrix
        // anchors the constant mode), but only if Robin is present
        // somewhere to dissipate. Refuse the pathological case where
        // neither Dirichlet nor Robin exists -- T drifts to infinity.
        bool any_robin = false;
        for (const auto& bc : cfg.bcs)
            if (bc.kind == BcKind::Robin) { any_robin = true; break; }
        if (!any_robin) {
            out.error =
                "solve_transient_heat: no Dirichlet or Robin BC -- system "
                "has no way to dissipate, T grows unbounded";
            return out;
        }
    }
    A.prune(0.0);

    // -- factor A once ----------------------------------------------
    Eigen::SimplicialLLT<Sparse> solver;
    solver.compute(A);
    if (solver.info() != Eigen::Success) {
        out.error = "solve_transient_heat: Cholesky factorization failed";
        return out;
    }

    // -- initial condition ------------------------------------------
    Eigen::VectorXd T(static_cast<Eigen::Index>(N));
    if (cfg.initial_temperature.size() == N) {
        for (std::size_t r = 0; r < N; ++r) {
            T[static_cast<Eigen::Index>(r)] =
                cfg.initial_temperature.data()[r];
        }
    } else {
        T.setConstant(cfg.initial_temperature_uniform);
    }
    // Stamp the Dirichlet values into the initial state so the first
    // step doesn't start with an inconsistent boundary.
    for (std::size_t r = 0; r < N; ++r) {
        if (dirichlet_pinned[r])
            T[static_cast<Eigen::Index>(r)] = dirichlet_value[r];
    }

    // -- record initial state ----------------------------------------
    out.times.reserve(cfg.steps + 1);
    out.times.push_back(0.0);
    out.obs_history.assign(cfg.observation_points.size(), {});
    for (std::size_t p = 0; p < cfg.observation_points.size(); ++p) {
        const auto& q = cfg.observation_points[p];
        if (q.i >= 0 && q.j >= 0 && q.k >= 0 &&
            q.i < nx && q.j < ny && q.k < nz) {
            out.obs_history[p].push_back(
                T[static_cast<Eigen::Index>(lin(q.i, q.j, q.k, nx, ny))]);
        } else {
            out.obs_history[p].push_back(0.0);
        }
    }

    // -- time stepping ----------------------------------------------
    Eigen::VectorXd b(static_cast<Eigen::Index>(N));
    for (int step = 0; step < cfg.steps; ++step) {
        // b = M/dt .* T + b_const + b_dirichlet, then pin RHS rows.
        for (std::size_t r = 0; r < N; ++r) {
            b[static_cast<Eigen::Index>(r)] =
                m_over_dt[static_cast<Eigen::Index>(r)]
                * T[static_cast<Eigen::Index>(r)]
                + b_const[static_cast<Eigen::Index>(r)]
                + b_dirichlet[static_cast<Eigen::Index>(r)];
            if (dirichlet_pinned[r])
                b[static_cast<Eigen::Index>(r)] = dirichlet_value[r];
        }
        Eigen::VectorXd Tn = solver.solve(b);
        if (solver.info() != Eigen::Success) {
            out.error = "solve_transient_heat: solver back-substitution failed";
            return out;
        }
        T.swap(Tn);

        out.times.push_back((step + 1) * cfg.dt_s);
        for (std::size_t p = 0; p < cfg.observation_points.size(); ++p) {
            const auto& q = cfg.observation_points[p];
            if (q.i >= 0 && q.j >= 0 && q.k >= 0 &&
                q.i < nx && q.j < ny && q.k < nz) {
                out.obs_history[p].push_back(
                    T[static_cast<Eigen::Index>(lin(q.i, q.j, q.k, nx, ny))]);
            } else {
                out.obs_history[p].push_back(0.0);
            }
        }
    }

    out.final_temperature.resize(nx, ny, nz);
    for (std::size_t r = 0; r < N; ++r) {
        out.final_temperature.data()[r] = T[static_cast<Eigen::Index>(r)];
    }
    out.ok = true;
    return out;
}

}  // namespace mpkit
