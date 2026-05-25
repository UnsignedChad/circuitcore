#include "StackupWriter.h"

#include <fstream>
#include <sstream>

#include "circuitcore/sexpr/SExpr.h"

namespace pdnkit {

namespace {

using circuitcore::sexpr::Node;

// Find the first child list with the given tag (first child symbol).
Node* find_tagged_child(Node& parent, std::string_view tag) {
    if (!parent.is_list()) return nullptr;
    for (auto& c : parent.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

// (layer "F.Cu" ...) -> the layer name, or empty if not a layer form.
std::string layer_name(const Node& n) {
    if (!n.is_list() || n.tag() != "layer" || n.children.size() < 2) return {};
    const auto& name_node = n.children[1];
    if (name_node.is_string() || name_node.is_symbol()) return name_node.text;
    return {};
}

// Update the (thickness X) child of a (layer ...) node to the given mm value.
// Returns true if a thickness child was found and updated.
bool update_thickness(Node& layer, double thickness_mm) {
    if (auto* th = find_tagged_child(layer, "thickness");
        th && th->children.size() >= 2) {
        auto& val = th->children[1];
        val.kind = Node::Kind::Number;
        val.number = thickness_mm;
        val.text.clear();
        return true;
    }
    return false;
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

StackupSaveResult save_modified_stackup(
    const std::filesystem::path& src_path,
    const std::filesystem::path& dst_path,
    const circuitcore::board::Board& board) {
    StackupSaveResult r;
    if (src_path == dst_path) {
        r.error = "destination must differ from source (safety)";
        return r;
    }
    std::string src;
    try {
        src = slurp(src_path);
    } catch (...) {
        r.error = "cannot read source file";
        return r;
    }
    Node root;
    try {
        root = circuitcore::sexpr::parse(src);
    } catch (const circuitcore::sexpr::ParseError& e) {
        r.error = std::string("parse failed: ") + e.what();
        return r;
    }
    auto* setup = find_tagged_child(root, "setup");
    auto* stackup = setup ? find_tagged_child(*setup, "stackup") : nullptr;
    if (!stackup) {
        r.error = "no (setup (stackup ...)) block in this file; "
                  "KiCad has not been told to author one yet "
                  "(Board > Board Setup > Physical Stackup)";
        return r;
    }
    // For each (layer ...) entry, find the matching in-memory layer by
    // name and update its thickness.
    for (auto& layer : stackup->children) {
        const std::string nm = layer_name(layer);
        if (nm.empty()) continue;
        for (const auto& L : board.stackup.layers) {
            if (L.name == nm && L.thickness > 0.0) {
                if (update_thickness(layer, L.thickness * 1000.0)) {
                    ++r.layers_updated;
                }
                break;
            }
        }
    }

    const std::string text = circuitcore::sexpr::emit(root);
    std::ofstream out(dst_path);
    if (!out) {
        r.error = "cannot open destination for write";
        return r;
    }
    out << text;
    if (!out.good()) {
        r.error = "write failed";
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace pdnkit
