// Eye-diagram math: TX waveform generation, simple channel models, and
// UI-aligned folding into a 2D bin grid. Rendering (Qt overlay or popup
// widget) is layered on top in a separate module.

#pragma once

#include <cstddef>
#include <vector>

namespace sikit::ibis { struct Model; }

namespace sikit::eye {

struct EyeGrid {
    int time_bins = 0;
    int volt_bins = 0;
    std::vector<int> counts;
    double v_min = 0.0;
    double v_max = 0.0;

    int& at(int t_bin, int v_bin) { return counts[t_bin + v_bin * time_bins]; }
    int  at(int t_bin, int v_bin) const { return counts[t_bin + v_bin * time_bins]; }
    int max_count() const;
};

// Generate an NRZ TX waveform: each bit becomes `samples_per_ui` samples
// at level +1 / −1 with step edges (infinite bandwidth).
std::vector<double> nrz_waveform(const std::vector<int>& bits,
                                  int samples_per_ui);

// Bandwidth-limited NRZ: transitions take `ramp_fraction · UI` samples
// to slew between bit levels. ramp_fraction = 0 → identical to
// nrz_waveform; ramp_fraction = 1.0 → fully ramped (sawtooth-ish). Real
// IBIS drivers are typically 0.05 – 0.30.
std::vector<double> nrz_with_ramp(const std::vector<int>& bits,
                                   int samples_per_ui,
                                   double ramp_fraction);

// Given a baud rate (Hz) and an IBIS Model, return the ramp fraction
// (rise/fall transition time as a fraction of the unit interval). Uses
// the typ corner. Falls back to 0.0 if the model has no usable ramp.
double ramp_fraction_from_ibis(const sikit::ibis::Model& m, double baud_hz);

std::vector<int> prbs7(int num_bits);

std::vector<double> rc_lowpass(const std::vector<double>& x,
                                double dt,
                                double cutoff_hz);

EyeGrid build_eye(const std::vector<double>& y,
                  int samples_per_ui,
                  int time_bins = 128,
                  int volt_bins = 128,
                  int warmup_uis = 4);

}  // namespace sikit::eye
