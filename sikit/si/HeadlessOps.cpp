// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "HeadlessOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "si/ChannelSynthesis.h"
#include "si/Compliance.h"
#include "si/EyeMask.h"
#include "si/SpiceExport.h"
#include "si/SParam.h"
#include "si/Overlay.h"
#include "si/BusGroup.h"
#include "si/ReturnPath.h"
#include "si/Report.h"
#include "si/Touchstone.h"
#include "si/TouchstoneWriter.h"
#include "si/TraceImpedance.h"
#include "si/Skew.h"
#include "si/SchematicTopology.h"
#include "si/Fdtd3d.h"
#include "si/FdtdRasterize.h"
#include "circuitcore/netlist/Netlist.h"
#include "si/VectorFit.h"

namespace sikit::cli {

namespace {

// Helper used by every subcommand that takes (net_name, layer_name): looks
// the names up against the loaded board and stuffs the IDs into out_net /
// out_layer_ord. Returns 0 on success, 3 if the net is missing, 4 if the
// layer is missing.
int resolve_net_layer(const circuitcore::board::Board& board,
                       const std::string& net_name,
                       const std::string& layer_name,
                       int& out_net_id, int& out_layer_ord) {
    const auto* net = board.find_net_by_name(net_name);
    if (!net) {
        std::fprintf(stderr, "sikit: no net named '%s'. Available nets:\n",
                      net_name.c_str());
        for (const auto& n : board.nets) {
            std::fprintf(stderr, "  #%d  %s\n", n.id, n.name.c_str());
        }
        return 3;
    }
    out_net_id = net->id;

    out_layer_ord = -1;
    for (const auto& L : board.stackup.layers) {
        if (L.name == layer_name) {
            out_layer_ord = L.ordinal;
            break;
        }
    }
    if (out_layer_ord < 0) {
        std::fprintf(stderr,
                      "sikit: no layer named '%s'. Available layers:\n",
                      layer_name.c_str());
        for (const auto& L : board.stackup.layers) {
            std::fprintf(stderr, "  %3d  %s  (%s)\n", L.ordinal,
                          L.name.c_str(), L.type.c_str());
        }
        return 4;
    }
    return 0;
}

// Walk the board's segments on the named net + layer, return the median
// trace width and the summed length. Used by the synthesise path -- one
// trace width per net is the simplification baked in, even though a real net
// may transition between widths.
struct NetGeometry {
    double median_width_m = 0.0;
    double total_length_m = 0.0;
    int segment_count = 0;
};

NetGeometry walk_net_geometry(const circuitcore::board::Board& board,
                                int net_id, int layer_ord) {
    NetGeometry g;
    std::vector<double> widths;
    for (const auto& s : board.segments) {
        if (s.net_id != net_id) continue;
        if (s.layer_ordinal != layer_ord) continue;
        widths.push_back(s.width);
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        g.total_length_m += std::sqrt(dx * dx + dy * dy);
        ++g.segment_count;
    }
    if (!widths.empty()) {
        std::sort(widths.begin(), widths.end());
        g.median_width_m = widths[widths.size() / 2];
    }
    return g;
}

std::vector<double> linear_freq_grid(double f_lo, double f_hi, int n) {
    std::vector<double> g;
    g.reserve(n);
    if (n <= 0) return g;
    if (n == 1) { g.push_back(0.5 * (f_lo + f_hi)); return g; }
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / (n - 1);
        g.push_back(f_lo + t * (f_hi - f_lo));
    }
    return g;
}

}  // namespace

