// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Lumped port + S-parameter extraction for FDTD3D.
//
// What this is
//
//   The final piece of the 3.15 full-wave epic. Wraps FDTD3D's hard /
//   soft sources into a "lumped port" abstraction that knows its own
//   driving waveform, records the field at its location over time,
//   and produces a frequency-domain S-parameter via FFT.
//
// Sources: hard vs. soft
//
//   FDTD3D::HardESource overwrites E at the port cell each step.
//   That's what we want for "drive this voltage", but it acts as a
//   PEC for any incoming reflected wave -- reflections bounce off
//   the source point and corrupt the measurement. The soft source
//   added in this PR adds the drive waveform to whatever E was just
//   updated by the curl, so the cell is "transparent" to incoming
//   waves: drive injects, reflections pass through.
//
// S-parameter extraction
//
//   For a single-port problem, we run two simulations:
//     1. Total run: solver with geometry + soft source at port
//     2. Reference run: solver in vacuum with the same source
//   The reference gives V_inc(t); subtracting from V_total(t) gives
//   V_ref(t). S11(f) = FFT(V_ref) / FFT(V_inc).
//
//   For convenience we also expose the lower-level building blocks
//   (gaussian_modulated_sinusoid, extract_s11_from_histories) so a
//   user can construct multi-port flows.

#pragma once

#include <complex>
#include <vector>

#include "si/Fdtd3d.h"

namespace sikit::fdtd {

// Soft E-field source. At every step, ADDS sample[n] to the chosen E
// component at (i, j, k) AFTER the curl update. Multiple sources can
// target the same cell; their contributions sum.
struct SoftESource {
    int i = 0, j = 0, k = 0;
    enum class Comp { Ex, Ey, Ez } comp = Comp::Ez;
    std::vector<double> samples;
};

// Single-cell lumped port. The port "drives" via a SoftESource and
// "probes" by reading the E component at the same cell.
//
// For a quasi-TEM port at a microstrip launch, the user would set
// (i, j, k) to one cell above the ground plane on the launch end of
// the trace, and pick comp = Ez (normal to the ground plane).
struct LumpedPort {
    int i = 0, j = 0, k = 0;
    SoftESource::Comp comp = SoftESource::Comp::Ez;
    double z0 = 50.0;  // reference impedance, ohms (unused in the
                       //                          incident/reflected
                       //                          split but stored for
                       //                          downstream consumers)
    std::vector<double> drive;  // pre-computed drive waveform
};

// Time-domain Gaussian-modulated sinusoid:
//   v(t) = exp(-((t-t0)/spread)^2) * sin(2*pi*fc*(t-t0))
// Standard broadband excitation in FDTD: the spectrum is centred at
// fc with a half-power bandwidth proportional to 1/spread. Choose
// t0 >= 3*spread so v(t<0) is essentially zero.
double gaussian_modulated_sinusoid(double t, double t0, double spread,
                                    double fc);

// Convenience: pre-compute samples[0..n_steps-1] of the modulated
// Gaussian with the given parameters and timestep.
std::vector<double> make_gms_drive(int n_steps, double dt,
                                     double t0, double spread, double fc);

// FFT-based S11 extraction.
//
//   v_inc(t):    voltage history from a reference (vacuum) run
//   v_total(t):  voltage history from the actual run
//   dt:          time step (seconds)
//   freqs:       frequencies (Hz) to evaluate S11 at; need not be on
//                an FFT-natural grid -- interpolation is done linearly
//                across the FFT bins.
//
// Returns S11 evaluated at each requested frequency. Histories are
// zero-padded to the next power of 2 internally.
std::vector<std::complex<double>>
extract_s11_from_histories(const std::vector<double>& v_inc,
                              const std::vector<double>& v_total,
                              double dt,
                              const std::vector<double>& freqs);

// Wire the port up to an FDTD3D instance: install a SoftESource that
// plays the port's drive waveform, run for the drive length steps,
// and return the field history at the port cell. Single-step convenience
// for tests + the demo CLI.
std::vector<double> run_with_port(FDTD3D& s, const LumpedPort& p);

}  // namespace sikit::fdtd
