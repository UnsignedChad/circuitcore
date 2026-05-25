#include "si/Touchstone.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <numbers>
#include <sstream>
#include <utility>

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


// ----- Touchstone v2 helpers --------------------------------------------

// Strip leading/trailing whitespace.
std::string trim(const std::string& s) {
    std::size_t lo = 0, hi = s.size();
    while (lo < hi && std::isspace(static_cast<unsigned char>(s[lo]))) ++lo;
    while (hi > lo && std::isspace(static_cast<unsigned char>(s[hi - 1]))) --hi;
    return s.substr(lo, hi - lo);
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

// Detect whether a Touchstone source string is in v2 form. The IBIS spec
// requires v2 files to carry a "[Version] 2.0" line before any data.
bool is_touchstone_v2(std::string_view src) {
    std::istringstream in{std::string(src)};
    std::string line;
    while (std::getline(in, line)) {
        // Strip ! comments.
        auto bang = line.find('!');
        if (bang != std::string::npos) line = line.substr(0, bang);
        auto t = trim(line);
        if (t.empty()) continue;
        // Stop scanning at the first data line (anything not a keyword
        // or option line). Don't want to walk a 1000-point .s4p body.
        if (t[0] == '[') {
            const auto u = upper(t);
            if (u.rfind("[VERSION]", 0) == 0) return true;
            // Other keywords without a Version line means v2-ish but
            // technically non-conformant; treat as v1 with extra noise.
            continue;
        }
        if (t[0] == '#') continue;
        if (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-' ||
            t[0] == '+') {
            return false;   // hit a data line first
        }
    }
    return false;
}

// Parse a [Reference] line's impedance list.
std::vector<double> parse_reference_line(const std::string& tail) {
    std::vector<double> out;
    std::istringstream in(tail);
    double z;
    while (in >> z) out.push_back(z);
    return out;
}

}  // namespace

