#include "emi/BoardAnalysis.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace emikit::emi {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool name_matches_any(const std::string& net_name,
                        const std::vector<std::string>& patterns) {
    if (patterns.empty()) return true;
    const std::string n = lower(net_name);
    for (const auto& p : patterns) {
        if (n.find(lower(p)) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

std::vector<double> default_cispr_freq_grid(int n_points) {
    std::vector<double> g;
    g.reserve(n_points);
    const double lo = std::log10(30.0e6);
    const double hi = std::log10(1.0e9);
    for (int i = 0; i < n_points; ++i) {
        const double t = (n_points == 1) ? 0.5
                          : static_cast<double>(i) / (n_points - 1);
        g.push_back(std::pow(10.0, lo + t * (hi - lo)));
    }
    return g;
}

AnalysisResult analyze_board(
    const circuitcore::board::Board& board,
    const EmissionsMask& mask,
    const AnalysisConfig& config) {
    AnalysisResult R;
    const auto freqs = config.freq_hz.empty() ? default_cispr_freq_grid()
                                                : config.freq_hz;

    // Per-net length sum.
    std::unordered_map<int, double> length_by_net;
    std::unordered_map<int, int>    layer_by_net;
    for (const auto& s : board.segments) {
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        const double L  = std::sqrt(dx * dx + dy * dy);
        length_by_net[s.net_id] += L;
        layer_by_net.try_emplace(s.net_id, s.layer_ordinal);
    }

    // Per-frequency drive current envelope -- shared across all nets in v1.
    // Use the worst-case envelope (Montrose form) rather than the
    // exact harmonic magnitudes. Real EMI receivers see the envelope
    // -- their IF bandwidth catches multiple harmonics and real
    // signals have edge jitter that fills in the sinc nulls.
    const auto drive_a = envelope_sweep(config.drive, freqs);

    // Initialize worst-case envelope to -inf.
    R.worst_case_dbuv.assign(freqs.size(), -1000.0);
    std::string worst_overall_net;
    double worst_overall_value = -1000.0;

    for (const auto& [net_id, total_length] : length_by_net) {
        if (total_length <= 0.0) continue;
        const auto* net = board.find_net(net_id);
        const std::string name = net ? net->name : std::string{};
        if (!name_matches_any(name, config.net_filter)) continue;

        NetEmission ne;
        ne.net_id = net_id;
        ne.net_name = name;
        ne.layer_ordinal = layer_by_net[net_id];
        ne.total_length_m = total_length;
        ne.loop_area_m2 = total_length * config.loop_height_m;
        ne.e_dbuv = loop_e_field_dbuv_sweep(
            ne.loop_area_m2, freqs, drive_a, config.test_distance_m);

        for (std::size_t k = 0; k < freqs.size(); ++k) {
            if (ne.e_dbuv[k] > R.worst_case_dbuv[k]) {
                R.worst_case_dbuv[k] = ne.e_dbuv[k];
            }
            if (ne.e_dbuv[k] > worst_overall_value) {
                worst_overall_value = ne.e_dbuv[k];
                worst_overall_net   = name;
            }
        }
        R.nets.push_back(std::move(ne));
    }

    // Cables. Each cable carries a common-mode current derived from
    // the shared drive spectrum (or supplied explicitly) and radiates
    // independently of the loop. The two contributions are summed in
    // power per frequency.
    for (const auto& cable : config.cables) {
        if (cable.length_m <= 0.0) continue;
        CableEmission ce;
        ce.length_m = cable.length_m;
        ce.cm_current_a = estimate_cm_current(cable, drive_a);
        ce.e_dbuv.assign(freqs.size(), -1000.0);

        for (std::size_t k = 0; k < freqs.size(); ++k) {
            CableSpec instant = cable;
            instant.cm_current_a = ce.cm_current_a[k];
            const double e_v =
                cable_cm_e_field(instant, freqs[k], config.test_distance_m);
            if (e_v <= 0.0) continue;
            ce.e_dbuv[k] = 20.0 * std::log10(e_v * 1.0e6);

            // Power-sum into the worst-case envelope.
            const double loop_dbuv  = R.worst_case_dbuv[k];
            const double loop_v     = (loop_dbuv > -900.0)
                                      ? std::pow(10.0, loop_dbuv / 20.0)
                                      : 0.0;
            const double cable_v    = ce.e_dbuv[k] > -900.0
                                      ? std::pow(10.0, ce.e_dbuv[k] / 20.0)
                                      : 0.0;
            const double total_v    = std::sqrt(loop_v * loop_v +
                                                  cable_v * cable_v);
            R.worst_case_dbuv[k]    = 20.0 * std::log10(total_v);

            if (R.worst_case_dbuv[k] > worst_overall_value) {
                worst_overall_value = R.worst_case_dbuv[k];
                // Tag cables in the verdict with their length to
                // distinguish from nets.
                worst_overall_net = "<cable " +
                    std::to_string(static_cast<int>(cable.length_m * 100.0)) +
                    " cm>";
            }
        }
        R.cables.push_back(std::move(ce));
    }

    if (R.nets.empty() && R.cables.empty()) {
        // Nothing matched -- absence of routed nets is not a PASS.
        R.verdict.status = Verdict::Status::NoData;
        return R;
    }

    // Score vs mask: smallest margin across the worst-case envelope.
    double min_margin = std::numeric_limits<double>::infinity();
    double min_margin_freq = 0.0;
    for (std::size_t k = 0; k < freqs.size(); ++k) {
        const double m = margin_db(mask, freqs[k], R.worst_case_dbuv[k]);
        if (m < min_margin) { min_margin = m; min_margin_freq = freqs[k]; }
    }
    R.verdict.worst_freq_hz = min_margin_freq;
    R.verdict.worst_margin_db = min_margin;
    R.verdict.worst_net = worst_overall_net;
    R.verdict.status = (min_margin >= 0.0) ? Verdict::Status::Pass
                                              : Verdict::Status::Fail;
    return R;
}

}  // namespace emikit::emi
