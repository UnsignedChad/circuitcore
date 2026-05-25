// Compares emikit against TI SBAA548A (April 2022) chamber data for
// the ADS8686SEVM-PDK at three SCLK rates. Uses the ground-bounce
// estimator for cable CM current:
//
//     I_cm = (2 * L_gnd / (L_cable_per_m * cable_length)) * I_signal(f)
//
// inputs:
//   * cable length 0.3 m -- PHI USB to host PC
//   * L_cable_per_m 1.0 uH/m -- typical unshielded USB CM mode
//   * L_gnd 15 nH -- mid-range for a real digital board (Ott Ch 11
//     quotes 5-30 nH); EVM route crosses a layer transition and the
//     PHI connector pinout is not optimal for return

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

void run_test(const char* label,
              double sclk_hz,
              double measured_freq_hz,
              double measured_dbuv) {
    auto board = sclk_board(30.0e-3);

    ee::AnalysisConfig cfg;
    cfg.drive.i_peak_a    = 6.0e-3;
    cfg.drive.rise_time_s = 2.0e-9;
    cfg.drive.period_s    = 1.0 / sclk_hz;
    cfg.loop_height_m     = 0.2e-3;
    cfg.test_distance_m   = 10.0;
    cfg.freq_hz           = ee::default_cispr_freq_grid(200);

    ee::CableSpec usb;
    usb.length_m                       = 0.30;
    usb.ground_inductance_h            = 15.0e-9;     // see header comment
    usb.cable_cm_inductance_per_m_h    = 1.0e-6;
    cfg.cables.push_back(usb);

    auto R = ee::analyze_board(board, ee::cispr32_class_a(), cfg);

    // Pull out the envelope value at the measurement frequency.
    double best_df = 1e30;
    double pred_dbuv = -1000.0;
    double pred_i_cm = 0.0;
    for (std::size_t k = 0; k < cfg.freq_hz.size(); ++k) {
        const double df = std::abs(cfg.freq_hz[k] - measured_freq_hz);
        if (df < best_df) {
            best_df = df;
            pred_dbuv = R.worst_case_dbuv[k];
            if (!R.cables.empty()) pred_i_cm = R.cables[0].cm_current_a[k];
        }
    }

    std::printf("\n== %s (SCLK=%.1f MHz) ==\n", label, sclk_hz / 1e6);
    std::printf("  measured chamber:       %6.2f dBuV/m at %.1f MHz\n",
                  measured_dbuv, measured_freq_hz / 1e6);
    std::printf("  emikit total envelope:  %6.2f dBuV/m (gap %+.2f dB)\n",
                  pred_dbuv, pred_dbuv - measured_dbuv);
    std::printf("  estimated I_cm here:    %.2f uA\n",
                  pred_i_cm * 1e6);
}

}  // namespace

int main() {
    std::printf("emikit TI ADS8686S validation -- estimator-driven\n");
    std::printf("--------------------------------------------------\n");
    std::printf("Loop:   30 mm SCLK, 0.15 mm wide, 0.2 mm above GND,\n");
    std::printf("        I = 6 mA peak, rise time 2 ns\n");
    std::printf("Cable:  30 cm USB, L_gnd = 15 nH (estimator-driven)\n");
    std::printf("        I_cm derived from drive spectrum via Hockanson 1996\n");

    run_test("Test 1", 10.0e6, 600.05e6, 34.67);
    run_test("Test 2", 50.0e6, 479.96e6, 54.73);
    run_test("Test 3", 10.0e6, 479.83e6, 51.06);

    return 0;
}
