#include "eatsbits/eatscript/lexer.hpp"
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace eatsbits::eatscript {

const char* tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Newline: return "Newline";
        case TokenType::Indent: return "Indent";
        case TokenType::Dedent: return "Dedent";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";
        case TokenType::TrueLiteral: return "True";
        case TokenType::FalseLiteral: return "False";
        case TokenType::Def: return "def";
        case TokenType::Return: return "return";
        case TokenType::If: return "if";
        case TokenType::Else: return "else";
        case TokenType::Elif: return "elif";
        case TokenType::While: return "while";
        case TokenType::For: return "for";
        case TokenType::In: return "in";
        case TokenType::And: return "and";
        case TokenType::Or: return "or";
        case TokenType::Not: return "not";
        case TokenType::Import: return "import";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Multiply: return "*";
        case TokenType::Divide: return "/";
        case TokenType::Modulo: return "%";
        case TokenType::Power: return "**";
        case TokenType::Assign: return "=";
        case TokenType::Equals: return "==";
        case TokenType::NotEquals: return "!=";
        case TokenType::Less: return "<";
        case TokenType::LessEqual: return "<=";
        case TokenType::Greater: return ">";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::LeftParen: return "(";
        case TokenType::RightParen: return ")";
        case TokenType::LeftBracket: return "[";
        case TokenType::RightBracket: return "]";
        case TokenType::LeftBrace: return "{";
        case TokenType::RightBrace: return "}";
        case TokenType::Comma: return ",";
        case TokenType::Colon: return ":";
        case TokenType::Dot: return ".";
    }
    return "Unknown";
}

static const std::unordered_map<std::string, TokenType> kKeywords = {
    {"def", TokenType::Def},
    {"return", TokenType::Return},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"elif", TokenType::Elif},
    {"while", TokenType::While},
    {"for", TokenType::For},
    {"in", TokenType::In},
    {"and", TokenType::And},
    {"or", TokenType::Or},
    {"not", TokenType::Not},
    {"import", TokenType::Import},
    {"True", TokenType::TrueLiteral},
    {"False", TokenType::FalseLiteral},
    {"true", TokenType::TrueLiteral},
    {"false", TokenType::FalseLiteral},
};

Lexer::Lexer(std::string source) : source_(std::move(source)) {
    indentStack_.push_back(0);
}

std::vector<Token> Lexer::tokenize() {
    tokens_.clear();

    while (!isAtEnd()) {
        if (atLineStart_) {
            handleIndentation();
            atLineStart_ = false;
            if (isAtEnd()) break;
        }

        start_ = current_;
        scanToken();
    }

    // Emit remaining dedents
    while (indentStack_.size() > 1) {
        indentStack_.pop_back();
        Token dedent;
        dedent.type = TokenType::Dedent;
        dedent.line = line_;
        dedent.column = column_;
        tokens_.push_back(dedent);
    }

    Token eof;
    eof.type = TokenType::EndOfFile;
    eof.line = line_;
    eof.column = column_;
    tokens_.push_back(eof);

    return tokens_;
}

char Lexer::peek() const noexcept {
    if (isAtEnd()) return '\0';
    return source_[current_];
}

char Lexer::peekNext() const noexcept {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char Lexer::advance() noexcept {
    char c = source_[current_++];
    column_++;
    return c;
}

bool Lexer::isAtEnd() const noexcept {
    return current_ >= source_.size();
}

bool Lexer::match(char expected) noexcept {
    if (isAtEnd()) return false;
    if (source_[current_] != expected) return false;
    current_++;
    column_++;
    return true;
}

void Lexer::handleIndentation() {
    if (bracketNesting_ > 0) return;

    uint32_t spaces = 0;
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ') {
            advance();
            spaces++;
        } else if (c == '\t') {
            advance();
            spaces += 4; // Standard 4 spaces per tab
        } else if (c == '\r') {
            advance();
        } else if (c == '\n') {
            advance();
            line_++;
            column_ = 1;
            spaces = 0; // Blank line, restart indentation count
        } else if (c == '#') {
            // Comment on empty line, skip to end of line
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            if (!isAtEnd() && peek() == '\n') {
                advance();
                line_++;
                column_ = 1;
                spaces = 0;
            }
        } else {
            break;
        }
    }

    if (isAtEnd()) return;

    uint32_t currentIndent = indentStack_.back();
    if (spaces > currentIndent) {
        indentStack_.push_back(spaces);
        Token t;
        t.type = TokenType::Indent;
        t.line = line_;
        t.column = spaces;
        tokens_.push_back(t);
    } else if (spaces < currentIndent) {
        while (indentStack_.size() > 1 && indentStack_.back() > spaces) {
            indentStack_.pop_back();
            Token t;
            t.type = TokenType::Dedent;
            t.line = line_;
            t.column = spaces;
            tokens_.push_back(t);
        }
    }
}

