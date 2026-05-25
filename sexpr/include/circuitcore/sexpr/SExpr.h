// Minimal S-expression parser for KiCad .kicad_pcb files.
//
// KiCad's PCB format is plain S-expressions:
//   (kicad_pcb (version 20240108) (layers (0 "F.Cu" signal) ...) ...)
//
// Atoms come in three flavors:
//   * Symbol  : bare identifier (kicad_pcb, signal, F.Cu)
//   * String  : double-quoted, supports \" \ \n \t escapes
//   * Number  : integer or float, optionally signed

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace circuitcore::sexpr {

struct ParseError : std::runtime_error {
    ParseError(std::string msg, std::size_t line, std::size_t col);
    std::size_t line;
    std::size_t col;
};

struct Node {
    enum class Kind { List, Symbol, String, Number };

    Kind kind = Kind::List;
    std::string text;             // Symbol or String payload
    double number = 0.0;          // Number payload
    std::vector<Node> children;   // List payload
    std::size_t line = 0;         // 1-based source line of first token
    std::size_t col = 0;          // 1-based source column of first token

    bool is_list() const noexcept { return kind == Kind::List; }
    bool is_symbol() const noexcept { return kind == Kind::Symbol; }
    bool is_string() const noexcept { return kind == Kind::String; }
    bool is_number() const noexcept { return kind == Kind::Number; }

    // For list nodes whose first child is a symbol (the form tag, like 'kicad_pcb').
    // Returns empty string_view if the list is empty or untagged.
    std::string_view tag() const noexcept;
};

// Parse a complete S-expression source into a single top-level Node.
// Throws ParseError on malformed input. The returned Node is typically a List.
Node parse(std::string_view src);

// Formatting options for the emitter. Defaults match KiCad's pcbnew save
// style (tab indent, line-broken outer forms, atoms inline) so round-tripping
// a .kicad_pcb produces a file pcbnew will accept without complaint.
struct EmitConfig {
    // String used per indent level. KiCad uses "\t".
    std::string indent = "\t";

    // If a list contains nested lists, each child goes on its own line.
    // If the list is "small" (only atoms, total inline width below this
    // threshold), it stays on one line.
    int inline_width_threshold = 80;
};

// Serialize a Node tree back to text. Round-trip safe in the sense that
// parse(emit(N)) yields a tree structurally equal to N for any N parsed
// from a .kicad_pcb. Whitespace and comments from the original source are
// not preserved -- the output is canonically formatted.
std::string emit(const Node& root, const EmitConfig& cfg = {});

}  // namespace circuitcore::sexpr
