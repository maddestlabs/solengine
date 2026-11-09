#ifndef EATS_VM_HPP
#define EATS_VM_HPP

#include <array>
#include <string>
#include <memory>
#include <unordered_map>
#include "bytecode.hpp"
#include "ast.hpp"

namespace eatsbits::eatscript {

struct ShaderPixelContext {
    double u{0.0}, v{0.0};
    double nx{0.0}, ny{0.0};
    double px{0.0}, py{0.0};
    double inR{0.0}, inG{0.0}, inB{0.0}, inA{1.0};
    double height{0.0}, mask{0.0};
    double rank{1.0}, suit{0.0}, isRed{0.0};
    double u0{0.0}, u1{0.0}, u2{0.0}, u3{0.0};
    const uint8_t* srcRgba{nullptr};
    int width{0}, heightPx{0};
};

struct ShaderSlotBindings {
    int u{-1}, v{-1};
    int nx{-1}, ny{-1};
    int px{-1}, py{-1};
    int inR{-1}, inG{-1}, inB{-1}, inA{-1};
    int r{-1}, g{-1}, b{-1}, a{-1};
    int height{-1}, elev{-1}, mask{-1};
    int rank{-1}, suit{-1}, isRed{-1};
    int u0{-1}, u1{-1}, u2{-1}, u3{-1};
};

/**
 * High-Performance, Allocation-Free Bytecode Virtual Machine for Eatscript.
 * Executes live scripts during real-time audio synthesis callbacks and parallel 2D pixel shaders.
 */
class VM {
public:
    VM() noexcept;

    // Compilation (offline / hot-reloading thread)
    bool compileSource(const std::string& source);
    bool compileProgram(const Program& program);
    bool compileShaderFunction(FunctionDef* func);
    bool compileShaderSource(const std::string& source, const std::string& funcName = "");

    // Thread-Safe Parallel Pixel Shader Execution (zero heap allocations, bounded op count)
    void executePixelShader(const ShaderPixelContext& ctx,
                            double& outR, double& outG, double& outB, double& outA) const noexcept;

    // Real-Time Safe Execution (audio thread callback)
    [[nodiscard]] double executeProcess(double time, double freq, double note,
                                       const float* params = nullptr, size_t numParams = 0) noexcept;

    /// Real-time safe stereo Audio FX process execution: def process(in_l, in_r, params) -> [out_l, out_r]
    [[nodiscard]] std::pair<double, double> executeStereoProcess(double inL, double inR,
                                                                 const float* params = nullptr,
                                                                 size_t numParams = 0) noexcept;

    [[nodiscard]] bool isStereoEffect() const noexcept {
        return isStereoEffect_;
    }

    [[nodiscard]] bool hasCompiledProcess() const noexcept {
        return hasProcess_;
    }

    [[nodiscard]] const Chunk& getProcessChunk() const noexcept {
        return processChunk_;
    }

    // Set parameter value
    void setParam(uint32_t id, float value) noexcept;
    [[nodiscard]] float getParam(uint32_t id) const noexcept;

private:
    bool compileFunction(FunctionDef* func);
    void compileStmt(Stmt* stmt);
    void compileExpr(Expr* expr);

    int resolveLocal(const std::string& name);
    int addLocal(const std::string& name);

    Chunk processChunk_;
    bool hasProcess_{false};
    bool isStereoEffect_{false};
    int slotInL_{-1};
    int slotInR_{-1};
    ShaderSlotBindings shaderSlots_{};

    // Pre-allocated runtime evaluation environment (Zero runtime allocations)
    static constexpr size_t STACK_MAX = 256;
    static constexpr size_t LOCALS_MAX = 64;
    static constexpr size_t PARAMS_MAX = 64;

    alignas(64) std::array<double, STACK_MAX> stack_{};
    alignas(64) std::array<double, LOCALS_MAX> locals_{};
    alignas(64) std::array<float, PARAMS_MAX> params_{};

    std::vector<std::string> localSymbolTable_;
    uint32_t rngState_{123456789};
};

} // namespace eatsbits::eatscript

#endif // EATS_VM_HPP
