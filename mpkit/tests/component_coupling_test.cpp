// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Component metadata -> mpkit physics coupling.
//
// Tests cover the three things in ComponentCoupling: default-metadata
// fill, dissipated-power-to-Joule-source, and the summary helper.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

#include "circuitcore/board/Board.h"
#include "mp/ComponentCoupling.h"
#include "mp/Voxelizer.h"

using mpkit::apply_default_metadata;
using mpkit::component_power_to_joule_source;
using mpkit::compute_component_summary;
using mpkit::JouleSourceField;
using mpkit::VoxelMaterialField;
using circuitcore::board::Board;
using circuitcore::board::Component;
using circuitcore::board::Pad;
using circuitcore::board::PadShape;

namespace {

// Two-layer voxel grid: k=0 is bottom copper (B.Cu, ord 31), k=nz-1 is
// top copper (F.Cu, ord 0). Material id 0 = copper.
VoxelMaterialField two_layer_grid(int nx, int ny, int nz,
                                   double dx, double dy, double dz) {
    VoxelMaterialField f;
    f.grid.spec = {nx, ny, nz, dx, dy, dz};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(nx) * ny * nz, /*copper*/0);
    f.layer_ordinal_to_k[0]  = nz - 1;
    f.layer_ordinal_to_k[31] = 0;
    return f;
}

Board board_with_one_top_component(const std::string& name,
                                    double power_w,
                                    double cx, double cy,
                                    double half_w, double half_h) {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    b.stackup.layers.push_back({31, "B.Cu", "signal"});
    Pad pd;
    pd.at = {cx, cy};
    pd.size = {2 * half_w, 2 * half_h};
    pd.layer_ordinals = {0};   // top
    pd.parent_ref = "U1";
    pd.shape = PadShape::Rect;
    b.pads.push_back(pd);

    Component c;
    c.name      = name;
    c.reference = "U1";
    c.at        = {cx, cy};
    c.courtyard_lo = {cx - half_w, cy - half_h};
    c.courtyard_hi = {cx + half_w, cy + half_h};
    c.dissipated_power_w = power_w;
    b.components.push_back(c);
    return b;
}

}  // namespace

TEST_CASE("apply_default_metadata fills height + mass from package family",
          "[component-couple]") {
    Board b;
    Component c;
    c.name = "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm";
    c.reference = "U2";
    b.components.push_back(c);

    REQUIRE(b.components[0].body_height_m == 0.0);
    REQUIRE(b.components[0].mass_kg       == 0.0);

    const int n = apply_default_metadata(b);
    REQUIRE(n == 1);
    REQUIRE(b.components[0].body_height_m > 0.0);
    REQUIRE(b.components[0].mass_kg       > 0.0);

    // Idempotent on a second call.
    REQUIRE(apply_default_metadata(b) == 0);
}

TEST_CASE("apply_default_metadata respects user-set values",
          "[component-couple]") {
    Board b;
    Component c;
    c.name = "Resistor_SMD:R_0402";
    c.body_height_m = 5.5e-3;   // user override
    c.mass_kg       = 1.0e-3;
    b.components.push_back(c);
    REQUIRE(apply_default_metadata(b) == 0);
    REQUIRE(b.components[0].body_height_m == 5.5e-3);
    REQUIRE(b.components[0].mass_kg       == 1.0e-3);
}

TEST_CASE("component_power_to_joule_source stamps a top-side component",
          "[component-couple]") {
    const double dx = 0.5e-3, dy = 0.5e-3, dz = 0.5e-3;
    const int nx = 16, ny = 16, nz = 8;
    auto f = two_layer_grid(nx, ny, nz, dx, dy, dz);

    // Footprint centered at (4 mm, 4 mm), 2 mm x 2 mm.
    Board b = board_with_one_top_component(
        "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
        /*power_w=*/1.0,
        /*cx=*/0.004, /*cy=*/0.004,
        /*half_w=*/0.001, /*half_h=*/0.001);

    JouleSourceField out = component_power_to_joule_source(b, f);
    REQUIRE(out.ok);
    REQUIRE(out.total_power_w == 1.0);
    REQUIRE(out.dropped_nodes == 0);

    // Integral of the source field over volume must equal total power.
    double integral = 0.0;
    const double V = dx * dy * dz;
    for (int kk = 0; kk < nz; ++kk)
        for (int jj = 0; jj < ny; ++jj)
            for (int ii = 0; ii < nx; ++ii)
                integral += out.source.at(ii, jj, kk) * V;
    REQUIRE(std::abs(integral - 1.0) < 1e-9);

    // Top-side means the stamp lands at k_top (nz-1) or nearby; k=0 (bottom)
    // should be unaffected.
    REQUIRE(out.source.at(8, 8, 0) == 0.0);
    // The cell directly under the component on top must be non-zero.
    REQUIRE(out.source.at(8, 8, nz - 1) > 0.0);
}

TEST_CASE("component_power_to_joule_source skips zero-power parts",
          "[component-couple]") {
    const double dx = 1e-3, dy = 1e-3, dz = 1e-3;
    auto f = two_layer_grid(4, 4, 2, dx, dy, dz);

    Board b = board_with_one_top_component(
        "Resistor_SMD:R_0402", /*power_w=*/0.0,
        0.002, 0.002, 0.0005, 0.0005);

    JouleSourceField out = component_power_to_joule_source(b, f);
    REQUIRE(out.ok);
    REQUIRE(out.total_power_w == 0.0);
    // No power deposited anywhere.
    for (std::size_t i = 0; i < out.source.size(); ++i) {
        REQUIRE(out.source.data()[i] == 0.0);
    }
}

TEST_CASE("compute_component_summary tallies mass + power + hottest",
          "[component-couple]") {
    Board b;
    auto push = [&](std::string ref, std::string name,
                     double p, double mass) {
        Component c;
        c.reference = std::move(ref);
        c.name = std::move(name);
        c.dissipated_power_w = p;
        c.mass_kg = mass;
        b.components.push_back(c);
    };
    push("U1", "Package_SO:SOIC-8",   0.25, 0.2e-3);
    push("U2", "Package_TO_SOT_SMD:TO-263-2_TabPin2", 2.50, 1.5e-3);
    push("R5", "Resistor_SMD:R_0402", 0.01, 0.1e-3);

    auto s = compute_component_summary(b);
    REQUIRE(s.n_components == 3);
    REQUIRE(std::abs(s.total_power_w - (0.25 + 2.50 + 0.01)) < 1e-12);
    REQUIRE(std::abs(s.total_mass_kg - (0.2e-3 + 1.5e-3 + 0.1e-3)) < 1e-12);
    REQUIRE(s.hottest_reference == "U2");
    REQUIRE(s.hottest_power_w == 2.50);
}

TEST_CASE("summary uses PackageDefaults mass when component has no mass set",
          "[component-couple]") {
    Board b;
    Component c;
    c.name = "Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical";
    c.reference = "J1";
    // mass_kg stays at 0 -> summary falls back to package lookup.
    b.components.push_back(c);

    auto s = compute_component_summary(b);
    REQUIRE(s.total_mass_kg > 0.0);
}
