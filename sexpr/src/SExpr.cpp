#include "circuitcore/sexpr/SExpr.h"

#include <cctype>
#include <charconv>
#include <format>
#include <string>

namespace circuitcore::sexpr {

ParseError::ParseError(std::string msg, std::size_t ln, std::size_t cl)
    : std::runtime_error(std::format("sexpr parse error at line {}, col {}: {}", ln, cl, msg)),
      line(ln),
      col(cl) {}

std::string_view Node::tag() const noexcept {
    if (kind != Kind::List || children.empty()) return {};
    const Node& head = children.front();
    if (head.kind != Kind::Symbol) return {};
    return head.text;
}

namespace {

class Lexer {
public:
    explicit Lexer(std::string_view src) : src_(src) {}

    enum class Tok { LParen, RParen, Symbol, String, Number, End };

    struct Token {
        Tok kind;
        std::string text;
        double number = 0.0;
        std::size_t line;
        std::size_t col;
    };

    Token next() {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
            return {Tok::End, {}, 0.0, line_, col_};
        }

        const std::size_t tok_line = line_;
        const std::size_t tok_col = col_;
        const char c = src_[pos_];

        if (c == '(') {
            advance();
            return {Tok::LParen, "(", 0.0, tok_line, tok_col};
        }
        if (c == ')') {
            advance();
            return {Tok::RParen, ")", 0.0, tok_line, tok_col};
        }
        if (c == '"') {
            return read_string(tok_line, tok_col);
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
            Token t = try_read_number(tok_line, tok_col);
            if (t.kind != Tok::End) return t;
        }
        return read_symbol(tok_line, tok_col);
    }

private:
    void advance() {
        if (pos_ >= src_.size()) return;
        if (src_[pos_] == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        ++pos_;
    }

    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                advance();
            } else if (c == '#' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '|') {
                advance();
                advance();
                while (pos_ + 1 < src_.size() && !(src_[pos_] == '|' && src_[pos_ + 1] == '#')) {
                    advance();
                }
                if (pos_ + 1 < src_.size()) {
                    advance();
                    advance();
                }
            } else if (c == ';') {
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
            } else {
                break;
            }
        }
    }

    Token read_string(std::size_t tok_line, std::size_t tok_col) {
        advance();  // consume opening quote
        std::string out;
        while (pos_ < src_.size() && src_[pos_] != '"') {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                char esc = src_[pos_ + 1];
                advance();
                advance();
                switch (esc) {
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case '\\': out += '\\'; break;
                    case '"':  out += '"'; break;
                    default:   out += esc; break;
                }
            } else {
                out += src_[pos_];
                advance();
            }
        }
        if (pos_ >= src_.size()) {
            throw ParseError("unterminated string literal", tok_line, tok_col);
        }
        advance();  // consume closing quote
        return {Tok::String, std::move(out), 0.0, tok_line, tok_col};
    }

    // Attempt to scan a number. If the candidate token isn't a valid number,
    // returns {End,...} so the caller can fall back to symbol parsing.
    Token try_read_number(std::size_t tok_line, std::size_t tok_col) {
        std::size_t start = pos_;
        std::size_t end = pos_;
        while (end < src_.size() && !is_delim(src_[end])) ++end;

        std::string_view candidate = src_.substr(start, end - start);
        std::string_view to_parse = candidate;
        // std::from_chars rejects a leading '+' for floats; strip it.
        if (!to_parse.empty() && to_parse.front() == '+') {
            to_parse.remove_prefix(1);
            if (to_parse.empty()) return {Tok::End, {}, 0.0, tok_line, tok_col};
        }
        double value = 0.0;
        auto [ptr, ec] = std::from_chars(to_parse.data(), to_parse.data() + to_parse.size(), value);
        if (ec == std::errc() && ptr == to_parse.data() + to_parse.size()) {
            while (pos_ < end) advance();
            return {Tok::Number, std::string(candidate), value, tok_line, tok_col};
        }
        return {Tok::End, {}, 0.0, tok_line, tok_col};
    }

    Token read_symbol(std::size_t tok_line, std::size_t tok_col) {
        std::string out;
        while (pos_ < src_.size() && !is_delim(src_[pos_])) {
            out += src_[pos_];
            advance();
        }
        if (out.empty()) {
            throw ParseError("unexpected character", tok_line, tok_col);
        }
        return {Tok::Symbol, std::move(out), 0.0, tok_line, tok_col};
    }

    static bool is_delim(char c) {
        return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '"';
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t col_ = 1;
};

Node parse_node(Lexer& lex, Lexer::Token first);

Node parse_list(Lexer& lex, std::size_t open_line, std::size_t open_col) {
    Node list;
    list.kind = Node::Kind::List;
    list.line = open_line;
    list.col = open_col;
    while (true) {
        auto tok = lex.next();
        if (tok.kind == Lexer::Tok::RParen) return list;
        if (tok.kind == Lexer::Tok::End) {
            throw ParseError("unterminated list (missing ')')", open_line, open_col);
        }
        list.children.push_back(parse_node(lex, std::move(tok)));
    }
}

Node parse_node(Lexer& lex, Lexer::Token first) {
    Node n;
    n.line = first.line;
    n.col = first.col;
    switch (first.kind) {
        case Lexer::Tok::LParen:
            return parse_list(lex, first.line, first.col);
        case Lexer::Tok::Symbol:
            n.kind = Node::Kind::Symbol;
            n.text = std::move(first.text);
            return n;
        case Lexer::Tok::String:
            n.kind = Node::Kind::String;
            n.text = std::move(first.text);
            return n;
        case Lexer::Tok::Number:
            n.kind = Node::Kind::Number;
            n.text = std::move(first.text);
            n.number = first.number;
            return n;
        case Lexer::Tok::RParen:
            throw ParseError("unexpected ')'", first.line, first.col);
        case Lexer::Tok::End:
            throw ParseError("unexpected end of input", first.line, first.col);
    }
    throw ParseError("internal: unknown token kind", first.line, first.col);
}

}  // namespace

Node parse(std::string_view src) {
    Lexer lex(src);
    auto first = lex.next();
    if (first.kind == Lexer::Tok::End) {
        throw ParseError("empty input", 1, 1);
    }
    Node root = parse_node(lex, std::move(first));
    // Once the main form has been parsed successfully, anything past it is
    // best-effort: real-world KiCad output sometimes contains sibling
    // top-level forms (embedded_fonts, 3D model entries) AND occasional
    // dangling close-parens or syntax errors near EOF. Swallow them rather
    // than refuse the whole load -- the user wants the board parsed.
    try {
        auto next = lex.next();
        while (next.kind != Lexer::Tok::End) {
            if (next.kind == Lexer::Tok::RParen) break;
            (void)parse_node(lex, std::move(next));
            next = lex.next();
        }
    } catch (const ParseError&) {
        // Discard trailing errors silently.
    }
    return root;
}

}  // namespace circuitcore::sexpr
