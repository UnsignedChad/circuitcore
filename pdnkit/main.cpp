#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <tuple>

#include <QApplication>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QSurfaceFormat>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <Eigen/Core>

#include "MainWindow.h"
#include "circuitcore/formats/kicad/PcbParser.h"
#include "pi/IrMesher.h"
#include "pi/PowerDrc.h"
#include "pi/SpiceExport.h"
#include "pi/TargetZ.h"
#include "pi/ViaInductance.h"
#include "pi/Dielectric.h"
#include "pi/Touchstone.h"
#include "pi/CavityModel.h"
#include "pi/IrSolver.h"
#include "pi/Transient.h"

namespace {

// Headless analysis. Loads the board, finds (net, layer), runs the full
// pipeline, prints a one-line result, returns exit code (0 = ok).
int run_headless_analysis(const std::string& pcb_path,
                          const std::string& net_name,
                          const std::string& layer_name,
                          double current,
                          double cell_size_mm) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }

    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'. Available nets:\n",
                     net_name.c_str());
        for (const auto& n : board.nets) {
            std::fprintf(stderr, "  #%d  %s\n", n.id, n.name.c_str());
        }
        return 3;
    }

    int layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) {
            layer_ord = L.ordinal;
            break;
        }
    }
    if (layer_ord < 0) {
        std::fprintf(stderr, "pdnkit: no layer named '%s'. Available layers:\n",
                     layer_name.c_str());
        for (const auto& L : board.stackup.layers) {
            std::fprintf(stderr, "  %3d  %s  (%s)\n", L.ordinal,
                         L.name.c_str(), L.type.c_str());
        }
        return 4;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = cell_size_mm * 1.0e-3;
    mc.net_id = net->id;
    mc.layer_ordinal = layer_ord;
    auto mesh = pdnkit::pi::IrMesher::build(board, mc);
    if (mesh.nodes.empty()) {
        std::fprintf(stderr,
                     "pdnkit: mesher produced no nodes for net '%s' on '%s'\n",
                     net_name.c_str(), layer_name.c_str());
        return 5;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        std::fprintf(stderr,
                     "pdnkit: need at least 2 pads on (net, layer) for "
                     "source/sink (auto-pick failed)\n");
        return 6;
    }

    auto sol = pdnkit::pi::IrSolver::solve(mesh, {current});
    if (!sol.ok) {
        std::fprintf(stderr, "pdnkit: solve failed: %s\n", sol.error.c_str());
        return 7;
    }

    const double drop_mv = (sol.max_v - sol.min_v) * 1000.0;
    std::string reported_layer = layer_name;
    if (mesh.primary_layer_used >= 0 && mesh.primary_layer_used != layer_ord) {
        for (const auto& L : board.stackup.layers) {
            if (L.ordinal == mesh.primary_layer_used) {
                reported_layer = L.name + std::string("(auto)");
                break;
            }
        }
    }
    std::printf("pdnkit IR drop  net=%s  layer=%s  current=%.3fA  "
                "nodes=%zu  resistors=%zu  Vmax=%.6fmV  Vmin=%.6fmV  "
                "drop=%.6fmV\n",
                net_name.c_str(), reported_layer.c_str(), current,
                mesh.nodes.size(), mesh.resistors.size(),
                sol.max_v * 1000.0, sol.min_v * 1000.0, drop_mv);
    return 0;
}