namespace {

// v2 parser. Walks the input twice: first pass picks up [Version],
// [Number of Ports], [Reference], [Two-Port Order], and [Matrix Format]
// keywords; second pass extracts the # option line and numeric body
// (same data layout the v1 parser produces).
TouchstoneFile parse_v2(std::string_view src, int default_num_ports) {
    TouchstoneFile out;
    out.version = 2;
    out.num_ports = default_num_ports;   // overridden by [Number of Ports]

    // ----- Pass 1: walk lines, classify keywords + option + data -----
    std::istringstream in{std::string(src)};
    std::string line;
    bool saw_options = false;
    bool inside_network_data = false;
    bool saw_end = false;
    std::vector<double> values;
    while (std::getline(in, line)) {
        auto bang = line.find('!');
        if (bang != std::string::npos) line = line.substr(0, bang);
        auto t = trim(line);
        if (t.empty()) continue;
        if (saw_end) continue;

        if (t[0] == '[') {
            // Keyword section. Parse the bracketed name + tail.
            auto close = t.find(']');
            if (close == std::string::npos) {
                throw TouchstoneParseError(std::format(
                    "malformed v2 keyword line: '{}'", t));
            }
            std::string name = upper(trim(t.substr(1, close - 1)));
            std::string tail = trim(t.substr(close + 1));

            if (name == "VERSION") {
                // Already detected; no-op but accept any 2.x string.
                continue;
            }
            if (name == "NUMBER OF PORTS") {
                std::istringstream is(tail);
                int n = 0; is >> n;
                if (n < 1) throw TouchstoneParseError(
                    "v2: [Number of Ports] requires a positive integer");
                out.num_ports = n;
                continue;
            }
            if (name == "NUMBER OF FREQUENCIES") {
                // Informational only -- we count freq points from data
                // length. Accept and ignore.
                continue;
            }
            if (name == "TWO-PORT ORDER") {
                std::string u = upper(tail);
                if (u == "12_21") {
                    out.two_port_order = TwoPortOrder::RowMajor;
                } else if (u == "21_12") {
                    out.two_port_order = TwoPortOrder::LegacyColumnMajor;
                } else {
                    throw TouchstoneParseError(std::format(
                        "v2: unknown [Two-Port Order] '{}'", tail));
                }
                continue;
            }
            if (name == "REFERENCE") {
                out.port_impedances = parse_reference_line(tail);
                if (!out.port_impedances.empty()) {
                    out.reference_impedance = out.port_impedances.front();
                }
                continue;
            }
            if (name == "MATRIX FORMAT") {
                std::string u = upper(tail);
                if (u != "FULL") {
                    throw TouchstoneParseError(std::format(
                        "v2: only [Matrix Format] Full is supported in v1; "
                        "got '{}'", tail));
                }
                continue;
            }
            if (name == "NETWORK DATA") {
                inside_network_data = true;
                continue;
            }
            if (name == "END") { saw_end = true; continue; }
            // Unknown keyword (e.g. [Information], [Noise Data]) --
            // skip silently so future spec extensions don't break us.
            continue;
        }

        if (t[0] == '#') {
            if (saw_options) continue;
            // Reuse the v1 option-line parsing path inline.
            std::istringstream opts(t.substr(1));
            std::string unit, param, fmt, r_tok;
            double zref = 50.0;
            opts >> unit >> param >> fmt >> r_tok >> zref;
            if (unit.empty() || param.empty() || fmt.empty()) {
                throw TouchstoneParseError(std::format(
                    "v2: malformed option line: '{}'", t));
            }
            param = upper(param);
            if (param != "S") {
                throw TouchstoneParseError(std::format(
                    "v2: only S-parameters supported, got '{}'", param));
            }
            out.frequency_scale = freq_scale_for(unit);
            out.format = format_for(fmt);
            r_tok = upper(r_tok);
            if (r_tok != "R") {
                throw TouchstoneParseError(std::format(
                    "v2: expected 'R' before reference impedance, got '{}'",
                    r_tok));
            }
            if (out.port_impedances.empty()) {
                out.reference_impedance = zref;
            }
            saw_options = true;
            continue;
        }

        // Data line. If [Network Data] keyword has not been seen yet
        // we still accept it -- some files emit the option line and
        // numeric data without the explicit [Network Data] marker.
        (void)inside_network_data;
        std::istringstream data(t);
        double v;
        while (data >> v) values.push_back(v);
    }

    if (!saw_options) {
        throw TouchstoneParseError("v2: missing option line");
    }
    if (out.num_ports < 1) {
        throw TouchstoneParseError("v2: [Number of Ports] not set and no "
                                     "filename hint available");
    }

    // Chunk the value stream into records.
    const std::size_t N = static_cast<std::size_t>(out.num_ports);
    const std::size_t per_record = 1 + 2 * N * N;
    if (values.size() % per_record != 0) {
        throw TouchstoneParseError(std::format(
            "v2: data length {} not a multiple of (1 + 2*{}^2) = {}",
            values.size(), N, per_record));
    }
    const std::size_t num_freqs = values.size() / per_record;
    out.frequencies.reserve(num_freqs);
    out.s_matrices.reserve(num_freqs);

    for (std::size_t k = 0; k < num_freqs; ++k) {
        const std::size_t base = k * per_record;
        out.frequencies.push_back(values[base] * out.frequency_scale);
        std::vector<std::complex<double>> mat(N * N);
        for (std::size_t i = 0; i < N * N; ++i) {
            const double a = values[base + 1 + 2 * i];
            const double b = values[base + 2 + 2 * i];
            mat[i] = to_complex(a, b, out.format);
        }
        // Reordering: for 2-port v1 / 12_21 v2, the on-disk order is
        // S11 S12 S21 S22 (row-major), but our column-major storage
        // wants S11 S21 S12 S22. The v1 path keeps the legacy
        // 21_12 ordering literally (read = stored). For row-major
        // we swap entries 1 and 2.
        if (N == 2 && out.two_port_order == TwoPortOrder::RowMajor) {
            std::swap(mat[1], mat[2]);
        }
        out.s_matrices.push_back(std::move(mat));
    }
    return out;
}

}  // namespace

TouchstoneFile TouchstoneReader::read_string(std::string_view src, int num_ports) {
    if (is_touchstone_v2(src)) {
        return parse_v2(src, num_ports);
    }
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
