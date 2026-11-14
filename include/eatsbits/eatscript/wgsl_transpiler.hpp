#ifndef EATS_WGSL_TRANSPILER_HPP
#define EATS_WGSL_TRANSPILER_HPP

#include <string>
#include <unordered_set>
#include "ast.hpp"

namespace eatsbits::eatscript {

/**
 * Transpiles an Eatscript pixel/post-processing shader FunctionDef AST
 * into a self-contained WGSL fullscreen vertex + fragment shader.
 */
class WgslShaderTranspiler {
public:
    WgslShaderTranspiler() = default;

    /// Transpiles a FunctionDef AST into a full WGSL shader module (vs_main + fs_main).
    /// Returns empty string on failure and populates errorOut if provided.
    std::string transpilePostFx(const FunctionDef* func, std::string* errorOut = nullptr);

    /// Convenience: parses Eatscript source, finds `funcName` (or the first function), and transpiles.
    std::string transpilePostFxSource(const std::string& source,
                                      const std::string& funcName = "",
                                      std::string* errorOut = nullptr);

    /// Returns the shared WGSL preamble (Uniforms, Texture/Sampler bindings, vs_main, math/noise/sample helpers).
    static std::string buildPostFxPreamble();

private:
    void emitStmt(const Stmt* stmt);
    std::string emitExpr(const Expr* expr);
    std::string indent() const;

    std::string body_;
    int indentLevel_{1};
    std::unordered_set<std::string> declaredLocals_;
    std::string error_;
};

} // namespace eatsbits::eatscript

#endif // EATS_WGSL_TRANSPILER_HPP
