#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "circuitcore/formats/kicad/PcbParser.h"

using circuitcore::formats::kicad::PcbParser;
using circuitcore::formats::kicad::ParseError;

// Minimal but representative .kicad_pcb covering layers, nets, segment, via,
// zone (outline + filled), and a footprint with a pad.
constexpr auto kTinyBoard = R"(
(kicad_pcb
    (version 20240108)
    (generator "pcbnew")
    (general
        (thickness 1.6)
    )
    (layers
        (0 "F.Cu" signal)
        (31 "B.Cu" signal)
        (32 "F.SilkS" user)
    )
    (net 0 "")
    (net 1 "GND")
    (net 2 "+3V3")

    (segment
        (start 10 20) (end 30 20) (width 0.25)
        (layer "F.Cu") (net 2)
    )

    (via
        (at 25 25) (size 0.8) (drill 0.4)
        (layers "F.Cu" "B.Cu") (net 2)
    )

    (zone
        (net 1) (net_name "GND") (layer "F.Cu")
        (polygon
            (pts (xy 0 0) (xy 100 0) (xy 100 100) (xy 0 100))
        )
        (filled_polygon
            (layer "F.Cu")
            (pts (xy 5 5) (xy 95 5) (xy 95 95) (xy 5 95))
        )
    )

    (footprint "Resistor_SMD:R_0402"
        (at 40 50 0)
        (property "Reference" "R1" (at 40 48 0))
        (property "Value" "10k" (at 40 52 0))
        (pad "1" smd rect
            (at -0.5 0)
            (size 0.5 0.5)
            (layers "F.Cu")
            (net 2)
        )
        (pad "2" smd rect
            (at 0.5 0)
            (size 0.5 0.5)
            (layers "F.Cu")
            (net 1)
        )
    )
)
)";

TEST_CASE("parser: top-level requires kicad_pcb", "[parser]") {
    REQUIRE_FALSE(PcbParser::parse_string("(other)").has_value());
}

TEST_CASE("parser: parses stackup and thickness", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.stackup.layers.size() == 3);
    REQUIRE(b.stackup.layers[0].name == "F.Cu");
    REQUIRE(b.stackup.layers[1].ordinal == 31);
    REQUIRE(b.stackup.layers[2].type == "user");
    REQUIRE(b.stackup.total_thickness == 1.6e-3);
    REQUIRE(b.find_layer(0)->is_copper());
    REQUIRE_FALSE(b.find_layer(32)->is_copper());
}

TEST_CASE("parser: parses nets", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.nets.size() == 3);
    REQUIRE(b.find_net(0)->name.empty());
    REQUIRE(b.find_net(1)->name == "GND");
    REQUIRE(b.find_net_by_name("+3V3")->id == 2);
}

TEST_CASE("parser: parses segment with mm-to-m conversion", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.segments.size() == 1);
    const auto& s = b.segments[0];
    REQUIRE(s.start.x == 10e-3);
    REQUIRE(s.start.y == 20e-3);
    REQUIRE(s.end.x == 30e-3);
    REQUIRE(s.width == 0.25e-3);
    REQUIRE(s.layer_ordinal == 0);
    REQUIRE(s.net_id == 2);
}

TEST_CASE("parser: parses via with from/to layer ordinals", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.vias.size() == 1);
    const auto& v = b.vias[0];
    REQUIRE(v.at.x == 25e-3);
    REQUIRE(v.outer_diameter == 0.8e-3);
    REQUIRE(v.drill == 0.4e-3);
    REQUIRE(v.from_layer == 0);
    REQUIRE(v.to_layer == 31);
    REQUIRE(v.net_id == 2);
}

