#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

#include "si/Compliance.h"
#include "si/Eye.h"
#include "si/EyeMask.h"

using namespace sikit::specs;
using Catch::Approx;

TEST_CASE("compliance: every shipped spec carries a usable mask",
          "[compliance]") {
    for (const auto* s : all_compliance_specs()) {
        REQUIRE_FALSE(s->name.empty());
        REQUIRE_FALSE(s->family.empty());
        REQUIRE(s->baud_hz > 0.0);
        REQUIRE(s->ui_seconds == Approx(1.0 / s->baud_hz));
        REQUIRE(s->reference_impedance > 0.0);
        REQUIRE(s->ber_target > 0.0);
        REQUIRE(s->ber_target < 1.0);
        REQUIRE_FALSE(s->mask.polygon.empty());
        REQUIRE(s->mask.polygon.size() >= 3);
        REQUIRE_FALSE(s->source.empty());
    }
}

TEST_CASE("compliance: name lookup retrieves each shipped spec",
          "[compliance]") {
    for (const auto* s : all_compliance_specs()) {
        const auto* by_name = compliance_by_name(s->name);
        REQUIRE(by_name == s);
    }
    REQUIRE(compliance_by_name("not a real spec") == nullptr);
}

TEST_CASE("compliance: spec names are unique", "[compliance]") {
    std::set<std::string> names;
    for (const auto* s : all_compliance_specs()) {
        const auto [_, inserted] = names.insert(s->name);
        REQUIRE(inserted);
    }
    REQUIRE(names.size() == all_compliance_specs().size());
}

TEST_CASE("compliance: PCIe generations have strictly increasing baud",
          "[compliance][pcie]") {
    REQUIRE(pcie_gen3().baud_hz < pcie_gen4().baud_hz);
    REQUIRE(pcie_gen4().baud_hz < pcie_gen5().baud_hz);
    // PCIe Gen6 uses PAM4 so the symbol rate stays at Gen5's value
    // even though the bits/sec doubles. We assert the symbol rate
    // (which baud_hz tracks) is at least Gen5's.
    REQUIRE(pcie_gen6().baud_hz >= pcie_gen5().baud_hz);
}

TEST_CASE("compliance: PAM4 specs are flagged is_pam4=true",
          "[compliance][pam4]") {
    REQUIRE(pcie_gen6().is_pam4);
    REQUIRE(ethernet_50gbase_kr().is_pam4);
    // NRZ specs are not flagged.
    REQUIRE_FALSE(pcie_gen5().is_pam4);
    REQUIRE_FALSE(ddr4_3200().is_pam4);
    REQUIRE_FALSE(usb31_gen2().is_pam4);
}

TEST_CASE("compliance: spec families cover the listed standards",
          "[compliance]") {
    std::set<std::string> families;
    for (const auto* s : all_compliance_specs()) {
        families.insert(s->family);
    }
    REQUIRE(families.contains("PCIe"));
    REQUIRE(families.contains("DDR"));
    REQUIRE(families.contains("USB"));
    REQUIRE(families.contains("HDMI"));
    REQUIRE(families.contains("Ethernet"));
}

TEST_CASE("compliance: mask polygons are non-degenerate", "[compliance]") {
    // For every spec, no two consecutive vertices coincide and the
    // polygon has positive area (i.e. the spec author didn't paste an
    // empty mask in by mistake).
    for (const auto* s : all_compliance_specs()) {
        const auto& p = s->mask.polygon;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto& a = p[i];
            const auto& b = p[(i + 1) % p.size()];
            const double dt = a.first  - b.first;
            const double dv = a.second - b.second;
            REQUIRE(dt * dt + dv * dv > 1e-12);
        }
        // Shoelace area.
        double area = 0.0;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const auto& a = p[i];
            const auto& b = p[(i + 1) % p.size()];
            area += a.first * b.second - b.first * a.second;
        }
        REQUIRE(std::abs(area) > 1e-6);
    }
}

TEST_CASE("compliance: every mask centre is at the eye-centre keep-out",
          "[compliance]") {
    // Every shipped mask is a centred hexagon / diamond shape; the
    // geometric centre (0.5 UI, 0 V) should sit INSIDE the forbidden
    // region (that's the whole point of an eye mask). Catches authoring
    // mistakes where the polygon got reflected or shifted.
    for (const auto* s : all_compliance_specs()) {
        REQUIRE(point_in_polygon(0.5, 0.0, s->mask.polygon));
    }
}

TEST_CASE("compliance: a clean centred eye passes every NRZ spec",
          "[compliance]") {
    // Build a totally-open synthetic eye: two horizontal stripes at +1
    // and -1, no transitions through the centre. Every NRZ compliance
    // spec should record zero violations against this.
    sikit::eye::EyeGrid g;
    g.time_bins = 128;
    g.volt_bins = 96;
    g.v_min = -1.5;
    g.v_max = 1.5;
    g.counts.assign(g.time_bins * g.volt_bins, 0);
    const int v_top = static_cast<int>(0.95 * g.volt_bins);
    const int v_bot = static_cast<int>(0.05 * g.volt_bins);
    for (int t = 0; t < g.time_bins; ++t) {
        g.at(t, v_top) = 100;
        g.at(t, v_bot) = 100;
    }
    for (const auto* s : all_compliance_specs()) {
        if (s->is_pam4) continue;
        REQUIRE(passes(g, s->mask));
    }
}
