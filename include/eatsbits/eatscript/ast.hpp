#ifndef EATS_AST_HPP
#define EATS_AST_HPP

#include <string>
#include <vector>
#include <memory>
#include <type_traits>
#include "token.hpp"

namespace eatsbits::eatscript {

struct ASTVisitor;

enum class ExprKind : uint8_t {
    Number,
    String,
    Boolean,
    Identifier,
    Member,
    Binary,
    Unary,
    Call,
    List,
    Dict,
    Index
};

enum class StmtKind : uint8_t {
    Expr,
    Assign,
    IndexAssign,
    Return,
    If,
    FunctionDef
};

namespace detail {
    inline volatile const void* g_wasm_sink = nullptr;
}
#define EATS_WASM_KEEP_THIS ::eatsbits::eatscript::detail::g_wasm_sink = this

// Base Expression
struct Expr {
    ExprKind kind;
    explicit Expr(ExprKind k) : kind(k) {}
    ~Expr() { EATS_WASM_KEEP_THIS; }
};

// Base Statement
struct Stmt {
    StmtKind kind;
    explicit Stmt(StmtKind k) : kind(k) {}
    ~Stmt() { EATS_WASM_KEEP_THIS; }
};

// Expressions
struct NumberLiteral : Expr {
    double value{0.0};
    explicit NumberLiteral(double v) : Expr(ExprKind::Number), value(v) {}
    ~NumberLiteral() { EATS_WASM_KEEP_THIS; }
};

struct StringLiteral : Expr {
    std::string value;
    explicit StringLiteral(std::string v) : Expr(ExprKind::String), value(std::move(v)) {}
    ~StringLiteral() { EATS_WASM_KEEP_THIS; }
};

struct BooleanLiteral : Expr {
    bool value{false};
    explicit BooleanLiteral(bool v) : Expr(ExprKind::Boolean), value(v) {}
    ~BooleanLiteral() { EATS_WASM_KEEP_THIS; }
};

struct IdentifierExpr : Expr {
    std::string name;
    explicit IdentifierExpr(std::string n) : Expr(ExprKind::Identifier), name(std::move(n)) {}
    ~IdentifierExpr() { EATS_WASM_KEEP_THIS; }
};

struct MemberExpr : Expr {
    std::shared_ptr<Expr> object;
    std::string member;
    MemberExpr(std::shared_ptr<Expr> obj, std::string mem)
        : Expr(ExprKind::Member), object(std::move(obj)), member(std::move(mem)) {}
    ~MemberExpr() { EATS_WASM_KEEP_THIS; }
};

struct BinaryExpr : Expr {
    std::shared_ptr<Expr> left;
    TokenType op;
    std::shared_ptr<Expr> right;
    BinaryExpr(std::shared_ptr<Expr> l, TokenType o, std::shared_ptr<Expr> r)
        : Expr(ExprKind::Binary), left(std::move(l)), op(o), right(std::move(r)) {}
    ~BinaryExpr() { EATS_WASM_KEEP_THIS; }
};

struct UnaryExpr : Expr {
    TokenType op;
    std::shared_ptr<Expr> operand;
    UnaryExpr(TokenType o, std::shared_ptr<Expr> opnd)
        : Expr(ExprKind::Unary), op(o), operand(std::move(opnd)) {}
    ~UnaryExpr() { EATS_WASM_KEEP_THIS; }
};

struct CallExpr : Expr {
    std::shared_ptr<Expr> callee;
    std::vector<std::shared_ptr<Expr>> args;
    std::vector<std::string> argNames;
    CallExpr(std::shared_ptr<Expr> c, std::vector<std::shared_ptr<Expr>> a, std::vector<std::string> names = {})
        : Expr(ExprKind::Call), callee(std::move(c)), args(std::move(a)), argNames(std::move(names)) {}
    ~CallExpr() { EATS_WASM_KEEP_THIS; }
};

struct ListLiteral : Expr {
    std::vector<std::shared_ptr<Expr>> elements;
    explicit ListLiteral(std::vector<std::shared_ptr<Expr>> el)
        : Expr(ExprKind::List), elements(std::move(el)) {}
    ~ListLiteral() { EATS_WASM_KEEP_THIS; }
};

struct DictEntry {
    std::string key;
    std::shared_ptr<Expr> value;
};

struct DictLiteral : Expr {
    std::vector<DictEntry> entries;
    explicit DictLiteral(std::vector<DictEntry> e)
        : Expr(ExprKind::Dict), entries(std::move(e)) {}
    ~DictLiteral() { EATS_WASM_KEEP_THIS; }
};

struct IndexExpr : Expr {
    std::shared_ptr<Expr> object;
    std::shared_ptr<Expr> index;
    IndexExpr(std::shared_ptr<Expr> obj, std::shared_ptr<Expr> idx)
        : Expr(ExprKind::Index), object(std::move(obj)), index(std::move(idx)) {}
    ~IndexExpr() { EATS_WASM_KEEP_THIS; }
};

// Statements
struct ExprStmt : Stmt {
    std::shared_ptr<Expr> expression;
    explicit ExprStmt(std::shared_ptr<Expr> e)
        : Stmt(StmtKind::Expr), expression(std::move(e)) {}
    ~ExprStmt() { EATS_WASM_KEEP_THIS; }
};

struct AssignStmt : Stmt {
    std::string variableName;
    std::shared_ptr<Expr> value;
    AssignStmt(std::string name, std::shared_ptr<Expr> val)
        : Stmt(StmtKind::Assign), variableName(std::move(name)), value(std::move(val)) {}
    ~AssignStmt() { EATS_WASM_KEEP_THIS; }
};

struct IndexAssignStmt : Stmt {
    std::string variableName;
    std::shared_ptr<Expr> index;
    std::shared_ptr<Expr> value;
    IndexAssignStmt(std::string name, std::shared_ptr<Expr> idx, std::shared_ptr<Expr> val)
        : Stmt(StmtKind::IndexAssign), variableName(std::move(name)), index(std::move(idx)), value(std::move(val)) {}
    ~IndexAssignStmt() { EATS_WASM_KEEP_THIS; }
};

struct ReturnStmt : Stmt {
    std::shared_ptr<Expr> value;
    explicit ReturnStmt(std::shared_ptr<Expr> val)
        : Stmt(StmtKind::Return), value(std::move(val)) {}
    ~ReturnStmt() { EATS_WASM_KEEP_THIS; }
};

struct IfStmt : Stmt {
    std::shared_ptr<Expr> condition;
    std::vector<std::shared_ptr<Stmt>> thenBranch;
    std::vector<std::shared_ptr<Stmt>> elseBranch;
    IfStmt(std::shared_ptr<Expr> cond, std::vector<std::shared_ptr<Stmt>> t, std::vector<std::shared_ptr<Stmt>> e = {})
        : Stmt(StmtKind::If), condition(std::move(cond)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}
    ~IfStmt() { EATS_WASM_KEEP_THIS; }
};

struct FunctionDef : Stmt {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::shared_ptr<Stmt>> body;
    FunctionDef(std::string n, std::vector<std::string> p, std::vector<std::shared_ptr<Stmt>> b)
        : Stmt(StmtKind::FunctionDef), name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
    ~FunctionDef() { EATS_WASM_KEEP_THIS; }
};

struct Program {
    std::vector<std::shared_ptr<Stmt>> statements;
    ~Program() { EATS_WASM_KEEP_THIS; }
};

// Type-safe fast cast helpers (avoiding RTTI/dynamic_cast issues in WASM)
template <typename T> struct ASTTraits;
template <> struct ASTTraits<NumberLiteral> { static constexpr ExprKind exprKind = ExprKind::Number; };
template <> struct ASTTraits<StringLiteral> { static constexpr ExprKind exprKind = ExprKind::String; };
template <> struct ASTTraits<BooleanLiteral> { static constexpr ExprKind exprKind = ExprKind::Boolean; };
template <> struct ASTTraits<IdentifierExpr> { static constexpr ExprKind exprKind = ExprKind::Identifier; };
template <> struct ASTTraits<MemberExpr> { static constexpr ExprKind exprKind = ExprKind::Member; };
template <> struct ASTTraits<BinaryExpr> { static constexpr ExprKind exprKind = ExprKind::Binary; };
template <> struct ASTTraits<UnaryExpr> { static constexpr ExprKind exprKind = ExprKind::Unary; };
template <> struct ASTTraits<CallExpr> { static constexpr ExprKind exprKind = ExprKind::Call; };
template <> struct ASTTraits<ListLiteral> { static constexpr ExprKind exprKind = ExprKind::List; };
template <> struct ASTTraits<DictLiteral> { static constexpr ExprKind exprKind = ExprKind::Dict; };
template <> struct ASTTraits<IndexExpr> { static constexpr ExprKind exprKind = ExprKind::Index; };

template <> struct ASTTraits<ExprStmt> { static constexpr StmtKind stmtKind = StmtKind::Expr; };
template <> struct ASTTraits<AssignStmt> { static constexpr StmtKind stmtKind = StmtKind::Assign; };
template <> struct ASTTraits<IndexAssignStmt> { static constexpr StmtKind stmtKind = StmtKind::IndexAssign; };
template <> struct ASTTraits<ReturnStmt> { static constexpr StmtKind stmtKind = StmtKind::Return; };
template <> struct ASTTraits<IfStmt> { static constexpr StmtKind stmtKind = StmtKind::If; };
template <> struct ASTTraits<FunctionDef> { static constexpr StmtKind stmtKind = StmtKind::FunctionDef; };

template <typename T, typename Base>
inline T* ast_cast(Base* b) {
    if (!b) return nullptr;
    if constexpr (std::is_base_of_v<Expr, T>) {
        return (b->kind == ASTTraits<T>::exprKind) ? static_cast<T*>(b) : nullptr;
    } else if constexpr (std::is_base_of_v<Stmt, T>) {
        return (b->kind == ASTTraits<T>::stmtKind) ? static_cast<T*>(b) : nullptr;
    } else {
        return nullptr;
    }
}

template <typename T, typename Base>
inline const T* ast_cast(const Base* b) {
    if (!b) return nullptr;
    if constexpr (std::is_base_of_v<Expr, T>) {
        return (b->kind == ASTTraits<T>::exprKind) ? static_cast<const T*>(b) : nullptr;
    } else if constexpr (std::is_base_of_v<Stmt, T>) {
        return (b->kind == ASTTraits<T>::stmtKind) ? static_cast<const T*>(b) : nullptr;
    } else {
        return nullptr;
    }
}

} // namespace eatsbits::eatscript

#endif // EATS_AST_HPP

