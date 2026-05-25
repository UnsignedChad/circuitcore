#include "Report.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

namespace pdnkit {

namespace {

std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            default:  out += c;
        }
    }
    return out;
}

std::string fmt_mv(double v) {
    return std::format("{:.3f} mV", v * 1000.0);
}

// Tiny inline SVG line plot for log-frequency Z(f) sweep.
std::string svg_zf_plot(const std::vector<double>& freqs,
                        const std::vector<std::complex<double>>& zs,
                        double target_ohm) {
    if (freqs.empty() || zs.empty() || freqs.size() != zs.size()) return "";
    constexpr int W = 640, H = 240, mx = 60, my = 30;
    double f_lo = freqs.front(), f_hi = freqs.back();
    double z_lo = 1.0, z_hi = 0.001;
    for (auto z : zs) {
        const double m = std::abs(z);
        if (m > 0.0 && m < z_lo) z_lo = m;
        if (m > z_hi) z_hi = m;
    }
    if (target_ohm > 0.0) {
        if (target_ohm < z_lo) z_lo = target_ohm;
        if (target_ohm > z_hi) z_hi = target_ohm;
    }
    z_lo *= 0.5;
    z_hi *= 2.0;
    const double log_f_lo = std::log10(f_lo), log_f_hi = std::log10(f_hi);
    const double log_z_lo = std::log10(z_lo), log_z_hi = std::log10(z_hi);
    auto px = [&](double f) {
        return mx + (W - 2 * mx) * (std::log10(f) - log_f_lo) /
                                    (log_f_hi - log_f_lo);
    };
    auto py = [&](double m) {
        return H - my - (H - 2 * my) * (std::log10(m) - log_z_lo) /
                                        (log_z_hi - log_z_lo);
    };

    std::ostringstream s;
    s << "<svg width=\"" << W << "\" height=\"" << H << "\""
      << " xmlns=\"http://www.w3.org/2000/svg\""
      << " style=\"background:#1e1e22;\">";
    // Border + axes ticks.
    s << "<rect x=\"" << mx << "\" y=\"" << my << "\""
      << " width=\"" << (W - 2 * mx) << "\" height=\"" << (H - 2 * my) << "\""
      << " fill=\"none\" stroke=\"#666\"/>";
    // Target line.
    if (target_ohm > 0.0) {
        const double y = py(target_ohm);
        s << "<line x1=\"" << mx << "\" y1=\"" << y << "\""
          << " x2=\"" << (W - mx) << "\" y2=\"" << y << "\""
          << " stroke=\"#ff8888\" stroke-dasharray=\"4 4\"/>";
        s << "<text x=\"" << (W - mx + 4) << "\" y=\"" << (y + 4) << "\""
          << " fill=\"#ff8888\" font-size=\"11\" font-family=\"monospace\">"
          << std::format("{:.3g} ohm", target_ohm) << "</text>";
    }
    // Curve.
    s << "<polyline fill=\"none\" stroke=\"#fde725\" stroke-width=\"1.5\" points=\"";
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        if (i > 0) s << " ";
        s << px(freqs[i]) << "," << py(std::abs(zs[i]));
    }
    s << "\"/>";
    // Axis labels.
    s << "<text x=\"" << mx << "\" y=\"" << (H - 8) << "\""
      << " fill=\"#aaa\" font-size=\"11\" font-family=\"monospace\">"
      << std::format("{:.1e} Hz", f_lo) << "</text>";
    s << "<text x=\"" << (W - mx - 60) << "\" y=\"" << (H - 8) << "\""
      << " fill=\"#aaa\" font-size=\"11\" font-family=\"monospace\">"
      << std::format("{:.1e} Hz", f_hi) << "</text>";
    s << "<text x=\"4\" y=\"" << (my + 12) << "\""
      << " fill=\"#aaa\" font-size=\"11\" font-family=\"monospace\">"
      << std::format("{:.1e}", z_hi) << "</text>";
    s << "<text x=\"4\" y=\"" << (H - my) << "\""
      << " fill=\"#aaa\" font-size=\"11\" font-family=\"monospace\">"
      << std::format("{:.1e}", z_lo) << "</text>";
    s << "</svg>";
    return s.str();
}

}  // namespace

