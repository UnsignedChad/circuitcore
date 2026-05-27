// gmsh v2.2 ASCII writer for VoxelMaterialField. See header.

#include "circuitcore/formats/gmsh/MshWriter.h"

#include <fstream>
#include <set>
#include <sstream>

namespace circuitcore::formats::gmsh {

std::string MaterialNameMap::operator()(mpkit::MaterialId id) const {
    switch (id) {
        case mpkit::kAirMaterialId:       return "air";
        case mpkit::kSubstrateMaterialId: return "substrate";
        case mpkit::kCopperMaterialId:    return "copper";
        default: {
            std::ostringstream os;
            os << "material_" << id;
            return os.str();
        }
    }
}

namespace {

// Map a vertex (vi, vj, vk) to a 1-based gmsh node id. Vertex extents
// are (nx+1, ny+1, nz+1) since each voxel cell uses the corners of the
// vertex lattice.
inline std::size_t vertex_id(int vi, int vj, int vk,
                              int nxp1, int nyp1) noexcept {
    return 1 + static_cast<std::size_t>(vi)
             + static_cast<std::size_t>(vj) * nxp1
             + static_cast<std::size_t>(vk) * nxp1 * nyp1;
}

}  // namespace

std::expected<void, WriteError> write_voxel_field_msh(
    const mpkit::VoxelMaterialField& field,
    std::ostream& out,
    MaterialNameMap names) {

    const auto& g = field.grid;
    const int nx = g.nx(), ny = g.ny(), nz = g.nz();
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        return std::unexpected(WriteError{
            "write_voxel_field_msh: grid has zero extent on one or more axes"});
    }
    const std::size_t voxel_count =
        static_cast<std::size_t>(nx) * ny * nz;
    if (field.ids.size() != voxel_count) {
        return std::unexpected(WriteError{
            "write_voxel_field_msh: field.ids size does not match grid extent"});
    }

    // Header.
    out << "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n";

    // Collect distinct non-air material ids in order of first appearance.
    // gmsh expects $PhysicalNames before $Elements so each element can
    // reference a tag the parser has already seen.
    std::vector<mpkit::MaterialId> materials;
    {
        std::set<mpkit::MaterialId> seen;
        for (const auto id : field.ids) {
            if (id == mpkit::kAirMaterialId) continue;
            if (seen.insert(id).second) materials.push_back(id);
        }
    }
    out << "$PhysicalNames\n" << materials.size() << "\n";
    for (const auto id : materials) {
        // dimension=3 (volume), tag=id, "name"
        out << 3 << " " << static_cast<int>(id) << " \""
            << names(id) << "\"\n";
    }
    out << "$EndPhysicalNames\n";

    // Nodes. Emit every vertex of the (nx+1, ny+1, nz+1) lattice. Some
    // may end up unused (corners of all-air regions) -- ElmerGrid drops
    // them on import. The simpler write outweighs the file-size win
    // from compacting; v2 can revisit if 100 MB .msh files matter.
    const int nxp1 = nx + 1;
    const int nyp1 = ny + 1;
    const int nzp1 = nz + 1;
    const std::size_t node_count =
        static_cast<std::size_t>(nxp1) * nyp1 * nzp1;
    out << "$Nodes\n" << node_count << "\n";
    for (int vk = 0; vk < nzp1; ++vk) {
        const double z = g.z0 + vk * g.dz();
        for (int vj = 0; vj < nyp1; ++vj) {
            const double y = g.y0 + vj * g.dy();
            for (int vi = 0; vi < nxp1; ++vi) {
                const double x = g.x0 + vi * g.dx();
                out << vertex_id(vi, vj, vk, nxp1, nyp1)
                    << " " << x << " " << y << " " << z << "\n";
            }
        }
    }
    out << "$EndNodes\n";

    // Elements. One hex8 per non-air voxel.
    std::size_t n_elements = 0;
    for (const auto id : field.ids)
        if (id != mpkit::kAirMaterialId) ++n_elements;
    out << "$Elements\n" << n_elements << "\n";

    std::size_t elem_id = 1;
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const std::size_t idx =
                    static_cast<std::size_t>(i)
                    + static_cast<std::size_t>(j) * nx
                    + static_cast<std::size_t>(k) * nx * ny;
                const auto id = field.ids[idx];
                if (id == mpkit::kAirMaterialId) continue;

                // gmsh hex8 node ordering (zero-based corners):
                //   0:(i  ,j  ,k  )  1:(i+1,j  ,k  )
                //   2:(i+1,j+1,k  )  3:(i  ,j+1,k  )
                //   4:(i  ,j  ,k+1)  5:(i+1,j  ,k+1)
                //   6:(i+1,j+1,k+1)  7:(i  ,j+1,k+1)
                const auto n0 = vertex_id(i,   j,   k,   nxp1, nyp1);
                const auto n1 = vertex_id(i+1, j,   k,   nxp1, nyp1);
                const auto n2 = vertex_id(i+1, j+1, k,   nxp1, nyp1);
                const auto n3 = vertex_id(i,   j+1, k,   nxp1, nyp1);
                const auto n4 = vertex_id(i,   j,   k+1, nxp1, nyp1);
                const auto n5 = vertex_id(i+1, j,   k+1, nxp1, nyp1);
                const auto n6 = vertex_id(i+1, j+1, k+1, nxp1, nyp1);
                const auto n7 = vertex_id(i,   j+1, k+1, nxp1, nyp1);

                // elem-id 5(=hex8) 2-tags physical-tag elementary-tag n0..n7
                out << elem_id << " 5 2 " << static_cast<int>(id)
                    << " " << static_cast<int>(id)
                    << " " << n0 << " " << n1 << " " << n2 << " " << n3
                    << " " << n4 << " " << n5 << " " << n6 << " " << n7
                    << "\n";
                ++elem_id;
            }
        }
    }
    out << "$EndElements\n";

    if (!out) {
        return std::unexpected(WriteError{
            "write_voxel_field_msh: stream went bad mid-write"});
    }
    return {};
}

std::expected<void, WriteError> write_voxel_field_msh(
    const mpkit::VoxelMaterialField& field,
    const std::filesystem::path& path,
    MaterialNameMap names) {
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(WriteError{
            "write_voxel_field_msh: cannot open " + path.string()});
    }
    return write_voxel_field_msh(field, out, std::move(names));
}

}  // namespace circuitcore::formats::gmsh
