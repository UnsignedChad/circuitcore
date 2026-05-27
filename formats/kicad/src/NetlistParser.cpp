// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/formats/kicad/NetlistParser.h"

#include <format>
#include <fstream>
#include <sstream>
#include <utility>

#include "circuitcore/sexpr/SExpr.h"

namespace circuitcore::formats::kicad {

using circuitcore::sexpr::Node;
using circuitcore::sexpr::parse;

std::string NetlistParseError::format() const {
    if (line == 0 && col == 0) return message;
    return std::format("netlist parse error at line {}, col {}: {}",
                        line, col, message);
}

namespace {

const Node* find_child(const Node& n, std::string_view tag) {
    if (!n.is_list()) return nullptr;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

std::vector<const Node*> find_children(const Node& n, std::string_view tag) {
    std::vector<const Node*> out;
    if (!n.is_list()) return out;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) out.push_back(&c);
    }
    return out;
}

// (key "value")  or  (key value)
std::string read_string_field(const Node& n) {
    if (!n.is_list() || n.children.size() < 2) return {};
    const auto& v = n.children[1];
    if (v.is_string() || v.is_symbol()) return v.text;
    return {};
}

netlist::Component parse_component(const Node& comp) {
    netlist::Component c;
    if (const Node* r = find_child(comp, "ref"))       c.ref       = read_string_field(*r);
    if (const Node* v = find_child(comp, "value"))     c.value     = read_string_field(*v);
    if (const Node* f = find_child(comp, "footprint")) c.footprint = read_string_field(*f);
    return c;
}

netlist::Node parse_node(const Node& node) {
    netlist::Node out;
    if (const Node* r = find_child(node, "ref"))         out.component_ref = read_string_field(*r);
    if (const Node* p = find_child(node, "pin"))         out.pin           = read_string_field(*p);
    if (const Node* f = find_child(node, "pinfunction")) out.pin_function  = read_string_field(*f);
    if (const Node* t = find_child(node, "pintype"))     out.pin_type      = read_string_field(*t);
    return out;
}

}  // namespace

std::expected<netlist::Netlist, NetlistParseError>
NetlistParser::parse_string(std::string_view src) {
    Node root;
    try {
        root = parse(src);
    } catch (const sexpr::ParseError& e) {
        return std::unexpected(NetlistParseError{e.what(), e.line, e.col});
    }
    if (!root.is_list() || root.tag() != "export") {
        return std::unexpected(NetlistParseError{
            "not a KiCad netlist (root tag != export)", 0, 0});
    }

    netlist::Netlist out;
    if (const Node* design = find_child(root, "design")) {
        if (const Node* src_node = find_child(*design, "source")) {
            out.source_sheet = read_string_field(*src_node);
        }
    }

    if (const Node* comps = find_child(root, "components")) {
        for (const Node* c : find_children(*comps, "comp")) {
            out.components.push_back(parse_component(*c));
        }
    }

    if (const Node* nets = find_child(root, "nets")) {
        for (const Node* n : find_children(*nets, "net")) {
            netlist::Net net;
            if (const Node* code = find_child(*n, "code")) {
                if (code->children.size() >= 2) {
                    const auto& v = code->children[1];
                    if (v.is_number()) {
                        net.code = static_cast<int>(v.number);
                    } else if (v.is_string() || v.is_symbol()) {
                        // KiCad sometimes writes (code "1") as a string.
                        try { net.code = std::stoi(v.text); }
                        catch (...) { net.code = 0; }
                    }
                }
            }
            if (const Node* nm = find_child(*n, "name")) {
                net.name = read_string_field(*nm);
            }
            for (const Node* nd : find_children(*n, "node")) {
                net.nodes.push_back(parse_node(*nd));
            }
            out.nets.push_back(std::move(net));
        }
    }
    return out;
}

std::expected<netlist::Netlist, NetlistParseError>
NetlistParser::parse_file(const std::filesystem::path& path) {
    std::ifstream is(path);
    if (!is) {
        return std::unexpected(NetlistParseError{
            std::format("could not open {}", path.string()), 0, 0});
    }
    std::ostringstream ss;
    ss << is.rdbuf();
    return parse_string(ss.str());
}

}  // namespace circuitcore::formats::kicad
