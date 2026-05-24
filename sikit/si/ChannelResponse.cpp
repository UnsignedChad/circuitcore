#include "si/ChannelResponse.h"

#include <algorithm>
#include <stdexcept>

#include "si/Fft.h"

namespace sikit::dsp {

using Complex = std::complex<double>;

std::complex<double> interpolate_s21(
    const touchstone::TouchstoneFile& channel, double freq_hz) {
    if (channel.num_ports != 2) {
        throw std::invalid_argument("interpolate_s21 requires a 2-port file");
    }
    const auto& freqs = channel.frequencies;
    if (freqs.empty()) return Complex(1.0, 0.0);

    // Strictly below the bottom of the sweep → treat as ideal passthrough.
    if (freq_hz < freqs.front()) return Complex(1.0, 0.0);

    // At/above the top → clamp to the last value.
    if (freq_hz >= freqs.back()) {
        // 2-port column-major: S21 is at flat index 1.
        return channel.s_matrices.back()[1];
    }

    // upper_bound gives the first index strictly greater than freq_hz.
    // freq_hz == freqs.front() lands here with hi=1, lo=0, t=0 → exact value.
    auto it = std::upper_bound(freqs.begin(), freqs.end(), freq_hz);
    const std::size_t hi = static_cast<std::size_t>(it - freqs.begin());
    const std::size_t lo = hi - 1;

    const double f_lo = freqs[lo];
    const double f_hi = freqs[hi];
    const double t = (freq_hz - f_lo) / (f_hi - f_lo);

    const Complex s_lo = channel.s_matrices[lo][1];
    const Complex s_hi = channel.s_matrices[hi][1];
    return s_lo + (s_hi - s_lo) * t;
}

std::vector<double> apply_channel(const std::vector<double>& tx,
                                   double tx_sample_rate_hz,
                                   const touchstone::TouchstoneFile& channel) {
    if (tx.empty()) return {};
    if (tx_sample_rate_hz <= 0.0) {
        throw std::invalid_argument("tx_sample_rate_hz must be > 0");
    }
    if (channel.num_ports != 2) {
        throw std::invalid_argument("apply_channel requires a 2-port Touchstone file");
    }

    // Pad to the next power of 2 for FFT.
    const std::size_t N = next_power_of_2(tx.size());
    std::vector<Complex> spec(N, Complex(0.0, 0.0));
    for (std::size_t i = 0; i < tx.size(); ++i) {
        spec[i] = Complex(tx[i], 0.0);
    }

    fft(spec, /*inverse=*/false);

    // Apply S21(f) across the spectrum. For real input, FFT output is
    // Hermitian-symmetric: spec[k] = conj(spec[N-k]). We interpolate S21
    // on the positive-frequency side and mirror to preserve that symmetry.
    const double df = tx_sample_rate_hz / static_cast<double>(N);
    spec[0] *= Complex(1.0, 0.0);  // DC: passthrough by convention
    const std::size_t half = N / 2;
    for (std::size_t k = 1; k < half; ++k) {
        const double f = df * static_cast<double>(k);
        const Complex h = interpolate_s21(channel, f);
        spec[k]      *= h;
        spec[N - k]  *= std::conj(h);  // mirror for Hermitian symmetry
    }
    // Nyquist bin (only present for even N): purely real, use real-valued
    // gain to keep the IFFT output real.
    if (N % 2 == 0) {
        const double f_nyq = df * static_cast<double>(half);
        const Complex h_nyq = interpolate_s21(channel, f_nyq);
        spec[half] *= Complex(h_nyq.real(), 0.0);
    }

    fft(spec, /*inverse=*/true);

    std::vector<double> rx(tx.size());
    for (std::size_t i = 0; i < tx.size(); ++i) {
        rx[i] = spec[i].real();
    }
    return rx;
}

}  // namespace sikit::dsp
