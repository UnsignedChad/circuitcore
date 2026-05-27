// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "pi/DecapOptimizer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pdnkit::pi {

namespace {

// Build log-spaced frequency vector.
std::vector<double> log_freqs(double f_min, double f_max, int n) {
    std::vector<double> out;
    out.reserve(n);
    const double lo = std::log10(f_min);
    const double hi = std::log10(f_max);
    for (int i = 0; i < n; ++i) {
        const double t = (n == 1) ? 0.0 : static_cast<double>(i) / (n - 1);
        out.push_back(std::pow(10.0, lo + t * (hi - lo)));
    }
    return out;
}

// Integrated above-target excess (rectangular sum in log-f). Smaller is better.
double excess_score(const std::vector<double>& mags, double target) {
    double s = 0.0;
    for (double m : mags) {
        s += std::max(0.0, m - target);
    }
    return s;
}

}  // namespace

DecapOptimizerResult optimize_decaps(
    const CavityConfig& cavity,
    double obs_x, double obs_y,
    const DecapOptimizerConfig& opt) {

    DecapOptimizerResult result;

    if (opt.target_z <= 0.0 || opt.f_min <= 0.0 || opt.f_max <= opt.f_min ||
        opt.n_points < 2 || opt.max_caps < 1) {
        return result;
    }

    const auto freqs = log_freqs(opt.f_min, opt.f_max, opt.n_points);

    auto run_sweep = [&](const std::vector<Decap>& decaps) {
        return cavity_impedance_with_decaps_magnitude_sweep(
            cavity, obs_x, obs_y, decaps, freqs);
    };

    auto check_done = [&](const std::vector<double>& mags) {
        for (double m : mags) {
            if (m > opt.target_z) return false;
        }
        return true;
    };

    // Each new cap goes at a unique slot in a small grid around the requested
    // cap position (1mm spacing). Co-located ports make the cavity Z-matrix
    // singular, so we keep them distinct. The 7x7 grid below gives 49 slots
    // before wrapping -- more than the default 30-cap cap.
    auto slot_position = [&](int idx) {
        constexpr double step = 1.0e-3;  // 1mm
        constexpr int width = 7;
        const int i = idx % width;
        const int j = (idx / width) % width;
        return std::pair<double, double>{
            opt.cap_x + step * (i - width / 2),
            opt.cap_y + step * (j - width / 2)
        };
    };

    std::vector<Decap> chosen;

    for (int iter = 0; iter < opt.max_caps; ++iter) {
        auto z_current = run_sweep(chosen);
        if (check_done(z_current)) {
            result.target_met = true;
            result.final_max_z = *std::max_element(z_current.begin(), z_current.end());
            break;
        }
        const double cur_score = excess_score(z_current, opt.target_z);

        Decap best_cap{};
        double best_score_improvement = 0.0;
        bool any_helped = false;

        for (int k = 0; k < kCommonDecapCount; ++k) {
            const auto& lib = kCommonDecaps[k];
            auto [px, py] = slot_position(static_cast<int>(chosen.size()));
            std::vector<Decap> test = chosen;
            test.push_back({px, py, effective_C(lib), lib.esr, lib.esl});

            auto z_test = run_sweep(test);
            const double test_score = excess_score(z_test, opt.target_z);
            const double improvement = cur_score - test_score;

            if (improvement > best_score_improvement) {
                best_score_improvement = improvement;
                best_cap = test.back();
                any_helped = true;
            }
        }

        if (!any_helped) {
            // No library cap helped further — bail out so we don't loop forever.
            result.final_max_z = *std::max_element(z_current.begin(), z_current.end());
            break;
        }
        chosen.push_back(best_cap);
    }

    if (!result.target_met) {
        auto z_final = run_sweep(chosen);
        if (!z_final.empty()) {
            result.final_max_z = *std::max_element(z_final.begin(), z_final.end());
            result.target_met = check_done(z_final);
        }
    }

    result.decaps = std::move(chosen);
    return result;
}

}  // namespace pdnkit::pi
