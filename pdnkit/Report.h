// PDN signoff report.
//
// One HTML file summarising the analysis the user has just done:
// board parsed, IR drop, thermal coupling result, DRC violations,
// Z(f) sweep with target line. Hand it to the design reviewer; or
// to your future self when you come back to this board.
//
// Pure data formatting -- no Qt, no GUI. Anything that can be cheaply
// reduced to text or a small SVG fragment lands here.

#pragma once

#include <complex>
#include <optional>
#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "pi/PowerDrc.h"
#include "pi/Thermal.h"

namespace pdnkit {

struct SignoffReport {
    // Required: the parsed board the analysis ran on.
    const circuitcore::board::Board* board = nullptr;
    std::string board_path;

    // Optional: most recent IR-drop result. Empty mesh means "no IR
    // result; skip that section."
    const pi::IrMesh*   ir_mesh = nullptr;
    const pi::Solution* ir_solution = nullptr;
    std::optional<pi::ThermalResult> thermal;

    // Optional: most recent DRC report.
    std::optional<pi::DrcReport> drc;

    // Optional: most recent Z(f) sweep -- pair-parallel vectors.
    std::vector<double> zf_freqs;
    std::vector<std::complex<double>> zf_z;
    double zf_target_ohm = 0.0;
};

// Serialize the report as a single-file HTML document. Self-contained
// (inline CSS, inline SVG) so it works without any external assets.
std::string render_signoff_html(const SignoffReport& r);

// Convenience: write to file. Returns true on success.
bool write_signoff_html(const SignoffReport& r, const std::string& path);

}  // namespace pdnkit
