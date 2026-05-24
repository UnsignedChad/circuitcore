// Touchstone (.s1p / .s2p / .s4p / .s8p) S-parameter file reader.
//
// Touchstone is the de facto interchange format for measured / simulated
// network parameters in SI work. Format:
//
//   ! comment lines start with bang
//   # <freq_unit> <param_type> <format> R <ref_impedance>
//   <freq>  <p1_a> <p1_b>  <p2_a> <p2_b>  ...
//   ...
//
// We support S-parameters in RI (real/imag), MA (mag/angle-deg), and
// DB (dB/angle-deg) formats. Reference impedance is parsed from the option
// line. Multi-line data records (common for .s4p+) are handled by streaming
// floats and chunking into 1 + 2N² values per frequency point.
//
// Reference: Touchstone File Format Specification, Version 1.0 (IBIS Open Forum).

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

struct TouchstoneFile {
    int num_ports = 0;
    Format format = Format::RealImaginary;
    double reference_impedance = 50.0;  // ohms
    double frequency_scale = 1.0;       // multiplier to convert raw freq → Hz

    // One entry per frequency point.
    std::vector<double> frequencies;   // in Hz (after scale applied)

    // S-matrices, one per frequency point. Each matrix is stored
    // **column-major** with size num_ports × num_ports:
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
