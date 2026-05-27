// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/TouchstoneCsv.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <format>
#include <fstream>
#include <numbers>
#include <sstream>

namespace sikit::touchstone {

namespace {

double mag_db(std::complex<double> z) {
    const double m = std::abs(z);
    if (m <= 0.0) return -std::numeric_limits<double>::infinity();
    return 20.0 * std::log10(m);
}

double phase_deg(std::complex<double> z) {
    return std::atan2(z.imag(), z.real()) * 180.0 / std::numbers::pi;
}

}  // namespace

std::string TouchstoneCsv::to_string(const TouchstoneFile& f) {
    if (f.num_ports < 2) {
        throw TouchstoneParseError("csv: need at least a 2-port Touchstone");
    }
    if (f.frequencies.size() != f.s_matrices.size()) {
        throw TouchstoneParseError(
            "csv: frequencies / s_matrices length mismatch");
    }
    const std::size_t expected = static_cast<std::size_t>(f.num_ports) *
                                  static_cast<std::size_t>(f.num_ports);
    for (const auto& m : f.s_matrices) {
        if (m.size() != expected) {
            throw TouchstoneParseError(
                "csv: s_matrices entry has wrong port-count layout");
        }
    }

    std::ostringstream os;
    os << "freq_hz,s11_mag_db,s11_phase_deg,s21_mag_db,s21_phase_deg,"
       << "z_in_real_ohm,z_in_imag_ohm\n";

    const double Zr = f.reference_impedance;
    for (std::size_t k = 0; k < f.frequencies.size(); ++k) {
        // Column-major: [S11, S21, S12, S22] for the 2-port case; for
        // N != 2 we still pull S11 at index 0 and S21 at index 1
        // (column 0 of the column-major matrix). Higher-order ports
        // are ignored in this CSV view — they round-trip through the
        // Touchstone reader/writer just fine; we just don't dump them.
        const auto S11 = f.s_matrices[k][0];
        const auto S21 = f.s_matrices[k][1];

        const std::complex<double> one(1.0, 0.0);
        const auto z_in = Zr * (one + S11) / (one - S11);

        os << std::format("{:.6e},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f}\n",
                          f.frequencies[k],
                          mag_db(S11), phase_deg(S11),
                          mag_db(S21), phase_deg(S21),
                          z_in.real(), z_in.imag());
    }
    return os.str();
}

void TouchstoneCsv::write_file(const TouchstoneFile& f,
                                const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        throw TouchstoneParseError(
            std::format("csv: cannot open file: {}", path.string()));
    }
    out << to_string(f);
}

}  // namespace sikit::touchstone
