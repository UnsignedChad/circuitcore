#include "circuitcore/netlist/Netlist.h"

namespace circuitcore::netlist {

const Component* Netlist::find_component(std::string_view ref) const {
    for (const auto& c : components) {
        if (c.ref == ref) return &c;
    }
    return nullptr;
}

const Net* Netlist::find_net(std::string_view name) const {
    for (const auto& n : nets) {
        if (n.name == name) return &n;
    }
    return nullptr;
}

}  // namespace circuitcore::netlist
