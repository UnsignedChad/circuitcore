// Regular-grid mesher for static IR-drop analysis.
//
// Takes the filled copper polygons of a single (layer, net) pair and produces
// a 2D grid mesh where each grid cell that lies inside the copper becomes a
// node, and each pair of adjacent in-copper nodes becomes a resistor.
//
// For square cells, the resistance between adjacent cell centers reduces to
// the sheet resistance ρ/t (Ω per square), independent of grid spacing —
// see Wadell, "Transmission Line Design Handbook," §3.2 for the derivation.
// So conductance per resistor: G = t / ρ  (Siemens).
//
// Source/sink nodes are taken from pads on (layer, net): leftmost pad → source,
// rightmost pad → sink. The full per-pad current-source UI lands in a later
// commit; this is the minimal viable IR-drop input.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "circuitcore/board/Board.h"

namespace pdnkit::pi {

struct MeshConfig {
    double cell_size = 0.5e-3;          // grid spacing in meters (default 0.5 mm)
    double copper_thickness = 35.0e-6;  // 1 oz copper (35 µm)
    double copper_rho = 1.68e-8;        // copper resistivity Ω·m at 20°C
    int net_id = 0;                     // target net (e.g., a power rail)
    int layer_ordinal = 0;              // primary copper layer
    // Additional copper layers to include in the mesh. Vias on the target
    // net that connect any pair of meshed layers add a via-resistor
    // (R = rho * board_thickness / cross_section). Empty == single-layer.
    std::vector<int> extra_layer_ordinals;

    // If the requested layer_ordinal has no filled zones for net_id, search
    // every copper layer and switch to the one with the largest zone area
    // before meshing. Default ON -- the user usually wants an analysis, not
    // an empty mesh. Set false to enforce strict layer selection.
    bool auto_select_layer = true;

    // Optional explicit source/sink pad selection by pad name. When non-empty,
    // these override the v0 leftmost/rightmost auto-pick. Pads must match
    // (net_id, layer_ordinal) and have the given name.
    std::vector<std::string> source_pad_names;
    std::vector<std::string> sink_pad_names;

    // Picking a *specific* pad (not all pads with a given designator). Used
    // by the GUI right-click probe-R workflow, where you click two pads and
    // want exactly those two, not every "pad 1" on the same net. Indices
    // are into Board::pads. Empty -> fall back to name-based picking above.
    std::vector<int> source_pad_indices;
    std::vector<int> sink_pad_indices;

    // Optional per-pad current map (key = pad name, value = Amperes;
    // + injects, - draws). When non-empty, overrides BOTH source/sink lists
    // and the solver default split-over-source behavior -- the solver builds
    // its RHS directly from these. Sum must be ~0 (current conservation).
    std::unordered_map<std::string, double> pad_currents;
};

struct Node {
    int id = 0;
    double x = 0.0;
    double y = 0.0;
    int grid_i = 0;        // column in this layer's grid
    int grid_j = 0;        // row in this layer's grid
    int layer_ordinal = 0; // which copper layer this node sits on
};

struct Resistor {
    int from_node = 0;
    int to_node = 0;
    double conductance = 0.0;  // Siemens
};

struct IrMesh {
    std::vector<Node> nodes;
    std::vector<Resistor> resistors;

    // v0 source/sink lists. Solver uses these when node_currents is empty:
    // total_current is split across sources, sinks are pinned to 0 V.
    std::vector<int> source_node_ids;
    std::vector<int> sink_node_ids;

    // Per-node explicit currents (Amperes; + injects, - draws). When
    // non-empty, the solver uses this directly and ignores source/sink
    // lists / SolveConfig::total_current. Populated by the mesher when
    // MeshConfig::pad_currents is non-empty.
    std::vector<std::pair<int, double>> node_currents;

    // World-space bbox of the meshed copper (handy for renderers).
    double bbox_lo_x = 0.0, bbox_lo_y = 0.0;
    double bbox_hi_x = 0.0, bbox_hi_y = 0.0;

    // The primary copper layer that the mesher actually built on. Equals
    // MeshConfig::layer_ordinal in the normal case; differs when
    // auto_select_layer switched to a different layer with more copper.
    // -1 if the mesh is empty.
    int primary_layer_used = -1;
};

class IrMesher {
public:
    // Build the grid mesh. Returns an empty mesh if the (net, layer) pair has
    // no zone fill.
    static IrMesh build(const circuitcore::board::Board& board, const MeshConfig& cfg);
};

}  // namespace pdnkit::pi
