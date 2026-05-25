// Decap sensitivity analysis.
//
// Given a populated PDN, the question every signoff asks: which caps
// actually matter? If I remove this one, does Z(f) blow through target?
// If I cut it, will anything notice?
//
// Leave-one-out method: compute Z(f) with all decaps, then for each
// decap k compute Z(f) with that one removed. The relative change is
// the cap's sensitivity. Caps ranked at the top are critical; caps at
// the bottom are slack and candidates for removal.
//
// Cheap (N+1 cavity sweeps for N decaps) and gives the engineer a
// concrete ordered list to act on.

#pragma once

#include <vector>

#include "pi/CavityModel.h"

namespace pdnkit::pi {

struct SensitivitySample {
    int    decap_index;             // index into the input decaps vector
    double peak_z_with_caps_ohm;    // |Z|_max in the band with all caps
    double peak_z_without_cap_ohm;  // |Z|_max in the band without this cap
    double peak_freq_hz;            // frequency where the impact is worst
    double max_relative_change;     // max |dZ|/|Z| across the band
};

// Returns one SensitivitySample per decap, sorted by max_relative_change
// descending (most critical first).
std::vector<SensitivitySample> sensitivity_sweep(
    const CavityConfig& cfg,
    double xo, double yo,
    const std::vector<Decap>& decaps,
    const std::vector<double>& freqs_hz);

}  // namespace pdnkit::pi
