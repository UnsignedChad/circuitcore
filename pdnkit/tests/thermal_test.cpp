#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/formats/kicad/PcbParser.h"
#include "pi/Thermal.h"

using namespace pdnkit::pi;
using namespace circuitcore::board;
using circuitcore::formats::kicad::PcbParser;
using Catch::Approx;

#ifndef PDNKIT_TEST_FIXTURES_DIR
#define PDNKIT_TEST_FIXTURES_DIR "tests/fixtures"
#endif

namespace {
constexpr double kCuThickness = 35.0e-6;
constexpr double kRhoCu       = 1.68e-8;
constexpr double kCurrent     = 5.0;       // amps -- enough to heat
constexpr double kCellSize    = 0.5e-3;
}

// Trace_100mm at 5A with R_theta = 100 K/W. Closed-form expectation:
//   R20 ~ rho*L/(W*t) = 1.68e-8 * 0.1 / (0.01 * 35e-6) = 4.8 mOhm
//   P   = I^2 * R = 25 * 4.8e-3 = 0.12 W
//   dT  = P * R_theta = 12 C
//   alpha*dT = 0.0472 -> 4.7% R increase, settle a bit higher with
//   iteration to ~5%. Final dT ends up in the 12-14 C band depending
//   on convergence.
TEST_CASE("thermal: trace_100mm at 5A heats ~12 C with R_theta=100",
          "[thermal][validation]") {
    auto b = PcbParser::parse_file(
        std::string(PDNKIT_TEST_FIXTURES_DIR) + "/trace_100mm.kicad_pcb").value();

    MeshConfig mc;
    mc.cell_size = kCellSize;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;

    SolveConfig sc;
    sc.total_current = kCurrent;

    ThermalConfig tc;
    tc.r_theta_total_kw = 100.0;
    tc.max_iterations = 15;

    auto r = solve_ir_with_thermal(b, mc, sc, tc);
    REQUIRE(r.converged);
    REQUIRE(r.iterations >= 2);

    INFO("dT       = " << r.final_delta_t_c << " C");
    INFO("rho_final= " << r.final_rho << " ohm*m  (rho_20 = " << kRhoCu << ")");
    INFO("power    = " << r.final_power_w * 1000.0 << " mW");
    INFO("iterations= " << r.iterations);

    REQUIRE(r.final_delta_t_c > 10.0);     // not below the simple estimate
    REQUIRE(r.final_delta_t_c < 18.0);     // not absurdly above either
    // Final rho must be elevated above the 20 C value.
    REQUIRE(r.final_rho > kRhoCu * 1.03);
    REQUIRE(r.final_rho < kRhoCu * 1.08);
}

// Lower R_theta -> lower temp rise -> less R inflation.
TEST_CASE("thermal: bigger heatsink (lower R_theta) means less rise",
          "[thermal]") {
    auto b = PcbParser::parse_file(
        std::string(PDNKIT_TEST_FIXTURES_DIR) + "/trace_100mm.kicad_pcb").value();

    MeshConfig mc;
    mc.cell_size = kCellSize;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;

    SolveConfig sc; sc.total_current = kCurrent;

    ThermalConfig tc_hot{ .r_theta_total_kw = 200.0 };
    ThermalConfig tc_cold{ .r_theta_total_kw = 20.0 };

    auto hot  = solve_ir_with_thermal(b, mc, sc, tc_hot);
    auto cold = solve_ir_with_thermal(b, mc, sc, tc_cold);
    REQUIRE(hot.final_delta_t_c > cold.final_delta_t_c);
    REQUIRE(hot.final_rho > cold.final_rho);
}

// Zero current: no heating, dT == 0, rho unchanged.
TEST_CASE("thermal: no current means no heating", "[thermal]") {
    auto b = PcbParser::parse_file(
        std::string(PDNKIT_TEST_FIXTURES_DIR) + "/trace_100mm.kicad_pcb").value();

    MeshConfig mc;
    mc.cell_size = kCellSize;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;

    SolveConfig sc; sc.total_current = 0.0;
    ThermalConfig tc;
    auto r = solve_ir_with_thermal(b, mc, sc, tc);
    REQUIRE(r.converged);
    REQUIRE(r.final_delta_t_c == Approx(0.0).margin(0.01));
    // rho is at ambient (25 C), not the 20 C reference -- expect a
    // small alpha*(T_amb - 20) bump even with zero current.
    const double rho_amb = kRhoCu * (1.0 + tc.alpha_per_c * (tc.t_ambient_c - 20.0));
    REQUIRE(r.final_rho == Approx(rho_amb));
}

// Convergence: with reasonable tolerance, iteration count is small.
TEST_CASE("thermal: convergence in a handful of iterations", "[thermal]") {
    auto b = PcbParser::parse_file(
        std::string(PDNKIT_TEST_FIXTURES_DIR) + "/trace_100mm.kicad_pcb").value();

    MeshConfig mc;
    mc.cell_size = kCellSize;
    mc.net_id = b.find_net_by_name("VRAIL")->id;
    mc.layer_ordinal = 0;
    mc.copper_thickness = kCuThickness;
    mc.copper_rho = kRhoCu;

    SolveConfig sc; sc.total_current = kCurrent;
    ThermalConfig tc;
    auto r = solve_ir_with_thermal(b, mc, sc, tc);
    REQUIRE(r.converged);
    REQUIRE(r.iterations <= 6);
}
