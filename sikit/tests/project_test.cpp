// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "si/Project.h"

using namespace sikit::project;

TEST_CASE("project: round-trip a minimal project", "[project]") {
    Project p;
    p.kicad_pcb = "boards/eval.kicad_pcb";
    auto s = serialize_to_string(p);
    auto q = parse_from_string(s);
    REQUIRE(q.version == 1);
    REQUIRE(q.kicad_pcb == "boards/eval.kicad_pcb");
    REQUIRE_FALSE(q.ibis.has_value());
    REQUIRE_FALSE(q.ami.has_value());
    REQUIRE(q.observed_nets.empty());
}

TEST_CASE("project: round-trip with IBIS, AMI, observed nets, FDM on",
          "[project]") {
    Project p;
    p.kicad_pcb = "/abs/path/board.kicad_pcb";
    p.ibis = IbisRef{"drv.ibs", "TX_3V3"};
    p.ami  = AmiRef{"drv.ami", "drv.so"};
    p.use_fdm = true;
    p.observed_nets = {"USB_DP", "USB_DN", "CLK0_P"};

    auto s = serialize_to_string(p);
    auto q = parse_from_string(s);
    REQUIRE(q.kicad_pcb == "/abs/path/board.kicad_pcb");
    REQUIRE(q.ibis.has_value());
    REQUIRE(q.ibis->file  == "drv.ibs");
    REQUIRE(q.ibis->model == "TX_3V3");
    REQUIRE(q.ami.has_value());
    REQUIRE(q.ami->params  == "drv.ami");
    REQUIRE(q.ami->library == "drv.so");
    REQUIRE(q.use_fdm);
    REQUIRE(q.observed_nets.size() == 3);
    REQUIRE(q.observed_nets[1] == "USB_DN");
}

TEST_CASE("project: rejects non-sikit S-expression", "[project]") {
    REQUIRE_THROWS_AS(parse_from_string("(some-other-format (version 1))"),
                      ProjectIoError);
    REQUIRE_THROWS_AS(parse_from_string("(((("), ProjectIoError);
}

TEST_CASE("project: file save/load round-trip", "[project]") {
    auto tmp = std::filesystem::temp_directory_path() / "sikit_proj_test.sikitproj";
    Project p;
    p.kicad_pcb = "x.kicad_pcb";
    p.use_fdm = true;
    save_project(p, tmp);
    auto q = load_project(tmp);
    REQUIRE(q.kicad_pcb == "x.kicad_pcb");
    REQUIRE(q.use_fdm);
    std::filesystem::remove(tmp);
}

TEST_CASE("project: tolerates missing optional sections", "[project]") {
    const std::string minimal =
        "(sikit-project (version 1) (kicad-pcb \"b.kicad_pcb\"))";
    auto q = parse_from_string(minimal);
    REQUIRE(q.kicad_pcb == "b.kicad_pcb");
    REQUIRE_FALSE(q.ibis.has_value());
    REQUIRE_FALSE(q.ami.has_value());
    REQUIRE_FALSE(q.use_fdm);
}

TEST_CASE("project: bool field accepts true/yes/on/1", "[project]") {
    for (const char* val : {"true", "yes", "on", "1"}) {
        std::string src = std::string("(sikit-project (engine (use-fdm ") +
                          val + ")))";
        auto q = parse_from_string(src);
        REQUIRE(q.use_fdm);
    }
    auto q = parse_from_string("(sikit-project (engine (use-fdm false)))");
    REQUIRE_FALSE(q.use_fdm);
}
