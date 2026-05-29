// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/Rlgc.h"

#include <cmath>

namespace sikit::em2d {

namespace {

constexpr double kC0 = 2.99792458e8;

// One column of the C matrix: excite signal `excite_id` at +1 V, hold every
// other signal at 0 V plus the ground reference at 0 V, FDM-solve, then
// read charge per unit length on each signal conductor.
bool extract_column(const CrossSection& cs_template,
                     const std::vector<int>& signal_ids,
                     int ground_id,
                     int excite_id,
                     double cell_size,
                     const SolveConfig& cfg,
                     Eigen::VectorXd& column) {
    CrossSection cs = cs_template;
    for (auto& c : cs.conductors) {
        if (c.id == ground_id)     c.voltage = 0.0;
        else if (c.id == excite_id) c.voltage = 1.0;
        else                        c.voltage = 0.0;
    }
    FdmGrid g = build_grid(cs, cell_size);
    auto sr = solve(g, cfg);
    if (!sr.ok) return false;
    column.resize(static_cast<int>(signal_ids.size()));
    for (std::size_t i = 0; i < signal_ids.size(); ++i) {
        column[static_cast<int>(i)] = charge_per_length(g, signal_ids[i]);
    }
    return true;
}

}  // namespace

RlgcMatrices compute_rlgc(const CrossSection& cs,
                           const std::vector<int>& signal_ids,
                           int ground_id,
                           double cell_size_m,
                           const SolveConfig& cfg) {
    RlgcMatrices r;
    r.n = static_cast<int>(signal_ids.size());
    if (r.n < 1) return r;
    r.C.resize(r.n, r.n);
    r.C_air.resize(r.n, r.n);

    // Real-dielectric pass.
    for (int k = 0; k < r.n; ++k) {
        Eigen::VectorXd col;
        if (!extract_column(cs, signal_ids, ground_id, signal_ids[k],
                            cell_size_m, cfg, col)) {
            return r;  // ok stays false
        }
        r.C.col(k) = col;
    }

    // All-air pass.
    CrossSection cs_air = cs;
    for (auto& d : cs_air.stack) d.epsilon_r = 1.0;
    for (int k = 0; k < r.n; ++k) {
        Eigen::VectorXd col;
        if (!extract_column(cs_air, signal_ids, ground_id, signal_ids[k],
                            cell_size_m, cfg, col)) {
            return r;
        }
        r.C_air.col(k) = col;
    }

    // L = (1/c²) · C_air⁻¹.  Symmetric-positive-definite in theory; we use
    // a general inverse for robustness against tiny asymmetry from
    // discretisation noise.
    const Eigen::MatrixXd C_air_inv = r.C_air.inverse();
    // .inverse() does not throw on a singular matrix -- it yields NaN/Inf.
    // Degenerate geometry (coincident conductors) must fail loudly, not
    // hand back a garbage L flagged ok.
    if (!C_air_inv.allFinite()) {
        r.ok = false;
        return r;
    }
    r.L = (1.0 / (kC0 * kC0)) * C_air_inv;
    r.ok = true;
    return r;
}

CrosstalkCoefficients crosstalk_for_pair(const RlgcMatrices& rlgc,
                                          int aggressor, int victim) {
    CrosstalkCoefficients ct;
    if (!rlgc.ok || aggressor == victim) return ct;
    if (aggressor < 0 || aggressor >= rlgc.n) return ct;
    if (victim    < 0 || victim    >= rlgc.n) return ct;

    const double C_self = rlgc.C(aggressor, aggressor);
    const double L_self = rlgc.L(aggressor, aggressor);
    // C_mutual is positive (negate the negative off-diagonal Maxwell entry).
    const double C_m    = -rlgc.C(victim, aggressor);
    const double L_m    =  rlgc.L(victim, aggressor);

    if (C_self <= 0.0 || L_self <= 0.0) return ct;

    // Near-end (backward) coupling is the SUM of the normalized mutual
    // L and C; the difference is the modal/FEXT term, kept separately.
    ct.modal_mismatch = L_m / L_self - C_m / C_self;
    ct.k_near_end     = 0.25 * (L_m / L_self + C_m / C_self);
    return ct;
}

}  // namespace sikit::em2d
