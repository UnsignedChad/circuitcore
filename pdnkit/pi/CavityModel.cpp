#include "pi/CavityModel.h"

#include <Eigen/Dense>

#include <cmath>
#include <numbers>

namespace pdnkit::pi {

namespace {
constexpr double kMu0  = 4.0e-7 * std::numbers::pi;
constexpr double kEps0 = 8.854187817e-12;
}  // namespace

std::complex<double> cavity_impedance(
    const CavityConfig& cfg,
    double x1, double y1, double x2, double y2,
    double omega) {
    using cd = std::complex<double>;
    const cd j(0.0, 1.0);

    const double mu  = cfg.mu_r * kMu0;
    // Complex permittivity: eps = eps_r eps_0 (1 - j tan_delta).
    const cd eps = cfg.eps_r * kEps0 * cd(1.0, -cfg.tan_delta);
    const cd k_squared = omega * omega * mu * eps;

    cd sum(0.0, 0.0);
    const double inv_a = std::numbers::pi / cfg.a;
    const double inv_b = std::numbers::pi / cfg.b;

    for (int m = 0; m <= cfg.max_modes; ++m) {
        const double eps_m = (m == 0) ? 1.0 : 2.0;
        const double kx    = m * inv_a;
        const double cos1x = std::cos(kx * x1);
        const double cos2x = std::cos(kx * x2);
        for (int n = 0; n <= cfg.max_modes; ++n) {
            const double eps_n = (n == 0) ? 1.0 : 2.0;
            const double chi   = eps_m * eps_n;
            const double ky    = n * inv_b;
            const double cos1y = std::cos(ky * y1);
            const double cos2y = std::cos(ky * y2);

            const double k_mn_sq = kx * kx + ky * ky;
            const cd denom = cd(k_mn_sq, 0.0) - k_squared;
            // With non-zero tan_delta the denominator never vanishes (small
            // imaginary floor); skip only the truly degenerate case to be safe.
            if (std::abs(denom) < 1.0e-30) continue;

            sum += chi * cos1x * cos1y * cos2x * cos2y / denom;
        }
    }

    return j * omega * mu * cfg.d / (cfg.a * cfg.b) * sum;
}

std::vector<double> cavity_impedance_magnitude_sweep(
    const CavityConfig& cfg,
    double x1, double y1, double x2, double y2,
    const std::vector<double>& freqs_hz) {
    std::vector<double> out;
    out.reserve(freqs_hz.size());
    for (double f : freqs_hz) {
        const double omega = 2.0 * std::numbers::pi * f;
        out.push_back(std::abs(cavity_impedance(cfg, x1, y1, x2, y2, omega)));
    }
    return out;
}



std::complex<double> cavity_impedance_with_decaps(
    const CavityConfig& cfg,
    double xo, double yo,
    const std::vector<Decap>& decaps,
    double omega) {
    using cd = std::complex<double>;
    const int N = static_cast<int>(decaps.size());

    if (N == 0) return cavity_impedance(cfg, xo, yo, xo, yo, omega);

    auto port_x = [&](int i) { return (i == 0) ? xo : decaps[i - 1].x; };
    auto port_y = [&](int i) { return (i == 0) ? yo : decaps[i - 1].y; };

    Eigen::MatrixXcd Z(N + 1, N + 1);
    for (int i = 0; i <= N; ++i) {
        for (int j = i; j <= N; ++j) {
            const cd z = cavity_impedance(cfg, port_x(i), port_y(i),
                                          port_x(j), port_y(j), omega);
            Z(i, j) = z;
            Z(j, i) = z;  // reciprocity
        }
    }

    Eigen::MatrixXcd Y = Z.inverse();

    const cd j(0.0, 1.0);
    for (int k = 0; k < N; ++k) {
        const auto& d = decaps[k];
        // Z_decap(omega) = ESR + jw*(ESL + L_mount) - j/(wC).
        // L_mount captures the via loop from cap pads to the planes
        // (Ruehli partial inductance, see pi/ViaInductance.h). Defaults
        // to 0 -> identical to the old behavior.
        if (d.C <= 0.0) continue;
        const double l_eff = d.esl + d.mounting_via_loop_l_h;
        const cd z_dec = d.esr + j * omega * l_eff - j / (omega * d.C);
        if (std::abs(z_dec) > 0.0) Y(k + 1, k + 1) += 1.0 / z_dec;
    }

    Eigen::MatrixXcd Z_new = Y.inverse();
    return Z_new(0, 0);
}

std::vector<double> cavity_impedance_with_decaps_magnitude_sweep(
    const CavityConfig& cfg,
    double xo, double yo,
    const std::vector<Decap>& decaps,
    const std::vector<double>& freqs_hz) {
    std::vector<double> out;
    out.reserve(freqs_hz.size());
    for (double f : freqs_hz) {
        const double omega = 2.0 * std::numbers::pi * f;
        out.push_back(std::abs(cavity_impedance_with_decaps(cfg, xo, yo, decaps, omega)));
    }
    return out;
}


std::vector<double> cavity_mode_shape_grid(
    const CavityConfig& cfg,
    double obs_x, double obs_y,
    double omega,
    int nx, int ny) {
    std::vector<double> out;
    if (nx < 1 || ny < 1) return out;
    out.reserve(static_cast<std::size_t>(nx) * ny);

    const double dx = cfg.a / nx;
    const double dy = cfg.b / ny;
    for (int j = 0; j < ny; ++j) {
        const double y = (j + 0.5) * dy;
        for (int i = 0; i < nx; ++i) {
            const double x = (i + 0.5) * dx;
            out.push_back(std::abs(
                cavity_impedance(cfg, obs_x, obs_y, x, y, omega)));
        }
    }
    return out;
}

}  // namespace pdnkit::pi
