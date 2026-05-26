#include "mp/MaterialLibrary.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace mpkit {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

Material copper() {
    Material m;
    m.name = "copper";
    m.thermal_conductivity   = 401.0;       // W/(m*K) annealed
    m.density                = 8960.0;      // kg/m^3
    m.specific_heat          = 385.0;       // J/(kg*K)
    m.electrical_resistivity = 1.68e-8;     // ohm*m at 20 C
    m.relative_permittivity  = 1.0;         // effectively
    m.relative_permeability  = 1.0;
    m.youngs_modulus         = 110.0e9;     // Pa
    m.poissons_ratio         = 0.34;
    m.thermal_expansion      = 16.5e-6;     // 1/K
    m.yield_strength         = 70.0e6;      // Pa (annealed; cold-worked is higher)
    return m;
}

Material fr4() {
    Material m;
    m.name = "fr4";
    m.thermal_conductivity   = 0.30;        // in-plane higher (~0.8), through ~0.3
    m.density                = 1850.0;
    m.specific_heat          = 1150.0;
    m.electrical_resistivity = 1.0e14;      // bulk; surface lower
    m.relative_permittivity  = 4.3;         // at 1 GHz; drops with f
    m.loss_tangent           = 0.020;
    m.youngs_modulus         = 22.0e9;
    m.poissons_ratio         = 0.18;
    m.thermal_expansion      = 14.0e-6;     // in-plane below Tg
    return m;
}

Material air() {
    Material m;
    m.name = "air";
    m.thermal_conductivity   = 0.026;       // W/(m*K) at 25 C, still air
    m.density                = 1.184;       // kg/m^3 at 25 C, 1 atm
    m.specific_heat          = 1005.0;
    m.relative_permittivity  = 1.0006;
    m.relative_permeability  = 1.0;
    m.electrical_resistivity = 1.3e16;
    return m;
}

Material aluminium() {
    Material m;
    m.name = "aluminium";
    m.thermal_conductivity   = 237.0;
    m.density                = 2700.0;
    m.specific_heat          = 897.0;
    m.electrical_resistivity = 2.82e-8;
    m.relative_permittivity  = 1.0;
    m.youngs_modulus         = 69.0e9;
    m.poissons_ratio         = 0.33;
    m.thermal_expansion      = 23.1e-6;
    m.yield_strength         = 95.0e6;      // 6061-O; T6 is much higher
    return m;
}

Material silver() {
    Material m;
    m.name = "silver";
    m.thermal_conductivity   = 429.0;
    m.density                = 10490.0;
    m.specific_heat          = 235.0;
    m.electrical_resistivity = 1.59e-8;
    m.youngs_modulus         = 83.0e9;
    m.poissons_ratio         = 0.37;
    m.thermal_expansion      = 18.9e-6;
    return m;
}

Material solder_sac305() {
    Material m;
    m.name = "solder_sac305";
    m.thermal_conductivity   = 60.0;        // SAC305 ballpark
    m.density                = 7400.0;
    m.specific_heat          = 220.0;
    m.electrical_resistivity = 13.0e-8;
    m.youngs_modulus         = 51.0e9;
    m.poissons_ratio         = 0.36;
    m.thermal_expansion      = 23.0e-6;
    m.yield_strength         = 30.0e6;
    return m;
}

Material polyimide() {
    Material m;
    m.name = "polyimide";
    m.thermal_conductivity   = 0.12;
    m.density                = 1420.0;
    m.specific_heat          = 1090.0;
    m.electrical_resistivity = 1.0e17;
    m.relative_permittivity  = 3.5;
    m.loss_tangent           = 0.005;
    m.youngs_modulus         = 2.5e9;
    m.poissons_ratio         = 0.34;
    m.thermal_expansion      = 20.0e-6;
    return m;
}

namespace {

const std::unordered_map<std::string, Material(*)()>& registry() {
    static const std::unordered_map<std::string, Material(*)()> r = {
        {"copper",         &copper},
        {"fr4",            &fr4},
        {"air",            &air},
        {"aluminium",      &aluminium},
        {"aluminum",       &aluminium},  // US spelling alias
        {"silver",         &silver},
        {"solder_sac305",  &solder_sac305},
        {"polyimide",      &polyimide},
    };
    return r;
}

}  // namespace

Material material_by_name(const std::string& name) {
    const auto& r = registry();
    auto it = r.find(to_lower(name));
    if (it == r.end()) {
        throw std::out_of_range("mpkit::material_by_name: unknown material '"
                                 + name + "'");
    }
    return it->second();
}

std::vector<std::string> material_names() {
    std::vector<std::string> out;
    for (const auto& [k, _] : registry()) out.push_back(k);
    // Aluminium spelling alias hides aluminum; drop the alias from the list.
    out.erase(std::remove(out.begin(), out.end(), std::string("aluminum")),
              out.end());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace mpkit
