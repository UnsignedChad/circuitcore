#include "si/FdmSolver.h"
#include "circuitcore/field/GridSpec.h"

#include <algorithm>
#include <cmath>

namespace sikit::em2d {

namespace {

using circuitcore::field::kEps0;

// Whether the cell at centre (y, z) with side `h` intersects any conductor
// rectangle. Using cell–rect overlap rather than centre-in-rect catches
// thin conductors (trace thickness < h) which would otherwise straddle
// cell centres and produce no marked cells at all.
bool inside_any_conductor(const CrossSection& cs, double y, double z, double h,
                          int& out_id, double& out_v) {
    const double hh = 0.5 * h;
    for (const auto& c : cs.conductors) {
        const double y0 = c.y_center - 0.5 * c.width;
        const double y1 = c.y_center + 0.5 * c.width;
        const double z0 = c.z_top;
        const double z1 = c.z_top + c.thickness;
        if (y + hh > y0 && y - hh < y1 &&
            z + hh > z0 && z - hh < z1) {
            out_id = c.id;
            out_v = c.voltage;
            return true;
        }
    }
    return false;
}

// Face-averaged ε between two cells (harmonic mean — preserves flux through
// dielectric interfaces).
double face_eps(double e1, double e2) {
    if (e1 <= 0.0 || e2 <= 0.0) return 0.0;
    return 2.0 * e1 * e2 / (e1 + e2);
}

}  // namespace

FdmGrid build_grid(const CrossSection& cs, double cell_size_m) {
    FdmGrid g;
    g.h = cell_size_m;
    g.y_min = cs.y_min;
    g.z_min = -cs.air_above;
    const double y_span = cs.y_max - cs.y_min;
    const double z_span = cs.air_above + cs.board_thickness();
    g.ny = std::max(2, static_cast<int>(std::ceil(y_span / cell_size_m)));
    g.nz = std::max(2, static_cast<int>(std::ceil(z_span / cell_size_m)));

    const std::size_t N = static_cast<std::size_t>(g.ny) *
                          static_cast<std::size_t>(g.nz);
    g.V.assign(N, 0.0);
    g.epsilon_r.assign(N, 1.0);
    g.is_conductor.assign(N, 0u);
    g.conductor_id.assign(N, -1);

    for (int j = 0; j < g.nz; ++j) {
        const double zc = g.z_center(j);
        for (int i = 0; i < g.ny; ++i) {
            const double yc = g.y_center(i);
            const std::size_t k = g.idx(i, j);

            int cid = -1;
            double cv = 0.0;
            if (inside_any_conductor(cs, yc, zc, g.h, cid, cv)) {
                g.is_conductor[k] = 1u;
                g.conductor_id[k] = cid;
                g.V[k] = cv;
                g.epsilon_r[k] = 1.0;  // value doesn't matter inside metal
            } else {
                g.epsilon_r[k] = cs.epsilon_r_at(yc, zc);
            }
        }
    }
    return g;
}

SolveResult solve(FdmGrid& g, const SolveConfig& cfg) {
    SolveResult r;
    for (int iter = 0; iter < cfg.max_iterations; ++iter) {
        double max_delta = 0.0;
        // Interior cells only; boundary cells stay at 0 (Dirichlet). The
        // user supplies enough lateral / vertical air space in the cross-
        // section that fringing fields have decayed before reaching the
        // walls; the cross-section's defaults handle this for typical
        // microstrip / stripline geometries.
        for (int j = 1; j < g.nz - 1; ++j) {
            for (int i = 1; i < g.ny - 1; ++i) {
                const std::size_t k = g.idx(i, j);
                if (g.is_conductor[k]) continue;

                const double ec = g.epsilon_r[k];
                const double e_e = face_eps(ec, g.epsilon_r[g.idx(i + 1, j)]);
                const double e_w = face_eps(ec, g.epsilon_r[g.idx(i - 1, j)]);
                const double e_n = face_eps(ec, g.epsilon_r[g.idx(i, j + 1)]);
                const double e_s = face_eps(ec, g.epsilon_r[g.idx(i, j - 1)]);

                const double sum_e = e_e + e_w + e_n + e_s;
                if (sum_e <= 0.0) continue;

                const double v_new =
                    (e_e * g.V[g.idx(i + 1, j)] +
                     e_w * g.V[g.idx(i - 1, j)] +
                     e_n * g.V[g.idx(i, j + 1)] +
                     e_s * g.V[g.idx(i, j - 1)]) / sum_e;

                const double v_old = g.V[k];
                const double v_sor = v_old + cfg.omega * (v_new - v_old);
                g.V[k] = v_sor;

                const double d = std::abs(v_sor - v_old);
                if (d > max_delta) max_delta = d;
            }
        }
        r.iterations = iter + 1;
        r.final_residual = max_delta;
        if (max_delta < cfg.tolerance) {
            r.ok = true;
            return r;
        }
    }
    r.ok = false;  // hit max_iterations without converging to tolerance
    return r;
}

ImpedanceResult compute_z0(const CrossSection& cs,
                            int trace_id, int ground_id,
                            double cell_size_m,
                            const SolveConfig& cfg) {
    constexpr double kC0 = 2.99792458e8;  // speed of light, m/s

    ImpedanceResult r;

    // Solve 1: real dielectric stack.
    FdmGrid g = build_grid(cs, cell_size_m);
    auto s1 = solve(g, cfg);
    r.iter_dielectric = s1.iterations;
    if (!s1.ok) return r;
    r.c_per_m = charge_per_length(g, trace_id);

    // Solve 2: all-air variant.
    CrossSection cs_air = cs;
    for (auto& d : cs_air.stack) d.epsilon_r = 1.0;
    FdmGrid g_air = build_grid(cs_air, cell_size_m);
    auto s2 = solve(g_air, cfg);
    r.iter_air = s2.iterations;
    if (!s2.ok) return r;
    r.c_air_per_m = charge_per_length(g_air, trace_id);

    if (r.c_per_m <= 0.0 || r.c_air_per_m <= 0.0) return r;
    r.z0_ohm  = 1.0 / (kC0 * std::sqrt(r.c_per_m * r.c_air_per_m));
    r.eps_eff = r.c_per_m / r.c_air_per_m;
    r.v_phase = kC0 / std::sqrt(r.eps_eff);
    r.ok = true;
    (void)ground_id;  // GND is V=0 in the input; not needed beyond that
    return r;
}

RefinedImpedanceResult compute_z0_refined(const CrossSection& cs,
                                            int trace_id, int ground_id,
                                            double cell_size_m,
                                            const SolveConfig& cfg) {
    constexpr double kC0 = 2.99792458e8;
    RefinedImpedanceResult r;

    const auto fine   = compute_z0(cs, trace_id, ground_id, cell_size_m,        cfg);
    const auto coarse = compute_z0(cs, trace_id, ground_id, 2.0 * cell_size_m,  cfg);

    r.iter_fine   = fine.iter_dielectric + fine.iter_air;
    r.iter_coarse = coarse.iter_dielectric + coarse.iter_air;
    r.z0_fine     = fine.z0_ohm;
    r.z0_coarse   = coarse.z0_ohm;

    if (!fine.ok || !coarse.ok) {
        // Fall back to the fine result if available.
        if (fine.ok) {
            r.c_per_m     = fine.c_per_m;
            r.c_air_per_m = fine.c_air_per_m;
            r.z0_ohm      = fine.z0_ohm;
            r.eps_eff     = fine.eps_eff;
            r.v_phase     = fine.v_phase;
            r.ok = true;
        }
        return r;
    }

    // First-order Richardson: C_true ≈ 2·C(h) − C(2h). The cell-rect
    // classifier introduces a linear edge-fattening error that scales
    // with h; this linear combination cancels it. The C values
    // themselves get extrapolated; we recompute Z₀ from the extrapolated
    // C/C_air pair rather than averaging the Z₀ outputs (which would
    // mix lossy reciprocals).
    r.c_per_m     = 2.0 * fine.c_per_m     - coarse.c_per_m;
    r.c_air_per_m = 2.0 * fine.c_air_per_m - coarse.c_air_per_m;

    if (r.c_per_m > 0.0 && r.c_air_per_m > 0.0) {
        r.z0_ohm  = 1.0 / (kC0 * std::sqrt(r.c_per_m * r.c_air_per_m));
        r.eps_eff = r.c_per_m / r.c_air_per_m;
        r.v_phase = kC0 / std::sqrt(r.eps_eff);
        r.ok = true;
    } else {
        // Extrapolation went negative — fall back to the fine result.
        r.c_per_m     = fine.c_per_m;
        r.c_air_per_m = fine.c_air_per_m;
        r.z0_ohm      = fine.z0_ohm;
        r.eps_eff     = fine.eps_eff;
        r.v_phase     = fine.v_phase;
        r.ok = true;
    }
    return r;
}

double charge_per_length(const FdmGrid& g, int conductor_id) {
    // Apply Gauss's law on the conductor's boundary cells: for each
    // boundary face between a conductor cell and a non-conductor cell,
    // contribute -ε₀ · ε_r · (V_neighbour - V_conductor) (flux out per
    // unit length, since the cell extends by h in the through-page
    // direction and we want C/m). Sum over the conductor's perimeter.
    double q = 0.0;
    for (int j = 1; j < g.nz - 1; ++j) {
        for (int i = 1; i < g.ny - 1; ++i) {
            const std::size_t k = g.idx(i, j);
            if (!g.is_conductor[k] || g.conductor_id[k] != conductor_id) continue;
            const double v_c = g.V[k];

            auto contribute_face = [&](int ii, int jj) {
                const std::size_t kk = g.idx(ii, jj);
                if (g.is_conductor[kk]) return;  // facing another metal cell
                // Face-averaged ε on the metal/dielectric boundary.
                const double e_face = face_eps(g.epsilon_r[k], g.epsilon_r[kk]);
                // E field across the face: −(V_neighbour − V_metal)/h, outward.
                // Flux per unit length = ε · E · (face length) = ε · ΔV.
                q += kEps0 * e_face * (v_c - g.V[kk]);
            };
            contribute_face(i + 1, j);
            contribute_face(i - 1, j);
            contribute_face(i, j + 1);
            contribute_face(i, j - 1);
        }
    }
    return q;
}

}  // namespace sikit::em2d
