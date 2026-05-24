#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "si/Fdtd3d.h"

using namespace sikit::fdtd;
using Catch::Approx;

TEST_CASE("fdtd3d: CFL dt formula matches the closed-form bound",
          "[fdtd3d]") {
    GridSpec g{10, 10, 10, 1e-3, 1e-3, 1e-3};
    const double inv2 = 1.0 / (g.dx * g.dx) + 1.0 / (g.dy * g.dy)
                      + 1.0 / (g.dz * g.dz);
    const double expected = 0.99 / (kC0 * std::sqrt(inv2));
    REQUIRE(cfl_dt(g) == Approx(expected).epsilon(1e-12));
    // Anisotropic grid: the shortest dimension dominates inv2, but
    // larger cells in the other axes loosen the bound. dt_aniso must
    // be >= the dt for the uniform-shortest-cell grid (which is the
    // most restrictive isotropic case).
    GridSpec aniso{10, 10, 10, 1e-3, 2e-3, 5e-3};
    const double dt_aniso = cfl_dt(aniso);
    const double dt_iso_smallest = 0.99 / (kC0 * std::sqrt(3.0) / 1e-3);
    REQUIRE(dt_aniso >= dt_iso_smallest);
}

TEST_CASE("fdtd3d: Field3D round-trips reads and writes", "[fdtd3d]") {
    Field3D f(4, 3, 2);
    REQUIRE(f.size() == 24u);
    f.at(0, 0, 0) = 1.0;
    f.at(3, 2, 1) = 2.0;
    f.at(2, 1, 0) = 3.0;
    REQUIRE(f.at(0, 0, 0) == 1.0);
    REQUIRE(f.at(3, 2, 1) == 2.0);
    REQUIRE(f.at(2, 1, 0) == 3.0);
    f.fill(0.0);
    REQUIRE(f.at(2, 1, 0) == 0.0);
}

TEST_CASE("fdtd3d: gaussian pulse peaks at t0", "[fdtd3d]") {
    const double t0 = 50e-12;
    const double s  = 10e-12;
    REQUIRE(gaussian_pulse(t0, t0, s) == Approx(1.0));
    REQUIRE(gaussian_pulse(t0 + s, t0, s) ==
            Approx(std::exp(-1.0)).epsilon(1e-12));
    REQUIRE(gaussian_pulse(0.0, t0, s) < 1e-10);  // pulse is buried
}

TEST_CASE("fdtd3d: cavity_mode_freq matches Pozar formula",
          "[fdtd3d]") {
    // Rectangular cavity from Pozar Ex. 6.1: 5cm x 4cm x 3cm.
    // TE_101 = c/2 * sqrt(1/a^2 + 1/d^2)
    const double f = cavity_mode_freq(0.05, 0.04, 0.03, 1, 0, 1);
    const double expected = 0.5 * kC0 * std::sqrt(1.0 / (0.05 * 0.05)
                                                       + 1.0 / (0.03 * 0.03));
    REQUIRE(f == Approx(expected).epsilon(1e-12));
}

TEST_CASE("fdtd3d: solver constructs and reports memory footprint",
          "[fdtd3d]") {
    FDTD3D s(GridSpec{8, 8, 8, 1e-3, 1e-3, 1e-3});
    REQUIRE(s.grid().nx == 8);
    REQUIRE(s.step_count() == 0);
    // 6 Yee arrays of roughly 9*9*8 doubles each; check it's the right
    // order of magnitude rather than an exact value.
    REQUIRE(s.bytes() > 6u * 8u * 8u * 8u * sizeof(double));
    REQUIRE(s.bytes() < 6u * 16u * 16u * 16u * sizeof(double));
}

TEST_CASE("fdtd3d: step without dt set throws", "[fdtd3d]") {
    FDTD3D s(GridSpec{4, 4, 4, 1e-3, 1e-3, 1e-3});
    REQUIRE_THROWS(s.step());
}

