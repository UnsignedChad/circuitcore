// Multi-aggressor time-domain crosstalk simulation.
//
// What this models, and why it's distinct from the existing frequency-
// domain crosstalk math:
//
//   The em2d::compute_rlgc() pass extracts the multi-conductor RLGC
//   matrix from a 2-D cross-section and yields the modal S-parameters
//   (DiffSynth.cpp does this for two-trace coupled pairs). That gives
//   the right *magnitude* of NEXT/FEXT versus frequency, but says
//   nothing about the time-domain superposition you see at the victim
//   sampler when 1-3 aggressor traces happen to be switching with their
//   own uncorrelated data patterns. Eye closure due to crosstalk is
//   inherently a time-domain phenomenon: a steady-state |S_xtalk| of
//   -30 dB sounds harmless until you realise that in the worst alignment
//   the contributions from three aggressors stack constructively at
//   exactly your decision instant.
//
//   This module bridges the two by re-running each aggressor as an
//   independent PRBS-7 NRZ stream through its aggressor-to-victim
//   coupling S-parameter file, then summing the contributions onto the
//   victim's RX waveform before the eye-folding pass. It complements
//   PDA / statistical-eye (StatEye.h), which gives the *deterministic
//   worst-case* envelope under the assumption that every aggressor
//   bit is chosen adversarially. Both views are useful; this one shows
//   what a typical scope capture looks like.
//
// Reference: Stojanovic, "Channel-Limited High-Speed Links: Modeling,
// Analysis and Design" (2008), ch. 4 on time-domain crosstalk
// superposition. Also Hall & Heck, "Advanced Signal Integrity for
// High-Speed Digital Designs", ch. 11 (multi-line crosstalk).
//
// Scope of v1 (and what's deliberately not here):
//
//   * Victim and aggressors are NRZ PRBS-7. PAM4 is a future
//     extension that requires extending eye::nrz_waveform first.
//   * Each aggressor has an independent PRBS phase offset; the
//     defaults stagger them at 0.5 UI (worst-realistic alignment for
//     two-aggressor) and at 0.33 / 0.66 UI for three aggressors. The
//     caller can override.
//   * Aggressor TX edge time matches the victim TX edge time. Mixed-
//     edge-rate aggressors would just take a per-aggressor ramp_frac.
//   * No receiver equalisation. Run the result through an AMI RX in
//     the calling code (MainWindow::applyAmiIfLoaded) if you want
//     equalised crosstalk eyes.

#pragma once

#include <vector>

#include "si/Eye.h"
#include "si/Touchstone.h"

namespace sikit::analysis {

struct CrosstalkScenario {
    // Victim's own through-channel S21. 2-port Touchstone.
    sikit::touchstone::TouchstoneFile victim_thru;

    // One 2-port Touchstone per aggressor describing the path from
    // that aggressor's TX driver to the victim's RX sampling point.
    // Conventionally this is the FEXT coupling at the victim's far
    // end (aggressor TX -> victim RX path).
    std::vector<sikit::touchstone::TouchstoneFile> aggressor_to_victim_coupling;

    // Per-aggressor PRBS phase offset, in fractions of a UI in [0, 1).
    // If shorter than aggressor_to_victim_coupling, the trailing
    // aggressors use a default staggered phase (i / (N+1) UI).
    std::vector<double> aggressor_phase_offsets_ui;
};

// Generate the victim-side eye diagram with all aggressors active.
//
// baud_hz       : bit rate of all traces (victim and aggressors share clock)
// n_bits        : PRBS length (>= 256 advised for noisy eye, 2000+ for clean)
// samples_per_ui: time resolution per UI; 32 is the conventional default
// eye_t_bins    : horizontal eye grid resolution (typ 128)
// eye_v_bins    : vertical eye grid resolution (typ 96)
// warmup_ui     : UIs of head-of-stream to skip before folding (lets the
//                 channel impulse response decay through the initial
//                 transient; typ 8)
sikit::eye::EyeGrid simulate_crosstalk_eye(
    const CrosstalkScenario& scenario,
    double baud_hz,
    int n_bits = 2000,
    int samples_per_ui = 32,
    int eye_t_bins = 128,
    int eye_v_bins = 96,
    int warmup_ui = 8);

// Convenience extractor for diff-pair .s4p files. Treats the P trace as
// the victim and the N trace as the single aggressor; pulls the
// near-victim-to-far-victim path (S21 in PNPN port order, also known as
// S31 in PPNN) as victim_thru, and the aggressor-TX-to-victim-RX path
// as the coupling. Port-order is the same convention used elsewhere in
// the SParam library.
//
// Two minor caveats baked into this v1:
//   1. The extracted "2-port" Touchstones have S11 = S22 = 0 and
//      S12 = S21. This drops back-reflections at the endpoints; for
//      a matched-termination scenario this is exact, and for the
//      typical 50-ohm reference impedance used by DiffSynth it's a
//      very mild approximation.
//   2. Symmetric crosstalk only -- the aggressor sees the victim too,
//      but we model only one direction. PRBS-7 patterns are long enough
//      that bidirectional coupling washes out in the eye fold for
//      tightly coupled pairs.
enum class S4pPortOrder {
    PNPN,  // [P_near, N_near, P_far, N_far]
    PPNN,  // [P_near, P_far, N_near, N_far]
};

CrosstalkScenario diff_pair_to_scenario(
    const sikit::touchstone::TouchstoneFile& s4p,
    S4pPortOrder order = S4pPortOrder::PNPN);

}  // namespace sikit::analysis