int impedance_op(const circuitcore::board::Board& board,
                 const sikit::si::SiStackup& sis,
                 const std::string& net_name,
                 const std::string& layer_name,
                 bool use_fdm) {
    int net_id = -1, layer_ord = -1;
    if (int rc = resolve_net_layer(board, net_name, layer_name,
                                     net_id, layer_ord);
        rc != 0) return rc;

    const auto geom = walk_net_geometry(board, net_id, layer_ord);
    if (geom.segment_count == 0) {
        std::fprintf(stderr,
            "sikit: net '%s' has no segments on layer '%s'\n",
            net_name.c_str(), layer_name.c_str());
        return 5;
    }

    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    const auto engine = use_fdm ? sikit::analysis::Engine::Fdm
                                : sikit::analysis::Engine::ClosedForm;
    sikit::analysis::SegmentImpedance imp;
    try {
        imp = (engine == sikit::analysis::Engine::Fdm)
            ? sikit::analysis::compute_one_fdm(geom.median_width_m,
                                                 layer_ord, stackup)
            : sikit::analysis::compute_one(geom.median_width_m,
                                             layer_ord, stackup);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: impedance compute failed: %s\n", e.what());
        return 5;
    }

    std::printf("net      = %s\n", net_name.c_str());
    std::printf("layer    = %s (ord %d)\n", layer_name.c_str(), layer_ord);
    std::printf("width    = %.4f mm  (median across %d segments)\n",
                 geom.median_width_m * 1e3, geom.segment_count);
    std::printf("length   = %.2f mm\n", geom.total_length_m * 1e3);
    std::printf("engine   = %s\n", use_fdm ? "FDM" : "closed-form");
    std::printf("Z0       = %.2f ohm\n", imp.z0);
    std::printf("v_phase  = %.3e m/s\n", imp.v_phase);
    std::printf("eps_eff  = %.3f\n", imp.eps_eff);
    return 0;
}

int touchstone_op(const circuitcore::board::Board& board,
                  const sikit::si::SiStackup& sis,
                  const std::string& net_name,
                  const std::string& layer_name,
                  const std::filesystem::path& out_path,
                  double f_lo_hz, double f_hi_hz, int n_points,
                  bool use_fdm) {
    int net_id = -1, layer_ord = -1;
    if (int rc = resolve_net_layer(board, net_name, layer_name,
                                     net_id, layer_ord);
        rc != 0) return rc;

    const auto geom = walk_net_geometry(board, net_id, layer_ord);
    if (geom.segment_count == 0 || geom.total_length_m <= 0.0) {
        std::fprintf(stderr,
            "sikit: net '%s' has no length on layer '%s'\n",
            net_name.c_str(), layer_name.c_str());
        return 5;
    }

    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    sikit::analysis::ChannelSpec spec;
    spec.trace_width = geom.median_width_m;
    spec.length_m = geom.total_length_m;
    spec.layer_ordinal = layer_ord;
    spec.stackup = stackup;
    spec.engine = use_fdm ? sikit::analysis::Engine::Fdm
                          : sikit::analysis::Engine::ClosedForm;

    const auto freqs = linear_freq_grid(f_lo_hz, f_hi_hz, n_points);

    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: synth failed: %s\n", e.what());
        return 5;
    }
    try {
        sikit::touchstone::TouchstoneWriter::write_file(ts, out_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: write failed: %s\n", e.what());
        return 6;
    }
    std::printf("Wrote %s  (%zu freq points, %.2f mm, W=%.3f mm)\n",
                 out_path.string().c_str(), freqs.size(),
                 geom.total_length_m * 1e3, geom.median_width_m * 1e3);
    return 0;
}