int run_headless_zf(const std::string& pcb_path,
                    const std::string& net_name,
                    const std::string& layer_name,
                    double port1_x_mm, double port1_y_mm,
                    double port2_x_mm, double port2_y_mm,
                    double eps_r, double tan_delta, double thickness_mm,
                    double f_min, double f_max,
                    int points, int modes) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }
    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'\n", net_name.c_str());
        return 3;
    }
    int layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) { layer_ord = L.ordinal; break; }
    }
    if (layer_ord < 0) {
        std::fprintf(stderr, "pdnkit: no layer named '%s'\n", layer_name.c_str());
        return 4;
    }

    // Plane bbox from zone fill on (net, layer).
    bool any = false;
    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    for (const auto& z : board.zones) {
        if (z.net_id != net->id || z.layer_ordinal != layer_ord) continue;
        for (const auto& fp : z.filled) {
            for (const auto& p : fp.outline) {
                if (!any) { lo_x = hi_x = p.x; lo_y = hi_y = p.y; any = true; }
                else {
                    if (p.x < lo_x) lo_x = p.x;
                    if (p.x > hi_x) hi_x = p.x;
                    if (p.y < lo_y) lo_y = p.y;
                    if (p.y > hi_y) hi_y = p.y;
                }
            }
        }
    }
    if (!any) {
        std::fprintf(stderr, "pdnkit: no filled zones on (net, layer)\n");
        return 5;
    }

    pdnkit::pi::CavityConfig cfg;
    cfg.a = hi_x - lo_x;
    cfg.b = hi_y - lo_y;
    cfg.d = thickness_mm * 1.0e-3;
    cfg.eps_r = eps_r;
    cfg.tan_delta = tan_delta;
    cfg.max_modes = modes;

    std::vector<double> freqs;
    freqs.reserve(points);
    const double log_lo = std::log10(f_min);
    const double log_hi = std::log10(f_max);
    for (int i = 0; i < points; ++i) {
        const double t = (points == 1) ? 0.0 : static_cast<double>(i) / (points - 1);
        freqs.push_back(std::pow(10.0, log_lo + t * (log_hi - log_lo)));
    }
    auto mags = pdnkit::pi::cavity_impedance_magnitude_sweep(
        cfg,
        port1_x_mm * 1.0e-3, port1_y_mm * 1.0e-3,
        port2_x_mm * 1.0e-3, port2_y_mm * 1.0e-3,
        freqs);

    // CSV to stdout
    std::printf("freq_hz,abs_z_ohm\n");
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        std::printf("%.8g,%.8g\n", freqs[i], mags[i]);
    }
    return 0;
}

int run_headless_list_nets(const std::string& pcb_path) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }
    // Tally per-net pad / segment / zone counts.
    std::map<int, std::tuple<int, int, int>> counts;
    for (const auto& p : board.pads)     std::get<0>(counts[p.net_id])++;
    for (const auto& s : board.segments) std::get<1>(counts[s.net_id])++;
    for (const auto& z : board.zones)    std::get<2>(counts[z.net_id])++;

    std::printf("net_id,net_name,pads,segments,zones\n");
    for (const auto& n : board.nets) {
        auto it = counts.find(n.id);
        const int p = it == counts.end() ? 0 : std::get<0>(it->second);
        const int s = it == counts.end() ? 0 : std::get<1>(it->second);
        const int z = it == counts.end() ? 0 : std::get<2>(it->second);
        std::printf("%d,%s,%d,%d,%d\n",
                    n.id, n.name.c_str(), p, s, z);
    }
    return 0;
}

int run_headless_list_layers(const std::string& pcb_path) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }
    std::printf("ordinal,name,type,is_copper,thickness_um\n");
    for (const auto& L : board.stackup.layers) {
        std::printf("%d,%s,%s,%d,%.2f\n",
                    L.ordinal, L.name.c_str(), L.type.c_str(),
                    L.is_copper() ? 1 : 0, L.thickness * 1.0e6);
    }
    return 0;
}

