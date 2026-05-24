#include "circuitcore/intent/Checks.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <unordered_set>

namespace circuitcore::intent {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

bool is_power_net(std::string_view name, const std::vector<std::string>& keywords) {
    const std::string up = upper(std::string(name));
    for (const auto& k : keywords) {
        if (up.find(upper(k)) != std::string::npos) return true;
    }
    return false;
}

bool is_ground_net(std::string_view name) {
    const std::string up = upper(std::string(name));
    return up == "GND" || up == "GROUND" || up == "AGND" || up == "DGND" ||
           up == "PGND" || up == "VSS";
}

bool is_capacitor_ref(std::string_view ref) {
    return !ref.empty() && (ref[0] == 'C' || ref[0] == 'c');
}

double distance(const board::Point2& a, const board::Point2& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Find a pad belonging to the named component. Returns nullptr if
// the board doesn't carry that footprint (or if PcbParser failed to
// stamp parent_ref, e.g. on legacy .kicad_pcb files that predate the
// parent_ref work).
const board::Pad* first_pad_of(const board::Board& board,
                                  std::string_view ref) {
    for (const auto& p : board.pads) {
        if (p.parent_ref == ref) return &p;
    }
    return nullptr;
}

// All pads belonging to the named component.
std::vector<const board::Pad*> pads_of(const board::Board& board,
                                          std::string_view ref) {
    std::vector<const board::Pad*> out;
    for (const auto& p : board.pads) {
        if (p.parent_ref == ref) out.push_back(&p);
    }
    return out;
}

void check_missing_footprints(const netlist::Netlist& nl,
                                const board::Board& board,
                                std::vector<Violation>& out) {
    for (const auto& c : nl.components) {
        if (!first_pad_of(board, c.ref)) {
            out.push_back({
                Violation::Kind::MissingFootprint,
                c.ref,
                std::format("'{}' ({} {}) in netlist but no pad with that "
                              "parent_ref on the board",
                              c.ref, c.value, c.footprint),
            });
        }
    }
}

void check_unrouted_nets(const netlist::Netlist& nl,
                          const board::Board& board,
                          std::vector<Violation>& out) {
    // A net is "routed" if at least one segment, via, or pad references it.
    std::unordered_set<int> used_nets;
    for (const auto& s : board.segments) used_nets.insert(s.net_id);
    for (const auto& v : board.vias)     used_nets.insert(v.net_id);
    for (const auto& p : board.pads)     used_nets.insert(p.net_id);

    for (const auto& net : nl.nets) {
        // Single-node nets (no-connects / dangling pins) don't need routing.
        if (net.nodes.size() < 2) continue;
        // Skip the auto-generated "Net-(C1-Pad1)" style sometimes left
        // by unconnected pins -- those rarely indicate real intent.
        if (net.name.rfind("Net-(", 0) == 0) continue;

        const board::Net* bnet = board.find_net_by_name(net.name);
        if (!bnet) {
            out.push_back({
                Violation::Kind::UnroutedNet,
                net.name,
                std::format("net '{}' has {} schematic nodes but no "
                              "matching board net", net.name, net.nodes.size()),
            });
            continue;
        }
        if (used_nets.find(bnet->id) == used_nets.end()) {
            out.push_back({
                Violation::Kind::UnroutedNet,
                net.name,
                std::format("net '{}' (id {}) exists on board but has no "
                              "segments / vias / pads", net.name, bnet->id),
            });
        }
    }
}

void check_decoupling_proximity(const netlist::Netlist& nl,
                                  const board::Board& board,
                                  const CheckOptions& opts,
                                  std::vector<Violation>& out) {
    // For each cap whose pins land on (power, ground), find the IC pin
    // sharing that power rail and measure distance.
    for (const auto& c : nl.components) {
        if (!is_capacitor_ref(c.ref)) continue;

        // What nets does this cap sit on?
        std::vector<std::string> cap_nets;
        for (const auto& net : nl.nets) {
            for (const auto& node : net.nodes) {
                if (node.component_ref == c.ref) {
                    cap_nets.push_back(net.name);
                    break;
                }
            }
        }
        if (cap_nets.size() != 2) continue;
        // One side must be power, the other ground.
        const std::string* power = nullptr;
        for (const auto& n : cap_nets) {
            if (is_power_net(n, opts.power_net_keywords) && !is_ground_net(n)) {
                power = &n;
            }
        }
        bool has_gnd = false;
        for (const auto& n : cap_nets) if (is_ground_net(n)) has_gnd = true;
        if (!power || !has_gnd) continue;

        // Find the IC(s) that ALSO sit on this power net (i.e. the rail
        // user this cap is decoupling).
        const netlist::Net* pnet = nl.find_net(*power);
        if (!pnet) continue;
        std::vector<std::string> ic_refs;
        for (const auto& node : pnet->nodes) {
            if (node.component_ref == c.ref) continue;
            if (is_capacitor_ref(node.component_ref)) continue;
            ic_refs.push_back(node.component_ref);
        }
        if (ic_refs.empty()) continue;

        // Cap's location on the board.
        const board::Pad* cap_pad = first_pad_of(board, c.ref);
        if (!cap_pad) continue;   // covered by missing-footprint check

        // Closest IC pin on the same power net.
        double best = std::numeric_limits<double>::infinity();
        std::string best_ic;
        for (const auto& ic_ref : ic_refs) {
            for (const auto* pad : pads_of(board, ic_ref)) {
                if (!c.value.empty() && pad->net_id == cap_pad->net_id) {
                    const double d = distance(cap_pad->at, pad->at);
                    if (d < best) { best = d; best_ic = ic_ref; }
                }
            }
        }
        if (!std::isfinite(best) || best_ic.empty()) continue;
        if (best > opts.decoupling_max_distance_m) {
            out.push_back({
                Violation::Kind::DecouplingCapTooFar,
                c.ref,
                std::format("decoupling cap '{}' on '{}' sits {:.2f} mm "
                              "from nearest '{}' pad (budget {:.2f} mm)",
                              c.ref, *power, best * 1e3, best_ic,
                              opts.decoupling_max_distance_m * 1e3),
            });
        }
    }
}

}  // namespace

const char* kind_name(Violation::Kind k) {
    switch (k) {
        case Violation::Kind::MissingFootprint:    return "missing_footprint";
        case Violation::Kind::UnroutedNet:         return "unrouted_net";
        case Violation::Kind::DecouplingCapTooFar: return "decoupling_too_far";
    }
    return "?";
}

std::vector<Violation> check_design_intent(
    const netlist::Netlist& netlist,
    const board::Board& board,
    const CheckOptions& opts) {
    std::vector<Violation> out;
    if (opts.check_missing_footprint)
        check_missing_footprints(netlist, board, out);
    if (opts.check_unrouted_net)
        check_unrouted_nets(netlist, board, out);
    if (opts.check_decoupling_proximity)
        check_decoupling_proximity(netlist, board, opts, out);
    return out;
}

}  // namespace circuitcore::intent
