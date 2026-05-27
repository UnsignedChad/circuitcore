// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/Crosstalk.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <stdexcept>
#include <utility>

#include "si/ChannelResponse.h"

namespace sikit::analysis {

namespace {

using Complex = std::complex<double>;

// Build a "2-port slice" of an N-port Touchstone for use with
// apply_channel. The slice keeps the freq grid + reference impedance but
// folds a single src->dst column of the N-port into the 2-port S21
// position. Back-reflections (S11/S22) are zeroed and S12 is set equal
// to S21 (reciprocity); see header for why this is an acceptable
// approximation in matched-termination scenarios.
sikit::touchstone::TouchstoneFile extract_two_port_slice(
    const sikit::touchstone::TouchstoneFile& src,
    int src_port, int dst_port) {
    sikit::touchstone::TouchstoneFile out;
    out.num_ports = 2;
    out.format = sikit::touchstone::Format::RealImaginary;
    out.reference_impedance = src.reference_impedance;
    out.frequency_scale = src.frequency_scale;
    out.frequencies = src.frequencies;
    out.s_matrices.reserve(src.frequencies.size());

    const int N = src.num_ports;
    for (const auto& m : src.s_matrices) {
        // Touchstone storage is column-major: m[row + col * N].
        const Complex s_dst_src = m[dst_port + src_port * N];
        std::vector<Complex> two_port(4, Complex(0, 0));
        // 2-port column-major: [S11, S21, S12, S22].
        two_port[0] = Complex(0, 0);             // S11
        two_port[1] = s_dst_src;                  // S21 (TX -> RX)
        two_port[2] = s_dst_src;                  // S12 (reciprocal)
        two_port[3] = Complex(0, 0);             // S22
        out.s_matrices.push_back(std::move(two_port));
    }
    return out;
}

// Time-rotate a sample buffer by `samples` positions (cyclic shift).
// Used to give each aggressor an independent PRBS phase relative to the
// victim without re-running prbs7() with a different seed.
void cyclic_shift(std::vector<double>& v, int samples) {
    if (v.empty() || samples == 0) return;
    int n = static_cast<int>(v.size());
    samples = ((samples % n) + n) % n;
    if (samples == 0) return;
    std::rotate(v.begin(), v.begin() + samples, v.end());
}

}  // namespace

sikit::eye::EyeGrid simulate_crosstalk_eye(
    const CrosstalkScenario& scenario,
    double baud_hz, int n_bits, int samples_per_ui,
    int eye_t_bins, int eye_v_bins, int warmup_ui) {
    if (baud_hz <= 0.0 || n_bits <= 0 || samples_per_ui <= 1) {
        throw std::runtime_error(
            "simulate_crosstalk_eye: invalid baud/bits/samples_per_ui");
    }
    if (scenario.victim_thru.num_ports != 2 ||
        scenario.victim_thru.frequencies.empty()) {
        throw std::runtime_error(
            "simulate_crosstalk_eye: victim_thru must be a populated 2-port");
    }
    for (const auto& c : scenario.aggressor_to_victim_coupling) {
        if (c.num_ports != 2 || c.frequencies.empty()) {
            throw std::runtime_error(
                "simulate_crosstalk_eye: each aggressor coupling must be a "
                "populated 2-port");
        }
    }

    const double fs = baud_hz * samples_per_ui;

    // Victim TX, channel, RX.
    auto victim_bits = sikit::eye::prbs7(n_bits);
    auto victim_tx   = sikit::eye::nrz_waveform(victim_bits, samples_per_ui);
    auto victim_rx   = sikit::dsp::apply_channel(victim_tx, fs,
                                                  scenario.victim_thru);

    // Sum aggressor contributions onto victim_rx.
    const int n_agg = static_cast<int>(
        scenario.aggressor_to_victim_coupling.size());
    for (int i = 0; i < n_agg; ++i) {
        // Each aggressor gets its own PRBS phase, achieved by cyclic-
        // shifting the same PRBS-7 sequence. Using the same sequence
        // shifted means we don't re-roll RNG state and the result is
        // deterministic across runs.
        auto agg_bits = sikit::eye::prbs7(n_bits);
        // Shift by a per-aggressor bit offset that's coprime to n_bits
        // (using 31 as a Mersenne prime) so the aggressor patterns don't
        // re-align with the victim every few UIs.
        const int bit_shift = (31 * (i + 1)) % n_bits;
        std::rotate(agg_bits.begin(),
                    agg_bits.begin() + bit_shift,
                    agg_bits.end());
        auto agg_tx = sikit::eye::nrz_waveform(agg_bits, samples_per_ui);

        // Optional sub-UI phase offset, in samples.
        const double phase_ui =
            (i < static_cast<int>(scenario.aggressor_phase_offsets_ui.size()))
                ? scenario.aggressor_phase_offsets_ui[i]
                : static_cast<double>(i + 1) / (n_agg + 1);
        const int phase_samples =
            static_cast<int>(std::round(phase_ui * samples_per_ui))
            % samples_per_ui;
        cyclic_shift(agg_tx, phase_samples);

        // Apply this aggressor's coupling channel.
        auto noise = sikit::dsp::apply_channel(
            agg_tx, fs, scenario.aggressor_to_victim_coupling[i]);

        // Sum onto the victim RX.
        const std::size_t n = std::min(noise.size(), victim_rx.size());
        for (std::size_t j = 0; j < n; ++j) victim_rx[j] += noise[j];
    }

    return sikit::eye::build_eye(victim_rx, samples_per_ui,
                                  eye_t_bins, eye_v_bins, warmup_ui);
}

CrosstalkScenario diff_pair_to_scenario(
    const sikit::touchstone::TouchstoneFile& s4p,
    S4pPortOrder order) {
    if (s4p.num_ports != 4) {
        throw std::runtime_error(
            "diff_pair_to_scenario: input must be a 4-port file");
    }
    int p_near, p_far, n_near, n_far;
    if (order == S4pPortOrder::PNPN) {
        // [P_near, N_near, P_far, N_far]
        p_near = 0; n_near = 1; p_far = 2; n_far = 3;
    } else {
        // [P_near, P_far, N_near, N_far]
        p_near = 0; p_far = 1; n_near = 2; n_far = 3;
    }

    CrosstalkScenario sc;
    // Victim through-path: P_near -> P_far.
    sc.victim_thru = extract_two_port_slice(s4p, p_near, p_far);
    // Aggressor (N trace) -> victim RX (P_far): TX is N_near, RX is P_far.
    sc.aggressor_to_victim_coupling.push_back(
        extract_two_port_slice(s4p, n_near, p_far));
    return sc;
}

}  // namespace sikit::analysis
