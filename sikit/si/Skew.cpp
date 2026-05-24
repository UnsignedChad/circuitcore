#include "si/Skew.h"

#include <cmath>
#include <map>
#include <vector>

#include "si/DiffPair.h"

namespace sikit::si {

namespace {

constexpr double kC0 = 2.99792458e8;

// Sum segment lengths grouped by (net_id, layer). Used to pick the
// majority layer per pair when the routes have transitioned between
// layers via blind / through-hole vias.
struct LegInfo {
    std::map<int, double> length_by_layer;  // layer_ordinal -> total m
    double total_length() const {
        double t = 0.0;
        for (const auto& [_, L] : length_by_layer) t += L;
        return t;
    }
    int majority_layer() const {
        int best = -1;
        double best_len = -1.0;
        for (const auto& [layer, L] : length_by_layer) {
            if (L > best_len) { best = layer; best_len = L; }
        }
        return best;
    }
};

LegInfo walk_leg(const circuitcore::board::Board& board, int net_id) {
    LegInfo out;
    for (const auto& s : board.segments) {
        if (s.net_id != net_id) continue;
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        const double L = std::sqrt(dx * dx + dy * dy);
        out.length_by_layer[s.layer_ordinal] += L;
    }
    return out;
}

}  // namespace

std::vector<DiffPairSkew> compute_diff_pair_skews(
    const circuitcore::board::Board& board,
    const sikit::analysis::AnalysisStackup& stackup,
    double budget_ps) {
    const auto pairs = sikit::highspeed::find_diff_pairs(board);
    std::vector<DiffPairSkew> out;
    out.reserve(pairs.size());

    for (const auto& dp : pairs) {
        const auto leg_p = walk_leg(board, dp.net_p_id);
        const auto leg_n = walk_leg(board, dp.net_n_id);
        const double Lp = leg_p.total_length();
        const double Ln = leg_n.total_length();
        if (Lp == 0.0 && Ln == 0.0) continue;

        DiffPairSkew r;
        r.base_name = dp.base_name;
        r.net_p_id = dp.net_p_id;
        r.net_n_id = dp.net_n_id;
        r.length_p_m = Lp;
        r.length_n_m = Ln;
        r.skew_m = Lp - Ln;

        // Pick the majority layer across both legs to read v_phase from.
        // Compute eps_eff on whichever leg has more length there.
        const int layer_p = leg_p.majority_layer();
        const int layer_n = leg_n.majority_layer();
        r.layer_ordinal = (Lp >= Ln) ? layer_p : layer_n;
        if (r.layer_ordinal < 0) r.layer_ordinal = 0;

        // Use compute_one on a representative segment width (any width
        // gives the same v_phase for a given stackup/layer; pick the
        // first segment width we find on that layer).
        double trace_w = 0.0;
        for (const auto& s : board.segments) {
            if (s.net_id != dp.net_p_id && s.net_id != dp.net_n_id) continue;
            if (s.layer_ordinal != r.layer_ordinal) continue;
            trace_w = s.width;
            break;
        }
        if (trace_w <= 0.0) trace_w = 0.20e-3;   // sensible default

        try {
            const auto imp = sikit::analysis::compute_one(
                trace_w, r.layer_ordinal, stackup);
            r.v_phase = imp.v_phase > 0.0 ? imp.v_phase
                                            : kC0 / std::sqrt(stackup.epsilon_r);
        } catch (...) {
            r.v_phase = kC0 / std::sqrt(stackup.epsilon_r);
        }
        if (r.v_phase <= 0.0) r.v_phase = kC0 / std::sqrt(stackup.epsilon_r);

        r.skew_ps = (r.skew_m / r.v_phase) * 1e12;
        r.exceeds_budget = std::abs(r.skew_ps) > budget_ps;
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace sikit::si
