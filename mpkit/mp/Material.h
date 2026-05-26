// Per-material physical property bundle.
//
// One Material describes a single homogeneous substance -- copper, FR-4,
// air, etc. mpkit solvers read only the fields they care about: the
// steady-thermal solver reads thermal_conductivity, the transient
// thermal solver also reads density + specific_heat, the elasticity
// solver reads youngs_modulus + poissons_ratio + thermal_expansion,
// and so on. Filling every field for a substance is good practice but
// not required as long as the solver being run only touches present
// fields; missing properties default to NaN so the solver can detect
// and complain rather than silently using zero.

#pragma once

#include <limits>
#include <string>

namespace mpkit {

struct Material {
    std::string name;

    // -- thermal
    double thermal_conductivity = std::numeric_limits<double>::quiet_NaN();  // W/(m*K)
    double density              = std::numeric_limits<double>::quiet_NaN();  // kg/m^3
    double specific_heat        = std::numeric_limits<double>::quiet_NaN();  // J/(kg*K)

    // -- electrical
    double electrical_resistivity = std::numeric_limits<double>::quiet_NaN(); // ohm*m
    double relative_permittivity  = std::numeric_limits<double>::quiet_NaN(); // dimensionless
    double loss_tangent           = std::numeric_limits<double>::quiet_NaN(); // dimensionless
    double relative_permeability  = 1.0;                                      // non-magnetic default

    // -- mechanical
    double youngs_modulus    = std::numeric_limits<double>::quiet_NaN();  // Pa
    double poissons_ratio    = std::numeric_limits<double>::quiet_NaN();  // dimensionless
    double thermal_expansion = std::numeric_limits<double>::quiet_NaN();  // 1/K (linear CTE)
    double yield_strength    = std::numeric_limits<double>::quiet_NaN();  // Pa (optional)
};

}  // namespace mpkit
