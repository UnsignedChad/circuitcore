#include "mp/StudyRun.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "mp/JouleCoupling.h"
#include "mp/MaterialLibrary.h"
#include "mp/SteadyHeat.h"

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

namespace mpkit {

namespace {

using circuitcore::sexpr::Node;

// -- small sexpr helpers ----------------------------------------------

const Node* find_child(const Node& parent, std::string_view tag) {
    if (!parent.is_list()) return nullptr;
    for (const auto& c : parent.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}
double num_field(const Node& parent, std::string_view tag, double fallback) {
    const Node* c = find_child(parent, tag);
    if (!c || c->children.size() < 2) return fallback;
    return c->children[1].is_number() ? c->children[1].number : fallback;
}
int int_field(const Node& parent, std::string_view tag, int fallback) {
    return static_cast<int>(num_field(parent, tag, fallback));
}
std::string str_field(const Node& parent, std::string_view tag,
                      std::string fallback) {
    const Node* c = find_child(parent, tag);
    if (!c || c->children.size() < 2) return fallback;
    const Node& v = c->children[1];
    if (v.is_string() || v.is_symbol()) return v.text;
    return fallback;
}

bool set_number_at(Node& root, const std::vector<std::string>& path,
                    double value) {
    Node* cursor = &root;
    for (const auto& key : path) {
        if (!cursor->is_list()) return false;
        Node* next = nullptr;
        for (auto& c : cursor->children) {
            if (c.is_list() && c.tag() == key) { next = &c; break; }
        }
        if (!next) return false;
        cursor = next;
    }
    if (cursor->children.size() < 2) return false;
    Node& v = cursor->children[1];
    if (!v.is_number()) return false;
    v.number = value;
    return true;
}

struct ParamPath {
    std::string              node_id;
    std::vector<std::string> keys;
};
std::optional<ParamPath> parse_param_path(const std::string& s) {
    if (s.rfind("node:", 0) != 0) return std::nullopt;
    auto slash = s.find('/', 5);
    if (slash == std::string::npos) return std::nullopt;
    ParamPath out;
    out.node_id = s.substr(5, slash - 5);
    std::string rest = s.substr(slash + 1);
    if (rest.rfind("config", 0) != 0) return std::nullopt;
    if (rest.size() == 6) return out;
    if (rest[6] != '/') return std::nullopt;
    rest = rest.substr(7);
    while (!rest.empty()) {
        auto sl = rest.find('/');
        if (sl == std::string::npos) { out.keys.push_back(rest); break; }
        out.keys.push_back(rest.substr(0, sl));
        rest = rest.substr(sl + 1);
    }
    return out;
}

BcTarget bc_target_from_string(const std::string& s) {
    if (s == "FaceXmin") return BcTarget::FaceXmin;
    if (s == "FaceXmax") return BcTarget::FaceXmax;
    if (s == "FaceYmin") return BcTarget::FaceYmin;
    if (s == "FaceYmax") return BcTarget::FaceYmax;
    if (s == "FaceZmin") return BcTarget::FaceZmin;
    if (s == "FaceZmax") return BcTarget::FaceZmax;
    return BcTarget::VoxelRange;
}
BcKind bc_kind_from_string(const std::string& s) {
    if (s == "Dirichlet") return BcKind::Dirichlet;
    if (s == "Neumann")   return BcKind::Neumann;
    return BcKind::Robin;
}

SteadyHeatConfig build_steady_heat_config(
    const Node& cfg,
    const VoxelMaterialField& vmf,
    const std::vector<Material>& mt,
    const circuitcore::field::Field3D& source_or_empty) {
    SteadyHeatConfig hc;
    hc.material_field    = vmf;
    hc.material_table    = mt;
    hc.volumetric_source = source_or_empty;
    for (const auto& c : cfg.children) {
        if (!c.is_list() || c.tag() != "bc") continue;
        BoundaryCondition bc;
        bc.target = bc_target_from_string(str_field(c, "target", "FaceXmin"));
        bc.kind   = bc_kind_from_string  (str_field(c, "kind",   "Dirichlet"));
        bc.value  = num_field(c, "value", 0.0);
        bc.h      = num_field(c, "h",     0.0);
        bc.u_ref  = num_field(c, "u_ref", 0.0);
        hc.bcs.push_back(bc);
    }
    return hc;
}

struct IrRunOutput {
    pdnkit::pi::IrMesh   mesh;
    pdnkit::pi::Solution solution;
    bool        ok = false;
    std::string error;
};

IrRunOutput run_ir_drop(const Node& cfg,
                         const circuitcore::board::Board* board) {
    IrRunOutput out;
    if (!board) {
        out.error = "PdnIrDrop: StudyRunInput::board is null but a "
                    "PdnIrDrop node was requested";
        return out;
    }
    pdnkit::pi::MeshConfig mc;
    mc.net_id        = int_field(cfg, "net_id", 0);
    mc.layer_ordinal = int_field(cfg, "layer_ordinal", 0);
    mc.cell_size     = num_field(cfg, "cell_size", 5.0e-4);
    out.mesh = pdnkit::pi::IrMesher::build(*board, mc);
    if (out.mesh.nodes.empty()) {
        out.error = "PdnIrDrop: IrMesher returned empty mesh "
                    "(check net_id / layer_ordinal / cell_size vs board)";
        return out;
    }
    pdnkit::pi::SolveConfig sc;
    sc.total_current = num_field(cfg, "total_current", 1.0);
    out.solution = pdnkit::pi::IrSolver::solve(out.mesh, sc);
    if (!out.solution.ok) {
        out.error = "PdnIrDrop: IrSolver failed -- " + out.solution.error;
        return out;
    }
    out.ok = true;
    return out;
}

struct OneSweepResult {
    std::vector<StudyStepResult> steps;
    bool        ok = false;
    std::string error;
};

OneSweepResult run_one_sweep(const Study& s,
                              const StudyRunInput& input,
                              int sweep_index,
                              const std::vector<Material>& mt) {
    OneSweepResult out;
    const VoxelMaterialField& vmf = input.material_field;

    std::unordered_map<std::string, IrRunOutput>                  ir_cache;
    std::unordered_map<std::string, circuitcore::field::Field3D>  temp_cache;

    std::vector<std::string> order = s.solve_order;
    if (order.empty()) for (const auto& n : s.nodes) order.push_back(n.id);

    std::unordered_map<std::string, const PhysicsNode*> by_id;
    for (const auto& n : s.nodes) by_id[n.id] = &n;

    // Helper to push the failing step + propagate error. Captures the
    // error string BEFORE moving the step (otherwise the error is
    // read from a moved-from std::string -- usually empty).
    auto fail_step = [&](StudyStepResult&& step) {
        const std::string err = step.error;
        out.steps.push_back(std::move(step));
        out.error = err;
    };

    for (const auto& node_id : order) {
        auto it = by_id.find(node_id);
        if (it == by_id.end()) {
            out.error = "solve_order references unknown node '" + node_id + "'";
            return out;
        }
        const PhysicsNode& node = *it->second;
        StudyStepResult step;
        step.node_id     = node_id;
        step.sweep_index = sweep_index;

        // Resolve inbound couplings -> typed-input fields.
        circuitcore::field::Field3D heat_source;
        for (const auto& cp : s.couplings) {
            if (cp.target_node_id != node_id) continue;
            if (cp.transform == "joule") {
                auto src = ir_cache.find(cp.source_node_id);
                if (src == ir_cache.end()) {
                    step.error = "joule coupling references source node '"
                                 + cp.source_node_id
                                 + "' that has not run yet (check solve_order)";
                    fail_step(std::move(step));
                    return out;
                }
                auto j = ir_solution_to_joule_source(
                    src->second.mesh, src->second.solution, vmf);
                if (!j.ok) {
                    step.error = "joule coupling failed: " + j.error;
                    fail_step(std::move(step));
                    return out;
                }
                heat_source              = j.source;
                step.total_joule_power_w = j.total_power_w;
            } else if (cp.transform.empty()) {
                auto src = temp_cache.find(cp.source_node_id);
                if (src != temp_cache.end()) heat_source = src->second;
            }
        }

        switch (node.kind) {
            case PhysicsKind::PdnIrDrop: {
                auto ir = run_ir_drop(node.config, input.board);
                step.ok    = ir.ok;
                step.error = ir.error;
                if (ir.ok) {
                    step.voltages = ir.solution.voltages;
                    ir_cache[node_id] = std::move(ir);
                }
                if (!step.ok) {
                    fail_step(std::move(step));
                    return out;
                }
                out.steps.push_back(std::move(step));
                break;
            }
            case PhysicsKind::SteadyHeat: {
                auto hc = build_steady_heat_config(
                    node.config, vmf, mt, heat_source);
                auto r = solve_steady_heat(hc);
                step.ok          = r.ok;
                step.error       = r.error;
                step.temperature = r.temperature;
                if (r.ok) temp_cache[node_id] = r.temperature;
                if (!r.ok) {
                    step.error = "SteadyHeat node '" + node_id + "': " + r.error;
                    fail_step(std::move(step));
                    return out;
                }
                out.steps.push_back(std::move(step));
                break;
            }
            default:
                step.error = std::string("PhysicsKind ")
                              + to_string(node.kind)
                              + " is not yet dispatched by the runner";
                fail_step(std::move(step));
                return out;
        }
    }

    out.ok = true;
    return out;
}

}  // namespace

StudyRunResult run_study(const StudyRunInput& input) {
    StudyRunResult out;

    std::vector<Material> mt = input.material_table;
    if (mt.empty()) {
        mt.resize(3);
        mt[kAirMaterialId]       = air();
        mt[kSubstrateMaterialId] = fr4();
        mt[kCopperMaterialId]    = copper();
    }

    if (input.study.sweeps.empty()) {
        auto one = run_one_sweep(input.study, input, 0, mt);
        for (auto& s : one.steps) out.steps.push_back(std::move(s));
        out.ok    = one.ok;
        out.error = one.error;
        return out;
    }

    const auto& sw = input.study.sweeps.front();
    auto pp = parse_param_path(sw.parameter_path);
    if (!pp) {
        out.error = "Unparseable sweep parameter_path: " + sw.parameter_path;
        return out;
    }

    for (int i = 0; i < static_cast<int>(sw.values.size()); ++i) {
        Study s_copy = input.study;
        bool patched = false;
        for (auto& n : s_copy.nodes) {
            if (n.id != pp->node_id) continue;
            if (set_number_at(n.config, pp->keys, sw.values[i])) patched = true;
            break;
        }
        if (!patched) {
            out.error = "Sweep parameter '" + sw.parameter_path
                       + "' not found or not numeric";
            return out;
        }
        StudyRunInput per_step = input;
        per_step.study = std::move(s_copy);
        auto one = run_one_sweep(per_step.study, per_step, i, mt);
        for (auto& s : one.steps) out.steps.push_back(std::move(s));
        if (!one.ok) { out.error = one.error; return out; }
    }
    out.ok = true;
    return out;
}

}  // namespace mpkit
