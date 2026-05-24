#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <memory>
#include <vector>

#include "si/Topology.h"
#include "si/Touchstone.h"
#include "si/TraceImpedance.h"
#include "si/ViaModel.h"

using namespace sikit::si;
using Complex = std::complex<double>;
using Catch::Approx;

namespace {

sikit::analysis::AnalysisStackup canonical_microstrip() {
    sikit::analysis::AnalysisStackup s;
    s.outer_dielectric_height = 0.2e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.3;
    s.tan_delta = 0.02;
    return s;
}

}  // namespace

TEST_CASE("topology: empty channel cascades to a passthrough 2-port",
          "[topology]") {
    Channel c;
    auto ts = c.cascade({1e9, 5e9, 10e9}, 50.0);
    REQUIRE(ts.num_ports == 2);
    REQUIRE(ts.frequencies.size() == 3);
    for (const auto& m : ts.s_matrices) {
        // Column-major: [S11, S21, S12, S22].
        REQUIRE(std::abs(m[0]) == Approx(0.0).margin(1e-9));  // S11
        REQUIRE(std::abs(m[1]) == Approx(1.0).margin(1e-9));  // S21
        REQUIRE(std::abs(m[2]) == Approx(1.0).margin(1e-9));  // S12
        REQUIRE(std::abs(m[3]) == Approx(0.0).margin(1e-9));  // S22
    }
}

TEST_CASE("topology: single TraceBlock matches direct synthesize_channel",
          "[topology]") {
    auto stk = canonical_microstrip();
    Channel c;
    c.add(std::make_unique<TraceBlock>(0.20e-3, 0.05, 0, stk));

    auto ts_chan = c.cascade({1e9, 5e9}, 50.0);

    // Same trace synthesized directly via the standalone pipeline.
    sikit::analysis::ChannelSpec spec;
    spec.trace_width = 0.20e-3;
    spec.length_m = 0.05;
    spec.layer_ordinal = 0;
    spec.stackup = stk;
    auto ts_direct = sikit::analysis::synthesize_channel(spec, {1e9, 5e9}, 50.0);

    for (std::size_t k = 0; k < ts_chan.frequencies.size(); ++k) {
        for (int i = 0; i < 4; ++i) {
            REQUIRE(std::abs(ts_chan.s_matrices[k][i] -
                              ts_direct.s_matrices[k][i]) <
                    1e-9 * std::abs(ts_direct.s_matrices[k][i]) + 1e-12);
        }
    }
}

TEST_CASE("topology: LumpedBlock series-R inserts insertion loss",
          "[topology][lumped]") {
    Channel c;
    c.add(std::make_unique<LumpedBlock>(LumpedBlock::Topology::SeriesR, 10.0));
    auto ts = c.cascade({1e9}, 50.0);
    // A 10 ohm series resistor in a 50 ohm system reduces |S21| from 1.0
    // to roughly 2*50/(2*50 + 10) = 100/110 ~ 0.909.
    const double s21 = std::abs(ts.s_matrices[0][1]);
    REQUIRE(s21 == Approx(0.909).margin(0.005));
}

TEST_CASE("topology: LumpedBlock shunt-C rolls off at high frequency",
          "[topology][lumped]") {
    // 1 pF shunt to ground. At 1 GHz |Y_C| = 2pi*1e9*1e-12 = 6.28 mS.
    // Should drop |S21| measurably; at 10 GHz the drop is much larger.
    Channel c;
    c.add(std::make_unique<LumpedBlock>(LumpedBlock::Topology::ShuntC, 1e-12));
    auto ts = c.cascade({1e8, 1e9, 1e10}, 50.0);
    const double s21_low  = std::abs(ts.s_matrices[0][1]);
    const double s21_mid  = std::abs(ts.s_matrices[1][1]);
    const double s21_high = std::abs(ts.s_matrices[2][1]);
    REQUIRE(s21_low > s21_mid);
    REQUIRE(s21_mid > s21_high);
}

TEST_CASE("topology: IdealLineBlock matched to z_ref is lossless and "
          "phase-shifts only", "[topology][line]") {
    // 50 ohm line, 50 ohm reference -> no impedance mismatch -> |S11| = 0,
    // |S21| = 1, S21 phase advances with length * freq.
    Channel c;
    const double v_p = 1.5e8;
    const double L   = 0.05;
    c.add(std::make_unique<IdealLineBlock>(50.0, L, v_p));
    auto ts = c.cascade({1e9, 5e9}, 50.0);
    for (const auto& m : ts.s_matrices) {
        REQUIRE(std::abs(m[0]) == Approx(0.0).margin(1e-9));   // S11
        REQUIRE(std::abs(m[1]) == Approx(1.0).margin(1e-9));   // |S21| = 1
    }
}