TEST_CASE("fdtd3d: PEC walls keep tangential E pinned at zero",
          "[fdtd3d]") {
    // Hard Ez source in the middle; let it run for a few steps and
    // verify the outer i=0 / j=0 / i=nx / j=ny columns of Ez stay 0.
    GridSpec g{8, 8, 8, 1e-3, 1e-3, 1e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    FDTD3D::HardESource src;
    src.i = 4; src.j = 4; src.k = 4;
    src.comp = FDTD3D::HardESource::Comp::Ez;
    src.samples.assign(200, 0.0);
    for (int n = 0; n < 200; ++n) {
        src.samples[n] = gaussian_pulse(n * cfl_dt(g),
                                            40 * cfl_dt(g),
                                            10 * cfl_dt(g));
    }
    s.add_hard_e_source(src);
    for (int n = 0; n < 100; ++n) s.step();
    // Spot-check the four PEC edges of one z-slice for Ez.
    for (int j = 0; j <= g.ny; ++j) {
        REQUIRE(s.ez(0,    j, 4) == 0.0);
        REQUIRE(s.ez(g.nx, j, 4) == 0.0);
    }
    for (int i = 0; i <= g.nx; ++i) {
        REQUIRE(s.ez(i, 0,    4) == 0.0);
        REQUIRE(s.ez(i, g.ny, 4) == 0.0);
    }
}

TEST_CASE("fdtd3d: causal propagation -- probe is quiet before wave-front "
          "arrives, lit up after",
          "[fdtd3d]") {
    // Drive Ez at the center of a 24-cell-cube grid. A leapfrog FDTD
    // update propagates a wave-front at no more than c per second
    // (with mild grid dispersion that's never faster than c). Probe
    // 10 cells away in +x. Strict causality says: before
    // (10*dx)/c seconds of simulation time elapses, the probe is
    // exactly zero. Well after that, the probe has seen the wave and
    // is non-zero. This is a tighter and more meaningful test of the
    // curl update than a brittle peak-arrival-time check, which is
    // muddied by Yee dispersion + near-field components of a point
    // source.
    const int nx = 24, ny = 24, nz = 24;
    const double dx = 1e-3;
    GridSpec g{nx, ny, nz, dx, dx, dx};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const double dt = s.dt();

    // Step source: zero for the first 5 steps so the "before" window
    // is genuinely silent, then a fixed 1.0 from step 5 onward.
    const int N_steps = 200;
    FDTD3D::HardESource src;
    src.i = 12; src.j = 12; src.k = 12;
    src.comp = FDTD3D::HardESource::Comp::Ez;
    src.samples.assign(N_steps, 0.0);
    for (int n = 5; n < N_steps; ++n) src.samples[n] = 1.0;
    s.add_hard_e_source(src);

    const int probe_i = src.i + 10;
    const int probe_j = src.j;
    const int probe_k = src.k;

    // Number of steps before the wave-front *can* reach the probe.
    // 10 cells at c => tof_steps = ceil(10*dx / (c*dt)). With safety
    // factor 0.99 and isotropic dx, dt = 0.99/(c*sqrt(3)/dx); the
    // wave-front in the axial direction moves c*dt/dx = 0.99/sqrt(3)
    // cells per step ≈ 0.57. So 10 cells take ~17.5 steps minimum.
    const double v_max_cells_per_step = kC0 * dt / dx;
    const int causal_steps =
        static_cast<int>(10.0 / v_max_cells_per_step) - 1;
    REQUIRE(causal_steps > 0);

    std::vector<double> probe(N_steps, 0.0);
    for (int n = 0; n < N_steps; ++n) {
        s.step();
        probe[n] = s.ez(probe_i, probe_j, probe_k);
    }

    // Before the wave can have arrived (n < causal_steps - 5 for some
    // slack on source-start), probe must be machine-zero. The source
    // only injects Ez at the center cell, so the probe stays exactly
    // 0.0 until information from a chain of curl updates reaches it.
    for (int n = 0; n < causal_steps - 5; ++n) {
        REQUIRE(probe[n] == 0.0);
    }

    // Well after the wave can have arrived, the probe is reading a
    // nonzero field. Use the final sample.
    REQUIRE(std::abs(probe.back()) > 1e-9);
}
