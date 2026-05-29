// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/SpiceExport.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sikit::si {

namespace {

// Emit a SPICE-friendly real number: scientific with a fixed mantissa
// width so columns line up nicely in the generated netlist. SPICE-3
// accepts standard C floating-point literals; we deliberately avoid
// engineering suffixes (1k, 1u, etc.) because parsers handle plain
// scientific more uniformly.
std::string sp_num(double v) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(6) << v;
    return os.str();
}

bool is_valid_identifier(const std::string& s) {
    if (s.empty()) return false;
    auto is_alpha = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    auto is_alnum_us = [&](char c) {
        return is_alpha(c) || (c >= '0' && c <= '9');
    };
    if (!is_alpha(s[0])) return false;
    for (char c : s) if (!is_alnum_us(c)) return false;
    return true;
}

}  // namespace

std::string spice_subckt_from_fit(
    const VectorFitResult& fit, const SpiceExportOptions& opts) {
    if (!is_valid_identifier(opts.subckt_name)) {
        throw std::runtime_error(
            "SpiceExport: subckt_name must be a SPICE identifier "
            "(letters, digits, underscore; no leading digit)");
    }
    // residues are indexed by the pole loop below; a caller-built fit with
    // fewer residues than poles would read out of bounds.
    if (fit.residues.size() < fit.poles.size()) {
        throw std::runtime_error(
            "SpiceExport: fit has fewer residues than poles");
    }
    std::ostringstream os;

    if (opts.include_header) {
        os << "* " << opts.subckt_name << " -- rational fit emitted by sikit\n";
        os << "*\n";
        os << "* Vector-fit summary:\n";
        os << "*   poles      = " << fit.poles.size() << "\n";
        os << "*   d (const)  = " << sp_num(fit.d) << "\n";
        os << "*   iterations = " << fit.iterations << "\n";
        os << "*   converged  = " << (fit.converged ? "yes" : "no") << "\n";
        os << "*   rms error  = " << sp_num(fit.rms_error) << "\n";
        os << "*\n";
        os << "* Foster-canonical RC ladder. Each parallel branch realises\n";
        os << "* one r_n / (s - p_n) term; the summing E source recombines\n";
        os << "* them with the direct term d. Universal SPICE-3 syntax.\n";
        os << "*\n";
    }

    os << ".subckt " << opts.subckt_name << " in out\n";

    // One RC branch per pole. Node naming: nN_<idx> for pole idx.
    // Capacitor value: C_n = -1/p_n (positive when p_n < 0).
    // Resistor value: 1 ohm (sets the time constant Rn*Cn = -1/p_n).
    for (std::size_t n = 0; n < fit.poles.size(); ++n) {
        const double p = fit.poles[n];
        const double C = (p < 0.0) ? -1.0 / p : 1.0 / std::abs(p);
        os << "R" << n << " in  n" << n << " 1\n";
        os << "C" << n << " n" << n << " 0 " << sp_num(C) << "\n";
    }

    // Summing controlled source: Vout = sum_n r_n * V(n_n) + d * V(in).
    // Use a B-source (behavioural) with a sum expression; this syntax
    // is supported by both ngspice and LTspice. For HSPICE compatibility
    // an equivalent E-source POLY form would work too, but B-sources
    // are cleaner.
    os << "B_out out 0 V=";
    bool first = true;
    if (std::abs(fit.d) > 0.0) {
        os << sp_num(fit.d) << "*V(in)";
        first = false;
    }
    for (std::size_t n = 0; n < fit.poles.size(); ++n) {
        if (!first) os << " + ";
        os << sp_num(fit.residues[n]) << "*V(n" << n << ")";
        first = false;
    }
    if (first) {
        // No poles + no d -> emit a zero source so the netlist parses.
        os << "0";
    }
    os << "\n";

    os << ".ends " << opts.subckt_name << "\n";
    return os.str();
}

std::string spice_subckt_from_touchstone(
    const sikit::touchstone::TouchstoneFile& channel,
    const SpiceExportOptions& opts) {
    if (channel.frequencies.empty()) {
        throw std::runtime_error(
            "SpiceExport: Touchstone has no frequency samples");
    }
    if (channel.s_matrices.empty()) {
        throw std::runtime_error(
            "SpiceExport: Touchstone has no S-parameter matrices");
    }
    const int N = channel.num_ports;
    const int idx = opts.s_param_index;
    const int max_idx = N * N;
    if (idx < 0 || idx >= max_idx) {
        throw std::runtime_error(
            "SpiceExport: s_param_index out of range for this Touchstone");
    }
    // Pull the requested complex response across the freq grid.
    std::vector<std::complex<double>> response;
    response.reserve(channel.frequencies.size());
    for (const auto& m : channel.s_matrices) {
        if (static_cast<int>(m.size()) <= idx) {
            throw std::runtime_error(
                "SpiceExport: S-matrix smaller than requested index");
        }
        response.push_back(m[idx]);
    }
    VectorFitResult fit = vector_fit(channel.frequencies, response, opts.fit);
    return spice_subckt_from_fit(fit, opts);
}

bool write_spice_subckt(
    const sikit::touchstone::TouchstoneFile& channel,
    const std::filesystem::path& path,
    const SpiceExportOptions& opts) {
    std::ofstream os(path, std::ios::trunc);
    if (!os) return false;
    os << spice_subckt_from_touchstone(channel, opts);
    return static_cast<bool>(os);
}

}  // namespace sikit::si
