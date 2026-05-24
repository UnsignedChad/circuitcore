// Frequency-domain plane impedance via the cavity model.
//
// For a rectangular plane pair (a x b) separated by dielectric of thickness d,
// the self/transfer impedance between two ports (x1,y1) and (x2,y2) is the
// classical mode-sum result (Okoshi 1972; see Swaminathan, Power Integrity
// Modeling and Design, eq. 5.21):
//
//   Z(w) = (j w mu d / (a b)) * SUM_{m,n} chi_mn * cos(m pi x1/a) cos(n pi y1/b)
//                                              * cos(m pi x2/a) cos(n pi y2/b)
//                                              / (k_mn^2 - k^2)
//
// where k_mn^2 = (m pi/a)^2 + (n pi/b)^2 and k^2 = w^2 * mu * eps. The
// Neumann normalization chi_mn = eps_m * eps_n with eps_m = 1 (m=0) else 2.
//
// Loss tangent enters via eps = eps_r * eps_0 * (1 - j tan_delta) so the
// imaginary part of eps damps the resonances finitely.
//
// This is the cavity-shape closed form: applies to rectangular planes. The
// segmentation method that extends it to arbitrary planar shapes lives in
// a later commit.

#pragma once

#include <complex>
#include <vector>

namespace pdnkit::pi {

struct CavityConfig {
    double a = 0.100;          // plane width  (m)
    double b = 0.100;          // plane height (m)
    double d = 1.6e-3;         // plane separation (m) — board substrate thickness
    double eps_r = 4.3;        // relative permittivity (FR-4 default)
    double mu_r = 1.0;         // relative permeability (~1 for PCB dielectrics)
    double tan_delta = 0.020;  // dielectric loss tangent (FR-4 default)
    int max_modes = 30;        // m, n each summed 0..max_modes inclusive
};

// Self/transfer impedance at angular frequency omega (rad/s) between ports
// at (x1,y1) and (x2,y2). Origin is the bottom-left of the plane.
std::complex<double> cavity_impedance(
    const CavityConfig& cfg,
    double x1, double y1, double x2, double y2,
    double omega);

// Convenience: sweep over a list of frequencies (Hz), return |Z| in ohms.
std::vector<double> cavity_impedance_magnitude_sweep(
    const CavityConfig& cfg,
    double x1, double y1, double x2, double y2,
    const std::vector<double>& freqs_hz);

// One decoupling capacitor mounted somewhere on the plane.
struct Decap {
    double x = 0.0;     // position, meters (plane-local origin)
    double y = 0.0;
    double C  = 1.0e-6; // capacitance (F)
    double esr = 0.005; // equivalent series resistance (ohms)
    double esl = 0.5e-9;// equivalent series inductance (H)
};

// Self-impedance at the observation port (xo, yo) with N decaps mounted at
// decaps[k].(x,y). Builds the (N+1)x(N+1) Z-matrix from cavity_impedance,
// inverts to Y, adds each decap admittance on the diagonal, re-inverts, and
// returns Z'[0,0]. N small (<= ~30 in practice).
std::complex<double> cavity_impedance_with_decaps(
    const CavityConfig& cfg,
    double xo, double yo,
    const std::vector<Decap>& decaps,
    double omega);

// Evaluate the cavity-model transfer impedance |Z(obs, grid_point, omega)|
// on a regular nx*ny grid spanning the plane. Returned in row-major order
// (out[j*nx + i] is the cell at column i, row j). Used by the renderer to
// paint the standing-wave pattern of a plane resonance.
std::vector<double> cavity_mode_shape_grid(
    const CavityConfig& cfg,
    double obs_x, double obs_y,
    double omega,
    int nx, int ny);

// |Z| sweep with decaps.
std::vector<double> cavity_impedance_with_decaps_magnitude_sweep(
    const CavityConfig& cfg,
    double xo, double yo,
    const std::vector<Decap>& decaps,
    const std::vector<double>& freqs_hz);

}  // namespace pdnkit::pi
