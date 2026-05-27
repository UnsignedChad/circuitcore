// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Touchstone S-parameter file reader and writer.
//
// Two format versions are supported:
//
//   v1 (.s1p / .s2p / .s4p / .s8p)
//       The original IBIS Open Forum Touchstone 1.0 spec. Header line:
//           # <freq_unit> <param_type> <format> R <ref_impedance>
//       Body is N freq + 2*N^2 floats per record. Port count comes
//       from the filename suffix.
//
//   v2 (.ts and the .sNp family with embedded [Version] keyword)
//       The IBIS Open Forum Touchstone 2.0 spec, 2009. Adds bracketed
//       keyword sections around the same numeric body:
//
//           [Version] 2.0
//           # GHz S RI R 50
//           [Number of Ports] 2
//           [Two-Port Order] 12_21
//           [Reference] 50 50
//           [Network Data]
//           1.0 0.1 0.2 ...
//           [End]
//
//       Per-port reference impedances are supported via the
//       port_impedances vector on TouchstoneFile (empty -> reuse the
//       scalar reference_impedance for all ports). [Two-Port Order]
//       lets the file pick between the v1 quirky 21_12 ordering and
//       the conventional 12_21 ordering. v1 files behave as before;
//       v2 detection is based on the presence of the [Version] line.
//
// Format conversions (RI, MA, DB) are supported in both reader paths.
//
// References: Touchstone File Format Specification v1.0 (IBIS Open
// Forum, 2002) and v2.0 (IBIS Open Forum, 2009).

#pragma once

#include <complex>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sikit::touchstone {

enum class Format {
    RealImaginary,
    MagnitudeAngle,
    DbAngle,
};

// Two-port S-parameter ordering quirk: v1 files store 2-port records as
// freq S11 S21 S12 S22 (column-major). v2 lets the file pick between
// that legacy order and the matrix-natural row-major freq S11 S12 S21
// S22. We always keep the in-memory storage column-major; this enum
// only affects the on-disk row order at read/write time.
enum class TwoPortOrder {
    LegacyColumnMajor,   // 21_12 -- freq S11 S21 S12 S22 (v1 default)
    RowMajor,            // 12_21 -- freq S11 S12 S21 S22 (v2 friendly)
};

struct TouchstoneFile {
    int num_ports = 0;
    Format format = Format::RealImaginary;
    double reference_impedance = 50.0;  // ohms (scalar; see port_impedances)
    double frequency_scale = 1.0;       // multiplier to convert raw freq -> Hz

    // Per-port reference impedance (v2 [Reference] section). Empty when
    // the file uses a single scalar Z_ref for all ports.
    std::vector<double> port_impedances;

    // Format version that produced this file: 1 or 2. Used at write
    // time to decide whether to emit v2 [...] keyword sections.
    int version = 1;

    // On-disk 2-port order. Only meaningful when num_ports == 2;
    // ignored otherwise.
    TwoPortOrder two_port_order = TwoPortOrder::LegacyColumnMajor;

    // One entry per frequency point.
    std::vector<double> frequencies;   // in Hz (after scale applied)

    // S-matrices, one per frequency point. Each matrix is stored
    // column-major with size num_ports x num_ports:
    //     S[row + col*num_ports]
    // so s_matrices[k][r + c*N] is the (r,c) entry at frequencies[k].
    std::vector<std::vector<std::complex<double>>> s_matrices;
};

struct TouchstoneParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class TouchstoneReader {
public:
    // Read from disk. num_ports is inferred from the filename suffix
    // (".s1p" → 1, ".s2p" → 2, ".s4p" → 4, ".s8p" → 8). Throws on missing
    // file or unparseable content.
    static TouchstoneFile read_file(const std::filesystem::path& path);

    // Parse from a memory buffer. num_ports must be supplied explicitly
    // since there's no filename to infer it from.
    static TouchstoneFile read_string(std::string_view src, int num_ports);
};

}  // namespace sikit::touchstone