int spice_op(const circuitcore::board::Board& board,
             const sikit::si::SiStackup& sis,
             const std::string& net_name,
             const std::string& layer_name,
             const std::filesystem::path& out_path,
             int n_poles, double f_lo_hz, double f_hi_hz, int n_points,
             bool use_fdm) {
    int net_id = -1, layer_ord = -1;
    if (int rc = resolve_net_layer(board, net_name, layer_name,
                                     net_id, layer_ord);
        rc != 0) return rc;
    const auto geom = walk_net_geometry(board, net_id, layer_ord);
    if (geom.segment_count == 0 || geom.total_length_m <= 0.0) {
        std::fprintf(stderr,
            "sikit: net '%s' has no length on layer '%s'\n",
            net_name.c_str(), layer_name.c_str());
        return 5;
    }

    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    sikit::analysis::ChannelSpec spec;
    spec.trace_width = geom.median_width_m;
    spec.length_m = geom.total_length_m;
    spec.layer_ordinal = layer_ord;
    spec.stackup = stackup;
    spec.engine = use_fdm ? sikit::analysis::Engine::Fdm
                          : sikit::analysis::Engine::ClosedForm;

    const auto freqs = linear_freq_grid(f_lo_hz, f_hi_hz, n_points);
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: synth failed: %s\n", e.what());
        return 5;
    }

    sikit::si::SpiceExportOptions opts;
    opts.fit.n_poles = n_poles;
    // Sanitise the subckt name to a valid SPICE identifier (drop
    // non-alphanumeric chars; if first char is a digit, prefix with N_).
    std::string name;
    for (char c : net_name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            name.push_back(c);
        } else {
            name.push_back('_');
        }
    }
    if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0]))) {
        name = "N_" + name;
    }
    opts.subckt_name = name;

    if (!sikit::si::write_spice_subckt(ts, out_path, opts)) {
        std::fprintf(stderr, "sikit: write failed for %s\n",
                      out_path.string().c_str());
        return 6;
    }
    std::printf("Wrote SPICE subckt '%s' to %s  (%d poles)\n",
                 opts.subckt_name.c_str(), out_path.string().c_str(),
                 n_poles);
    return 0;
}

int compliance_op(const std::filesystem::path& touchstone_in,
                  const std::string& spec_name) {
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::touchstone::TouchstoneReader::read_file(touchstone_in);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: failed to load %s: %s\n",
                      touchstone_in.string().c_str(), e.what());
        return 2;
    }
    const auto* spec = sikit::specs::compliance_by_name(spec_name);
    if (!spec) {
        std::fprintf(stderr, "sikit: no compliance spec named '%s'. "
                              "Available:\n", spec_name.c_str());
        for (const auto& n : sikit::specs::available_compliance_specs()) {
            std::fprintf(stderr, "  %s\n", n.c_str());
        }
        return 3;
    }
    // Report header info and the spec's mask metadata. Full
    // eye reconstruction from the Touchstone would require choosing a
    // bit rate, a PRBS pattern, and a sample-folding pass; that ties
    // into the existing eye::build_eye pipeline and lands as a CLI
    // follow-up. The headline check this version does: confirm the
    // file's freq range covers at least 4 * baud, which is the minimum
    // PRBS spectrum coverage needed to do a credible compliance test.
    const double freq_min = ts.frequencies.empty() ? 0.0
                            : ts.frequencies.front();
    const double freq_max = ts.frequencies.empty() ? 0.0
                            : ts.frequencies.back();
    std::printf("file      = %s\n", touchstone_in.string().c_str());
    std::printf("ports     = %d\n", ts.num_ports);
    std::printf("freq      = %.3e .. %.3e Hz (%zu points)\n",
                 freq_min, freq_max, ts.frequencies.size());
    std::printf("spec      = %s (%s)\n", spec->name.c_str(),
                 spec->family.c_str());
    std::printf("baud      = %.3e symbols/s\n", spec->baud_hz);
    std::printf("ber       = %.0e\n", spec->ber_target);
    std::printf("source    = %s\n", spec->source.c_str());

    const double freq_min_needed = spec->baud_hz / 100.0;
    const double freq_max_needed = spec->baud_hz * 4.0;
    if (freq_min > freq_min_needed) {
        std::printf("WARN: file freq_min (%.3e) above 1/100 of baud (%.3e); "
                     "low-freq behaviour will be extrapolated\n",
                     freq_min, freq_min_needed);
    }
    if (freq_max < freq_max_needed) {
        std::printf("WARN: file freq_max (%.3e) below 4x baud (%.3e); "
                     "PRBS harmonics will be clipped\n",
                     freq_max, freq_max_needed);
    }
    return 0;
}