std::string render_signoff_html(const SignoffReport& r) {
    std::ostringstream out;
    out << R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>pdnkit PDN signoff</title>
<style>
body { font-family: -apple-system, "Segoe UI", sans-serif;
       background: #1e1e22; color: #ddd; margin: 24px; max-width: 980px; }
h1 { color: #fde725; border-bottom: 1px solid #444; padding-bottom: 6px; }
h2 { color: #61dafb; margin-top: 28px; }
table { border-collapse: collapse; margin: 8px 0; }
th, td { padding: 4px 12px; border-bottom: 1px solid #333; text-align: left; }
th { background: #2a2a30; }
.bad  { color: #ff8888; }
.good { color: #aaffaa; }
.dim  { color: #888; }
code { background: #2a2a30; padding: 1px 4px; border-radius: 3px; }
</style></head><body>
)";
    out << "<h1>pdnkit PDN signoff</h1>\n";

    // ---- Board ----
    out << "<h2>Board</h2><table>";
    out << "<tr><th>File</th><td><code>" << esc(r.board_path) << "</code></td></tr>";
    if (r.board) {
        int copper_layers = 0;
        for (const auto& L : r.board->stackup.layers)
            if (L.is_copper()) ++copper_layers;
        out << "<tr><th>Layers (copper / total)</th><td>"
            << copper_layers << " / " << r.board->stackup.layers.size()
            << "</td></tr>";
        out << "<tr><th>Nets</th><td>" << r.board->nets.size() << "</td></tr>";
        out << "<tr><th>Pads</th><td>" << r.board->pads.size() << "</td></tr>";
        out << "<tr><th>Segments</th><td>" << r.board->segments.size()
            << "</td></tr>";
        out << "<tr><th>Vias</th><td>" << r.board->vias.size() << "</td></tr>";
        out << "<tr><th>Zones</th><td>" << r.board->zones.size() << "</td></tr>";
    }
    out << "</table>\n";

    // ---- IR drop ----
    if (r.ir_mesh && r.ir_solution && r.ir_solution->ok) {
        out << "<h2>Static IR drop</h2><table>";
        const double dv_mv = (r.ir_solution->max_v - r.ir_solution->min_v)
                              * 1000.0;
        out << "<tr><th>Mesh</th><td>" << r.ir_mesh->nodes.size()
            << " nodes, " << r.ir_mesh->resistors.size()
            << " resistors</td></tr>";
        out << "<tr><th>V max</th><td>" << fmt_mv(r.ir_solution->max_v)
            << "</td></tr>";
        out << "<tr><th>V min</th><td>" << fmt_mv(r.ir_solution->min_v)
            << "</td></tr>";
        out << "<tr><th>Drop</th><td><b>"
            << std::format("{:.3f} mV", dv_mv) << "</b></td></tr>";
        if (r.thermal && r.thermal->converged) {
            out << "<tr><th>Thermal coupled</th><td>"
                << "&Delta;T = "
                << std::format("{:.2f}", r.thermal->final_delta_t_c)
                << " &deg;C ("
                << r.thermal->iterations << " iterations"
                << ", rho up "
                << std::format("{:.2f}", (r.thermal->final_rho / 1.68e-8 - 1.0)
                                          * 100.0) << "%)"
                << "</td></tr>";
        }
        out << "</table>\n";
    } else {
        out << "<h2>Static IR drop</h2><p class=\"dim\">No analysis run.</p>\n";
    }

    // ---- DRC ----
    if (r.drc) {
        out << "<h2>IPC-2152 DRC</h2>";
        out << "<p>" << r.drc->segments_checked << " segments checked across "
            << r.drc->nets_checked << " net(s). ";
        if (r.drc->violations.empty()) {
            out << "<span class=\"good\">No violations.</span></p>\n";
        } else {
            out << "<span class=\"bad\">" << r.drc->violations.size()
                << " violation(s):</span></p>";
            out << "<table><tr><th>Seg</th><th>Net</th><th>Layer</th>"
                << "<th>Width (mm)</th><th>Required (mm)</th></tr>";
            for (const auto& v : r.drc->violations) {
                out << "<tr><td>" << v.segment_index << "</td>"
                    << "<td>" << v.net_id << "</td>"
                    << "<td>" << v.layer_ordinal << "</td>"
                    << "<td>"
                    << std::format("{:.3f}", v.width_actual_m * 1000.0)
                    << "</td>"
                    << "<td class=\"bad\">"
                    << std::format("{:.3f}", v.width_required_m * 1000.0)
                    << "</td></tr>";
            }
            out << "</table>\n";
        }
    }

    // ---- Z(f) ----
    if (!r.zf_freqs.empty()) {
        out << "<h2>Plane Z(f)</h2>";
        out << svg_zf_plot(r.zf_freqs, r.zf_z, r.zf_target_ohm) << "\n";
        double peak = 0.0;
        double peak_f = 0.0;
        for (std::size_t i = 0; i < r.zf_z.size(); ++i) {
            const double m = std::abs(r.zf_z[i]);
            if (m > peak) { peak = m; peak_f = r.zf_freqs[i]; }
        }
        out << "<p>Peak |Z| = <b>"
            << std::format("{:.4g} ohm", peak)
            << "</b> at "
            << std::format("{:.3e} Hz", peak_f);
        if (r.zf_target_ohm > 0.0) {
            const bool over = peak > r.zf_target_ohm;
            out << ", target "
                << std::format("{:.4g}", r.zf_target_ohm) << " &mdash; "
                << "<span class=\"" << (over ? "bad" : "good") << "\">"
                << (over ? "exceeds target" : "passes target") << "</span>";
        }
        out << ".</p>\n";
    }

    out << "<p class=\"dim\">Generated by pdnkit.</p>\n";
    out << "</body></html>\n";
    return out.str();
}

bool write_signoff_html(const SignoffReport& r, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << render_signoff_html(r);
    return f.good();
}

}  // namespace pdnkit
