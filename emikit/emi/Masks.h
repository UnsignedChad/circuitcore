// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Radiated-emissions limit lines for the regulatory standards a
// consumer or industrial product typically targets.
//
// Mask shapes are piecewise-constant in dBuV/m vs frequency, measured
// at the spec's distance (3 m or 10 m, semi-anechoic chamber, quasi-
// peak detector). The library returns the limit value at any
// frequency by linear lookup on the breakpoints.
//
// Standards in v1:
//   CISPR 22 / EN 55022 -- IT equipment, Class A (commercial) and
//                          Class B (residential)
//   CISPR 32 / EN 55032 -- multimedia equipment, supersedes CISPR 13/22
//                          for new designs
//   FCC Part 15 Subpart B -- US consumer / industrial limits
//
// All four ship as compiled-in tables; no external data files.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace emikit::emi {

struct MaskPoint {
    double f_hz;            // frequency at which limit_dbuv applies
    double limit_dbuv;      // dBuV/m
};

struct EmissionsMask {
    std::string name;       // "CISPR 32 Class B at 3 m"
    std::string family;     // "CISPR", "FCC"
    double test_distance_m; // 3.0 or 10.0
    std::vector<MaskPoint> points;  // sorted by f_hz; piecewise-constant
    std::string source;     // spec section
};

// Built-in masks.
const EmissionsMask& cispr32_class_a();    // 10 m, commercial
const EmissionsMask& cispr32_class_b();    // 3 m,  residential
const EmissionsMask& cispr22_class_a();    // older but still required for IT-only products
const EmissionsMask& cispr22_class_b();
const EmissionsMask& fcc_part15_class_a(); // commercial / industrial
const EmissionsMask& fcc_part15_class_b(); // consumer

// Registry.
std::vector<const EmissionsMask*> all_masks();
const EmissionsMask* mask_by_name(std::string_view name);

// Limit at a frequency, in dBuV/m. Frequencies below or above the
// table's coverage return the nearest endpoint's value.
double limit_at(const EmissionsMask& mask, double freq_hz);

// Convenience: "margin" against the mask at a given freq.
// > 0 -> under the limit by that many dB (passes)
// < 0 -> over the limit (fails)
double margin_db(const EmissionsMask& mask, double freq_hz, double e_dbuv);

}  // namespace emikit::emi
