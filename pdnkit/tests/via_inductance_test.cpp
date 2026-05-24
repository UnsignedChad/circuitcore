#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pi/ViaInductance.h"

using pdnkit::pi::via_self_inductance;
using pdnkit::pi::via_mutual_inductance;
using pdnkit::pi::via_pair_loop_inductance;
using Catch::Approx;

// 0.3 mm OD via through a 1.6 mm board. Hand-computed:
//   r = 0.15 mm, h = 1.6 mm,  2h/r = 21.33,  ln(21.33) - 1 = 2.06
//   prefactor = mu0 * h / 2pi = 3.2e-10 H/m   (with h in m)
//   L = 3.2e-10 * 2.06 = 660 pH
// Compare against published Wadell value of ~700 pH for the same geometry.
TEST_CASE("via-L: 0.3mm/1.6mm self-inductance is ~660 pH",
          "[via-l][validation]") {
    const double L = via_self_inductance(0.15e-3, 1.6e-3);
    INFO("L_self = " << L * 1.0e12 << " pH");
    REQUIRE(L > 600.0e-12);   // 600 pH
    REQUIRE(L < 750.0e-12);   // 750 pH
}

// Same via, doubled length. Length appears linearly in the prefactor and
// inside the log, so doubling h gives roughly 2x * (ln correction).
// Roughly 2.3x for typical PCB dimensions.
TEST_CASE("via-L: doubling length more than doubles inductance",
          "[via-l]") {
    const double L1 = via_self_inductance(0.15e-3, 1.6e-3);
    const double L2 = via_self_inductance(0.15e-3, 3.2e-3);
    INFO("L(1.6mm) = " << L1*1e12 << " pH, L(3.2mm) = " << L2*1e12 << " pH");
    REQUIRE(L2 / L1 > 2.0);
    REQUIRE(L2 / L1 < 3.0);
}

// Same via, halve the radius. Self-L grows as ln(2h/r) so halving r adds
// ln(2) to the bracket.
TEST_CASE("via-L: thinner via has higher self-L", "[via-l]") {
    const double L_thick = via_self_inductance(0.15e-3, 1.6e-3);
    const double L_thin  = via_self_inductance(0.075e-3, 1.6e-3);
    REQUIRE(L_thin > L_thick);
    const double diff_pH = (L_thin - L_thick) * 1.0e12;
    INFO("delta = " << diff_pH << " pH");
    // mu0*h/(2pi) * ln(2) = 3.2e-10 * 0.693 = ~222 pH
    REQUIRE(diff_pH == Approx(222.0).margin(5.0));
}

// Mutual inductance between two adjacent vias on a 0.5 mm grid
// (typical stitching spacing).
// h = 1.6 mm, d = 0.5 mm,  h/d = 3.2
//   bracket = ln(3.2 + sqrt(11.24)) - sqrt(1.098) + 0.3125
//           = ln(6.55) - 1.048 + 0.3125 = 1.146
//   M = 3.2e-10 * 1.146 = 367 pH
TEST_CASE("via-L: mutual at 0.5mm spacing is ~370 pH",
          "[via-l][validation]") {
    const double M = via_mutual_inductance(0.15e-3, 1.6e-3, 0.5e-3);
    INFO("M = " << M * 1.0e12 << " pH");
    REQUIRE(M > 340.0e-12);
    REQUIRE(M < 400.0e-12);
}

// Loop = self - mutual. For sig/return pair at 0.5mm, ~290 pH.
TEST_CASE("via-L: pair loop inductance is self - mutual",
          "[via-l][validation]") {
    const double r = 0.15e-3;
    const double h = 1.6e-3;
    const double d = 0.5e-3;
    const double L = via_self_inductance(r, h);
    const double M = via_mutual_inductance(r, h, d);
    const double loop = via_pair_loop_inductance(r, h, d);
    REQUIRE(loop == Approx(L - M));
    INFO("loop = " << loop * 1.0e12 << " pH");
    REQUIRE(loop > 250.0e-12);
    REQUIRE(loop < 350.0e-12);
}

// Edge case: zero geometry -> zero inductance, not NaN.
TEST_CASE("via-L: zero radius or length returns 0", "[via-l]") {
    REQUIRE(via_self_inductance(0.0, 1.0e-3) == 0.0);
    REQUIRE(via_self_inductance(0.1e-3, 0.0) == 0.0);
    REQUIRE(via_mutual_inductance(0.1e-3, 1.0e-3, 0.0) == 0.0);
}

// Mutual decays as spacing grows. At very wide spacing it should
// approach zero asymptotically.
TEST_CASE("via-L: mutual goes to zero as spacing grows", "[via-l]") {
    const double M_close = via_mutual_inductance(0.15e-3, 1.6e-3, 0.5e-3);
    const double M_far   = via_mutual_inductance(0.15e-3, 1.6e-3, 10.0e-3);
    REQUIRE(M_far < M_close);
    REQUIRE(M_far < 100.0e-12);
}
