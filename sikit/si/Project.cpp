#include "si/Project.h"

#include <fstream>
#include <sstream>

#include "circuitcore/sexpr/SExpr.h"

namespace sikit::project {

namespace {

// Find the first child of `node` whose tag matches `tag`. Returns nullptr
// if none — fields are optional, callers handle the missing case.
const circuitcore::sexpr::Node* find_child(const circuitcore::sexpr::Node& node, std::string_view tag) {
    if (!node.is_list()) return nullptr;
    for (const auto& c : node.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

// Extract a string-or-symbol payload from a single-arg form like
// (file "foo.ibs"). Returns empty string if the form is malformed.
std::string get_string_arg(const circuitcore::sexpr::Node& node) {
    if (!node.is_list() || node.children.size() < 2) return {};
    const auto& v = node.children[1];
    if (v.is_string() || v.is_symbol()) return v.text;
    return {};
}

bool get_bool_arg(const circuitcore::sexpr::Node& node) {
    if (!node.is_list() || node.children.size() < 2) return false;
    const auto& v = node.children[1];
    if (v.is_symbol()) {
        return v.text == "true" || v.text == "yes" || v.text == "on" ||
               v.text == "1";
    }
    if (v.is_number()) return v.number != 0.0;
    return false;
}

// Escape a string for S-expression output: wrap in quotes, escape backslash
// and double-quote. Newlines are encoded literally — the parser handles
// them fine and round-trip stays clean.
std::string sx_quote(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

}  // namespace

std::string serialize_to_string(const Project& p) {
    std::ostringstream os;
    os << "(sikit-project\n";
    os << "  (version " << p.version << ")\n";
    if (!p.kicad_pcb.empty()) {
        os << "  (kicad-pcb " << sx_quote(p.kicad_pcb) << ")\n";
    }
    if (p.ibis) {
        os << "  (ibis (file " << sx_quote(p.ibis->file) << ")";
        if (!p.ibis->model.empty()) {
            os << " (model " << sx_quote(p.ibis->model) << ")";
        }
        os << ")\n";
    }
    if (p.ami) {
        os << "  (ami (params "  << sx_quote(p.ami->params) << ")"
           <<      " (library " << sx_quote(p.ami->library) << "))\n";
    }
    os << "  (engine (use-fdm " << (p.use_fdm ? "true" : "false") << "))\n";
    if (!p.observed_nets.empty()) {
        os << "  (observed-nets";
        for (const auto& n : p.observed_nets) {
            os << ' ' << sx_quote(n);
        }
        os << ")\n";
    }
    os << ")\n";
    return os.str();
}

Project parse_from_string(const std::string& src) {
    circuitcore::sexpr::Node root;
    try {
        root = circuitcore::sexpr::parse(src);
    } catch (const circuitcore::sexpr::ParseError& e) {
        throw ProjectIoError(std::string("malformed .sikitproj: ") + e.what());
    }
    if (!root.is_list() || root.tag() != "sikit-project") {
        throw ProjectIoError("not a sikit-project file (root tag mismatch)");
    }

    Project p;
    if (const auto* v = find_child(root, "version")) {
        if (v->children.size() >= 2 && v->children[1].is_number()) {
            p.version = static_cast<int>(v->children[1].number);
        }
    }
    if (const auto* k = find_child(root, "kicad-pcb")) {
        p.kicad_pcb = get_string_arg(*k);
    }
    if (const auto* ibis = find_child(root, "ibis")) {
        IbisRef r;
        if (const auto* f = find_child(*ibis, "file"))  r.file  = get_string_arg(*f);
        if (const auto* m = find_child(*ibis, "model")) r.model = get_string_arg(*m);
        if (!r.file.empty()) p.ibis = std::move(r);
    }
    if (const auto* ami = find_child(root, "ami")) {
        AmiRef r;
        if (const auto* pf = find_child(*ami, "params"))  r.params  = get_string_arg(*pf);
        if (const auto* lf = find_child(*ami, "library")) r.library = get_string_arg(*lf);
        if (!r.params.empty() || !r.library.empty()) p.ami = std::move(r);
    }
    if (const auto* eng = find_child(root, "engine")) {
        if (const auto* uf = find_child(*eng, "use-fdm")) p.use_fdm = get_bool_arg(*uf);
    }
    if (const auto* nets = find_child(root, "observed-nets")) {
        for (std::size_t i = 1; i < nets->children.size(); ++i) {
            const auto& c = nets->children[i];
            if (c.is_string() || c.is_symbol()) p.observed_nets.push_back(c.text);
        }
    }
    return p;
}

Project load_project(const std::filesystem::path& path) {
    std::ifstream is(path);
    if (!is) {
        throw ProjectIoError("could not open " + path.string());
    }
    std::ostringstream ss;
    ss << is.rdbuf();
    return parse_from_string(ss.str());
}

void save_project(const Project& p, const std::filesystem::path& path) {
    std::ofstream os(path, std::ios::trunc);
    if (!os) {
        throw ProjectIoError("could not write " + path.string());
    }
    os << serialize_to_string(p);
}

}  // namespace sikit::project
