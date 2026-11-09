#ifndef EATS_PARSER_HPP
#define EATS_PARSER_HPP

#include <vector>
#include <memory>
#include <string>
#include "token.hpp"
#include "ast.hpp"

namespace eatsbits::eatscript {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    std::unique_ptr<Program> parse();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool isAtEnd() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);
    void skipNewlines();

    std::shared_ptr<Stmt> parseDeclaration();
    std::shared_ptr<FunctionDef> parseFunctionDef();
    std::vector<std::shared_ptr<Stmt>> parseBlock();
    std::shared_ptr<Stmt> parseStatement();
    std::shared_ptr<Stmt> parseIfStatement();
    std::shared_ptr<Stmt> parseReturnStatement();
    std::shared_ptr<Stmt> parseExpressionOrAssignment();

    std::shared_ptr<Expr> parseExpression();
    std::shared_ptr<Expr> parseLogicalOr();
    std::shared_ptr<Expr> parseLogicalAnd();
    std::shared_ptr<Expr> parseEquality();
    std::shared_ptr<Expr> parseComparison();
    std::shared_ptr<Expr> parseTerm();
    std::shared_ptr<Expr> parseFactor();
    std::shared_ptr<Expr> parsePower();
    std::shared_ptr<Expr> parseUnary();
    std::shared_ptr<Expr> parseCall();
    std::shared_ptr<Expr> parsePrimary();

    std::vector<Token> tokens_;
    size_t current_{0};
};

} // namespace eatsbits::eatscript

#endif // EATS_PARSER_HPP
