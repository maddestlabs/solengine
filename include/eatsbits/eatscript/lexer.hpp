#ifndef EATS_LEXER_HPP
#define EATS_LEXER_HPP

#include <string>
#include <vector>
#include <stack>
#include "token.hpp"

namespace eatsbits::eatscript {

class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> tokenize();

private:
    char peek() const noexcept;
    char peekNext() const noexcept;
    char advance() noexcept;
    bool isAtEnd() const noexcept;
    bool match(char expected) noexcept;

    void scanToken();
    void scanIdentifierOrKeyword();
    void scanNumber();
    void scanString(char quoteChar);
    void handleIndentation();

    std::string source_;
    size_t start_{0};
    size_t current_{0};
    uint32_t line_{1};
    uint32_t column_{1};

    std::vector<Token> tokens_;
    std::vector<uint32_t> indentStack_;
    bool atLineStart_{true};
    int bracketNesting_{0};
};

} // namespace eatsbits::eatscript

#endif // EATS_LEXER_HPP
