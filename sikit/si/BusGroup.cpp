#include "si/BusGroup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <unordered_set>

#include "si/DiffPair.h"

namespace sikit::si {

namespace {

constexpr double kC0 = 2.99792458e8;

// Strip a trailing run of digits from the net name. Returns (base, index)
// where base is the prefix with the digits removed and index is the
// parsed integer. If there's no trailing digit, returns (name, -1).
std::pair<std::string, int> split_trailing_int(const std::string& name) {
    std::size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) {
        --i;
    }
    if (i == name.size()) return {name, -1};
    return {name.substr(0, i), std::stoi(name.substr(i))};
}

double sum_net_length(const circuitcore::board::Board& board, int net_id) {
    double L = 0.0;
    for (const auto& s : board.segments) {
        if (s.net_id != net_id) continue;
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        L += std::sqrt(dx * dx + dy * dy);
    }
    return L;
}

}  // namespace

std::vector<BusGroup> compute_bus_groups(
    const circuitcore::board::Board& board,
    const sikit::analysis::AnalysisStackup& stackup,
    double budget_ps) {
    // Step 1: detect diff pairs. Their _N halves get masked out so the
    // group walks them as a single member named after the P leg's base.
    const auto pairs = sikit::highspeed::find_diff_pairs(board);
    std::unordered_set<int> n_halves;
    std::map<int, int> p_to_n;       // net_p_id -> net_n_id
    std::map<int, std::string> p_base_name;
    for (const auto& dp : pairs) {
        n_halves.insert(dp.net_n_id);
        p_to_n[dp.net_p_id] = dp.net_n_id;
        p_base_name[dp.net_p_id] = dp.base_name;
    }

    // Step 2: walk nets, build BusMember entries (skipping N halves of
    // detected pairs). The "name for grouping" is the diff-pair base
    // name when the net is the P leg, else the raw net name.
    struct Candidate {
        std::string name_for_grouping;
        BusMember m;
    };
    std::vector<Candidate> all;
    all.reserve(board.nets.size());
    for (const auto& n : board.nets) {
        if (n.id <= 0) continue;
        if (n_halves.contains(n.id)) continue;
        Candidate c;
        const auto it_pair = p_to_n.find(n.id);
        if (it_pair != p_to_n.end()) {
            c.m.is_diff_pair = true;
            c.m.partner_net_id = it_pair->second;
            c.name_for_grouping = p_base_name[n.id];
        } else {
            c.name_for_grouping = n.name;
        }
        c.m.name = n.name;
        c.m.net_id = n.id;
        c.m.length_m = sum_net_length(board, n.id);
        all.push_back(std::move(c));
    }

    // Step 3: group by (base, after stripping trailing int from
    // name_for_grouping). Members with no trailing int are skipped --
    // they can't be a bus member.
    std::map<std::string, std::vector<BusMember>> by_base;
    for (auto& c : all) {
        auto [base, idx] = split_trailing_int(c.name_for_grouping);
        if (idx < 0) continue;
        c.m.index = idx;
        by_base[base].push_back(std::move(c.m));
    }

    // Step 4: emit BusGroup for every base with >= 2 members.
    // Phase velocity from compute_one on a representative segment. The
    // skew computation does not actually need width-specific v_phase --
    // we use one value across the bus and call it good.
    double v_phase = 0.0;
    try {
        const auto imp = sikit::analysis::compute_one(0.20e-3, 0, stackup);
        v_phase = imp.v_phase;
    } catch (...) {}
    if (v_phase <= 0.0) v_phase = kC0 / std::sqrt(std::max(stackup.epsilon_r, 1.0));

    std::vector<BusGroup> out;
    out.reserve(by_base.size());
    for (auto& [base, members] : by_base) {
        if (members.size() < 2) continue;
        std::sort(members.begin(), members.end(),
                   [](const BusMember& a, const BusMember& b) {
                       return a.index < b.index;
                   });
        BusGroup g;
        g.base_name = base;
        g.members = std::move(members);
        g.v_phase = v_phase;
        g.min_length_m = g.max_length_m = g.members.front().length_m;
        for (const auto& m : g.members) {
            g.min_length_m = std::min(g.min_length_m, m.length_m);
            g.max_length_m = std::max(g.max_length_m, m.length_m);
        }
        g.skew_m = g.max_length_m - g.min_length_m;
        g.skew_ps = (g.skew_m / v_phase) * 1e12;
        g.exceeds_budget = g.skew_ps > budget_ps;
        out.push_back(std::move(g));
    }
    return out;
}

}  // namespace sikit::si
