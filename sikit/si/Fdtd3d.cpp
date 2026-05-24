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
    ex_.resize(g.nx,     g.ny + 1, g.nz + 1);
    ey_.resize(g.nx + 1, g.ny,     g.nz + 1);
    ez_.resize(g.nx + 1, g.ny + 1, g.nz    );
    hx_.resize(g.nx + 1, g.ny,     g.nz    );
    hy_.resize(g.nx,     g.ny + 1, g.nz    );
    hz_.resize(g.nx,     g.ny,     g.nz + 1);
}

void FDTD3D::set_dt_from_cfl(double safety) { dt_ = cfl_dt(g_, safety); }

void FDTD3D::set_dt(double dt) {
    if (dt <= 0) throw std::invalid_argument("FDTD3D::set_dt: dt<=0");
    dt_ = dt;
}

void FDTD3D::set_boundary(Boundary b) {
    boundary_ = b;
    // Defer buffer allocation until step() so the dt is known (the
    // Mur coefficient is dt-dependent).
    mur_buffers_ready_ = false;
}

void FDTD3D::add_hard_e_source(HardESource src) {
    sources_.push_back(std::move(src));
}

std::size_t FDTD3D::bytes() const {
    return (ex_.size() + ey_.size() + ez_.size()
           + hx_.size() + hy_.size() + hz_.size()) * sizeof(double);
}

// --- core curl updates ---------------------------------------------------
//
// H curl is computed from E samples one cell apart -- the staggered grid
// makes the centered difference free. PEC boundary means tangential E on
// the outer faces is zero and we never write it. Mur ABC writes the same
// cells AFTER update_e, via apply_mur_abc().

