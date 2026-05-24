// Greedy decap selection: given a target |Z(f)| over a frequency band,
// pick the smallest set of decoupling capacitors from a library that pulls
// the plane impedance below target.
//
// Algorithm: at each step, find the candidate library cap that reduces the
// integrated above-target excess the most when added at a fixed position.
// Repeats until target met or max_caps exceeded.

#pragma once

#include <string>
#include <vector>

#include "pi/CavityModel.h"

namespace pdnkit::pi {

struct LibraryDecap {
    const char* name;
    double C;     // nominal capacitance (F) -- the printed-on-the-part value
    double esr;   // equiv series resistance (ohm)
    double esl;   // equiv series inductance (H)
    // Effective C as a fraction of nominal under typical DC bias on a digital
    // PDN rail (~50-70% of rated voltage). 1.0 = no derate (C0G/NP0 ceramics,
    // tantalum, electrolytic). 0.5-0.7 = realistic for X5R / X7R MLCC in
    // small packages, which is where most of the bypass library lives.
    double derating_factor = 1.0;
};

// Effective C for analysis: nominal * derating_factor. Use this anywhere a
// derated value is wanted (the optimizer does).
inline double effective_C(const LibraryDecap& d) {
    return d.C * d.derating_factor;
}

// Default decap library: representative X5R/X7R MLCCs at common values, with
// ESR/ESL numbers in the ballpark for typical 0402/0603/0805 cases. Values
// are intentionally rounded for clarity; replace with vendor-specific data
// when modeling a real BOM.
// Expanded decap library: 24 entries spanning common SMT MLCC values from
// 100 uF bulk down to 10 pF tuning caps. ESR and ESL are representative of
// modern X5R/X7R/C0G parts (Murata GRM / TDK CGA series). Vendor-specific
// data substitution is straightforward -- edit values or extend the array.
//
// Inductance scales roughly with package size: 0402 ~ 0.3 nH, 0603 ~ 0.5,
// 0805 ~ 0.7, 1210 ~ 1.2.
//
// Dielectric notes:
//   * X5R/X7R: ceramic Class II, dominant for bulk + bypass; significant
//     DC bias derating at large fractions of rated voltage (not modelled
//     yet -- assume mid-bias).
//   * C0G/NP0: ceramic Class I, low loss + zero DC bias derating, used
//     for high-frequency tuning at <= 1 nF.
//   * Tantalum: high-stable ESR, useful for damping (~50-200 mOhm).
constexpr LibraryDecap kCommonDecaps[] = {
    // Bulk X5R caps (heavy derate -- ~50% at common digital voltages).
    {"100 uF X5R 1210",   100.0e-6,   2.0e-3, 1.2e-9, 0.50},
    {"47 uF X5R 0805",     47.0e-6,   3.0e-3, 0.9e-9, 0.55},
    {"22 uF X5R 0805",     22.0e-6,   3.0e-3, 0.8e-9, 0.60},
    // Mid-value ceramics. Smaller package / lower C -> less derate.
    {"10 uF X5R 0805",     10.0e-6,   4.0e-3, 0.7e-9, 0.65},
    {"10 uF X5R 0603",     10.0e-6,   5.0e-3, 0.6e-9, 0.55},
    {"4.7 uF X5R 0603",    4.7e-6,    6.0e-3, 0.5e-9, 0.60},
    {"2.2 uF X7R 0603",    2.2e-6,    8.0e-3, 0.5e-9, 0.70},
    {"1 uF X7R 0603",      1.0e-6,   10.0e-3, 0.5e-9, 0.75},
    {"1 uF X7R 0402",      1.0e-6,   12.0e-3, 0.4e-9, 0.65},
    // Standard bypass band.
    {"470 nF X7R 0402",  470.0e-9,   15.0e-3, 0.4e-9, 0.75},
    {"220 nF X7R 0402",  220.0e-9,   18.0e-3, 0.4e-9, 0.80},
    {"100 nF X7R 0402",  100.0e-9,   25.0e-3, 0.3e-9, 0.85},
    {"47 nF X7R 0402",    47.0e-9,   30.0e-3, 0.3e-9, 0.90},
    {"22 nF X7R 0402",    22.0e-9,   35.0e-3, 0.3e-9, 0.90},
    {"10 nF X7R 0402",    10.0e-9,   30.0e-3, 0.3e-9, 0.95},
    {"4.7 nF X7R 0402",    4.7e-9,   40.0e-3, 0.3e-9, 0.95},
    {"2.2 nF X7R 0402",    2.2e-9,   50.0e-3, 0.3e-9, 0.95},
    {"1 nF X7R 0402",      1.0e-9,   50.0e-3, 0.3e-9, 0.95},
    // C0G / NP0 class I -- zero DC-bias derate by construction.
    {"470 pF C0G 0402",  470.0e-12,  60.0e-3, 0.3e-9, 1.00},
    {"220 pF C0G 0402",  220.0e-12,  80.0e-3, 0.3e-9, 1.00},
    {"100 pF C0G 0402",  100.0e-12, 100.0e-3, 0.3e-9, 1.00},
    {"47 pF C0G 0402",    47.0e-12, 150.0e-3, 0.3e-9, 1.00},
    {"22 pF C0G 0402",    22.0e-12, 200.0e-3, 0.3e-9, 1.00},
    {"10 pF C0G 0402",    10.0e-12, 300.0e-3, 0.3e-9, 1.00},
    // Tantalum -- DC bias does not derate solid Ta. High ESR (useful for
    // damping plane resonances).
    {"47 uF Ta TAJB",      47.0e-6, 100.0e-3, 2.0e-9, 1.00},
    {"100 uF Ta TAJC",    100.0e-6,  80.0e-3, 2.5e-9, 1.00},
};

struct DecapOptimizerConfig {
    double target_z = 0.025;     // ohm
    double f_min    = 1.0e6;     // Hz
    double f_max    = 1.0e9;
    int    n_points = 50;        // log-spaced sweep points used during search
    double cap_x    = 0.0;       // position (m) where all suggested caps go
    double cap_y    = 0.0;
    int    max_caps = 30;        // safety cap on iteration count
};

struct DecapOptimizerResult {
    std::vector<Decap> decaps;   // the selected decap network
    bool target_met = false;     // true if all sweep points end up below target
    double final_max_z = 0.0;    // max |Z| over the sweep after selection
};

// Library available to UIs that want to expose value-pick lists.
constexpr int kCommonDecapCount = sizeof(kCommonDecaps) / sizeof(kCommonDecaps[0]);

// Run the greedy search.
DecapOptimizerResult optimize_decaps(
    const CavityConfig& cavity,
    double obs_x, double obs_y,
    const DecapOptimizerConfig& opt);

}  // namespace pdnkit::pi
