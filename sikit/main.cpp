// Sikit entry point.
//
// Two modes:
//
//   1. No subcommand (default): launches the Qt GUI. An optional
//      positional KiCad PCB path is opened on startup.
//
//   2. One of the headless subcommands -- "impedance", "touchstone",
//      "spice", "compliance", "list-specs", "list-nets". These run a
//      single analysis pipeline against a board file (or, for
//      compliance and list-specs, no board at all) and print a
//      machine-parseable result to stdout. Designed for shell-script
//      integration: CI sweeps, regression checks, batch SPICE export.
//
// The headless paths never instantiate QApplication, so the binary
// runs cleanly on a headless server with no X11 / Wayland / OpenGL
// available. The Qt library is still linked because the GUI code
// path needs it; if a fully Qt-free build flavour becomes useful that
// is a separate CMake option.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <QApplication>
#include <QSurfaceFormat>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "si/HeadlessOps.h"
#include "MainWindow.h"
#include "circuitcore/formats/kicad/PcbParser.h"
#include "si/SiStackup.h"

namespace {

// Read a .kicad_pcb into a (Board, SiStackup) pair, or print an error and
// return false. Used by every headless subcommand that takes a board.
bool load_board(const std::filesystem::path& pcb_path,
                 circuitcore::board::Board& out_board,
                 sikit::si::SiStackup& out_sis) {
    auto board_result = circuitcore::formats::kicad::PcbParser::parse_file(pcb_path);
    if (!board_result) {
        std::fprintf(stderr, "sikit: failed to parse %s: %s\n",
                      pcb_path.string().c_str(),
                      board_result.error().format().c_str());
        return false;
    }
    out_board = std::move(*board_result);

    auto sis_result = sikit::si::load_si_stackup(pcb_path);
    if (sis_result) {
        out_sis = std::move(*sis_result);
    } else {
        out_sis = {};
        std::fprintf(stderr, "sikit: warning: SI stackup load failed: %s\n",
                      sis_result.error().message.c_str());
    }
    return true;
}

int launch_gui(int argc, char** argv, const std::string& pcb_path) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("sikit");
    QApplication::setApplicationVersion("0.0.1");
    spdlog::info("sikit starting");

    MainWindow w;
    w.show();
    if (!pcb_path.empty()) {
        w.loadKicadPcb(QString::fromStdString(pcb_path));
    }
    return app.exec();
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{
        "sikit -- open-source Signal Integrity analysis for KiCad PCBs"};
    app.allow_extras();   // tolerate Qt's --platform etc.

    // GUI launch is the default. The optional positional opens that
    // file on launch.
    std::string gui_pcb_path;
    app.add_option("--open,pcb", gui_pcb_path,
                    "KiCad .kicad_pcb file to open on startup");

    // -------- impedance --------
    auto* imp_cmd = app.add_subcommand(
        "impedance",
        "Compute trace impedance for a selected net and layer (headless)");
    std::string imp_pcb;
    std::string imp_net;
    std::string imp_layer = "F.Cu";
    bool imp_fdm = false;
    imp_cmd->add_option("pcb", imp_pcb, ".kicad_pcb file")
        ->required()->check(CLI::ExistingFile);
    imp_cmd->add_option("--net", imp_net, "Net name")->required();
    imp_cmd->add_option("--layer", imp_layer,
                         "Layer name (default F.Cu)");
    imp_cmd->add_flag("--fdm", imp_fdm,
                       "Use the in-house FDM solver (default closed-form)");

    // -------- touchstone --------
    auto* ts_cmd = app.add_subcommand(
        "touchstone",
        "Emit a 2-port .s2p for the selected net (headless)");
    std::string ts_pcb;
    std::string ts_net;
    std::string ts_layer = "F.Cu";
    std::string ts_out;
    double ts_flo = 10e6;
    double ts_fhi = 20e9;
    int ts_n = 200;
    bool ts_fdm = false;
    ts_cmd->add_option("pcb", ts_pcb, ".kicad_pcb file")
        ->required()->check(CLI::ExistingFile);
    ts_cmd->add_option("--net", ts_net, "Net name")->required();
    ts_cmd->add_option("--layer", ts_layer, "Layer name");
    ts_cmd->add_option("-o,--out", ts_out, "Output .s2p path")->required();
    ts_cmd->add_option("--f-lo", ts_flo, "Frequency lower bound Hz");
    ts_cmd->add_option("--f-hi", ts_fhi, "Frequency upper bound Hz");
    ts_cmd->add_option("-n,--n-points", ts_n, "Number of freq samples");
    ts_cmd->add_flag("--fdm", ts_fdm, "Use the in-house FDM solver");

    // -------- spice --------
    auto* spice_cmd = app.add_subcommand(
        "spice",
        "Synthesise + vector-fit + emit a SPICE-3 subcircuit for "
        "the selected net (headless)");
    std::string sp_pcb;
    std::string sp_net;
    std::string sp_layer = "F.Cu";
    std::string sp_out;
    int sp_poles = 12;
    double sp_flo = 10e6;
    double sp_fhi = 20e9;
    int sp_n = 200;
    bool sp_fdm = false;
    spice_cmd->add_option("pcb", sp_pcb, ".kicad_pcb file")
        ->required()->check(CLI::ExistingFile);
    spice_cmd->add_option("--net", sp_net, "Net name")->required();
    spice_cmd->add_option("--layer", sp_layer, "Layer name");
    spice_cmd->add_option("-o,--out", sp_out, "Output .cir path")
        ->required();
    spice_cmd->add_option("--n-poles", sp_poles,
                           "Number of vector-fit poles");
    spice_cmd->add_option("--f-lo", sp_flo, "Frequency lower bound Hz");
    spice_cmd->add_option("--f-hi", sp_fhi, "Frequency upper bound Hz");
    spice_cmd->add_option("-n,--n-points", sp_n,
                           "Number of synth freq samples");
    spice_cmd->add_flag("--fdm", sp_fdm, "Use the in-house FDM solver");