TEST_CASE("topology: cascaded ideal line phases add", "[topology][line]") {
    // Two 50 mm sections should give twice the phase shift of one.
    const double v_p = 1.5e8;
    Channel one;
    one.add(std::make_unique<IdealLineBlock>(50.0, 0.05, v_p));
    Channel two;
    two.add(std::make_unique<IdealLineBlock>(50.0, 0.05, v_p));
    two.add(std::make_unique<IdealLineBlock>(50.0, 0.05, v_p));

    auto ts1 = one.cascade({5e9}, 50.0);
    auto ts2 = two.cascade({5e9}, 50.0);
    const double phase1 = std::arg(ts1.s_matrices[0][1]);
    const double phase2 = std::arg(ts2.s_matrices[0][1]);
    // Up to a possible 2*pi wrap. Use the wrapped difference modulo
    // 2*pi: 2*phase1 - phase2 should be within a few hundred nanoradians
    // of zero (or one full turn).
    const double diff_raw = std::abs(2.0 * phase1 - phase2);
    const double diff_wrapped =
        std::min(diff_raw, std::abs(diff_raw - 2.0 * 3.14159265358979));
    REQUIRE(diff_wrapped == Approx(0.0).margin(1e-6));
}

TEST_CASE("topology: ViaBlock contributes via insertion loss", "[topology][via]") {
    sikit::analysis::ViaSpec v;
    v.drill_diameter   = 0.30e-3;
    v.pad_diameter     = 0.60e-3;
    v.antipad_diameter = 1.00e-3;
    v.total_length     = 1.6e-3;
    v.pad_to_plane_h   = 0.20e-3;
    v.epsilon_r        = 4.3;
    Channel c;
    c.add(std::make_unique<ViaBlock>(v));
    auto ts = c.cascade({1e8, 1e9, 1e10, 3e10}, 50.0);
    // |S21| at low freq close to 1, then drops at high freq.
    const double s21_low  = std::abs(ts.s_matrices[0][1]);
    const double s21_high = std::abs(ts.s_matrices[3][1]);
    REQUIRE(s21_low > 0.95);
    REQUIRE(s21_high < s21_low);
}

TEST_CASE("topology: TouchstoneBlock wraps an external 2-port",
          "[topology][touchstone]") {
    using namespace sikit::touchstone;
    TouchstoneFile ts;
    ts.num_ports = 2;
    ts.reference_impedance = 50.0;
    ts.format = Format::RealImaginary;
    ts.frequencies = {1e9, 10e9};
    ts.s_matrices = {
        {Complex(0, 0), Complex(0.8, 0), Complex(0.8, 0), Complex(0, 0)},
        {Complex(0, 0), Complex(0.5, 0), Complex(0.5, 0), Complex(0, 0)},
    };
    Channel c;
    c.add(std::make_unique<TouchstoneBlock>(ts, "vendor_cable"));
    auto out = c.cascade({1e9, 10e9}, 50.0);
    REQUIRE(std::abs(out.s_matrices[0][1]) == Approx(0.8).margin(0.01));
    REQUIRE(std::abs(out.s_matrices[1][1]) == Approx(0.5).margin(0.01));
}

TEST_CASE("topology: TouchstoneBlock rejects non-2-port input",
          "[topology][touchstone]") {
    using namespace sikit::touchstone;
    TouchstoneFile ts;
    ts.num_ports = 4;
    REQUIRE_THROWS(TouchstoneBlock(ts, "bad"));
}

TEST_CASE("topology: a realistic chain has compounded insertion loss",
          "[topology]") {
    // Trace -> via -> trace cascade. Each individual block has < 1 dB
    // insertion loss at 5 GHz; the cascade should have measurably more
    // than either single block.
    auto stk = canonical_microstrip();
    sikit::analysis::ViaSpec via;
    via.drill_diameter = 0.30e-3;
    via.pad_diameter = 0.60e-3;
    via.antipad_diameter = 1.0e-3;
    via.total_length = 1.6e-3;
    via.pad_to_plane_h = 0.20e-3;
    via.epsilon_r = 4.3;

    Channel trace_only;
    trace_only.add(std::make_unique<TraceBlock>(0.20e-3, 0.05, 0, stk));
    auto ts_trace = trace_only.cascade({5e9}, 50.0);

    Channel full;
    full.add(std::make_unique<TraceBlock>(0.20e-3, 0.05, 0, stk));
    full.add(std::make_unique<ViaBlock>(via));
    full.add(std::make_unique<TraceBlock>(0.20e-3, 0.05, 0, stk));
    auto ts_full = full.cascade({5e9}, 50.0);

    const double s21_trace = std::abs(ts_trace.s_matrices[0][1]);
    const double s21_full  = std::abs(ts_full.s_matrices[0][1]);
    REQUIRE(s21_full < s21_trace);
}

TEST_CASE("topology: clear() resets the block list", "[topology]") {
    Channel c;
    c.add(std::make_unique<LumpedBlock>(LumpedBlock::Topology::SeriesR, 5.0));
    REQUIRE(c.size() == 1);
    c.clear();
    REQUIRE(c.empty());
    auto ts = c.cascade({1e9}, 50.0);
    REQUIRE(std::abs(ts.s_matrices[0][1]) == Approx(1.0));
}
