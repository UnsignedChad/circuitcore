// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "si/SiStackup.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

#include "circuitcore/sexpr/SExpr.h"

namespace sikit::si {

const SiStackupItem* SiStackup::adjacent_dielectric(
    std::string_view copper_name, int side) const noexcept {
    std::size_t idx = items.size();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == SiStackupItem::Kind::Copper &&
            items[i].name == copper_name) {
            idx = i;
            break;
        }
    }
    if (idx >= items.size()) return nullptr;

    if (side < 0) {
        for (std::size_t i = idx; i > 0; --i) {
            if (items[i - 1].kind == SiStackupItem::Kind::Dielectric)
                return &items[i - 1];
        }
    } else {
        for (std::size_t i = idx + 1; i < items.size(); ++i) {
            if (items[i].kind == SiStackupItem::Kind::Dielectric)
                return &items[i];
        }
    }
    return nullptr;
}

const SiStackupItem* SiStackup::any_dielectric() const noexcept {
    for (const auto& it : items) {
        if (it.kind == SiStackupItem::Kind::Dielectric) return &it;
    }
    return nullptr;
}

namespace {

constexpr double kMmToM = 1.0e-3;

const circuitcore::sexpr::Node* find_child(
    const circuitcore::sexpr::Node& n, std::string_view tag) {
    if (!n.is_list()) return nullptr;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

std::vector<const circuitcore::sexpr::Node*> find_children(
    const circuitcore::sexpr::Node& n, std::string_view tag) {
    std::vector<const circuitcore::sexpr::Node*> out;
    if (!n.is_list()) return out;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) out.push_back(&c);
    }
    return out;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

std::expected<SiStackup, SiStackupError> parse_si_stackup(std::string_view src) {
    using namespace circuitcore::sexpr;
    Node root;
    try {
        root = parse(src);
    } catch (const ParseError& e) {
        return std::unexpected(SiStackupError{
            std::string("kicad_pcb parse failed: ") + e.what()});
    }
    SiStackup result;
    const Node* setup = find_child(root, "setup");
    if (!setup) return result;
    const Node* stack = find_child(*setup, "stackup");
    if (!stack) return result;

    for (const Node* lay : find_children(*stack, "layer")) {
        if (lay->children.size() < 2) continue;
        const auto& name_node = lay->children[1];
        if (!name_node.is_string() && !name_node.is_symbol()) continue;
        SiStackupItem item;
        item.name = name_node.text;

        std::string tstr;
        if (const Node* t = find_child(*lay, "type")) {
            if (t->children.size() >= 2 &&
                (t->children[1].is_string() || t->children[1].is_symbol())) {
                tstr = t->children[1].text;
            }
        }
        std::string tlower = lower(tstr);
        if (tlower == "copper") {
            item.kind = SiStackupItem::Kind::Copper;
        } else if (tlower.find("dielectric") != std::string::npos ||
                   tlower == "core" || tlower == "prepreg") {
            item.kind = SiStackupItem::Kind::Dielectric;
        } else if (tlower.find("solder mask") != std::string::npos ||
                   tlower.find("soldermask") != std::string::npos) {
            item.kind = SiStackupItem::Kind::SolderMask;
        } else if (tlower.find("silk") != std::string::npos) {
            item.kind = SiStackupItem::Kind::Silkscreen;
        } else if (tlower.find("paste") != std::string::npos) {
            item.kind = SiStackupItem::Kind::Paste;
        } else {
            item.kind = SiStackupItem::Kind::Other;
        }

        if (const Node* th = find_child(*lay, "thickness")) {
            if (th->children.size() >= 2 && th->children[1].is_number()) {
                item.thickness = th->children[1].number * kMmToM;
            }
        }
        if (const Node* m = find_child(*lay, "material")) {
            if (m->children.size() >= 2 &&
                (m->children[1].is_string() || m->children[1].is_symbol())) {
                item.material = m->children[1].text;
            }
        }
        if (const Node* e = find_child(*lay, "epsilon_r")) {
            if (e->children.size() >= 2 && e->children[1].is_number()) {
                item.epsilon_r = e->children[1].number;
            }
        }
        if (const Node* l = find_child(*lay, "loss_tangent")) {
            if (l->children.size() >= 2 && l->children[1].is_number()) {
                item.loss_tangent = l->children[1].number;
            }
        }
        result.items.push_back(std::move(item));
    }
    return result;
}

std::expected<SiStackup, SiStackupError> load_si_stackup(
    const std::filesystem::path& path) {
    std::ifstream is(path);
    if (!is) {
        return std::unexpected(SiStackupError{
            "could not open " + path.string()});
    }
    std::ostringstream ss;
    ss << is.rdbuf();
    return parse_si_stackup(ss.str());
}

}  // namespace sikit::si
