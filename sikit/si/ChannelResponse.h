// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Apply a Touchstone S21 channel to a time-domain TX waveform via the
// frequency-domain multiplication trick:
//
//     RX(t) = IFFT( FFT(TX) · S21(f) )
//
// S21 is interpolated onto the TX spectrum's frequency grid. Frequencies
// outside the Touchstone range are extrapolated conservatively: below the
// Touchstone minimum we assume ideal passthrough (S21 = 1); above the
// maximum we clamp to the last value (better than zeroing — most
// real channels are smooth at the top end and zeroing creates ringing).

#pragma once

#include <complex>
#include <vector>

#include "si/Touchstone.h"

namespace sikit::dsp {

// Linearly interpolate a 2-port Touchstone file's S21 at `freq_hz` (in Hz).
// Returns 1+0j for freq_hz ≤ channel.frequencies.front(), clamps to the
// last value for freq_hz ≥ channel.frequencies.back().
std::complex<double> interpolate_s21(const touchstone::TouchstoneFile& channel,
                                      double freq_hz);

// Apply the channel to a TX waveform. Pads to the next power of 2 for the
// FFT, performs the spectral multiply with S21 interpolated onto the FFT
// frequency grid, IFFTs, and trims back to `tx.size()`.
std::vector<double> apply_channel(
    const std::vector<double>& tx,
    double tx_sample_rate_hz,
    const touchstone::TouchstoneFile& channel);

}  // namespace sikit::dsp
