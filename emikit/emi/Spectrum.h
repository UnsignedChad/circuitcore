// Harmonic content of a trapezoidal pulse train.
//
// Digital signals don't radiate at their fundamental clock rate alone;
// every clock has a tail of odd harmonics that decay slowly and a
// rise-time-determined upper roll-off. The classic Mark Montrose
// envelope (EMC and the Printed Circuit Board, ch. 5) puts the spectrum
// at frequency n*f_clock at
//
//     |I_n| = 2*I_peak*(tau/T) * |sinc(n*pi*tau/T)| * |sinc(n*pi*t_r/T)|
//
// where tau is pulse width, t_r is rise (or fall) time, T is period.
// The two sincs give the Bode-style two-corner envelope: -20 dB/decade
// past 1/(pi*tau) and -40 dB/decade past 1/(pi*t_r). What every EMC
// textbook shows on slide 3.
//
// We expose two entry points: the per-harmonic magnitude, and a sweep
// that returns the envelope across a frequency grid (useful for
// plotting against a CISPR mask).

#pragma once

#include <vector>

namespace emikit::emi {

struct TrapezoidalSpec {
    double i_peak_a = 1.0e-3;    // peak current swing, amperes (1 mA default)
    double period_s = 1.0e-8;    // 1 / f_clock; default 100 MHz -> 10 ns
    double duty_cycle = 0.5;     // tau / T; 50% standard for symmetric clocks
    double rise_time_s = 1.0e-9; // rise (and fall) time; default 1 ns
};

// Magnitude (amperes) of the n-th harmonic (n >= 1). Returns 0 for the
// degenerate cases (zero period, n == 0).
double harmonic_magnitude(const TrapezoidalSpec& spec, int n);

// Sweep |I(f)| across a frequency grid. For each f we pick the nearest
// integer harmonic n and evaluate harmonic_magnitude there. This is the
// step-function envelope that overlays cleanly on a CISPR mask plot.
std::vector<double> spectrum_sweep(const TrapezoidalSpec& spec,
                                    const std::vector<double>& freq_hz);

// The two corner frequencies of the envelope. Useful for sanity-checking
// the rise-time-vs-bit-rate trade-off the user is making.
struct EnvelopeCorners {
    double f_tau_hz;   // = 1 / (pi * tau);  start of -20 dB/decade
    double f_tr_hz;    // = 1 / (pi * t_r);  start of -40 dB/decade
};
EnvelopeCorners envelope_corners(const TrapezoidalSpec& spec);

}  // namespace emikit::emi