int run_headless_probe_r(const std::string& pcb_path,
                         const std::string& net_name,
                         const std::string& layer_name,
                         const std::string& pad_a,
                         const std::string& pad_b,
                         double cell_size_mm) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }
    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'\n", net_name.c_str());
        return 3;
    }
    int layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) { layer_ord = L.ordinal; break; }
    }
    if (layer_ord < 0) {
        std::fprintf(stderr, "pdnkit: no layer named '%s'\n", layer_name.c_str());
        return 4;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = cell_size_mm * 1.0e-3;
    mc.net_id = net->id;
    mc.layer_ordinal = layer_ord;
    mc.source_pad_names = {pad_a};
    mc.sink_pad_names   = {pad_b};

    auto mesh = pdnkit::pi::IrMesher::build(board, mc);
    if (mesh.nodes.empty()) {
        std::fprintf(stderr, "pdnkit: mesher produced no nodes\n");
        return 5;
    }
    if (mesh.source_node_ids.empty()) {
        std::fprintf(stderr, "pdnkit: pad '%s' did not attach to any mesh node\n",
                     pad_a.c_str());
        return 6;
    }
    if (mesh.sink_node_ids.empty()) {
        std::fprintf(stderr, "pdnkit: pad '%s' did not attach to any mesh node\n",
                     pad_b.c_str());
        return 6;
    }

    auto sol = pdnkit::pi::IrSolver::solve(mesh, {1.0});
    if (!sol.ok) {
        std::fprintf(stderr, "pdnkit: solve failed: %s\n", sol.error.c_str());
        return 7;
    }

    // Effective R = (V_source - V_sink) / I_total at 1 A injection.
    // With edge-contact, source/sink lists hold multiple nodes; average V
    // across each set.
    auto avg_v = [&](const std::vector<int>& ids) {
        double s = 0.0;
        for (int id : ids) {
            if (id >= 0 && id < static_cast<int>(sol.voltages.size()))
                s += sol.voltages[id];
        }
        return s / static_cast<double>(ids.size());
    };
    const double v_src = avg_v(mesh.source_node_ids);
    const double v_snk = avg_v(mesh.sink_node_ids);
    const double r_eff = v_src - v_snk;

    std::printf("pdnkit R-probe  net=%s  layer=%s  "
                "pad_a=%s pad_b=%s  R_eff=%.6e ohm  (%.4f m-ohm)\n",
                net_name.c_str(), layer_name.c_str(),
                pad_a.c_str(), pad_b.c_str(),
                r_eff, r_eff * 1000.0);
    return 0;
}

int run_headless_transient(const std::string& pcb_path,
                           const std::string& net_name,
                           const std::string& layer_name,
                           double current_a,
                           double cell_size_mm,
                           double dt_ns,
                           int n_steps,
                           double eps_r,
                           double thickness_mm) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }
    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'\n", net_name.c_str());
        return 3;
    }
    int layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) { layer_ord = L.ordinal; break; }
    }
    if (layer_ord < 0) {
        std::fprintf(stderr, "pdnkit: no layer named '%s'\n", layer_name.c_str());
        return 4;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = cell_size_mm * 1.0e-3;
    mc.net_id = net->id;
    mc.layer_ordinal = layer_ord;
    auto mesh = pdnkit::pi::IrMesher::build(board, mc);
    if (mesh.nodes.empty()) {
        std::fprintf(stderr, "pdnkit: mesher produced no nodes\n");
        return 5;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        std::fprintf(stderr, "pdnkit: need at least 2 pads on net for "
                             "source/sink\n");
        return 6;
    }

    auto c_vec = pdnkit::pi::build_distributed_capacitance(
        mesh, mc.cell_size, eps_r, thickness_mm * 1.0e-3, {});

    pdnkit::pi::TransientConfig tcfg;
    tcfg.per_node_capacitances = std::move(c_vec);
    tcfg.dt = dt_ns * 1.0e-9;
    tcfg.n_steps = n_steps;
    tcfg.step_current = current_a;

    auto res = pdnkit::pi::solve_step_transient(mesh, tcfg);
    if (!res.ok) {
        std::fprintf(stderr, "pdnkit: transient solve failed: %s\n",
                     res.error.c_str());
        return 7;
    }

    std::printf("time_s,v_obs_v,v_max_v\n");
    for (std::size_t i = 0; i < res.times.size(); ++i) {
        std::printf("%.8g,%.8g,%.8g\n",
                    res.times[i], res.obs_v[i], res.max_v[i]);
    }
    return 0;
}

}  // namespace

int run_headless_drc(const std::string& pcb_path,
                     const std::string& net_name,
                     double current_amps,
                     double temp_rise_c) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }

    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'\n", net_name.c_str());
        return 3;
    }

    pdnkit::pi::DrcRule rule;
    rule.net_id = net->id;
    rule.current_amps = current_amps;
    rule.temp_rise_c = temp_rise_c;

    auto report = pdnkit::pi::check_ipc2152(board, {rule});

    std::printf("pdnkit IPC-2152 DRC  net=%s  I=%.3f A  dT=%.1f C\n",
                net_name.c_str(), current_amps, temp_rise_c);
    std::printf("  %d segment(s) checked\n", report.segments_checked);
    if (report.violations.empty()) {
        std::printf("  OK -- no violations.\n");
        return 0;
    }
    std::printf("  %zu violation(s):\n", report.violations.size());
    for (const auto& v : report.violations) {
        std::printf("    [seg %d] %s\n", v.segment_index, v.message.c_str());
    }
    return 4;
}

