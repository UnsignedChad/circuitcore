#include <catch2/catch_test_macros.hpp>

#include "si/DiffPair.h"

using namespace sikit::highspeed;
using namespace circuitcore::board;

namespace {
Board make_board() {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu", "signal"});
    return b;
}
}

TEST_CASE("diff: _P/_N pair detected", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "USB_DP_P"});
    b.nets.push_back({2, "USB_DP_N"});
    b.nets.push_back({3, "GND"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].net_p_id == 1);
    REQUIRE(pairs[0].net_n_id == 2);
    REQUIRE(pairs[0].base_name == "USB_DP");
    REQUIRE(pairs[0].suffix_style == "_P/_N");
}

TEST_CASE("diff: +/- suffix style", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "D+"});
    b.nets.push_back({2, "D-"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].base_name == "D");
    REQUIRE(pairs[0].suffix_style == "+/-");
}

TEST_CASE("diff: _POS/_NEG suffix style", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "RX_POS"});
    b.nets.push_back({2, "RX_NEG"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].suffix_style == "_POS/_NEG");
}

TEST_CASE("diff: DP/DM suffix style (no separator)", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "USBDP"});
    b.nets.push_back({2, "USBDM"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].base_name == "USB");
    REQUIRE(pairs[0].suffix_style == "DP/DM");
}

TEST_CASE("diff: multiple pairs in one board", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "USB_DP"});
    b.nets.push_back({2, "USB_DM"});
    b.nets.push_back({3, "PCIE_TX_P"});
    b.nets.push_back({4, "PCIE_TX_N"});
    b.nets.push_back({5, "PCIE_RX_P"});
    b.nets.push_back({6, "PCIE_RX_N"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 3);
}

TEST_CASE("diff: unpaired _P net is not reported", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "FOO_P"});
    b.nets.push_back({2, "BAR"});  // no FOO_N

    REQUIRE(find_diff_pairs(b).empty());
}

TEST_CASE("diff: empty net name skipped", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({0, ""});       // unconnected/no-net
    b.nets.push_back({1, "FOO_P"});
    b.nets.push_back({2, "FOO_N"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 1);
}

TEST_CASE("diff: prefer more-specific suffix (_POS over P)", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "CLK_POS"});
    b.nets.push_back({2, "CLK_NEG"});

    auto pairs = find_diff_pairs(b);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].suffix_style == "_POS/_NEG");
    REQUIRE(pairs[0].base_name == "CLK");
}

TEST_CASE("highspeed keyword: protocol names match", "[diffpair]") {
    REQUIRE(looks_high_speed("USB_DP"));
    REQUIRE(looks_high_speed("PCIE_TX_P"));
    REQUIRE(looks_high_speed("DDR4_DQ0"));
    REQUIRE(looks_high_speed("hdmi_clk"));
    REQUIRE(looks_high_speed("MIPI_CSI_D0_P"));
    REQUIRE(looks_high_speed("LVDS_RX0+"));
    REQUIRE(looks_high_speed("SATA_RX_P"));
}

TEST_CASE("highspeed keyword: non-matches", "[diffpair]") {
    REQUIRE_FALSE(looks_high_speed("GND"));
    REQUIRE_FALSE(looks_high_speed("VCC"));
    REQUIRE_FALSE(looks_high_speed("+3V3"));
    REQUIRE_FALSE(looks_high_speed("RESET_N"));
    REQUIRE_FALSE(looks_high_speed(""));
}

TEST_CASE("find_high_speed_nets includes diff-pair members and keyword hits", "[diffpair]") {
    Board b = make_board();
    b.nets.push_back({1, "USB_DP_P"});
    b.nets.push_back({2, "USB_DP_N"});
    b.nets.push_back({3, "GND"});
    b.nets.push_back({4, "VCC"});
    b.nets.push_back({5, "PCIE_REFCLK"});  // keyword-only, no pair

    auto hs = find_high_speed_nets(b);
    // 1, 2, 5 should be in; 3, 4 should not.
    std::sort(hs.begin(), hs.end());
    REQUIRE(hs.size() == 3);
    REQUIRE(hs[0] == 1);
    REQUIRE(hs[1] == 2);
    REQUIRE(hs[2] == 5);
}
