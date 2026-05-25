#include "si/DiffPair.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sikit::highspeed {

namespace {

struct SuffixPair {
    std::string_view pos;
    std::string_view neg;
    std::string_view style;
};

// Order matters: prefer longer / more specific suffixes first so that
// "_POS/_NEG" wins over "+/-" on the same name.
constexpr std::array<SuffixPair, 8> kSuffixes{{
    {"_POS", "_NEG", "_POS/_NEG"},
    {"_pos", "_neg", "_pos/_neg"},
    {"_DP",  "_DM",  "_DP/_DM"},
    {"_P",   "_N",   "_P/_N"},
    {"_p",   "_n",   "_p/_n"},
    {"DP",   "DM",   "DP/DM"},
    {"+",    "-",    "+/-"},
    {"P",    "N",    "P/N"},
}};

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

// High-speed keyword list. Substring match (case-insensitive) against the
// lowercased net name. These cover the protocols you'll actually encounter
// on a typical multi-rate digital board; extend as needed.
constexpr std::array<std::string_view, 14> kHighSpeedKeywords{{
    "usb", "pcie", "ddr", "hdmi", "displayport",
    "mipi", "serdes", "sata", "lvds", "ethernet",
    "sgmii", "rgmii", "rxd", "txd",
}};

}  // namespace

bool looks_high_speed(std::string_view net_name) {
    const std::string lower = to_lower(net_name);
    for (const auto& kw : kHighSpeedKeywords) {
        if (lower.find(kw) != std::string_view::npos) return true;
    }
    return false;
}

std::vector<DiffPair> find_diff_pairs(const circuitcore::board::Board& board) {
    // Index: net name → id.
    std::unordered_map<std::string, int> name_to_id;
    name_to_id.reserve(board.nets.size());
    for (const auto& n : board.nets) {
        if (!n.name.empty()) name_to_id.emplace(n.name, n.id);
    }

    std::vector<DiffPair> pairs;
    std::unordered_set<int> already_paired;

    for (const auto& net : board.nets) {
        if (net.name.empty()) continue;
        if (already_paired.count(net.id)) continue;

        for (const auto& sfx : kSuffixes) {
            if (!ends_with(net.name, sfx.pos)) continue;

            const std::string base =
                net.name.substr(0, net.name.size() - sfx.pos.size());
            const std::string counterpart = base + std::string(sfx.neg);

            auto it = name_to_id.find(counterpart);
            if (it == name_to_id.end()) continue;
            if (it->second == net.id) continue;  // self-match (shouldn't happen)

            // Avoid double-counting if we already grouped the counterpart.
            if (already_paired.count(it->second)) continue;

            DiffPair dp;
            dp.net_p_id = net.id;
            dp.net_n_id = it->second;
            dp.base_name = base;
            dp.suffix_style = std::string(sfx.style);
            pairs.push_back(std::move(dp));
            already_paired.insert(net.id);
            already_paired.insert(it->second);
            break;  // matched one suffix style; stop trying others
        }
    }

    return pairs;
}

std::vector<int> find_high_speed_nets(const circuitcore::board::Board& board) {
    std::unordered_set<int> hs;

    // Every net that's part of a diff pair counts.
    for (const auto& dp : find_diff_pairs(board)) {
        hs.insert(dp.net_p_id);
        hs.insert(dp.net_n_id);
    }
    // Plus nets whose name contains a high-speed keyword.
    for (const auto& n : board.nets) {
        if (looks_high_speed(n.name)) hs.insert(n.id);
    }

    return std::vector<int>(hs.begin(), hs.end());
}

}  // namespace sikit::highspeed
