// HTML compliance report.
//
// Runs every sikit analysis pipeline against a loaded board and bundles
// the results into a single self-contained HTML page. Suitable for
// pasting into a design-review wiki, attaching to a JIRA ticket, or
// archiving as a per-revision board snapshot. No external CSS / JS;
// browser-portable.
//
// Scope: tabular summary. Embedded waveform plots (eye diagrams,
// S-parameter sweeps) are a future enhancement -- they require either
// inline SVG rendering or a separate image-emit pass. The current
// pass-through covers what a design-review meeting actually wants in
// the first cut: at-a-glance PASS / FAIL per net.

#pragma once

#include <string>
#include <vector>

#include "circuitcore/board/Board.h"
#include "si/BusGroup.h"
#include "si/ReturnPath.h"
#include "si/SiStackup.h"
#include "si/Skew.h"

namespace sikit::report {

struct NetSummary {
    std::string net_name;
    int net_id = -1;
    int layer_ordinal = 0;
    double trace_width_m = 0.0;
    double total_length_m = 0.0;
    double z0_ohm = 0.0;
    double v_phase = 0.0;
    double eps_eff = 0.0;
    bool valid = false;       // false when length / width data was missing
};

struct BoardReport {
    std::string board_path;
    int net_count = 0;
    int segment_count = 0;
    int via_count = 0;
    int copper_layer_count = 0;

    // Per-net impedance + length summary.
    std::vector<NetSummary> nets;

    // Existing analysis structs wired through verbatim.
    std::vector<sikit::si::DiffPairSkew> diff_pairs;
    std::vector<sikit::si::BusGroup> buses;
    std::vector<sikit::si::ReturnPathViolation> return_path_violations;

    // Aggregate PASS/FAIL flags so the HTML banner can colour the page.
    int diff_pair_fails = 0;
    int bus_fails = 0;
    int return_path_fails = 0;
    bool overall_pass() const {
        return diff_pair_fails == 0 && bus_fails == 0 &&
               return_path_fails == 0;
    }
};

// Run every analysis against the board and bundle the result.
BoardReport build_board_report(
    const circuitcore::board::Board& board,
    const sikit::si::SiStackup& sis,
    double diff_skew_budget_ps = 5.0,
    double bus_skew_budget_ps = 10.0);

// Render the report as a self-contained HTML document.
std::string render_html(const BoardReport& report);

}  // namespace sikit::report