void Lexer::scanToken() {
    char c = advance();

    switch (c) {
        case ' ':
        case '\t':
        case '\r':
            // Ignore inline whitespace
            break;

        case '\n': {
            if (bracketNesting_ == 0) {
                Token t;
                t.type = TokenType::Newline;
                t.line = line_;
                t.column = column_;
                tokens_.push_back(t);
            }
            line_++;
            column_ = 1;
            atLineStart_ = (bracketNesting_ == 0);
            break;
        }

        case '#':
            // Comment until newline
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            break;

        case '+': {
            Token t{TokenType::Plus, "+", 0.0, line_, column_};
            tokens_.push_back(t);
            break;
        }
        case '-': {
            Token t{TokenType::Minus, "-", 0.0, line_, column_};
            tokens_.push_back(t);
            break;
        }
        case '*': {
            if (match('*')) {
                tokens_.push_back(Token{TokenType::Power, "**", 0.0, line_, column_});
            } else {
                tokens_.push_back(Token{TokenType::Multiply, "*", 0.0, line_, column_});
            }
            break;
        }
        case '/': {
            tokens_.push_back(Token{TokenType::Divide, "/", 0.0, line_, column_});
            break;
        }
        case '%': {
            tokens_.push_back(Token{TokenType::Modulo, "%", 0.0, line_, column_});
            break;
        }
        case '=': {
            if (match('=')) {
                tokens_.push_back(Token{TokenType::Equals, "==", 0.0, line_, column_});
            } else {
                tokens_.push_back(Token{TokenType::Assign, "=", 0.0, line_, column_});
            }
            break;
        }
        case '!': {
            if (match('=')) {
                tokens_.push_back(Token{TokenType::NotEquals, "!=", 0.0, line_, column_});
            }
            break;
        }
        case '<': {
            if (match('=')) {
                tokens_.push_back(Token{TokenType::LessEqual, "<=", 0.0, line_, column_});
            } else {
                tokens_.push_back(Token{TokenType::Less, "<", 0.0, line_, column_});
            }
            break;
        }
        case '>': {
            if (match('=')) {
                tokens_.push_back(Token{TokenType::GreaterEqual, ">=", 0.0, line_, column_});
            } else {
                tokens_.push_back(Token{TokenType::Greater, ">", 0.0, line_, column_});
            }
            break;
        }
        case '(': bracketNesting_++; tokens_.push_back(Token{TokenType::LeftParen, "(", 0.0, line_, column_}); break;
        case ')': if (bracketNesting_ > 0) bracketNesting_--; tokens_.push_back(Token{TokenType::RightParen, ")", 0.0, line_, column_}); break;
        case '[': bracketNesting_++; tokens_.push_back(Token{TokenType::LeftBracket, "[", 0.0, line_, column_}); break;
        case ']': if (bracketNesting_ > 0) bracketNesting_--; tokens_.push_back(Token{TokenType::RightBracket, "]", 0.0, line_, column_}); break;
        case '{': bracketNesting_++; tokens_.push_back(Token{TokenType::LeftBrace, "{", 0.0, line_, column_}); break;
        case '}': if (bracketNesting_ > 0) bracketNesting_--; tokens_.push_back(Token{TokenType::RightBrace, "}", 0.0, line_, column_}); break;
        case ',': tokens_.push_back(Token{TokenType::Comma, ",", 0.0, line_, column_}); break;
        case ':': tokens_.push_back(Token{TokenType::Colon, ":", 0.0, line_, column_}); break;
        case '.': tokens_.push_back(Token{TokenType::Dot, ".", 0.0, line_, column_}); break;

        case '"':
        case '\'':
            scanString(c);
            break;

        default:
            if (std::isdigit(c)) {
                scanNumber();
            } else if (std::isalpha(c) || c == '_') {
                scanIdentifierOrKeyword();
            }
            break;
    }
}

