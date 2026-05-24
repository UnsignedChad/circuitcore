#include "si/Report.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

#include "si/TraceImpedance.h"

namespace sikit::report {

namespace {

// HTML-escape a string. Used for net names and the board path -- both
// can theoretically contain ampersand / angle-bracket characters that
// would otherwise break the markup.
std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

double sum_net_length(const circuitcore::board::Board& b, int net_id,
                       int layer_ordinal) {
    double L = 0.0;
    for (const auto& s : b.segments) {
        if (s.net_id != net_id) continue;
        if (s.layer_ordinal != layer_ordinal) continue;
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        L += std::sqrt(dx * dx + dy * dy);
    }
    return L;
}

double median_net_width(const circuitcore::board::Board& b, int net_id,
                         int layer_ordinal) {
    std::vector<double> widths;
    for (const auto& s : b.segments) {
        if (s.net_id != net_id) continue;
        if (s.layer_ordinal != layer_ordinal) continue;
        widths.push_back(s.width);
    }
    if (widths.empty()) return 0.0;
    std::sort(widths.begin(), widths.end());
    return widths[widths.size() / 2];
}

// Pick the layer where the net has the most copper length. Returns -1
// if the net has no segments at all.
int net_majority_layer(const circuitcore::board::Board& b, int net_id) {
    std::vector<std::pair<int, double>> by_layer;
    for (const auto& s : b.segments) {
        if (s.net_id != net_id) continue;
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        const double L = std::sqrt(dx * dx + dy * dy);
        bool found = false;
        for (auto& [lyr, total] : by_layer) {
            if (lyr == s.layer_ordinal) { total += L; found = true; break; }
        }
        if (!found) by_layer.emplace_back(s.layer_ordinal, L);
    }
    if (by_layer.empty()) return -1;
    int best = by_layer.front().first;
    double best_L = by_layer.front().second;
    for (const auto& [lyr, total] : by_layer) {
        if (total > best_L) { best = lyr; best_L = total; }
    }
    return best;
}

}  // namespace

BoardReport build_board_report(
    const circuitcore::board::Board& board,
    const sikit::si::SiStackup& sis,
    double diff_skew_budget_ps,
    double bus_skew_budget_ps) {
    BoardReport r;
    r.net_count = static_cast<int>(board.nets.size());
    r.segment_count = static_cast<int>(board.segments.size());
    r.via_count = static_cast<int>(board.vias.size());
    for (const auto& L : board.stackup.layers) {
        if (L.is_copper()) ++r.copper_layer_count;
    }

    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);

    for (const auto& n : board.nets) {
        if (n.id <= 0) continue;
        NetSummary ns;
        ns.net_name = n.name;
        ns.net_id = n.id;
        ns.layer_ordinal = net_majority_layer(board, n.id);
        if (ns.layer_ordinal < 0) continue;
        ns.trace_width_m = median_net_width(board, n.id, ns.layer_ordinal);
        ns.total_length_m = sum_net_length(board, n.id, ns.layer_ordinal);
        if (ns.trace_width_m <= 0.0 || ns.total_length_m <= 0.0) {
            r.nets.push_back(ns);
            continue;
        }
        try {
            const auto imp = sikit::analysis::compute_one(
                ns.trace_width_m, ns.layer_ordinal, stackup);
            ns.z0_ohm = imp.z0;
            ns.v_phase = imp.v_phase;
            ns.eps_eff = imp.eps_eff;
            ns.valid = true;
        } catch (...) {
            // Geometry the closed-form solver can't handle; leave valid=false.
        }
        r.nets.push_back(ns);
    }

    r.diff_pairs = sikit::si::compute_diff_pair_skews(
        board, stackup, diff_skew_budget_ps);
    r.buses = sikit::si::compute_bus_groups(
        board, stackup, bus_skew_budget_ps);
    r.return_path_violations =
        sikit::si::detect_return_path_violations(board);

