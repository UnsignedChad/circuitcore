#include "mp/StudySerial.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mpkit {

namespace {

using circuitcore::sexpr::Node;

Node make_symbol(std::string s) {
    Node n; n.kind = Node::Kind::Symbol; n.text = std::move(s);
    return n;
}
Node make_string(std::string s) {
    Node n; n.kind = Node::Kind::String; n.text = std::move(s);
    return n;
}
Node make_number(double v) {
    Node n; n.kind = Node::Kind::Number; n.number = v;
    return n;
}
Node make_list(std::string tag) {
    Node n; n.kind = Node::Kind::List;
    if (!tag.empty()) n.children.push_back(make_symbol(std::move(tag)));
    return n;
}

// Walk children looking for a tagged sub-list (the convention sexpr
// uses for "named field" entries). Returns nullptr if not found.
const Node* find_child(const Node& parent, std::string_view tag) {
    for (const auto& c : parent.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

// Convenience: get a string from the second child of (tag value).
std::string str_field(const Node& parent, std::string_view tag,
                      std::string fallback = "") {
    const Node* c = find_child(parent, tag);
    if (!c || c->children.size() < 2) return fallback;
    const Node& v = c->children[1];
    if (v.is_string() || v.is_symbol()) return v.text;
    return fallback;
}

double num_field(const Node& parent, std::string_view tag,
                 double fallback = 0.0) {
    const Node* c = find_child(parent, tag);
    if (!c || c->children.size() < 2) return fallback;
    const Node& v = c->children[1];
    if (v.is_number()) return v.number;
    return fallback;
}

int int_field(const Node& parent, std::string_view tag, int fallback = 0) {
    return static_cast<int>(num_field(parent, tag, fallback));
}

}  // namespace

const char* to_string(PhysicsKind k) {
    switch (k) {
        case PhysicsKind::SteadyHeat:    return "steady_heat";
        case PhysicsKind::TransientHeat: return "transient_heat";
        case PhysicsKind::Elasticity:    return "elasticity";
        case PhysicsKind::PdnIrDrop:     return "pdn_ir_drop";
        case PhysicsKind::PdnCavityZf:   return "pdn_cavity_zf";
        case PhysicsKind::SikitFdtd:     return "sikit_fdtd";
    }
    return "unknown";
}

PhysicsKind physics_kind_from_string(const std::string& s) {
    if (s == "steady_heat")    return PhysicsKind::SteadyHeat;
    if (s == "transient_heat") return PhysicsKind::TransientHeat;
    if (s == "elasticity")     return PhysicsKind::Elasticity;
    if (s == "pdn_ir_drop")    return PhysicsKind::PdnIrDrop;
    if (s == "pdn_cavity_zf")  return PhysicsKind::PdnCavityZf;
    if (s == "sikit_fdtd")     return PhysicsKind::SikitFdtd;
    throw std::runtime_error("mpkit: unknown PhysicsKind '" + s + "'");
}

// ---- serialise -------------------------------------------------------

std::string study_to_sexpr(const Study& s) {
    Node root = make_list("mpkit_study");
    {
        Node v = make_list("version"); v.children.push_back(make_number(s.version));
        root.children.push_back(v);
    }
    {
        Node n = make_list("name"); n.children.push_back(make_string(s.name));
        root.children.push_back(n);
    }
    for (const auto& node : s.nodes) {
        Node nn = make_list("node");
        {
            Node id = make_list("id"); id.children.push_back(make_string(node.id));
            nn.children.push_back(id);
        }
        {
            Node lb = make_list("label"); lb.children.push_back(make_string(node.label));
            nn.children.push_back(lb);
        }
        {
            Node k = make_list("kind");
            k.children.push_back(make_string(to_string(node.kind)));
            nn.children.push_back(k);
        }
        // The config node is already a sexpr tree; embed it under
        // (config ...).
        Node cfg = make_list("config");
        if (node.config.is_list()) {
            for (const auto& c : node.config.children) cfg.children.push_back(c);
        }
        nn.children.push_back(cfg);
        root.children.push_back(nn);
    }
    for (const auto& cp : s.couplings) {
        Node cc = make_list("coupling");
        Node sn = make_list("source_node"); sn.children.push_back(make_string(cp.source_node_id));
        Node so = make_list("source_output"); so.children.push_back(make_string(cp.source_output));
        Node tn = make_list("target_node"); tn.children.push_back(make_string(cp.target_node_id));
        Node ti = make_list("target_input"); ti.children.push_back(make_string(cp.target_input));
        cc.children.push_back(sn);
        cc.children.push_back(so);
        cc.children.push_back(tn);
        cc.children.push_back(ti);
        if (!cp.transform.empty()) {
            Node tr = make_list("transform"); tr.children.push_back(make_string(cp.transform));
            cc.children.push_back(tr);
        }
        root.children.push_back(cc);
    }
    if (!s.solve_order.empty()) {
        Node ord = make_list("solve_order");
        for (const auto& id : s.solve_order) ord.children.push_back(make_string(id));
        root.children.push_back(ord);
    }
    for (const auto& sw : s.sweeps) {
        Node sn = make_list("sweep");
        Node pp = make_list("parameter"); pp.children.push_back(make_string(sw.parameter_path));
        sn.children.push_back(pp);
        Node vv = make_list("values");
        for (double v : sw.values) vv.children.push_back(make_number(v));
        sn.children.push_back(vv);
        root.children.push_back(sn);
    }
    for (const auto& rf : s.result_files) {
        Node f = make_list("field");
        Node n = make_list("node");   n.children.push_back(make_string(rf.node_id));     f.children.push_back(n);
        Node o = make_list("output"); o.children.push_back(make_string(rf.output_name)); f.children.push_back(o);
        Node sx = make_list("sweep_index"); sx.children.push_back(make_number(rf.sweep_index)); f.children.push_back(sx);
        Node p  = make_list("path"); p.children.push_back(make_string(rf.path));          f.children.push_back(p);
        root.children.push_back(f);
    }
    return circuitcore::sexpr::emit(root);
}

// ---- parse ----------------------------------------------------------

Study study_from_sexpr(const std::string& src) {
    using circuitcore::sexpr::parse;
    Node root = parse(src);
    if (!root.is_list() || root.tag() != "mpkit_study") {
        throw std::runtime_error(
            "mpkit::study_from_sexpr: root must be (mpkit_study ...)");
    }
    Study s;
    s.version = int_field(root, "version", 1);
    s.name    = str_field(root, "name", "Untitled study");

    for (const auto& c : root.children) {
        if (!c.is_list()) continue;
        const auto t = c.tag();
        if (t == "node") {
            PhysicsNode n;
            n.id    = str_field(c, "id");
            n.label = str_field(c, "label");
            n.kind  = physics_kind_from_string(str_field(c, "kind", "steady_heat"));
            const Node* cfg = find_child(c, "config");
            if (cfg) n.config = *cfg;
            s.nodes.push_back(std::move(n));
        } else if (t == "coupling") {
            CouplingSpec cp;
            cp.source_node_id = str_field(c, "source_node");
            cp.source_output  = str_field(c, "source_output");
            cp.target_node_id = str_field(c, "target_node");
            cp.target_input   = str_field(c, "target_input");
            cp.transform      = str_field(c, "transform");
            s.couplings.push_back(std::move(cp));
        } else if (t == "solve_order") {
            for (std::size_t i = 1; i < c.children.size(); ++i) {
                const Node& ch = c.children[i];
                if (ch.is_string() || ch.is_symbol())
                    s.solve_order.push_back(ch.text);
            }
        } else if (t == "sweep") {
            SweepSpec sw;
            sw.parameter_path = str_field(c, "parameter");
            const Node* vv = find_child(c, "values");
            if (vv) {
                for (std::size_t i = 1; i < vv->children.size(); ++i) {
                    const Node& ch = vv->children[i];
                    if (ch.is_number()) sw.values.push_back(ch.number);
                }
            }
            s.sweeps.push_back(std::move(sw));
        } else if (t == "field") {
            StoredField f;
            f.node_id     = str_field(c, "node");
            f.output_name = str_field(c, "output");
            f.sweep_index = int_field(c, "sweep_index", 0);
            f.path        = str_field(c, "path");
            s.result_files.push_back(std::move(f));
        }
    }
    return s;
}

// ---- file I/O -------------------------------------------------------

void save_study(const Study& s, const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    fs::create_directories(dir);
    const fs::path p = dir / "study.mpstudy";
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        throw std::runtime_error(
            "mpkit::save_study: cannot write " + p.string());
    }
    f << study_to_sexpr(s);
}

Study load_study(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    const fs::path p = dir / "study.mpstudy";
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        throw std::runtime_error(
            "mpkit::load_study: cannot read " + p.string());
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return study_from_sexpr(ss.str());
}

}  // namespace mpkit
