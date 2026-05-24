#include "HeadlessOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "si/ChannelSynthesis.h"
#include "si/Compliance.h"
#include "si/EyeMask.h"
#include "si/SpiceExport.h"
#include "si/Touchstone.h"
#include "si/TouchstoneWriter.h"
#include "si/TraceImpedance.h"
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
// trace width per net is the v1 simplification, even though a real net
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
    // For v1: report header info and the spec's mask metadata. Full
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

}  // namespace sikit::cli
