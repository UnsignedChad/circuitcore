#include "si/SchematicTopology.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace sikit::si {

namespace {

// KiCad pintype strings -- documented under eeschema's pin type enum.
// We collapse the variants we don't care about (open_emitter is a
// driver just like open_collector, tri_state can drive when enabled).
TopologyRole classify_pintype(std::string_view pt) {
    if (pt.empty()) return TopologyRole::Unspecified;
    if (pt == "output" || pt == "tri_state" || pt == "tri-state" ||
        pt == "open_collector" || pt == "openCollector" ||
        pt == "open_emitter"   || pt == "openEmitter"   ||
        pt == "bidirectional") {
        return TopologyRole::Driver;
    }
    if (pt == "input") return TopologyRole::Receiver;
    if (pt == "passive" || pt == "free") return TopologyRole::Passive;
    if (pt == "power_in" || pt == "power_out") return TopologyRole::Power;
    if (pt == "no_connect" || pt == "nc" || pt == "unconnected") {
        return TopologyRole::NoConnect;
    }
    return TopologyRole::Unspecified;
}

bool starts_with(const std::string& s, std::string_view prefix) {
    return s.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), s.begin());
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

const char* role_name(TopologyRole r) {
    switch (r) {
        case TopologyRole::Driver:      return "driver";
        case TopologyRole::Receiver:    return "receiver";
        case TopologyRole::Passive:     return "passive";
        case TopologyRole::Power:       return "power";
        case TopologyRole::NoConnect:   return "nc";
        case TopologyRole::Unspecified: return "unspecified";
    }
    return "unspecified";
}

std::vector<const TopologyEndpoint*> DerivedTopology::drivers() const {
    std::vector<const TopologyEndpoint*> out;
    for (const auto& e : endpoints) {
        if (e.role == TopologyRole::Driver) out.push_back(&e);
    }
    return out;
}

std::vector<const TopologyEndpoint*> DerivedTopology::receivers() const {
    std::vector<const TopologyEndpoint*> out;
    for (const auto& e : endpoints) {
        if (e.role == TopologyRole::Receiver) out.push_back(&e);
    }
    return out;
}

std::vector<const TopologyEndpoint*> DerivedTopology::passives() const {
    std::vector<const TopologyEndpoint*> out;
    for (const auto& e : endpoints) {
        if (e.role == TopologyRole::Passive) out.push_back(&e);
    }
    return out;
}

bool DerivedTopology::has_driver_problem() const {
    const auto n = drivers().size();
    return n == 0 || n > 1;
}

bool looks_like_power_net(const std::string& net_name) {
    const auto up = upper(net_name);
    // Strip a single leading '/' from KiCad hierarchical-label names.
    const auto core = (!up.empty() && up.front() == '/') ? up.substr(1) : up;
    if (core == "GND" || core == "AGND" || core == "DGND" ||
        core == "PGND" || core == "EGND" || core == "GNDREF") {
        return true;
    }
    if (starts_with(core, "VDD") || starts_with(core, "VCC") ||
        starts_with(core, "VSS") || starts_with(core, "VEE") ||
        starts_with(core, "VBAT") || starts_with(core, "VBUS")) {
        return true;
    }
    // +3V3 / +5V / +1V8 / -12V style.
    if (!core.empty() && (core.front() == '+' || core.front() == '-')) {
        return true;
    }
    return false;
}

DerivedTopology derive_topology(const circuitcore::netlist::Netlist& nl,
                                  const std::string& net_name) {
    DerivedTopology t;
    t.net_name = net_name;
    const auto* net = nl.find_net(net_name);
    if (!net) return t;
    t.net_code = net->code;
    t.endpoints.reserve(net->nodes.size());
    for (const auto& n : net->nodes) {
        TopologyEndpoint ep;
        ep.component_ref = n.component_ref;
        ep.pin           = n.pin;
        ep.pin_function  = n.pin_function;
        ep.pin_type      = n.pin_type;
        ep.role          = classify_pintype(n.pin_type);
        t.endpoints.push_back(std::move(ep));
    }
    return t;
}

std::vector<DerivedTopology> derive_all_topologies(
    const circuitcore::netlist::Netlist& nl) {
    std::vector<DerivedTopology> out;
    out.reserve(nl.nets.size());
    for (const auto& n : nl.nets) {
        if (looks_like_power_net(n.name)) continue;
        out.push_back(derive_topology(nl, n.name));
    }
    return out;
}

}  // namespace sikit::si
