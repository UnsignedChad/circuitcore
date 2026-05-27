// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "circuitcore/sexpr/SExpr.h"

using circuitcore::sexpr::Node;
using circuitcore::sexpr::parse;
using circuitcore::sexpr::emit;

namespace {

// Structural equality: same kind + same atoms + recursively-equal children.
// Ignores source line/col (the emitter produces a fresh tree).
bool tree_equal(const Node& a, const Node& b) {
    if (a.kind != b.kind) return false;
    using K = Node::Kind;
    switch (a.kind) {
        case K::Symbol:
        case K::String: return a.text == b.text;
        case K::Number: return a.number == b.number;
        case K::List:   break;
    }
    if (a.children.size() != b.children.size()) return false;
    for (std::size_t i = 0; i < a.children.size(); ++i) {
        if (!tree_equal(a.children[i], b.children[i])) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("emit: simple inline form", "[sexpr][emit]") {
    auto n = parse("(foo 1 2 3)");
    REQUIRE(emit(n) == "(foo 1 2 3)\n");
}

TEST_CASE("emit: nested lists break onto their own lines",
          "[sexpr][emit]") {
    auto n = parse("(setup (stackup (layer F.Cu) (layer B.Cu)))");
    const std::string out = emit(n);
    INFO(out);
    // Has at least one newline (block form fired).
    REQUIRE(out.find('\n') != std::string::npos);
    // Round-trip: re-parse and assert structural equality.
    auto n2 = parse(out);
    REQUIRE(tree_equal(n, n2));
}

TEST_CASE("emit: strings stay quoted, escapes round-trip",
          "[sexpr][emit]") {
    auto n = parse(R"((name "F&B.Cu") (slogan "say "hi""))");
    auto out = emit(n);
    auto n2 = parse(out);
    REQUIRE(tree_equal(n, n2));
}

TEST_CASE("emit: numbers use the shortest round-trippable form",
          "[sexpr][emit]") {
    auto n = parse("(coords 1.5 0.51 100 3.14159265358979)");
    const std::string out = emit(n);
    INFO(out);
    REQUIRE(out.find("1.5") != std::string::npos);
    REQUIRE(out.find("0.51") != std::string::npos);
    REQUIRE(out.find("100") != std::string::npos);
    // No "1.500000000000" cruft.
    REQUIRE(out.find("1.500") == std::string::npos);
}

TEST_CASE("emit: empty list emits ()", "[sexpr][emit]") {
    auto n = parse("(foo () (bar))");
    auto n2 = parse(emit(n));
    REQUIRE(tree_equal(n, n2));
}

TEST_CASE("emit: round-trips a kicad_pcb-shaped tree",
          "[sexpr][emit][validation]") {
    // Mirrors the shape of a real .kicad_pcb top form.
    auto n = parse(R"(
(kicad_pcb
    (version 20240108)
    (generator "pcbnew")
    (general (thickness 1.6))
    (layers
        (0 "F.Cu" signal)
        (31 "B.Cu" signal)
    )
    (setup
        (stackup
            (layer "F.Cu" (type "copper") (thickness 0.035))
            (layer "dielectric 1" (type "core")
                                  (thickness 0.51)
                                  (material "FR4")
                                  (epsilon_r 4.5))
            (layer "B.Cu" (type "copper") (thickness 0.035))
        )
    )
)
)");
    const std::string out = emit(n);
    auto n2 = parse(out);
    REQUIRE(tree_equal(n, n2));
}
