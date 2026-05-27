// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "emi/Masks.h"

namespace emikit::emi {

const EmissionsMask& cispr32_class_b() {
    static const EmissionsMask m{
        "CISPR 32 Class B (3 m)", "CISPR", 3.0,
        {
            // QP limits per CISPR 32 ed.2, Table A.1.
            {30.0e6,   40.0},
            {230.0e6,  40.0},
            {230.0e6,  47.0},   // step at 230 MHz
            {1.0e9,    47.0},
            {1.0e9,    50.0},   // CISPR 32 extends to 6 GHz with 50/54 dBuV/m
            {3.0e9,    50.0},
            {3.0e9,    54.0},
            {6.0e9,    54.0},
        },
        "CISPR 32 ed.2 Table A.1 (residential)",
    };
    return m;
}

const EmissionsMask& cispr32_class_a() {
    static const EmissionsMask m{
        "CISPR 32 Class A (10 m)", "CISPR", 10.0,
        {
            {30.0e6,   40.0},
            {230.0e6,  40.0},
            {230.0e6,  47.0},
            {1.0e9,    47.0},
            {1.0e9,    56.0},
            {3.0e9,    56.0},
            {3.0e9,    60.0},
            {6.0e9,    60.0},
        },
        "CISPR 32 ed.2 Table A.2 (commercial)",
    };
    return m;
}

const EmissionsMask& cispr22_class_b() {
    static const EmissionsMask m{
        "CISPR 22 Class B (3 m)", "CISPR", 3.0,
        {
            {30.0e6,   30.0},
            {230.0e6,  30.0},
            {230.0e6,  37.0},
            {1.0e9,    37.0},
        },
        "CISPR 22 (IT equipment, residential)",
    };
    return m;
}

const EmissionsMask& cispr22_class_a() {
    static const EmissionsMask m{
        "CISPR 22 Class A (10 m)", "CISPR", 10.0,
        {
            {30.0e6,   40.0},
            {230.0e6,  40.0},
            {230.0e6,  47.0},
            {1.0e9,    47.0},
        },
        "CISPR 22 (IT equipment, commercial)",
    };
    return m;
}

const EmissionsMask& fcc_part15_class_b() {
    static const EmissionsMask m{
        "FCC Part 15 Class B (3 m)", "FCC", 3.0,
        {
            // FCC 15.109 quasi-peak (3 m chamber).
            {30.0e6,   40.0},
            {88.0e6,   40.0},
            {88.0e6,   43.5},
            {216.0e6,  43.5},
            {216.0e6,  46.0},
            {960.0e6,  46.0},
            {960.0e6,  54.0},
            {1.0e9,    54.0},
        },
        "FCC 47 CFR 15.109 (consumer)",
    };
    return m;
}

const EmissionsMask& fcc_part15_class_a() {
    static const EmissionsMask m{
        "FCC Part 15 Class A (10 m)", "FCC", 10.0,
        {
            {30.0e6,   39.0},
            {88.0e6,   39.0},
            {88.0e6,   43.5},
            {216.0e6,  43.5},
            {216.0e6,  46.4},
            {960.0e6,  46.4},
            {960.0e6,  49.5},
            {1.0e9,    49.5},
        },
        "FCC 47 CFR 15.109 (commercial / industrial)",
    };
    return m;
}

std::vector<const EmissionsMask*> all_masks() {
    return {
        &cispr32_class_b(), &cispr32_class_a(),
        &cispr22_class_b(), &cispr22_class_a(),
        &fcc_part15_class_b(), &fcc_part15_class_a(),
    };
}

const EmissionsMask* mask_by_name(std::string_view name) {
    for (const auto* m : all_masks()) {
        if (m->name == name) return m;
    }
    return nullptr;
}

double limit_at(const EmissionsMask& mask, double freq_hz) {
    if (mask.points.empty()) return 0.0;
    if (freq_hz <= mask.points.front().f_hz) return mask.points.front().limit_dbuv;
    if (freq_hz >= mask.points.back().f_hz)  return mask.points.back().limit_dbuv;
    // Piecewise-constant: the limit at f equals the value of the
    // highest breakpoint <= f. Mask tables include duplicate f-values
    // at step edges; we honour them by returning the second (higher)
    // value at the step.
    double current = mask.points.front().limit_dbuv;
    for (const auto& p : mask.points) {
        if (p.f_hz > freq_hz) break;
        current = p.limit_dbuv;
    }
    return current;
}

double margin_db(const EmissionsMask& mask, double freq_hz, double e_dbuv) {
    return limit_at(mask, freq_hz) - e_dbuv;
}

}  // namespace emikit::emi
