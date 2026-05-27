// Board model: in-memory representation of a KiCad PCB used by all of pdnkit.
//
// Units are SI throughout (meters, radians). KiCad's .kicad_pcb stores
// coordinates in millimeters; the parser converts at load time so downstream
// code (mesher, solver, renderer) never deals with unit mixups.

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace circuitcore::board {

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct Layer {
    int ordinal = 0;           // KiCad layer ID (0=F.Cu, 31=B.Cu, etc.)
    std::string name;          // 'F.Cu', 'In1.Cu', 'B.Cu'
    std::string type;          // 'signal', 'power', 'mixed', 'jumper', 'user'
    // Per-layer stackup data extracted from (setup (stackup ...)), when
    // present. Zero/empty means the KiCad file did not specify it -- caller
    // should fall back to a sensible default (35um copper, FR-4 eps_r 4.3).
    double thickness = 0.0;    // meters
    std::string material;      // e.g. 'copper', 'FR4', 'prepreg'
    double epsilon_r = 0.0;    // dielectric only
    double loss_tangent = 0.0; // dielectric only

    bool is_copper() const noexcept {
        // KiCad copper layer types: signal / power / mixed / jumper.
        // Using type rather than ordinal range -- v10 reshuffled the layer
        // numbering so the old ordinal<=31 heuristic falsely matched mask
        // and silkscreen.
        return type == "signal" || type == "power" ||
               type == "mixed"  || type == "jumper";
    }
};

struct Stackup {
    std::vector<Layer> layers;
    double total_thickness = 1.6e-3;  // meters; default 1.6mm board
};

struct Net {
    int id = 0;
    std::string name;
};

// Track segment (a copper wire on one layer).
struct Segment {
    Point2 start;
    Point2 end;
    double width = 0.0;        // meters
    int layer_ordinal = 0;
    int net_id = 0;
};

// Through-hole, blind, or buried via.
struct Via {
    Point2 at;
    double outer_diameter = 0.0;  // pad diameter (m)
    double drill = 0.0;           // hole diameter (m)
    int from_layer = 0;
    int to_layer = 0;
    int net_id = 0;
};

enum class PadShape {
    Circle,      // (drill diameter for through-hole, size.x for SMD round)
    Rect,        // size = (width, height)
    Oval,        // rounded rectangle approximated as Rect for v0
    RoundRect,   // rounded rectangle approximated as Rect for v0
    Custom,      // not supported v0 — falls back to Rect with size
};

// Component pad. Size is post-mm-to-m converted; defaults are 0 meaning
// "unknown" (the renderer falls back to a small disk).
struct Pad {
    Point2 at;
    double rotation = 0.0;     // radians
    Point2 size{0.0, 0.0};     // width, height in meters
    PadShape shape = PadShape::Circle;
    std::vector<int> layer_ordinals;
    int net_id = 0;
    std::string name;          // KiCad pad designator ("1", "+", etc.)
    std::string parent_ref;    // footprint reference ("C12", "U1", ...)
};

// One footprint placement on the board. Carries identifier + position
// + courtyard bounding box so thermal / mechanical solvers can treat
// the component as an extruded body and the GUI can label hotspots
// by reference designator. Pads belonging to the footprint live in
// Board::pads with parent_ref pointing at this Component::reference.
struct Component {
    std::string name;        // footprint library id ("Connector:PinHeader_1x02")
    std::string reference;   // designator on the board ("R1", "U3", "C12")
    std::string value;       // value text ("10K", "TPS5430DDA", "+3V3")
    Point2 at;               // origin in world coords (m)
    double rotation = 0.0;   // radians, CCW
    // Axis-aligned courtyard bounding box in world coords. Both points
    // (0, 0) means the footprint defined no courtyard (or our parser
    // could not find one); in that case downstream consumers should
    // fall back to the pad bounding box.
    Point2 courtyard_lo;
    Point2 courtyard_hi;
    // Optional per-component metadata supplied by the user (or by a
    // future analogkit). Zero means unknown / not specified.
    double dissipated_power_w = 0.0;
    double body_height_m      = 0.0;   // populated by a per-package-family lookup later
    double mass_kg            = 0.0;
};

// A closed polygon, possibly with holes (interior cutouts).
struct Polygon {
    std::vector<Point2> outline;
    std::vector<std::vector<Point2>> holes;
};

// Copper pour. The 'outline' is the user-drawn boundary; 'filled' is the
// post-processed copper after thermal reliefs and clearance subtraction.
// PI analysis meshes 'filled' (that's the actual conductor).
struct Zone {
    int net_id = 0;
    std::string net_name;
    int layer_ordinal = 0;
    Polygon outline;
    std::vector<Polygon> filled;
};

// Line segment on the board outline (Edge.Cuts layer). Arcs are converted
// to polylines at parse time so the renderer only needs straight segments.
struct OutlineSegment {
    Point2 start;
    Point2 end;
};

// Non-copper graphical item parsed from KiCad's gr_line / gr_arc /
// gr_circle / gr_poly / fp_text and their fp_* footprint-local twins.
// Used by the renderer for solder mask, silkscreen, courtyard, and
// fab-layer overlays. Footprint-local items have already been
// transformed by the footprint position + rotation at parse time, so
// `points` is always in board coordinates.
//
// Geometry is pre-tessellated for arcs and circles (matches what the
// existing outline parser does). Polygons keep their outline as-is so
// the renderer can fill them with the same earcut pipeline that zones
// use.
struct GraphicItem {
    enum class Kind {
        Line,     // points = [start, end]
        Arc,      // points = polyline (~24 segments) approximating the arc
        Circle,   // points = closed 48-gon outline
        Polygon,  // points = outline (open or closed -- renderer wraps)
        Text,     // points = [position]; see `text`, `text_angle`, `text_size`
    };
    Kind kind = Kind::Line;
    int layer_ordinal = 0;
    double stroke_width = 0.0;     // m, 0 = use default thin line
    std::vector<Point2> points;
    std::string text;              // Text only
    double text_angle = 0.0;       // radians, Text only
    double text_size  = 0.0;       // m, Text only (vertical glyph size)
};


struct Board {
    Stackup stackup;
    std::vector<Net> nets;
    std::vector<Segment> segments;
    std::vector<Via> vias;
    std::vector<Pad> pads;
    std::vector<Component> components;
    std::vector<Zone> zones;
    std::vector<OutlineSegment> outline;
    std::vector<GraphicItem> graphics;

    const Net* find_net(int id) const noexcept {
        auto it = std::find_if(nets.begin(), nets.end(),
                               [id](const Net& n) { return n.id == id; });
        return it == nets.end() ? nullptr : &*it;
    }

    const Net* find_net_by_name(std::string_view name) const noexcept {
        auto it = std::find_if(nets.begin(), nets.end(),
                               [name](const Net& n) { return n.name == name; });
        return it == nets.end() ? nullptr : &*it;
    }

    const Layer* find_layer(int ordinal) const noexcept {
        auto it = std::find_if(stackup.layers.begin(), stackup.layers.end(),
                               [ordinal](const Layer& l) { return l.ordinal == ordinal; });
        return it == stackup.layers.end() ? nullptr : &*it;
    }
};

}  // namespace circuitcore::board
