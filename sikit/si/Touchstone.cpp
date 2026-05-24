#include "si/Touchstone.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <numbers>
#include <sstream>

namespace sikit::touchstone {

namespace {

double freq_scale_for(std::string unit) {
    std::transform(unit.begin(), unit.end(), unit.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (unit == "HZ")  return 1.0;
    if (unit == "KHZ") return 1e3;
    if (unit == "MHZ") return 1e6;
    if (unit == "GHZ") return 1e9;
    if (unit == "THZ") return 1e12;
    throw TouchstoneParseError(std::format("unknown frequency unit '{}'", unit));
}

Format format_for(std::string fmt) {
    std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (fmt == "RI") return Format::RealImaginary;
    if (fmt == "MA") return Format::MagnitudeAngle;
    if (fmt == "DB") return Format::DbAngle;
    throw TouchstoneParseError(std::format("unknown data format '{}' (want RI, MA, or DB)", fmt));
}

std::complex<double> to_complex(double a, double b, Format f) {
    switch (f) {
        case Format::RealImaginary:
            return {a, b};
        case Format::MagnitudeAngle: {
            const double rad = b * std::numbers::pi / 180.0;
            return std::polar(a, rad);
        }
        case Format::DbAngle: {
            const double mag = std::pow(10.0, a / 20.0);
            const double rad = b * std::numbers::pi / 180.0;
            return std::polar(mag, rad);
        }
    }
    return {};  // unreachable
}

// Strip comments (starting with '!') and trailing whitespace. Returns lowercased
// option-line tokens if the line begins with '#', otherwise returns the data line
// unchanged.
std::string strip_comment(const std::string& line) {
    auto bang = line.find('!');
    return (bang == std::string::npos) ? line : line.substr(0, bang);
}

int infer_num_ports(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    // Lowercase the extension.
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // Expect ".sNp" where N is a positive integer.
    if (ext.size() >= 4 && ext.front() == '.' && ext[1] == 's' && ext.back() == 'p') {
        try {
            return std::stoi(ext.substr(2, ext.size() - 3));
        } catch (...) {
            // fall through to error
        }
    }
    throw TouchstoneParseError(std::format(
        "cannot infer port count from filename '{}' (expected .sNp)", path.string()));
}

}  // namespace

TouchstoneFile TouchstoneReader::read_string(std::string_view src, int num_ports) {
    if (num_ports < 1) {
        throw TouchstoneParseError(
            std::format("num_ports must be >= 1, got {}", num_ports));
    }

    TouchstoneFile out;
    out.num_ports = num_ports;

    // Walk lines. First non-comment '#' line is the option line; everything
    // after is whitespace-separated floats forming records of 1+2N² values.
    std::vector<double> values;
    bool saw_options = false;

    std::istringstream in{std::string(src)};
    std::string line;
    while (std::getline(in, line)) {
        std::string clean = strip_comment(line);
        // Trim trailing whitespace.
        while (!clean.empty() &&
               std::isspace(static_cast<unsigned char>(clean.back()))) {
            clean.pop_back();
        }
        if (clean.empty()) continue;

        // Trim leading whitespace.
        std::size_t i = 0;
        while (i < clean.size() &&
               std::isspace(static_cast<unsigned char>(clean[i]))) {
            ++i;
        }
        if (i >= clean.size()) continue;

        if (clean[i] == '#') {
            if (saw_options) {
                // Multiple option lines: take the first, ignore later ones
                // (some tools emit redundant headers).
                continue;
            }
            // Tokenize after the '#'.
            std::istringstream opts(clean.substr(i + 1));
            std::string unit, param, fmt, r_tok;
            double zref = 50.0;
            opts >> unit >> param >> fmt >> r_tok >> zref;
            if (unit.empty() || param.empty() || fmt.empty()) {
                throw TouchstoneParseError(std::format(
                    "malformed option line: '{}'", clean));
            }
            std::transform(param.begin(), param.end(), param.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (param != "S") {
                throw TouchstoneParseError(std::format(
                    "unsupported parameter type '{}' (only S supported in v0)", param));
            }
            out.frequency_scale = freq_scale_for(unit);
            out.format = format_for(fmt);
            std::transform(r_tok.begin(), r_tok.end(), r_tok.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (r_tok != "R") {
                throw TouchstoneParseError(std::format(
                    "expected 'R' before reference impedance, got '{}'", r_tok));
            }
            out.reference_impedance = zref;
            saw_options = true;
            continue;
        }

        // Data line — extract floats.
        std::istringstream data(clean.substr(i));
        double v;
        while (data >> v) values.push_back(v);
    }

    if (!saw_options) {
        throw TouchstoneParseError("missing option line (# ... R ...)");
    }

    // Chunk values into records: 1 frequency + 2*N² floats per record.
    const std::size_t per_record = 1 + 2 * static_cast<std::size_t>(num_ports) *
                                       static_cast<std::size_t>(num_ports);
    if (values.size() % per_record != 0) {
        throw TouchstoneParseError(std::format(
            "data size {} is not a multiple of expected record length {} "
            "(for {}-port file)",
            values.size(), per_record, num_ports));
    }
    const std::size_t n_freq = values.size() / per_record;
    out.frequencies.reserve(n_freq);
    out.s_matrices.reserve(n_freq);

    const std::size_t N = static_cast<std::size_t>(num_ports);

    for (std::size_t k = 0; k < n_freq; ++k) {
        const std::size_t off = k * per_record;
        out.frequencies.push_back(values[off] * out.frequency_scale);

        std::vector<std::complex<double>> mat(N * N);
        // Touchstone ordering:
        //   * 2-port files: S11 S21 S12 S22 (yes, S21 before S12 — historical
        //     and yes that is column-major naturally).
        //   * N != 2 files: row-major (S11 S12 ... S1N, S21 ... S2N, ...).
        if (num_ports == 2) {
            // 4 complex values: S11 S21 S12 S22, already column-major when
            // mapped to mat[r + c*N].
            for (std::size_t idx = 0; idx < 4; ++idx) {
                mat[idx] = to_complex(values[off + 1 + 2 * idx],
                                      values[off + 2 + 2 * idx],
                                      out.format);
            }
        } else {
            // Row-major in the file → column-major in storage.
            for (std::size_t r = 0; r < N; ++r) {
                for (std::size_t c = 0; c < N; ++c) {
                    const std::size_t file_idx = r * N + c;
                    mat[r + c * N] = to_complex(values[off + 1 + 2 * file_idx],
                                                values[off + 2 + 2 * file_idx],
                                                out.format);
                }
            }
        }
        out.s_matrices.push_back(std::move(mat));
    }

    return out;
}

TouchstoneFile TouchstoneReader::read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw TouchstoneParseError(
            std::format("cannot open Touchstone file: {}", path.string()));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return read_string(ss.str(), infer_num_ports(path));
}

}  // namespace sikit::touchstone
