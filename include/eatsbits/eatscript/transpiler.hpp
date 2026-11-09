#ifndef EATS_TRANSPILER_HPP
#define EATS_TRANSPILER_HPP

#include <string>
#include <memory>
#include "ast.hpp"
#include "dispatch_scanner.hpp"

namespace eatsbits::eatscript {

/**
 * Ahead-Of-Time (AOT) C++ Transpiler for Eatscript scripts.
 * Emits pure C++ structs and C-ABI export functions matching EatsPluginDescriptor.
 */
class Transpiler {
public:
    Transpiler() = default;

    std::string transpile(const Program& program, const std::string& pluginId = "my_synth",
                          const std::string& pluginName = "My Synth");

    std::string transpileSource(const std::string& source, const std::string& pluginId = "my_synth",
                                const std::string& pluginName = "My Synth");

    /// Emits a self-contained C++ translation unit wrapping ClapPluginShell<T>
    /// with the AST DSP math or native dispatch adapter.
    std::string emitClapPluginEntry(const Program& program, const DependencyScanResult& scanResult);

    /// High-level convenience method: scans script with DependencyScanner and emits CLAP plugin bundle C++ code.
    std::string transpileClap(const std::string& source);

    void transpileStmt(const Stmt* stmt);
    void transpileExpr(const Expr* expr);

    void visit(NumberLiteral* node);
    void visit(StringLiteral* node);
    void visit(BooleanLiteral* node);
    void visit(IdentifierExpr* node);
    void visit(MemberExpr* node);
    void visit(BinaryExpr* node);
    void visit(UnaryExpr* node);
    void visit(CallExpr* node);
    void visit(ListLiteral* node);
    void visit(DictLiteral* node);
    void visit(IndexExpr* node);
    void visit(ExprStmt* node);
    void visit(AssignStmt* node);
    void visit(IndexAssignStmt* node);
    void visit(ReturnStmt* node);
    void visit(IfStmt* node);
    void visit(FunctionDef* node);

private:
    std::string emitIndent() const;

    std::string output_;
    std::string processBodyCode_;
    int indentLevel_{0};
    bool inProcessFunction_{false};
    bool inClapMode_{false};
    const DependencyScanResult* currentScanResult_{nullptr};
};

} // namespace eatsbits::eatscript

#endif // EATS_TRANSPILER_HPP