void FDTD3D::update_h() {
    const double cx = dt_ / (kMu0 * g_.dx);
    const double cy = dt_ / (kMu0 * g_.dy);
    const double cz = dt_ / (kMu0 * g_.dz);

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

// --- Mur 1st-order ABC ---------------------------------------------------
//
// For a face perpendicular to x (here, the -x face at i=0), tangential
// E components Ey and Ez are governed by the one-way wave equation
// using a centred finite-difference discretisation from Taflove eq. 7.34:
//
//   E[0]^{n+1} = E[1]^n  +  k*(E[1]^{n+1} - E[0]^n)
//   k = (c*dt - dx) / (c*dt + dx)
//
// We snapshot E[0] and E[1] BEFORE update_e (so they're the time-n
// values), then after update_e the interior has the time-(n+1) values
// at E[1]. Combine to get the new boundary value, then write it back.
//
// Note dx in this formula is the cell pitch perpendicular to the face;
// for the y face we use dy, for the z face we use dz.

namespace {

inline double mur_k(double c_dt, double d) {
    return (c_dt - d) / (c_dt + d);
}

// 2D buffer indexed by (a, b) with stride na.
inline double& tap(std::vector<double>& v, int a, int b, int na) {
    return v[static_cast<std::size_t>(a)
            + static_cast<std::size_t>(b) * na];
}

}  // namespace

void FDTD3D::allocate_mur_buffers() {
    if (boundary_ != Boundary::Mur1stOrder) return;
    const int nx = g_.nx, ny = g_.ny, nz = g_.nz;

    auto alloc = [](MurFace& f, int na_a, int nb_a, int na_b, int nb_b) {
        f.na_a = na_a; f.nb_a = nb_a;
        f.na_b = na_b; f.nb_b = nb_b;
        f.prev_t0_a.assign(static_cast<std::size_t>(na_a) * nb_a, 0.0);
        f.prev_t1_a.assign(static_cast<std::size_t>(na_a) * nb_a, 0.0);
        f.prev_t0_b.assign(static_cast<std::size_t>(na_b) * nb_b, 0.0);
        f.prev_t1_b.assign(static_cast<std::size_t>(na_b) * nb_b, 0.0);
    };
    // x faces: tangential Ey (ny, nz+1), Ez (ny+1, nz).
    alloc(mur_xlo_, ny,    nz + 1, ny + 1, nz);
    alloc(mur_xhi_, ny,    nz + 1, ny + 1, nz);
    // y faces: tangential Ex (nx, nz+1), Ez (nx+1, nz).
    alloc(mur_ylo_, nx,    nz + 1, nx + 1, nz);
    alloc(mur_yhi_, nx,    nz + 1, nx + 1, nz);
    // z faces: tangential Ex (nx, ny+1), Ey (nx+1, ny).
    alloc(mur_zlo_, nx,    ny + 1, nx + 1, ny);
    alloc(mur_zhi_, nx,    ny + 1, nx + 1, ny);

    mur_buffers_ready_ = true;
}

// MurFace::na_a etc. are protected; resolve via helper accessors below.
//
// "snapshot" reads E[boundary] and E[boundary+/-1] BEFORE update_e.
// "close" computes the new boundary value from the snapshot + the
// post-update interior, then writes back.

void FDTD3D::apply_mur_abc() {
    if (boundary_ != Boundary::Mur1stOrder) return;
    if (!mur_buffers_ready_) return;

    const int nx = g_.nx, ny = g_.ny, nz = g_.nz;
    const double cdt = kC0 * dt_;
    const double kx = mur_k(cdt, g_.dx);
    const double ky = mur_k(cdt, g_.dy);
    const double kz = mur_k(cdt, g_.dz);

    // The fully-correct Mur 1st-order needs the boundary value BEFORE
    // update_e was applied. Since update_e leaves the outermost
    // tangential E cells alone (the loops skip i=0/i=nx, j=0/j=ny etc.),
    // those cells still hold E^n, which is what we need.

    // -x face (i = 0):   Ey at (0, j, k), Ez at (0, j, k)
    {
        auto& f = mur_xlo_;
        // Ey at i=0
        for (int k = 0; k <= nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                const double ey_inner_new = ey_.at(1, j, k);
                const double ey_b_old     = tap(f.prev_t0_a, j, k, f.na_a);
                const double ey_inner_old = tap(f.prev_t1_a, j, k, f.na_a);
                const double ey_b_new =
                    ey_inner_old + kx * (ey_inner_new - ey_b_old);
                ey_.at(0, j, k) = ey_b_new;
                tap(f.prev_t0_a, j, k, f.na_a) = ey_b_new;
                tap(f.prev_t1_a, j, k, f.na_a) = ey_inner_new;
            }
        }
        // Ez at i=0
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j <= ny; ++j) {
                const double ez_inner_new = ez_.at(1, j, k);
                const double ez_b_old     = tap(f.prev_t0_b, j, k, f.na_b);
                const double ez_inner_old = tap(f.prev_t1_b, j, k, f.na_b);
                const double ez_b_new =
                    ez_inner_old + kx * (ez_inner_new - ez_b_old);
                ez_.at(0, j, k) = ez_b_new;
                tap(f.prev_t0_b, j, k, f.na_b) = ez_b_new;
                tap(f.prev_t1_b, j, k, f.na_b) = ez_inner_new;
            }
        }
    }
    // +x face (i = nx): Ey at (nx, j, k), Ez at (nx, j, k)
    {
        auto& f = mur_xhi_;
        for (int k = 0; k <= nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                const double ey_inner_new = ey_.at(nx - 1, j, k);
                const double ey_b_old     = tap(f.prev_t0_a, j, k, f.na_a);
                const double ey_inner_old = tap(f.prev_t1_a, j, k, f.na_a);
                const double ey_b_new =
                    ey_inner_old + kx * (ey_inner_new - ey_b_old);
                ey_.at(nx, j, k) = ey_b_new;
                tap(f.prev_t0_a, j, k, f.na_a) = ey_b_new;
                tap(f.prev_t1_a, j, k, f.na_a) = ey_inner_new;
            }
        }
        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j <= ny; ++j) {
                const double ez_inner_new = ez_.at(nx - 1, j, k);
                const double ez_b_old     = tap(f.prev_t0_b, j, k, f.na_b);
                const double ez_inner_old = tap(f.prev_t1_b, j, k, f.na_b);
                const double ez_b_new =
                    ez_inner_old + kx * (ez_inner_new - ez_b_old);
                ez_.at(nx, j, k) = ez_b_new;
                tap(f.prev_t0_b, j, k, f.na_b) = ez_b_new;
                tap(f.prev_t1_b, j, k, f.na_b) = ez_inner_new;
            }
        }
    }
    // -y face (j = 0):  Ex at (i, 0, k), Ez at (i, 0, k)
    {
        auto& f = mur_ylo_;
        for (int k = 0; k <= nz; ++k) {
            for (int i = 0; i < nx; ++i) {
                const double v_new = ex_.at(i, 1, k);
                const double b_old = tap(f.prev_t0_a, i, k, f.na_a);
                const double i_old = tap(f.prev_t1_a, i, k, f.na_a);
                const double b_new = i_old + ky * (v_new - b_old);
                ex_.at(i, 0, k) = b_new;
                tap(f.prev_t0_a, i, k, f.na_a) = b_new;
                tap(f.prev_t1_a, i, k, f.na_a) = v_new;
            }
        }
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i <= nx; ++i) {
                const double v_new = ez_.at(i, 1, k);
                const double b_old = tap(f.prev_t0_b, i, k, f.na_b);
                const double i_old = tap(f.prev_t1_b, i, k, f.na_b);
                const double b_new = i_old + ky * (v_new - b_old);
                ez_.at(i, 0, k) = b_new;
                tap(f.prev_t0_b, i, k, f.na_b) = b_new;
                tap(f.prev_t1_b, i, k, f.na_b) = v_new;
            }
        }
    }
    // +y face (j = ny):
    {
        auto& f = mur_yhi_;
        for (int k = 0; k <= nz; ++k) {
            for (int i = 0; i < nx; ++i) {
                const double v_new = ex_.at(i, ny - 1, k);
                const double b_old = tap(f.prev_t0_a, i, k, f.na_a);
                const double i_old = tap(f.prev_t1_a, i, k, f.na_a);
                const double b_new = i_old + ky * (v_new - b_old);
                ex_.at(i, ny, k) = b_new;
                tap(f.prev_t0_a, i, k, f.na_a) = b_new;
                tap(f.prev_t1_a, i, k, f.na_a) = v_new;
            }
        }
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i <= nx; ++i) {
                const double v_new = ez_.at(i, ny - 1, k);
                const double b_old = tap(f.prev_t0_b, i, k, f.na_b);
                const double i_old = tap(f.prev_t1_b, i, k, f.na_b);
                const double b_new = i_old + ky * (v_new - b_old);
                ez_.at(i, ny, k) = b_new;
                tap(f.prev_t0_b, i, k, f.na_b) = b_new;
                tap(f.prev_t1_b, i, k, f.na_b) = v_new;
            }
        }
    }
    // -z face (k = 0):  Ex at (i, j, 0), Ey at (i, j, 0)
    {
        auto& f = mur_zlo_;
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const double v_new = ex_.at(i, j, 1);
                const double b_old = tap(f.prev_t0_a, i, j, f.na_a);
                const double i_old = tap(f.prev_t1_a, i, j, f.na_a);
                const double b_new = i_old + kz * (v_new - b_old);
                ex_.at(i, j, 0) = b_new;
                tap(f.prev_t0_a, i, j, f.na_a) = b_new;
                tap(f.prev_t1_a, i, j, f.na_a) = v_new;
            }
        }
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                const double v_new = ey_.at(i, j, 1);
                const double b_old = tap(f.prev_t0_b, i, j, f.na_b);
                const double i_old = tap(f.prev_t1_b, i, j, f.na_b);
                const double b_new = i_old + kz * (v_new - b_old);
                ey_.at(i, j, 0) = b_new;
                tap(f.prev_t0_b, i, j, f.na_b) = b_new;
                tap(f.prev_t1_b, i, j, f.na_b) = v_new;
            }
        }
    }
    // +z face (k = nz):
    {
        auto& f = mur_zhi_;
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const double v_new = ex_.at(i, j, nz - 1);
                const double b_old = tap(f.prev_t0_a, i, j, f.na_a);
                const double i_old = tap(f.prev_t1_a, i, j, f.na_a);
                const double b_new = i_old + kz * (v_new - b_old);
                ex_.at(i, j, nz) = b_new;
                tap(f.prev_t0_a, i, j, f.na_a) = b_new;
                tap(f.prev_t1_a, i, j, f.na_a) = v_new;
            }
        }
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                const double v_new = ey_.at(i, j, nz - 1);
                const double b_old = tap(f.prev_t0_b, i, j, f.na_b);
                const double i_old = tap(f.prev_t1_b, i, j, f.na_b);
                const double b_new = i_old + kz * (v_new - b_old);
                ey_.at(i, j, nz) = b_new;
                tap(f.prev_t0_b, i, j, f.na_b) = b_new;
                tap(f.prev_t1_b, i, j, f.na_b) = v_new;
            }
        }
    }
}

void FDTD3D::step() {
    if (dt_ <= 0) {
        throw std::runtime_error(
            "FDTD3D::step: dt not set (call set_dt_from_cfl first)");
    }
    if (boundary_ == Boundary::Mur1stOrder && !mur_buffers_ready_) {
        allocate_mur_buffers();
    }
    update_h();
    update_e();
    apply_mur_abc();
    apply_sources();
    ++n_steps_;
}

}  // namespace sikit::fdtd
