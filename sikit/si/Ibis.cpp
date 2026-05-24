#include "si/Ibis.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>

namespace sikit::ibis {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

std::string trim(std::string_view sv) {
    std::size_t lo = 0, hi = sv.size();
    while (lo < hi && std::isspace(static_cast<unsigned char>(sv[lo]))) ++lo;
    while (hi > lo && std::isspace(static_cast<unsigned char>(sv[hi - 1]))) --hi;
    return std::string(sv.substr(lo, hi - lo));
}

// IBIS numbers can carry an engineering unit suffix (T/G/M/k/m/u/n/p/f)
// and a trailing unit letter (V, A, Ohm, F, H, s) which we ignore. The
// literal token "NA" means the column was not supplied — return NaN.
double parse_ibis_number(std::string_view tok) {
    std::string t = trim(tok);
    if (t.empty()) return kNaN;
    if (upper(t) == "NA") return kNaN;

    const char* begin = t.data();
    const char* end_ptr = begin;
    double v = std::strtod(begin, const_cast<char**>(&end_ptr));
    if (end_ptr == begin) return kNaN;  // not a number

    // Suffix character — first non-numeric char after the mantissa.
    std::string suffix(end_ptr, t.size() - (end_ptr - begin));
    if (suffix.empty()) return v;
    const char c = suffix[0];
    switch (c) {
        case 'T': v *= 1e12;  break;
        case 'G': v *= 1e9;   break;
        case 'M':
            // 'M' is mega; but 'm' is milli. IBIS spec is unambiguous on case.
            v *= 1e6;
            break;
        case 'k': case 'K': v *= 1e3;   break;
        case 'm':           v *= 1e-3;  break;
        case 'u': case 'U': v *= 1e-6;  break;
        case 'n': case 'N': v *= 1e-9;  break;
        case 'p': case 'P': v *= 1e-12; break;
        case 'f': case 'F': v *= 1e-15; break;
        default: break;  // bare unit letter like 'V' / 'A' / 's' — ignore
    }
    return v;
}

// Split a whitespace-separated line into tokens.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream is(line);
    std::string t;
    while (is >> t) out.push_back(std::move(t));
    return out;
}

// Some IBIS files use "Foo/Bar" for the Ramp section — split.
std::pair<std::string, std::string> split_slash(std::string_view t) {
    auto slash = t.find('/');
    if (slash == std::string::npos) return {std::string(t), ""};
    return {std::string(t.substr(0, slash)), std::string(t.substr(slash + 1))};
}

ModelType parse_model_type(std::string_view s) {
    std::string u = upper(std::string(s));
    if (u == "INPUT")          return ModelType::Input;
    if (u == "OUTPUT")         return ModelType::Output;
    if (u == "I/O")            return ModelType::IO;
    if (u == "3-STATE" || u == "TRISTATE") return ModelType::Tristate;
    if (u == "OPEN_DRAIN")     return ModelType::OpenDrain;
    if (u == "OPEN_SINK")      return ModelType::OpenSink;
    if (u == "OPEN_SOURCE")    return ModelType::OpenSource;
    if (u == "TERMINATOR")     return ModelType::Terminator;
    if (u == "SERIES")         return ModelType::Series;
    if (u == "SERIES_SWITCH")  return ModelType::SeriesSwitch;
    return ModelType::Unknown;
}

class Parser {
public:
    explicit Parser(std::string_view src) : src_(src) {}

    IbisFile parse() {
        std::istringstream is{std::string(src_)};
        std::string raw;
        while (std::getline(is, raw)) {
            ++line_no_;
            const std::string line = strip_comment(raw);
            const std::string t = trim(line);
            if (t.empty()) continue;

            if (t.front() == '[') {
                handle_section(t);
                continue;
            }
            handle_data(t);
        }
        // Push the last model out (no [End] required for graceful files).
        flush_model();
        return std::move(file_);
    }

private:
    enum class Mode { TopLevel, InModel, Pulldown, Pullup, Ramp, OtherIgnored };

    static std::string strip_comment(const std::string& line) {
        auto bar = line.find('|');
        return (bar == std::string::npos) ? line : line.substr(0, bar);
    }

    std::string extract_section_name(const std::string& sect) {
        // "[IBIS Ver]   5.0" → name = "IBIS Ver", rest = "5.0"
        auto close = sect.find(']');
        if (close == std::string::npos) {
            throw IbisParseError("malformed section header at line " +
                                  std::to_string(line_no_));
        }
        return trim(sect.substr(1, close - 1));
    }

    std::string extract_section_value(const std::string& sect) {
        auto close = sect.find(']');
        if (close == std::string::npos) return "";
        return trim(sect.substr(close + 1));
    }

    void flush_model() {
        if (current_model_active_) {
            file_.models.push_back(std::move(current_model_));
            current_model_ = Model{};
            current_model_active_ = false;
        }
    }

