// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Radix-2 Cooley-Tukey FFT for power-of-2 sizes.
//
// Hand-rolled to keep sikit free of external FFT dependencies (no FFTW,
// no kissfft). Plenty fast for the typical SI problem size (N = 1024 …
// 65536 samples). If we ever need bigger or non-power-of-2 sizes, this
// is the natural place to swap in PocketFFT or FFTW behind the same API.

#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace sikit::dsp {

// In-place radix-2 FFT. `data.size()` must be a positive power of 2.
// inverse=false: standard forward DFT (no normalization).
// inverse=true:  inverse DFT with 1/N normalization, so fft(fft(x), true) == x
//                up to floating-point round-off.
void fft(std::vector<std::complex<double>>& data, bool inverse = false);

// Smallest power-of-2 ≥ n. Returns 1 for n ≤ 1.
std::size_t next_power_of_2(std::size_t n);

}  // namespace sikit::dsp
