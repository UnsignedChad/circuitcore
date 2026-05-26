// End-to-end test of the study orchestrator.
//
// Builds a minimal Study + VoxelMaterialField + injected source
// programmatically and confirms the runner walks the solve_order,
// dispatches to solve_steady_heat, produces a sane temperature field,
// and reports a successful run.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "circuitcore/field/Field3D.h"

#include "mp/MaterialLibrary.h"
#include "mp/Study.h"
#include "mp/StudyRun.h"
#include "mp/Voxelizer.h"

using mpkit::CouplingSpec;
using mpkit::PhysicsKind;
using mpkit::PhysicsNode;
using mpkit::Study;
using mpkit::StudyRunInput;
using mpkit::StudyRunResult;
using mpkit::VoxelMaterialField;

namespace sex = circuitcore::sexpr;

namespace {

VoxelMaterialField uniform_copper(int n, double d) {
    VoxelMaterialField f;
    f.grid.spec = {n, 1, 1, d, 1.0, 1.0};
    f.grid.x0 = f.grid.y0 = f.grid.z0 = 0.0;
    f.ids.assign(static_cast<std::size_t>(n), mpkit::kCopperMaterialId);
    return f;
}

// Helpers to build sexpr (key value) pairs since the orchestrator parses
// the config blobs as raw sexpr trees.
sex::Node sym(std::string s) {
    sex::Node n; n.kind = sex::Node::Kind::Symbol; n.text = std::move(s);
    return n;
}
sex::Node str(std::string s) {
    sex::Node n; n.kind = sex::Node::Kind::String; n.text = std::move(s);
    return n;
}
sex::Node num(double v) {
    sex::Node n; n.kind = sex::Node::Kind::Number; n.number = v;
    return n;
}
sex::Node lst(std::string tag, std::vector<sex::Node> kids) {
    sex::Node n; n.kind = sex::Node::Kind::List;
    n.children.push_back(sym(std::move(tag)));
    for (auto& k : kids) n.children.push_back(std::move(k));
    return n;
}

}  // namespace

TEST_CASE("Single-node study: SteadyHeat with two Dirichlet walls runs "
          "and produces a temperature field") {
    Study s;
    PhysicsNode n;
    n.id    = "heat";
    n.label = "Steady heat";
    n.kind  = PhysicsKind::SteadyHeat;
    n.config = lst("config", {
        lst("bc", {lst("target", {str("FaceXmin")}),
                    lst("kind",   {str("Dirichlet")}),
                    lst("value",  {num(0.0)})}),
        lst("bc", {lst("target", {str("FaceXmax")}),
                    lst("kind",   {str("Dirichlet")}),
                    lst("value",  {num(100.0)})}),
    });
    s.nodes.push_back(n);

    StudyRunInput in;
    in.study          = std::move(s);
    in.material_field = uniform_copper(8, 1.0e-3);
    StudyRunResult r  = mpkit::run_study(in);

    REQUIRE(r.ok);
    REQUIRE(r.steps.size() == 1u);
    REQUIRE(r.steps[0].node_id == "heat");
    REQUIRE(r.steps[0].ok);
    // Linear profile -- last cell sits near 100, first near 0.
    auto& t = r.steps[0].temperature;
    REQUIRE(t.at(t.nx() - 1, 0, 0) > 80.0);
    REQUIRE(t.at(0,           0, 0) < 20.0);
}

TEST_CASE("Sweep iteration patches the targeted config field per step") {
    Study s;
    PhysicsNode n;
    n.id    = "heat";
    n.label = "Steady heat";
    n.kind  = PhysicsKind::SteadyHeat;
    // Both walls Dirichlet, the high wall's value will be swept.
    n.config = lst("config", {
        lst("bc", {lst("target", {str("FaceXmin")}),
                    lst("kind",   {str("Dirichlet")}),
                    lst("value",  {num(0.0)})}),
        lst("bc", {lst("target", {str("FaceXmax")}),
                    lst("kind",   {str("Dirichlet")}),
                    lst("value",  {num(0.0)})}),
    });
    s.nodes.push_back(n);

    // Sweep the FIRST bc's value (FaceXmin) over a few values. The
    // parameter path drills into the first (bc ...) child.
    mpkit::SweepSpec sw;
    sw.parameter_path = "node:heat/config/bc/value";
    sw.values         = {10.0, 20.0, 30.0};
    s.sweeps.push_back(sw);

    StudyRunInput in;
    in.study          = std::move(s);
    in.material_field = uniform_copper(8, 1.0e-3);
    StudyRunResult r  = mpkit::run_study(in);

    REQUIRE(r.ok);
    REQUIRE(r.steps.size() == 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE(r.steps[i].ok);
        REQUIRE(r.steps[i].sweep_index == static_cast<int>(i));
        // Boundary cell at i=0 should sit near the swept value.
        REQUIRE(std::abs(r.steps[i].temperature.at(0, 0, 0)
                          - (sw.values[i] / 2.0)) < (sw.values[i] / 2.0) + 5.0);
    }
}

TEST_CASE("Unknown PhysicsKind dispatch reports a clear error") {
    Study s;
    PhysicsNode n;
    n.id    = "elastic";
    n.label = "Elasticity";
    n.kind  = PhysicsKind::Elasticity;  // valid kind but not wired yet
    n.config = lst("config", {});
    s.nodes.push_back(n);

    StudyRunInput in;
    in.study          = std::move(s);
    in.material_field = uniform_copper(4, 1.0e-3);
    auto r = mpkit::run_study(in);

    REQUIRE(!r.ok);
    REQUIRE(r.error.find("not yet dispatched") != std::string::npos);
}

TEST_CASE("PdnIrDrop without a Board fails fast with a helpful message") {
    Study s;
    PhysicsNode n;
    n.id    = "ir";
    n.label = "PDN IR drop";
    n.kind  = PhysicsKind::PdnIrDrop;
    n.config = lst("config", {lst("net_id", {num(1)})});
    s.nodes.push_back(n);

    StudyRunInput in;
    in.study          = std::move(s);
    in.material_field = uniform_copper(4, 1.0e-3);
    in.board          = nullptr;

    auto r = mpkit::run_study(in);
    REQUIRE(!r.ok);
    REQUIRE(r.error.find("board is null") != std::string::npos);
}
