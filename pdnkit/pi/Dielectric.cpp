#include "pi/Dielectric.h"

#include <cmath>

namespace pdnkit::pi {

DielectricSample dj_sarkar_at(const DjordjevicSarkar& m, double f_hz) {
    DielectricSample s;
    if (f_hz <= 0.0 || m.f1_hz <= 0.0 || m.f2_hz <= m.f1_hz) {
        s.eps_r_real = m.eps_inf;
        return s;
    }
    const double L12 = std::log(m.f2_hz / m.f1_hz);
    const double num = m.f2_hz * m.f2_hz + f_hz * f_hz;
    const double den = m.f1_hz * m.f1_hz + f_hz * f_hz;
    s.eps_r_real = m.eps_inf
        + (m.delta_eps / (2.0 * L12)) * std::log(num / den);
    s.eps_r_imag = (m.delta_eps / L12)
        * (std::atan(f_hz / m.f1_hz) - std::atan(f_hz / m.f2_hz));
    s.tan_delta = (s.eps_r_real > 0.0) ? (s.eps_r_imag / s.eps_r_real) : 0.0;
    return s;
}

}  // namespace pdnkit::pi
