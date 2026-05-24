// Synthesize a Touchstone 2-port S-parameter file from a single trace's
// geometry and length. Closes the loop on the SI workflow:
//
//   KiCad PCB → trace width + layer + length → Z₀, v_phase (via engine)
//             → ABCD per frequency → S-params (Touchstone)
//             → eye-diagram channel
//
// Optionally drives the channel with a Djordjevic-Sarkar dispersion
// model so εr and tan δ vary with frequency the way they actually do
// in FR-4 / Rogers / etc. Without a model, behaviour reverts to the
// constant-εr / constant-tan_δ behaviour of the earlier versions.

#pragma once

#include <optional>
#include <vector>

#include "si/TraceImpedance.h"
#include "si/DjordjevicSarkar.h"
#include "si/Touchstone.h"

namespace sikit::analysis {

struct ChannelSpec {
    double trace_width = 0.0;       // m
    int    layer_ordinal = 0;
    double length_m = 0.0;          // physical trace length
    AnalysisStackup stackup;
    Engine engine = Engine::ClosedForm;

    // Optional frequency-dependent material model. When set, εr and
    // tan δ at each frequency point come from this model instead of
    // the constant stackup values. Construct via
    //     dispersion::DjordjevicSarkar::from_reference(eps_r, tan_d, f0)
    std::optional<dispersion::DjordjevicSarkar> dispersion_model;
};

sikit::touchstone::TouchstoneFile synthesize_channel(
    const ChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance = 50.0);

}  // namespace sikit::analysis
