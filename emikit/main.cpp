// emikit -- radiated emissions pre-compliance estimator.
//
// Pre-compliance means: a magnetic-dipole estimate of how much
// energy each routed trace radiates against a chosen CISPR / FCC
// mask. Not a chamber sweep -- assume +/- 6 dB versus a real test
// house measurement. Useful to spot the rails that need attention
// while routing is still in progress.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

#include "circuitcore/board/Board.h"
#include "circuitcore/formats/kicad/PcbParser.h"
#include "emi/BoardAnalysis.h"
#include "emi/Masks.h"
#include "emi/Spectrum.h"

namespace {

int list_masks_op() {
    std::printf("Available radiated-emissions masks:\n");
    for (const auto* m : emikit::emi::all_masks()) {
        std::printf("  %-32s  family=%-6s  distance=%.1fm\n",
                      m->name.c_str(), m->family.c_str(),
                      m->test_distance_m);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"emikit -- radiated emissions pre-compliance for KiCad PCBs"};

    auto* check = app.add_subcommand(
        "check", "Estimate per-net emissions and score vs a mask");
    std::string pcb_path;
    std::string mask_name = "CISPR 32 Class B (3 m)";
    std::vector<std::string> nets_filter;
    double clock_hz = 100.0e6;
    double rise_ns  = 1.0;
    double i_peak_ma = 20.0;
    double loop_height_mm = 0.2;
    check->add_option("pcb", pcb_path, ".kicad_pcb file")
        ->required()->check(CLI::ExistingFile);
    check->add_option("--mask", mask_name,
                       "Mask name (see emikit list-masks)");
    check->add_option("--net", nets_filter,
                       "Restrict to nets containing any of these "
                       "substrings (repeatable; default: all routed)")
        ->expected(-1);
    check->add_option("--clock-hz", clock_hz,
                       "Fundamental clock frequency (default 100 MHz)");
    check->add_option("--rise-ns", rise_ns,
                       "TX edge rise/fall time in ns (default 1 ns)");
    check->add_option("--i-peak-ma", i_peak_ma,
                       "Peak current per net in mA (default 20)");
    check->add_option("--loop-height-mm", loop_height_mm,
                       "Vertical distance trace to reference plane "
                       "(default 0.2 mm)");

    auto* list_masks_cmd = app.add_subcommand(
        "list-masks", "List built-in regulatory masks");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (list_masks_cmd->parsed()) return list_masks_op();

    if (check->parsed()) {
        auto board_r = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
        if (!board_r) {
            std::fprintf(stderr, "emikit: parse failed: %s\n",
                          board_r.error().format().c_str());
            return 2;
        }
        const auto* mask = emikit::emi::mask_by_name(mask_name);
        if (!mask) {
            std::fprintf(stderr, "emikit: unknown mask '%s'. Try emikit list-masks\n",
                          mask_name.c_str());
            return 3;
        }

        emikit::emi::AnalysisConfig cfg;
        cfg.drive.i_peak_a = i_peak_ma * 1e-3;
        cfg.drive.period_s = 1.0 / clock_hz;
        cfg.drive.rise_time_s = rise_ns * 1e-9;
        cfg.loop_height_m = loop_height_mm * 1e-3;
        cfg.test_distance_m = mask->test_distance_m;
        cfg.net_filter = nets_filter;
        // Leave cfg.freq_hz empty -> defaults to 30 MHz - 1 GHz log grid.

        auto R = emikit::emi::analyze_board(board_r.value(), *mask, cfg);

        std::printf("Board:   %s\n", pcb_path.c_str());
        std::printf("Mask:    %s\n", mask->name.c_str());
        std::printf("Nets:    %zu evaluated\n", R.nets.size());
        std::printf("Worst:   %s at %.1f MHz, margin %+.1f dB  -> %s\n",
                      R.verdict.worst_net.c_str(),
                      R.verdict.worst_freq_hz / 1e6,
                      R.verdict.worst_margin_db,
                      R.verdict.status == emikit::emi::Verdict::Status::Pass
                          ? "PASS" : "FAIL");
        return R.verdict.status == emikit::emi::Verdict::Status::Pass ? 0 : 1;
    }

    std::printf("%s\n", app.help().c_str());
    return 0;
}
