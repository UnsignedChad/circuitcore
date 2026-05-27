// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Run emikit against TI's published ADS8686SEVM-PDK chamber data
// (SBAA548A, April 2022). With the cable common-mode model added,
// see if predicted emissions land within pre-compliance accuracy of
// the chamber numbers across all three test conditions.
//
// Two contributions are summed in power:
//   1. Differential-mode loop radiation from the SCLK trace (existing
//      LoopEmissions code; small-loop magnetic-dipole far-field).
//   2. Common-mode radiation from the USB cable connecting the PHI
//      controller to the host PC (new CableCommonMode; short electric
//      dipole over a ground plane, per Hockanson 1996).
//
// 10 uA cable CM applied to all three tests (estimate -- comes from
// a few mV ground bounce / ~200 ohm typical cable CM impedance).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "circuitcore/board/Board.h"
#include "emi/BoardAnalysis.h"
#include "emi/CableCommonMode.h"
#include "emi/Masks.h"

namespace cb = circuitcore::board;
namespace ee = emikit::emi;

namespace {

cb::Board sclk_board(double length_m) {
    cb::Board b;
    b.nets.push_back({1, "SCLK"});
    cb::Segment s;
    s.start = {0.0, 0.0};
    s.end   = {length_m, 0.0};
    s.width = 0.15e-3;
    s.layer_ordinal = 0;
    s.net_id = 1;
    b.segments.push_back(s);
    return b;
}

// Power-sum two dBuV/m contributions: E_total^2 = E_loop^2 + E_cable^2.
double sum_dbuv(double a_dbuv, double b_dbuv) {
    const double a_v = std::pow(10.0, a_dbuv / 20.0);
    const double b_v = std::pow(10.0, b_dbuv / 20.0);
    const double s   = std::sqrt(a_v * a_v + b_v * b_v);
    return 20.0 * std::log10(s);
}

void run_test(const char* label,
              double sclk_hz,
              double measured_freq_hz,
              double measured_dbuv,
              double cable_length_m,
              double cm_current_a) {
    auto board = sclk_board(30.0e-3);

    ee::AnalysisConfig cfg;
    cfg.drive.i_peak_a    = 6.0e-3;
    cfg.drive.rise_time_s = 2.0e-9;
    cfg.drive.period_s    = 1.0 / sclk_hz;
    cfg.loop_height_m     = 0.2e-3;
    cfg.test_distance_m   = 10.0;
    cfg.freq_hz           = ee::default_cispr_freq_grid(200);

    auto R = ee::analyze_board(board, ee::cispr32_class_a(), cfg);

    // Find the analyzer's predicted loop-only value at the chamber's
    // measured peak frequency.
    double best_df = 1e30;
    double loop_dbuv_at_peak = -1000.0;
    for (std::size_t k = 0; k < cfg.freq_hz.size(); ++k) {
        const double df = std::abs(cfg.freq_hz[k] - measured_freq_hz);
        if (df < best_df) {
            best_df = df;
            loop_dbuv_at_peak = R.worst_case_dbuv[k];
        }
    }

    // Cable contribution at the same frequency.
    const ee::CableSpec cable{cable_length_m, cm_current_a};
    const double cable_dbuv_at_peak =
        ee::cable_cm_e_field_dbuv(cable, measured_freq_hz, cfg.test_distance_m);

    // Combined.
    const double total_dbuv = sum_dbuv(loop_dbuv_at_peak, cable_dbuv_at_peak);

    std::printf("\n== %s (SCLK=%.1f MHz) ==\n", label, sclk_hz / 1e6);
    std::printf("  measured chamber:       %6.2f dBuV/m at %.1f MHz\n",
                  measured_dbuv, measured_freq_hz / 1e6);
    std::printf("  emikit loop only:       %6.2f dBuV/m (gap %+.2f dB)\n",
                  loop_dbuv_at_peak, loop_dbuv_at_peak - measured_dbuv);
    std::printf("  cable CM (%4.1f uA):     %6.2f dBuV/m\n",
                  cm_current_a * 1e6, cable_dbuv_at_peak);
    std::printf("  combined (power-sum):   %6.2f dBuV/m (gap %+.2f dB)\n",
                  total_dbuv, total_dbuv - measured_dbuv);
}

}  // namespace

int main() {
    std::printf("emikit TI ADS8686S validation -- loop + cable CM\n");
    std::printf("------------------------------------------------\n");
    std::printf("Reference: TI SBAA548A 'EMC Compliance Testing for "
                  "Precision ADC Systems'\n");
    std::printf("Loop:      30 mm SCLK trace, 0.15 mm wide, 0.2 mm above GND,\n");
    std::printf("           I = 6 mA peak, rise time 2 ns\n");
    std::printf("Cable:     30 cm USB cable, CM current 10 uA assumed\n");
    std::printf("           (single value across all three tests)\n");

    // 30 cm USB cable. I_cm ~10 uA from a few mV ground bounce over
    // ~200 ohm typical CM impedance. one value for all three tests.
    const double cable_L  = 0.30;
    const double cable_I  = 10.0e-6;

    run_test("Test 1", 10.0e6, 600.05e6, 34.67, cable_L, cable_I);
    run_test("Test 2", 50.0e6, 479.96e6, 54.73, cable_L, cable_I);
    run_test("Test 3", 10.0e6, 479.83e6, 51.06, cable_L, cable_I);

    return 0;
}
