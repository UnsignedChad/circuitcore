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

TEST_CASE("fdtd3d: Mur ABC absorbs the wave -- much smaller residual "
          "energy than the PEC reflector",
          "[fdtd3d][mur]") {
    // Two identical runs side-by-side: one with PEC boundary, one with
    // Mur 1st-order. Drive an Ez pulse at the center, let it propagate
    // out, wait long enough for the PEC version to bounce. The Mur
    // box should hold far less stored energy at that late time --
    // most of the energy has left through the absorber.
    const int nx = 30, ny = 30, nz = 30;
    const double dx = 1e-3;
    const GridSpec g{nx, ny, nz, dx, dx, dx};
    const double dt = cfl_dt(g);

    // Wide-ish Gaussian pulse that fits comfortably below the grid's
    // dispersion limit. Pulse peaks at step 30, finishes by step ~60.
    const int N_steps    = 220;  // long enough for a PEC reflection
    const int src_step   = 30;
    const int src_spread = 8;

    auto build_src = [&](int /*not used*/) {
        FDTD3D::HardESource s;
        s.i = nx / 2; s.j = ny / 2; s.k = nz / 2;
        s.comp = FDTD3D::HardESource::Comp::Ez;
        s.samples.resize(N_steps);
        for (int n = 0; n < N_steps; ++n) {
            s.samples[n] = gaussian_pulse(n * dt, src_step * dt,
                                              src_spread * dt);
        }
        // Source goes silent after step ~60 -- past that point, any
        // energy in the box is a wave still bouncing around (PEC) or
        // residual ringdown (Mur).
        for (int n = 60; n < N_steps; ++n) s.samples[n] = 0.0;
        return s;
    };

    auto run = [&](Boundary b) {
        FDTD3D s(g);
        s.set_dt(dt);
        s.set_boundary(b);
        s.add_hard_e_source(build_src(0));
        for (int n = 0; n < N_steps; ++n) s.step();
        // Sum of squared Ez across the volume -- a proxy for stored
        // electric-field energy.
        double e2 = 0.0;
        for (int k = 0; k < nz; ++k) {
            for (int j = 1; j < ny; ++j) {
                for (int i = 1; i < nx; ++i) {
                    const double v = s.ez(i, j, k);
                    e2 += v * v;
                }
            }
        }
        return e2;
    };

    const double e_pec = run(Boundary::PEC);
    const double e_mur = run(Boundary::Mur1stOrder);

    // The Mur box should have shed most of the wave through the walls
    // by step 220; the PEC box still holds the reflected pulse.
    REQUIRE(e_pec > 0.0);
    REQUIRE(e_mur < 0.20 * e_pec);
}

