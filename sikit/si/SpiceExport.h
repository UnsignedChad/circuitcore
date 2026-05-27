// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Foster-canonical SPICE-3 emitter for a vector-fit rational transfer
// function. Produces a .subckt that any SPICE flavour (ngspice, LTspice,
// HSPICE, Spectre's spice mode) can load. The subcircuit input is an
// ideal voltage probe; the output is a controlled voltage source whose
// frequency response equals the vector-fit reconstruction of S21.
//
// Topology (per pole n, summed at the output node):
//
//      Vin ---[Rn = 1 ohm]---+----[Cn = -1/p_n]--- GND
//                            |
//                            V(node_n)  driven across the RC pole
//
//      Vout = sum_n r_n * V(node_n)  +  d * V(in)
//
// Each branch is a first-order RC pole 1/(s - p_n) scaled by residue
// r_n; the d (direct) term is added as a feed-through controlled
// source. The whole assembly realises
//
//      H(s) = sum_n r_n / (s - p_n) + d
//
// to within numerical precision.
//
// Why Foster ladder and not ngspice's LAPLACE element: the LAPLACE
// behavioural primitive is ngspice/LTspice specific and the polynomial-
// coefficient form is fragile (Newton's iteration on a 24th-order
// polynomial is not what you want next to a SerDes transient). Foster
// branches are universal SPICE and run cleanly in any solver. They
// produce 12-20 extra nodes per channel, which is nothing next to the
// 10k+ nodes a real IC model brings.
//
// What this module does NOT do (yet):
//   * Multi-port export. Single H(s) -> single .subckt. A
//     full 4-port .s4p export needs four parallel Foster ladders plus
//     reciprocity handling (S21 == S12 only if the network is passive,
//     which we don't enforce in the fitting step). Tracked as a
//     follow-up against the rational-fit work.
//   * Passivity enforcement. The vector_fit pole-flipping pass guarantees
//     causal (LHP) poles. Passivity (|H(j*omega)| <= 1 across the band)
//     is a stronger constraint that real channels can violate due to
//     fitting noise; user must inspect the emitted .subckt and clamp if
//     it shows >0 dB gain anywhere.

#pragma once

#include <filesystem>
#include <string>

#include "si/Touchstone.h"
#include "si/VectorFit.h"

namespace sikit::si {

struct SpiceExportOptions {
    // Subcircuit name used in the .subckt directive. Must be a valid
    // SPICE identifier (letters, digits, underscore; no leading digit).
    std::string subckt_name = "CHANNEL";
    // Which S-parameter entry to fit. 1 = S21 (the usual transfer
    // function). 0 = S11 (reflection). For an N-port the index is
    // row + col*N column-major. Default 1 = S21 of a 2-port.
    int s_param_index = 1;
    // Pass-through to VectorFit.
    VectorFitOptions fit;
    // Include a header comment block with the fit summary (rms error,
    // pole count, iterations). Set false for machine-generated workflows
    // that want a minimal netlist.
    bool include_header = true;
};

// Generate a SPICE-3 .subckt as a string.
//
// Throws if the Touchstone has fewer than 4 * n_poles points or if
// the indexed S-parameter is missing. Emits an empty subckt if the
// vector fit fails to converge (with the rms error stamped in the
// header so the user knows).
std::string spice_subckt_from_touchstone(
    const sikit::touchstone::TouchstoneFile& channel,
    const SpiceExportOptions& opts = {});

// Same, but writes straight to disk. Returns false on IO failure.
bool write_spice_subckt(
    const sikit::touchstone::TouchstoneFile& channel,
    const std::filesystem::path& path,
    const SpiceExportOptions& opts = {});

// Lower-level: build the Foster netlist body from a pre-computed fit.
// Used by the high-level functions above and by the test suite (which
// constructs a fit synthetically to test the netlist formatting in
// isolation from the LS pipeline).
std::string spice_subckt_from_fit(
    const VectorFitResult& fit,
    const SpiceExportOptions& opts = {});

}  // namespace sikit::si
