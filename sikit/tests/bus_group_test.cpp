#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "si/BusGroup.h"
#include "si/SiStackup.h"
#include "si/TraceImpedance.h"
#include "circuitcore/board/Board.h"

using sikit::si::compute_bus_groups;
using sikit::si::BusGroup;
using Catch::Approx;

namespace {

sikit::analysis::AnalysisStackup canonical_microstrip() {
    sikit::analysis::AnalysisStackup s;
    s.outer_dielectric_height = 0.2e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.3;
    return s;
}

void add_net(circuitcore::board::Board& b, int id, const std::string& name,
              double length_m) {
    b.nets.push_back({id, name});
    circuitcore::board::Segment s;
    s.start = {0, 0};
    s.end   = {length_m, 0};
    s.width = 0.20e-3;
    s.layer_ordinal = 0;
    s.net_id = id;
    b.segments.push_back(s);
}

circuitcore::board::Board base_board() {
    circuitcore::board::Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal", 35e-6,
                                  "copper", 0.0, 0.0});
    b.stackup.total_thickness = 1.6e-3;
    return b;
}

const BusGroup* find_bus(const std::vector<BusGroup>& bs,
                          const std::string& base) {
    for (const auto& g : bs) if (g.base_name == base) return &g;
    return nullptr;
}

}  // namespace

TEST_CASE("bus: groups DDR_DQ0..3 as a single bus", "[bus]") {
    auto b = base_board();
    add_net(b, 1, "DDR_DQ0", 50e-3);
    add_net(b, 2, "DDR_DQ1", 50.1e-3);
    add_net(b, 3, "DDR_DQ2", 49.9e-3);
    add_net(b, 4, "DDR_DQ3", 50.0e-3);
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    const auto* dq = find_bus(groups, "DDR_DQ");
    REQUIRE(dq != nullptr);
    REQUIRE(dq->members.size() == 4);
}

TEST_CASE("bus: min / max length report the bus extremes", "[bus]") {
    auto b = base_board();
    add_net(b, 1, "DDR_DQ0", 50.0e-3);
    add_net(b, 2, "DDR_DQ1", 50.1e-3);
    add_net(b, 3, "DDR_DQ2", 49.5e-3);   // shortest
    add_net(b, 4, "DDR_DQ3", 50.3e-3);   // longest
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    const auto* dq = find_bus(groups, "DDR_DQ");
    REQUIRE(dq != nullptr);
    REQUIRE(dq->min_length_m == Approx(49.5e-3).margin(1e-6));
    REQUIRE(dq->max_length_m == Approx(50.3e-3).margin(1e-6));
    REQUIRE(dq->skew_m == Approx(0.8e-3).margin(1e-6));
}

TEST_CASE("bus: skew_ps + budget flag set correctly", "[bus]") {
    auto b = base_board();
    add_net(b, 1, "DDR_DQ0", 50.0e-3);
    add_net(b, 2, "DDR_DQ1", 60.0e-3);   // 10 mm spread -- ~67 ps at FR-4 microstrip
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    const auto* dq = find_bus(groups, "DDR_DQ");
    REQUIRE(dq != nullptr);
    REQUIRE(dq->skew_ps > 50.0);     // well above 10 ps budget
    REQUIRE(dq->exceeds_budget);
}

TEST_CASE("bus: single-member group is not reported", "[bus]") {
    auto b = base_board();
    add_net(b, 1, "DDR_DQ0", 50.0e-3);
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    REQUIRE(find_bus(groups, "DDR_DQ") == nullptr);
}

TEST_CASE("bus: nets without a trailing integer are skipped", "[bus]") {
    auto b = base_board();
    add_net(b, 1, "USB_DP", 30e-3);
    add_net(b, 2, "USB_DN", 30e-3);
    add_net(b, 3, "SCK", 20e-3);
    add_net(b, 4, "MOSI", 25e-3);
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    REQUIRE(groups.empty());
}

TEST_CASE("bus: diff pairs collapse to a single member", "[bus]") {
    // PCIE_TX_LANE0_P/_N, PCIE_TX_LANE1_P/_N, ... should detect as
    // diff pairs first, then group by base = "PCIE_TX_LANE" via the
    // pair base name's trailing index.
    auto b = base_board();
    add_net(b, 1, "PCIE_TX_LANE0_P", 40e-3);
    add_net(b, 2, "PCIE_TX_LANE0_N", 40e-3);
    add_net(b, 3, "PCIE_TX_LANE1_P", 42e-3);
    add_net(b, 4, "PCIE_TX_LANE1_N", 42e-3);
    add_net(b, 5, "PCIE_TX_LANE2_P", 41e-3);
    add_net(b, 6, "PCIE_TX_LANE2_N", 41e-3);
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    const auto* lanes = find_bus(groups, "PCIE_TX_LANE");
    REQUIRE(lanes != nullptr);
    REQUIRE(lanes->members.size() == 3);
    for (const auto& m : lanes->members) REQUIRE(m.is_diff_pair);
    // Indices come through sorted.
    REQUIRE(lanes->members[0].index == 0);
    REQUIRE(lanes->members[1].index == 1);
    REQUIRE(lanes->members[2].index == 2);
}

TEST_CASE("bus: empty board returns empty result", "[bus]") {
    circuitcore::board::Board b;
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    REQUIRE(groups.empty());
}

TEST_CASE("bus: zero-length skew gives zero ps + no budget flag", "[bus]") {
    auto b = base_board();
    add_net(b, 1, "DDR_DQ0", 50e-3);
    add_net(b, 2, "DDR_DQ1", 50e-3);
    add_net(b, 3, "DDR_DQ2", 50e-3);
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    const auto* dq = find_bus(groups, "DDR_DQ");
    REQUIRE(dq != nullptr);
    REQUIRE(std::abs(dq->skew_m) < 1e-9);
    REQUIRE(std::abs(dq->skew_ps) < 1e-3);
    REQUIRE_FALSE(dq->exceeds_budget);
}

TEST_CASE("bus: members come back sorted by index", "[bus]") {
    auto b = base_board();
    // Add out of order.
    add_net(b, 1, "DDR_DQ3", 50e-3);
    add_net(b, 2, "DDR_DQ0", 50e-3);
    add_net(b, 3, "DDR_DQ7", 50e-3);
    add_net(b, 4, "DDR_DQ1", 50e-3);
    auto groups = compute_bus_groups(b, canonical_microstrip(), 10.0);
    const auto* dq = find_bus(groups, "DDR_DQ");
    REQUIRE(dq != nullptr);
    REQUIRE(dq->members[0].index == 0);
    REQUIRE(dq->members[1].index == 1);
    REQUIRE(dq->members[2].index == 3);
    REQUIRE(dq->members[3].index == 7);
}
