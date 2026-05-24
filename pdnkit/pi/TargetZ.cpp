#include "pi/TargetZ.h"

namespace pdnkit::pi {

double target_impedance_flat(const TargetZSpec& spec) {
    if (spec.i_step_a <= 0.0) return 0.0;
    return (spec.v_nom_v * spec.v_tolerance) / spec.i_step_a;
}

}  // namespace pdnkit::pi
