#include "eatsbits/eatscript/parser.hpp"
#include <stdexcept>
#include <iostream>

namespace eatsbits::eatscript {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile;
}

const Token& Parser::advance() {
    if (!isAtEnd()) current_++;
    return previous();
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (auto t : types) {
        if (check(t)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw std::runtime_error("Parser Error at line " + std::to_string(peek().line) + ": " + message +
                             " (got '" + peek().text + "')");
}

void Parser::skipNewlines() {
    while (match(TokenType::Newline)) {}
}

std::unique_ptr<Program> Parser::parse() {
    auto prog = std::make_unique<Program>();
    skipNewlines();

    while (!isAtEnd()) {
        try {
            auto stmt = parseDeclaration();
            if (stmt) {
                prog->statements.push_back(std::move(stmt));
            }
        } catch (const std::exception& ex) {
            std::cerr << "[Eatscript Parser Error] " << ex.what() << std::endl;
            // Panic mode recovery: synchronize to next newline or def
            advance();
            while (!isAtEnd() && peek().type != TokenType::Newline && peek().type != TokenType::Def) {
                advance();
            }
        }
        skipNewlines();
    }

    return prog;
}

std::shared_ptr<Stmt> Parser::parseDeclaration() {
    if (match(TokenType::Def)) {
        return parseFunctionDef();
    }
    if (match(TokenType::Import)) {
        // Skip import statement: import math
        while (!isAtEnd() && peek().type != TokenType::Newline) {
            advance();
        }
        match(TokenType::Newline);
        return nullptr;
    }
    return parseStatement();
}

std::shared_ptr<FunctionDef> Parser::parseFunctionDef() {
    const Token& nameToken = consume(TokenType::Identifier, "Expected function name after 'def'");
    consume(TokenType::LeftParen, "Expected '(' after function name");

    std::vector<std::string> params;
    skipNewlines();
    if (!check(TokenType::RightParen)) {
        do {
            skipNewlines();
            if (check(TokenType::RightParen)) break;
            const Token& p = consume(TokenType::Identifier, "Expected parameter name");
            params.push_back(p.text);
            // Ignore default parameter values: targetNote=0, isSlide=False
            if (match(TokenType::Assign)) {
                parseExpression();
            }
            skipNewlines();
        } while (match(TokenType::Comma));
    }
    skipNewlines();
    consume(TokenType::RightParen, "Expected ')' after parameter list");
    consume(TokenType::Colon, "Expected ':' after function signature");

    skipNewlines();
    auto body = parseBlock();
    return std::make_shared<FunctionDef>(nameToken.text, std::move(params), std::move(body));
}

std::vector<std::shared_ptr<Stmt>> Parser::parseBlock() {
    std::vector<std::shared_ptr<Stmt>> statements;
    consume(TokenType::Indent, "Expected indentation block");

    skipNewlines();
    while (!check(TokenType::Dedent) && !isAtEnd()) {
        auto s = parseStatement();
        if (s) {
            statements.push_back(std::move(s));
        }
        skipNewlines();
    }

    consume(TokenType::Dedent, "Expected dedent at end of block");
    return statements;
}

std::shared_ptr<Stmt> Parser::parseStatement() {
    if (match(TokenType::If)) {
        return parseIfStatement();
    }
    if (match(TokenType::Return)) {
        return parseReturnStatement();
    }
    return parseExpressionOrAssignment();
}

std::shared_ptr<Stmt> Parser::parseIfStatement() {
    auto condition = parseExpression();
    consume(TokenType::Colon, "Expected ':' after if condition");
    skipNewlines();
    auto thenBranch = parseBlock();

    std::vector<std::shared_ptr<Stmt>> elseBranch;
    skipNewlines();
    if (match(TokenType::Elif)) {
        elseBranch.push_back(parseIfStatement());
    } else if (match(TokenType::Else)) {
        consume(TokenType::Colon, "Expected ':' after else");
        skipNewlines();
        elseBranch = parseBlock();
    }

    return std::make_shared<IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

std::shared_ptr<Stmt> Parser::parseReturnStatement() {
    std::shared_ptr<Expr> value = nullptr;
    if (!check(TokenType::Newline) && !check(TokenType::Dedent) && !isAtEnd()) {
        value = parseExpression();
    }
    match(TokenType::Newline);
    return std::make_shared<ReturnStmt>(std::move(value));
}

std::shared_ptr<Stmt> Parser::parseExpressionOrAssignment() {
    if (check(TokenType::Identifier)) {
        // Lookahead to see if next token is '='
        if (current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenType::Assign) {
            std::string varName = advance().text;
            consume(TokenType::Assign, "Expected '=' in assignment");
            auto val = parseExpression();
            match(TokenType::Newline);
            return std::make_shared<AssignStmt>(std::move(varName), std::move(val));
        }
        // Lookahead for var[index] = expr
        if (current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenType::LeftBracket) {
            size_t probe = current_ + 2;
            int bracketDepth = 1;
            while (probe < tokens_.size() && bracketDepth > 0) {
                if (tokens_[probe].type == TokenType::LeftBracket) bracketDepth++;
                else if (tokens_[probe].type == TokenType::RightBracket) bracketDepth--;
                probe++;
            }
            if (bracketDepth == 0 && probe < tokens_.size() && tokens_[probe].type == TokenType::Assign) {
                std::string varName = advance().text;
                consume(TokenType::LeftBracket, "Expected '['");
                auto idxExpr = parseExpression();
                consume(TokenType::RightBracket, "Expected ']'");
                consume(TokenType::Assign, "Expected '=' in index assignment");
                auto val = parseExpression();
                match(TokenType::Newline);
                return std::make_shared<IndexAssignStmt>(std::move(varName), std::move(idxExpr), std::move(val));
            }
        }
    }

    auto expr = parseExpression();
    match(TokenType::Newline);
    return std::make_shared<ExprStmt>(std::move(expr));
}

std::shared_ptr<Expr> Parser::parseExpression() {
    return parseLogicalOr();
}

std::shared_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    while (match(TokenType::Or)) {
        TokenType op = previous().type;
        auto right = parseLogicalAnd();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseLogicalAnd() {
    auto expr = parseEquality();
    while (match(TokenType::And)) {
        TokenType op = previous().type;
        auto right = parseEquality();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseEquality() {
    auto expr = parseComparison();
    while (match({TokenType::Equals, TokenType::NotEquals})) {
        TokenType op = previous().type;
        auto right = parseComparison();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseComparison() {
    auto expr = parseTerm();
    while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual})) {
        TokenType op = previous().type;
        auto right = parseTerm();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseTerm() {
    auto expr = parseFactor();
    while (match({TokenType::Plus, TokenType::Minus})) {
        TokenType op = previous().type;
        auto right = parseFactor();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseFactor() {
    auto expr = parsePower();
    while (match({TokenType::Multiply, TokenType::Divide, TokenType::Modulo})) {
        TokenType op = previous().type;
        auto right = parsePower();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parsePower() {
    auto expr = parseUnary();
    if (match(TokenType::Power)) {
        TokenType op = previous().type;
        auto right = parseUnary();
        expr = std::make_shared<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::shared_ptr<Expr> Parser::parseUnary() {
    if (match({TokenType::Minus, TokenType::Not})) {
        TokenType op = previous().type;
        auto operand = parseUnary();
        return std::make_shared<UnaryExpr>(op, std::move(operand));
    }
    return parseCall();
}

std::shared_ptr<Expr> Parser::parseCall() {
    auto expr = parsePrimary();

    while (true) {
        if (match(TokenType::LeftParen)) {
            // Function call
            std::vector<std::shared_ptr<Expr>> args;
            std::vector<std::string> argNames;
            if (!check(TokenType::RightParen)) {
                do {
                    skipNewlines();
                    if (check(TokenType::RightParen)) break;
                    std::string argName;
                    // Check for named argument: step=0.25
                    if (check(TokenType::Identifier) && current_ + 1 < tokens_.size() &&
                        tokens_[current_ + 1].type == TokenType::Assign) {
                        argName = advance().text; // name
                        advance(); // =
                    }
                    argNames.push_back(std::move(argName));
                    args.push_back(parseExpression());
                    skipNewlines();
                } while (match(TokenType::Comma));
            }
            skipNewlines();
            consume(TokenType::RightParen, "Expected ')' after argument list");
            expr = std::make_shared<CallExpr>(std::move(expr), std::move(args), std::move(argNames));
        } else if (match(TokenType::Dot)) {
            const Token& name = consume(TokenType::Identifier, "Expected property or method name after '.'");
            expr = std::make_shared<MemberExpr>(std::move(expr), name.text);
        } else if (match(TokenType::LeftBracket)) {
            auto idx = parseExpression();
            consume(TokenType::RightBracket, "Expected ']' after index");
            expr = std::make_shared<IndexExpr>(std::move(expr), std::move(idx));
        } else {
            break;
        }
    }

    return expr;
}

std::shared_ptr<Expr> Parser::parsePrimary() {
    if (match(TokenType::Number)) {
        return std::make_shared<NumberLiteral>(previous().numberValue);
    }
    if (match(TokenType::String)) {
        return std::make_shared<StringLiteral>(previous().text);
    }
    if (match(TokenType::TrueLiteral)) {
        return std::make_shared<BooleanLiteral>(true);
    }
    if (match(TokenType::FalseLiteral)) {
        return std::make_shared<BooleanLiteral>(false);
    }
    if (match(TokenType::Identifier)) {
        return std::make_shared<IdentifierExpr>(previous().text);
    }

    // List: [a, b, c]
    if (match(TokenType::LeftBracket)) {
        std::vector<std::shared_ptr<Expr>> elements;
        skipNewlines();
        if (!check(TokenType::RightBracket)) {
            while (true) {
                skipNewlines();
                if (check(TokenType::RightBracket)) break;
                elements.push_back(parseExpression());
                skipNewlines();
                if (!match(TokenType::Comma)) break;
            }
        }
        skipNewlines();
        consume(TokenType::RightBracket, "Expected ']' after list");
        return std::make_shared<ListLiteral>(std::move(elements));
    }

    // Dict: {"key": value}
    if (match(TokenType::LeftBrace)) {
        std::vector<DictEntry> entries;
        skipNewlines();
        if (!check(TokenType::RightBrace)) {
            while (true) {
                skipNewlines();
                if (check(TokenType::RightBrace)) break;
                std::string keyName;
                if (check(TokenType::String) || check(TokenType::Identifier)) {
                    keyName = advance().text;
                } else {
                    throw std::runtime_error("Expected string or identifier key in dictionary at line " + std::to_string(peek().line));
                }
                consume(TokenType::Colon, "Expected ':' after dictionary key");
                skipNewlines();
                auto val = parseExpression();
                entries.push_back(DictEntry{std::move(keyName), std::move(val)});
                skipNewlines();
                if (!match(TokenType::Comma)) break;
            }
        }
        skipNewlines();
        consume(TokenType::RightBrace, "Expected '}' after dictionary");
        return std::make_shared<DictLiteral>(std::move(entries));
    }

    // Parenthesized expression: (expr)
    if (match(TokenType::LeftParen)) {
        auto expr = parseExpression();
        consume(TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }

    throw std::runtime_error("Unexpected token in expression at line " + std::to_string(peek().line) +
                             ": '" + peek().text + "'");
}

} // namespace eatsbits::eatscript
