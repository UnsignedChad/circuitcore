#include "si/TraceImpedance.h"

#include "si/SiStackup.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>

#include "si/CrossSection.h"
#include "si/FdmSolver.h"
#include "si/DiffPair.h"
#include "si/Impedance.h"

namespace sikit::analysis {

namespace {

bool is_outer_copper(int ord) {
    return ord == 0 || ord == 31;
}

double first_copper_thickness(const sikit::si::SiStackup& s) {
    for (const auto& it : s.items) {
        if (it.kind == sikit::si::SiStackupItem::Kind::Copper && it.thickness > 0.0) {
            return it.thickness;
        }
    }
    return -1.0;
}

// Build the synthetic cross-section the FDM solver evaluates for a single
// trace on the given layer. Outer copper → microstrip-style (trace above
// dielectric, ground plane at the other side). Inner copper → stripline
// (trace at midplane, planes above and below).
em2d::CrossSection make_xsection(double trace_width, int layer_ordinal,
                                   const AnalysisStackup& s) {
    em2d::CrossSection cs;
    cs.air_above = 5.0 * s.outer_dielectric_height;

    // Pick a horizontal box that's wide enough for the field to decay
    // before hitting the side wall.
    const double half_box = std::max(5e-3, 5.0 * trace_width);
    cs.y_min = -half_box;
    cs.y_max =  half_box;

    em2d::Conductor trace;
    trace.id = 0;
    trace.y_center = 0;
    trace.width = trace_width;
    trace.thickness = s.copper_thickness;
    trace.voltage = 1.0;

    em2d::Conductor gnd;
    gnd.id = 1;
    gnd.y_center = 0;
    gnd.width = 2.0 * half_box;
    gnd.thickness = std::max(s.copper_thickness, 1e-4);
    gnd.voltage = 0.0;

    if (is_outer_copper(layer_ordinal)) {
        cs.stack.push_back({s.outer_dielectric_height, s.epsilon_r, 0.0, ""});
        trace.z_top = -s.copper_thickness;     // sits in the air above stack
        gnd.z_top   = s.outer_dielectric_height; // just below board
    } else {
        // Stripline: two half-dielectrics, trace at the middle.
        const double half = 0.5 * s.inner_plane_separation;
        cs.stack.push_back({half, s.epsilon_r, 0.0, ""});
        cs.stack.push_back({half, s.epsilon_r, 0.0, ""});
        trace.z_top = half - 0.5 * s.copper_thickness;
        // For stripline we need TWO ground planes — represent the upper one
        // as another conductor at the top of the stack and the lower one
        // at the bottom.
        gnd.z_top = s.inner_plane_separation;
        em2d::Conductor top_gnd = gnd;
        top_gnd.id = 1;  // share an id with the bottom plane (same net)
        top_gnd.z_top = -gnd.thickness;
        cs.conductors.push_back(top_gnd);
    }

    cs.conductors.push_back(trace);
    cs.conductors.push_back(gnd);
    return cs;
}

// Pick a sensible FDM cell size for a given trace width. Aim for ~25
// cells across the trace, clamped to a range that keeps grids workable.
// We don't shrink h to the copper thickness: the build_grid classifier
// uses cell-rect overlap, so a single-cell-thick trace still gets marked.
double cell_size_for(double trace_width, double /*copper_thickness*/) {
    return std::clamp(trace_width / 25.0, 20e-6, 200e-6);
}

}  // namespace

AnalysisStackup AnalysisStackup::from_board(const circuitcore::board::Board& b,
                                              const sikit::si::SiStackup& sis) {
    AnalysisStackup s;

    const sikit::si::SiStackupItem* outer_d = nullptr;
    if (const auto* d = sis.adjacent_dielectric("F.Cu", +1)) outer_d = d;
    else if (const auto* d = sis.adjacent_dielectric("B.Cu", -1)) outer_d = d;
    else outer_d = sis.any_dielectric();

    if (outer_d) {
        if (outer_d->thickness > 0.0) s.outer_dielectric_height = outer_d->thickness;
        if (outer_d->epsilon_r > 0.0) s.epsilon_r = outer_d->epsilon_r;
        if (outer_d->loss_tangent > 0.0) s.tan_delta = outer_d->loss_tangent;
        s.from_real_stackup = true;
    }

    for (const auto& L : b.stackup.layers) {
        if (!L.is_copper() || is_outer_copper(L.ordinal)) continue;
        const auto* above = sis.adjacent_dielectric(L.name, -1);
        const auto* below = sis.adjacent_dielectric(L.name, +1);
        double sep = 0.0;
        if (above && above->thickness > 0.0) sep += above->thickness;
        if (below && below->thickness > 0.0) sep += below->thickness;
        if (sep > 0.0) {
            s.inner_plane_separation = sep;
            s.from_real_stackup = true;
        }
        break;
    }

    const double cu_t = first_copper_thickness(sis);
    if (cu_t > 0.0) {
        s.copper_thickness = cu_t;
        s.from_real_stackup = true;
    }
    return s;
}

SegmentImpedance compute_one(double trace_width, int layer_ordinal,
                              const AnalysisStackup& s) {
    constexpr double kC0 = 2.99792458e8;
    SegmentImpedance r;
    r.layer_ordinal = layer_ordinal;
    r.trace_width = trace_width;
    if (trace_width <= 0.0) {
        r.in_valid_range = false;
        return r;
    }
    if (is_outer_copper(layer_ordinal)) {
        impedance::MicrostripParams mp{
            .trace_width = trace_width,
            .dielectric_height = s.outer_dielectric_height,
            .trace_thickness = s.copper_thickness,
            .epsilon_r = s.epsilon_r,
        };
        r.z0 = impedance::microstrip_z0(mp);
        r.in_valid_range = impedance::microstrip_in_valid_range(mp);
        // Hammerstad effective permittivity for microstrip.
        const double wh = trace_width / s.outer_dielectric_height;
        const double er = s.epsilon_r;
        r.eps_eff = 0.5 * (er + 1.0) +
                    0.5 * (er - 1.0) / std::sqrt(1.0 + 12.0 / wh);
        r.v_phase = kC0 / std::sqrt(r.eps_eff);
    } else {
        impedance::StriplineParams sp{
            .trace_width = trace_width,
            .plane_separation = s.inner_plane_separation,
            .trace_thickness = s.copper_thickness,
            .epsilon_r = s.epsilon_r,
        };
        r.z0 = impedance::stripline_z0(sp);
        r.in_valid_range = impedance::stripline_in_valid_range(sp);
        // Stripline trace is fully embedded → eps_eff = eps_r.
        r.eps_eff = s.epsilon_r;
        r.v_phase = kC0 / std::sqrt(r.eps_eff);
    }
    return r;
}

namespace {

em2d::CrossSection make_diff_xsection(double trace_width, double spacing,
                                       int layer_ordinal,
                                       const AnalysisStackup& s) {
    em2d::CrossSection cs;
    cs.air_above = 5.0 * s.outer_dielectric_height;

    const double half_box = std::max(8e-3, 3.0 * (2.0 * trace_width + spacing));
    cs.y_min = -half_box;
    cs.y_max =  half_box;

    const double offset = 0.5 * (trace_width + spacing);

    em2d::Conductor trace_p, trace_n, gnd;
    trace_p.id = 0;       trace_p.y_center = -offset;
    trace_n.id = 1;       trace_n.y_center = +offset;
    gnd.id     = 2;       gnd.y_center = 0;
    trace_p.width = trace_n.width = trace_width;
    trace_p.thickness = trace_n.thickness = s.copper_thickness;
    trace_p.voltage =  0.5;   // V_p − V_n = 1 → C extracted = C_odd
    trace_n.voltage = -0.5;
    gnd.voltage = 0.0;

    if (is_outer_copper(layer_ordinal)) {
        cs.stack.push_back({s.outer_dielectric_height, s.epsilon_r, 0.0, ""});
        trace_p.z_top = -s.copper_thickness;
        trace_n.z_top = -s.copper_thickness;
        gnd.z_top     =  s.outer_dielectric_height;
        gnd.width = 2.0 * half_box;
        gnd.thickness = std::max(s.copper_thickness, 1e-4);
    } else {
        const double half = 0.5 * s.inner_plane_separation;
        cs.stack.push_back({half, s.epsilon_r, 0.0, ""});
        cs.stack.push_back({half, s.epsilon_r, 0.0, ""});
        trace_p.z_top = half - 0.5 * s.copper_thickness;
        trace_n.z_top = half - 0.5 * s.copper_thickness;
        gnd.z_top  = s.inner_plane_separation;
        gnd.width  = 2.0 * half_box;
        gnd.thickness = std::max(s.copper_thickness, 1e-4);
        em2d::Conductor top_gnd = gnd;
        top_gnd.id    = 2;
        top_gnd.z_top = -gnd.thickness;
        cs.conductors.push_back(top_gnd);
    }
    cs.conductors.push_back(trace_p);
    cs.conductors.push_back(trace_n);
    cs.conductors.push_back(gnd);
    return cs;
}

}  // namespace

double compute_diff_z0_fdm(double trace_width, double spacing,
                            int layer_ordinal,
                            const AnalysisStackup& s) {
    if (trace_width <= 0.0 || spacing < 0.0) return 0.0;
    constexpr double kC0 = 2.99792458e8;

    auto cs = make_diff_xsection(trace_width, spacing, layer_ordinal, s);
    em2d::SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 100000;
    const double h = cell_size_for(trace_width, s.copper_thickness);

    em2d::FdmGrid g = em2d::build_grid(cs, h);
    auto r1 = em2d::solve(g, cfg);
    if (!r1.ok) return 0.0;
    const double q = em2d::charge_per_length(g, /*trace_p=*/0);
    if (q <= 0.0) return 0.0;

    auto cs_air = cs;
    for (auto& d : cs_air.stack) d.epsilon_r = 1.0;
    em2d::FdmGrid g_air = em2d::build_grid(cs_air, h);
    auto r2 = em2d::solve(g_air, cfg);
    if (!r2.ok) return 0.0;
    const double q_air = em2d::charge_per_length(g_air, /*trace_p=*/0);
    if (q_air <= 0.0) return 0.0;

    // Excitation is V_p = +0.5, V_n = −0.5 (V_diff = 1). For pure odd mode
    // the per-conductor odd-mode capacitance is C_odd = Q / (V_p) = 2·Q.
    // Z_odd = 1 / (c · √(C_odd · C_odd_air)) = 1 / (2 c · √(q · q_air)).
    // Z_diff = 2 · Z_odd = 1 / (c · √(q · q_air)).
    return 1.0 / (kC0 * std::sqrt(q * q_air));
}

SegmentImpedance compute_one_fdm(double trace_width, int layer_ordinal,
                                  const AnalysisStackup& s) {
    SegmentImpedance r;
    r.layer_ordinal = layer_ordinal;
    r.trace_width = trace_width;
    if (trace_width <= 0.0) {
        r.in_valid_range = false;
        return r;
    }
    auto cs = make_xsection(trace_width, layer_ordinal, s);
    em2d::SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 100000;
    const double h = cell_size_for(trace_width, s.copper_thickness);
    auto out = em2d::compute_z0(cs, 0, 1, h, cfg);
    if (!out.ok) {
        r.in_valid_range = false;
        return r;
    }
    r.z0 = out.z0_ohm;
    r.eps_eff = out.eps_eff;
    r.v_phase = out.v_phase;
    return r;
}

std::vector<SegmentImpedance> compute_all(const circuitcore::board::Board& board,
                                           const AnalysisStackup& s,
                                           Engine engine) {
    std::vector<SegmentImpedance> out;
    out.reserve(board.segments.size());

    // FDM cache: each unique (width, layer) combo solved once. Keyed by a
    // string for portability; the precision is way more than enough to
    // distinguish realistic trace widths.
    std::unordered_map<std::string, SegmentImpedance> fdm_cache;
    auto fdm_key = [](double w, int L) {
        return std::format("{:.9f}|{}", w, L);
    };

    for (std::size_t i = 0; i < board.segments.size(); ++i) {
        const auto& seg = board.segments[i];
        const auto* L = board.find_layer(seg.layer_ordinal);
        if (!L || !L->is_copper()) continue;

        SegmentImpedance r;
        if (engine == Engine::Fdm) {
            const auto k = fdm_key(seg.width, seg.layer_ordinal);
            auto it = fdm_cache.find(k);
            if (it == fdm_cache.end()) {
                r = compute_one_fdm(seg.width, seg.layer_ordinal, s);
                fdm_cache.emplace(k, r);
            } else {
                r = it->second;
            }
        } else {
            r = compute_one(seg.width, seg.layer_ordinal, s);
        }
        r.segment_index = i;
        r.net_id = seg.net_id;
        out.push_back(r);
    }
    return out;
}

double compute_diff_z0_closed_form(double trace_width, double spacing,
                                    int layer_ordinal,
                                    const AnalysisStackup& s) {
    if (trace_width <= 0.0 || spacing < 0.0) return 0.0;
    auto se = compute_one(trace_width, layer_ordinal, s);
    if (se.z0 <= 0.0) return 0.0;
    if (is_outer_copper(layer_ordinal)) {
        return impedance::edge_coupled_microstrip_diff(
            se.z0, spacing, s.outer_dielectric_height);
    }
    return impedance::edge_coupled_stripline_diff(
        se.z0, spacing, s.inner_plane_separation);
}

namespace {

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Estimate edge-to-edge spacing between two nets that are routed as a
// diff pair. For each cross-net segment pair on `layer_ordinal` that is
// (a) approximately parallel and (b) has its midpoints projecting onto
// the other segment (so they're actually adjacent, not separated end-
// to-end), compute the perpendicular centre-to-centre distance, then
// subtract the average trace width to get the gap. Returns the median
// of valid pair gaps, or -1 if no valid pair is found.
double estimate_diff_pair_spacing(const circuitcore::board::Board& b,
                                    int net_p_id, int net_n_id,
                                    int layer_ordinal) {
    constexpr double kParallelDot = 0.95;
    std::vector<double> gaps;
    for (const auto& sp : b.segments) {
        if (sp.net_id != net_p_id || sp.layer_ordinal != layer_ordinal) continue;
        const double pdx = sp.end.x - sp.start.x;
        const double pdy = sp.end.y - sp.start.y;
        const double plen = std::sqrt(pdx * pdx + pdy * pdy);
        if (plen <= 0.0) continue;
        const double pux = pdx / plen;
        const double puy = pdy / plen;
        const double pmx = 0.5 * (sp.start.x + sp.end.x);
        const double pmy = 0.5 * (sp.start.y + sp.end.y);

        for (const auto& sn : b.segments) {
            if (sn.net_id != net_n_id || sn.layer_ordinal != layer_ordinal) continue;
            const double ndx = sn.end.x - sn.start.x;
            const double ndy = sn.end.y - sn.start.y;
            const double nlen = std::sqrt(ndx * ndx + ndy * ndy);
            if (nlen <= 0.0) continue;
            const double nux = ndx / nlen;
            const double nuy = ndy / nlen;

            // Parallel (or anti-parallel) check.
            if (std::abs(pux * nux + puy * nuy) < kParallelDot) continue;

            const double nmx = 0.5 * (sn.start.x + sn.end.x);
            const double nmy = 0.5 * (sn.start.y + sn.end.y);

            // Project sn's midpoint onto sp's line to make sure they
            // genuinely overlap rather than just point in the same
            // direction. Param t along sp.
            const double t_p = ((nmx - sp.start.x) * pux +
                                (nmy - sp.start.y) * puy) / plen;
            if (t_p < 0.0 || t_p > 1.0) continue;

            // Perpendicular distance from sn's midpoint to sp's line.
            const double dx = nmx - sp.start.x;
            const double dy = nmy - sp.start.y;
            const double cross = dx * puy - dy * pux;
            const double centre_dist = std::abs(cross);

            // Edge-to-edge gap = centre_dist − ½(W_p + W_n).
            const double gap = centre_dist - 0.5 * (sp.width + sn.width);
            if (gap > 0.0) gaps.push_back(gap);
        }
    }
    if (gaps.empty()) return -1.0;
    std::sort(gaps.begin(), gaps.end());
    return gaps[gaps.size() / 2];
}

}  // namespace

std::vector<DiffPairImpedance> compute_diff_pairs(
    const circuitcore::board::Board& board, const AnalysisStackup& s, Engine engine) {

    std::vector<DiffPairImpedance> out;
    auto pairs = sikit::highspeed::find_diff_pairs(board);
    for (const auto& dp : pairs) {
        DiffPairImpedance r;
        r.net_p_id = dp.net_p_id;
        r.net_n_id = dp.net_n_id;
        r.base_name = dp.base_name;

        // Collect widths and segment indices on F.Cu for either net.
        std::vector<double> widths;
        for (std::size_t i = 0; i < board.segments.size(); ++i) {
            const auto& seg = board.segments[i];
            if (seg.net_id != dp.net_p_id && seg.net_id != dp.net_n_id) continue;
            if (seg.layer_ordinal != 0) continue;  // F.Cu only for v0
            if (seg.width > 0.0) widths.push_back(seg.width);
            r.segment_indices.push_back(i);
        }
        if (widths.empty()) {
            r.z_diff = 0.0;
            out.push_back(std::move(r));
            continue;
        }
        r.trace_width = median(widths);
        r.layer_ordinal = 0;
        // Try to recover the actual routed spacing from the segment
        // geometry; fall back to S = W if the heuristic can't find a
        // parallel-overlap pair (often happens for routed-as-couple
        // diff pairs that haven't been laid out yet).
        const double geom_gap =
            estimate_diff_pair_spacing(board, dp.net_p_id, dp.net_n_id, 0);
        r.spacing = (geom_gap > 0.0) ? geom_gap : r.trace_width;

        if (engine == Engine::Fdm) {
            r.z_diff = compute_diff_z0_fdm(r.trace_width, r.spacing,
                                            r.layer_ordinal, s);
        } else {
            r.z_diff = compute_diff_z0_closed_form(r.trace_width, r.spacing,
                                                    r.layer_ordinal, s);
        }
        out.push_back(std::move(r));
    }
    return out;
}

ImpedanceColor color_for_error(double z0_ohms, double target_ohms) {
    if (target_ohms <= 0.0 || z0_ohms <= 0.0) {
        return {0.5f, 0.5f, 0.5f, 0.8f};
    }
    const double err = std::abs(z0_ohms - target_ohms) / target_ohms;
    if (err < 0.05) return {0.25f, 0.85f, 0.30f, 0.85f};
    if (err < 0.10) return {0.95f, 0.85f, 0.20f, 0.85f};
    return                  {0.92f, 0.30f, 0.25f, 0.85f};
}

}  // namespace sikit::analysis
