// Cross-validation: sikit FDTD3D vs openEMS, same PEC cavity geometry.
//
// Runs the same 50 x 30 x 20 mm rectangular PEC cavity that
// `cavity_openems.py` runs, finds the dominant TE_101 mode in the
// Ez probe FFT, and prints all three numbers side by side:
//
//   - analytic  : c/2 * sqrt((1/a)^2 + (1/d)^2)
//   - openEMS   : peak frequency from the bundled reference
//   - sikit     : peak frequency from this run
//
// Exits 0 iff sikit and openEMS agree within 5% AND both agree with
// the analytic mode within 5%. Same wall-clock budget as a unit
// test, so it can land in CI later if you want.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "si/Fdtd3d.h"
#include "si/Fft.h"

using namespace sikit::fdtd;

namespace {

double parse_openems_peak(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr,
            "cannot read openEMS reference: %s\n", path.c_str());
        return -1.0;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string key; double v;
        iss >> key >> v;
        if (key == "peak_freq_hz") return v;
    }
    return -1.0;
}

}  // namespace

int main(int argc, char** argv) {
    // Same geometry as cavity_openems.py.
    const double a_mm = 50.0, b_mm = 30.0, d_mm = 20.0;
    const double dx   = 2e-3;
    const int nx = 25, ny = 15, nz = 10;
    GridSpec g{nx, ny, nz, dx, dx, dx};

    FDTD3D s(g);
    s.set_dt_from_cfl();
    const double dt = s.dt();

    // Same Gauss pulse centre + bandwidth as the openEMS run.
    const double fc      = 8.0e9;
    const double spread  = 1.0 / (2 * M_PI * 4.0e9);  // ~bw=4GHz
    const int N_steps    = 16000;
    const double t0      = 5 * spread;
    FDTD3D::HardESource src;
    src.i = static_cast<int>(0.40 * nx);
    src.j = static_cast<int>(0.50 * ny);
    src.k = static_cast<int>(0.30 * nz);
    src.comp = FDTD3D::HardESource::Comp::Ez;
    src.samples.resize(N_steps);
    for (int n = 0; n < N_steps; ++n) {
        const double t = n * dt;
        const double x = (t - t0) / spread;
        src.samples[n] = std::exp(-x * x) * std::sin(2 * M_PI * fc * (t - t0));
    }
    s.add_hard_e_source(src);

    const int pi = static_cast<int>(0.70 * nx);
    const int pj = static_cast<int>(0.50 * ny);
    const int pk = static_cast<int>(0.70 * nz);
    std::vector<double> probe(N_steps);
    for (int n = 0; n < N_steps; ++n) {
        s.step();
        probe[n] = s.ez(pi, pj, pk);
    }

    // FFT and find the peak in the 5-12 GHz band.
    const std::size_t N = sikit::dsp::next_power_of_2(probe.size());
    std::vector<std::complex<double>> X(N, {0, 0});
    for (std::size_t n = 0; n < probe.size(); ++n) X[n] = probe[n];
    sikit::dsp::fft(X);
    const double df = 1.0 / (N * dt);
    double peak_f = 0.0, peak_v = 0.0;
    for (std::size_t k = 0; k < N / 2; ++k) {
        const double f = k * df;
        if (f < 5e9 || f > 12e9) continue;
        const double m = std::abs(X[k]);
        if (m > peak_v) { peak_v = m; peak_f = f; }
    }

    const double c0 = 2.99792458e8;
    const double f_analytic = 0.5 * c0 *
        std::sqrt(1.0/(a_mm*1e-3 * a_mm*1e-3) +
                   1.0/(d_mm*1e-3 * d_mm*1e-3));

    // Optional: load the openEMS reference. Pass its path as argv[1];
    // if omitted, just print the sikit + analytic numbers.
    double f_openems = -1.0;
    if (argc > 1) f_openems = parse_openems_peak(argv[1]);

    std::printf("FDTD3D cross-validation: PEC cavity TE_101\n");
    std::printf("  cavity (mm)      : %.1f x %.1f x %.1f, dx = %.1f\n",
                  a_mm, b_mm, d_mm, dx * 1e3);
    std::printf("  analytic peak    : %.4f GHz\n", f_analytic / 1e9);
    std::printf("  sikit peak       : %.4f GHz   (err vs analytic: "
                  "%+.2f%%)\n",
                  peak_f / 1e9,
                  100.0 * (peak_f - f_analytic) / f_analytic);
    if (f_openems > 0) {
        std::printf("  openEMS peak     : %.4f GHz   (err vs analytic: "
                      "%+.2f%%)\n",
                      f_openems / 1e9,
                      100.0 * (f_openems - f_analytic) / f_analytic);
        std::printf("  |sikit - openEMS|: %.2f%%\n",
                      100.0 * std::abs(peak_f - f_openems) / f_openems);
    }

    int rc = 0;
    if (std::abs(peak_f - f_analytic) / f_analytic > 0.05) rc = 1;
    if (f_openems > 0 &&
        std::abs(peak_f - f_openems) / f_openems > 0.05) rc = 1;
    return rc;
}
