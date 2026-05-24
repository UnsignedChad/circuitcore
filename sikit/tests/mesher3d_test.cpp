#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "circuitcore/board/Board.h"
#include "render/Mesher3D.h"
#include "si/SiStackup.h"

using namespace sikit::render;
using namespace circuitcore::board;
using Catch::Approx;

namespace {

Board minimal_2layer() {
    Board b;
    b.stackup.total_thickness = 1.6e-3;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});
    return b;
}

std::pair<Board, sikit::si::SiStackup> explicit_4layer() {
    Board b;
    sikit::si::SiStackup sis;
    b.stackup.total_thickness = 1.6e-3;
    b.stackup.layers.push_back({0,  "F.Cu",   "signal"});
    b.stackup.layers.push_back({1,  "In1.Cu", "power"});
    b.stackup.layers.push_back({2,  "In2.Cu", "power"});
    b.stackup.layers.push_back({31, "B.Cu",   "signal"});

    auto push = [&](sikit::si::SiStackupItem::Kind k, std::string n, double t, double e = 0) {
        sikit::si::SiStackupItem it;
        it.kind = k; it.name = std::move(n); it.thickness = t; it.epsilon_r = e;
        sis.items.push_back(it);
    };
    push(sikit::si::SiStackupItem::Kind::Copper,     "F.Cu",   35e-6);
    push(sikit::si::SiStackupItem::Kind::Dielectric, "prepreg-1", 0.20e-3, 4.5);
    push(sikit::si::SiStackupItem::Kind::Copper,     "In1.Cu", 18e-6);
    push(sikit::si::SiStackupItem::Kind::Dielectric, "core",    0.71e-3, 4.4);
    push(sikit::si::SiStackupItem::Kind::Copper,     "In2.Cu", 18e-6);
    push(sikit::si::SiStackupItem::Kind::Dielectric, "prepreg-2", 0.20e-3, 4.5);
    push(sikit::si::SiStackupItem::Kind::Copper,     "B.Cu",   35e-6);
    return {std::move(b), std::move(sis)};
}

}  // namespace

TEST_CASE("mesher3d: empty board yields empty meshes", "[m3d]") {
    Board b = minimal_2layer();
    sikit::si::SiStackup sis;
    auto m = build_board_mesh_3d(b, sis);
    REQUIRE(m.copper.empty());
    REQUIRE(m.vias.empty());
    REQUIRE(m.dielectric.empty());
}

TEST_CASE("mesher3d: a single segment produces a copper box (24 verts, 36 idx)",
          "[m3d]") {
    Board b = minimal_2layer();
    sikit::si::SiStackup sis;
    Segment s;
    s.start = {0.0, 0.0};
    s.end   = {0.010, 0.0};
    s.width = 0.0002;
    s.layer_ordinal = 0;
    b.segments.push_back(s);

    auto m = build_board_mesh_3d(b, sis);
    REQUIRE(!m.copper.empty());
    // 6 faces × 4 verts × (pos3+normal3+rgba4 = 10 floats) = 240 floats per box.
    REQUIRE(m.copper.vertices.size() == 240);
    REQUIRE(m.copper.indices.size()  == 36);
    // Dielectric should also appear (synthesised fallback creates one slab).
    REQUIRE(!m.dielectric.empty());
}

TEST_CASE("mesher3d: a single via produces a cylinder mesh", "[m3d]") {
    Board b = minimal_2layer();
    sikit::si::SiStackup sis;
    Via v;
    v.at = {0.005, 0.005};
    v.outer_diameter = 0.8e-3;
    v.drill = 0.4e-3;
    v.from_layer = 0;
    v.to_layer = 31;
    b.vias.push_back(v);

    auto m = build_board_mesh_3d(b, sis);
    REQUIRE(!m.vias.empty());
    // 24-sided cylinder side wall (24 quads × 4 verts) + 2 caps (1 centre +
    // 24 rim verts each) → 24·4 + 2·25 = 146 vertices.
    const std::size_t verts = m.vias.vertices.size() / 10;
    REQUIRE(verts == 146);
}

TEST_CASE("mesher3d: explicit 4-layer stackup → multiple dielectric slabs",
          "[m3d]") {
    auto [b, sis] = explicit_4layer();
    // Need at least one segment so the board has a non-zero bounding box,
    // otherwise the mesher exits early.
    Segment s;
    s.start = {0, 0}; s.end = {1e-3, 0}; s.width = 0.0002;
    s.layer_ordinal = 0; b.segments.push_back(s);

    auto m = build_board_mesh_3d(b, sis);
    // Three dielectric items × 240 floats/box = 720 floats.
    REQUIRE(m.dielectric.vertices.size() == 720);
    REQUIRE(m.dielectric.indices.size()  == 3 * 36);
}

TEST_CASE("mesher3d: synthesized stackup fallback when items empty",
          "[m3d]") {
    Board b = minimal_2layer();
    sikit::si::SiStackup sis;
    // No StackupItems → mesher should synthesise a single slab + place
    // copper layers using the default thickness.
    Segment s;
    s.start = {0, 0}; s.end = {1e-3, 0}; s.width = 0.0002;
    s.layer_ordinal = 0; b.segments.push_back(s);

    auto m = build_board_mesh_3d(b, sis);
    REQUIRE(m.dielectric.indices.size() == 36);   // exactly one slab box
    REQUIRE(!m.copper.empty());
}

TEST_CASE("mesher3d: segments on non-existent layers are skipped", "[m3d]") {
    Board b = minimal_2layer();
    sikit::si::SiStackup sis;
    Segment good;
    good.start = {0, 0}; good.end = {1e-3, 0}; good.width = 0.0002;
    good.layer_ordinal = 0;
    Segment bad = good;
    bad.layer_ordinal = 99;       // not in the stackup
    b.segments.push_back(good);
    b.segments.push_back(bad);

    auto m = build_board_mesh_3d(b, sis);
    // Only one segment got meshed.
    REQUIRE(m.copper.indices.size() == 36);
}

TEST_CASE("mesher3d: copper vertices carry non-zero normals", "[m3d]") {
    // Sanity: every vertex should have a unit-ish normal so the lit
    // shader can shade it. Zero normals would render black.
    Board b = minimal_2layer();
    sikit::si::SiStackup sis;
    Segment s;
    s.start = {0, 0}; s.end = {1e-3, 0}; s.width = 0.0002;
    s.layer_ordinal = 0; b.segments.push_back(s);

    auto m = build_board_mesh_3d(b, sis);
    const std::size_t n_verts = m.copper.vertices.size() / 10;
    REQUIRE(n_verts > 0);
    for (std::size_t i = 0; i < n_verts; ++i) {
        const float nx = m.copper.vertices[i * 10 + 3];
        const float ny = m.copper.vertices[i * 10 + 4];
        const float nz = m.copper.vertices[i * 10 + 5];
        const float len = nx * nx + ny * ny + nz * nz;
        REQUIRE(len == Approx(1.0f).margin(0.01f));
    }
}
