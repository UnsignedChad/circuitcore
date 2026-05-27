// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Per-trace impedance analysis with a swappable solver engine.
//
//   Engine::ClosedForm   IPC-2141A formulas (fast, ±5–10%).
//   Engine::Fdm          In-house 2D finite-difference solver (slower,
//                        captures real stackup, ±10–15% at v0 mesh density).

#pragma once

#include <cstddef>
#include <vector>

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"

#include "si/SurfaceRoughness.h"

namespace sikit::analysis {

enum class Engine {
    ClosedForm,
    Fdm,
};

struct AnalysisStackup {
    double outer_dielectric_height = 0.2e-3;
    double inner_plane_separation = 0.4e-3;
    double copper_thickness = 35e-6;
    double epsilon_r = 4.4;

    // Loss parameters (default FR-4 / 1oz copper).
    double tan_delta     = 0.02;        // dielectric loss tangent
    double sigma_copper  = 5.8e7;       // S/m (annealed copper)

    // Copper surface roughness model. Defaults to None (smooth) so v0
    // behavior is preserved; callers opt in for high-frequency accuracy.
    RoughnessSpec roughness;

    bool   from_real_stackup = false;

    // Build from the canonical board PLUS the sikit-side dielectric
    // items list (parsed separately via si::load_si_stackup).
    static AnalysisStackup from_board(const circuitcore::board::Board& b,
                                       const sikit::si::SiStackup& s);
};

struct SegmentImpedance {
    std::size_t segment_index = 0;
    int layer_ordinal = 0;
    int net_id = 0;
    double trace_width = 0.0;
    double z0 = 0.0;
    double v_phase = 0.0;     // m/s — phase velocity along the trace
    double eps_eff = 0.0;     // effective relative permittivity
    bool in_valid_range = true;
};

SegmentImpedance compute_one(double trace_width,
                             int layer_ordinal,
                             const AnalysisStackup& s);

SegmentImpedance compute_one_fdm(double trace_width,
                                  int layer_ordinal,
                                  const AnalysisStackup& s);

double compute_diff_z0_fdm(double trace_width,
                            double spacing,
                            int layer_ordinal,
                            const AnalysisStackup& s);

// Closed-form differential impedance: single-ended Z₀ via Wadell, then
// the standard edge-coupled correction (microstrip or stripline form
// depending on layer).
double compute_diff_z0_closed_form(double trace_width,
                                    double spacing,
                                    int layer_ordinal,
                                    const AnalysisStackup& s);

// Per-pair result for a detected diff pair on the board.
struct DiffPairImpedance {
    int net_p_id = -1;
    int net_n_id = -1;
    std::string base_name;       // shared stem from the pair detector
    double trace_width = 0.0;    // representative (median of F.Cu segments)
    double spacing = 0.0;        // edge-to-edge gap used in the calculation
    int    layer_ordinal = 0;
    double z_diff = 0.0;         // Ω
    // Indices into board.segments belonging to either net of this pair.
    std::vector<std::size_t> segment_indices;
};

// Detect diff pairs in `board` and compute differential impedance for
// each. Representative width is the median F.Cu segment width on the
// positive net; spacing defaults to that width (≈ 1:1 routing) since
// the parser doesn't yet extract the actual routed gap. Engine selects
// closed-form (fast) or FDM (slower, captures stackup nuances).
std::vector<DiffPairImpedance> compute_diff_pairs(
    const circuitcore::board::Board& board,
    const AnalysisStackup& s,
    Engine engine = Engine::ClosedForm);

std::vector<SegmentImpedance> compute_all(const circuitcore::board::Board& board,
                                          const AnalysisStackup& s,
                                          Engine engine = Engine::ClosedForm);

struct ImpedanceColor {
    float r, g, b, a;
};

ImpedanceColor color_for_error(double z0_ohms, double target_ohms);

}  // namespace sikit::analysis
