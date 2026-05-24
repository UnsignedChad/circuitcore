// 2D PCB cross-section data model for the in-house field solver.
//
// Coordinate convention:
//   y is horizontal (across the board, in-plane)
//   z is vertical  (through the board's thickness)
//   z grows downward; z = 0 is the top of the topmost dielectric / conductor
//
// A cross-section is a horizontal strip described by (a) a vertical stack
// of dielectric layers and (b) a list of rectangular conductors placed at
// known (y, z) positions. The bottom plane z = sum(thicknesses) is the
// implicit reference ground; an explicit ground plane can be added as a
// thin conductor if a more accurate field treatment is desired.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sikit::em2d {

struct DielectricLayer {
    double thickness = 0.0;  // m
    double epsilon_r = 1.0;
    double loss_tangent = 0.0;
    std::string material;
};

// Rectangular conductor specified by its top-left corner (y, z_top) and
// extent (width, thickness). The conductor occupies y ∈ [y_center − w/2,
// y_center + w/2] and z ∈ [z_top, z_top + thickness].
struct Conductor {
    int id = 0;          // user-assigned (used to index the capacitance matrix)
    std::string label;   // optional human name
    double y_center = 0.0;
    double z_top = 0.0;
    double width = 0.0;
    double thickness = 0.0;
    double voltage = 0.0;  // Dirichlet BC for the solver
};

struct CrossSection {
    std::vector<DielectricLayer> stack;   // top-to-bottom
    std::vector<Conductor> conductors;

    // Lateral extent of the analysis region. The horizontal walls are
    // treated as Neumann (∂V/∂n = 0) boundaries; pick this wide enough
    // that field rolls off to ~0 before reaching them (rule of thumb:
    // 5–10× the trace width or stack thickness, whichever is larger).
    double y_min = -5e-3;
    double y_max =  5e-3;

    // Extra air space above the topmost conductor — needed because the
    // dielectric stack stops at the board surface but field extends into
    // air above. Default 3× board thickness gives clean rolloff.
    double air_above = 3e-3;

    // Total board thickness derived from the dielectric stack.
    double board_thickness() const {
        double t = 0.0;
        for (const auto& d : stack) t += d.thickness;
        return t;
    }

    // Lookup ε_r at world position (y, z). Returns 1.0 for air, the layer
    // value when inside the dielectric stack.
    double epsilon_r_at(double y, double z) const;
};

}  // namespace sikit::em2d
