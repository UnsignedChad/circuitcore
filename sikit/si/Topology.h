// Topology editor data model.
//
// What this exists for
//
//   sikit's existing channel pipeline assumes the user has a single
//   board net and wants the S-parameters end-to-end across that net.
//   Real SI analyses are rarely that simple: a serial link runs from
//   the silicon die, through a package, out a BGA ball, through a via
//   to inner-layer routing, down a trace to a connector, across a
//   cable, up a via on the other side, through more routing to another
//   package, into the receive die. Six to ten distinct two-port blocks
//   that the engineer wants to compose and re-run the eye against.
//
//   This module is the data model and cascade logic for that
//   composition. Each block exposes a 2-port S-matrix at any
//   frequency; a Channel owns an ordered list of blocks and cascades
//   them via T-parameter multiplication to produce an end-to-end
//   Touchstone result that drops back into the existing channel-
//   synthesis / eye pipeline.
//
// What is in v1 and what is not
//
//   In: the data model (ChannelBlock hierarchy, Channel container),
//       five concrete block types covering the cases an SI engineer
//       hits day to day, and the cascade implementation.
//
//   Not yet:
//     * The Qt graphical block-strip widget. The model layer ships
//       first because the cascade logic is mathematically distinct
//       from rendering and benefits from its own commit + test pass
//       before the UI lands on top.
//     * Multi-port (N>2) topology. v1 is single-channel two-port.
//       The diff-pair case is covered separately by the DiffSynth
//       module today; a future merge unifies them.
//     * Block parameter optimization (e.g. "what trace length would
//       make S21 hit a target loss at f"). Manual edit-and-re-run for
//       now; this becomes a sweep when the topology editor UI gets
//       a "what-if" panel.
//
// Reference: the cascade math is the standard T-parameter chain from
// Pozar, Microwave Engineering, Ch. 4. sparam::s_to_t / t_to_s already
// implement the 2-port conversions; this module just walks the block
// list.

#pragma once

#include <complex>
#include <memory>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "si/ChannelSynthesis.h"
#include "si/Touchstone.h"
#include "si/TraceImpedance.h"
#include "si/ViaModel.h"

namespace sikit::si {

// Each ChannelBlock realises a 2-port network. Subclasses compute their
// S-matrix at an arbitrary frequency using whatever model is natural
// (closed-form impedance, lumped pi, interpolated Touchstone, etc.).
class ChannelBlock {
public:
    virtual ~ChannelBlock() = default;

    // Short human-readable label, shown in the UI block strip.
    virtual std::string label() const = 0;

    // 2-port S-matrix at the requested frequency. Reference impedance
    // is supplied so blocks can normalise correctly when the channel
    // uses a non-standard Z_ref.
    virtual Eigen::Matrix2cd s_at(double freq_hz,
                                    double z_ref) const = 0;
};

// A microstrip / stripline trace. Wraps ChannelSpec; the per-frequency
// solve uses the same synthesize_channel pipeline the standalone trace
// path uses.
class TraceBlock : public ChannelBlock {
public:
    TraceBlock(double width_m, double length_m, int layer_ordinal,
                sikit::analysis::AnalysisStackup stackup,
                sikit::analysis::Engine engine = sikit::analysis::Engine::ClosedForm);
    std::string label() const override;
    Eigen::Matrix2cd s_at(double freq_hz, double z_ref) const override;

    double width()  const { return spec_.trace_width; }
    double length() const { return spec_.length_m; }

private:
    sikit::analysis::ChannelSpec spec_;
};

// Lumped pi-model via using the existing ViaModel.
class ViaBlock : public ChannelBlock {
public:
    explicit ViaBlock(sikit::analysis::ViaSpec spec);
    std::string label() const override;
    Eigen::Matrix2cd s_at(double freq_hz, double z_ref) const override;

    const sikit::analysis::ViaSpec& spec() const { return spec_; }

private:
    sikit::analysis::ViaSpec spec_;
};

// Single lumped element placed either in series with the signal path
// (between port 1 and port 2) or in shunt to ground (from the signal
// node to ground). Useful for AC-coupling caps, ESD diodes, DC blocks,
// vendor-suggested component placement on the channel.
class LumpedBlock : public ChannelBlock {
public:
    enum class Topology {
        SeriesR, SeriesL, SeriesC,
        ShuntR,  ShuntL,  ShuntC,
    };
    LumpedBlock(Topology t, double value);
    std::string label() const override;
    Eigen::Matrix2cd s_at(double freq_hz, double z_ref) const override;

    Topology topology() const { return topology_; }
    double value() const { return value_; }

private:
    Topology topology_;
    double value_;   // ohms / henries / farads depending on topology
};

// An arbitrary user-supplied 2-port Touchstone. Useful for vendor-
// supplied connector or cable S-parameter files dropped into the
// channel between board-internal sections.
class TouchstoneBlock : public ChannelBlock {
public:
    TouchstoneBlock(touchstone::TouchstoneFile ts, std::string label);
    std::string label() const override;
    Eigen::Matrix2cd s_at(double freq_hz, double z_ref) const override;

private:
    touchstone::TouchstoneFile ts_;
    std::string label_;
};

// Lossless ideal transmission line: characteristic impedance Z0,
// physical length, phase velocity. Useful as a placeholder block while
// the user is sketching a channel before committing real geometry.
class IdealLineBlock : public ChannelBlock {
public:
    IdealLineBlock(double z0, double length_m, double v_phase);
    std::string label() const override;
    Eigen::Matrix2cd s_at(double freq_hz, double z_ref) const override;

private:
    double z0_;
    double length_m_;
    double v_phase_;
};

// Ordered series of blocks. Cascade goes left to right (block 0 sees
// the source side, last block sees the load side).
class Channel {
public:
    void add(std::unique_ptr<ChannelBlock> block);
    void clear();
    std::size_t size() const { return blocks_.size(); }
    bool empty() const { return blocks_.empty(); }
    const ChannelBlock& at(std::size_t i) const { return *blocks_[i]; }

    // Cascade every block at every frequency and produce a 2-port
    // Touchstone. An empty channel produces a passthrough (S21 = 1).
    touchstone::TouchstoneFile cascade(
        const std::vector<double>& freqs,
        double reference_impedance = 50.0) const;

private:
    std::vector<std::unique_ptr<ChannelBlock>> blocks_;
};

}  // namespace sikit::si
