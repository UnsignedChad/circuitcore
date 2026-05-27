// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Default body-height / mass lookup. See header for rationale.
//
// The matching strategy is a prefix walk: for each candidate package
// family we ask "does the footprint name contain this token". KiCad
// libraries are not perfectly consistent ("R_0402_1005Metric",
// "Capacitor_SMD:C_0402", "SOIC-8_3.9x4.9mm_P1.27mm", "Connector_PinHeader:
// PinHeader_1x02_P2.54mm_Vertical") so we look for the family token
// anywhere in the string and pick the first match in order from
// most-specific to most-generic. Package families and their typical
// body heights are taken from JEDEC / IPC standards; +/-10% accuracy
// is fine for what mpkit needs (thermal mass + extruded body for the
// viewer).

#include "circuitcore/board/PackageDefaults.h"

#include <array>
#include <string>

namespace circuitcore::board {

namespace {

struct Entry {
    std::string_view token;     // case-insensitive prefix to look for
    double height_m;            // body height
    double mass_kg;             // typical body + leads mass
};

// Order matters: longer / more-specific tokens go first so "SOIC" does
// not pre-empty "TSSOP" etc. Heights from JEDEC body-size tables.
constexpr std::array<Entry, 38> kPackages = {{
    // ---- chip resistors / caps ----
    {"0201",      0.30e-3,  0.05e-6},  // 50 ug
    {"0402",      0.45e-3,  0.10e-6},
    {"0603",      0.55e-3,  0.20e-6},
    {"0805",      0.70e-3,  0.50e-6},
    {"1206",      1.00e-3,  1.00e-6},
    {"1210",      1.10e-3,  1.50e-6},
    {"1812",      1.50e-3,  3.00e-6},
    {"2010",      1.50e-3,  4.00e-6},
    {"2512",      1.80e-3,  6.00e-6},
    // ---- SMD discrete + small IC ----
    {"SOT-23",    1.10e-3,  8.00e-6},
    {"SOT23",     1.10e-3,  8.00e-6},
    {"SOT-223",   1.60e-3, 60.00e-6},
    {"SOT-89",    1.50e-3, 40.00e-6},
    {"SOT-323",   0.90e-3,  4.00e-6},
    {"SOT-89",    1.50e-3, 40.00e-6},
    {"DPAK",      2.30e-3, 0.60e-3},
    {"D2PAK",     4.40e-3, 1.60e-3},
    {"TO-263",    4.40e-3, 1.50e-3},
    {"TO-252",    2.30e-3, 0.60e-3},
    // ---- SMD IC families ----
    {"TSSOP",     1.10e-3, 0.10e-3},
    {"MSOP",      1.10e-3, 0.05e-3},
    {"SSOP",      1.75e-3, 0.20e-3},
    {"SOIC",      1.75e-3, 0.30e-3},
    {"QFN",       0.85e-3, 0.10e-3},
    {"DFN",       0.85e-3, 0.08e-3},
    {"LQFP",      1.40e-3, 0.50e-3},
    {"TQFP",      1.00e-3, 0.40e-3},
    {"QFP",       2.50e-3, 1.00e-3},
    {"BGA",       1.20e-3, 0.50e-3},
    // ---- THT IC + discretes ----
    {"DIP",       4.00e-3, 1.00e-3},
    {"TO-220",    4.50e-3, 2.00e-3},
    {"TO-247",    5.00e-3, 6.00e-3},
    {"TO-92",     5.20e-3, 0.20e-3},
    // ---- mech / passive bulk ----
    {"PinHeader", 8.50e-3, 1.00e-3},
    {"Connector", 8.00e-3, 4.00e-3},
    {"USB",       7.00e-3, 3.00e-3},
    {"Crystal",   4.00e-3, 0.30e-3},
    {"Inductor",  2.50e-3, 0.30e-3},
}};

constexpr double kDefaultHeight = 1.00e-3;   // generic small SMD
constexpr double kDefaultMass   = 0.10e-3;   // 0.1 g

bool contains_ci(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > hay.size()) return false;
    auto eq = [](char a, char b) {
        auto l = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
        return l(a) == l(b);
    };
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (!eq(hay[i + j], needle[j])) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

const Entry* match(std::string_view name) noexcept {
    for (const auto& e : kPackages) {
        if (contains_ci(name, e.token)) return &e;
    }
    return nullptr;
}

}  // namespace

double default_body_height_m(std::string_view footprint_name) noexcept {
    if (const auto* e = match(footprint_name)) return e->height_m;
    return kDefaultHeight;
}

double default_mass_kg(std::string_view footprint_name) noexcept {
    if (const auto* e = match(footprint_name)) return e->mass_kg;
    return kDefaultMass;
}

}  // namespace circuitcore::board
