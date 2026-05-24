// Run emikit against a reconstruction of the TI ADS8686S EMC test board
// and compare the predicted E-field envelope to the chamber measurements
// published in TI app note SBAA548A (April 2022, rev May 2022).
//
// TI's chamber data, summarized:
//   Test 1: SCLK = 10 MHz, peak emission at 600.05 MHz, margin 22.83 dB
//           under the CISPR 11 Class A 57.5 dBuV/m line -> 34.67 dBuV/m.
//   Test 2: SCLK = 50 MHz, peak emission at 479.96 MHz, margin  2.77 dB
//           -> 54.73 dBuV/m.
//   Test 3: SCLK = 10 MHz, peak emission at 479.83 MHz, margin  6.44 dB
//           -> 51.06 dBuV/m.
//
// Geometry was estimated from the EVM layout figures in TI SBAU319
// (ADS8686SEVM-PDK User's Guide, Section 7.2, Figures 21-25):
//   * 4-layer FR-4 stackup, GND solid on Layer 2 below the top signal layer.
//   * SCLK trace ~30 mm from the PHI controller connector to the ADC pin
//     (midpoint estimate; visible route on Layer 1 spans 25-40 mm).
//   * Top-to-L2 dielectric ~0.2 mm prepreg (typical for TI 4-layer EVMs).
//   * Trace width ~0.15 mm.
//
// Drive parameters were estimated from the PHI controller's MSP430-class
// 3.3V CMOS output stage:
//   * I_peak ~ 6 mA (8 mA pin drive strength derated for the actual edge).
//   * Rise/fall time ~ 2 ns nominal.
//
// This is a reconstructed approximation -- 0.1 mm of trace length or a
// changed via stitching pattern can shift the peak by a few dB. Aim is
// order-of-magnitude agreement, not chamber-grade accuracy.

#include <cmath>
#include <cstdio>
#include <vector>

#include "circuitcore/board/Board.h"
#include "emi/BoardAnalysis.h"
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
    cfg.test_distance_m   = 10.0;             // TI tested at 10 m
    cfg.freq_hz           = ee::default_cispr_freq_grid(200);

    auto R = ee::analyze_board(board, ee::cispr32_class_a(), cfg);

    // Find emikit's predicted value at the measurement frequency.
    double best_df = 1e30, predicted_at_peak = 0.0;
    double envelope_max = -1000.0;
    double envelope_max_freq = 0.0;
    for (std::size_t k = 0; k < cfg.freq_hz.size(); ++k) {
        const double df = std::abs(cfg.freq_hz[k] - measured_freq_hz);
        if (df < best_df) {
            best_df = df;
            predicted_at_peak = R.worst_case_dbuv[k];
        }
        if (R.worst_case_dbuv[k] > envelope_max) {
            envelope_max = R.worst_case_dbuv[k];
            envelope_max_freq = cfg.freq_hz[k];
        }
    }

    std::printf("\n== %s (SCLK=%.1f MHz) ==\n", label, sclk_hz / 1e6);
    std::printf("  measured peak:  %.2f dBuV/m at %.2f MHz\n",
                  measured_dbuv, measured_freq_hz / 1e6);
    std::printf("  emikit at meas: %.2f dBuV/m  (delta %+.2f dB)\n",
                  predicted_at_peak, predicted_at_peak - measured_dbuv);
    std::printf("  emikit max:     %.2f dBuV/m at %.2f MHz\n",
                  envelope_max, envelope_max_freq / 1e6);
}

}  // namespace

int main() {
    std::printf("emikit TI ADS8686S validation\n");
    std::printf("------------------------------\n");
    std::printf("Reference: TI SBAA548A 'EMC Compliance Testing for Precision ADC Systems'\n");
    std::printf("Geometry:  30 mm SCLK, 0.15 mm wide, 0.2 mm above GND, I=6 mA, tr=2 ns\n");

    run_test("Test 1", 10.0e6, 600.05e6, 34.67);
    run_test("Test 2", 50.0e6, 479.96e6, 54.73);
    run_test("Test 3", 10.0e6, 479.83e6, 51.06);

    std::printf("\nNote: peak frequencies in TI's report do not all land on SCLK\n");
    std::printf("harmonics (e.g. Test 1's 600 MHz is the 60th harmonic of 10 MHz;\n");
    std::printf("Test 3's 480 MHz is the 48th).  emikit's small-loop-dipole model\n");
    std::printf("predicts a smooth f^2 envelope multiplied by the trapezoidal\n");
    std::printf("sinc; the comb structure visible in the chamber plots comes from\n");
    std::printf("the EMI receiver's narrow IF bandwidth picking out individual\n");
    std::printf("harmonics, not a feature emikit reproduces.\n");
    return 0;
}
