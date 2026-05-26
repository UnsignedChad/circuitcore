// Round-trip tests for Study + FieldIO.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "circuitcore/field/Field3D.h"
#include "mp/FieldIO.h"
#include "mp/Study.h"
#include "mp/StudySerial.h"

using mpkit::CouplingSpec;
using mpkit::Grid;
using mpkit::PhysicsKind;
using mpkit::PhysicsNode;
using mpkit::StoredField;
using mpkit::Study;
using mpkit::SweepSpec;

namespace {

std::filesystem::path scratch_dir() {
    auto base = std::filesystem::temp_directory_path() /
                ("mpkit_study_test_"
                 + std::to_string(static_cast<long>(std::rand())));
    std::filesystem::create_directories(base);
    return base;
}

}  // namespace

TEST_CASE("Study sexpr round-trip preserves every field") {
    Study s;
    s.name = "Cascade study";
    PhysicsNode n0;
    n0.id = "ir"; n0.label = "PDN IR drop"; n0.kind = PhysicsKind::PdnIrDrop;
    s.nodes.push_back(n0);
    PhysicsNode n1;
    n1.id = "heat"; n1.label = "Steady thermal"; n1.kind = PhysicsKind::SteadyHeat;
    s.nodes.push_back(n1);

    CouplingSpec c;
    c.source_node_id = "ir"; c.source_output  = "voltages";
    c.target_node_id = "heat"; c.target_input = "volumetric_source";
    c.transform      = "joule";
    s.couplings.push_back(c);

    s.solve_order = {"ir", "heat"};

    SweepSpec sw;
    sw.parameter_path = "node:ir/config/total_current";
    sw.values = {5.0, 10.0, 15.0};
    s.sweeps.push_back(sw);

    StoredField rf;
    rf.node_id = "heat"; rf.output_name = "temperature";
    rf.sweep_index = 0; rf.path = "results/0_temperature.mpfield";
    s.result_files.push_back(rf);

    const std::string text = mpkit::study_to_sexpr(s);
    Study round = mpkit::study_from_sexpr(text);

    REQUIRE(round.name == s.name);
    REQUIRE(round.nodes.size() == s.nodes.size());
    REQUIRE(round.nodes[0].id == "ir");
    REQUIRE(round.nodes[1].kind == PhysicsKind::SteadyHeat);
    REQUIRE(round.couplings.size() == 1u);
    REQUIRE(round.couplings[0].source_output == "voltages");
    REQUIRE(round.couplings[0].transform == "joule");
    REQUIRE(round.solve_order.size() == 2u);
    REQUIRE(round.solve_order[1] == "heat");
    REQUIRE(round.sweeps.size() == 1u);
    REQUIRE(round.sweeps[0].values.size() == 3u);
    REQUIRE(round.sweeps[0].values[2] == 15.0);
    REQUIRE(round.result_files.size() == 1u);
    REQUIRE(round.result_files[0].path == "results/0_temperature.mpfield");
}

TEST_CASE("save_study + load_study on disk round-trips identically") {
    Study s; s.name = "Disk round-trip";
    PhysicsNode n;
    n.id = "elastic"; n.label = "Elasticity"; n.kind = PhysicsKind::Elasticity;
    s.nodes.push_back(n);

    auto dir = scratch_dir();
    mpkit::save_study(s, dir);
    REQUIRE(std::filesystem::exists(dir / "study.mpstudy"));

    Study back = mpkit::load_study(dir);
    REQUIRE(back.name == "Disk round-trip");
    REQUIRE(back.nodes.size() == 1u);
    REQUIRE(back.nodes[0].kind == PhysicsKind::Elasticity);

    std::filesystem::remove_all(dir);
}

TEST_CASE("FieldIO save + load preserves shape, spacing and every value") {
    Grid g;
    g.spec = {4, 3, 2, 1.0e-3, 0.5e-3, 2.0e-3};
    g.x0 = -1.0e-3; g.y0 = 0.0; g.z0 = 5.0e-4;
    circuitcore::field::Field3D f(g.nx(), g.ny(), g.nz());
    for (int k = 0; k < g.nz(); ++k)
        for (int j = 0; j < g.ny(); ++j)
            for (int i = 0; i < g.nx(); ++i)
                f.at(i, j, k) = 100.0 * k + 10.0 * j + i + 0.5;

    auto path = scratch_dir() / "f.mpfield";
    mpkit::save_field(g, f, path);

    auto [g2, f2] = mpkit::load_field(path);
    REQUIRE(g2.nx() == g.nx());
    REQUIRE(g2.ny() == g.ny());
    REQUIRE(g2.nz() == g.nz());
    REQUIRE(g2.dx() == g.dx());
    REQUIRE(g2.x0 == g.x0);
    for (int k = 0; k < g.nz(); ++k)
        for (int j = 0; j < g.ny(); ++j)
            for (int i = 0; i < g.nx(); ++i)
                REQUIRE(f2.at(i, j, k) == f.at(i, j, k));

    std::filesystem::remove(path);
    std::filesystem::remove(path.parent_path());
}

TEST_CASE("FieldIO rejects bad magic with a clear error") {
    auto path = scratch_dir() / "bogus.mpfield";
    {
        std::ofstream bad(path, std::ios::binary);
        bad << "NOPEnotamagic";  // anything that is not MPFD\1
    }
    REQUIRE_THROWS_AS(mpkit::load_field(path), std::runtime_error);
    std::filesystem::remove(path);
    std::filesystem::remove(path.parent_path());
}
