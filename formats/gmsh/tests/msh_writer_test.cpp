// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// MshWriter spec-conformance checks. We exercise the writer against
// small synthetic VoxelMaterialFields and verify the emitted gmsh
// stream has the expected node count, element count, hex8 elements,
// and physical-name table. A full ElmerFEM round-trip would need
// Elmer installed in CI -- we punt that to manual validation.

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "circuitcore/formats/gmsh/MshWriter.h"
#include "mp/Voxelizer.h"

using circuitcore::formats::gmsh::write_voxel_field_msh;
using circuitcore::formats::gmsh::MaterialNameMap;
using mpkit::Grid;
using mpkit::VoxelMaterialField;
using mpkit::kAirMaterialId;
using mpkit::kCopperMaterialId;
using mpkit::kSubstrateMaterialId;

namespace {

VoxelMaterialField uniform_field(int nx, int ny, int nz,
                                  mpkit::MaterialId id) {
    VoxelMaterialField f;
    f.grid.spec = {nx, ny, nz, 1e-3, 1e-3, 1e-3};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(nx) * ny * nz, id);
    return f;
}

std::size_t count_lines(const std::string& body, std::string_view prefix) {
    std::size_t n = 0, pos = 0;
    while ((pos = body.find(prefix, pos)) != std::string::npos) {
        ++n; pos += prefix.size();
    }
    return n;
}

}  // namespace

TEST_CASE("msh writer: empty grid is rejected", "[msh]") {
    VoxelMaterialField f;
    f.grid.spec = {0, 0, 0, 1e-3, 1e-3, 1e-3};
    std::ostringstream os;
    auto r = write_voxel_field_msh(f, os);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("msh writer: writes correct node + element counts", "[msh]") {
    // 4 x 3 x 2 grid of copper voxels. Expect:
    //   - 5 * 4 * 3 = 60 nodes
    //   - 24 elements (all non-air)
    auto f = uniform_field(4, 3, 2, kCopperMaterialId);
    std::ostringstream os;
    REQUIRE(write_voxel_field_msh(f, os).has_value());
    const std::string body = os.str();

    // Spot-check section markers and counts.
    REQUIRE(body.find("$MeshFormat") != std::string::npos);
    REQUIRE(body.find("2.2 0 8")     != std::string::npos);
    REQUIRE(body.find("$Nodes\n60")  != std::string::npos);
    REQUIRE(body.find("$Elements\n24") != std::string::npos);
    REQUIRE(body.find("$EndElements") != std::string::npos);

    // Every element line for hex8 starts with `id 5 2 ...`. There should
    // be 24 such lines.
    REQUIRE(count_lines(body, " 5 2 ") == 24);
}

TEST_CASE("msh writer: air voxels are skipped from elements", "[msh]") {
    auto f = uniform_field(2, 2, 2, kAirMaterialId);
    // One of the 8 cells is copper.
    f.ids[3] = kCopperMaterialId;

    std::ostringstream os;
    REQUIRE(write_voxel_field_msh(f, os).has_value());
    const std::string body = os.str();

    // Only 1 element (the lone copper voxel).
    REQUIRE(body.find("$Elements\n1") != std::string::npos);
    // 27 nodes (3 x 3 x 3 vertex lattice for a 2 x 2 x 2 grid).
    REQUIRE(body.find("$Nodes\n27") != std::string::npos);
}

TEST_CASE("msh writer: distinct material ids become $PhysicalNames entries",
          "[msh]") {
    VoxelMaterialField f;
    f.grid.spec = {3, 1, 1, 1e-3, 1e-3, 1e-3};
    f.ids = {kCopperMaterialId, kSubstrateMaterialId,
             /*custom=*/mpkit::MaterialId{7}};

    std::ostringstream os;
    REQUIRE(write_voxel_field_msh(f, os).has_value());
    const std::string body = os.str();

    REQUIRE(body.find("$PhysicalNames\n3")   != std::string::npos);
    REQUIRE(body.find("\"copper\"")          != std::string::npos);
    REQUIRE(body.find("\"substrate\"")       != std::string::npos);
    REQUIRE(body.find("\"material_7\"")      != std::string::npos);
}

TEST_CASE("msh writer: hex8 node ordering matches gmsh convention",
          "[msh]") {
    // 1 x 1 x 1 grid -> one element with corner ids 1..8 in order
    // (z varies slowest, then y, then x in our vertex_id() layout).
    auto f = uniform_field(1, 1, 1, kCopperMaterialId);
    std::ostringstream os;
    REQUIRE(write_voxel_field_msh(f, os).has_value());
    const std::string body = os.str();

    // Element row should end with the 8 node ids in our canonical
    // ordering: 1 2 4 3 5 6 8 7 (bottom CCW, then top CCW).
    //   (0,0,0)=1, (1,0,0)=2, (0,1,0)=3, (1,1,0)=4,
    //   (0,0,1)=5, (1,0,1)=6, (0,1,1)=7, (1,1,1)=8.
    // hex8 corner order is 0:1, 1:2, 2:4, 3:3, 4:5, 5:6, 6:8, 7:7.
    REQUIRE(body.find("1 2 4 3 5 6 8 7") != std::string::npos);
}
