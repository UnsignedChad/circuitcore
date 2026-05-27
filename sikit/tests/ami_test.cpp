// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "si/Ami.h"

using namespace sikit::ibis::ami;
using Catch::Approx;

constexpr auto kAmiSample = R"(
(My_FFE_Model
    (Reserved_Parameters
        (AMI_Version          (Usage Info) (Type String) (Default "7.0"))
        (Init_Returns_Impulse (Usage Info) (Type Boolean) (Default True))
        (GetWave_Exists       (Usage Info) (Type Boolean) (Default True))
    )
    (Model_Specific
        (Tap1 (Usage In) (Type Float) (Range 0.0 -0.5 0.5) (Default 0.0))
        (Tap2 (Usage In) (Type Float) (Range 0.1 -0.3 0.3) (Default 0.1))
        (Mode (Usage In) (Type Integer) (Default 1)
              (Description "0=bypass, 1=normal"))
    )
)
)";

TEST_CASE("ami parser: model name + sections", "[ami]") {
    auto f = AmiParser::read_string(kAmiSample);
    REQUIRE(f.model_name == "My_FFE_Model");
    REQUIRE(f.reserved.size() == 3);
    REQUIRE(f.model_specific.size() == 3);
}

TEST_CASE("ami parser: usage + type per parameter", "[ami]") {
    auto f = AmiParser::read_string(kAmiSample);

    // Reserved
    REQUIRE(f.reserved[0].name == "AMI_Version");
    REQUIRE(f.reserved[0].usage == ParamUsage::Info);
    REQUIRE(f.reserved[0].type  == ParamType::String);
    REQUIRE(f.reserved[1].usage == ParamUsage::Info);
    REQUIRE(f.reserved[1].type  == ParamType::Boolean);

    // Model-specific
    REQUIRE(f.model_specific[0].name == "Tap1");
    REQUIRE(f.model_specific[0].usage == ParamUsage::In);
    REQUIRE(f.model_specific[0].type  == ParamType::Float);
    REQUIRE(f.model_specific[2].type  == ParamType::Integer);
}

TEST_CASE("ami parser: ranges parsed (typ, min, max)", "[ami]") {
    auto f = AmiParser::read_string(kAmiSample);
    REQUIRE(f.model_specific[0].has_range);
    REQUIRE(f.model_specific[0].range_typ == Approx(0.0));
    REQUIRE(f.model_specific[0].range_min == Approx(-0.5));
    REQUIRE(f.model_specific[0].range_max == Approx(0.5));
    REQUIRE(f.model_specific[1].range_typ == Approx(0.1));
    REQUIRE(f.model_specific[1].range_min == Approx(-0.3));
}

TEST_CASE("ami parser: defaults captured", "[ami]") {
    auto f = AmiParser::read_string(kAmiSample);
    REQUIRE(f.model_specific[1].default_value == "0.1");
    REQUIRE(f.model_specific[2].default_value == "1");
}

TEST_CASE("ami parser: description field captured", "[ami]") {
    auto f = AmiParser::read_string(kAmiSample);
    REQUIRE(f.model_specific[2].description.find("normal") != std::string::npos);
}

TEST_CASE("ami parser: missing file throws", "[ami]") {
    REQUIRE_THROWS_AS(AmiParser::read_file("/nonexistent/path.ami"),
                       AmiParseError);
}

// ---- Loader integration test (uses tests/stub_ami.so built by CMake) ----

TEST_CASE("ami loader: stub library round-trips through Init + GetWave", "[ami]") {
    // The stub is built next to the test binary by CMake. Find it across
    // the various working-directory conventions Catch2 / ctest / direct
    // execution use; skip silently if none of them turn it up (CI may
    // run on a system that can't build .so files).
    namespace fs = std::filesystem;
    const fs::path candidates[] = {
        fs::current_path() / "stub_ami.so",
        fs::current_path() / "tests" / "stub_ami.so",
        fs::current_path() / "build" / "tests" / "stub_ami.so",
    };
    fs::path so;
    for (const auto& c : candidates) {
        if (fs::exists(c)) { so = c; break; }
    }
    if (so.empty()) {
        SUCCEED("stub_ami.so not present at expected paths — skipping");
        return;
    }

    AmiModel m(so);
    REQUIRE(m.init_available());
    REQUIRE(m.close_available());
    REQUIRE(m.has_get_wave());

    std::vector<double> impulse(16, 1.0);
    auto ir = m.init(impulse, 16, 1, 1e-12, 100e-12, "(tap_test)");
    REQUIRE(ir.return_code == 1);
    // The stub scales the impulse by 0.8.
    REQUIRE(impulse[0] == Approx(0.8));
    REQUIRE(ir.parameters_out.find("Tap1") != std::string::npos);

    std::vector<double> wave(32, 0.0);
    std::vector<double> clocks(32, 0.0);
    auto gr = m.get_wave(wave, clocks);
    REQUIRE(gr.return_code == 1);
    // The stub adds 0.05 to each sample.
    REQUIRE(wave[0] == Approx(0.05));
}
