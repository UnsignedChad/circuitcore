#include "si/DjordjevicSarkar.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace sikit::dispersion {

namespace {

// A(ω) = ln((ω2 + jω) / (ω1 + jω)) / ln(ω2 / ω1)
//
// This is the dimensionless shape of the dispersion. Re(A) varies from
// ~1 at low ω to ~0 at high ω; Im(A) is negative everywhere in the band
// and contributes positive ε'' for lossy material (since ε = ε_∞ + Δε·A
// and ε'' = -Im(ε)).
std::complex<double> shape_A(double f, double f1, double f2) {
    using std::complex;
    const double w  = 2.0 * std::numbers::pi * f;
    const double w1 = 2.0 * std::numbers::pi * f1;
    const double w2 = 2.0 * std::numbers::pi * f2;
    const complex<double> num(w2,  w);
    const complex<double> den(w1,  w);
    const double real_denom = std::log(w2 / w1);
    return std::log(num / den) / real_denom;
}

}  // namespace

DjordjevicSarkar::DjordjevicSarkar(double eps_inf, double delta_eps,
                                     double f1_hz, double f2_hz)
    : eps_inf_(eps_inf),
      delta_eps_(delta_eps),
      f1_(f1_hz),
      f2_(f2_hz) {
    if (f1_ <= 0.0 || f2_ <= f1_) {
        throw std::invalid_argument(
            "DjordjevicSarkar: require 0 < f1 < f2");
    }
}

DjordjevicSarkar DjordjevicSarkar::from_reference(double eps_r,
                                                    double tan_delta,
                                                    double f0_hz,
                                                    double f1_hz,
                                                    double f2_hz) {
    if (f0_hz <= 0.0 || eps_r <= 0.0) {
        throw std::invalid_argument(
            "DjordjevicSarkar::from_reference: bad inputs");
    }
    const auto A0 = shape_A(f0_hz, f1_hz, f2_hz);
    // At f0:
    //   εr_real = ε_∞ + Δε · Re(A0)
    //   εr_imag = Δε · Im(A0)             (Im(A0) < 0)
    //   tan_δ   = -εr_imag / εr_real = -Δε · Im(A0) / εr_real
    //   ⇒ Δε = -tan_δ · εr_real / Im(A0)
    const double im_A0 = A0.imag();
    if (std::abs(im_A0) < 1.0e-30) {
        throw std::invalid_argument(
            "DjordjevicSarkar: f0 outside (f1, f2) band — Im(A) ≈ 0");
    }
    const double delta_eps = -tan_delta * eps_r / im_A0;
    const double eps_inf   = eps_r - delta_eps * A0.real();
    return DjordjevicSarkar(eps_inf, delta_eps, f1_hz, f2_hz);
}

std::complex<double> DjordjevicSarkar::epsilon_complex(double f_hz) const {
    return std::complex<double>(eps_inf_, 0.0) +
           delta_eps_ * shape_A(f_hz, f1_, f2_);
}

double DjordjevicSarkar::epsilon_r(double f_hz) const {
    return epsilon_complex(f_hz).real();
}

double DjordjevicSarkar::tan_delta(double f_hz) const {
    const auto e = epsilon_complex(f_hz);
    if (e.real() <= 0.0) return 0.0;
    return -e.imag() / e.real();
}

}  // namespace sikit::dispersion