int list_specs_op() {
    std::printf("Available compliance specs:\n");
    for (const auto* s : sikit::specs::all_compliance_specs()) {
        std::printf("  %-44s  family=%-10s  baud=%.3e\n",
                     s->name.c_str(), s->family.c_str(), s->baud_hz);
    }
    return 0;
}

int list_nets_op(const circuitcore::board::Board& board) {
    std::printf("%-4s  %-32s\n", "id", "name");
    std::printf("----  --------------------------------\n");
    for (const auto& n : board.nets) {
        std::printf("%4d  %s\n", n.id, n.name.c_str());
    }
    return 0;
}


int deembed_op(const std::filesystem::path& measured_in,
                const std::filesystem::path& fixture_in,
                const std::filesystem::path& out_path) {
    sikit::touchstone::TouchstoneFile meas, fix;
    try {
        meas = sikit::touchstone::TouchstoneReader::read_file(measured_in);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: failed to load %s: %s\n",
                      measured_in.string().c_str(), e.what());
        return 2;
    }
    try {
        fix = sikit::touchstone::TouchstoneReader::read_file(fixture_in);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: failed to load %s: %s\n",
                      fixture_in.string().c_str(), e.what());
        return 2;
    }
    sikit::touchstone::TouchstoneFile dut;
    try {
        dut = sikit::sparam::deembed_symmetric(meas, fix);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: deembed failed: %s\n", e.what());
        return 5;
    }
    try {
        sikit::touchstone::TouchstoneWriter::write_file(dut, out_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: write failed: %s\n", e.what());
        return 6;
    }
    std::printf("De-embedded DUT written to %s  (%zu freq points)\n",
                 out_path.string().c_str(), dut.frequencies.size());
    return 0;
}