TEST_CASE("parser: parses zone with outline and filled polygons", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.zones.size() == 1);
    const auto& z = b.zones[0];
    REQUIRE(z.net_id == 1);
    REQUIRE(z.net_name == "GND");
    REQUIRE(z.layer_ordinal == 0);
    REQUIRE(z.outline.outline.size() == 4);
    REQUIRE(z.outline.outline[2].x == 100e-3);
    REQUIRE(z.filled.size() == 1);
    REQUIRE(z.filled[0].outline.size() == 4);
    REQUIRE(z.filled[0].outline[0].x == 5e-3);
}

TEST_CASE("parser: footprint pads transformed by footprint origin", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.pads.size() == 2);
    // Footprint origin (40, 50), rotation 0°.
    // Pad 1 local (-0.5, 0) → world (39.5, 50).
    REQUIRE(b.pads[0].at.x == (40 - 0.5) * 1e-3);
    REQUIRE(b.pads[0].at.y == 50e-3);
    REQUIRE(b.pads[0].net_id == 2);
    REQUIRE(b.pads[1].at.x == (40 + 0.5) * 1e-3);
    REQUIRE(b.pads[1].net_id == 1);
    REQUIRE(b.pads[0].layer_ordinals.size() == 1);
    REQUIRE(b.pads[0].layer_ordinals[0] == 0);
    // Pad name extracted from (pad "1" ...) form.
    REQUIRE(b.pads[0].name == "1");
    REQUIRE(b.pads[1].name == "2");
}

TEST_CASE("parser: unknown layer name in segment is an error", "[parser]") {
    constexpr auto bad = R"(
        (kicad_pcb
            (layers (0 "F.Cu" signal))
            (segment (start 0 0) (end 1 1) (width 0.25) (layer "Mystery") (net 0))
        )
    )";
    REQUIRE_FALSE(PcbParser::parse_string(bad).has_value());
}

TEST_CASE("parser: pad shape and size extracted", "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.pads.size() == 2);
    // kTinyBoard pads are (pad "N" smd rect (size 0.5 0.5) ...).
    REQUIRE(b.pads[0].shape == circuitcore::board::PadShape::Rect);
    REQUIRE(b.pads[0].size.x == 0.0005);
    REQUIRE(b.pads[0].size.y == 0.0005);
    REQUIRE(b.pads[1].shape == circuitcore::board::PadShape::Rect);
}

TEST_CASE("parser: parent_ref populated from footprint Reference property",
          "[parser]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.pads.size() == 2);
    REQUIRE(b.pads[0].parent_ref == "R1");
    REQUIRE(b.pads[1].parent_ref == "R1");
}

