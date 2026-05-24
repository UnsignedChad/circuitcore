#include "si/Topology.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <format>
#include <numbers>
#include <stdexcept>

#include "si/SParam.h"

namespace sikit::si {

using Complex = std::complex<double>;

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// ABCD -> S, reference impedance Z_r. Pozar eq. 4.45. Duplicated from
// ViaModel and DiffSynth so the topology cascade doesn't have to plumb
// across modules; same well-tested formula.
Eigen::Matrix2cd abcd_to_s(Complex A, Complex B, Complex C, Complex D,
                            double Zr) {
    const Complex denom = A + B / Zr + C * Zr + D;
    Eigen::Matrix2cd s;
    s(0, 0) = (A + B / Zr - C * Zr - D) / denom;            // S11
    s(0, 1) = Complex(2, 0) * (A * D - B * C) / denom;       // S12
    s(1, 0) = Complex(2, 0) / denom;                          // S21
    s(1, 1) = (-A + B / Zr - C * Zr + D) / denom;            // S22
    return s;
}

// Linear interpolation of a complex value across two frequency knots.
Complex lerp_c(Complex a, Complex b, double t) {
    return a * (1.0 - t) + b * t;
}

// Build a 2x2 S-matrix by interpolating each entry across the
// Touchstone grid. Used by TouchstoneBlock so its s_at() can return a
// continuous response when the requested frequency does not coincide
// with a grid sample.
Eigen::Matrix2cd interp_2port_s(const touchstone::TouchstoneFile& ts,
                                  double freq_hz) {
    Eigen::Matrix2cd s = Eigen::Matrix2cd::Zero();
    if (ts.frequencies.empty() || ts.num_ports != 2) return s;

    if (freq_hz <= ts.frequencies.front()) {
        // Extrapolate downward as the first sample. SI channels are
        // smooth-ish at the low end; clamping is safer than a real
        // extrapolation when the user supplies a band-limited file.
        const auto& m = ts.s_matrices.front();
        s(0, 0) = m[0]; s(1, 0) = m[1]; s(0, 1) = m[2]; s(1, 1) = m[3];
        return s;
    }
    if (freq_hz >= ts.frequencies.back()) {
        const auto& m = ts.s_matrices.back();
        s(0, 0) = m[0]; s(1, 0) = m[1]; s(0, 1) = m[2]; s(1, 1) = m[3];
        return s;
    }

    // Find the segment containing freq_hz. Binary search.
    auto it = std::upper_bound(ts.frequencies.begin(),
                                 ts.frequencies.end(), freq_hz);
    const std::size_t hi = it - ts.frequencies.begin();
    const std::size_t lo = hi - 1;
    const double t = (freq_hz - ts.frequencies[lo]) /
                     (ts.frequencies[hi] - ts.frequencies[lo]);
    const auto& a = ts.s_matrices[lo];
    const auto& b = ts.s_matrices[hi];
    s(0, 0) = lerp_c(a[0], b[0], t);
    s(1, 0) = lerp_c(a[1], b[1], t);
    s(0, 1) = lerp_c(a[2], b[2], t);
    s(1, 1) = lerp_c(a[3], b[3], t);
    return s;
}

}  // namespace

// ----- TraceBlock -------------------------------------------------------

TraceBlock::TraceBlock(double width_m, double length_m, int layer_ordinal,
                         sikit::analysis::AnalysisStackup stackup, sikit::analysis::Engine engine) {
    spec_.trace_width = width_m;
    spec_.length_m = length_m;
    spec_.layer_ordinal = layer_ordinal;
    spec_.stackup = std::move(stackup);
    spec_.engine = engine;
}

std::string TraceBlock::label() const {
    return std::format("Trace W={:.3f}mm L={:.1f}mm",
                        spec_.trace_width * 1e3, spec_.length_m * 1e3);
}

Eigen::Matrix2cd TraceBlock::s_at(double freq_hz, double z_ref) const {
    // synthesize_channel evaluates the full propagation at every
    // requested frequency. For a single freq we just pass one element.
    auto ts = sikit::analysis::synthesize_channel(spec_, {freq_hz}, z_ref);
    Eigen::Matrix2cd s;
    const auto& m = ts.s_matrices.front();
    s(0, 0) = m[0]; s(1, 0) = m[1]; s(0, 1) = m[2]; s(1, 1) = m[3];
    return s;
}

// ----- ViaBlock ---------------------------------------------------------

ViaBlock::ViaBlock(sikit::analysis::ViaSpec spec) : spec_(std::move(spec)) {}

std::string ViaBlock::label() const {
    return std::format("Via d={:.2f}mm L={:.2f}mm",
                        spec_.drill_diameter * 1e3,
                        spec_.total_length    * 1e3);
}

Eigen::Matrix2cd ViaBlock::s_at(double freq_hz, double z_ref) const {
    auto ts = sikit::analysis::compute_via_s2p(spec_, {freq_hz}, z_ref);
    Eigen::Matrix2cd s;
    const auto& m = ts.s_matrices.front();
    s(0, 0) = m[0]; s(1, 0) = m[1]; s(0, 1) = m[2]; s(1, 1) = m[3];
    return s;
}

// ----- LumpedBlock ------------------------------------------------------