    // -------- compliance --------
    auto* comp_cmd = app.add_subcommand(
        "compliance",
        "Cross-check a Touchstone file against a named compliance "
        "spec (headless)");
    std::string comp_in;
    std::string comp_spec;
    comp_cmd->add_option("-i,--in", comp_in, "Touchstone file")
        ->required()->check(CLI::ExistingFile);
    comp_cmd->add_option("--spec", comp_spec,
                          "Compliance spec name (see list-specs)")
        ->required();

    // -------- deembed --------
    auto* deemb_cmd = app.add_subcommand(
        "deembed",
        "Strip a symmetric fixture from a measured Touchstone (headless)");
    std::string deemb_in, deemb_fix, deemb_out;
    deemb_cmd->add_option("-i,--in", deemb_in, "Measured Touchstone")
        ->required()->check(CLI::ExistingFile);
    deemb_cmd->add_option("--fixture", deemb_fix, "Fixture Touchstone (same on both sides)")
        ->required()->check(CLI::ExistingFile);
    deemb_cmd->add_option("-o,--out", deemb_out, "Output DUT Touchstone path")
        ->required();

    // -------- compare --------
    auto* cmp_cmd = app.add_subcommand(
        "compare",
        "Compare two Touchstone files; print max |dB| delta + PASS/FAIL");
    std::string cmp_a, cmp_b;
    int cmp_idx = 1;
    double cmp_budget = 1.0;
    cmp_cmd->add_option("-a", cmp_a, "First Touchstone (e.g. measured)")
        ->required()->check(CLI::ExistingFile);
    cmp_cmd->add_option("-b", cmp_b, "Second Touchstone (e.g. simulated)")
        ->required()->check(CLI::ExistingFile);
    cmp_cmd->add_option("--index", cmp_idx,
                          "S-parameter index (default 1 = S21 of 2-port)");
    cmp_cmd->add_option("--max-db", cmp_budget,
                          "|dB| delta budget (default 1.0)");

    // -------- skew --------
    auto* skew_cmd = app.add_subcommand(
        "skew",
        "Diff-pair length skew report (headless)");
    std::string skew_pcb;
    double skew_budget = 5.0;
    skew_cmd->add_option("pcb", skew_pcb, ".kicad_pcb file")
        ->required()->check(CLI::ExistingFile);
    skew_cmd->add_option("--budget", skew_budget,
                          "Skew budget in ps (default 5)");

    // -------- list-specs --------
    auto* list_specs_cmd = app.add_subcommand(
        "list-specs", "List all built-in compliance specifications");

    // -------- list-nets --------
    auto* list_nets_cmd = app.add_subcommand(
        "list-nets", "List nets defined in a .kicad_pcb");
    std::string list_nets_pcb;
    list_nets_cmd->add_option("pcb", list_nets_pcb, ".kicad_pcb file")
        ->required()->check(CLI::ExistingFile);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Dispatch.
    if (imp_cmd->parsed()) {
        circuitcore::board::Board b; sikit::si::SiStackup sis;
        if (!load_board(imp_pcb, b, sis)) return 2;
        return sikit::cli::impedance_op(b, sis, imp_net, imp_layer, imp_fdm);
    }
    if (ts_cmd->parsed()) {
        circuitcore::board::Board b; sikit::si::SiStackup sis;
        if (!load_board(ts_pcb, b, sis)) return 2;
        return sikit::cli::touchstone_op(
            b, sis, ts_net, ts_layer, ts_out, ts_flo, ts_fhi, ts_n, ts_fdm);
    }
    if (spice_cmd->parsed()) {
        circuitcore::board::Board b; sikit::si::SiStackup sis;
        if (!load_board(sp_pcb, b, sis)) return 2;
        return sikit::cli::spice_op(
            b, sis, sp_net, sp_layer, sp_out, sp_poles, sp_flo, sp_fhi,
            sp_n, sp_fdm);
    }
    if (comp_cmd->parsed()) {
        return sikit::cli::compliance_op(comp_in, comp_spec);
    }
    if (deemb_cmd->parsed()) {
        return sikit::cli::deembed_op(deemb_in, deemb_fix, deemb_out);
    }
    if (cmp_cmd->parsed()) {
        return sikit::cli::compare_op(cmp_a, cmp_b, cmp_idx, cmp_budget);
    }
    if (skew_cmd->parsed()) {
        circuitcore::board::Board b; sikit::si::SiStackup sis;
        if (!load_board(skew_pcb, b, sis)) return 2;
        return sikit::cli::skew_op(b, sis, skew_budget);
    }
    if (list_specs_cmd->parsed()) {
        return sikit::cli::list_specs_op();
    }
    if (list_nets_cmd->parsed()) {
        circuitcore::board::Board b; sikit::si::SiStackup sis;
        if (!load_board(list_nets_pcb, b, sis)) return 2;
        return sikit::cli::list_nets_op(b);
    }

    // No subcommand: launch GUI.
    return launch_gui(argc, argv, gui_pcb_path);
}
