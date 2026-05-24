// Parametric connector / cable Touchstone generator.
//
// Real SI work usually starts with a vendor-supplied .s2p / .s4p for
// the SMA launches, USB-C receptacles, RJ45 magnetics etc. that sit
// between the board and the test instrument. When that vendor file
// isn't to hand, you still need *something* in the channel cascade to
// avoid wildly optimistic predictions -- a placeholder with the right
// IL slope and RL floor is far better than nothing.
//
// This module generates physics-plausible Touchstone files from a
// small parameter set (IL slope vs sqrt(f), RL floor, electrical
// length, optional stub-resonance notch, optional differential mode-
// conversion) and ships named presets for the connector types
// engineers reach for most. Users with vendor files just drop them
// in via TouchstoneReader::read_file instead.
//
// The generator is intentionally simple -- it is a placeholder, not a
// simulation. The IL model is sqrt(f) (skin + dielectric), phase
// comes from a fixed electrical length, RL is a flat magnitude floor.
// Real connectors have more structure (impedance mismatches at the
// transition regions, mode-conversion that varies with frequency,
// crosstalk between pairs). When that detail matters, the vendor's
// measured Touchstone is the right input.

#pragma once

#include <string>
#include <vector>

#include "si/Touchstone.h"

namespace sikit::si {

struct ConnectorSpec {
    std::string name;                  // "SMA edge launch (placeholder)"
    int num_ports = 2;                 // 2 for single-ended, 4 for diff pair

    // Frequency response model:
    //   |S21|(dB) = -il_slope * sqrt(f_GHz) - il_constant
    // Cap at -60 dB to avoid silly large IL values at extrapolation.
    double il_slope_db_per_sqrt_ghz = 0.05;
    double il_constant_db = 0.0;

    // Return loss: |S11| = |S22| flat at this dB value below unity.
    // Positive values; the model places |S11| = 10^(-rl_db/20).
    double rl_db = 25.0;

    // Electrical length used to compute phase: delay_s = length / v_p.
    double electrical_length_m = 0.01;     // 10 mm typical for SMA launch

    // Optional stub-resonance notch (set notch_freq_hz to 0 to disable).
    // Adds an additional dip of `notch_depth_db` at notch_freq_hz with
    // a Q controlled by notch_q.
    double notch_freq_hz = 0.0;
    double notch_depth_db = 0.0;
    double notch_q = 30.0;

    // 4-port differential extras (ignored when num_ports == 2):
    //   * far-end coupling between traces, dB at 1 GHz, ramps with sqrt(f)
    //   * mode conversion (Sdc, dB at 1 GHz), models skew-driven crosstalk
    double diff_xtalk_db = -40.0;
    double mode_conv_db  = -50.0;

    // Reference impedance for the generated file. Single-ended ports
    // default to 50 ohm; diff connectors usually print at 100 ohm
    // per the convention used by VNA exports.
    double reference_impedance = 50.0;
};

// Generate a Touchstone file on the supplied frequency grid using the
// parametric model above. Frequencies must be positive and sorted.
sikit::touchstone::TouchstoneFile generate_connector_touchstone(
    const ConnectorSpec& spec,
    const std::vector<double>& freq_hz);

// Named presets returning a ConnectorSpec the caller can pass to
// generate_connector_touchstone(). Numbers come from a ballpark survey
// of vendor datasheets for the named family; they will not match any
// specific part exactly. See file-level comment.

ConnectorSpec preset_sma_edge_launch();      // 2-port, single-ended, low-loss
ConnectorSpec preset_sma_panel_mount();      // 2-port, single-ended, higher loss
ConnectorSpec preset_usb_c_diff_pair();      // 4-port, diff pair, USB 3.x band
ConnectorSpec preset_rj45_diff_pair();       // 4-port, diff pair, with notable mode conversion
ConnectorSpec preset_samtec_btb();           // 2-port, board-to-board

std::vector<std::string> available_connector_presets();
ConnectorSpec connector_preset_by_name(const std::string& name);

}  // namespace sikit::si
