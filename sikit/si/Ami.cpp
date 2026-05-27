// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/Ami.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include "circuitcore/sexpr/SExpr.h"

namespace sikit::ibis::ami {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

ParamUsage parse_usage(std::string_view s) {
    const std::string u = upper(std::string(s));
    if (u == "IN")    return ParamUsage::In;
    if (u == "OUT")   return ParamUsage::Out;
    if (u == "INOUT" || u == "IN_OUT") return ParamUsage::InOut;
    if (u == "INFO")  return ParamUsage::Info;
    return ParamUsage::Unknown;
}

ParamType parse_type(std::string_view s) {
    const std::string u = upper(std::string(s));
    if (u == "INTEGER") return ParamType::Integer;
    if (u == "FLOAT")   return ParamType::Float;
    if (u == "BOOLEAN") return ParamType::Boolean;
    if (u == "STRING")  return ParamType::String;
    if (u == "TAP")     return ParamType::Tap;
    return ParamType::Unknown;
}

// One parameter node looks like
//   (Tap1 (Usage In) (Type Float) (Range 0.0 -0.5 0.5) (Default 0.0))
// where the head is the parameter's name and the children are tagged
// attributes.
AmiParameter parse_parameter(const circuitcore::sexpr::Node& n) {
    AmiParameter p;
    if (!n.is_list() || n.children.empty()) return p;
    if (n.children[0].is_symbol() || n.children[0].is_string()) {
        p.name = n.children[0].text;
    }
    for (std::size_t i = 1; i < n.children.size(); ++i) {
        const auto& c = n.children[i];
        if (!c.is_list() || c.children.empty()) continue;
        const std::string tag = upper(std::string(c.tag()));
        if (tag == "USAGE" && c.children.size() >= 2) {
            p.usage = parse_usage(c.children[1].text);
        } else if (tag == "TYPE" && c.children.size() >= 2) {
            p.type = parse_type(c.children[1].text);
        } else if (tag == "DEFAULT" && c.children.size() >= 2) {
            const auto& v = c.children[1];
            p.default_value = v.is_number() ? v.text : std::string(v.text);
        } else if (tag == "RANGE" && c.children.size() >= 4) {
            p.has_range = true;
            // IBIS-AMI convention: Range <typ> <min> <max>.
            p.range_typ = c.children[1].number;
            p.range_min = c.children[2].number;
            p.range_max = c.children[3].number;
        } else if (tag == "DESCRIPTION" && c.children.size() >= 2) {
            p.description = c.children[1].text;
        }
    }
    return p;
}

void collect_params_from_group(const circuitcore::sexpr::Node& group,
                                std::vector<AmiParameter>& out) {
    if (!group.is_list()) return;
    for (std::size_t i = 1; i < group.children.size(); ++i) {
        const auto& c = group.children[i];
        if (c.is_list() && !c.children.empty()) {
            out.push_back(parse_parameter(c));
        }
    }
}

}  // namespace

AmiFile AmiParser::read_string(std::string_view src) {
    circuitcore::sexpr::Node root;
    try {
        root = circuitcore::sexpr::parse(src);
    } catch (const circuitcore::sexpr::ParseError& e) {
        throw AmiParseError(std::string("ami: ") + e.what());
    }

    AmiFile f;
    if (!root.is_list() || root.children.empty()) {
        throw AmiParseError("ami: top-level must be a named S-expression list");
    }
    // Top-level head is the model name.
    f.model_name = root.children[0].text;

    for (std::size_t i = 1; i < root.children.size(); ++i) {
        const auto& c = root.children[i];
        const std::string tag = upper(std::string(c.tag()));
        if (tag == "RESERVED_PARAMETERS") {
            collect_params_from_group(c, f.reserved);
        } else if (tag == "MODEL_SPECIFIC") {
            collect_params_from_group(c, f.model_specific);
        }
        // Unknown top-level groups (e.g. "Description") just get skipped.
    }
    return f;
}

AmiFile AmiParser::read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw AmiParseError(std::string("ami: cannot open file: ") + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return read_string(ss.str());
}

// ---------- AmiModel: dynamic loader ----------

#ifndef _WIN32

AmiModel::AmiModel(const std::filesystem::path& library_path) {
    dl_handle_ = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!dl_handle_) {
        const char* err = dlerror();
        throw AmiLoadError(std::string("ami: dlopen failed: ") +
                            (err ? err : "(no error message)"));
    }
    dlerror();  // clear residual error state
    init_fp_    = dlsym(dl_handle_, "AMI_Init");
    getwave_fp_ = dlsym(dl_handle_, "AMI_GetWave");
    close_fp_   = dlsym(dl_handle_, "AMI_Close");
    if (!init_fp_) {
        dlclose(dl_handle_);
        dl_handle_ = nullptr;
        throw AmiLoadError("ami: AMI_Init symbol not found in library");
    }
    // AMI_GetWave is optional per the spec; AMI_Close is required.
    if (!close_fp_) {
        dlclose(dl_handle_);
        dl_handle_ = nullptr;
        throw AmiLoadError("ami: AMI_Close symbol not found in library");
    }
}