int compare_op(const std::filesystem::path& a_path,
                const std::filesystem::path& b_path,
                int s_param_index, double max_abs_db) {
    sikit::touchstone::TouchstoneFile a, b;
    try {
        a = sikit::touchstone::TouchstoneReader::read_file(a_path);
        b = sikit::touchstone::TouchstoneReader::read_file(b_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: failed to load: %s\n", e.what());
        return 2;
    }
    sikit::analysis::OverlayDelta d;
    try {
        d = sikit::analysis::overlay_delta(a, b, s_param_index);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sikit: %s\n", e.what());
        return 5;
    }
    std::printf("a         = %s\n", a_path.string().c_str());
    std::printf("b         = %s\n", b_path.string().c_str());
    std::printf("index     = %d\n", s_param_index);
    std::printf("max |dB|  = %.3f dB  at %.3e Hz\n",
                  d.max_abs_db, d.max_freq_hz);
    std::printf("budget    = %.3f dB\n", max_abs_db);
    const bool pass = d.max_abs_db <= max_abs_db;
    std::printf("verdict   = %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int skew_op(const circuitcore::board::Board& board,
             const sikit::si::SiStackup& sis,
             double budget_ps) {
    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    const auto rows = sikit::si::compute_diff_pair_skews(board, stackup, budget_ps);
    if (rows.empty()) {
        std::printf("sikit: no diff pairs detected on this board\n");
        return 3;
    }
    std::printf("%-20s  %8s  %8s  %8s  %7s  %6s\n",
                  "pair", "P (mm)", "N (mm)",
                  "|skew|mm", "skew_ps", "budget");
    int over = 0;
    for (const auto& r : rows) {
        std::printf("%-20s  %8.3f  %8.3f  %8.3f  %+7.2f  %s\n",
                      r.base_name.c_str(),
                      r.length_p_m * 1e3, r.length_n_m * 1e3,
                      std::abs(r.skew_m) * 1e3, r.skew_ps,
                      r.exceeds_budget ? "FAIL" : "PASS");
        if (r.exceeds_budget) ++over;
    }
    std::printf("\n%zu pair(s) checked, %d over the %.2f ps budget\n",
                  rows.size(), over, budget_ps);
    return over == 0 ? 0 : 1;
}


int bus_skew_op(const circuitcore::board::Board& board,
                 const sikit::si::SiStackup& sis,
                 double budget_ps) {
    const auto stackup =
        sikit::analysis::AnalysisStackup::from_board(board, sis);
    const auto groups = sikit::si::compute_bus_groups(board, stackup, budget_ps);
    if (groups.empty()) {
        std::printf("sikit: no multi-bit buses detected on this board\n");
        return 3;
    }
    std::printf("%-20s  %5s  %8s  %8s  %7s  %6s\n",
                  "bus", "N", "min(mm)", "max(mm)", "skew_ps", "budget");
    int over = 0;
    for (const auto& g : groups) {
        std::printf("%-20s  %5zu  %8.3f  %8.3f  %+7.2f  %s\n",
                      g.base_name.c_str(), g.members.size(),
                      g.min_length_m * 1e3, g.max_length_m * 1e3,
                      g.skew_ps,
                      g.exceeds_budget ? "FAIL" : "PASS");
        if (g.exceeds_budget) ++over;
    }
    std::printf("\n%zu bus(es) checked, %d over the %.2f ps budget\n",
                  groups.size(), over, budget_ps);
    return over == 0 ? 0 : 1;
}

int return_path_op(const circuitcore::board::Board& board,
                    int samples_per_segment,
                    double off_plane_threshold) {
    const auto v = sikit::si::detect_return_path_violations(
        board, samples_per_segment, off_plane_threshold);
    if (v.empty()) {
        std::printf("sikit: no return-path violations on this board\n");
        return 0;
    }
    std::printf("%-5s  %-12s  %-8s  %-8s  %-12s  %-10s\n",
                  "rank", "net", "sig_lyr", "ref_lyr",
                  "off-plane", "severity_mm");
    int rank = 0;
    for (const auto& r : v) {
        const auto* net = board.find_net(r.net_id);
        const std::string nm = net ? net->name : std::to_string(r.net_id);
        std::printf("%-5d  %-12s  %-8d  %-8d  %7.1f%%       %8.2f\n",
                      ++rank, nm.c_str(), r.signal_layer, r.reference_layer,
                      r.off_plane_fraction * 100.0,
                      r.severity_m * 1e3);
    }
    std::printf("\n%zu segment(s) flagged\n", v.size());
    return 1;
}


int report_op(const circuitcore::board::Board& board,
                const sikit::si::SiStackup& sis,
                const std::filesystem::path& out_path) {
    auto r = sikit::report::build_board_report(board, sis);
    r.board_path = out_path.filename().string();
    const std::string html = sikit::report::render_html(r);
    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "sikit: cannot write %s\n",
                      out_path.string().c_str());
        return 6;
    }
    out << html;
    std::printf("Report written to %s  (%d nets, %zu pairs, %zu buses, "
                  "%zu RP violations -> %s)\n",
                  out_path.string().c_str(), r.net_count,
                  r.diff_pairs.size(), r.buses.size(),
                  r.return_path_violations.size(),
                  r.overall_pass() ? "PASS" : "FAIL");
    return r.overall_pass() ? 0 : 1;
}


namespace {

void print_topology(const sikit::si::DerivedTopology& t) {
    std::printf("net '%s' (code %d): %zu endpoints\n",
                  t.net_name.c_str(), t.net_code, t.endpoints.size());
    for (const auto& ep : t.endpoints) {
        std::printf("  %-12s %-6s  role=%-11s  type='%s'%s%s\n",
                      ep.component_ref.c_str(),
                      ep.pin.c_str(),
                      sikit::si::role_name(ep.role),
                      ep.pin_type.c_str(),
                      ep.pin_function.empty() ? "" : "  fn=",
                      ep.pin_function.c_str());
    }
    const auto ndrv = t.drivers().size();
    if (t.has_driver_problem()) {
        if (ndrv == 0) {
            std::printf("  PROBLEM: no drivers found on this net\n");
        } else {
            std::printf("  PROBLEM: %zu drivers on this net "
                          "(contention or multi-drop bus)\n", ndrv);
        }
    }
}

}  // namespace

