#include "si/Connector.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace sikit::si {

namespace {

using Complex = std::complex<double>;

constexpr double kC0 = 2.99792458e8;
constexpr double kEpsR_PTFE = 2.1;     // typical SMA dielectric
constexpr double kEpsR_FR4  = 4.3;     // typical PCB trace dielectric

// Linear IL in dB at frequency f given the parametric shape +
// optional notch. Returned value is positive (magnitude of loss).
double il_db(const ConnectorSpec& s, double f_hz) {
    const double f_ghz = f_hz / 1e9;
    double il = s.il_constant_db + s.il_slope_db_per_sqrt_ghz *
                                       std::sqrt(std::max(f_ghz, 1e-9));
    if (s.notch_freq_hz > 0.0 && s.notch_depth_db > 0.0) {
        // Lorentzian-shaped dip at notch_freq_hz with Q determining
        // the half-width. (f/f0 - f0/f) is the standard normalised
        // detuning used in resonator IL approximations.
        const double f0 = s.notch_freq_hz;
        const double x  = (f_hz / f0 - f0 / std::max(f_hz, 1e-9));
        const double denom = 1.0 + std::pow(s.notch_q * x, 2);
        il += s.notch_depth_db / denom;
    }
    return std::min(il, 60.0);     // cap absurd extrapolation
}

Complex s21_at(const ConnectorSpec& s, double f_hz, double v_p) {
    const double mag = std::pow(10.0, -il_db(s, f_hz) / 20.0);
    // Phase from a flat electrical-delay model: phase = -2*pi*f*L/v_p
    const double phase = -2.0 * std::numbers::pi * f_hz *
                          s.electrical_length_m / v_p;
    return std::polar(mag, phase);
}

// Build a 2-port column-major flat record.
std::vector<Complex> two_port_flat(Complex s11, Complex s21,
                                     Complex s12, Complex s22) {
    return {s11, s21, s12, s22};
}

// Couple of helper presets we expose individually below.
ConnectorSpec make_sma_like(const std::string& name,
                             double il_slope, double rl_db) {
    ConnectorSpec s;
    s.name = name;
    s.num_ports = 2;
    s.il_slope_db_per_sqrt_ghz = il_slope;
    s.il_constant_db = 0.05;
    s.rl_db = rl_db;
    s.electrical_length_m = 0.012;
    s.reference_impedance = 50.0;
    return s;
}

ConnectorSpec make_diff_pair(const std::string& name, double il_slope,
                              double rl_db, double xtalk_db,
                              double mode_conv_db,
                              double notch_freq_hz = 0.0,
                              double notch_depth_db = 0.0) {
    ConnectorSpec s;
    s.name = name;
    s.num_ports = 4;
    s.il_slope_db_per_sqrt_ghz = il_slope;
    s.il_constant_db = 0.10;
    s.rl_db = rl_db;
    s.electrical_length_m = 0.020;
    s.diff_xtalk_db = xtalk_db;
    s.mode_conv_db = mode_conv_db;
    s.notch_freq_hz = notch_freq_hz;
    s.notch_depth_db = notch_depth_db;
    s.reference_impedance = 50.0;   // single-ended; user converts to diff
    return s;
}

}  // namespace

ConnectorSpec preset_sma_edge_launch() {
    return make_sma_like("SMA edge launch (placeholder)", 0.04, 25.0);
}

ConnectorSpec preset_sma_panel_mount() {
    return make_sma_like("SMA panel mount (placeholder)", 0.07, 20.0);
}

ConnectorSpec preset_usb_c_diff_pair() {
    // USB 3.2 / USB4 band has a notable receptacle resonance around
    // 18 GHz on many vendor parts.
    return make_diff_pair("USB-C diff pair (placeholder)",
                           0.12, 18.0, -38.0, -45.0,
                           18e9, 2.0);
}

ConnectorSpec preset_rj45_diff_pair() {
    // RJ45 magnetics modules show more mode conversion than coax-style
    // connectors -- they are wirewound transformers.
    return make_diff_pair("RJ45 (placeholder)",
                           0.20, 14.0, -28.0, -32.0);
}

ConnectorSpec preset_samtec_btb() {
    return make_sma_like("Samtec board-to-board (placeholder)",
                          0.06, 22.0);
}

std::vector<std::string> available_connector_presets() {
    return {
        preset_sma_edge_launch().name,
        preset_sma_panel_mount().name,
        preset_usb_c_diff_pair().name,
        preset_rj45_diff_pair().name,
        preset_samtec_btb().name,
    };
}

