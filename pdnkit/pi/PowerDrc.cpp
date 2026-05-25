#include "pi/PowerDrc.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace pdnkit::pi {

namespace {

// IPC-2221 constants. k values are calibrated for I in amps, A in mil^2,
// and temperature rise in degrees C.
constexpr double kK_external = 0.048;
constexpr double kK_internal = 0.024;
constexpr double kTempExponent = 0.44;
constexpr double kAreaExponent = 0.725;

// Unit conversions.
//   1 mil = 0.0254 mm = 2.54e-5 m
//   1 mil^2 = 6.4516e-10 m^2
constexpr double kMil2_to_m2 = 6.4516e-10;

double m2_to_mil2(double a_m2) { return a_m2 / kMil2_to_m2; }
double mil2_to_m2(double a_mil2) { return a_mil2 * kMil2_to_m2; }

// Identify the external (top/bottom) copper layers. KiCad convention:
// F.Cu is ordinal 0 and is always external; B.Cu is the highest-ordinal
// copper layer in the stackup and is also external. Everything between
// is internal.
bool is_external_layer(const circuitcore::board::Board& board, int layer_ord) {
    if (layer_ord == 0) return true;  // F.Cu
    // Find the highest copper ordinal in the stackup -- that's B.Cu.
    int max_copper_ord = 0;
    for (const auto& L : board.stackup.layers) {
        if (L.is_copper() && L.ordinal > max_copper_ord) {
            max_copper_ord = L.ordinal;
        }
    }
    return layer_ord == max_copper_ord;
}

double layer_thickness(const circuitcore::board::Board& board, int layer_ord,
                       double fallback) {
    if (const auto* L = board.find_layer(layer_ord)) {
        return L->thickness > 0.0 ? L->thickness : fallback;
    }
    return fallback;
}

}  // namespace

double ipc2221_max_current(double area_m2, double temp_rise_c, bool external) {
    if (area_m2 <= 0.0 || temp_rise_c <= 0.0) return 0.0;
    const double k = external ? kK_external : kK_internal;
    const double area_mil2 = m2_to_mil2(area_m2);
    return k * std::pow(temp_rise_c, kTempExponent)
             * std::pow(area_mil2, kAreaExponent);
}

double ipc2221_min_area(double current_amps, double temp_rise_c, bool external) {
    if (current_amps <= 0.0 || temp_rise_c <= 0.0) return 0.0;
    const double k = external ? kK_external : kK_internal;
    // I = k * dT^0.44 * A^0.725
    //   -> A^0.725 = I / (k * dT^0.44)
    //   -> A = (I / (k * dT^0.44))^(1/0.725)
    const double rhs = current_amps / (k * std::pow(temp_rise_c, kTempExponent));
    const double area_mil2 = std::pow(rhs, 1.0 / kAreaExponent);
    return mil2_to_m2(area_mil2);
}

DrcReport check_ipc2152(const circuitcore::board::Board& board,
                        const std::vector<DrcRule>& rules,
                        double fallback_cu_thickness_m) {
    DrcReport report;

    // Index rules by net id for O(1) lookup as we scan segments.
    auto find_rule = [&](int net_id) -> const DrcRule* {
        auto it = std::find_if(rules.begin(), rules.end(),
            [net_id](const DrcRule& r) { return r.net_id == net_id; });
        return it == rules.end() ? nullptr : &*it;
    };

    report.nets_checked = static_cast<int>(rules.size());

    for (std::size_t i = 0; i < board.segments.size(); ++i) {
        const auto& seg = board.segments[i];
        const auto* rule = find_rule(seg.net_id);
        if (!rule) continue;
        report.segments_checked++;

        const bool external = is_external_layer(board, seg.layer_ordinal);
        const double t = layer_thickness(board, seg.layer_ordinal,
                                          fallback_cu_thickness_m);
        const double area_actual = seg.width * t;
        const double area_required = ipc2221_min_area(
            rule->current_amps, rule->temp_rise_c, external);

        if (area_actual >= area_required) continue;

        const double width_required = (t > 0.0) ? area_required / t : 0.0;

        DrcViolation v;
        v.segment_index = static_cast<int>(i);
        v.net_id = seg.net_id;
        v.layer_ordinal = seg.layer_ordinal;
        v.external = external;
        v.current_amps = rule->current_amps;
        v.temp_rise_c = rule->temp_rise_c;
        v.width_actual_m = seg.width;
        v.width_required_m = width_required;
        v.cu_thickness_m = t;

        const auto* net = board.find_net(seg.net_id);
        const auto* layer = board.find_layer(seg.layer_ordinal);
        v.message = std::format(
            "net {} ({}) on layer {} ({}): segment width {:.3f} mm, "
            "needs {:.3f} mm for {:.2f} A at +{:.0f} C rise "
            "(IPC-2221, t = {:.1f} um, {})",
            seg.net_id,
            net && !net->name.empty() ? net->name : std::string("?"),
            seg.layer_ordinal,
            layer ? layer->name : std::string("?"),
            seg.width * 1000.0,
            width_required * 1000.0,
            rule->current_amps,
            rule->temp_rise_c,
            t * 1.0e6,
            external ? "external" : "internal");
        report.violations.push_back(std::move(v));
    }

    return report;
}

}  // namespace pdnkit::pi
