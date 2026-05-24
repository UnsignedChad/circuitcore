// GL-ready quad mesh for an IR-drop solution.
//
// One small square per IrMesh node, sized to the mesher's cell_size and
// colored by per-vertex voltage. Per-vertex voltage is normalized to [0,1]
// (relative to the solution's min/max) so the fragment shader can index a
// fixed colormap without needing extra uniforms.

#pragma once

#include <cstdint>
#include <vector>

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

namespace pdnkit::render {

struct IrResultMesh {
    // Interleaved per-vertex: x, y, t  (t = voltage normalized to [0, 1]).
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    // Raw voltage range used for the normalization, for legend / status bar.
    double v_min = 0.0;
    double v_max = 0.0;

    // Index-range per copper layer so the renderer can skip hidden layers.
    // Layers appear in first-seen order from the source IrMesh.
    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };
    std::vector<LayerRange> layer_ranges;

    // Source/sink markers shown over the heat-map.
    struct Marker {
        double x = 0.0;
        double y = 0.0;
        double current = 0.0;  // >0 source, <0 sink
    };
    std::vector<Marker> markers;

    // Worst-case node, populated by the builders. Voltage view: the node
    // with the lowest V (largest IR drop from source). Current-density
    // view: the node with the highest |J| (bottleneck). The canvas
    // renders a yellow ring + label here so the user sees the problem
    // spot at a glance.
    struct Hotspot {
        bool valid = false;
        double x = 0.0;
        double y = 0.0;
        double value = 0.0;       // V (voltage view) or A/m (J view)
        bool is_current = false;  // true: A/m; false: V
    };
    Hotspot hotspot;

    std::size_t vertex_count() const noexcept { return vertices.size() / 3; }
};

// Build a colored quad per node. cell_size in meters (matches the mesher's
// MeshConfig::cell_size). Empty result if the solution is not ok.
IrResultMesh build_ir_result_mesh(const pi::IrMesh& mesh,
                                   const pi::Solution& solution,
                                   double cell_size);

// Build a current-density |J| heat-map from an IR-drop solution. Per node,
// |J|_sheet = t * |grad V| / rho_cu  (units A/m, sheet current density --
// the natural quantity for PCB current crowding). Central-difference in
// (i,j) grid space where neighbors exist; one-sided at the boundaries.
// The returned IrResultMesh's v_min/v_max hold the |J| range (A/m) so
// callers can label the legend appropriately. cell_size, copper_thickness,
// and rho_cu must be in SI units (meters, ohm*m).
IrResultMesh build_current_density_mesh(const pi::IrMesh& mesh,
                                         const pi::Solution& solution,
                                         double cell_size,
                                         double copper_thickness,
                                         double rho_cu);

// Build a colored grid mesh from a flat magnitudes array (row-major,
// nx*ny). origin_x/y is the world position of the lower-left corner; the
// grid covers (origin_x, origin_y) to (origin_x + nx*dx, origin_y + ny*dy).
IrResultMesh build_grid_mesh(const std::vector<double>& mags,
                              int nx, int ny,
                              double dx, double dy,
                              double origin_x, double origin_y);

}  // namespace pdnkit::render
