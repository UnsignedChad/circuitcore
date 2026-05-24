#include <cmath>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "pi/IrMesher.h"

using pdnkit::pi::IrMesher;
using pdnkit::pi::IrMesh;
using pdnkit::pi::MeshConfig;
using namespace circuitcore::board;
using Catch::Approx;

namespace {

Board with_square_zone(int net, int layer, double side) {
    Board b;
    b.stackup.layers.push_back({layer, "F.Cu", "signal"});
    b.nets.push_back({net, "VCC"});
    Zone z;
    z.net_id = net;
    z.layer_ordinal = layer;
    Polygon p;
    p.outline = {{0, 0}, {side, 0}, {side, side}, {0, side}};
    z.filled.push_back(p);
    b.zones.push_back(z);
    return b;
}

}  // namespace

TEST_CASE("mesher: no matching geometry returns empty mesh", "[irmesh]") {
    Board b;
    MeshConfig cfg;
    cfg.net_id = 99;
    cfg.layer_ordinal = 0;
    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.nodes.empty());
    REQUIRE(m.resistors.empty());
}

TEST_CASE("mesher: 10mm square zone with 1mm cells → ~100 nodes", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);  // 10mm × 10mm
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    // 10mm / 1mm = 10 cells per axis → 10*10 = 100 nodes.
    REQUIRE(m.nodes.size() == 100);
    // Edge resistors: 2 * 10 * 9 = 180 (horizontal + vertical interior bonds).
    REQUIRE(m.resistors.size() == 180);
}

TEST_CASE("mesher: invalid config returns empty mesh", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.cell_size = 0.0;  // invalid
    REQUIRE(IrMesher::build(b, cfg).nodes.empty());
}

TEST_CASE("mesher: per-resistor conductance = t/ρ for square cells", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.copper_thickness = 35.0e-6;
    cfg.copper_rho = 1.68e-8;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    REQUIRE_FALSE(m.resistors.empty());
    const double expected = 35.0e-6 / 1.68e-8;  // ≈ 2083 S
    REQUIRE(m.resistors.front().conductance == Approx(expected));
}

TEST_CASE("mesher: mesh respects zone hole (no nodes inside)", "[irmesh]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VCC"});
    Zone z;
    z.net_id = 1;
    z.layer_ordinal = 0;
    Polygon p;
    p.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
    p.holes.push_back({{0.003, 0.003}, {0.007, 0.003}, {0.007, 0.007}, {0.003, 0.007}});
    z.filled.push_back(p);
    b.zones.push_back(z);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    // 100 - 16 (4x4 hole cells) = 84 expected nodes.
    REQUIRE(m.nodes.size() == 84);
}

TEST_CASE("mesher: leftmost and rightmost pads become source and sink", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    // Two pads on the target net+layer at opposite ends.
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 1; p1.layer_ordinals = {0};
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {0};
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.source_node_ids.size() == 1);
    REQUIRE(m.sink_node_ids.size() == 1);

    const auto& src = m.nodes[m.source_node_ids[0]];
    const auto& snk = m.nodes[m.sink_node_ids[0]];
    REQUIRE(src.x < snk.x);  // leftmost pad → leftmost node
}

TEST_CASE("mesher: pad on wrong net or layer is ignored", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 2 /* wrong net */; p1.layer_ordinals = {0};
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {31} /* wrong layer */;
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.source_node_ids.empty());
    REQUIRE(m.sink_node_ids.empty());
}

TEST_CASE("mesher: bbox matches the source polygon bbox", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.bbox_lo_x == Approx(0.0));
    REQUIRE(m.bbox_lo_y == Approx(0.0));
    REQUIRE(m.bbox_hi_x == Approx(0.010));
    REQUIRE(m.bbox_hi_y == Approx(0.010));
}

