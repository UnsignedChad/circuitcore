// Target impedance for a power rail.
//
// Given a load's nominal voltage, the allowable supply tolerance, and the
// peak step current it can demand, the rail's PDN must have an impedance
// below this target across the load's bandwidth -- otherwise transient
// current pulls the rail outside spec.
//
// The flat-target form (Larry Smith, "PDN by the numbers"):
//
//   Z_target = (V_nom * V_tolerance) / I_step
//
// where V_tolerance is fractional (0.05 for 5 percent). The target holds
// from DC up to the load's bandwidth, which the user knows from the IC's
// datasheet. Above that frequency, on-package bypass takes over and the
// board PDN target relaxes.

#pragma once

namespace pdnkit::pi {

struct TargetZSpec {
    double v_nom_v = 3.3;        // nominal supply voltage
    double v_tolerance = 0.05;   // fractional, e.g. 0.05 = 5 percent
    double i_step_a = 1.0;       // peak transient current step (A)
};

// Z_target = (V_nom * V_tolerance) / I_step, ohms. Returns 0 if I_step
// is non-positive (no current step -> no PDN constraint).
double target_impedance_flat(const TargetZSpec& spec);

}  // namespace pdnkit::pi