// Fixture for the GraphicItem parser: one of each shape on silk + mask
// + courtyard layers, plus one footprint-local fp_line that has to be
// transformed by the footprint's (at 40 50 0) position.
constexpr auto kGraphicsBoard = R"(
(kicad_pcb
    (version 20240108) (generator "pcbnew")
    (general (thickness 1.6))
    (layers
        (0 "F.Cu" signal)
        (31 "B.Cu" signal)
        (36 "B.SilkS" user)
        (37 "F.SilkS" user)
        (38 "B.Mask"  user)
        (39 "F.Mask"  user)
        (48 "B.CrtYd" user)
        (49 "F.CrtYd" user)
    )
    (gr_line  (start 0 0) (end 10 0)  (layer "F.SilkS") (stroke (width 0.15)))
    (gr_arc   (start 0 0) (mid 5 5) (end 10 0) (layer "F.SilkS") (stroke (width 0.15)))
    (gr_circle (center 5 5) (end 7 5) (layer "F.SilkS") (stroke (width 0.15)))
    (gr_poly  (pts (xy 0 0) (xy 10 0) (xy 5 10)) (layer "F.Mask") (width 0))
    (gr_text "HELLO" (at 5 5 0) (layer "F.SilkS")
        (effects (font (size 1 1))))
    (footprint "Test:FP"
        (at 40 50 0)
        (property "Reference" "U1" (at 40 50 0))
        (fp_line (start 0 0) (end 1 0) (layer "F.CrtYd")
                 (stroke (width 0.05)))
    )
)
)";
TEST_CASE("parser: graphic items on non-copper layers", "[parser][graphics]") {
    auto b = PcbParser::parse_string(kGraphicsBoard).value();
    REQUIRE(b.graphics.size() >= 6);  // 5 board-level + 1 fp_line

    int line = 0, arc = 0, circ = 0, poly = 0, text = 0;
    bool found_fp_line = false;
    for (const auto& g : b.graphics) {
        using K = circuitcore::board::GraphicItem::Kind;
        switch (g.kind) {
            case K::Line:    ++line;    break;
            case K::Arc:     ++arc;     break;
            case K::Circle:  ++circ;    break;
            case K::Polygon: ++poly;    break;
            case K::Text:    ++text;    break;
        }
        // The fp_line is on F.CrtYd (ordinal 49) and starts at the
        // footprint's (40, 50) origin -- if the transform worked.
        if (g.kind == K::Line && g.layer_ordinal == 49) {
            REQUIRE(g.points.size() == 2);
            REQUIRE(std::abs(g.points[0].x - 0.040) < 1e-9);
            REQUIRE(std::abs(g.points[0].y - 0.050) < 1e-9);
            REQUIRE(std::abs(g.points[1].x - 0.041) < 1e-9);
            REQUIRE(std::abs(g.points[1].y - 0.050) < 1e-9);
            found_fp_line = true;
        }
    }
    REQUIRE(line >= 2);  // gr_line + fp_line
    REQUIRE(arc  >= 1);
    REQUIRE(circ >= 1);
    REQUIRE(poly >= 1);
    REQUIRE(text >= 1);
    REQUIRE(found_fp_line);
}

TEST_CASE("parser: graphic items skip Edge.Cuts and copper layers",
          "[parser][graphics]") {
    // Edge.Cuts gr_line still ends up in the outline list, not graphics.
    auto b = PcbParser::parse_string(kGraphicsBoard).value();
    for (const auto& g : b.graphics) {
        const auto* L = b.find_layer(g.layer_ordinal);
        REQUIRE(L != nullptr);
        REQUIRE_FALSE(L->is_copper());
        REQUIRE(L->name != "Edge.Cuts");
    }
}

TEST_CASE("parser: emits Component per footprint", "[parser][components]") {
    auto b = PcbParser::parse_string(kTinyBoard).value();
    REQUIRE(b.components.size() == 1);
    const auto& c = b.components[0];
    REQUIRE(c.name      == "Resistor_SMD:R_0402");
    REQUIRE(c.reference == "R1");
    REQUIRE(c.value     == "10k");
    REQUIRE(c.at.x == 40e-3);
    REQUIRE(c.at.y == 50e-3);
    REQUIRE(c.rotation == 0.0);
    // No courtyard primitives on kTinyBoard's footprint -> bbox stays
    // at the (0,0)/(0,0) default and downstream code should fall back
    // to the pad bbox.
    REQUIRE(c.courtyard_lo.x == 0.0);
    REQUIRE(c.courtyard_hi.x == 0.0);
}

TEST_CASE("parser: component courtyard bbox from fp_line on F.CrtYd",
          "[parser][components]") {
    auto b = PcbParser::parse_string(kGraphicsBoard).value();
    REQUIRE(b.components.size() == 1);
    const auto& c = b.components[0];
    REQUIRE(c.reference == "U1");
    // fp_line is (start 0 0) (end 1 0) on F.CrtYd, footprint at (40,50).
    // Transformed: world endpoints (40, 50) and (41, 50). With no other
    // primitives the bbox should collapse to that line's extent.
    REQUIRE(std::abs(c.courtyard_lo.x - 0.040) < 1e-9);
    REQUIRE(std::abs(c.courtyard_lo.y - 0.050) < 1e-9);
    REQUIRE(std::abs(c.courtyard_hi.x - 0.041) < 1e-9);
    REQUIRE(std::abs(c.courtyard_hi.y - 0.050) < 1e-9);
}
