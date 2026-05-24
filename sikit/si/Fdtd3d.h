// Full-wave 3D FDTD solver.
//
// What this is
//
//   The "moat" full-wave solver for sikit. Implements a Yee-grid
//   Maxwell update with selectable outer-boundary behaviour (PEC or
//   1st-order Mur ABC) and a hard E-field source. No PML, no
//   dielectric, no PCB integration yet -- that's the rest of the
//   3.15 epic.
//
// Why FDTD and not FEM / MoM
//
//   PCB structures have lots of conductor surface AND lots of
//   substrate volume. Surface integral methods (MoM) need to mesh
//   only conductors, but the dielectric handling becomes painful.
//   FEM needs a body-fitted tetrahedral mesh, which means a real
//   mesher up-front. FDTD takes a uniform Cartesian Yee grid and
//   walks the curl equations directly: cheap to implement, cheap to
//   debug, broadband response in one shot via Gaussian excitation.
//   The trade-off is staircase error on curved geometry, which PCB
//   structures (rectilinear traces, axis-aligned vias) suffer from
//   less than e.g. a chassis with curved seams.
//
//   References: Taflove & Hagness, "Computational Electrodynamics",
//   chs. 3-4 (Yee grid, CFL stability), ch. 7 (Mur ABC), and
//   Sullivan, "Electromagnetic Simulation Using the FDTD Method",
//   ch. 3 (the 3D update written out cell-by-cell).
//
// Grid layout (Yee staggered)
//
//   For an (Nx, Ny, Nz) cell volume:
//     Ex sits at cell centers in y,z but on x-faces        (Nx,   Ny+1, Nz+1)
//     Ey sits at cell centers in x,z but on y-faces        (Nx+1, Ny,   Nz+1)
//     Ez sits at cell centers in x,y but on z-faces        (Nx+1, Ny+1, Nz  )
//     Hx sits at edges parallel to x                       (Nx+1, Ny,   Nz  )
//     Hy sits at edges parallel to y                       (Nx,   Ny+1, Nz  )
//     Hz sits at edges parallel to z                       (Nx,   Ny,   Nz+1)
//   H is updated at half-integer time steps, E at integer steps. The
//   leapfrog scheme is second-order accurate in space and time.

#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace sikit::fdtd {

// Free-space constants in SI units. Module-local so callers don't
// have to dig through a units header.
inline constexpr double kEps0 = 8.8541878128e-12;
inline constexpr double kMu0  = 1.25663706212e-6;
inline constexpr double kC0   = 2.99792458e8;

struct GridSpec {
    int nx = 0;
    int ny = 0;
    int nz = 0;
    double dx = 0.0;  // metres
    double dy = 0.0;
    double dz = 0.0;
};

// CFL-stable time step for a uniform-mesh free-space FDTD solver.
//   dt <= 1 / (c * sqrt(1/dx^2 + 1/dy^2 + 1/dz^2))
// Caller usually multiplies by a safety factor (0.99 is conventional).
double cfl_dt(const GridSpec& g, double safety = 0.99);

// 3D scalar field stored in (x-stride, y-stride, z-stride) order with
// x varying fastest. Sizes are explicit so the Yee staggering doesn't
// get fudged at the boundaries.
class Field3D {
public:
    Field3D() = default;
    Field3D(int nx, int ny, int nz) { resize(nx, ny, nz); }

    void resize(int nx, int ny, int nz) {
        if (nx < 0 || ny < 0 || nz < 0) {
            throw std::invalid_argument("Field3D: negative extent");
        }
        nx_ = nx; ny_ = ny; nz_ = nz;
        data_.assign(static_cast<std::size_t>(nx) * ny * nz, 0.0);
    }

    int nx() const { return nx_; }
    int ny() const { return ny_; }
    int nz() const { return nz_; }
    std::size_t size() const { return data_.size(); }

    double& at(int i, int j, int k) {
        return data_[idx(i, j, k)];
    }
    double at(int i, int j, int k) const {
        return data_[idx(i, j, k)];
    }

    void fill(double v) { std::fill(data_.begin(), data_.end(), v); }

private:
    std::size_t idx(int i, int j, int k) const {
        return static_cast<std::size_t>(i)
             + static_cast<std::size_t>(j) * nx_
             + static_cast<std::size_t>(k) * nx_ * ny_;
    }
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<double> data_;
};