TEST_CASE("mesher: explicit source/sink pad names override auto-pick", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    // Three pads — auto-pick would pick A (leftmost) and C (rightmost).
    // Explicit config should pick B and C instead.
    Pad pa; pa.at = {0.001, 0.005}; pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "A";
    Pad pb; pb.at = {0.005, 0.005}; pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "B";
    Pad pc; pc.at = {0.009, 0.005}; pc.net_id = 1; pc.layer_ordinals = {0}; pc.name = "C";
    b.pads.push_back(pa);
    b.pads.push_back(pb);
    b.pads.push_back(pc);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.source_pad_names = {"B"};
    cfg.sink_pad_names = {"C"};

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.source_node_ids.size() == 1);
    REQUIRE(m.sink_node_ids.size() == 1);

    const auto& src = m.nodes[m.source_node_ids[0]];
    const auto& snk = m.nodes[m.sink_node_ids[0]];
    // Source picked is the one nearest pad B (x ~= 0.005), not pad A
    // (x = 0.001) — the grid snaps to the nearest cell center, which is
    // 0.0045 or 0.0055 depending on tie-break.
    REQUIRE(std::abs(src.x - 0.005) < 0.001);
    REQUIRE(std::abs(snk.x - 0.009) < 0.001);
}

TEST_CASE("mesher: missing-name explicit list falls back to auto-pick", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 1; p1.layer_ordinals = {0}; p1.name = "L";
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {0}; p2.name = "R";
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.source_pad_names = {"DoesNotExist"};  // explicit but no match

    auto m = IrMesher::build(b, cfg);
    // source auto-fills to leftmost (L); sink also auto-fills to rightmost (R).
    REQUIRE(m.source_node_ids.size() == 1);
    REQUIRE(m.sink_node_ids.size() == 1);
}

TEST_CASE("mesher: explicit pad_currents populate node_currents", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 1; p1.layer_ordinals = {0}; p1.name = "A";
    Pad p2; p2.at = {0.005, 0.005}; p2.net_id = 1; p2.layer_ordinals = {0}; p2.name = "B";
    Pad p3; p3.at = {0.009, 0.005}; p3.net_id = 1; p3.layer_ordinals = {0}; p3.name = "C";
    b.pads.push_back(p1);
    b.pads.push_back(p2);
    b.pads.push_back(p3);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.pad_currents = {{"A", 2.0}, {"B", -0.5}, {"C", -1.5}};

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.node_currents.size() == 3);
    double sum = 0;
    for (auto& [_, c] : m.node_currents) sum += c;
    REQUIRE(sum == Approx(0.0).margin(1e-9));
}

TEST_CASE("mesher: multi-layer meshes both layers and wires via resistors", "[irmesh]") {
    Board b;
    b.stackup.total_thickness = 1.6e-3;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});
    b.nets.push_back({1, "GND"});

    // Same square zone on both layers.
    for (int ord : {0, 31}) {
        Zone z;
        z.net_id = 1;
        z.layer_ordinal = ord;
        Polygon p;
        p.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
        z.filled.push_back(p);
        b.zones.push_back(z);
    }

    // One through-via on GND connecting the two layers.
    Via v;
    v.at = {0.005, 0.005};
    v.outer_diameter = 0.8e-3;
    v.drill = 0.4e-3;
    v.from_layer = 0;
    v.to_layer = 31;
    v.net_id = 1;
    b.vias.push_back(v);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.extra_layer_ordinals = {31};

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.nodes.size() == 200);  // 100 per layer

    // Each layer has 180 sheet resistors. Plus one via resistor.
    REQUIRE(m.resistors.size() == 180 + 180 + 1);

    // Nodes carry their layer ordinal.
    int f_count = 0, b_count = 0;
    for (const auto& n : m.nodes) {
        if (n.layer_ordinal == 0) ++f_count;
        if (n.layer_ordinal == 31) ++b_count;
    }
    REQUIRE(f_count == 100);
    REQUIRE(b_count == 100);
}

TEST_CASE("mesher: extra_layer_ordinals duplicate of primary is harmless", "[irmesh]") {
    Board b = with_square_zone(1, 0, 0.010);
    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.extra_layer_ordinals = {0, 0, 0};  // duplicates of primary

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.nodes.size() == 100);  // not 400
}

