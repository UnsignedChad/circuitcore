// S-parameter math: 2-port cascade via T-parameter conversion, plus
// insertion/return loss helpers and mixed-mode (differential) extraction
// from a 4-port single-ended block.
//
// Reference: Pozar, Microwave Engineering, Ch. 4. T-parameter cascade is
// the textbook trick: convert each S-matrix to its T (transmission) form,
// multiply T_A · T_B, convert back to S.

#pragma once

#include <complex>
#include <stdexcept>

#include <Eigen/Dense>

#include "si/Touchstone.h"

#include <vector>

namespace sikit::sparam {

using Complex = std::complex<double>;

// Convert a 2-port S-matrix to its T (transmission) form. Throws if S21 is
// zero (cascading a perfectly blocking network is undefined).
Eigen::Matrix2cd s_to_t(const Eigen::Matrix2cd& s);

// Inverse of s_to_t. Throws if T22 is zero.
Eigen::Matrix2cd t_to_s(const Eigen::Matrix2cd& t);

// Cascade two 2-port networks A → B at a single frequency point.
//   S_cascade = T_to_S( S_to_T(A) · S_to_T(B) )
Eigen::Matrix2cd cascade(const Eigen::Matrix2cd& a,
                          const Eigen::Matrix2cd& b);

// Cascade two Touchstone files frequency-by-frequency. Both must be 2-port
// and share the same frequency grid.
touchstone::TouchstoneFile cascade(const touchstone::TouchstoneFile& a,
                                    const touchstone::TouchstoneFile& b);

// Port-ordering convention used by mixed-mode conversion. Convention
// names match the way Touchstone files in the wild are organised:
//   PNPN: [P1, N1, P2, N2]   most VNA exports + Pozar §7.2
//   PPNN: [P1, P2, N1, N2]   the order DiffSynth.cpp emits
enum class PortOrder {
    PNPN,
    PPNN,
};

// Extract the differential-mode 2×2 S-matrix Sdd from a 4-port
// single-ended block. Port order defaults to PNPN.
Eigen::Matrix2cd single_ended_to_differential(const Eigen::Matrix4cd& s_se,
                                              PortOrder order = PortOrder::PNPN);

// Full mixed-mode 4×4: layout is
//
//     [ Sdd11 Sdd12 | Sdc11 Sdc12 ]
//     [ Sdd21 Sdd22 | Sdc21 Sdc22 ]
//     [-------------+-------------]
//     [ Scd11 Scd12 | Scc11 Scc12 ]
//     [ Scd21 Scd22 | Scc21 Scc22 ]
//
// where d = differential mode (P − N)/√2, c = common mode (P + N)/√2.
// The top-left 2×2 is the same Sdd block returned by
// single_ended_to_differential. The off-diagonal blocks measure mode
// conversion -- Sdc != 0 means an applied common-mode signal produces
// differential noise, which is exactly the EMI failure mode skew creates.
Eigen::Matrix4cd single_ended_to_mixed_mode(const Eigen::Matrix4cd& s_se,
                                            PortOrder order = PortOrder::PNPN);

// Convert an entire 4-port Touchstone file to a 4-port mixed-mode
// Touchstone file. Port semantics in the result are [d1, d2, c1, c2]:
// the output is still N=4 so any plot widget that handles 4-port files
// works directly, but the curves represent S_dd / S_dc / S_cd / S_cc.
touchstone::TouchstoneFile to_mixed_mode(const touchstone::TouchstoneFile& a,
                                          PortOrder order = PortOrder::PNPN);

// Insertion loss in dB. Positive values = attenuation.
double insertion_loss_db(Complex s21);

// Return loss in dB. Positive values = good match (low reflection).
double return_loss_db(Complex s11);

// ------------- Time-Domain Reflectometry / Transmission --------------
//
// Build a step-response impedance trace Z(t) from a single S_ii curve
// (typically S11). This is the textbook IFFT-then-cumsum approach:
//   * input spectrum is windowed (Hann) over the measured band
//   * bins below the lowest measured frequency stay zero (we make no
//     attempt to extrapolate to DC; the trace shape is correct but the
//     absolute Z reading floats by a constant offset)
//   * conjugate-symmetric two-sided spectrum -> IFFT -> impulse response
//   * cumulative sum of the real part -> step response rho(t)
//   * Z(t) = Zref * (1+rho) / (1-rho), clamped so |rho| < 0.99
//
// Frequencies must be on a uniform grid (df = freqs[1]-freqs[0]); the
// helper picks an FFT size as the next power of 2 above 2*(k0+K) where
// k0 = round(freqs[0]/df) is the DC offset of the band.
struct TdrResult {
    std::vector<double> time;    // seconds
    std::vector<double> value;   // ohms (TDR) or dimensionless (TDT)
};

TdrResult tdr_step_response(const std::vector<double>& freqs,
                             const std::vector<Complex>& s_ii,
                             double z_ref = 50.0);

// Same FFT skeleton, but for transmission (S21 etc.). Returns the
// step response in [0,1] amplitude rather than impedance.
TdrResult tdt_step_response(const std::vector<double>& freqs,
                             const std::vector<Complex>& s_ij);

// ------------- De-embedding (Tier 3.18) -----------------------------
//
// Strip a known fixture network from a measured Touchstone file. The
// VNA captured the cascade of fixture_left + DUT + fixture_right; we
// want just the DUT.
//
// In T-parameter space the cascade is a product (s_to_t chain), so the
// inverse extraction is a matrix-inversion sandwich:
//
//     T_meas = T_left * T_DUT * T_right
//     T_DUT  = T_left^-1 * T_meas * T_right^-1
//
// Done per frequency. All three files must be 2-port, share the same
// frequency grid (within a 1e-9 tolerance), and have the same reference
// impedance -- the cascade math assumes a consistent Z_ref throughout.
//
// Reference: Pozar, Microwave Engineering, Ch. 4 (T-parameter cascade
// + inversion); Agilent Application Note AN 1364-1 "De-embedding and
// Embedding S-parameter Networks Using a Vector Network Analyzer".
//
// The symmetric helper is the common case: SMA test fixture identical
// on both sides of the DUT.

touchstone::TouchstoneFile deembed(
    const touchstone::TouchstoneFile& measured,
    const touchstone::TouchstoneFile& fixture_left,
    const touchstone::TouchstoneFile& fixture_right);

touchstone::TouchstoneFile deembed_symmetric(
    const touchstone::TouchstoneFile& measured,
    const touchstone::TouchstoneFile& fixture);

// Inverse of a 2-port T-matrix. Exposed for callers who want to
// verify a fixture file by checking that T * T^-1 ~= I. Throws if T
// is singular (det T ~ 0).
Eigen::Matrix2cd invert_t(const Eigen::Matrix2cd& t);

struct SParamError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace sikit::sparam
