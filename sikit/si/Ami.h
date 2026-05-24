// IBIS-AMI (Algorithmic Modeling Interface) support.
//
// AMI extends IBIS with vendor-supplied executable models for SerDes
// equalization (FFE / DFE / CTLE / clock recovery). The interface has
// two parts:
//
//   1. A text parameter file (.ami) declaring the model's input and
//      output parameters in an S-expression format. Parsed by
//      AmiParser into AmiFile.
//
//   2. A shared library (.so on Linux, .dll on Windows) exposing three
//      C entry points: AMI_Init, AMI_GetWave, AMI_Close. Loaded
//      dynamically by AmiModel via dlopen / dlsym (or LoadLibrary on
//      Windows). The library is vendor-locked and platform-specific.
//
// v0 scope: the parser is complete and tested. The loader compiles
// against the documented AMI ABI and can be wired up by callers, but
// integration testing requires a real .ami + .so pair which we don't
// ship with sikit. Standard reference is the IBIS spec, §10.
//
// Reference: IBIS Open Forum, IBIS specification version 7.0, §10.

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sikit::ibis::ami {

enum class ParamUsage { In, Out, InOut, Info, Unknown };
enum class ParamType  { Integer, Float, Boolean, String, Tap, Unknown };

// One declared parameter from a .ami file.
struct AmiParameter {
    std::string name;
    ParamUsage  usage = ParamUsage::Unknown;
    ParamType   type  = ParamType::Unknown;
    std::string default_value;
    std::string description;
    // Range fields only populated for numeric types that declare (Range ...).
    bool   has_range = false;
    double range_typ = 0.0;
    double range_min = 0.0;
    double range_max = 0.0;
};

struct AmiFile {
    std::string model_name;
    std::vector<AmiParameter> reserved;        // (Reserved_Parameters ...)
    std::vector<AmiParameter> model_specific;  // (Model_Specific ...)
};

struct AmiParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class AmiParser {
public:
    static AmiFile read_file(const std::filesystem::path& path);
    static AmiFile read_string(std::string_view src);
};

// Dynamic-library loader for vendor AMI executables. RAII: constructor
// dlopens, destructor dlcloses (and calls AMI_Close if a handle is live).
//
// Compiles unconditionally on POSIX; on Windows the dlopen calls would
// need to be swapped for LoadLibrary. We keep the surface portable by
// hiding the actual mechanism behind opaque void* handles.
class AmiModel {
public:
    explicit AmiModel(const std::filesystem::path& library_path);
    ~AmiModel();

    AmiModel(const AmiModel&) = delete;
    AmiModel& operator=(const AmiModel&) = delete;

    // True if the loaded model implements AMI_GetWave (some Init-only
    // models exist for impulse-response shaping).
    bool has_get_wave() const noexcept;

    // Invoke AMI_Init with the given impulse response. Mutates the
    // impulse_matrix in place per the AMI spec. parameters_in is the
    // AMI-format string to pass; returns the model's parameters_out
    // and any message string.
    struct InitResult {
        long return_code = 0;
        std::string parameters_out;
        std::string message;
    };
    InitResult init(std::vector<double>& impulse_matrix,
                    long row_size,
                    long aggressors,
                    double sample_interval_s,
                    double bit_time_s,
                    const std::string& parameters_in);

    // Run the time-domain waveform through the model. Wave and
    // clock_times are mutated in place.
    struct GetWaveResult {
        long return_code = 0;
        std::string parameters_out;
    };
    GetWaveResult get_wave(std::vector<double>& wave,
                            std::vector<double>& clock_times);

    // Resolved symbol availability — useful for callers wanting to
    // decide which path to take before allocating buffers.
    bool init_available()     const noexcept;
    bool close_available()    const noexcept;

private:
    void* dl_handle_  = nullptr;   // dlopen handle
    void* ami_handle_ = nullptr;   // AMI_memory_handle (per AMI_Init)
    void* init_fp_    = nullptr;   // AMI_Init function pointer
    void* getwave_fp_ = nullptr;
    void* close_fp_   = nullptr;
};

struct AmiLoadError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace sikit::ibis::ami
