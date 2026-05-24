#include <catch2/catch_test_macros.hpp>

#include "circuitcore/board/Board.h"
#include "si/Fdtd3d.h"
#include "si/FdtdRasterize.h"

using namespace sikit::fdtd;
using namespace circuitcore::board;

namespace {

// Build a tiny synthetic 2-layer board: F.Cu at the top, In1.Cu at
// some z down, dielectric between. One segment, one via.
Board make_synth_board() {
    Board b;
    b.stackup.layers.push_back(Layer{
        .ordinal = 0, .name = "F.Cu", .type = "signal",
        .thickness = 35e-6, .material = "copper"});
    b.stackup.layers.push_back(Layer{
        .ordinal = 1, .name = "dielectric",  .type = "dielectric",
        .thickness = 1.0e-3, .material = "FR4", .epsilon_r = 4.3});
    b.stackup.layers.push_back(Layer{
        .ordinal = 31, .name = "B.Cu", .type = "signal",
        .thickness = 35e-6, .material = "copper"});

    // 10mm long, 0.2mm wide segment on F.Cu.
    Segment s;
    s.start = {1e-3, 5e-3};
    s.end   = {11e-3, 5e-3};
    s.width = 0.2e-3;
    s.layer_ordinal = 0;
    b.segments.push_back(s);

    // Via from F.Cu to B.Cu.
    Via v;
    v.at = {6e-3, 5e-3};
    v.outer_diameter = 0.6e-3;
    v.drill = 0.3e-3;
    v.from_layer = 0;
    v.to_layer = 31;
    b.vias.push_back(v);

    // A 5x5 mm zone on B.Cu (ground pour).
    Zone z;
    z.layer_ordinal = 31;
    z.outline.outline = {{0.0, 0.0}, {12e-3, 0.0},
                          {12e-3, 10e-3}, {0.0, 10e-3}};
    b.zones.push_back(z);

    return b;
}

}  // namespace

TEST_CASE("rasterize: default mapping records copper-layer Zs",
          "[fdtd-raster]") {
    auto b = make_synth_board();
    auto m = make_default_mapping(b);
    // F.Cu at z=0 (top of stackup).
    REQUIRE(m.layer_z[0] == 0.0);
    // B.Cu after a 35um copper + 1.0mm dielectric.
    REQUIRE(m.layer_z.count(31) == 1);
    const double z_bcu = m.layer_z[31];
    REQUIRE(z_bcu > 1.0e-3);   // past the dielectric
    REQUIRE(z_bcu < 1.1e-3);   // not way past
}

TEST_CASE("rasterize: a segment writes PEC cells on its z-slab",
          "[fdtd-raster]") {
    auto b = make_synth_board();
    auto m = make_default_mapping(b);
    GridSpec g{60, 30, 8, 0.2e-3, 0.2e-3, 0.2e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const std::size_t cells = rasterize_segment(s, b.segments.front(), m);
    REQUIRE(cells > 0);
    // Board bbox spans (0,0)-(12mm,10mm) (the zone outline dominates),
    // so the mapping origin is at (0,0). Segment midpoint at (5mm,5mm)
    // lands on grid cell (25,25). z=0 -> k=0 (F.Cu layer).
    REQUIRE(s.pec_x(25, 25, 0) + s.pec_y(25, 25, 0) + s.pec_z(25, 25, 0) > 0);
    // Same (x, y) at a higher-k slice has no PEC.
    REQUIRE(s.pec_x(25, 25, 5) + s.pec_y(25, 25, 5) + s.pec_z(25, 25, 5) == 0);
}

TEST_CASE("rasterize: a via writes PEC through the stackup z range",
          "[fdtd-raster]") {
    auto b = make_synth_board();
    auto m = make_default_mapping(b);
    GridSpec g{60, 30, 8, 0.2e-3, 0.2e-3, 0.2e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const std::size_t cells = rasterize_via(s, b.vias.front(), m);
    REQUIRE(cells > 0);
    // Via at (6mm, 5mm) with origin (0,0) -> grid (30, 25). z range
    // covers k=0..k_bcu (B.Cu sits at ~1.07mm; with dz=0.2mm that's
    // k=5).
    REQUIRE(s.pec_x(30, 25, 0) + s.pec_y(30, 25, 0) > 0);
    REQUIRE(s.pec_x(30, 25, 5) + s.pec_y(30, 25, 5) > 0);
}

TEST_CASE("rasterize: a filled zone marks every interior cell",
          "[fdtd-raster]") {
    auto b = make_synth_board();
    auto m = make_default_mapping(b);
    GridSpec g{60, 60, 8, 0.2e-3, 0.2e-3, 0.2e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const std::size_t cells = rasterize_zone(s, b.zones.front(), m);
    // 12mm x 10mm zone at 0.2mm grid: ~60*50 = 3000 cells in the polygon.
    REQUIRE(cells > 1000u);
}

TEST_CASE("rasterize: rasterize_board reports nonzero counts on a "
          "non-trivial board",
          "[fdtd-raster]") {
    auto b = make_synth_board();
    auto m = make_default_mapping(b);
    GridSpec g{60, 60, 8, 0.2e-3, 0.2e-3, 0.2e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const auto r = rasterize_board(s, b, m);
    REQUIRE(r.n_segments_processed == 1);
    REQUIRE(r.n_vias_processed     == 1);
    REQUIRE(r.n_zones_processed    == 1);
    REQUIRE(r.segment_pec_cells > 0);
    REQUIRE(r.via_pec_cells     > 0);
    REQUIRE(r.zone_pec_cells    > 0);
    // Substrate cells should fill the dielectric layer between F.Cu
    // and B.Cu in the z direction.
    REQUIRE(r.substrate_cells > 0u);
}

TEST_CASE("rasterize: pec_cell_count tallies the same order as the "
          "rasteriser reports",
          "[fdtd-raster]") {
    auto b = make_synth_board();
    auto m = make_default_mapping(b);
    GridSpec g{60, 60, 8, 0.2e-3, 0.2e-3, 0.2e-3};
    FDTD3D s(g);
    s.set_dt_from_cfl();
    const auto r = rasterize_board(s, b, m);
    // mark_pec_box flags all three E components in each marked cell,
    // so pec_cell_count() is ~3x the report's PEC cell count (with
    // some overlap between zones / segments / vias). Just check both
    // are positive and within a sensible factor.
    const std::size_t reported = r.segment_pec_cells + r.zone_pec_cells +
                                  r.via_pec_cells;
    REQUIRE(s.pec_cell_count() > 0u);
    REQUIRE(s.pec_cell_count() >= reported);
}
