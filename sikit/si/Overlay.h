// Measured-vs-simulated S-parameter comparison.
//
// The hardware validation loop: engineer takes a VNA capture of their
// real board, synthesises sikit's prediction for the same net, and
// compares them. The closer the two |S| curves agree, the more trust
// the simulator earns. This module ships the data-plane piece (per-
// frequency dB delta between two Touchstone files); the visual overlay
// in SParamPlotWindow consumes the same math.
//
// Both files must share a frequency grid and port count. The s_param
// index follows the rest of sikit's column-major convention (index
// row + col * num_ports). Default is 1 = S21 of a 2-port.

#pragma once

#include <stdexcept>
#include <vector>

#include "si/Touchstone.h"

namespace sikit::analysis {

struct OverlayDelta {
    // Per-frequency 20*log10(|primary|/|overlay|), in dB. Positive
    // means primary is louder than overlay at that point.
    std::vector<double> delta_db;
    // Absolute-value worst case across the band.
    double max_abs_db = 0.0;
    // Frequency index where the worst case lives.
    std::size_t max_index = 0;
    // Frequency at that index, in Hz (convenience).
    double max_freq_hz = 0.0;
};

struct OverlayError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Compute the per-frequency dB delta of two Touchstone files for the
// chosen S-parameter index. Throws OverlayError on grid / port-count
// mismatch.
OverlayDelta overlay_delta(const sikit::touchstone::TouchstoneFile& primary,
                           const sikit::touchstone::TouchstoneFile& overlay,
                           int s_param_index = 1);

}  // namespace sikit::analysis
