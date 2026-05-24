// Closed-form Ohm's law verification.
//
// trace_100mm.kicad_pcb is a 100mm × 10mm × 35μm (1 oz) copper rectangle
// with two pads on the VRAIL net at x=5mm and x=95mm. The pads end up as
// the source and sink for static IR drop.
//
// For a uniform-current rectangular sheet conductor of length L (between
// the pads), width W, thickness t, made of copper (ρ = 1.68e-8 Ω·m):
//
//      R_ideal = ρ · L / (W · t)
//      V_ideal = I · R_ideal
//
// Numbers: L = 90mm, W = 10mm, t = 35μm, I = 1A
//      R_ideal ≈ 4.32 mΩ
//      V_ideal ≈ 4.32 mV
//
// pdnkit's solver mesh-discretizes this, so the result has two real
// physical residuals on top of the ideal:
//   * Spreading resistance at each pad-to-trace contact (we drive point
//     loads, not uniform line contacts) -- typically 0.3-1 mΩ per end
//     on this geometry.
//   * Discretization error of the grid mesh -- shrinks with cell_size.
//
// So we expect the actual mesh result to be 4-7 mV, conservatively
// within ±50% of the ideal. Tightening tolerance once we add edge-
// contact source/sink support is a follow-up.

#include <cmath>
#include <numeric>
#include <filesystem>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/formats/kicad/PcbParser.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "render/IrResultMesh.h"

#ifndef PDNKIT_TEST_FIXTURES_DIR
#error "PDNKIT_TEST_FIXTURES_DIR must be defined by CMake."
#endif

using circuitcore::formats::kicad::PcbParser;
using pdnkit::pi::IrMesher;
using pdnkit::pi::IrSolver;
using pdnkit::pi::MeshConfig;

namespace {
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(PDNKIT_TEST_FIXTURES_DIR) / name;
}

constexpr double kRhoCu        = 1.68e-8;   // Ω·m at 20°C
constexpr double kCuThickness  = 35.0e-6;   // 1 oz
constexpr double kTraceWidth   = 0.010;     // 10 mm
constexpr double kPadDistance  = 0.090;     // 90 mm between pad centers (5 → 95)
constexpr double kCurrent      = 1.0;       // A
}

TEST_CASE("ohms-law: 100mm trace solves close to ρL/(Wt)", "[ohms][validation]") {
    auto b = PcbParser::parse_file(fixture("trace_100mm.kicad_pcb")).value();

    REQUIRE(b.find_net_by_name("VRAIL") != nullptr);
    REQUIRE(b.zones.size() == 1);
    REQUIRE(b.pads.size() == 2);

    MeshConfig mc;
    mc.cell_size = 0.5e-3;  // 0.5 mm — gives ~200x20 = 4000 cells
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;

    auto mesh = IrMesher::build(b, mc);
    REQUIRE(mesh.nodes.size() > 1000);
    REQUIRE(mesh.source_node_ids.size() == 1);
    REQUIRE(mesh.sink_node_ids.size() == 1);

    auto sol = IrSolver::solve(mesh, {kCurrent});
    REQUIRE(sol.ok);

    const double v_drop_v = sol.max_v - sol.min_v;
    const double v_drop_mv = v_drop_v * 1000.0;

    const double r_ideal = kRhoCu * kPadDistance / (kTraceWidth * kCuThickness);
    const double v_ideal_mv = (kCurrent * r_ideal) * 1000.0;

    INFO("V_ideal (point-load excluded) = " << v_ideal_mv << " mV");
    INFO("V_drop (pdnkit mesh)          = " << v_drop_mv << " mV");
    INFO("ratio = " << v_drop_mv / v_ideal_mv);

    // Within +/-50% of the closed-form value (mesh + point-load spreading).
    REQUIRE(v_drop_mv >= 0.5 * v_ideal_mv);
    REQUIRE(v_drop_mv <= 1.5 * v_ideal_mv);

    // Should be within ~2x at worst — guard against gross math regressions.
    REQUIRE(v_drop_mv >= 0.5 * v_ideal_mv);
    REQUIRE(v_drop_mv <= 2.0 * v_ideal_mv);
}

