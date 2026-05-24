// Numerical metrics extracted from a populated EyeGrid. These are the
// figures that go in SI reports alongside the visual eye plot and the
// pass/fail mask check.

#pragma once

#include "si/Eye.h"

namespace sikit::eye {

struct EyeMetrics {
    // Eye-opening height at the centre of the UI: width (in volts) of
    // the empty voltage band that separates the upper trace cluster from
    // the lower one. Zero means the eye is closed.
    double height_v = 0.0;

    // Eye-opening width at the centre voltage: width (in unit-interval
    // fraction) of the empty time band across the eye centre.
    double width_ui = 0.0;

    // Peak-peak timing jitter at the mid-voltage crossing, expressed as
    // a fraction of the UI. Computed from the spread of zero-crossing
    // bins on either side of the eye centre.
    double jitter_pp_ui = 0.0;

    // The threshold voltage used for the width / jitter measurements
    // (midpoint of the eye's data range).
    double v_threshold = 0.0;
};

// Compute the metrics from an eye grid. Returns zeros for any field
// that can't be determined (e.g. eye fully closed → height_v = 0;
// no zero crossings detected → jitter_pp_ui = 0). All measurements
// honour the (v_min, v_max) span the grid was built with.
EyeMetrics measure_eye(const EyeGrid& g);

}  // namespace sikit::eye
