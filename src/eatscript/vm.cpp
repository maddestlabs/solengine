#include "eatsbits/eatscript/vm.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <iostream>

namespace eatsbits::eatscript {

namespace {

inline double hash2D(int ix, int iy) noexcept {
    uint32_t h = static_cast<uint32_t>(ix) * 374761393u + static_cast<uint32_t>(iy) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return static_cast<double>(h & 0xFFFFFFu) / static_cast<double>(0xFFFFFFu);
}

inline double smoothValueNoise2D(double x, double y) noexcept {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    double fx = x - static_cast<double>(ix);
    double fy = y - static_cast<double>(iy);
    double ux = fx * fx * (3.0 - 2.0 * fx);
    double uy = fy * fy * (3.0 - 2.0 * fy);

    double n00 = hash2D(ix, iy);
    double n10 = hash2D(ix + 1, iy);
    double n01 = hash2D(ix, iy + 1);
    double n11 = hash2D(ix + 1, iy + 1);

    double nx0 = n00 + (n10 - n00) * ux;
    double nx1 = n01 + (n11 - n01) * ux;
    return nx0 + (nx1 - nx0) * uy;
}

inline double fbmNoise2D(double x, double y) noexcept {
    double sum = 0.0;
    double amp = 0.5;
    double freq = 1.0;
    for (int i = 0; i < 4; ++i) {
        sum += amp * smoothValueNoise2D(x * freq, y * freq);
        freq *= 2.02;
        amp *= 0.5;
    }
    return sum;
}

// Exact/analytical 2D signed distance field (< 0 inside suit, > 0 outside)
// Coords (x, y) in centered pip space (~ -1..1), y pointing down.
inline double evalSuitSdf(double x, double y, int suitInt) noexcept {
    double ax = std::abs(x);
    if (suitInt == 2) {
        // Diamonds (0=spades, 1=hearts, 2=diamonds, 3=clubs): slightly concave Star-of-Bethlehem diamond
        return (ax * 1.15 + std::abs(y) * 0.92 - 0.55) * 0.75;
    } else if (suitInt == 1) {
        // Hearts: classic upper dual lobes + pointed lower tip
        double hy = -y + 0.12; // flip so +hy is up
        if (hy + ax > 0.52) {
            return std::hypot(ax - 0.26, hy - 0.26) - 0.30;
        }
        return ((ax - 0.52 * (hy + 0.48)) * 0.85);
    } else if (suitInt == 3) {
        // Clubs: three trefoil lobes + flared stem
        double dTop = std::hypot(x, y + 0.25) - 0.24;
        double dSides = std::hypot(ax - 0.24, y - 0.04) - 0.23;
        double dStem = std::max(ax - 0.07 - std::max(0.0, y - 0.05) * 0.22, std::abs(y - 0.26) - 0.22);
        return std::min({dTop, dSides, dStem});
    } else {
        // Spades (default 0): inverted heart body + flared stem
        double sy = y + 0.10; // inverted
        double body = 0.0;
        if (sy + ax > 0.48) {
            body = std::hypot(ax - 0.24, sy - 0.24) - 0.27;
        } else {
            body = (ax - 0.52 * (sy + 0.46)) * 0.85;
        }
        double dStem = std::max(ax - 0.07 - std::max(0.0, y - 0.08) * 0.24, std::abs(y - 0.28) - 0.20);
        return std::min(body, dStem);
    }
}

inline void sampleCanvasRgb(const ShaderPixelContext& ctx, double u, double v,
                            double& r, double& g, double& b) noexcept {
    if (!ctx.srcRgba || ctx.width <= 0 || ctx.heightPx <= 0) {
        r = ctx.inR; g = ctx.inG; b = ctx.inB;
        return;
    }
    int sx = std::clamp(static_cast<int>(std::round(u * static_cast<double>(ctx.width - 1))), 0, ctx.width - 1);
    int sy = std::clamp(static_cast<int>(std::round(v * static_cast<double>(ctx.heightPx - 1))), 0, ctx.heightPx - 1);
    size_t idx = static_cast<size_t>(sy * ctx.width + sx) * 4;
    r = static_cast<double>(ctx.srcRgba[idx + 0]) / 255.0;
    g = static_cast<double>(ctx.srcRgba[idx + 1]) / 255.0;
    b = static_cast<double>(ctx.srcRgba[idx + 2]) / 255.0;
}

} // namespace

VM::VM() noexcept {
    params_.fill(0.0f);
}

void VM::setParam(uint32_t id, float value) noexcept {
    if (id < PARAMS_MAX) {
        params_[id] = value;
    }
}

float VM::getParam(uint32_t id) const noexcept {
    if (id < PARAMS_MAX) {
        return params_[id];
    }
    return 0.0f;
}

bool VM::compileSource(const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    if (!prog) return false;
    return compileProgram(*prog);
}

bool VM::compileProgram(const Program& program) {
    hasProcess_ = false;
    processChunk_ = Chunk{};
    localSymbolTable_.clear();

    for (const auto& stmt : program.statements) {
        auto* func = ast_cast<FunctionDef>(stmt.get());
        if (func && func->name == "process") {
            if (compileFunction(func)) {
                hasProcess_ = true;
                return true;
            }
        }
    }
    return false;
}

int VM::resolveLocal(const std::string& name) {
    for (size_t i = 0; i < localSymbolTable_.size(); ++i) {
        if (localSymbolTable_[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int VM::addLocal(const std::string& name) {
    int existing = resolveLocal(name);
    if (existing != -1) return existing;
    localSymbolTable_.push_back(name);
    return static_cast<int>(localSymbolTable_.size() - 1);
}

bool VM::compileFunction(FunctionDef* func) {
    isStereoEffect_ = false;
    slotInL_ = -1;
    slotInR_ = -1;

    // Check if parameters indicate an Audio FX: e.g. input_l, input_r or in_l, in_r
    if (func->params.size() >= 2) {
        std::string p0 = func->params[0];
        std::string p1 = func->params[1];
        if (p0 == "input_l" || p0 == "in_l" || p0 == "l" || p0 == "left" ||
            p1 == "input_r" || p1 == "in_r" || p1 == "r" || p1 == "right") {
            isStereoEffect_ = true;
        }
    }

    for (const auto& p : func->params) {
        int s = addLocal(p);
        if (p == "input_l" || p == "in_l" || p == "l" || p == "left") slotInL_ = s;
        if (p == "input_r" || p == "in_r" || p == "r" || p == "right") slotInR_ = s;
    }

    // Default slots if not named identically
    addLocal("time");
    addLocal("freq");
    addLocal("note");
    int defL = addLocal("input_l");
    int defR = addLocal("input_r");
    addLocal("in_l");
    addLocal("in_r");
    if (slotInL_ == -1) slotInL_ = defL;
    if (slotInR_ == -1) slotInR_ = defR;

    for (const auto& stmt : func->body) {
        compileStmt(stmt.get());
    }

    // Ensure a return exists
    processChunk_.emitOp(OpCode::Return);
    return true;
}

bool VM::compileShaderFunction(FunctionDef* func) {
    if (!func) return false;
    hasProcess_ = false;
    processChunk_ = Chunk{};
    localSymbolTable_.clear();
    shaderSlots_ = ShaderSlotBindings{};

    // First register user's declared parameter names in order
    for (const auto& p : func->params) {
        addLocal(p);
    }

    // Ensure all standard pixel shader uniforms & varyings have deterministic local slots
    shaderSlots_.u      = addLocal("u");
    shaderSlots_.v      = addLocal("v");
    shaderSlots_.nx     = addLocal("nx");
    shaderSlots_.ny     = addLocal("ny");
    shaderSlots_.px     = addLocal("px");
    shaderSlots_.py     = addLocal("py");
    shaderSlots_.inR    = addLocal("in_r");
    shaderSlots_.inG    = addLocal("in_g");
    shaderSlots_.inB    = addLocal("in_b");
    shaderSlots_.inA    = addLocal("in_a");
    shaderSlots_.r      = addLocal("r");
    shaderSlots_.g      = addLocal("g");
    shaderSlots_.b      = addLocal("b");
    shaderSlots_.a      = addLocal("a");
    shaderSlots_.height = addLocal("height");
    shaderSlots_.elev   = addLocal("elev");
    shaderSlots_.mask   = addLocal("mask");
    shaderSlots_.rank   = addLocal("rank");
    shaderSlots_.suit   = addLocal("suit");
    shaderSlots_.isRed  = addLocal("is_red");
    shaderSlots_.u0     = addLocal("u0");
    shaderSlots_.u1     = addLocal("u1");
    shaderSlots_.u2     = addLocal("u2");
    shaderSlots_.u3     = addLocal("u3");

    for (const auto& stmt : func->body) {
        compileStmt(stmt.get());
    }

    processChunk_.emitOp(OpCode::Return);
    processChunk_.localNames = localSymbolTable_;
    hasProcess_ = true;
    return true;
}

bool VM::compileShaderSource(const std::string& source, const std::string& funcName) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();
    if (!prog) return false;

    FunctionDef* target = nullptr;
    for (const auto& stmt : prog->statements) {
        if (auto* func = ast_cast<FunctionDef>(stmt.get())) {
            if (funcName.empty() || func->name == funcName) {
                target = func;
                break;
            }
        }
    }
    return target ? compileShaderFunction(target) : false;
}

void VM::compileStmt(Stmt* stmt) {
    if (!stmt) return;

    if (auto* r = ast_cast<ReturnStmt>(stmt)) {
        if (auto* list = ast_cast<ListLiteral>(r->value.get())) {
            if (!list->elements.empty()) {
                for (const auto& el : list->elements) {
                    compileExpr(el.get());
                }
                processChunk_.emitOp(OpCode::Return);
                return;
            }
        }
        if (r->value) {
            compileExpr(r->value.get());
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
        processChunk_.emitOp(OpCode::Return);
    } else if (auto* a = ast_cast<AssignStmt>(stmt)) {
        compileExpr(a->value.get());
        int slot = addLocal(a->variableName);
        processChunk_.emitOp(OpCode::SetLocal);
        processChunk_.emitByte(static_cast<uint8_t>(slot));
    } else if (auto* ia = ast_cast<IndexAssignStmt>(stmt)) {
        compileExpr(ia->value.get());
        int slot = addLocal(ia->variableName);
        processChunk_.emitOp(OpCode::SetLocal);
        processChunk_.emitByte(static_cast<uint8_t>(slot));
    } else if (auto* e = ast_cast<ExprStmt>(stmt)) {
        compileExpr(e->expression.get());
    } else if (auto* ifs = ast_cast<IfStmt>(stmt)) {
        compileExpr(ifs->condition.get());
        processChunk_.emitOp(OpCode::JumpIfFalse);
        size_t jumpPos = processChunk_.code.size();
        processChunk_.emitByte(0); // placeholder
        processChunk_.emitByte(0);

        for (const auto& s : ifs->thenBranch) {
            compileStmt(s.get());
        }

        processChunk_.emitOp(OpCode::Jump);
        size_t exitJumpPos = processChunk_.code.size();
        processChunk_.emitByte(0);
        processChunk_.emitByte(0);

        // Patch condition jump
        size_t elseStart = processChunk_.code.size();
        processChunk_.code[jumpPos] = static_cast<uint8_t>((elseStart >> 8) & 0xFF);
        processChunk_.code[jumpPos + 1] = static_cast<uint8_t>(elseStart & 0xFF);

        for (const auto& s : ifs->elseBranch) {
            compileStmt(s.get());
        }

        // Patch exit jump
        size_t afterElse = processChunk_.code.size();
        processChunk_.code[exitJumpPos] = static_cast<uint8_t>((afterElse >> 8) & 0xFF);
        processChunk_.code[exitJumpPos + 1] = static_cast<uint8_t>(afterElse & 0xFF);
    }
}

void VM::compileExpr(Expr* expr) {
    if (!expr) return;

    if (auto* num = ast_cast<NumberLiteral>(expr)) {
        size_t idx = processChunk_.addConstant(num->value);
        processChunk_.emitOp(OpCode::Constant);
        processChunk_.emitByte(static_cast<uint8_t>(idx));
    } else if (auto* b = ast_cast<BooleanLiteral>(expr)) {
        size_t idx = processChunk_.addConstant(b->value ? 1.0 : 0.0);
        processChunk_.emitOp(OpCode::Constant);
        processChunk_.emitByte(static_cast<uint8_t>(idx));
    } else if (auto* id = ast_cast<IdentifierExpr>(expr)) {
        int slot = resolveLocal(id->name);
        if (slot != -1) {
            processChunk_.emitOp(OpCode::GetLocal);
            processChunk_.emitByte(static_cast<uint8_t>(slot));
        } else {
            // Check known constants
            if (id->name == "pi") {
                size_t idx = processChunk_.addConstant(std::numbers::pi_v<double>);
                processChunk_.emitOp(OpCode::Constant);
                processChunk_.emitByte(static_cast<uint8_t>(idx));
            } else {
                size_t idx = processChunk_.addConstant(0.0);
                processChunk_.emitOp(OpCode::Constant);
                processChunk_.emitByte(static_cast<uint8_t>(idx));
            }
        }
    } else if (auto* mem = ast_cast<MemberExpr>(expr)) {
        // e.g. math.pi
        auto* baseId = ast_cast<IdentifierExpr>(mem->object.get());
        if (baseId && baseId->name == "math" && mem->member == "pi") {
            size_t idx = processChunk_.addConstant(std::numbers::pi_v<double>);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
    } else if (auto* ie = ast_cast<IndexExpr>(expr)) {
        compileExpr(ie->object.get());
    } else if (auto* bin = ast_cast<BinaryExpr>(expr)) {
        compileExpr(bin->left.get());
        compileExpr(bin->right.get());

        switch (bin->op) {
            case TokenType::Plus: processChunk_.emitOp(OpCode::Add); break;
            case TokenType::Minus: processChunk_.emitOp(OpCode::Subtract); break;
            case TokenType::Multiply: processChunk_.emitOp(OpCode::Multiply); break;
            case TokenType::Divide: processChunk_.emitOp(OpCode::Divide); break;
            case TokenType::Modulo: processChunk_.emitOp(OpCode::Modulo); break;
            case TokenType::Power: processChunk_.emitOp(OpCode::Power); break;
            case TokenType::Equals: processChunk_.emitOp(OpCode::Equal); break;
            case TokenType::NotEquals: processChunk_.emitOp(OpCode::NotEqual); break;
            case TokenType::Less: processChunk_.emitOp(OpCode::Less); break;
            case TokenType::LessEqual: processChunk_.emitOp(OpCode::LessEqual); break;
            case TokenType::Greater: processChunk_.emitOp(OpCode::Greater); break;
            case TokenType::GreaterEqual: processChunk_.emitOp(OpCode::GreaterEqual); break;
            case TokenType::And: processChunk_.emitOp(OpCode::LogicAnd); break;
            case TokenType::Or: processChunk_.emitOp(OpCode::LogicOr); break;
            default: break;
        }
    } else if (auto* un = ast_cast<UnaryExpr>(expr)) {
        compileExpr(un->operand.get());
        if (un->op == TokenType::Minus) {
            processChunk_.emitOp(OpCode::Negate);
        } else if (un->op == TokenType::Not) {
            processChunk_.emitOp(OpCode::LogicNot);
        }
    } else if (auto* call = ast_cast<CallExpr>(expr)) {
        auto emitBuiltinCall = [&](const std::string& name) -> bool {
            auto emitArgs = [&](size_t count) {
                for (size_t i = 0; i < count; ++i) {
                    if (i < call->args.size()) {
                        compileExpr(call->args[i].get());
                    } else {
                        size_t zIdx = processChunk_.addConstant(0.0);
                        processChunk_.emitOp(OpCode::Constant);
                        processChunk_.emitByte(static_cast<uint8_t>(zIdx));
                    }
                }
            };

            if (name == "sin") { emitArgs(1); processChunk_.emitOp(OpCode::MathSin); return true; }
            if (name == "cos") { emitArgs(1); processChunk_.emitOp(OpCode::MathCos); return true; }
            if (name == "tanh") { emitArgs(1); processChunk_.emitOp(OpCode::MathTanh); return true; }
            if (name == "exp") { emitArgs(1); processChunk_.emitOp(OpCode::MathExp); return true; }
            if (name == "floor") { emitArgs(1); processChunk_.emitOp(OpCode::MathFloor); return true; }
            if (name == "sqrt") { emitArgs(1); processChunk_.emitOp(OpCode::MathSqrt); return true; }
            if (name == "abs") { emitArgs(1); processChunk_.emitOp(OpCode::MathAbs); return true; }
            if (name == "fract") { emitArgs(1); processChunk_.emitOp(OpCode::MathFract); return true; }
            if (name == "sign") { emitArgs(1); processChunk_.emitOp(OpCode::MathSign); return true; }
            if (name == "random") { processChunk_.emitOp(OpCode::MathRandom); return true; }

            if (name == "min") { emitArgs(2); processChunk_.emitOp(OpCode::MathMin); return true; }
            if (name == "max") { emitArgs(2); processChunk_.emitOp(OpCode::MathMax); return true; }
            if (name == "atan2") { emitArgs(2); processChunk_.emitOp(OpCode::MathAtan2); return true; }
            if (name == "hypot" || name == "length") { emitArgs(2); processChunk_.emitOp(OpCode::MathHypot); return true; }
            if (name == "step") { emitArgs(2); processChunk_.emitOp(OpCode::MathStep); return true; }
            if (name == "noise" || name == "noise2d") { emitArgs(2); processChunk_.emitOp(OpCode::MathNoise2D); return true; }
            if (name == "fbm" || name == "fbm2d") { emitArgs(2); processChunk_.emitOp(OpCode::MathFbm2D); return true; }
            if (name == "sample_r") { emitArgs(2); processChunk_.emitOp(OpCode::ShaderSampleR); return true; }
            if (name == "sample_g") { emitArgs(2); processChunk_.emitOp(OpCode::ShaderSampleG); return true; }
            if (name == "sample_b") { emitArgs(2); processChunk_.emitOp(OpCode::ShaderSampleB); return true; }
            if (name == "sample_lum") { emitArgs(2); processChunk_.emitOp(OpCode::ShaderSampleLum); return true; }

            if (name == "clamp") { emitArgs(3); processChunk_.emitOp(OpCode::MathClamp); return true; }
            if (name == "mix" || name == "lerp") { emitArgs(3); processChunk_.emitOp(OpCode::MathMix); return true; }
            if (name == "smoothstep") { emitArgs(3); processChunk_.emitOp(OpCode::MathSmoothstep); return true; }
            if (name == "sdf_suit") { emitArgs(3); processChunk_.emitOp(OpCode::ShaderSdfSuit); return true; }
            return false;
        };

        // Identify member calls: math.sin, math.cos, etc.
        auto* callMem = ast_cast<MemberExpr>(call->callee.get());
        if (callMem) {
            auto* objId = ast_cast<IdentifierExpr>(callMem->object.get());
            if (objId && (objId->name == "math" || objId->name == "shader")) {
                if (emitBuiltinCall(callMem->member)) return;
            } else if (objId && objId->name == "params" && callMem->member == "get") {
                uint8_t paramIdx = 0;
                if (!call->args.empty()) {
                    auto* strLit = ast_cast<StringLiteral>(call->args[0].get());
                    if (strLit) {
                        if (strLit->value == "Cutoff") paramIdx = 0;
                        else if (strLit->value == "Resonance") paramIdx = 1;
                        else if (strLit->value == "Volume") paramIdx = 2;
                    }
                }
                processChunk_.emitOp(OpCode::GetParam);
                processChunk_.emitByte(paramIdx);
                return;
            }
        }

        // Check direct function call (e.g. sin(...), smoothstep(...), sdf_suit(...))
        auto* fnId = ast_cast<IdentifierExpr>(call->callee.get());
        if (fnId) {
            if (emitBuiltinCall(fnId->name)) return;
            if (fnId->name == "param") {
                uint8_t paramIdx = 0;
                if (!call->args.empty()) {
                    auto* numLit = ast_cast<NumberLiteral>(call->args[0].get());
                    if (numLit) paramIdx = static_cast<uint8_t>(numLit->value);
                }
                processChunk_.emitOp(OpCode::GetParam);
                processChunk_.emitByte(paramIdx);
                return;
            }
        }

        // Fallback: compile single argument if available
        if (!call->args.empty()) {
            compileExpr(call->args[0].get());
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
    } else if (auto* list = ast_cast<ListLiteral>(expr)) {
        // For audio FX returning [l, r], compile first element for mono/left
        if (!list->elements.empty()) {
            compileExpr(list->elements[0].get());
        } else {
            size_t idx = processChunk_.addConstant(0.0);
            processChunk_.emitOp(OpCode::Constant);
            processChunk_.emitByte(static_cast<uint8_t>(idx));
        }
    }
}

double VM::executeProcess(double time, double freq, double note, const float* params, size_t numParams) noexcept {
    if (!hasProcess_ || processChunk_.code.empty()) {
        return 0.0;
    }

    // Set standard local variables
    locals_[0] = time;
    locals_[1] = freq;
    locals_[2] = note;

    // Load any input parameter array into cache
    if (params && numParams > 0) {
        const size_t count = std::min(numParams, PARAMS_MAX);
        for (size_t i = 0; i < count; ++i) {
            params_[i] = params[i];
        }
    }

    size_t ip = 0;
    size_t sp = 0;
    const uint8_t* code = processChunk_.code.data();
    const size_t codeSize = processChunk_.code.size();
    const double* constants = processChunk_.constants.data();

    while (ip < codeSize) {
        auto op = static_cast<OpCode>(code[ip++]);
        switch (op) {
            case OpCode::Constant: {
                uint8_t idx = code[ip++];
                stack_[sp++] = constants[idx];
                break;
            }
            case OpCode::GetLocal: {
                uint8_t slot = code[ip++];
                stack_[sp++] = locals_[slot];
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = code[ip++];
                locals_[slot] = stack_[--sp];
                break;
            }
            case OpCode::GetParam: {
                uint8_t idx = code[ip++];
                stack_[sp++] = static_cast<double>(params_[idx]);
                break;
            }
            case OpCode::Add: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a + b;
                break;
            }
            case OpCode::Subtract: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a - b;
                break;
            }
            case OpCode::Multiply: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a * b;
                break;
            }
            case OpCode::Divide: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? (a / b) : 0.0;
                break;
            }
            case OpCode::Modulo: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? std::fmod(a, b) : 0.0;
                break;
            }
            case OpCode::Power: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = std::pow(a, b);
                break;
            }
            case OpCode::Negate: {
                stack_[sp - 1] = -stack_[sp - 1];
                break;
            }
            case OpCode::Equal: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a == b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::NotEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a != b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Less: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a < b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::LessEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a <= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Greater: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a > b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::GreaterEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a >= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::MathSin: {
                stack_[sp - 1] = std::sin(stack_[sp - 1]);
                break;
            }
            case OpCode::MathCos: {
                stack_[sp - 1] = std::cos(stack_[sp - 1]);
                break;
            }
            case OpCode::MathTanh: {
                stack_[sp - 1] = std::tanh(stack_[sp - 1]);
                break;
            }
            case OpCode::MathExp: {
                stack_[sp - 1] = std::exp(stack_[sp - 1]);
                break;
            }
            case OpCode::MathFloor: {
                stack_[sp - 1] = std::floor(stack_[sp - 1]);
                break;
            }
            case OpCode::MathSqrt: {
                stack_[sp - 1] = std::sqrt(std::max(0.0, stack_[sp - 1]));
                break;
            }
            case OpCode::MathRandom: {
                rngState_ = rngState_ * 1664525u + 1013904223u;
                stack_[sp++] = static_cast<double>(rngState_) / 4294967296.0;
                break;
            }
            case OpCode::Jump: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip = offset;
                break;
            }
            case OpCode::JumpIfFalse: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip += 2;
                double cond = stack_[--sp];
                if (std::abs(cond) < 1e-12) {
                    ip = offset;
                }
                break;
            }
            case OpCode::Return: {
                if (sp > 0) {
                    return stack_[--sp];
                }
                return 0.0;
            }
        }
    }

    return (sp > 0) ? stack_[--sp] : 0.0;
}

