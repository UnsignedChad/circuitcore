// Linear elasticity FEM verification.
//
// Three closed-form comparisons pin the Q1 hex assembly + thermal
// strain RHS + symmetric Dirichlet pinning against known answers.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "circuitcore/field/Field3D.h"
#include "mp/Elasticity.h"
#include "mp/Material.h"
#include "mp/Voxelizer.h"

using mpkit::DofAll;
using mpkit::DofX;
using mpkit::DofY;
using mpkit::DofZ;
using mpkit::BcTarget;
using mpkit::ElasticityBC;
using mpkit::ElasticityConfig;
using mpkit::ElasticityResult;
using mpkit::Material;
using mpkit::VoxelMaterialField;

namespace {

VoxelMaterialField uniform_field(int nx, int ny, int nz,
                                   double dx, double dy, double dz) {
    VoxelMaterialField f;
    f.grid.spec = {nx, ny, nz, dx, dy, dz};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(nx) * ny * nz, 0);
    return f;
}

Material elastic_mat(double E, double nu, double alpha = 0.0) {
    Material m;
    m.youngs_modulus    = E;
    m.poissons_ratio    = nu;
    m.thermal_expansion = alpha;
    return m;
}

}  // namespace

TEST_CASE("Uniaxial compression with nu=0 reproduces sigma_xx = E u_x / L") {
    // 8 x 4 x 4 cells, cube of side L = 8 mm, dx = 1 mm.
    // Xmin clamped in all three axes, Xmax pinned to a uniform
    // displacement -delta in x (free in y, z). With nu = 0 there is no
    // lateral coupling so sigma_xx = E * (-delta / L) and sigma_yy =
    // sigma_zz = 0 anywhere in the bulk.
    constexpr int Nx = 8, Ny = 4, Nz = 4;
    const double dx = 1.0e-3, L = Nx * dx;
    const double E  = 100.0e9, nu = 0.0;
    const double delta = -1.0e-5;  // 10 um compression

    ElasticityConfig cfg;
    cfg.material_field = uniform_field(Nx, Ny, Nz, dx, dx, dx);
    cfg.material_table = {elastic_mat(E, nu)};
    ElasticityBC c_xmin;
    c_xmin.target   = BcTarget::FaceXmin;
    c_xmin.pin_axes = DofAll;
    cfg.bcs.push_back(c_xmin);
    ElasticityBC c_xmax;
    c_xmax.target   = BcTarget::FaceXmax;
    c_xmax.pin_axes = DofX;
    c_xmax.pin_ux   = delta;
    cfg.bcs.push_back(c_xmax);

    ElasticityResult r = mpkit::solve_elasticity(cfg);
    REQUIRE(r.ok);

    const double sigma_xx_expected = E * delta / L;
    // Sample stress at an interior cell.
    const std::size_t centre_cell =
        Nx / 2 + (Ny / 2) * Nx + (Nz / 2) * Nx * Ny;
    const double sigma_xx_got = r.stress[6 * centre_cell + 0];
    const double sigma_yy_got = r.stress[6 * centre_cell + 1];
    const double sigma_zz_got = r.stress[6 * centre_cell + 2];
    REQUIRE(std::abs(sigma_xx_got - sigma_xx_expected)
            < 0.01 * std::abs(sigma_xx_expected));
    REQUIRE(std::abs(sigma_yy_got) < 1.0e-3 * std::abs(sigma_xx_expected));
    REQUIRE(std::abs(sigma_zz_got) < 1.0e-3 * std::abs(sigma_xx_expected));
}

TEST_CASE("Constrained uniform heat: sigma = -E alpha dT / (1 - 2 nu) "
          "hydrostatic") {
    // Cube clamped on all six faces in all three axes, uniform dT in
    // every cell. Free expansion is suppressed everywhere -> the only
    // equilibrium state is a hydrostatic compressive stress.
    constexpr int N = 6;
    const double dx = 1.0e-3;
    const double E  = 100.0e9, nu = 0.30, alpha = 1.0e-5;
    const double dT = 50.0;

    ElasticityConfig cfg;
    cfg.material_field = uniform_field(N, N, N, dx, dx, dx);
    cfg.material_table = {elastic_mat(E, nu, alpha)};
    for (BcTarget face : {BcTarget::FaceXmin, BcTarget::FaceXmax,
                            BcTarget::FaceYmin, BcTarget::FaceYmax,
                            BcTarget::FaceZmin, BcTarget::FaceZmax}) {
        ElasticityBC bc;
        bc.target   = face;
        bc.pin_axes = DofAll;
        cfg.bcs.push_back(bc);
    }
    cfg.temperature_change.resize(N, N, N);
    cfg.temperature_change.fill(dT);

    ElasticityResult r = mpkit::solve_elasticity(cfg);
    REQUIRE(r.ok);

    const double sigma_expected = -E * alpha * dT / (1.0 - 2.0 * nu);
    // Sample at the centre cell.
    const std::size_t centre =
        N / 2 + (N / 2) * N + (N / 2) * N * N;
    const double sxx = r.stress[6 * centre + 0];
    const double syy = r.stress[6 * centre + 1];
    const double szz = r.stress[6 * centre + 2];
    const double sxy = r.stress[6 * centre + 3];
    const double syz = r.stress[6 * centre + 4];
    const double szx = r.stress[6 * centre + 5];

    REQUIRE(std::abs(sxx - sigma_expected)
            < 0.02 * std::abs(sigma_expected));
    REQUIRE(std::abs(syy - sigma_expected)
            < 0.02 * std::abs(sigma_expected));
    REQUIRE(std::abs(szz - sigma_expected)
            < 0.02 * std::abs(sigma_expected));
    REQUIRE(std::abs(sxy) < 1.0e-3 * std::abs(sigma_expected));
    REQUIRE(std::abs(syz) < 1.0e-3 * std::abs(sigma_expected));
    REQUIRE(std::abs(szx) < 1.0e-3 * std::abs(sigma_expected));
}

TEST_CASE("Rejected configuration: no Dirichlet pin returns an error") {
    constexpr int N = 4;
    const double dx = 1.0e-3;
    ElasticityConfig cfg;
    cfg.material_field = uniform_field(N, N, N, dx, dx, dx);
    cfg.material_table = {elastic_mat(100e9, 0.3)};
    // no BCs -- the rigid-body modes are unconstrained.

    ElasticityResult r = mpkit::solve_elasticity(cfg);
    REQUIRE(!r.ok);
    REQUIRE(r.error.find("Dirichlet pin") != std::string::npos);
}
