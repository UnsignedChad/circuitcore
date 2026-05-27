// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/Fft.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace sikit::dsp {

namespace {

bool is_power_of_2(std::size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

}  // namespace

std::size_t next_power_of_2(std::size_t n) {
    if (n <= 1) return 1;
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void fft(std::vector<std::complex<double>>& data, bool inverse) {
    const std::size_t N = data.size();
    if (N <= 1) return;
    if (!is_power_of_2(N)) {
        throw std::invalid_argument("fft requires a power-of-2 size");
    }

    // Bit-reversal permutation: swap each element with its bit-reversed index.
    std::size_t j = 0;
    for (std::size_t i = 1; i < N; ++i) {
        std::size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // Iterative Cooley-Tukey butterflies. Stage size doubles each pass.
    const double sign = inverse ? +1.0 : -1.0;
    for (std::size_t len = 2; len <= N; len <<= 1) {
        const double theta = sign * 2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(theta), std::sin(theta));
        for (std::size_t i = 0; i < N; i += len) {
            std::complex<double> w(1.0, 0.0);
            const std::size_t half = len >> 1;
            for (std::size_t k = 0; k < half; ++k) {
                const auto u = data[i + k];
                const auto v = data[i + k + half] * w;
                data[i + k]        = u + v;
                data[i + k + half] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        const double inv_n = 1.0 / static_cast<double>(N);
        for (auto& z : data) z *= inv_n;
    }
}

}  // namespace sikit::dsp