int run_headless_spice(const std::string& pcb_path,
                       const std::string& net_name,
                       const std::string& layer_name,
                       double current_amps,
                       double cell_size_mm,
                       const std::string& out_path) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }

    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'\n", net_name.c_str());
        return 3;
    }
    int layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) { layer_ord = L.ordinal; break; }
    }
    if (layer_ord < 0) {
        std::fprintf(stderr, "pdnkit: no layer named '%s'\n", layer_name.c_str());
        return 4;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = cell_size_mm * 1.0e-3;
    mc.net_id = net->id;
    mc.layer_ordinal = layer_ord;
    mc.auto_select_layer = true;

    auto mesh = pdnkit::pi::IrMesher::build(board, mc);
    if (mesh.nodes.empty()) {
        std::fprintf(stderr, "pdnkit: mesher produced no nodes\n");
        return 5;
    }

    pdnkit::pi::SpiceExportConfig cfg;
    cfg.title = std::format("pdnkit IR-drop network: net={} layer={}",
                             net_name, layer_name);
    cfg.default_total_current = current_amps;

    if (out_path.empty()) {
        std::printf("%s", pdnkit::pi::export_spice(mesh, cfg).c_str());
    } else {
        if (!pdnkit::pi::write_spice_netlist(mesh, out_path, cfg)) {
            std::fprintf(stderr, "pdnkit: failed to write '%s'\n",
                         out_path.c_str());
            return 6;
        }
        std::fprintf(stderr,
                     "pdnkit: wrote SPICE netlist to %s "
                     "(%zu nodes, %zu resistors)\n",
                     out_path.c_str(),
                     mesh.nodes.size(), mesh.resistors.size());
    }
    return 0;
}

int run_headless_target_z(double v_nom, double v_tol, double i_step) {
    pdnkit::pi::TargetZSpec spec{v_nom, v_tol, i_step};
    const double z = pdnkit::pi::target_impedance_flat(spec);
    std::printf("V_nom = %.3f V,  V_tol = %.3f (%.1f%%),  I_step = %.3f A\n",
                v_nom, v_tol, v_tol * 100.0, i_step);
    std::printf("Z_target = %.6e ohm  (%.3f m-ohm)\n", z, z * 1000.0);
    return 0;
}

int run_headless_via_l(double diameter_mm, double length_mm,
                       double spacing_mm) {
    const double r = 0.5 * diameter_mm * 1.0e-3;
    const double h = length_mm * 1.0e-3;
    const double L = pdnkit::pi::via_self_inductance(r, h);
    std::printf("Via barrel  diameter=%.3f mm  length=%.3f mm\n",
                diameter_mm, length_mm);
    std::printf("  L_self = %.6e H  (%.3f pH)\n", L, L * 1.0e12);
    if (spacing_mm > 0.0) {
        const double d = spacing_mm * 1.0e-3;
        const double M = pdnkit::pi::via_mutual_inductance(r, h, d);
        const double loop = L - M;
        std::printf("  with return at spacing=%.3f mm:\n", spacing_mm);
        std::printf("    M_mutual = %.3f pH\n", M * 1.0e12);
        std::printf("    L_loop   = %.3f pH  (self - mutual)\n",
                    loop * 1.0e12);
    }
    return 0;
}

int run_headless_eps_f(double f_hz, double eps_inf, double delta_eps,
                       double f1_hz, double f2_hz) {
    pdnkit::pi::DjordjevicSarkar m{eps_inf, delta_eps, f1_hz, f2_hz};
    auto s = pdnkit::pi::dj_sarkar_at(m, f_hz);
    std::printf("Djordjevic-Sarkar  eps_inf=%.3f  delta_eps=%.3f  "
                "f1=%.1e Hz  f2=%.1e Hz\n",
                eps_inf, delta_eps, f1_hz, f2_hz);
    std::printf("  at f = %.3e Hz:\n", f_hz);
    std::printf("    eps_r'  = %.4f\n", s.eps_r_real);
    std::printf("    eps_r\" = %.4e\n", s.eps_r_imag);
    std::printf("    tan(d)  = %.5f\n", s.tan_delta);
    return 0;
}