AmiModel::~AmiModel() {
    if (ami_handle_ && close_fp_) {
        using close_fn_t = long(*)(void*);
        reinterpret_cast<close_fn_t>(close_fp_)(ami_handle_);
        ami_handle_ = nullptr;
    }
    if (dl_handle_) {
        dlclose(dl_handle_);
        dl_handle_ = nullptr;
    }
}

#else  // Windows: LoadLibrary / GetProcAddress mirror of the POSIX path.

AmiModel::AmiModel(const std::filesystem::path& library_path) {
    // Use the native wide-string form so paths with non-ASCII characters
    // load. LoadLibraryW returns NULL on failure; GetLastError gives the
    // Win32 status -- wrap it into something a human can read.
    const std::wstring native = library_path.native();
    HMODULE h = LoadLibraryW(native.c_str());
    if (!h) {
        const DWORD err = GetLastError();
        throw AmiLoadError("ami: LoadLibrary failed for " +
                            library_path.string() +
                            " (Win32 error " + std::to_string(err) + ")");
    }
    dl_handle_  = reinterpret_cast<void*>(h);
    init_fp_    = reinterpret_cast<void*>(GetProcAddress(h, "AMI_Init"));
    getwave_fp_ = reinterpret_cast<void*>(GetProcAddress(h, "AMI_GetWave"));
    close_fp_   = reinterpret_cast<void*>(GetProcAddress(h, "AMI_Close"));
    if (!init_fp_) {
        FreeLibrary(h);
        dl_handle_ = nullptr;
        throw AmiLoadError("ami: AMI_Init symbol not found in library");
    }
    // AMI_GetWave is optional per the spec; AMI_Close is required.
    if (!close_fp_) {
        FreeLibrary(h);
        dl_handle_ = nullptr;
        throw AmiLoadError("ami: AMI_Close symbol not found in library");
    }
}

AmiModel::~AmiModel() {
    if (ami_handle_ && close_fp_) {
        using close_fn_t = long(*)(void*);
        reinterpret_cast<close_fn_t>(close_fp_)(ami_handle_);
        ami_handle_ = nullptr;
    }
    if (dl_handle_) {
        FreeLibrary(reinterpret_cast<HMODULE>(dl_handle_));
        dl_handle_ = nullptr;
    }
}

#endif

bool AmiModel::has_get_wave()      const noexcept { return getwave_fp_ != nullptr; }
bool AmiModel::init_available()    const noexcept { return init_fp_    != nullptr; }
bool AmiModel::close_available()   const noexcept { return close_fp_   != nullptr; }

AmiModel::InitResult AmiModel::init(std::vector<double>& impulse_matrix,
                                      long row_size, long aggressors,
                                      double sample_interval_s,
                                      double bit_time_s,
                                      const std::string& parameters_in) {
    InitResult r;
    if (!init_fp_) {
        throw AmiLoadError("ami: model has no AMI_Init");
    }
    using init_fn_t = long(*)(double*, long, long, double, double,
                                const char*, char**, void**, char**);
    char* params_out = nullptr;
    char* msg        = nullptr;
    auto fn = reinterpret_cast<init_fn_t>(init_fp_);
    r.return_code = fn(impulse_matrix.data(), row_size, aggressors,
                       sample_interval_s, bit_time_s,
                       parameters_in.c_str(),
                       &params_out, &ami_handle_, &msg);
    if (params_out) { r.parameters_out = params_out; std::free(params_out); }
    if (msg)        { r.message        = msg;        std::free(msg); }
    return r;
}

AmiModel::GetWaveResult AmiModel::get_wave(std::vector<double>& wave,
                                             std::vector<double>& clock_times) {
    GetWaveResult r;
    if (!getwave_fp_) {
        throw AmiLoadError("ami: model has no AMI_GetWave");
    }
    using getwave_fn_t = long(*)(double*, long, double*, char**, void*);
    if (clock_times.size() < wave.size()) clock_times.resize(wave.size(), 0.0);
    char* params_out = nullptr;
    auto fn = reinterpret_cast<getwave_fn_t>(getwave_fp_);
    r.return_code = fn(wave.data(), static_cast<long>(wave.size()),
                       clock_times.data(), &params_out, ami_handle_);
    if (params_out) { r.parameters_out = params_out; std::free(params_out); }
    return r;
}

}  // namespace sikit::ibis::ami