TEST_CASE("mesher: prune drops disconnected component without source/sink", "[irmesh][connectivity]") {
    // Two square zones, same net+layer but spatially separated so the mesher
    // builds them as disconnected components. Pads only on the LEFT square.
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VRAIL"});

    Zone z1; z1.net_id = 1; z1.layer_ordinal = 0;
    Polygon p1; p1.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
    z1.filled.push_back(p1); b.zones.push_back(z1);

    Zone z2; z2.net_id = 1; z2.layer_ordinal = 0;
    Polygon p2; p2.outline = {{0.030, 0}, {0.040, 0}, {0.040, 0.010}, {0.030, 0.010}};
    z2.filled.push_back(p2); b.zones.push_back(z2);

    Pad pa; pa.at = {0.001, 0.005}; pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "A";
    Pad pb; pb.at = {0.009, 0.005}; pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "B";
    b.pads.push_back(pa); b.pads.push_back(pb);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.source_pad_names = {"A"};
    cfg.sink_pad_names   = {"B"};

    auto m = IrMesher::build(b, cfg);
    // Left square is 10x10mm at 1mm cells -> 100 cells. Right square would
    // have been another 100 but lacks source/sink and is pruned.
    REQUIRE(m.nodes.size() == 100);
}

TEST_CASE("mesher: prune kills everything if source + sink are on different islands", "[irmesh][connectivity]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VRAIL"});

    Zone z1; z1.net_id = 1; z1.layer_ordinal = 0;
    Polygon p1; p1.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
    z1.filled.push_back(p1); b.zones.push_back(z1);

    Zone z2; z2.net_id = 1; z2.layer_ordinal = 0;
    Polygon p2; p2.outline = {{0.030, 0}, {0.040, 0}, {0.040, 0.010}, {0.030, 0.010}};
    z2.filled.push_back(p2); b.zones.push_back(z2);

    Pad pa; pa.at = {0.005, 0.005}; pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "A";
    Pad pb; pb.at = {0.035, 0.005}; pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "B";
    b.pads.push_back(pa); b.pads.push_back(pb);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.source_pad_names = {"A"};
    cfg.sink_pad_names   = {"B"};

    auto m = IrMesher::build(b, cfg);
    // No component contains BOTH a source and sink, so the prune drops
    // everything. Caller (Analyze) gets a clean 'mesher produced no nodes'
    // instead of the prior CHOLMOD: matrix not positive definite.
    REQUIRE(m.nodes.empty());
}

TEST_CASE("mesher: auto_select_layer picks the layer with the most copper", "[irmesh][autopick]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});
    b.nets.push_back({1, "GND"});

    // GND zone ONLY on B.Cu. User asks for F.Cu -- auto-pick should switch.
    Zone z;
    z.net_id = 1;
    z.layer_ordinal = 31;
    Polygon p;
    p.outline = {{0, 0}, {0.020, 0}, {0.020, 0.020}, {0, 0.020}};
    z.filled.push_back(p);
    b.zones.push_back(z);

    Pad p1; p1.at = {0.002, 0.010}; p1.net_id = 1; p1.layer_ordinals = {0, 31}; p1.name = "A";
    Pad p2; p2.at = {0.018, 0.010}; p2.net_id = 1; p2.layer_ordinals = {0, 31}; p2.name = "B";
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;  // ask for F.Cu (which has no zone)
    cfg.auto_select_layer = true;

    auto m = IrMesher::build(b, cfg);
    REQUIRE_FALSE(m.nodes.empty());
    REQUIRE(m.primary_layer_used == 31);  // auto-switched to B.Cu
    // Every node is on B.Cu.
    for (const auto& n : m.nodes) REQUIRE(n.layer_ordinal == 31);
}

TEST_CASE("mesher: auto_select_layer=false enforces the requested layer", "[irmesh][autopick]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});
    b.nets.push_back({1, "GND"});

    Zone z; z.net_id = 1; z.layer_ordinal = 31;
    Polygon p; p.outline = {{0, 0}, {0.010, 0}, {0.010, 0.010}, {0, 0.010}};
    z.filled.push_back(p); b.zones.push_back(z);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.auto_select_layer = false;  // strict

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.nodes.empty());  // F.Cu has no copper, no fallback
}

TEST_CASE("mesher: auto_select_layer is a no-op when primary layer already has copper", "[irmesh][autopick]") {
    Board b = with_square_zone(1, 0, 0.010);  // GND on F.Cu
    Pad p1; p1.at = {0.001, 0.005}; p1.net_id = 1; p1.layer_ordinals = {0}; p1.name = "A";
    Pad p2; p2.at = {0.009, 0.005}; p2.net_id = 1; p2.layer_ordinals = {0}; p2.name = "B";
    b.pads.push_back(p1);
    b.pads.push_back(p2);

    MeshConfig cfg;
    cfg.cell_size = 1.0e-3;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;
    cfg.auto_select_layer = true;

    auto m = IrMesher::build(b, cfg);
    REQUIRE(m.primary_layer_used == 0);  // stayed on F.Cu
}

