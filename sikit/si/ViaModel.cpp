#include "si/ViaModel.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace sikit::analysis {

using Complex = std::complex<double>;

namespace {

constexpr double kC0  = 2.99792458e8;
constexpr double kMu0 = 4.0e-7 * std::numbers::pi;
constexpr double kEps0 = 8.8541878128e-12;

// 2x2 ABCD matrix as a flat struct so we can multiply without pulling
// Eigen into a tight per-frequency loop (Eigen does fine but the math is
// dense enough that the inline form reads better).
struct Abcd {
    Complex A, B, C, D;

    static Abcd series_impedance(Complex Z) {
        return {Complex(1, 0), Z, Complex(0, 0), Complex(1, 0)};
    }
    static Abcd shunt_admittance(Complex Y) {
        return {Complex(1, 0), Complex(0, 0), Y, Complex(1, 0)};
    }

    Abcd operator*(const Abcd& r) const {
        return {
            A * r.A + B * r.C,
            A * r.B + B * r.D,
            C * r.A + D * r.C,
            C * r.B + D * r.D,
        };
    }
};

// ABCD → S, reference impedance Z_r. Pozar eq. 4.45.
void abcd_to_s(const Abcd& m, double Zr,
               Complex& S11, Complex& S12, Complex& S21, Complex& S22) {
    const Complex denom = m.A + m.B / Zr + m.C * Zr + m.D;
    S11 = (m.A + m.B / Zr - m.C * Zr - m.D) / denom;
    S12 = Complex(2.0, 0) * (m.A * m.D - m.B * m.C) / denom;
    S21 = Complex(2.0, 0) / denom;
    S22 = (-m.A + m.B / Zr - m.C * Zr + m.D) / denom;
}

}  // namespace

ViaLumped via_lumped(const ViaSpec& spec) {
    ViaLumped r;
    if (spec.drill_diameter <= 0.0 ||
        spec.antipad_diameter <= spec.drill_diameter ||
        spec.total_length <= 0.0) {
        return r;
    }
    const double h         = spec.total_length;
    const double d_drill   = spec.drill_diameter;
    const double D_antipad = spec.antipad_diameter;
    const double eps_r     = std::max(spec.epsilon_r, 1.0);

    // Series barrel inductance (Johnson, 1993).
    r.L_barrel = (kMu0 / (2.0 * std::numbers::pi)) * h *
                 std::log(D_antipad / d_drill);

    // End-pad shunt capacitance: pad area looking into adjacent reference
    // plane through the pad-to-plane dielectric. If the user didn't supply
    // pad_to_plane_h, fall back to a third of the total via length as a
    // typical near-pad dielectric thickness.
    const double h_pad = (spec.pad_to_plane_h > 0.0)
                             ? spec.pad_to_plane_h
                             : (spec.total_length / 3.0);
    const double pad_area = std::numbers::pi * std::pow(spec.pad_diameter * 0.5, 2);
    r.C_pad = kEps0 * eps_r * pad_area / std::max(h_pad, 1e-9);

    // Coaxial transmission-line impedance of the antipad region (for stub).
    //     Z = (1/(2 pi)) * sqrt(mu/eps) * ln(D/d)
    const double eta = std::sqrt(kMu0 / (kEps0 * eps_r));
    r.Z_stub = (eta / (2.0 * std::numbers::pi)) * std::log(D_antipad / d_drill);

    // Stub quarter-wave resonance frequency.
    if (spec.stub_length > 0.0) {
        const double v_p = kC0 / std::sqrt(eps_r);
        r.stub_resonance_hz = v_p / (4.0 * spec.stub_length);
    }
    return r;
}

sikit::touchstone::TouchstoneFile compute_via_s2p(
    const ViaSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance) {
    if (spec.drill_diameter <= 0.0) {
        throw std::runtime_error("compute_via_s2p: drill_diameter must be > 0");
    }
    if (spec.antipad_diameter <= spec.drill_diameter) {
        throw std::runtime_error(
            "compute_via_s2p: antipad_diameter must exceed drill_diameter");
    }
    if (spec.total_length <= 0.0) {
        throw std::runtime_error("compute_via_s2p: total_length must be > 0");
    }
    if (spec.stub_length < 0.0) {
        throw std::runtime_error("compute_via_s2p: stub_length must be >= 0");
    }

    const ViaLumped lumped = via_lumped(spec);
    const double Zs = lumped.Z_stub;
    const double eps_r = std::max(spec.epsilon_r, 1.0);
    const double v_p   = kC0 / std::sqrt(eps_r);

    sikit::touchstone::TouchstoneFile out;
    out.num_ports = 2;
    out.format = sikit::touchstone::Format::RealImaginary;
    out.reference_impedance = reference_impedance;
    out.frequency_scale = 1.0;
    out.frequencies = freq_hz;
    out.s_matrices.reserve(freq_hz.size());

    const Complex j(0, 1);
    for (double f : freq_hz) {
        const double w = 2.0 * std::numbers::pi * f;

        // Pi-model: shunt C/2 -- series L -- shunt C/2.
        // We assign full C_pad at each end (top and bottom pads are
        // separate parallel-plate capacitors, not a split single cap).
        Abcd via = Abcd::shunt_admittance(j * w * lumped.C_pad) *
                   Abcd::series_impedance(j * w * lumped.L_barrel) *
                   Abcd::shunt_admittance(j * w * lumped.C_pad);

        // Stub: open-circuited TL of length stub_length, impedance Z_stub.
        // Z_open(L) = -j * Z_stub / tan(beta * L), with optional tan-delta loss
        // baked into gamma. We model it as a shunt admittance at the
        // mid-point (where the connected signal layer meets the barrel),
        // approximated as a shunt branch at the end of the pi-model. For a
        // first-order model that captures the resonance position correctly
        // this is good enough.
        if (spec.stub_length > 0.0) {
            const double alpha = std::numbers::pi * f * spec.tan_delta *
                                 std::sqrt(eps_r) / kC0;
            const double beta  = w / v_p;
            const Complex gamma(alpha, beta);
            const Complex gl   = gamma * spec.stub_length;
            // Open-end input impedance: Z_open = Zs / tanh(gamma * l).
            // With small loss + nonzero length, tanh is well behaved.
            // Use std::cosh/sinh to avoid the cot pole at half-wave.
            const Complex Zo = Zs * std::cosh(gl) / std::sinh(gl);
            const Complex Ys = Complex(1, 0) / Zo;
            via = via * Abcd::shunt_admittance(Ys);
        }

        Complex S11, S12, S21, S22;
        abcd_to_s(via, reference_impedance, S11, S12, S21, S22);

        // Touchstone storage is column-major: flat[r + c*2].
        std::vector<Complex> flat{S11, S21, S12, S22};
        out.s_matrices.push_back(std::move(flat));
    }
    return out;
}

}  // namespace sikit::analysis
