#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "circuitcore/formats/kicad/PcbParser.h"
#include "pi/CavityModel.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "pi/Mor.h"
#include "pi/PowerDrc.h"
#include "pi/Sensitivity.h"
#include "pi/SpiceExport.h"
#include "pi/TargetZ.h"
#include "pi/Thermal.h"
#include "pi/Touchstone.h"
#include "pi/ViaInductance.h"
#include "pi/Vrm.h"
#include "pi/Roughness.h"
#include "pi/Dielectric.h"

using namespace pdnkit::pi;
using namespace circuitcore::board;
using circuitcore::formats::kicad::PcbParser;
using Catch::Approx;

#ifndef PDNKIT_TEST_FIXTURES_DIR
#define PDNKIT_TEST_FIXTURES_DIR "tests/fixtures"
#endif

// End-to-end pipeline: every Tier 1 and Tier 2 module exercised against
// one real fixture. Locks in that cross-tool refactors (parser, model,
// solver) do not silently break downstream physics modules.
TEST_CASE("pipeline: full Tier1+Tier2 sweep on tiny_pdn fixture",
          "[pipeline][validation]") {
    auto board = PcbParser::parse_file(
        std::string(PDNKIT_TEST_FIXTURES_DIR) + "/tiny_pdn.kicad_pcb").value();

    REQUIRE(!board.nets.empty());
    REQUIRE(!board.zones.empty());
    REQUIRE(!board.stackup.layers.empty());

    const auto* net_3v3 = board.find_net_by_name("+3V3");
    REQUIRE(net_3v3 != nullptr);

    // --- IR drop ---
    MeshConfig mc;
    mc.cell_size = 0.5e-3;
    mc.net_id = net_3v3->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = 35.0e-6;
    mc.copper_rho = 1.68e-8;
    auto mesh = IrMesher::build(board, mc);
    REQUIRE(!mesh.nodes.empty());

    auto sol = IrSolver::solve(mesh, SolveConfig{});
    REQUIRE(sol.ok);
    REQUIRE(sol.voltages.size() == mesh.nodes.size());

    // --- DRC ---
    DrcRule rule;
    rule.net_id = net_3v3->id;
    rule.current_amps = 0.5;
    rule.temp_rise_c = 10.0;
    auto drc = check_ipc2152(board, {rule});
    REQUIRE(drc.segments_checked >= 0);  // some segments may be checked

    // --- Cavity Z(f) bare + with decaps ---
    CavityConfig cfg;
    cfg.a = 0.050;
    cfg.b = 0.050;
    cfg.d = 1.6e-3;
    cfg.eps_r = 4.3;
    cfg.tan_delta = 0.020;
    cfg.max_modes = 8;
    auto z_bare = cavity_impedance(cfg, 0.020, 0.020, 0.025, 0.025,
                                   2.0 * 3.14159265358979 * 1.0e8);
    REQUIRE(std::isfinite(z_bare.real()));
    REQUIRE(std::isfinite(z_bare.imag()));

    std::vector<Decap> decaps = {
        {0.020, 0.020, 1.0e-6, 5.0e-3, 0.5e-9, 0.0},
        {0.030, 0.030, 100.0e-9, 30.0e-3, 0.3e-9, 0.0}
    };
    auto z_with = cavity_impedance_with_decaps(cfg, 0.025, 0.025, decaps,
                                                2.0 * 3.14159265358979 * 1.0e8);
    REQUIRE(std::isfinite(z_with.real()));

    // --- Sensitivity ---
    std::vector<double> freqs = {1.0e6, 1.0e7, 1.0e8, 1.0e9};
    auto sens = sensitivity_sweep(cfg, 0.025, 0.025, decaps, freqs);
    REQUIRE(sens.size() == decaps.size());

    // --- Target Z ---
    TargetZSpec tspec{3.3, 0.05, 1.0};
    const double z_target = target_impedance_flat(tspec);
    REQUIRE(z_target == Approx(0.165));

    // --- Via inductance + VRM + Dielectric + Roughness helpers ---
    REQUIRE(via_self_inductance(0.15e-3, 1.6e-3) > 500.0e-12);
    auto z_vrm = vrm_impedance({5.0e-3, 1.0e-6}, 2.0 * 3.14159265358979 * 1.0e6);
    REQUIRE(std::abs(z_vrm) > 0.0);
    auto eps = dj_sarkar_at({3.8, 1.0, 1.0e3, 1.0e9}, 1.0e6);
    REQUIRE(eps.eps_r_real > 4.0);   // FR-4 at 1 MHz ~ 4.3
    REQUIRE(hj_roughness_multiplier(1.0e-6, 1.0e10) > 1.5);

    // --- Thermal ---
    SolveConfig sc; sc.total_current = 1.0;
    ThermalConfig tc;
    auto th = solve_ir_with_thermal(board, mc, sc, tc);
    REQUIRE(th.solution.ok);
    REQUIRE(th.iterations >= 1);

    // --- MOR ---
    if (!mesh.source_node_ids.empty() && !mesh.sink_node_ids.empty()) {
        std::vector<int> ports = mesh.source_node_ids;
        for (int id : mesh.sink_node_ids) ports.push_back(id);
        auto reduced = reduce_to_ports(mesh, ports);
        REQUIRE(reduced.port_node_ids.size() == ports.size());
        auto netlist = export_reduced_spice(reduced, "pipeline test");
        REQUIRE(netlist.find(".end") != std::string::npos);
    }

    // --- SPICE export + Touchstone ---
    auto spice = export_spice(mesh, SpiceExportConfig{});
    REQUIRE(spice.find(".end") != std::string::npos);

    std::vector<TouchstoneSample> ts = {{1.0e6, {0.05, -0.10}},
                                         {1.0e9, {0.50, 0.20}}};
    auto path = std::filesystem::temp_directory_path() / "pipeline.s1p";
    REQUIRE(write_touchstone_z1p(path, ts, "pipeline test"));
    std::filesystem::remove(path);

}
