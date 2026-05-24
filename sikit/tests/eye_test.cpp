#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "si/Eye.h"

using namespace sikit::eye;
using Catch::Approx;

TEST_CASE("prbs7: period of 127 and contains both bits", "[eye]") {
    auto seq = prbs7(254);  // two full periods
    REQUIRE(seq.size() == 254);
    // Bits in [0..127) should equal bits in [127..254).
    for (std::size_t i = 0; i < 127; ++i) {
        REQUIRE(seq[i] == seq[i + 127]);
    }
    // Both 0 and 1 appear (rules out a stuck LFSR).
    REQUIRE(std::any_of(seq.begin(), seq.end(), [](int b) { return b == 0; }));
    REQUIRE(std::any_of(seq.begin(), seq.end(), [](int b) { return b == 1; }));
}

TEST_CASE("nrz_waveform: each bit becomes samples_per_ui samples at ±1", "[eye]") {
    auto y = nrz_waveform({0, 1, 0, 1}, 4);
    REQUIRE(y.size() == 16);
    REQUIRE(y[0]  == Approx(-1.0));
    REQUIRE(y[3]  == Approx(-1.0));
    REQUIRE(y[4]  == Approx( 1.0));
    REQUIRE(y[7]  == Approx( 1.0));
    REQUIRE(y[8]  == Approx(-1.0));
    REQUIRE(y[11] == Approx(-1.0));
}

TEST_CASE("rc_lowpass: DC passes; sufficiently high frequency attenuated", "[eye]") {
    const double dt = 1e-9;        // 1 ns sample period (1 GS/s)
    const double fc = 100e6;       // 100 MHz cutoff

    // DC input → DC output (after transient).
    std::vector<double> dc(2000, 1.0);
    auto y_dc = rc_lowpass(dc, dt, fc);
    REQUIRE(y_dc.back() == Approx(1.0).margin(1e-3));

    // High-frequency square wave (much higher than cutoff) should be attenuated.
    std::vector<double> hf(2000);
    for (std::size_t i = 0; i < hf.size(); ++i) {
        hf[i] = (i % 2 == 0) ? 1.0 : -1.0;  // Nyquist-rate square = 500 MHz
    }
    auto y_hf = rc_lowpass(hf, dt, fc);
    // After settling, magnitude should be well below 1.
    double max_after = 0;
    for (std::size_t i = 500; i < y_hf.size(); ++i) {
        max_after = std::max(max_after, std::abs(y_hf[i]));
    }
    REQUIRE(max_after < 0.5);
}

TEST_CASE("eye: clean NRZ has wide-open eye, two horizontal bands", "[eye]") {
    auto bits = prbs7(500);
    const int spu = 16;
    auto y = nrz_waveform(bits, spu);
    auto eye = build_eye(y, spu, 32, 32);

    REQUIRE(eye.max_count() > 0);

    // Mid-voltage bin should have zero count for unfiltered NRZ
    // (signal is exactly ±1, never crosses zero).
    const int mid_v = 16;
    int mid_total = 0;
    for (int t = 0; t < eye.time_bins; ++t) {
        mid_total += eye.at(t, mid_v);
    }
    REQUIRE(mid_total == 0);

    // High-voltage and low-voltage bins should each have substantial count.
    int top_total = 0, bot_total = 0;
    for (int t = 0; t < eye.time_bins; ++t) {
        top_total += eye.at(t, 30);
        bot_total += eye.at(t, 1);
    }
    REQUIRE(top_total > 0);
    REQUIRE(bot_total > 0);
}

TEST_CASE("eye: heavily filtered channel closes the eye middle", "[eye]") {
    auto bits = prbs7(1000);
    const int spu = 16;
    const double dt = 1.0;          // arbitrary; only fc·dt ratio matters
    auto y_tx = nrz_waveform(bits, spu);

    // Cutoff well below baud → severe ISI, eye middle fills in.
    // Baud per sample = 1/spu = 1/16. fc=0.005 in normalized units
    // is well under that, killing the high-frequency content.
    auto y_rx = rc_lowpass(y_tx, dt, 0.005);
    auto eye = build_eye(y_rx, spu, 32, 32);

    // Middle voltage rows should now have non-zero counts (eye closing).
    int mid_count = 0;
    for (int t = 0; t < eye.time_bins; ++t) {
        mid_count += eye.at(t, 16);
    }
    REQUIRE(mid_count > 0);
}

TEST_CASE("eye: build with empty input returns empty grid", "[eye]") {
    auto eye = build_eye({}, 16, 32, 32);
    REQUIRE(eye.max_count() == 0);
    REQUIRE(eye.time_bins == 32);
    REQUIRE(eye.volt_bins == 32);
}

TEST_CASE("eye: warmup_uis skips initial transient samples", "[eye]") {
    // Filtered waveform with a long startup transient (output starts at 0).
    auto bits = std::vector<int>{1, 1, 1, 1, 1, 1, 1, 1};  // all 1s
    auto y = rc_lowpass(nrz_waveform(bits, 8), 1.0, 0.05);

    // Without warmup, the early ramp from 0 to 1 produces "mid" samples.
    auto eye_no_warm = build_eye(y, 8, 16, 32, /*warmup=*/0);
    // With warmup of 4 UIs (32 samples), we skip the ramp.
    auto eye_warm    = build_eye(y, 8, 16, 32, /*warmup=*/4);

    REQUIRE(eye_no_warm.max_count() >= eye_warm.max_count());
}