std::pair<double, double> VM::executeStereoProcess(double inL, double inR, const float* params, size_t numParams) noexcept {
    if (!hasProcess_ || processChunk_.code.empty()) {
        return {inL, inR};
    }

    // Set stereo input locals
    if (slotInL_ >= 0 && slotInL_ < static_cast<int>(LOCALS_MAX)) {
        locals_[slotInL_] = inL;
    }
    if (slotInR_ >= 0 && slotInR_ < static_cast<int>(LOCALS_MAX)) {
        locals_[slotInR_] = inR;
    }

    int sInL = resolveLocal("in_l");
    if (sInL >= 0 && sInL < static_cast<int>(LOCALS_MAX)) locals_[sInL] = inL;
    int sInR = resolveLocal("in_r");
    if (sInR >= 0 && sInR < static_cast<int>(LOCALS_MAX)) locals_[sInR] = inR;

    // Load parameters
    if (params && numParams > 0) {
        const size_t count = std::min(numParams, PARAMS_MAX);
        for (size_t i = 0; i < count; ++i) {
            params_[i] = params[i];
        }
    }

    size_t ip = 0;
    size_t sp = 0;
    const uint8_t* code = processChunk_.code.data();
    const size_t codeSize = processChunk_.code.size();
    const double* constants = processChunk_.constants.data();

    while (ip < codeSize) {
        auto op = static_cast<OpCode>(code[ip++]);
        switch (op) {
            case OpCode::Constant: {
                uint8_t idx = code[ip++];
                stack_[sp++] = constants[idx];
                break;
            }
            case OpCode::GetLocal: {
                uint8_t slot = code[ip++];
                stack_[sp++] = locals_[slot];
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = code[ip++];
                locals_[slot] = stack_[--sp];
                break;
            }
            case OpCode::GetParam: {
                uint8_t idx = code[ip++];
                stack_[sp++] = static_cast<double>(params_[idx]);
                break;
            }
            case OpCode::Add: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a + b;
                break;
            }
            case OpCode::Subtract: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a - b;
                break;
            }
            case OpCode::Multiply: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = a * b;
                break;
            }
            case OpCode::Divide: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? (a / b) : 0.0;
                break;
            }
            case OpCode::Modulo: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(b) > 1e-12) ? std::fmod(a, b) : 0.0;
                break;
            }
            case OpCode::Power: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = std::pow(a, b);
                break;
            }
            case OpCode::Negate: {
                stack_[sp - 1] = -stack_[sp - 1];
                break;
            }
            case OpCode::Equal: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(a - b) < 1e-9) ? 1.0 : 0.0;
                break;
            }
            case OpCode::NotEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (std::abs(a - b) >= 1e-9) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Less: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a < b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::LessEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a <= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::Greater: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a > b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::GreaterEqual: {
                double b = stack_[--sp];
                double a = stack_[--sp];
                stack_[sp++] = (a >= b) ? 1.0 : 0.0;
                break;
            }
            case OpCode::MathSin: {
                stack_[sp - 1] = std::sin(stack_[sp - 1]);
                break;
            }
            case OpCode::MathCos: {
                stack_[sp - 1] = std::cos(stack_[sp - 1]);
                break;
            }
            case OpCode::MathTanh: {
                stack_[sp - 1] = std::tanh(stack_[sp - 1]);
                break;
            }
            case OpCode::MathExp: {
                stack_[sp - 1] = std::exp(stack_[sp - 1]);
                break;
            }
            case OpCode::MathFloor: {
                stack_[sp - 1] = std::floor(stack_[sp - 1]);
                break;
            }
            case OpCode::MathSqrt: {
                stack_[sp - 1] = std::sqrt(std::max(0.0, stack_[sp - 1]));
                break;
            }
            case OpCode::MathRandom: {
                rngState_ = rngState_ * 1664525u + 1013904223u;
                stack_[sp++] = static_cast<double>(rngState_) / 4294967296.0;
                break;
            }
            case OpCode::Jump: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip = offset;
                break;
            }
            case OpCode::JumpIfFalse: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip += 2;
                double cond = stack_[--sp];
                if (std::abs(cond) < 1e-12) {
                    ip = offset;
                }
                break;
            }
            case OpCode::Return: {
                if (sp >= 2) {
                    double r = stack_[--sp];
                    double l = stack_[--sp];
                    return {l, r};
                } else if (sp == 1) {
                    double val = stack_[--sp];
                    return {val, val};
                }
                return {0.0, 0.0};
            }
        }
    }

    if (sp >= 2) {
        double r = stack_[--sp];
        double l = stack_[--sp];
        return {l, r};
    } else if (sp == 1) {
        double val = stack_[--sp];
        return {val, val};
    }
    return {0.0, 0.0};
}

