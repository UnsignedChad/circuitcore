// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/DiffSynth.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <algorithm>
#include <utility>

#include "si/CrossSection.h"
#include "si/FdmSolver.h"
#include "si/Rlgc.h"

namespace sikit::analysis {

using Complex = std::complex<double>;

namespace {

constexpr double kC0 = 2.99792458e8;

bool is_outer_copper(int ord) { return ord == 0 || ord == 31; }

// Build the symmetric 2-trace cross-section used by the diff RLGC pass.
em2d::CrossSection make_diff_xsection(const DiffChannelSpec& spec) {
    em2d::CrossSection cs;
    cs.air_above = 5.0 * spec.stackup.outer_dielectric_height;
    const double half_box =
        std::max(8e-3, 3.0 * (2.0 * spec.trace_width + spec.spacing));
    cs.y_min = -half_box;
    cs.y_max = +half_box;

    const double offset = 0.5 * (spec.trace_width + spec.spacing);
    em2d::Conductor t_p, t_n, gnd;
    t_p.id = 0; t_p.y_center = -offset;
    t_n.id = 1; t_n.y_center = +offset;
    gnd.id = 2; gnd.y_center = 0.0;
    t_p.width = t_n.width = spec.trace_width;
    t_p.thickness = t_n.thickness = spec.stackup.copper_thickness;
    t_p.voltage = t_n.voltage = 0.0;  // RLGC excites these separately
    gnd.voltage = 0.0;

    if (is_outer_copper(spec.layer_ordinal)) {
        cs.stack.push_back(
            {spec.stackup.outer_dielectric_height, spec.stackup.epsilon_r,
             0.0, ""});
        t_p.z_top = -spec.stackup.copper_thickness;
        t_n.z_top = -spec.stackup.copper_thickness;
        gnd.z_top = spec.stackup.outer_dielectric_height;
        gnd.width = 2.0 * half_box;
        gnd.thickness = std::max(spec.stackup.copper_thickness, 1e-4);
    } else {
        const double half = 0.5 * spec.stackup.inner_plane_separation;
        cs.stack.push_back({half, spec.stackup.epsilon_r, 0.0, ""});
        cs.stack.push_back({half, spec.stackup.epsilon_r, 0.0, ""});
        t_p.z_top = half - 0.5 * spec.stackup.copper_thickness;
        t_n.z_top = half - 0.5 * spec.stackup.copper_thickness;
        gnd.z_top = spec.stackup.inner_plane_separation;
        gnd.width = 2.0 * half_box;
        gnd.thickness = std::max(spec.stackup.copper_thickness, 1e-4);
        em2d::Conductor top_gnd = gnd;
        top_gnd.id = 2;
        top_gnd.z_top = -gnd.thickness;
        cs.conductors.push_back(top_gnd);
    }
    cs.conductors.push_back(t_p);
    cs.conductors.push_back(t_n);
    cs.conductors.push_back(gnd);
    return cs;
}

// Per-mode ABCD matrix for a lossless transmission line.
struct ModeAbcd {
    Complex A, B, C, D;
};

ModeAbcd line_abcd(double Z0, double v, double length, double freq) {
    const double bl = 2.0 * std::numbers::pi * freq * length / v;
    const double cs_ = std::cos(bl);
    const double sn_ = std::sin(bl);
    ModeAbcd m;
    m.A = Complex(cs_, 0.0);
    m.D = Complex(cs_, 0.0);
    m.B = Complex(0.0, Z0 * sn_);
    m.C = Complex(0.0, sn_ / Z0);
    return m;
}

// ABCD → S parameters for a 2-port with reference impedance Zr.
struct ModeS {
    Complex S11, S12, S21, S22;
};

ModeS abcd_to_s(const ModeAbcd& m, double Zr) {
    const Complex denom = m.A + m.B / Zr + m.C * Zr + m.D;
    ModeS s;
    s.S11 = (m.A + m.B / Zr - m.C * Zr - m.D) / denom;
    s.S12 = Complex(2.0, 0.0) * (m.A * m.D - m.B * m.C) / denom;
    s.S21 = Complex(2.0, 0.0) / denom;
    s.S22 = (-m.A + m.B / Zr - m.C * Zr + m.D) / denom;
    return s;
}

}  // namespace

sikit::touchstone::TouchstoneFile synthesize_diff_channel(
    const DiffChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance) {
    if (spec.trace_width <= 0.0 || spec.length_m <= 0.0) {
        throw std::runtime_error("synthesize_diff_channel: bad geometry");
    }

    // RLGC matrices via the FDM solver.
    auto cs = make_diff_xsection(spec);
    em2d::SolveConfig cfg;
    cfg.tolerance = 5e-6;
    cfg.max_iterations = 100000;
    const double h = std::clamp(spec.trace_width / 25.0, 20e-6, 200e-6);
    auto rlgc = em2d::compute_rlgc(cs, /*signals=*/{0, 1}, /*ground=*/2, h, cfg);
    if (!rlgc.ok) {
        throw std::runtime_error(
            "synthesize_diff_channel: RLGC extraction failed");
    }

    // Symmetric pair → equal diagonals. Take averages for robustness
    // against tiny discretisation asymmetry in the FDM solver.
    const double L_self = 0.5 * (rlgc.L(0, 0) + rlgc.L(1, 1));
    const double L_mut  = 0.5 * (rlgc.L(0, 1) + rlgc.L(1, 0));
    // Maxwell C: diagonal positive, off-diagonal negative; C_mut > 0.
    const double C_self = 0.5 * (rlgc.C(0, 0) + rlgc.C(1, 1));
    const double C_mut  = -0.5 * (rlgc.C(0, 1) + rlgc.C(1, 0));

    if (L_self <= 0.0 || C_self <= 0.0) {
        throw std::runtime_error(
            "synthesize_diff_channel: degenerate RLGC matrix");
    }

    // Modal characteristic impedances and phase velocities.
    // Even mode (both traces at +1 V): L↑, C↓.
    // Odd  mode (one at +V, other at −V): L↓, C↑.
    const double Z_even = std::sqrt((L_self + L_mut) / std::max(1e-30, C_self - C_mut));
    const double Z_odd  = std::sqrt((L_self - L_mut) / (C_self + C_mut));
    const double v_even = 1.0 / std::sqrt((L_self + L_mut) * (C_self - C_mut));
    const double v_odd  = 1.0 / std::sqrt((L_self - L_mut) * (C_self + C_mut));

    sikit::touchstone::TouchstoneFile out;
    out.num_ports = 4;
    out.format = sikit::touchstone::Format::RealImaginary;
    out.reference_impedance = reference_impedance;
    out.frequency_scale = 1.0;
    out.frequencies = freq_hz;
    out.s_matrices.reserve(freq_hz.size());

    const double Zr = reference_impedance;

    for (double f : freq_hz) {
        const auto e = abcd_to_s(line_abcd(Z_even, v_even, spec.length_m, f), Zr);
        const auto o = abcd_to_s(line_abcd(Z_odd,  v_odd,  spec.length_m, f), Zr);

        // Recombine. Port order: 1=P_near, 2=P_far, 3=N_near, 4=N_far.
        // Textbook formulas (Pozar §7.2):
        //   S11 = ½(e.S11 + o.S11)       reflection on positive near end
        //   S33 = ½(e.S11 + o.S11)       symmetric
        //   S13 = ½(e.S11 − o.S11)       near-end coupling between traces
        //   S31 = S13
        //   S12 = ½(e.S21 + o.S21)       thru on positive
        //   S34 = ½(e.S21 + o.S21)
        //   S14 = ½(e.S21 − o.S21)       far-end coupling
        //   S32 = S14
        // (reflection on far end → similar block from e.S22 / o.S22).
        const Complex half(0.5, 0.0);
        const Complex r_near  = half * (e.S11 + o.S11);
        const Complex r_xnear = half * (e.S11 - o.S11);
        const Complex t_thru  = half * (e.S21 + o.S21);
        const Complex t_xfar  = half * (e.S21 - o.S21);
        const Complex r_far   = half * (e.S22 + o.S22);
        const Complex r_xfar  = half * (e.S22 - o.S22);

        // Column-major 4×4: index = row + col * 4.
        std::vector<Complex> M(16, Complex(0, 0));
        auto set = [&](int r, int c, Complex v) { M[r + c * 4] = v; };

        // Diagonal: reflection at each port.
        set(0, 0, r_near);                // S11
        set(2, 2, r_near);                // S33
        set(1, 1, r_far);                 // S22
        set(3, 3, r_far);                 // S44

        // P-trace thru (port 1 ↔ port 2) and N-trace thru (port 3 ↔ port 4).
        set(1, 0, t_thru);                // S21
        set(0, 1, t_thru);                // S12
        set(3, 2, t_thru);                // S43
        set(2, 3, t_thru);                // S34

        // Near-end coupling between traces (port 1 ↔ port 3, port 2 ↔ port 4).
        set(2, 0, r_xnear);               // S31
        set(0, 2, r_xnear);               // S13
        set(3, 1, r_xfar);                // S42
        set(1, 3, r_xfar);                // S24

        // Far-end coupling (cross diagonal: port 1 → port 4, port 3 → port 2).
        set(3, 0, t_xfar);                // S41
        set(0, 3, t_xfar);                // S14
        set(1, 2, t_xfar);                // S23
        set(2, 1, t_xfar);                // S32

        out.s_matrices.push_back(std::move(M));
    }
    return out;
}

}  // namespace sikit::analysis