    for (const auto& p : r.diff_pairs)
        if (p.exceeds_budget) ++r.diff_pair_fails;
    for (const auto& b : r.buses)
        if (b.exceeds_budget) ++r.bus_fails;
    r.return_path_fails =
        static_cast<int>(r.return_path_violations.size());
    return r;
}

std::string render_html(const BoardReport& report) {
    std::ostringstream os;
    os << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>sikit report)HTML";
    if (!report.board_path.empty()) os << ": " << esc(report.board_path);
    os << R"HTML(</title>
<style>
body { font-family: -apple-system, system-ui, sans-serif;
        color: #222; margin: 2em; max-width: 1200px; }
h1, h2 { margin-bottom: 0.2em; }
h2 { border-bottom: 1px solid #ddd; padding-bottom: 0.2em; margin-top: 1.5em; }
.banner { padding: 1em; border-radius: 4px; margin-bottom: 1em; }
.pass { background: #e6f4ea; color: #1e6b30; }
.fail { background: #fbeaea; color: #8b1a1a; }
.meta { color: #666; font-size: 0.9em; }
table { border-collapse: collapse; margin-top: 0.5em; }
th, td { padding: 4px 10px; border: 1px solid #ccc; text-align: left; }
th { background: #f3f3f3; font-weight: 600; }
tr.fail-row td { background: #fdecec; }
tr.pass-row td { background: #ecfbef; }
.num { text-align: right; font-variant-numeric: tabular-nums; }
.summary-tile { display: inline-block; padding: 0.4em 0.8em;
                  border-radius: 3px; margin-right: 0.5em;
                  background: #f5f5f5; }
</style>
</head>
<body>
<h1>sikit board report)HTML";

    if (!report.board_path.empty()) {
        os << "<div class=\"meta\">" << esc(report.board_path) << "</div>";
    }
    os << "</h1>\n";

    os << "<div class=\"banner "
       << (report.overall_pass() ? "pass" : "fail") << "\">"
       << (report.overall_pass() ? "All checks PASS" : "Issues found")
       << "</div>\n";

    os << "<p>"
       << "<span class=\"summary-tile\">" << report.net_count << " nets</span>"
       << "<span class=\"summary-tile\">" << report.segment_count
       << " segments</span>"
       << "<span class=\"summary-tile\">" << report.via_count << " vias</span>"
       << "<span class=\"summary-tile\">" << report.copper_layer_count
       << " copper layers</span>"
       << "</p>\n";

    // ----- Per-net impedance + length table.
    os << "<h2>Trace impedance</h2>\n";
    os << "<table><thead><tr>"
       << "<th>Net</th><th>Layer</th><th class=\"num\">Width (mm)</th>"
       << "<th class=\"num\">Length (mm)</th>"
       << "<th class=\"num\">Z<sub>0</sub> (&Omega;)</th>"
       << "<th class=\"num\">v<sub>p</sub> (m/s)</th>"
       << "<th class=\"num\">&epsilon;<sub>eff</sub></th>"
       << "</tr></thead><tbody>\n";
    for (const auto& n : report.nets) {
        if (!n.valid) continue;
        os << "<tr><td>" << esc(n.net_name) << "</td>"
           << "<td>L" << n.layer_ordinal << "</td>"
           << "<td class=\"num\">"
           << static_cast<int>(n.trace_width_m * 1e6 + 0.5) / 1000.0 << "</td>"
           << "<td class=\"num\">"
           << static_cast<int>(n.total_length_m * 1e4 + 0.5) / 10.0 << "</td>"
           << "<td class=\"num\">"
           << static_cast<int>(n.z0_ohm * 100 + 0.5) / 100.0 << "</td>"
           << "<td class=\"num\">"
           << static_cast<long long>(n.v_phase) << "</td>"
           << "<td class=\"num\">"
           << static_cast<int>(n.eps_eff * 1000 + 0.5) / 1000.0 << "</td>"
           << "</tr>\n";
    }
    os << "</tbody></table>\n";

    // ----- Diff-pair skew.
    if (!report.diff_pairs.empty()) {
        os << "<h2>Diff-pair skew</h2>\n";
        os << "<table><thead><tr>"
           << "<th>Pair</th>"
           << "<th class=\"num\">P (mm)</th>"
           << "<th class=\"num\">N (mm)</th>"
           << "<th class=\"num\">|skew| (mm)</th>"
           << "<th class=\"num\">skew (ps)</th>"
           << "<th>Verdict</th>"
           << "</tr></thead><tbody>\n";
        for (const auto& p : report.diff_pairs) {
            os << "<tr class=\""
               << (p.exceeds_budget ? "fail-row" : "pass-row") << "\">"
               << "<td>" << esc(p.base_name) << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(p.length_p_m * 1e4 + 0.5) / 10.0 << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(p.length_n_m * 1e4 + 0.5) / 10.0 << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(std::abs(p.skew_m) * 1e6 + 0.5) / 1000.0
               << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(p.skew_ps * 100 + 0.5) / 100.0 << "</td>"
               << "<td>" << (p.exceeds_budget ? "FAIL" : "PASS")
               << "</td></tr>\n";
        }
        os << "</tbody></table>\n";
    }

    // ----- Bus skew.
    if (!report.buses.empty()) {
        os << "<h2>Multi-bit bus skew</h2>\n";
        os << "<table><thead><tr>"
           << "<th>Bus</th>"
           << "<th class=\"num\">N</th>"
           << "<th class=\"num\">Min (mm)</th>"
           << "<th class=\"num\">Max (mm)</th>"
           << "<th class=\"num\">Skew (ps)</th>"
           << "<th>Verdict</th>"
           << "</tr></thead><tbody>\n";
        for (const auto& g : report.buses) {
            os << "<tr class=\""
               << (g.exceeds_budget ? "fail-row" : "pass-row") << "\">"
               << "<td>" << esc(g.base_name) << "</td>"
               << "<td class=\"num\">" << g.members.size() << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(g.min_length_m * 1e4 + 0.5) / 10.0 << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(g.max_length_m * 1e4 + 0.5) / 10.0 << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(g.skew_ps * 100 + 0.5) / 100.0 << "</td>"
               << "<td>" << (g.exceeds_budget ? "FAIL" : "PASS")
               << "</td></tr>\n";
        }
        os << "</tbody></table>\n";
    }

    // ----- Return-path violations.
    if (!report.return_path_violations.empty()) {
        os << "<h2>Return-path violations</h2>\n";
        os << "<table><thead><tr>"
           << "<th class=\"num\">Rank</th>"
           << "<th>Net</th>"
           << "<th>Signal layer</th>"
           << "<th>Reference layer</th>"
           << "<th class=\"num\">Off-plane (%)</th>"
           << "<th class=\"num\">Severity (mm)</th>"
           << "</tr></thead><tbody>\n";
        int rank = 0;
        for (const auto& v : report.return_path_violations) {
            os << "<tr class=\"fail-row\">"
               << "<td class=\"num\">" << ++rank << "</td>"
               << "<td>" << v.net_id << "</td>"
               << "<td>L" << v.signal_layer << "</td>"
               << "<td>L" << v.reference_layer << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(v.off_plane_fraction * 1000 + 0.5) / 10.0
               << "</td>"
               << "<td class=\"num\">"
               << static_cast<int>(v.severity_m * 1e4 + 0.5) / 10.0
               << "</td></tr>\n";
        }
        os << "</tbody></table>\n";
    }

    os << "<p class=\"meta\">Generated by sikit. Tabular only; embedded "
          "waveform plots are a future enhancement.</p>\n"
       << "</body></html>\n";
    return os.str();
}

}  // namespace sikit::report