void Lexer::scanIdentifierOrKeyword() {
    while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
        advance();
    }

    std::string text = source_.substr(start_, current_ - start_);
    TokenType type = TokenType::Identifier;

    auto it = kKeywords.find(text);
    if (it != kKeywords.end()) {
        type = it->second;
    }

    Token t;
    t.type = type;
    t.text = std::move(text);
    t.line = line_;
    t.column = column_;
    tokens_.push_back(t);
}

void Lexer::scanNumber() {
    // Check for hex (0x / 0X) or binary (0b / 0B) literal
    if (source_[start_] == '0' && !isAtEnd()) {
        char next = peek();
        if (next == 'x' || next == 'X') {
            advance(); // consume 'x' or 'X'
            while (!isAtEnd() && std::isxdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
            std::string text = source_.substr(start_, current_ - start_);
            double val = static_cast<double>(std::strtoull(text.c_str(), nullptr, 16));
            Token t;
            t.type = TokenType::Number;
            t.text = std::move(text);
            t.numberValue = val;
            t.line = line_;
            t.column = column_;
            tokens_.push_back(t);
            return;
        } else if (next == 'b' || next == 'B') {
            advance(); // consume 'b' or 'B'
            while (!isAtEnd() && (peek() == '0' || peek() == '1')) {
                advance();
            }
            std::string text = source_.substr(start_, current_ - start_);
            double val = 0.0;
            if (text.size() > 2) {
                val = static_cast<double>(std::strtoull(text.c_str() + 2, nullptr, 2));
            }
            Token t;
            t.type = TokenType::Number;
            t.text = std::move(text);
            t.numberValue = val;
            t.line = line_;
            t.column = column_;
            tokens_.push_back(t);
            return;
        }
    }

    while (!isAtEnd() && std::isdigit(peek())) {
        advance();
    }

    if (!isAtEnd() && peek() == '.' && std::isdigit(peekNext())) {
        advance(); // consume '.'
        while (!isAtEnd() && std::isdigit(peek())) {
            advance();
        }
    }

    // Scientific notation (e.g. 1e-3)
    if (!isAtEnd() && (peek() == 'e' || peek() == 'E')) {
        advance();
        if (!isAtEnd() && (peek() == '+' || peek() == '-')) {
            advance();
        }
        while (!isAtEnd() && std::isdigit(peek())) {
            advance();
        }
    }

    std::string text = source_.substr(start_, current_ - start_);
    double val = std::strtod(text.c_str(), nullptr);

    Token t;
    t.type = TokenType::Number;
    t.text = std::move(text);
    t.numberValue = val;
    t.line = line_;
    t.column = column_;
    tokens_.push_back(t);
}

void Lexer::scanString(char quoteChar) {
    // Check for triple quotes
    bool isTriple = false;
    if (peek() == quoteChar && peekNext() == quoteChar) {
        advance(); // second
        advance(); // third
        isTriple = true;
    }

    size_t contentStart = current_;
    if (isTriple) {
        while (!isAtEnd()) {
            if (peek() == quoteChar && peekNext() == quoteChar) {
                // Check third
                if (current_ + 2 < source_.size() && source_[current_ + 2] == quoteChar) {
                    break;
                }
            }
            if (peek() == '\n') {
                line_++;
                column_ = 1;
            }
            advance();
        }
        std::string text = source_.substr(contentStart, current_ - contentStart);
        if (!isAtEnd()) { advance(); advance(); advance(); }
        tokens_.push_back(Token{TokenType::String, std::move(text), 0.0, line_, column_});
    } else {
        while (!isAtEnd() && peek() != quoteChar && peek() != '\n') {
            advance();
        }
        std::string text = source_.substr(contentStart, current_ - contentStart);
        if (!isAtEnd() && peek() == quoteChar) {
            advance();
        }
        tokens_.push_back(Token{TokenType::String, std::move(text), 0.0, line_, column_});
    }
}

} // namespace eatsbits::eatscript