TEST_CASE("track-mesher: 2-segment trace builds 3 nodes, 2 resistors", "[irmesh][track]") {
    // No zones; only segments. Mesher should fall back to track-based 1D.
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VRAIL"});

    Segment s1; s1.start = {0, 0}; s1.end = {0.005, 0}; s1.width = 0.001; s1.layer_ordinal = 0; s1.net_id = 1;
    Segment s2; s2.start = {0.005, 0}; s2.end = {0.010, 0}; s2.width = 0.001; s2.layer_ordinal = 0; s2.net_id = 1;
    b.segments.push_back(s1); b.segments.push_back(s2);

    Pad pa; pa.at = {0, 0};       pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "A";
    Pad pb; pb.at = {0.010, 0};   pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "B";
    b.pads.push_back(pa); b.pads.push_back(pb);

    MeshConfig cfg;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    INFO("nodes=" << m.nodes.size() << " resistors=" << m.resistors.size() << " src=" << m.source_node_ids.size() << " snk=" << m.sink_node_ids.size());
    REQUIRE(m.nodes.size() == 3);     // endpoints (0,0), (5mm,0), (10mm,0)
    REQUIRE(m.resistors.size() == 2);
    REQUIRE_FALSE(m.source_node_ids.empty());
    REQUIRE_FALSE(m.sink_node_ids.empty());
}

TEST_CASE("track-mesher: T-junction merges 3 segments at one node", "[irmesh][track]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VRAIL"});

    // Three segments meeting at (0,0): N, S, E.
    Segment s1; s1.start = {0, 0}; s1.end = {0,  0.005}; s1.width = 0.001; s1.layer_ordinal = 0; s1.net_id = 1;
    Segment s2; s2.start = {0, 0}; s2.end = {0, -0.005}; s2.width = 0.001; s2.layer_ordinal = 0; s2.net_id = 1;
    Segment s3; s3.start = {0, 0}; s3.end = {0.005, 0}; s3.width = 0.001; s3.layer_ordinal = 0; s3.net_id = 1;
    b.segments.push_back(s1); b.segments.push_back(s2); b.segments.push_back(s3);

    Pad pa; pa.at = {0, 0.005};  pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "A";
    Pad pb; pb.at = {0.005, 0};  pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "B";
    b.pads.push_back(pa); b.pads.push_back(pb);

    MeshConfig cfg;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    // 4 distinct endpoints (junction + 3 ends). All 3 segments share the
    // (0,0) junction so connectivity is preserved.
    REQUIRE(m.nodes.size() == 4);
    REQUIRE(m.resistors.size() == 3);
}

TEST_CASE("track-mesher: pad far from any track endpoint is ignored", "[irmesh][track]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    b.nets.push_back({1, "VRAIL"});

    Segment s1; s1.start = {0, 0}; s1.end = {0.005, 0}; s1.width = 0.001; s1.layer_ordinal = 0; s1.net_id = 1;
    b.segments.push_back(s1);

    // Pads far from any track endpoint (>1mm tolerance).
    Pad pa; pa.at = {0.050, 0.050}; pa.net_id = 1; pa.layer_ordinals = {0}; pa.name = "FAR";
    Pad pb; pb.at = {0.080, 0.080}; pb.net_id = 1; pb.layer_ordinals = {0}; pb.name = "ALSO_FAR";
    b.pads.push_back(pa); b.pads.push_back(pb);

    MeshConfig cfg;
    cfg.net_id = 1;
    cfg.layer_ordinal = 0;

    auto m = IrMesher::build(b, cfg);
    // The far pads do not attach -- source/sink lists stay empty. The
    // connectivity prune is a no-op without source/sink, so the segment
    // nodes survive but with no injection point the user just cant solve.
    REQUIRE(m.source_node_ids.empty());
    REQUIRE(m.sink_node_ids.empty());
}
