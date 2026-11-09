#ifndef EATS_TOKEN_HPP
#define EATS_TOKEN_HPP

#include <string>
#include <string_view>

namespace eatsbits::eatscript {

enum class TokenType {
    // End of file & Layout
    EndOfFile,
    Newline,
    Indent,
    Dedent,

    // Literals & Identifiers
    Identifier,
    Number,
    String,
    TrueLiteral,
    FalseLiteral,

    // Keywords
    Def,
    Return,
    If,
    Else,
    Elif,
    While,
    For,
    In,
    And,
    Or,
    Not,
    Import,

    // Operators
    Plus,           // +
    Minus,          // -
    Multiply,       // *
    Divide,         // /
    Modulo,         // %
    Power,          // **
    Assign,         // =
    Equals,         // ==
    NotEquals,      // !=
    Less,           // <
    LessEqual,      // <=
    Greater,        // >
    GreaterEqual,   // >=

    // Delimiters
    LeftParen,      // (
    RightParen,     // )
    LeftBracket,    // [
    RightBracket,   // ]
    LeftBrace,      // {
    RightBrace,     // }
    Comma,          // ,
    Colon,          // :
    Dot             // .
};

struct Token {
    TokenType type{TokenType::EndOfFile};
    std::string text;
    double numberValue{0.0};
    uint32_t line{1};
    uint32_t column{1};
};

const char* tokenTypeToString(TokenType type);

} // namespace eatsbits::eatscript

#endif // EATS_TOKEN_HPP