TEST_CASE("fdtd3d: Mur ABC remains numerically stable for a long run",
          "[fdtd3d][mur]") {
    // Empty box, no source, run for many steps. Ez everywhere should
    // stay machine-zero. (Catches the classic 1st-order Mur late-time
    // instability bug where the boundary equation amplifies its own
    // round-off.)
    GridSpec g{12, 12, 12, 1e-3, 1e-3, 1e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    s.set_boundary(Boundary::Mur1stOrder);
    for (int n = 0; n < 500; ++n) s.step();
    double max_abs = 0.0;
    for (int k = 0; k < g.nz; ++k) {
        for (int j = 0; j <= g.ny; ++j) {
            for (int i = 0; i <= g.nx; ++i) {
                max_abs = std::max(max_abs, std::abs(s.ez(i, j, k)));
            }
        }
    }
    REQUIRE(max_abs == 0.0);
}

TEST_CASE("fdtd3d: switching boundary mode is observable",
          "[fdtd3d][mur]") {
    FDTD3D s(GridSpec{6, 6, 6, 1e-3, 1e-3, 1e-3});
    REQUIRE(s.boundary() == Boundary::PEC);
    s.set_boundary(Boundary::Mur1stOrder);
    REQUIRE(s.boundary() == Boundary::Mur1stOrder);
}

TEST_CASE("fdtd3d: default material is vacuum",
          "[fdtd3d][material]") {
    FDTD3D s(GridSpec{4, 4, 4, 1e-3, 1e-3, 1e-3});
    REQUIRE(s.epsr_x(0, 0, 0)  == 1.0);
    REQUIRE(s.epsr_y(2, 2, 2)  == 1.0);
    REQUIRE(s.epsr_z(1, 1, 1)  == 1.0);
    REQUIRE(s.sigma_x(0, 0, 0) == 0.0);
}

TEST_CASE("fdtd3d: set_uniform_material fills every E component",
          "[fdtd3d][material]") {
    FDTD3D s(GridSpec{4, 4, 4, 1e-3, 1e-3, 1e-3});
    s.set_uniform_material(4.2, 0.07);
    REQUIRE(s.epsr_x(0, 0, 0)  == 4.2);
    REQUIRE(s.epsr_y(3, 2, 2)  == 4.2);
    REQUIRE(s.epsr_z(2, 4, 3)  == 4.2);
    REQUIRE(s.sigma_x(1, 1, 1) == Approx(0.07));
}

TEST_CASE("fdtd3d: set_material_box scopes the change to inside the box",
          "[fdtd3d][material]") {
    FDTD3D s(GridSpec{8, 8, 8, 1e-3, 1e-3, 1e-3});
    s.set_material_box(2, 2, 2, 5, 5, 5, 9.8, 0.0);
    REQUIRE(s.epsr_z(0, 0, 0) == 1.0);   // outside the box
    REQUIRE(s.epsr_z(3, 3, 3) == 9.8);   // inside
    REQUIRE(s.epsr_z(7, 7, 7) == 1.0);   // outside
}

TEST_CASE("fdtd3d: causality is slower inside a dielectric",
          "[fdtd3d][material]") {
    // Two identical solves, one in vacuum and one filled with
    // eps_r = 4 throughout. In the eps_r=4 box, waves travel at
    // c / sqrt(4) = c/2. A probe 10 cells from the source therefore
    // stays at machine zero for roughly *twice* as many steps as in
    // the vacuum case. We don't measure the slowdown analytically;
    // we just verify the dielectric box is "still quiet" at a step
    // where vacuum would already have lit up.
    const int nx = 24, ny = 24, nz = 24;
    const double dx = 1e-3;
    const GridSpec g{nx, ny, nz, dx, dx, dx};

    auto run = [&](double eps_r) {
        FDTD3D s(g);
        s.set_dt_from_cfl();
        s.set_uniform_material(eps_r);
        // Step source: zero before step 5, +1.0 thereafter.
        FDTD3D::HardESource src;
        src.i = 12; src.j = 12; src.k = 12;
        src.comp = FDTD3D::HardESource::Comp::Ez;
        const int N_steps = 100;
        src.samples.assign(N_steps, 0.0);
        for (int n = 5; n < N_steps; ++n) src.samples[n] = 1.0;
        s.add_hard_e_source(src);

        const int probe_i = src.i + 10;
        std::vector<double> probe(N_steps, 0.0);
        for (int n = 0; n < N_steps; ++n) {
            s.step();
            probe[n] = s.ez(probe_i, src.j, src.k);
        }
        return probe;
    };

    const auto vac = run(1.0);
    const auto die = run(4.0);

    // At step ~25 in vacuum (well past 10 cells / 0.57 cells-per-step
    // = 17.5 steps) the probe is lit. The eps_r=4 box should still be
    // ~quiet at step 25 because the half-speed front hasn't arrived
    // (10 cells / 0.285 cells-per-step = 35 steps minimum).
    REQUIRE(std::abs(vac[25]) > 1e-9);
    REQUIRE(std::abs(die[25]) < std::abs(vac[25]) * 0.01);
}

TEST_CASE("fdtd3d: conductivity dissipates stored energy",
          "[fdtd3d][material]") {
    // Pulse-excite a PEC box, let the wave bounce; the lossless box
    // stores roughly all the injected energy after the source goes
    // silent, the lossy box has dissipated a meaningful fraction.
    // We compare the SUM of squared Ez across the volume, a proxy for
    // electric-field energy. The lossy run must be measurably less.
    //
    // Probing a single point ends up at the mercy of standing-wave
    // node positions, which shift with sigma -- a volumetric integral
    // is the right invariant.
    const int nx = 12, ny = 12, nz = 12;
    const GridSpec g{nx, ny, nz, 1e-3, 1e-3, 1e-3};
    const double dt = cfl_dt(g);

    auto run = [&](double sigma) {
        FDTD3D s(g);
        s.set_dt(dt);
        s.set_uniform_material(1.0, sigma);
        FDTD3D::HardESource src;
        src.i = nx / 2; src.j = ny / 2; src.k = nz / 2;
        src.comp = FDTD3D::HardESource::Comp::Ez;
        const int N_steps = 220;
        src.samples.resize(N_steps);
        for (int n = 0; n < N_steps; ++n) {
            src.samples[n] = gaussian_pulse(n * dt, 30 * dt, 8 * dt);
        }
        for (int n = 60; n < N_steps; ++n) src.samples[n] = 0.0;
        s.add_hard_e_source(src);
        for (int n = 0; n < N_steps; ++n) s.step();
        double e2 = 0.0;
        for (int k = 0; k < nz; ++k) {
            for (int j = 1; j < ny; ++j) {
                for (int i = 1; i < nx; ++i) {
                    const double v = s.ez(i, j, k);
                    e2 += v * v;
                }
            }
        }
        return e2;
    };

    const double e_lossless = run(0.0);
    const double e_lossy    = run(5.0);
    REQUIRE(e_lossless > 0.0);
    REQUIRE(e_lossy < 0.6 * e_lossless);
}
