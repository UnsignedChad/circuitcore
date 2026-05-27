// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include "circuitcore/sexpr/SExpr.h"

using circuitcore::sexpr::Node;
using circuitcore::sexpr::ParseError;
using circuitcore::sexpr::parse;

TEST_CASE("sexpr: empty list", "[sexpr]") {
    Node n = parse("()");
    REQUIRE(n.is_list());
    REQUIRE(n.children.empty());
}

TEST_CASE("sexpr: single symbol in list", "[sexpr]") {
    Node n = parse("(kicad_pcb)");
    REQUIRE(n.is_list());
    REQUIRE(n.children.size() == 1);
    REQUIRE(n.children[0].is_symbol());
    REQUIRE(n.children[0].text == "kicad_pcb");
    REQUIRE(n.tag() == "kicad_pcb");
}

TEST_CASE("sexpr: numbers (int, float, negative)", "[sexpr]") {
    Node n = parse("(values 1 2.5 -3 -0.25 +7 1e3)");
    REQUIRE(n.children.size() == 7);
    REQUIRE(n.children[1].is_number());
    REQUIRE(n.children[1].number == 1.0);
    REQUIRE(n.children[2].number == 2.5);
    REQUIRE(n.children[3].number == -3.0);
    REQUIRE(n.children[4].number == -0.25);
    REQUIRE(n.children[5].number == 7.0);
    REQUIRE(n.children[6].number == 1000.0);
}

TEST_CASE("sexpr: strings with escapes", "[sexpr]") {
    Node n = parse(R"((net 1 "GND" "with\"quote" "tab\there"))");
    REQUIRE(n.children.size() == 5);
    REQUIRE(n.children[2].is_string());
    REQUIRE(n.children[2].text == "GND");
    REQUIRE(n.children[3].text == "with\"quote");
    REQUIRE(n.children[4].text == "tab\there");
}

TEST_CASE("sexpr: nested lists", "[sexpr]") {
    Node n = parse("(a (b (c 1) (d 2)) e)");
    REQUIRE(n.children.size() == 3);
    REQUIRE(n.children[1].is_list());
    REQUIRE(n.children[1].tag() == "b");
    REQUIRE(n.children[1].children.size() == 3);
    REQUIRE(n.children[1].children[1].is_list());
    REQUIRE(n.children[1].children[1].tag() == "c");
    REQUIRE(n.children[1].children[1].children[1].number == 1.0);
}

TEST_CASE("sexpr: KiCad-flavored snippet", "[sexpr]") {
    constexpr auto src = R"(
        (kicad_pcb
            (version 20240108)
            (generator "pcbnew")
            (layers
                (0 "F.Cu" signal)
                (31 "B.Cu" signal)
            )
            (net 0 "")
            (net 1 "GND")
        )
    )";
    Node root = parse(src);
    REQUIRE(root.tag() == "kicad_pcb");
    REQUIRE(root.children.size() == 6);  // tag + version + generator + layers + 2 nets

    // version
    const Node& version = root.children[1];
    REQUIRE(version.tag() == "version");
    REQUIRE(version.children[1].number == 20240108.0);

    // layers
    const Node& layers = root.children[3];
    REQUIRE(layers.tag() == "layers");
    REQUIRE(layers.children.size() == 3);  // tag + 2 layer rows
    const Node& f_cu = layers.children[1];
    REQUIRE(f_cu.children[0].number == 0.0);
    REQUIRE(f_cu.children[1].text == "F.Cu");
    REQUIRE(f_cu.children[2].text == "signal");
    REQUIRE(f_cu.children[2].is_symbol());  // bare, not quoted

    // empty string is parsed as string atom
    const Node& net0 = root.children[4];
    REQUIRE(net0.children[2].is_string());
    REQUIRE(net0.children[2].text.empty());
}

TEST_CASE("sexpr: line comments are ignored", "[sexpr]") {
    Node n = parse("(a ; comment to end of line\n b c)");
    REQUIRE(n.children.size() == 3);
    REQUIRE(n.children[1].text == "b");
}

TEST_CASE("sexpr: error on unterminated list", "[sexpr]") {
    REQUIRE_THROWS_AS(parse("(a (b c"), ParseError);
}

TEST_CASE("sexpr: error on unterminated string", "[sexpr]") {
    REQUIRE_THROWS_AS(parse(R"((a "unterminated))"), ParseError);
}

TEST_CASE("sexpr: trailing junk after the main form is tolerated", "[sexpr]") {
    // Once the main form parses, anything past it (sibling forms, stray
    // tokens, syntax errors near EOF) is swallowed so real-world KiCad
    // files with minor end-of-file imbalance still load.
    auto a = parse("(kicad_pcb (version 1))");
    REQUIRE(a.tag() == "kicad_pcb");
    auto b = parse("(kicad_pcb (version 1)) (extra junk) )");
    REQUIRE(b.tag() == "kicad_pcb");
    auto c = parse("(kicad_pcb (v 1)) garbage ( ( (");
    REQUIRE(c.tag() == "kicad_pcb");
}

TEST_CASE("sexpr: error on empty input", "[sexpr]") {
    REQUIRE_THROWS_AS(parse(""), ParseError);
}

TEST_CASE("sexpr: line/col tracking", "[sexpr]") {
    Node n = parse("(a\n  b\n  c)");
    REQUIRE(n.line == 1);
    REQUIRE(n.col == 1);
    REQUIRE(n.children[1].line == 2);
    REQUIRE(n.children[1].col == 3);
    REQUIRE(n.children[2].line == 3);
}
