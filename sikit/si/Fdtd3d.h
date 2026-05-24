// Full-wave 3D FDTD solver (scaffolding).
//
// What this is
//
//   The first slice of sikit's "moat" full-wave solver. Implements the
//   bare-minimum Yee-grid Maxwell update with a PEC outer boundary
//   and a hard E-field source. No PML, no dielectric, no lossy
//   materials, no PCB integration -- that's the rest of the 3.15
//   epic. This module just has to step Maxwell's equations correctly
//   so the validation tests below pass; everything else bolts on.
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
//   chs. 3-4 (Yee grid, CFL stability) and Sullivan, "Electromagnetic
//   Simulation Using the FDTD Method", ch. 3 (the 3D update written
//   out cell-by-cell).
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

// Lossless free-space FDTD3D solver with PEC outer boundary.
//
// Usage:
//   FDTD3D s(GridSpec{nx, ny, nz, dx, dy, dz});
//   s.set_dt_from_cfl();           // or s.dt(...) manually
//   s.add_hard_e_source(...);      // optional excitations
//   for (int n = 0; n < N; ++n) {
//       s.step();
//       double v = s.ez(probe_i, probe_j, probe_k);  // record probe
//   }
class FDTD3D {
public:
    explicit FDTD3D(const GridSpec& g);

    const GridSpec& grid() const { return g_; }
    int    step_count() const { return n_steps_; }
    double dt() const { return dt_; }
    double sim_time() const { return n_steps_ * dt_; }

    // Set the time step from the CFL limit (safety 0.99). The solver
    // refuses to step if dt is unset or non-positive.
    void set_dt_from_cfl(double safety = 0.99);
    void set_dt(double dt);

    // Hard E-field source: at every time step, overwrite Ez(i,j,k)
    // with f(t). Multiple sources can be added; the last one to
    // overwrite a given cell wins.
    //
    // (Hard sources reflect incident waves -- they're the simplest
    // injection model. PR B will add total-field / scattered-field
    // separation so we can do clean broadband port excitation.)
    struct HardESource {
        int i, j, k;
        enum class Comp { Ex, Ey, Ez } comp = Comp::Ez;
        // Caller supplies a sample value at simulation time t (seconds).
        std::vector<double> samples;  // pre-computed waveform over Nsteps
    };
    void add_hard_e_source(HardESource src);

    // Single time step: H half-step, then E full step, then re-apply
    // hard sources at the new time level.
    void step();

    // Field accessors. PEC boundary: tangential E is zero at the outer
    // walls of the box, so reading there is meaningful (it will be 0).
    double ex(int i, int j, int k) const { return ex_.at(i, j, k); }
    double ey(int i, int j, int k) const { return ey_.at(i, j, k); }
    double ez(int i, int j, int k) const { return ez_.at(i, j, k); }
    double hx(int i, int j, int k) const { return hx_.at(i, j, k); }
    double hy(int i, int j, int k) const { return hy_.at(i, j, k); }
    double hz(int i, int j, int k) const { return hz_.at(i, j, k); }

    // Memory footprint of the six Yee field arrays, in bytes. Useful
    // for the CLI "fdtd info" command (PR D) to flag oversized grids
    // before they OOM the box.
    std::size_t bytes() const;

private:
    GridSpec g_;
    double dt_ = 0.0;
    int n_steps_ = 0;
    Field3D ex_, ey_, ez_;
    Field3D hx_, hy_, hz_;
    std::vector<HardESource> sources_;

    void update_h();
    void update_e();
    void apply_sources();
};

// Helper: Gaussian pulse  exp(-((t - t0) / spread)^2) at time t.
// spread sets the half-width; t0 should be >= 3*spread so the pulse
// starts near zero at t=0.
double gaussian_pulse(double t, double t0, double spread);

// Analytic resonance frequency of a rectangular PEC cavity of inner
// dimensions (a, b, d) in metres. (m, n, p) are the mode indices.
// Used by the cavity test to verify the FDTD update converges to
// known eigenfrequencies.
//   TE_mnp / TM_mnp: f = c/2 * sqrt((m/a)^2 + (n/b)^2 + (p/d)^2)
double cavity_mode_freq(double a, double b, double d,
                          int m, int n, int p);

}  // namespace sikit::fdtd
