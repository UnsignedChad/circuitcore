// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// IBIS (I/O Buffer Information Specification) file reader.
//
// IBIS is the vendor-neutral behavioral description format for digital
// I/O buffers — SDRAM pins, SerDes pads, GPIO drivers etc. Each Model
// block carries V/I tables (Pulldown, Pullup, ground/power clamps) plus
// timing data (Ramp) that together let you simulate a buffer's transient
// behavior without the vendor's proprietary SPICE netlist.
//
// v0 scope:
//   - File-level keywords: IBIS Ver, Component, Manufacturer
//   - Model blocks with type, C_comp (typ/min/max), Pulldown V/I table,
//     Pullup V/I table, Ramp (dV/dt rise + fall)
//   - Skips everything else gracefully (Voltage Range, Pin map, Submodels,
//     temperature ranges, packaging, GND Clamp, Power Clamp — known
//     unknowns that will land in follow-up commits)
//
// Reference: IBIS Open Forum, "I/O Buffer Information Specification" v5.0.

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sikit::ibis {

enum class ModelType {
    Input,
    Output,
    IO,
    Tristate,
    OpenDrain,
    OpenSink,
    OpenSource,
    Terminator,
    Series,
    SeriesSwitch,
    Unknown,
};

// One row of an IBIS V/I table. Currents that were listed as "NA" in the
// source file are returned as NaN — callers should check with std::isnan.
struct ViPoint {
    double voltage = 0.0;   // V
    double i_typ   = 0.0;   // A (NaN if NA)
    double i_min   = 0.0;
    double i_max   = 0.0;
};

// Typical / minimum / maximum tuple — IBIS-style "three corners" data.
struct TypMinMax {
    double typ = 0.0;
    double min = 0.0;
    double max = 0.0;
};

struct Ramp {
    // dV/dt is given as a pair (dV, dt) — voltage swing and time. We
    // store them separately so callers can compute dV/dt themselves and
    // also reconstruct the underlying values.
    TypMinMax dv_rise;   // V
    TypMinMax dt_rise;   // s
    TypMinMax dv_fall;
    TypMinMax dt_fall;
};

struct Model {
    std::string name;
    ModelType type = ModelType::Unknown;
    TypMinMax c_comp;                 // F
    std::vector<ViPoint> pulldown;    // V→I for pulldown FET
    std::vector<ViPoint> pullup;      // V→I for pullup FET
    Ramp ramp;
};

struct IbisFile {
    std::string version;
    std::string component;
    std::string manufacturer;
    std::vector<Model> models;
};

struct IbisParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class IbisReader {
public:
    static IbisFile read_file(const std::filesystem::path& path);
    static IbisFile read_string(std::string_view src);
};

}  // namespace sikit::ibis
