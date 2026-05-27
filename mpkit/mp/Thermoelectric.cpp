#include "mp/Thermoelectric.h"

#include <cmath>
#include <cstddef>

namespace mpkit::thermo {

namespace {

double seebeck_or_zero(const Material& m) {
    return std::isnan(m.seebeck_coefficient) ? 0.0 : m.seebeck_coefficient;
}

}  // namespace

double peltier_coefficient(const Material& m, double t_kelvin) {
    return seebeck_or_zero(m) * t_kelvin;
}

double seebeck_emf_1d(const std::vector<const Material*>& path_materials,
                       const std::vector<double>& temperatures) {
    if (path_materials.empty() ||
        temperatures.size() != path_materials.size() + 1) return 0.0;
    double v = 0.0;
    for (std::size_t i = 0; i < path_materials.size(); ++i) {
        if (!path_materials[i]) continue;
        const double s = seebeck_or_zero(*path_materials[i]);
        v += s * (temperatures[i + 1] - temperatures[i]);
    }
    return v;
}

double thermocouple_emf(const Material& wire_a,
                         const Material& wire_b,
                         double t_hot_kelvin,
                         double t_cold_kelvin) {
    const double dt = t_hot_kelvin - t_cold_kelvin;
    return (seebeck_or_zero(wire_a) - seebeck_or_zero(wire_b)) * dt;
}

double peltier_junction_flux(const Material& a,
                              const Material& b,
                              double t_kelvin,
                              double j_normal) {
    return (peltier_coefficient(a, t_kelvin)
            - peltier_coefficient(b, t_kelvin)) * j_normal;
}

circuitcore::field::Field3D thomson_volumetric_heat(
    const VoxelMaterialField&            material_field,
    const std::vector<Material>&         /*material_table*/,
    const circuitcore::field::Field3D&   temperature,
    const circuitcore::field::Field3D&   /*current_density_x*/,
    const circuitcore::field::Field3D&   /*current_density_y*/,
    const circuitcore::field::Field3D&   /*current_density_z*/) {
    // dS/dT is not yet a Material field, so tau = T * dS/dT = 0 for every
    // voxel under v1's piecewise-constant Seebeck approximation. Return
    // an all-zero source field shaped like the temperature field so
    // callers can drop it into a heat solver as a placeholder.
    circuitcore::field::Field3D q(temperature.nx(),
                                    temperature.ny(),
                                    temperature.nz());
    q.fill(0.0);
    (void)material_field;
    return q;
}

}  // namespace mpkit::thermo