    void handle_section(const std::string& sect) {
        const std::string name = upper(extract_section_name(sect));
        const std::string value = extract_section_value(sect);

        if (name == "IBIS VER") {
            file_.version = value;
            mode_ = Mode::TopLevel;
        } else if (name == "COMPONENT") {
            // [Component] may also appear inline as "[Component] Foo".
            if (!value.empty()) file_.component = value;
            mode_ = Mode::TopLevel;
        } else if (name == "MANUFACTURER") {
            mode_ = Mode::TopLevel;
            pending_keyword_ = "MANUFACTURER";
            if (!value.empty()) file_.manufacturer = value;
        } else if (name == "MODEL") {
            flush_model();
            current_model_active_ = true;
            current_model_ = Model{};
            current_model_.name = value;
            mode_ = Mode::InModel;
        } else if (name == "PULLDOWN")     mode_ = Mode::Pulldown;
        else if   (name == "PULLUP")       mode_ = Mode::Pullup;
        else if   (name == "RAMP")         mode_ = Mode::Ramp;
        else if   (name == "END")          mode_ = Mode::TopLevel;
        else {
            // Unknown / un-handled section — switch to a benign skip mode
            // but keep model state alive so subsequent [Pulldown] etc still
            // attach to it.
            mode_ = current_model_active_ ? Mode::InModel : Mode::OtherIgnored;
        }
    }

    void handle_data(const std::string& line) {
        switch (mode_) {
            case Mode::TopLevel:
                handle_topshallow(line);
                break;
            case Mode::InModel:
                handle_in_model(line);
                break;
            case Mode::Pulldown:
                if (auto p = parse_vi_row(line)) {
                    current_model_.pulldown.push_back(*p);
                }
                break;
            case Mode::Pullup:
                if (auto p = parse_vi_row(line)) {
                    current_model_.pullup.push_back(*p);
                }
                break;
            case Mode::Ramp:
                handle_ramp(line);
                break;
            case Mode::OtherIgnored:
                break;
        }
    }

    void handle_topshallow(const std::string& line) {
        // Continuation of a pending top-level keyword whose value is on
        // the following line (e.g. Manufacturer with no inline value).
        if (pending_keyword_ == "MANUFACTURER") {
            file_.manufacturer = trim(line);
            pending_keyword_.clear();
        }
    }

    void handle_in_model(const std::string& line) {
        auto toks = tokenize(line);
        if (toks.empty()) return;
        const std::string key = upper(toks[0]);

        if (key == "MODEL_TYPE" && toks.size() >= 2) {
            current_model_.type = parse_model_type(toks[1]);
        } else if (key == "C_COMP") {
            // Some files supply only typ (e.g. "C_comp 1pF"); fill any
            // missing min/max with the same value so callers downstream
            // don't have to defend against a zero default.
            if (toks.size() >= 2) {
                current_model_.c_comp.typ = parse_ibis_number(toks[1]);
                current_model_.c_comp.min = current_model_.c_comp.typ;
                current_model_.c_comp.max = current_model_.c_comp.typ;
            }
            if (toks.size() >= 3) current_model_.c_comp.min = parse_ibis_number(toks[2]);
            if (toks.size() >= 4) current_model_.c_comp.max = parse_ibis_number(toks[3]);
        }
        // Everything else (Voltage_Range, Vinl, Vinh, Submodel etc.) is
        // ignored in v0 — it's there but we don't have a use for it yet.
    }

    std::optional<ViPoint> parse_vi_row(const std::string& line) {
        auto toks = tokenize(line);
        if (toks.size() < 4) return std::nullopt;
        ViPoint p;
        p.voltage = parse_ibis_number(toks[0]);
        p.i_typ   = parse_ibis_number(toks[1]);
        p.i_min   = parse_ibis_number(toks[2]);
        p.i_max   = parse_ibis_number(toks[3]);
        if (std::isnan(p.voltage)) return std::nullopt;
        return p;
    }

    void handle_ramp(const std::string& line) {
        auto toks = tokenize(line);
        if (toks.size() < 4) return;
        const std::string key = upper(toks[0]);
        auto parse_pair = [](const std::string& s) -> std::pair<double, double> {
            auto [v, t] = split_slash(s);
            return {parse_ibis_number(v), parse_ibis_number(t)};
        };
        const auto p_typ = parse_pair(toks[1]);
        const auto p_min = parse_pair(toks[2]);
        const auto p_max = parse_pair(toks[3]);
        if (key == "DV/DT_R") {
            current_model_.ramp.dv_rise = {p_typ.first, p_min.first, p_max.first};
            current_model_.ramp.dt_rise = {p_typ.second, p_min.second, p_max.second};
        } else if (key == "DV/DT_F") {
            current_model_.ramp.dv_fall = {p_typ.first, p_min.first, p_max.first};
            current_model_.ramp.dt_fall = {p_typ.second, p_min.second, p_max.second};
        }
    }

    std::string_view src_;
    IbisFile file_;
    Mode mode_ = Mode::TopLevel;
    Model current_model_{};
    bool current_model_active_ = false;
    std::string pending_keyword_;
    int line_no_ = 0;
};

}  // namespace

IbisFile IbisReader::read_string(std::string_view src) {
    return Parser(src).parse();
}

IbisFile IbisReader::read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw IbisParseError("cannot open IBIS file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return read_string(ss.str());
}

}  // namespace sikit::ibis