int run_headless_touchstone(const std::string& pcb_path,
                            const std::string& net_name,
                            const std::string& layer_name,
                            double port1_x_mm, double port1_y_mm,
                            double port2_x_mm, double port2_y_mm,
                            double eps_r, double tan_delta, double thickness_mm,
                            double f_min, double f_max,
                            int points, int modes,
                            const std::string& out_path) {
    circuitcore::board::Board board;
    {
        auto result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!result) {
            std::fprintf(stderr, "pdnkit: parse failed: %s\n",
                         result.error().format().c_str());
            return 2;
        }
        board = std::move(*result);
    }
    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "pdnkit: no net named '%s'\n", net_name.c_str());
        return 3;
    }
    int layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) { layer_ord = L.ordinal; break; }
    }
    if (layer_ord < 0) {
        std::fprintf(stderr, "pdnkit: no layer named '%s'\n", layer_name.c_str());
        return 4;
    }

    bool any = false;
    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    for (const auto& z : board.zones) {
        if (z.net_id != net->id || z.layer_ordinal != layer_ord) continue;
        for (const auto& fp : z.filled) {
            for (const auto& p : fp.outline) {
                if (!any) { lo_x = hi_x = p.x; lo_y = hi_y = p.y; any = true; }
                else {
                    if (p.x < lo_x) lo_x = p.x;
                    if (p.x > hi_x) hi_x = p.x;
                    if (p.y < lo_y) lo_y = p.y;
                    if (p.y > hi_y) hi_y = p.y;
                }
            }
        }
    }
    if (!any) {
        std::fprintf(stderr, "pdnkit: no filled zones on (net, layer)\n");
        return 5;
    }

    pdnkit::pi::CavityConfig cfg;
    cfg.a = hi_x - lo_x;
    cfg.b = hi_y - lo_y;
    cfg.d = thickness_mm * 1.0e-3;
    cfg.eps_r = eps_r;
    cfg.tan_delta = tan_delta;
    cfg.max_modes = modes;

    std::vector<pdnkit::pi::TouchstoneSample> samples;
    samples.reserve(points);
    const double log_lo = std::log10(f_min);
    const double log_hi = std::log10(f_max);
    constexpr double k2pi = 2.0 * 3.14159265358979323846;
    for (int i = 0; i < points; ++i) {
        const double t = (points == 1) ? 0.0 :
                          static_cast<double>(i) / (points - 1);
        const double f = std::pow(10.0, log_lo + t * (log_hi - log_lo));
        const double w = k2pi * f;
        const auto z = pdnkit::pi::cavity_impedance(
            cfg,
            port1_x_mm * 1.0e-3, port1_y_mm * 1.0e-3,
            port2_x_mm * 1.0e-3, port2_y_mm * 1.0e-3, w);
        samples.push_back({f, z});
    }

    const std::string out = out_path.empty()
        ? std::string("pdnkit_zf.s1p") : out_path;
    const std::string comment = std::format(
        "pdnkit cavity Z(f) sweep\nnet={} layer={} a={:.1f}mm b={:.1f}mm "
        "d={:.3f}mm eps_r={:.2f} tan_d={:.4f}",
        net_name, layer_name,
        cfg.a * 1000.0, cfg.b * 1000.0,
        thickness_mm, eps_r, tan_delta);
    if (!pdnkit::pi::write_touchstone_z1p(out, samples, comment)) {
        std::fprintf(stderr, "pdnkit: failed to write '%s'\n", out.c_str());
        return 6;
    }
    std::fprintf(stderr,
                 "pdnkit: wrote Touchstone (.s1p) to %s (%d frequency points)\n",
                 out.c_str(), points);
    return 0;
}