// Outer-boundary behaviour for the FDTD3D solver.
//
//   PEC          - tangential E pinned to zero at every outer face.
//                  Waves bounce; useful for closed cavities.
//   Mur1stOrder  - one-way wave equation absorber. ~95% absorption at
//                  normal incidence, falling off at grazing. The
//                  pragmatic default for axial port-fed SI runs. CPML
//                  upgrade lives in a follow-up.
enum class Boundary {
    PEC,
    Mur1stOrder,
};

// Lossless free-space FDTD3D solver.
//
// Usage:
//   FDTD3D s(GridSpec{nx, ny, nz, dx, dy, dz});
//   s.set_dt_from_cfl();
//   s.set_boundary(Boundary::Mur1stOrder);  // optional
//   s.add_hard_e_source(...);
//   for (int n = 0; n < N; ++n) {
//       s.step();
//       double v = s.ez(probe_i, probe_j, probe_k);
//   }
class FDTD3D {
public:
    explicit FDTD3D(const GridSpec& g);

    const GridSpec& grid() const { return g_; }
    int    step_count() const { return n_steps_; }
    double dt() const { return dt_; }
    double sim_time() const { return n_steps_ * dt_; }

    void set_dt_from_cfl(double safety = 0.99);
    void set_dt(double dt);

    // Default boundary is PEC. Call set_boundary(Boundary::Mur1stOrder)
    // before the first step() to switch to absorbing.
    void set_boundary(Boundary b);
    Boundary boundary() const { return boundary_; }

    struct HardESource {
        int i, j, k;
        enum class Comp { Ex, Ey, Ez } comp = Comp::Ez;
        std::vector<double> samples;  // pre-computed waveform
    };
    void add_hard_e_source(HardESource src);

    void step();

    double ex(int i, int j, int k) const { return ex_.at(i, j, k); }
    double ey(int i, int j, int k) const { return ey_.at(i, j, k); }
    double ez(int i, int j, int k) const { return ez_.at(i, j, k); }
    double hx(int i, int j, int k) const { return hx_.at(i, j, k); }
    double hy(int i, int j, int k) const { return hy_.at(i, j, k); }
    double hz(int i, int j, int k) const { return hz_.at(i, j, k); }

    std::size_t bytes() const;

private:
    GridSpec g_;
    double dt_ = 0.0;
    int n_steps_ = 0;
    Boundary boundary_ = Boundary::PEC;
    Field3D ex_, ey_, ez_;
    Field3D hx_, hy_, hz_;
    std::vector<HardESource> sources_;

    // Mur ABC: store E at the boundary cell + the cell one step in,
    // both at the *previous* time step. Each face needs the two
    // tangential components.
    //
    // The data layout: a small 2D slab per face per tangential
    // component, indexed by the two coordinates along the face.
    struct MurFace {
        // For each face we store two tangential E components at the
        // boundary slab (t0) and the slab one cell inward (t1), both
        // captured at the previous time step.
        // The two tangential components have *different* sizes on the
        // Yee grid, so each gets its own na/nb stride pair.
        //   x-face: comp A = Ey (na_a=ny, nb_a=nz+1)
        //           comp B = Ez (na_b=ny+1, nb_b=nz)
        //   y-face: comp A = Ex (na_a=nx, nb_a=nz+1)
        //           comp B = Ez (na_b=nx+1, nb_b=nz)
        //   z-face: comp A = Ex (na_a=nx, nb_a=ny+1)
        //           comp B = Ey (na_b=nx+1, nb_b=ny)
        std::vector<double> prev_t0_a, prev_t1_a;
        std::vector<double> prev_t0_b, prev_t1_b;
        int na_a = 0, nb_a = 0;
        int na_b = 0, nb_b = 0;
    };
    MurFace mur_xlo_, mur_xhi_, mur_ylo_, mur_yhi_, mur_zlo_, mur_zhi_;
    bool mur_buffers_ready_ = false;

    void update_h();
    void update_e();
    void apply_sources();
    void allocate_mur_buffers();
    void apply_mur_abc();
};

// Helper: Gaussian pulse  exp(-((t - t0) / spread)^2) at time t.
double gaussian_pulse(double t, double t0, double spread);

// Analytic resonance frequency of a rectangular PEC cavity of inner
// dimensions (a, b, d) in metres. (m, n, p) are the mode indices.
//   TE_mnp / TM_mnp: f = c/2 * sqrt((m/a)^2 + (n/b)^2 + (p/d)^2)
double cavity_mode_freq(double a, double b, double d,
                          int m, int n, int p);

}  // namespace sikit::fdtd