LumpedBlock::LumpedBlock(Topology t, double value)
    : topology_(t), value_(value) {}

std::string LumpedBlock::label() const {
    const char* kind = "";
    switch (topology_) {
        case Topology::SeriesR: kind = "Series R"; break;
        case Topology::SeriesL: kind = "Series L"; break;
        case Topology::SeriesC: kind = "Series C"; break;
        case Topology::ShuntR:  kind = "Shunt R";  break;
        case Topology::ShuntL:  kind = "Shunt L";  break;
        case Topology::ShuntC:  kind = "Shunt C";  break;
    }
    return std::format("{} = {:.3g}", kind, value_);
}

Eigen::Matrix2cd LumpedBlock::s_at(double freq_hz, double z_ref) const {
    const double w = kTwoPi * freq_hz;
    const Complex jw(0, w);
    // Series element: impedance Z appears between input and output;
    // ABCD = [[1, Z], [0, 1]].
    // Shunt element: admittance Y appears between signal and ground;
    // ABCD = [[1, 0], [Y, 1]].
    Complex A(1, 0), B(0, 0), C(0, 0), D(1, 0);
    switch (topology_) {
        case Topology::SeriesR: B = Complex(value_, 0); break;
        case Topology::SeriesL: B = jw * value_; break;
        case Topology::SeriesC: B = Complex(1.0, 0) / (jw * value_); break;
        case Topology::ShuntR:  C = Complex(1.0, 0) / Complex(value_, 0); break;
        case Topology::ShuntL:  C = Complex(1.0, 0) / (jw * value_); break;
        case Topology::ShuntC:  C = jw * value_; break;
    }
    return abcd_to_s(A, B, C, D, z_ref);
}

// ----- TouchstoneBlock --------------------------------------------------

TouchstoneBlock::TouchstoneBlock(touchstone::TouchstoneFile ts,
                                   std::string label)
    : ts_(std::move(ts)), label_(std::move(label)) {
    if (ts_.num_ports != 2) {
        throw std::runtime_error(
            "TouchstoneBlock requires a 2-port Touchstone file");
    }
}

std::string TouchstoneBlock::label() const { return label_; }

Eigen::Matrix2cd TouchstoneBlock::s_at(double freq_hz, double /*z_ref*/) const {
    // We interpolate the file's S-matrix at the requested frequency.
    // The reference impedance baked into the Touchstone may differ
    // from z_ref; we do NOT renormalize for v1 (the cascade will
    // simply mismatch slightly). Full renormalization between
    // arbitrary Z_refs is a possible v2 extension.
    return interp_2port_s(ts_, freq_hz);
}

// ----- IdealLineBlock ---------------------------------------------------

IdealLineBlock::IdealLineBlock(double z0, double length_m, double v_phase)
    : z0_(z0), length_m_(length_m), v_phase_(v_phase) {}

std::string IdealLineBlock::label() const {
    return std::format("Line Z0={:.1f}Ω L={:.1f}mm",
                        z0_, length_m_ * 1e3);
}

Eigen::Matrix2cd IdealLineBlock::s_at(double freq_hz, double z_ref) const {
    // Lossless transmission line ABCD:
    //   A = D = cos(beta * length)
    //   B = j * Z0 * sin(beta * length)
    //   C = j * sin(beta * length) / Z0
    const double beta = kTwoPi * freq_hz / v_phase_;
    const double bl = beta * length_m_;
    const Complex A(std::cos(bl), 0);
    const Complex D(std::cos(bl), 0);
    const Complex B(0, z0_ * std::sin(bl));
    const Complex C(0, std::sin(bl) / z0_);
    return abcd_to_s(A, B, C, D, z_ref);
}

// ----- Channel ----------------------------------------------------------

void Channel::add(std::unique_ptr<ChannelBlock> block) {
    blocks_.push_back(std::move(block));
}

void Channel::clear() { blocks_.clear(); }

touchstone::TouchstoneFile Channel::cascade(
    const std::vector<double>& freqs, double z_ref) const {
    touchstone::TouchstoneFile out;
    out.num_ports = 2;
    out.format = touchstone::Format::RealImaginary;
    out.reference_impedance = z_ref;
    out.frequency_scale = 1.0;
    out.frequencies = freqs;
    out.s_matrices.reserve(freqs.size());

    for (double f : freqs) {
        // Walk blocks left to right, accumulating T = T_0 * T_1 * ... * T_N.
        // Empty channel: T stays identity, which decodes to a 2-port
        // passthrough.
        Eigen::Matrix2cd t_total = Eigen::Matrix2cd::Identity();
        for (const auto& b : blocks_) {
            const auto s = b->s_at(f, z_ref);
            t_total = t_total * sikit::sparam::s_to_t(s);
        }
        const auto s_total = sikit::sparam::t_to_s(t_total);
        std::vector<Complex> flat(4);
        // Column-major: [S11, S21, S12, S22].
        flat[0] = s_total(0, 0);
        flat[1] = s_total(1, 0);
        flat[2] = s_total(0, 1);
        flat[3] = s_total(1, 1);
        out.s_matrices.push_back(std::move(flat));
    }
    return out;
}

}  // namespace sikit::si
