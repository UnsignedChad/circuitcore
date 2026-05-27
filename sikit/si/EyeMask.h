// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Protocol eye masks and pass/fail checking.
//
// A mask is a "forbidden region" polygon in normalized (time/UI, voltage)
// space. Any non-zero bin in the eye that falls inside the polygon is a
// violation; a board passes the mask only if zero violations are detected.
//
// Coordinates:
//   t ∈ [0, 1]  — fraction of one UI
//   v ∈ [-1, 1] — normalized to the signed half-range of the captured eye

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "si/Eye.h"

namespace sikit::specs {

struct EyeMask {
    std::string name;
    // Counter-clockwise polygon vertices in normalized (t, v) space.
    std::vector<std::pair<double, double>> polygon;
    // Brief human-readable description of where the mask comes from.
    std::string source;
};

// A simple centered hexagonal opening — generic 1 UI test pattern that
// requires the eye to be clean within ±0.4 normalized volts over the
// middle 40% of the unit interval.
const EyeMask& generic_centered_opening();

// USB 2.0 high-speed transmit eye mask (Template 1, simplified rectilinear
// approximation). Real implementations should use the precise hexagon from
// the USB 2.0 spec; this is good enough for green/red sanity checks.
const EyeMask& usb20_hs_template1();

std::vector<std::string> available_mask_names();
const EyeMask* mask_by_name(std::string_view name);

// Standard ray-casting point-in-polygon test.
bool point_in_polygon(double t, double v,
                      const std::vector<std::pair<double, double>>& poly);

// Count eye bins that fall inside the mask's forbidden region.
int count_violations(const eye::EyeGrid& g, const EyeMask& mask);

inline bool passes(const eye::EyeGrid& g, const EyeMask& mask) {
    return count_violations(g, mask) == 0;
}

}  // namespace sikit::specs
