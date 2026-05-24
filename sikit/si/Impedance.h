// Closed-form transmission-line impedance for common PCB cross-sections.
//
// Implements IPC-2141A formulas, which match Wadell's transmission-line
// handbook for the common cases (microstrip + stripline + edge-coupled
// diff pairs). Accurate to ±5–10% in the typical PCB regime, which
// covers ~80% of real designs without a 2.5D field solver.
//
// References:
//   IPC-2141A, Design Guide for High-Speed Controlled Impedance Circuit Boards
//   Wadell, Transmission Line Design Handbook (Artech House, 1991)

#pragma once

#include <stdexcept>

namespace sikit::impedance {

struct MicrostripParams {
    double trace_width;        // W (m)
    double dielectric_height;  // H (m) — height of dielectric below trace
    double trace_thickness;    // T (m) — copper thickness; 1oz ≈ 35e-6 m
    double epsilon_r;          // relative permittivity of substrate
};

struct StriplineParams {
    double trace_width;        // W (m)
    double plane_separation;   // B (m) — distance between two reference planes
    double trace_thickness;    // T (m)
    double epsilon_r;
};

struct ImpedanceError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Single-ended characteristic impedance Z₀ (ohms).
//
// Microstrip (IPC-2141A, valid for 0.1 < W/H < 2.0 and 1 < εr < 15):
//     Z₀ = (87 / √(εr + 1.41)) · ln(5.98·H / (0.8·W + T))
//
// Stripline (IPC-2141A symmetric, valid for W/(B-T) < 0.35 and T/B < 0.25):
//     Z₀ = (60 / √εr) · ln(4·B / (0.67·π·(0.8·W + T)))
double microstrip_z0(const MicrostripParams& p);
double stripline_z0(const StriplineParams& p);

// Differential impedance for edge-coupled pairs, given the single-ended
// Z₀ for one trace and the edge-to-edge spacing S.
//
// Microstrip:  Zdiff = 2·Z₀ · (1 - 0.48 · exp(-0.96·S/H))
// Stripline:   Zdiff = 2·Z₀ · (1 - 0.347 · exp(-2.9·S/B))
double edge_coupled_microstrip_diff(double single_z0,
                                    double spacing,
                                    double dielectric_height);
double edge_coupled_stripline_diff(double single_z0,
                                   double spacing,
                                   double plane_separation);

// Validity flags — formulas extrapolate outside these but accuracy drops fast.
bool microstrip_in_valid_range(const MicrostripParams& p);
bool stripline_in_valid_range(const StriplineParams& p);

}  // namespace sikit::impedance