void VM::executePixelShader(const ShaderPixelContext& ctx,
                            double& outR, double& outG, double& outB, double& outA) const noexcept {
    outR = ctx.inR;
    outG = ctx.inG;
    outB = ctx.inB;
    outA = ctx.inA;
    if (!hasProcess_ || processChunk_.code.empty()) {
        return;
    }

    // Thread-local stack & local register file (safe for parallel OpenMP execution)
    std::array<double, LOCALS_MAX> loc{};
    std::array<double, STACK_MAX> stk{};

    auto setSlot = [&](int slot, double val) {
        if (slot >= 0 && slot < static_cast<int>(LOCALS_MAX)) {
            loc[slot] = val;
        }
    };

    setSlot(shaderSlots_.u,      ctx.u);
    setSlot(shaderSlots_.v,      ctx.v);
    setSlot(shaderSlots_.nx,     ctx.nx);
    setSlot(shaderSlots_.ny,     ctx.ny);
    setSlot(shaderSlots_.px,     ctx.px);
    setSlot(shaderSlots_.py,     ctx.py);
    setSlot(shaderSlots_.inR,    ctx.inR);
    setSlot(shaderSlots_.inG,    ctx.inG);
    setSlot(shaderSlots_.inB,    ctx.inB);
    setSlot(shaderSlots_.inA,    ctx.inA);
    setSlot(shaderSlots_.r,      ctx.inR);
    setSlot(shaderSlots_.g,      ctx.inG);
    setSlot(shaderSlots_.b,      ctx.inB);
    setSlot(shaderSlots_.a,      ctx.inA);
    setSlot(shaderSlots_.height, ctx.height);
    setSlot(shaderSlots_.elev,   ctx.height);
    setSlot(shaderSlots_.mask,   ctx.mask);
    setSlot(shaderSlots_.rank,   ctx.rank);
    setSlot(shaderSlots_.suit,   ctx.suit);
    setSlot(shaderSlots_.isRed,  ctx.isRed);
    setSlot(shaderSlots_.u0,     ctx.u0);
    setSlot(shaderSlots_.u1,     ctx.u1);
    setSlot(shaderSlots_.u2,     ctx.u2);
    setSlot(shaderSlots_.u3,     ctx.u3);

    size_t ip = 0;
    size_t sp = 0;
    const uint8_t* code = processChunk_.code.data();
    const size_t codeSize = processChunk_.code.size();
    const double* constants = processChunk_.constants.data();
    uint32_t h1 = static_cast<uint32_t>(static_cast<int64_t>(ctx.px)) * 374761393u
                + static_cast<uint32_t>(static_cast<int64_t>(ctx.py)) * 668265263u
                + 0x9E3779B9u;
    h1 = (h1 ^ (h1 >> 13)) * 1274126177u;
    uint32_t rng = h1 ^ (h1 >> 16);

    // Hard instruction budget prevents any infinite loop from stalling a worker thread
    constexpr size_t kMaxPixelOps = 1024;
    size_t ops = 0;

    while (ip < codeSize && ops++ < kMaxPixelOps) {
        auto op = static_cast<OpCode>(code[ip++]);
        switch (op) {
            case OpCode::Constant: {
                uint8_t idx = code[ip++];
                if (sp < STACK_MAX) stk[sp++] = constants[idx];
                break;
            }
            case OpCode::GetLocal: {
                uint8_t slot = code[ip++];
                if (sp < STACK_MAX) stk[sp++] = (slot < LOCALS_MAX) ? loc[slot] : 0.0;
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = code[ip++];
                if (sp > 0) {
                    double v = stk[--sp];
                    if (slot < LOCALS_MAX) loc[slot] = v;
                }
                break;
            }
            case OpCode::GetParam: {
                uint8_t idx = code[ip++];
                if (sp < STACK_MAX) stk[sp++] = (idx < PARAMS_MAX) ? static_cast<double>(params_[idx]) : 0.0;
                break;
            }
            case OpCode::Add: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] += b; }
                break;
            }
            case OpCode::Subtract: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] -= b; }
                break;
            }
            case OpCode::Multiply: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] *= b; }
                break;
            }
            case OpCode::Divide: {
                if (sp >= 2) {
                    double b = stk[--sp];
                    stk[sp - 1] = (std::abs(b) > 1e-12) ? (stk[sp - 1] / b) : 0.0;
                }
                break;
            }
            case OpCode::Modulo: {
                if (sp >= 2) {
                    double b = stk[--sp];
                    stk[sp - 1] = (std::abs(b) > 1e-12) ? std::fmod(stk[sp - 1], b) : 0.0;
                }
                break;
            }
            case OpCode::Power: {
                if (sp >= 2) {
                    double b = stk[--sp];
                    stk[sp - 1] = std::pow(std::max(0.0, stk[sp - 1]), b);
                }
                break;
            }
            case OpCode::Negate: {
                if (sp >= 1) stk[sp - 1] = -stk[sp - 1];
                break;
            }
            case OpCode::Equal: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = (std::abs(stk[sp - 1] - b) < 1e-7) ? 1.0 : 0.0; }
                break;
            }
            case OpCode::NotEqual: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = (std::abs(stk[sp - 1] - b) >= 1e-7) ? 1.0 : 0.0; }
                break;
            }
            case OpCode::Less: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = (stk[sp - 1] < b) ? 1.0 : 0.0; }
                break;
            }
            case OpCode::LessEqual: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = (stk[sp - 1] <= b) ? 1.0 : 0.0; }
                break;
            }
            case OpCode::Greater: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = (stk[sp - 1] > b) ? 1.0 : 0.0; }
                break;
            }
            case OpCode::GreaterEqual: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = (stk[sp - 1] >= b) ? 1.0 : 0.0; }
                break;
            }
            case OpCode::LogicAnd: {
                if (sp >= 2) {
                    double b = stk[--sp];
                    stk[sp - 1] = (std::abs(stk[sp - 1]) > 1e-9 && std::abs(b) > 1e-9) ? 1.0 : 0.0;
                }
                break;
            }
            case OpCode::LogicOr: {
                if (sp >= 2) {
                    double b = stk[--sp];
                    stk[sp - 1] = (std::abs(stk[sp - 1]) > 1e-9 || std::abs(b) > 1e-9) ? 1.0 : 0.0;
                }
                break;
            }
            case OpCode::LogicNot: {
                if (sp >= 1) stk[sp - 1] = (std::abs(stk[sp - 1]) < 1e-9) ? 1.0 : 0.0;
                break;
            }
            case OpCode::MathSin: {
                if (sp >= 1) stk[sp - 1] = std::sin(stk[sp - 1]);
                break;
            }
            case OpCode::MathCos: {
                if (sp >= 1) stk[sp - 1] = std::cos(stk[sp - 1]);
                break;
            }
            case OpCode::MathTanh: {
                if (sp >= 1) stk[sp - 1] = std::tanh(stk[sp - 1]);
                break;
            }
            case OpCode::MathExp: {
                if (sp >= 1) stk[sp - 1] = std::exp(stk[sp - 1]);
                break;
            }
            case OpCode::MathFloor: {
                if (sp >= 1) stk[sp - 1] = std::floor(stk[sp - 1]);
                break;
            }
            case OpCode::MathSqrt: {
                if (sp >= 1) stk[sp - 1] = std::sqrt(std::max(0.0, stk[sp - 1]));
                break;
            }
            case OpCode::MathAbs: {
                if (sp >= 1) stk[sp - 1] = std::abs(stk[sp - 1]);
                break;
            }
            case OpCode::MathFract: {
                if (sp >= 1) stk[sp - 1] = stk[sp - 1] - std::floor(stk[sp - 1]);
                break;
            }
            case OpCode::MathSign: {
                if (sp >= 1) stk[sp - 1] = (stk[sp - 1] > 0.0) ? 1.0 : ((stk[sp - 1] < 0.0) ? -1.0 : 0.0);
                break;
            }
            case OpCode::MathRandom: {
                rng = rng * 1664525u + 1013904223u;
                if (sp < STACK_MAX) stk[sp++] = static_cast<double>(rng) / 4294967296.0;
                break;
            }
            case OpCode::MathMin: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = std::min(stk[sp - 1], b); }
                break;
            }
            case OpCode::MathMax: {
                if (sp >= 2) { double b = stk[--sp]; stk[sp - 1] = std::max(stk[sp - 1], b); }
                break;
            }
            case OpCode::MathAtan2: {
                if (sp >= 2) { double x = stk[--sp]; stk[sp - 1] = std::atan2(stk[sp - 1], x); }
                break;
            }
            case OpCode::MathHypot: {
                if (sp >= 2) { double y = stk[--sp]; stk[sp - 1] = std::hypot(stk[sp - 1], y); }
                break;
            }
            case OpCode::MathStep: {
                if (sp >= 2) { double x = stk[--sp]; stk[sp - 1] = (x < stk[sp - 1]) ? 0.0 : 1.0; }
                break;
            }
            case OpCode::MathNoise2D: {
                if (sp >= 2) { double y = stk[--sp]; stk[sp - 1] = smoothValueNoise2D(stk[sp - 1], y); }
                break;
            }
            case OpCode::MathFbm2D: {
                if (sp >= 2) { double y = stk[--sp]; stk[sp - 1] = fbmNoise2D(stk[sp - 1], y); }
                break;
            }
            case OpCode::ShaderSampleR:
            case OpCode::ShaderSampleG:
            case OpCode::ShaderSampleB:
            case OpCode::ShaderSampleLum: {
                if (sp >= 2) {
                    double v = stk[--sp];
                    double u = stk[sp - 1];
                    double sr = 0.0, sg = 0.0, sb = 0.0;
                    sampleCanvasRgb(ctx, u, v, sr, sg, sb);
                    if (op == OpCode::ShaderSampleR) stk[sp - 1] = sr;
                    else if (op == OpCode::ShaderSampleG) stk[sp - 1] = sg;
                    else if (op == OpCode::ShaderSampleB) stk[sp - 1] = sb;
                    else stk[sp - 1] = 0.299 * sr + 0.587 * sg + 0.114 * sb;
                }
                break;
            }
            case OpCode::MathClamp: {
                if (sp >= 3) {
                    double hi = stk[--sp];
                    double lo = stk[--sp];
                    stk[sp - 1] = std::clamp(stk[sp - 1], lo, hi);
                }
                break;
            }
            case OpCode::MathMix: {
                if (sp >= 3) {
                    double t = stk[--sp];
                    double b = stk[--sp];
                    double a = stk[sp - 1];
                    stk[sp - 1] = a + (b - a) * t;
                }
                break;
            }
            case OpCode::MathSmoothstep: {
                if (sp >= 3) {
                    double x = stk[--sp];
                    double e1 = stk[--sp];
                    double e0 = stk[sp - 1];
                    double d = e1 - e0;
                    double t = (std::abs(d) > 1e-9) ? std::clamp((x - e0) / d, 0.0, 1.0) : ((x >= e0) ? 1.0 : 0.0);
                    stk[sp - 1] = t * t * (3.0 - 2.0 * t);
                }
                break;
            }
            case OpCode::ShaderSdfSuit: {
                if (sp >= 3) {
                    int sInt = static_cast<int>(std::round(stk[--sp]));
                    double y = stk[--sp];
                    double x = stk[sp - 1];
                    stk[sp - 1] = evalSuitSdf(x, y, sInt);
                }
                break;
            }
            case OpCode::Jump: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip = offset;
                break;
            }
            case OpCode::JumpIfFalse: {
                uint16_t offset = (static_cast<uint16_t>(code[ip]) << 8) | code[ip + 1];
                ip += 2;
                double cond = (sp > 0) ? stk[--sp] : 0.0;
                if (std::abs(cond) < 1e-12) {
                    ip = offset;
                }
                break;
            }
            case OpCode::Return: {
                ip = codeSize;
                break;
            }
        }
    }

    if (sp >= 4) {
        outA = stk[--sp];
        outB = stk[--sp];
        outG = stk[--sp];
        outR = stk[--sp];
    } else if (sp == 3) {
        outB = stk[--sp];
        outG = stk[--sp];
        outR = stk[--sp];
    } else if (sp == 2) {
        outG = stk[--sp];
        outR = stk[--sp];
        outB = ctx.inB;
    } else if (sp == 1) {
        double v = stk[--sp];
        outR = v; outG = v; outB = v;
    } else {
        // Or if the shader assigned to local `r, g, b, a` without returning a list
        if (shaderSlots_.r >= 0) outR = loc[shaderSlots_.r];
        if (shaderSlots_.g >= 0) outG = loc[shaderSlots_.g];
        if (shaderSlots_.b >= 0) outB = loc[shaderSlots_.b];
        if (shaderSlots_.a >= 0) outA = loc[shaderSlots_.a];
    }
}

} // namespace eatsbits::eatscript