TEST_CASE("ohms-law: drop scales linearly with current", "[ohms][validation]") {
    auto b = PcbParser::parse_file(fixture("trace_100mm.kicad_pcb")).value();
    MeshConfig mc;
    mc.cell_size = 0.5e-3;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;

    auto mesh = IrMesher::build(b, mc);

    auto s1 = IrSolver::solve(mesh, {1.0});
    auto s3 = IrSolver::solve(mesh, {3.0});
    REQUIRE(s1.ok);
    REQUIRE(s3.ok);

    const double r1 = s1.max_v - s1.min_v;
    const double r3 = s3.max_v - s3.min_v;
    // Should be exactly 3:1 (linear system).
    REQUIRE(r3 / r1 == Catch::Approx(3.0).epsilon(1e-9));
}

TEST_CASE("ohms-law: result tightens as cell size shrinks", "[ohms][validation]") {
    auto b = PcbParser::parse_file(fixture("trace_100mm.kicad_pcb")).value();
    const double r_ideal = kRhoCu * kPadDistance / (kTraceWidth * kCuThickness);

    auto solve_at = [&](double cell_size_m) {
        MeshConfig mc;
        mc.cell_size = cell_size_m;
        mc.net_id = b.find_net_by_name("VRAIL")->id;
        mc.layer_ordinal = 0;
        auto mesh = IrMesher::build(b, mc);
        auto sol = IrSolver::solve(mesh, {1.0});
        REQUIRE(sol.ok);
        return sol.max_v - sol.min_v;
    };

    const double v_coarse = solve_at(1.0e-3);   // 1.0mm
    const double v_fine   = solve_at(0.25e-3);  // 0.25mm

    INFO("R_ideal = " << r_ideal * 1000.0 << " mΩ");
    INFO("V at 1.0mm  = " << v_coarse * 1000.0 << " mV");
    INFO("V at 0.25mm = " << v_fine   * 1000.0 << " mV");

    // Both within ±50% of ideal.
    REQUIRE(v_coarse >= 0.5 * r_ideal);
    REQUIRE(v_coarse <= 1.5 * r_ideal);
    REQUIRE(v_fine   >= 0.5 * r_ideal);
    REQUIRE(v_fine   <= 1.5 * r_ideal);

    // With edge-contact source/sink (gap #3) the mesh converges TOWARD the
    // analytical R_ideal as cells shrink. Finer mesh must be at least as
    // close to ideal as the coarse mesh.
    const double err_coarse = std::abs(v_coarse - r_ideal);
    const double err_fine   = std::abs(v_fine   - r_ideal);
    REQUIRE(err_fine <= err_coarse);
    // And the 0.25mm result must hit within 5%.
    REQUIRE(err_fine / r_ideal < 0.05);
}


// Validation for the GUI right-click probe-R workflow: the mesher must
// honor explicit source/sink pad *indices* and produce the same V drop
// as name-based picking does. Anchors the new MeshConfig path against
// the existing one on a fixture where both should agree.
TEST_CASE("ohms-law: source/sink_pad_indices match name-based picking",
          "[ohms][validation][probe-r]") {
    auto b = PcbParser::parse_file(fixture("trace_100mm.kicad_pcb")).value();
    REQUIRE(b.pads.size() == 2);

    const int net_id = b.find_net_by_name("VRAIL")->id;

    // Reference: default mesher (auto-pick leftmost/rightmost pads).
    MeshConfig mc_ref;
    mc_ref.cell_size = 0.5e-3;
    mc_ref.net_id = net_id;
    mc_ref.layer_ordinal = 0;
    mc_ref.copper_thickness = kCuThickness;
    mc_ref.copper_rho = kRhoCu;
    auto mesh_ref = IrMesher::build(b, mc_ref);
    auto sol_ref = IrSolver::solve(mesh_ref, {kCurrent});
    REQUIRE(sol_ref.ok);
    const double v_ref = sol_ref.max_v - sol_ref.min_v;

    // Same config, but pick the pads explicitly by index (as the GUI
    // right-click workflow does).
    MeshConfig mc_idx = mc_ref;
    mc_idx.source_pad_indices = {0};
    mc_idx.sink_pad_indices   = {1};
    auto mesh_idx = IrMesher::build(b, mc_idx);
    REQUIRE(!mesh_idx.source_node_ids.empty());
    REQUIRE(!mesh_idx.sink_node_ids.empty());
    auto sol_idx = IrSolver::solve(mesh_idx, {kCurrent});
    REQUIRE(sol_idx.ok);
    const double v_idx = sol_idx.max_v - sol_idx.min_v;

    INFO("V (auto-pick)  = " << v_ref * 1000.0 << " mV");
    INFO("V (by-index)   = " << v_idx * 1000.0 << " mV");
    // Identical pads picked by either path -> identical solver result
    // (mesh layout is identical too). Allow a tiny numerical fuzz.
    REQUIRE(std::abs(v_idx - v_ref) / v_ref < 1.0e-6);

    // And swapping source/sink just flips the sign of the gradient ->
    // the |V drop| is unchanged.
    MeshConfig mc_swap = mc_ref;
    mc_swap.source_pad_indices = {1};
    mc_swap.sink_pad_indices   = {0};
    auto mesh_swap = IrMesher::build(b, mc_swap);
    auto sol_swap = IrSolver::solve(mesh_swap, {kCurrent});
    REQUIRE(sol_swap.ok);
    const double v_swap = sol_swap.max_v - sol_swap.min_v;
    REQUIRE(std::abs(v_swap - v_ref) / v_ref < 1.0e-6);
}