int main(int argc, char** argv) {
    CLI::App cli{"pdnkit — open-source Power Integrity analysis for KiCad PCBs"};
    cli.allow_extras();  // Don't trip on Qt's --platform, --style, etc.

    std::string pcb_path;
    cli.add_option("--open,pcb", pcb_path,
                   "KiCad .kicad_pcb file to open on startup")
        ->check(CLI::ExistingFile);

    bool analyze = false;
    std::string analyze_net = "GND";
    std::string analyze_layer = "F.Cu";
    double analyze_current = 1.0;
    double analyze_cell_mm = 0.5;
    cli.add_flag("--analyze", analyze,
                 "Run static IR-drop analysis headlessly and exit (no GUI). "
                 "Requires --open <file>.");
    cli.add_option("--net", analyze_net,
                   "Net name to analyze (default: GND)");
    cli.add_option("--layer", analyze_layer,
                   "Layer name to analyze (default: F.Cu)");
    cli.add_option("--current", analyze_current,
                   "Total current to inject, Amperes (default: 1.0)");
    cli.add_option("--cell-size", analyze_cell_mm,
                   "Mesh cell size, millimeters (default: 0.5)");

    bool zf = false;
    double zf_p1x = 0.0, zf_p1y = 0.0, zf_p2x = 0.0, zf_p2y = 0.0;
    double zf_eps_r = 4.3, zf_tan_delta = 0.020, zf_thickness_mm = 1.6;
    double zf_f_min = 1.0e6, zf_f_max = 5.0e9;
    int zf_points = 300, zf_modes = 30;
    cli.add_flag("--zf", zf,
                 "Run cavity-model Z(f) sweep headlessly. Prints CSV to stdout. "
                 "Uses --net and --layer for the plane.");
    cli.add_option("--port1-x", zf_p1x, "Z(f) port 1 X position (mm)");
    cli.add_option("--port1-y", zf_p1y, "Z(f) port 1 Y position (mm)");
    cli.add_option("--port2-x", zf_p2x, "Z(f) port 2 X position (mm)");
    cli.add_option("--port2-y", zf_p2y, "Z(f) port 2 Y position (mm)");
    cli.add_option("--eps-r", zf_eps_r, "Dielectric eps_r (default 4.3 FR-4)");
    cli.add_option("--tan-delta", zf_tan_delta, "Loss tangent (default 0.020)");
    cli.add_option("--thickness", zf_thickness_mm, "Substrate thickness (mm)");
    cli.add_option("--f-min", zf_f_min, "Sweep start frequency (Hz)");
    cli.add_option("--f-max", zf_f_max, "Sweep end frequency (Hz)");
    cli.add_option("--points", zf_points, "Number of log-spaced frequency points");
    cli.add_option("--modes", zf_modes, "Mode sum truncation per axis");

    bool touchstone = false;
    std::string ts_out;
    cli.add_flag("--touchstone", touchstone,
                 "Write the cavity Z(f) sweep as a Touchstone v1 .s1p "
                 "file (Z-form). Uses the same --net / --port* / --f-* "
                 "options as --zf.");
    cli.add_option("--touchstone-out", ts_out,
                   "Output path for --touchstone (default: pdnkit_zf.s1p)");

    bool list_nets = false;
    bool list_layers = false;
    cli.add_flag("--list-nets", list_nets,
                 "Print all nets in the board as CSV "
                 "(net_id,net_name,pads,segments,zones) and exit.");
    cli.add_flag("--list-layers", list_layers,
                 "Print the layer stackup as CSV "
                 "(ordinal,name,type,is_copper,thickness_um) and exit.");

    bool probe_r = false;
    std::string probe_pad_a, probe_pad_b;
    cli.add_flag("--probe-r", probe_r,
                 "Print effective resistance between --pad-a and --pad-b "
                 "on --net at --layer. 1 A injection.");
    cli.add_option("--pad-a", probe_pad_a, "Source pad name for --probe-r");
    cli.add_option("--pad-b", probe_pad_b, "Sink pad name for --probe-r");

    bool drc = false;
    double drc_current = 1.0;
    double drc_temp_rise = 10.0;
    cli.add_flag("--drc", drc,
                 "IPC-2152 power-aware DRC: flag segments on --net too "
                 "narrow to carry --drc-current at --drc-temp-rise without "
                 "exceeding the IPC-2221 closed-form limit.");
    cli.add_option("--drc-current", drc_current,
                   "Current carried by --net for --drc (A, default 1.0)");
    cli.add_option("--drc-temp-rise", drc_temp_rise,
                   "Allowable temperature rise for --drc (C, default 10)");

    bool spice = false;
    std::string spice_out;
    cli.add_flag("--spice", spice,
                 "Export the IR-drop mesh on --net at --layer as a SPICE "
                 "netlist. Writes to --out, or stdout if not given.");
    cli.add_option("--out", spice_out,
                   "Output file path for --spice (default: stdout)");

    bool target_z = false;
    double tz_v_nom = 3.3;
    double tz_v_tol = 0.05;
    double tz_i_step = 1.0;
    cli.add_flag("--target-z", target_z,
                 "Compute target PDN impedance Z = V_nom * V_tol / I_step "
                 "(Larry Smith flat-target form). No board file required.");
    cli.add_option("--v-nom",  tz_v_nom,
                   "Nominal supply voltage for --target-z (V, default 3.3)");
    cli.add_option("--v-tol",  tz_v_tol,
                   "Fractional voltage tolerance for --target-z "
                   "(e.g. 0.05 = 5%, default 0.05)");
    cli.add_option("--i-step", tz_i_step,
                   "Peak step current the load demands for --target-z "
                   "(A, default 1.0)");

    bool via_l = false;
    double via_d_mm = 0.3;
    double via_h_mm = 1.6;
    double via_s_mm = 0.0;
    cli.add_flag("--via-l", via_l,
                 "Compute partial inductance of a cylindrical via barrel "
                 "(Grover/Ruehli closed form). Optionally with a return "
                 "via at --via-spacing.");
    cli.add_option("--via-diameter", via_d_mm,
                   "Via barrel outer diameter (mm, default 0.3)");
    cli.add_option("--via-length",   via_h_mm,
                   "Via barrel length, typically board thickness "
                   "(mm, default 1.6)");
    cli.add_option("--via-spacing",  via_s_mm,
                   "Center-to-center spacing to a return via for loop-L "
                   "calc (mm, default 0 = self only)");
    bool eps_f = false;
    double eps_f_hz = 1.0e9;
    double eps_inf = 3.8;
    double eps_delta = 1.0;
    double eps_f1 = 1.0e3;
    double eps_f2 = 1.0e9;
    cli.add_flag("--eps-f", eps_f,
                 "Evaluate Djordjevic-Sarkar dielectric model "
                 "at --frequency Hz, print eps_r' / eps_r\" / tan(delta).");
    cli.add_option("--frequency", eps_f_hz,
                   "Frequency for --eps-f (Hz, default 1e9)");
    cli.add_option("--eps-inf",   eps_inf,
                   "Dielectric eps_inf (high-freq limit, default 3.8 FR-4)");
    cli.add_option("--delta-eps", eps_delta,
                   "Dispersion magnitude (default 1.0)");
    cli.add_option("--eps-f1",    eps_f1,
                   "Low-frequency corner f1 (Hz, default 1e3)");
    cli.add_option("--eps-f2",    eps_f2,
                   "High-frequency corner f2 (Hz, default 1e9)");

    bool transient = false;
    double trn_dt_ns = 10.0;
    int trn_steps = 1000;
    cli.add_flag("--transient", transient,
                 "Run a step-response transient analysis headlessly and print "
                 "CSV (time_s,v_obs_v,v_max_v) to stdout. Uses --net/--layer "
                 "for the mesh and --current for the step amplitude.");
    cli.add_option("--dt-ns", trn_dt_ns, "Transient timestep in nanoseconds (default 10)");
    cli.add_option("--n-steps", trn_steps, "Number of transient timesteps (default 1000)");

    bool show_version = false;
    cli.add_flag("--version", show_version,
                 "Print pdnkit version and exit");

    try {
        cli.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return cli.exit(e);
    }

    if (show_version) {
        std::printf("pdnkit 0.0.1\n");
        std::printf("  Qt %s\n", QT_VERSION_STR);
        std::printf("  Eigen %d.%d.%d\n",
                    EIGEN_WORLD_VERSION, EIGEN_MAJOR_VERSION, EIGEN_MINOR_VERSION);
#ifdef PDNKIT_HAVE_CHOLMOD
        std::printf("  CHOLMOD: yes (SuiteSparse)\n");
#else
        std::printf("  CHOLMOD: no (using Eigen SimplicialLLT fallback)\n");
#endif
        return 0;
    }

    if (list_nets) {
        if (pcb_path.empty()) {
            std::fprintf(stderr, "pdnkit: --list-nets requires a board file\n");
            return 1;
        }
        return run_headless_list_nets(pcb_path);
    }
    if (list_layers) {
        if (pcb_path.empty()) {
            std::fprintf(stderr, "pdnkit: --list-layers requires a board file\n");
            return 1;
        }
        return run_headless_list_layers(pcb_path);
    }
    if (analyze) {
        if (pcb_path.empty()) {
            std::fprintf(stderr,
                         "pdnkit: --analyze requires a board file "
                         "(--open <file> or positional)\n");
            return 1;
        }
        return run_headless_analysis(pcb_path, analyze_net, analyze_layer,
                                      analyze_current, analyze_cell_mm);
    }
    if (zf) {
        if (pcb_path.empty()) {
            std::fprintf(stderr,
                         "pdnkit: --zf requires a board file "
                         "(--open <file> or positional)\n");
            return 1;
        }
        return run_headless_zf(pcb_path, analyze_net, analyze_layer,
                               zf_p1x, zf_p1y, zf_p2x, zf_p2y,
                               zf_eps_r, zf_tan_delta, zf_thickness_mm,
                               zf_f_min, zf_f_max, zf_points, zf_modes);
    }
    if (touchstone) {
        if (pcb_path.empty()) {
            std::fprintf(stderr,
                         "pdnkit: --touchstone requires a board file\n");
            return 1;
        }
        return run_headless_touchstone(pcb_path, analyze_net, analyze_layer,
                                        zf_p1x, zf_p1y, zf_p2x, zf_p2y,
                                        zf_eps_r, zf_tan_delta, zf_thickness_mm,
                                        zf_f_min, zf_f_max, zf_points, zf_modes,
                                        ts_out);
    }
    if (probe_r) {
        if (pcb_path.empty() || probe_pad_a.empty() || probe_pad_b.empty()) {
            std::fprintf(stderr, "pdnkit: --probe-r requires --pad-a, --pad-b, "
                                 "and a board file\n");
            return 1;
        }
        return run_headless_probe_r(pcb_path, analyze_net, analyze_layer,
                                     probe_pad_a, probe_pad_b, analyze_cell_mm);
    }
    if (drc) {
        if (pcb_path.empty() || analyze_net.empty()) {
            std::fprintf(stderr,
                         "pdnkit: --drc requires --net and a board file\n");
            return 1;
        }
        return run_headless_drc(pcb_path, analyze_net, drc_current,
                                 drc_temp_rise);
    }
    if (spice) {
        if (pcb_path.empty() || analyze_net.empty()) {
            std::fprintf(stderr,
                         "pdnkit: --spice requires --net and a board file\n");
            return 1;
        }
        return run_headless_spice(pcb_path, analyze_net, analyze_layer,
                                   analyze_current, analyze_cell_mm,
                                   spice_out);
    }
    if (target_z) {
        return run_headless_target_z(tz_v_nom, tz_v_tol, tz_i_step);
    }
    if (via_l) {
        return run_headless_via_l(via_d_mm, via_h_mm, via_s_mm);
    }
    if (eps_f) {
        return run_headless_eps_f(eps_f_hz, eps_inf, eps_delta,
                                   eps_f1, eps_f2);
    }
    if (transient) {
        if (pcb_path.empty()) {
            std::fprintf(stderr,
                         "pdnkit: --transient requires a board file\n");
            return 1;
        }
        return run_headless_transient(pcb_path, analyze_net, analyze_layer,
                                      analyze_current, analyze_cell_mm,
                                      trn_dt_ns, trn_steps,
                                      zf_eps_r, zf_thickness_mm);
    }

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("pdnkit");
    QApplication::setApplicationVersion("0.0.1");

    // Programmatic app icon: viridis-gradient square with "pdn" lettering.
    {
        QPixmap pm(64, 64);
        QPainter pp(&pm);
        QLinearGradient g(0, 0, 0, 64);
        g.setColorAt(0.00, QColor(68,   1,  84));
        g.setColorAt(0.25, QColor(59,  81, 139));
        g.setColorAt(0.50, QColor(33, 145, 140));
        g.setColorAt(0.75, QColor(94, 201,  97));
        g.setColorAt(1.00, QColor(253, 231,  37));
        pp.fillRect(QRect(0, 0, 64, 64), g);
        pp.setPen(QColor(15, 15, 18));
        QFont f = pp.font();
        f.setBold(true);
        f.setPointSize(20);
        pp.setFont(f);
        pp.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, "pdn");
        pp.end();
        QApplication::setWindowIcon(QIcon(pm));
    }

    spdlog::info("pdnkit starting");

    MainWindow w;
    w.show();
    if (!pcb_path.empty()) {
        w.loadKicadPcb(QString::fromStdString(pcb_path));
    }
    return app.exec();
}
