#include "si/Fdtd3d.h"

#include <algorithm>
#include <cmath>

namespace sikit::fdtd {

double cfl_dt(const GridSpec& g, double safety) {
    if (g.dx <= 0 || g.dy <= 0 || g.dz <= 0) {
        throw std::invalid_argument("cfl_dt: non-positive cell size");
    }
    const double inv2 = 1.0 / (g.dx * g.dx)
                      + 1.0 / (g.dy * g.dy)
                      + 1.0 / (g.dz * g.dz);
    return safety / (kC0 * std::sqrt(inv2));
}

double gaussian_pulse(double t, double t0, double spread) {
    const double x = (t - t0) / spread;
    return std::exp(-x * x);
}

double cavity_mode_freq(double a, double b, double d, int m, int n, int p) {
    const double mx = (a > 0) ? (m / a) : 0.0;
    const double my = (b > 0) ? (n / b) : 0.0;
    const double mz = (d > 0) ? (p / d) : 0.0;
    return 0.5 * kC0 * std::sqrt(mx * mx + my * my + mz * mz);
}

FDTD3D::FDTD3D(const GridSpec& g) : g_(g) {
    if (g.nx <= 0 || g.ny <= 0 || g.nz <= 0) {
        throw std::invalid_argument("FDTD3D: non-positive grid");
    }
    if (g.dx <= 0 || g.dy <= 0 || g.dz <= 0) {
        throw std::invalid_argument("FDTD3D: non-positive cell size");
    }
    // Yee allocation -- sizes documented in the header.
    ex_.resize(g.nx,     g.ny + 1, g.nz + 1);
    ey_.resize(g.nx + 1, g.ny,     g.nz + 1);
    ez_.resize(g.nx + 1, g.ny + 1, g.nz    );
    hx_.resize(g.nx + 1, g.ny,     g.nz    );
    hy_.resize(g.nx,     g.ny + 1, g.nz    );
    hz_.resize(g.nx,     g.ny,     g.nz + 1);
}

void FDTD3D::set_dt_from_cfl(double safety) {
    dt_ = cfl_dt(g_, safety);
}

void FDTD3D::set_dt(double dt) {
    if (dt <= 0) throw std::invalid_argument("FDTD3D::set_dt: dt<=0");
    dt_ = dt;
}

void FDTD3D::add_hard_e_source(HardESource src) {
    sources_.push_back(std::move(src));
}

std::size_t FDTD3D::bytes() const {
    return (ex_.size() + ey_.size() + ez_.size()
           + hx_.size() + hy_.size() + hz_.size()) * sizeof(double);
}

// --- update equations ----------------------------------------------------
//
// H curl is computed from E samples one cell apart -- the staggered grid
// makes the centered difference free. PEC boundary means tangential E
// on the outer faces is zero (we never write it), so H updates near the
// boundary just read those zeros and produce a correct reflection.

void FDTD3D::update_h() {
    const double cx = dt_ / (kMu0 * g_.dx);
    const double cy = dt_ / (kMu0 * g_.dy);
    const double cz = dt_ / (kMu0 * g_.dz);

    // Hx[i, j, k] = Hx[i, j, k] - cy*(Ez[i, j+1, k] - Ez[i, j, k])
    //                          + cz*(Ey[i, j, k+1] - Ey[i, j, k])
    // index ranges follow the Yee allocation sizes.
    for (int k = 0; k < g_.nz; ++k) {
        for (int j = 0; j < g_.ny; ++j) {
            for (int i = 0; i <= g_.nx; ++i) {
                const double curl_e =
                    cy * (ez_.at(i, j + 1, k) - ez_.at(i, j, k))
                  - cz * (ey_.at(i, j, k + 1) - ey_.at(i, j, k));
                hx_.at(i, j, k) -= curl_e;
            }
        }
    }
    // Hy[i, j, k] = Hy[i, j, k] - cz*(Ex[i, j, k+1] - Ex[i, j, k])
    //                          + cx*(Ez[i+1, j, k] - Ez[i, j, k])
    for (int k = 0; k < g_.nz; ++k) {
        for (int j = 0; j <= g_.ny; ++j) {
            for (int i = 0; i < g_.nx; ++i) {
                const double curl_e =
                    cz * (ex_.at(i, j, k + 1) - ex_.at(i, j, k))
                  - cx * (ez_.at(i + 1, j, k) - ez_.at(i, j, k));
                hy_.at(i, j, k) -= curl_e;
            }
        }
    }
    // Hz[i, j, k] = Hz[i, j, k] - cx*(Ey[i+1, j, k] - Ey[i, j, k])
    //                          + cy*(Ex[i, j+1, k] - Ex[i, j, k])
    for (int k = 0; k <= g_.nz; ++k) {
        for (int j = 0; j < g_.ny; ++j) {
            for (int i = 0; i < g_.nx; ++i) {
                const double curl_e =
                    cx * (ey_.at(i + 1, j, k) - ey_.at(i, j, k))
                  - cy * (ex_.at(i, j + 1, k) - ex_.at(i, j, k));
                hz_.at(i, j, k) -= curl_e;
            }
        }
    }
}

void FDTD3D::update_e() {
    const double cx = dt_ / (kEps0 * g_.dx);
    const double cy = dt_ / (kEps0 * g_.dy);
    const double cz = dt_ / (kEps0 * g_.dz);

    // Ex[i, j, k] = Ex[i, j, k] + cy*(Hz[i, j, k] - Hz[i, j-1, k])
    //                          - cz*(Hy[i, j, k] - Hy[i, j, k-1])
    // j and k start at 1 because j=0/k=0 is the tangential-E PEC wall.
    for (int k = 1; k < g_.nz; ++k) {
        for (int j = 1; j < g_.ny; ++j) {
            for (int i = 0; i < g_.nx; ++i) {
                const double curl_h =
                    cy * (hz_.at(i, j, k) - hz_.at(i, j - 1, k))
                  - cz * (hy_.at(i, j, k) - hy_.at(i, j, k - 1));
                ex_.at(i, j, k) += curl_h;
            }
        }
    }
    // Ey: tangential at i=0 / k=0
    for (int k = 1; k < g_.nz; ++k) {
        for (int j = 0; j < g_.ny; ++j) {
            for (int i = 1; i < g_.nx; ++i) {
                const double curl_h =
                    cz * (hx_.at(i, j, k) - hx_.at(i, j, k - 1))
                  - cx * (hz_.at(i, j, k) - hz_.at(i - 1, j, k));
                ey_.at(i, j, k) += curl_h;
            }
        }
    }
    // Ez: tangential at i=0 / j=0
    for (int k = 0; k < g_.nz; ++k) {
        for (int j = 1; j < g_.ny; ++j) {
            for (int i = 1; i < g_.nx; ++i) {
                const double curl_h =
                    cx * (hy_.at(i, j, k) - hy_.at(i - 1, j, k))
                  - cy * (hx_.at(i, j, k) - hx_.at(i, j - 1, k));
                ez_.at(i, j, k) += curl_h;
            }
        }
    }
    // Tangential-E PEC: i=nx, j=ny, k=nz faces are also clamped to 0.
    // The loops above never write those cells (they were initialised to
    // zero and stay zero), so we don't need an explicit zero pass.
}

void FDTD3D::apply_sources() {
    for (const auto& s : sources_) {
        if (s.samples.empty()) continue;
        const int n = std::min<int>(static_cast<int>(s.samples.size()) - 1,
                                       n_steps_);
        const double v = s.samples[n];
        switch (s.comp) {
            case HardESource::Comp::Ex: ex_.at(s.i, s.j, s.k) = v; break;
            case HardESource::Comp::Ey: ey_.at(s.i, s.j, s.k) = v; break;
            case HardESource::Comp::Ez: ez_.at(s.i, s.j, s.k) = v; break;
        }
    }
}

void FDTD3D::step() {
    if (dt_ <= 0) {
        throw std::runtime_error(
            "FDTD3D::step: dt not set (call set_dt_from_cfl first)");
    }
    update_h();
    update_e();
    apply_sources();
    ++n_steps_;
}

}  // namespace sikit::fdtd