ConnectorSpec connector_preset_by_name(const std::string& name) {
    if (name == preset_sma_edge_launch().name) return preset_sma_edge_launch();
    if (name == preset_sma_panel_mount().name) return preset_sma_panel_mount();
    if (name == preset_usb_c_diff_pair().name) return preset_usb_c_diff_pair();
    if (name == preset_rj45_diff_pair().name)  return preset_rj45_diff_pair();
    if (name == preset_samtec_btb().name)      return preset_samtec_btb();
    throw std::runtime_error(std::format(
        "connector_preset_by_name: no preset named '{}'", name));
}

sikit::touchstone::TouchstoneFile generate_connector_touchstone(
    const ConnectorSpec& spec, const std::vector<double>& freq_hz) {
    if (freq_hz.empty()) {
        throw std::runtime_error(
            "generate_connector_touchstone: empty frequency grid");
    }
    if (spec.num_ports != 2 && spec.num_ports != 4) {
        throw std::runtime_error(
            "generate_connector_touchstone: only 2-port and 4-port models supported");
    }
    if (!std::is_sorted(freq_hz.begin(), freq_hz.end())) {
        throw std::runtime_error(
            "generate_connector_touchstone: frequencies must be sorted");
    }
    if (freq_hz.front() <= 0.0) {
        throw std::runtime_error(
            "generate_connector_touchstone: frequencies must be positive");
    }

    sikit::touchstone::TouchstoneFile out;
    out.num_ports = spec.num_ports;
    out.reference_impedance = spec.reference_impedance;
    out.format = sikit::touchstone::Format::RealImaginary;
    out.frequency_scale = 1.0;
    out.frequencies = freq_hz;
    out.s_matrices.reserve(freq_hz.size());

    const double rl_mag = std::pow(10.0, -spec.rl_db / 20.0);
    // Phase velocity used for delay: PTFE for SMA-like, FR-4 for diff
    // connectors. Distinction matters for absolute delay but not for
    // the IL or RL shape, so this is a small fidelity wrinkle.
    const double v_p = (spec.num_ports == 2)
                           ? kC0 / std::sqrt(kEpsR_PTFE)
                           : kC0 / std::sqrt(kEpsR_FR4);

    for (double f : freq_hz) {
        const Complex s21 = s21_at(spec, f, v_p);
        const Complex s11(rl_mag, 0);
        if (spec.num_ports == 2) {
            // Reciprocal + symmetric: S12 = S21, S22 = S11.
            out.s_matrices.push_back(two_port_flat(s11, s21, s21, s11));
            continue;
        }

        // ---- 4-port differential pair ----
        // Port convention: [P_near, N_near, P_far, N_far] (PNPN), matching
        // the sparam::PortOrder::PNPN convention.
        // S21 (port 1 -> 2): P_near -> N_near (zero, the two halves of
        // the same pair are not connected through-traffic).
        // The actual through is S31 (P_near -> P_far) and S42 (N_near -> N_far).
        // Cross-pair coupling: S41 (P_near -> N_far) and S32 (N_near -> P_far)
        // at the diff_xtalk_db level (Lorentzian-flat for v1).
        // Mode conversion: S31's even-mode response slightly different from
        // odd-mode -> represented as a small same-direction common-mode
        // term we fold into S21 / S12 (the within-pair near-end leakage).
        const double f_ghz = std::max(f / 1e9, 1e-9);
        const double xtalk_mag = std::pow(10.0, spec.diff_xtalk_db / 20.0) *
                                  std::sqrt(f_ghz);
        const double conv_mag  = std::pow(10.0, spec.mode_conv_db / 20.0) *
                                  std::sqrt(f_ghz);
        const Complex zero(0, 0);

        std::vector<Complex> m(16, zero);
        auto set = [&](int r, int c, Complex v) { m[r + c * 4] = v; };
        set(0, 0, s11);            // S11
        set(2, 2, s11);            // S33 (P_far self)
        set(1, 1, s11);            // S22 (N_near self)
        set(3, 3, s11);            // S44 (N_far self)
        set(2, 0, s21);            // S31: P_near -> P_far
        set(0, 2, s21);            // S13
        set(3, 1, s21);            // S42: N_near -> N_far
        set(1, 3, s21);            // S24
        set(3, 0, Complex(xtalk_mag, 0));        // S41: P_near -> N_far (FEXT)
        set(0, 3, Complex(xtalk_mag, 0));
        set(2, 1, Complex(xtalk_mag, 0));        // S32: N_near -> P_far
        set(1, 2, Complex(xtalk_mag, 0));
        set(1, 0, Complex(conv_mag, 0));         // S21: within-pair near-end leak
        set(0, 1, Complex(conv_mag, 0));
        set(3, 2, Complex(conv_mag, 0));         // S43: same on the far end
        set(2, 3, Complex(conv_mag, 0));
        out.s_matrices.push_back(std::move(m));
    }
    return out;
}

}  // namespace sikit::si