int derive_topology_op(const circuitcore::netlist::Netlist& nl,
                        const std::string& net_name) {
    if (!net_name.empty()) {
        if (!nl.find_net(net_name)) {
            std::fprintf(stderr, "sikit: no net named '%s'\n",
                          net_name.c_str());
            return 3;
        }
        const auto t = sikit::si::derive_topology(nl, net_name);
        print_topology(t);
        return t.has_driver_problem() ? 1 : 0;
    }

    const auto all = sikit::si::derive_all_topologies(nl);
    int problems = 0;
    for (const auto& t : all) {
        print_topology(t);
        if (t.has_driver_problem()) ++problems;
    }
    std::printf("\n%zu signal nets analysed, %d flagged\n",
                  all.size(), problems);
    return problems > 0 ? 1 : 0;
}


int fdtd_info_op(const circuitcore::board::Board& board, double dx_mm) {
    using sikit::fdtd::FDTD3D;
    using sikit::fdtd::GridSpec;

    // Find the board bbox.
    double xmin =  1e18, ymin =  1e18, zmin = 0.0;
    double xmax = -1e18, ymax = -1e18;
    for (const auto& s : board.segments) {
        xmin = std::min({xmin, s.start.x, s.end.x});
        ymin = std::min({ymin, s.start.y, s.end.y});
        xmax = std::max({xmax, s.start.x, s.end.x});
        ymax = std::max({ymax, s.start.y, s.end.y});
    }
    for (const auto& v : board.vias) {
        xmin = std::min(xmin, v.at.x); ymin = std::min(ymin, v.at.y);
        xmax = std::max(xmax, v.at.x); ymax = std::max(ymax, v.at.y);
    }
    for (const auto& z : board.zones) {
        for (const auto& p : z.outline.outline) {
            xmin = std::min(xmin, p.x); ymin = std::min(ymin, p.y);
            xmax = std::max(xmax, p.x); ymax = std::max(ymax, p.y);
        }
    }
    if (xmax < xmin) {
        std::fprintf(stderr, "fdtd info: empty board\n");
        return 5;
    }
    const double dx = dx_mm * 1e-3;
    const int nx = static_cast<int>(std::ceil((xmax - xmin) / dx)) + 1;
    const int ny = static_cast<int>(std::ceil((ymax - ymin) / dx)) + 1;
    const int nz = std::max<int>(8, static_cast<int>(
        std::ceil(board.stackup.total_thickness / dx)) + 1);

    GridSpec g{nx, ny, nz, dx, dx, dx};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const auto m = sikit::fdtd::make_default_mapping(board);
    const auto r = sikit::fdtd::rasterize_board(s, board, m);

    std::printf("FDTD3D grid summary\n");
    std::printf("  Grid    : %d x %d x %d cells @ %.3f mm = "
                  "%.1f x %.1f x %.1f mm\n",
                  nx, ny, nz, dx_mm,
                  nx * dx * 1e3, ny * dx * 1e3, nz * dx * 1e3);
    std::printf("  Yee arrays bytes : %.1f MB\n", s.bytes() / 1.0e6);
    std::printf("  Time step       : %.3e s (CFL bound at safety 0.99)\n",
                  s.dt());
    std::printf("\nRasterised\n");
    std::printf("  Segments         : %d -> %zu PEC cells\n",
                  r.n_segments_processed, r.segment_pec_cells);
    std::printf("  Zones            : %d -> %zu PEC cells\n",
                  r.n_zones_processed, r.zone_pec_cells);
    std::printf("  Vias             : %d -> %zu PEC cells\n",
                  r.n_vias_processed, r.via_pec_cells);
    std::printf("  Substrate        : %zu cells\n", r.substrate_cells);
    std::printf("  Total PEC cells  : %zu\n", s.pec_cell_count());
    return 0;
}

}  // namespace sikit::cli
