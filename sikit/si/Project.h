// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Project file (.sikitproj) loader / saver.
//
// Captures the persistent state of a sikit session so the user can pick up
// where they left off without re-opening the PCB, re-pointing at IBIS/AMI
// model files, or re-flipping the FDM toggle. Format is a tagged S-
// expression that reuses sikit::sexpr for parsing — same machinery the
// KiCad PCB and IBIS parsers use.
//
// Example file:
//
//   (sikit-project
//     (version 1)
//     (kicad-pcb "path/to/board.kicad_pcb")
//     (ibis    (file "drv.ibs") (model "TX_3V3"))
//     (ami     (params "drv.ami") (library "drv.so"))
//     (engine  (use-fdm true))
//     (observed-nets "USB_DP" "USB_DN" "CLK0_P"))
//
// All paths are stored verbatim as the user supplied them. They may be
// absolute or relative to the .sikitproj — the loader does not normalize.
// Missing optional sections (no IBIS yet, no AMI, no observed nets) just
// produce empty fields in the result.

#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sikit::project {

struct IbisRef {
    std::string file;    // .ibs path
    std::string model;   // active model name (may be empty if user didn't pick)
};

struct AmiRef {
    std::string params;     // .ami parameter file
    std::string library;    // .so / .dll / .dylib path
};

struct Project {
    int version = 1;
    std::string kicad_pcb;                       // .kicad_pcb path; may be empty
    std::optional<IbisRef> ibis;
    std::optional<AmiRef>  ami;
    bool use_fdm = false;
    std::vector<std::string> observed_nets;      // net names to watch
};

struct ProjectIoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Load a .sikitproj from disk. Throws on missing file or malformed
// contents.
Project load_project(const std::filesystem::path& path);

// Serialise a Project to disk in pretty-printed S-expression form. Throws
// if the file cannot be written.
void save_project(const Project& p, const std::filesystem::path& path);

// Helpers exposed for testing.
std::string serialize_to_string(const Project& p);
Project parse_from_string(const std::string& src);

}  // namespace sikit::project