// Current-density heat-map: on the trace_100mm fixture, the analytical
// answer is uniform K_sheet = I / W in the middle of the trace, where W
// is the trace width. The mesh's middle cells must hit that value within
// 10% once you exclude the source/sink edge-spreading regions.
TEST_CASE("ohms-law: current-density heat-map matches I/W in mid-trace",
          "[ohms][validation][current-density]") {
    auto b = PcbParser::parse_file(fixture("trace_100mm.kicad_pcb")).value();

    MeshConfig mc;
    mc.cell_size = 0.5e-3;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;
    auto mesh = IrMesher::build(b, mc);
    REQUIRE(!mesh.nodes.empty());

    auto sol = IrSolver::solve(mesh, {kCurrent});
    REQUIRE(sol.ok);

    auto rm = pdnkit::render::build_current_density_mesh(
        mesh, sol, mc.cell_size, kCuThickness, kRhoCu);
    REQUIRE(!rm.vertices.empty());
    REQUIRE(rm.v_max > rm.v_min);

    // Recompute |J| ourselves so we can pick middle-of-trace nodes.
    // (The render mesh discards which node each quad came from -- we
    // re-derive instead of cracking the interleaved vertex array.)
    // For the trace, dV/dx is the dominant gradient; central diff.
    // Build (i,j) -> nid map.
    std::unordered_map<long long, int> ij_to_nid;
    for (std::size_t k = 0; k < mesh.nodes.size(); ++k) {
        const auto& n = mesh.nodes[k];
        const long long key = (static_cast<long long>(n.grid_i) << 32) |
                              (static_cast<unsigned>(n.grid_j));
        ij_to_nid[key] = static_cast<int>(k);
    }
    // Pad centers in the fixture are at x = 5 mm and 95 mm; mid-trace is
    // x ~ 50 mm. Find nodes near that x and in the middle of the trace's y
    // span. Compute |J_x| = (V_left - V_right) / (2*dx) * t / rho.
    const double sigma_sheet = kCuThickness / kRhoCu;
    std::vector<double> mid_J;
    for (const auto& n : mesh.nodes) {
        if (std::abs(n.x - 0.050) > 5e-3) continue;  // within +/-5mm of middle
        // central diff in x
        const long long kp = (static_cast<long long>(n.grid_i + 1) << 32) |
                             (static_cast<unsigned>(n.grid_j));
        const long long km = (static_cast<long long>(n.grid_i - 1) << 32) |
                             (static_cast<unsigned>(n.grid_j));
        auto itp = ij_to_nid.find(kp);
        auto itm = ij_to_nid.find(km);
        if (itp == ij_to_nid.end() || itm == ij_to_nid.end()) continue;
        const double dv = sol.voltages[itp->second] -
                          sol.voltages[itm->second];
        const double grad_x = dv / (2.0 * mc.cell_size);
        const double Jx = std::abs(sigma_sheet * grad_x);
        mid_J.push_back(Jx);
    }
    REQUIRE(mid_J.size() > 10);

    // Analytical: K = I / W (uniform across the cross-section).
    const double K_ideal = kCurrent / kTraceWidth;  // A/m

    const double K_mean = std::accumulate(mid_J.begin(), mid_J.end(), 0.0) /
                          static_cast<double>(mid_J.size());
    INFO("K_ideal (I/W) = " << K_ideal << " A/m");
    INFO("K_mean (mid)  = " << K_mean << " A/m");
    INFO("|J| min/max in render mesh = "
         << rm.v_min << " / " << rm.v_max << " A/m");

    // 10% on the mean is plenty -- this is the spatial average over the
    // uniform-flow region of the trace.
    REQUIRE(std::abs(K_mean - K_ideal) / K_ideal < 0.10);
}
