#include "eatsbits/eatscript/wgsl_transpiler.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include <iomanip>
#include <sstream>

namespace eatsbits::eatscript {

std::string WgslShaderTranspiler::buildPostFxPreamble() {
    return R"(
struct PostFxUniforms {
    u0: f32,
    u1: f32,
    u2: f32,
    u3: f32,
    intensity: f32,
    param1: f32,
    param2: f32,
    time: f32,
    width: f32,
    height: f32,
    texel_x: f32,
    texel_y: f32,
};

@group(0) @binding(0) var<uniform> ufx: PostFxUniforms;
@group(0) @binding(1) var sceneTex: texture_2d<f32>;
@group(0) @binding(2) var sceneSampler: sampler;

struct VsOut {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VsOut {
    var out: VsOut;
    let x = f32((vid << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(vid & 2u) * 2.0 - 1.0;
    out.pos = vec4<f32>(x, -y, 0.0, 1.0);
    out.uv = vec2<f32>(x * 0.5 + 0.5, y * 0.5 + 0.5);
    return out;
}

fn es_bool(x: f32) -> bool {
    return x != 0.0;
}

fn es_mod(a: f32, b: f32) -> f32 {
    if (b == 0.0) { return 0.0; }
    return a - b * floor(a / b);
}

fn hypot(x: f32, y: f32) -> f32 {
    return length(vec2<f32>(x, y));
}

fn es_hash12(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn noise2d(x: f32, y: f32) -> f32 {
    let i = floor(vec2<f32>(x, y));
    let f = fract(vec2<f32>(x, y));
    let u = f * f * (3.0 - 2.0 * f);
    let a = es_hash12(i + vec2<f32>(0.0, 0.0));
    let b = es_hash12(i + vec2<f32>(1.0, 0.0));
    let c = es_hash12(i + vec2<f32>(0.0, 1.0));
    let d = es_hash12(i + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn fbm(x_in: f32, y_in: f32, octaves_in: f32) -> f32 {
    var val: f32 = 0.0;
    var amp: f32 = 0.5;
    var x: f32 = x_in;
    var y: f32 = y_in;
    let octs = clamp(i32(octaves_in), 1, 8);
    for (var i: i32 = 0; i < 8; i = i + 1) {
        if (i >= octs) { break; }
        val = val + amp * noise2d(x, y);
        x = x * 2.03 + 1.7;
        y = y * 2.03 + 9.2;
        amp = amp * 0.5;
    }
    return val;
}

fn sample_scene(uv: vec2<f32>) -> vec4<f32> {
    return textureSampleLevel(sceneTex, sceneSampler, clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)), 0.0);
}

fn sample_r(u: f32, v: f32) -> f32 {
    return sample_scene(vec2<f32>(u, v)).r;
}

fn sample_g(u: f32, v: f32) -> f32 {
    return sample_scene(vec2<f32>(u, v)).g;
}

fn sample_b(u: f32, v: f32) -> f32 {
    return sample_scene(vec2<f32>(u, v)).b;
}

fn sample_lum(u: f32, v: f32) -> f32 {
    let c = sample_scene(vec2<f32>(u, v)).rgb;
    return dot(c, vec3<f32>(0.299, 0.587, 0.114));
}

fn bayer4x4(px: f32, py: f32) -> f32 {
    let ix = i32(es_mod(floor(px), 4.0));
    let iy = i32(es_mod(floor(py), 4.0));
    let idx = iy * 4 + ix;
    var m = array<f32, 16>(
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0
    );
    return (m[idx] + 0.5) / 16.0;
}
)";
}

std::string WgslShaderTranspiler::indent() const {
    return std::string(static_cast<size_t>(indentLevel_) * 4, ' ');
}

std::string WgslShaderTranspiler::transpilePostFxSource(const std::string& source,
                                                        const std::string& funcName,
                                                        std::string* errorOut) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto prog = parser.parse();

    const FunctionDef* target = nullptr;
    if (prog) {
        for (const auto& stmt : prog->statements) {
        if (stmt->kind == StmtKind::FunctionDef) {
            auto* fn = static_cast<const FunctionDef*>(stmt.get());
            if (!funcName.empty()) {
                if (fn->name == funcName) {
                    target = fn;
                    break;
                }
            } else if (fn->name != "on_draw_background" && fn->name != "on_post_process") {
                target = fn;
                break;
            }
        }
    }
    }

    if (!target) {
        if (errorOut) *errorOut = "No shader function found in Eatscript source.";
        return "";
    }
    return transpilePostFx(target, errorOut);
}

std::string WgslShaderTranspiler::transpilePostFx(const FunctionDef* func, std::string* errorOut) {
    if (!func) {
        if (errorOut) *errorOut = "Null FunctionDef.";
        return "";
    }

    body_.clear();
    error_.clear();
    indentLevel_ = 1;
    declaredLocals_ = {
        "u", "v", "nx", "ny", "px", "py",
        "in_r", "in_g", "in_b", "in_a",
        "r", "g", "b", "a",
        "u0", "u1", "u2", "u3",
        "intensity", "param1", "param2", "time",
        "width", "height", "texel_x", "texel_y"
    };

    for (const auto& stmt : func->body) {
        emitStmt(stmt.get());
        if (!error_.empty()) {
            if (errorOut) *errorOut = error_;
            return "";
        }
    }

    std::ostringstream ss;
    ss << buildPostFxPreamble();
    ss << R"(
@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let u: f32 = in.uv.x;
    let v: f32 = in.uv.y;
    let aspect: f32 = ufx.height / max(ufx.width, 1.0);
    let nx: f32 = u * 2.0 - 1.0;
    let ny: f32 = (v * 2.0 - 1.0) * aspect;
    let px: f32 = u * ufx.width;
    let py: f32 = v * ufx.height;

    let baseCol = sample_scene(in.uv);
    let in_r: f32 = baseCol.r;
    let in_g: f32 = baseCol.g;
    let in_b: f32 = baseCol.b;
    let in_a: f32 = baseCol.a;

    var r: f32 = in_r;
    var g: f32 = in_g;
    var b: f32 = in_b;
    var a: f32 = in_a;

    let u0: f32 = ufx.u0;
    let u1: f32 = ufx.u1;
    let u2: f32 = ufx.u2;
    let u3: f32 = ufx.u3;
    let intensity: f32 = ufx.intensity;
    let param1: f32 = ufx.param1;
    let param2: f32 = ufx.param2;
    let time: f32 = ufx.time;
    let width: f32 = ufx.width;
    let height: f32 = ufx.height;
    let texel_x: f32 = ufx.texel_x;
    let texel_y: f32 = ufx.texel_y;

)";
    ss << body_;
    ss << R"(
    let mixW: f32 = clamp(ufx.intensity, 0.0, 1.0);
    let outRgb = mix(baseCol.rgb, clamp(vec3<f32>(r, g, b), vec3<f32>(0.0), vec3<f32>(1.0)), mixW);
    return vec4<f32>(outRgb, clamp(a, 0.0, 1.0));
}
)";
    return ss.str();
}

void WgslShaderTranspiler::emitStmt(const Stmt* stmt) {
    if (!stmt || !error_.empty()) return;

    switch (stmt->kind) {
        case StmtKind::Assign: {
            auto* as = static_cast<const AssignStmt*>(stmt);
            std::string val = emitExpr(as->value.get());
            if (declaredLocals_.count(as->variableName)) {
                body_ += indent() + as->variableName + " = " + val + ";\n";
            } else {
                declaredLocals_.insert(as->variableName);
                body_ += indent() + "var " + as->variableName + ": f32 = " + val + ";\n";
            }
            break;
        }
        case StmtKind::Expr: {
            auto* es = static_cast<const ExprStmt*>(stmt);
            std::string val = emitExpr(es->expression.get());
            body_ += indent() + "_ = " + val + ";\n";
            break;
        }
        case StmtKind::Return: {
            auto* rs = static_cast<const ReturnStmt*>(stmt);
            if (rs->value && rs->value->kind == ExprKind::List) {
                auto* list = static_cast<const ListLiteral*>(rs->value.get());
                if (list->elements.size() >= 3) {
                    std::string er = emitExpr(list->elements[0].get());
                    std::string eg = emitExpr(list->elements[1].get());
                    std::string eb = emitExpr(list->elements[2].get());
                    body_ += indent() + "r = " + er + ";\n";
                    body_ += indent() + "g = " + eg + ";\n";
                    body_ += indent() + "b = " + eb + ";\n";
                    if (list->elements.size() >= 4) {
                        std::string ea = emitExpr(list->elements[3].get());
                        body_ += indent() + "a = " + ea + ";\n";
                    }
                }
            }
            break;
        }
        case StmtKind::If: {
            auto* ifs = static_cast<const IfStmt*>(stmt);
            std::string cond = emitExpr(ifs->condition.get());
            body_ += indent() + "if (es_bool(" + cond + ")) {\n";
            indentLevel_++;
            for (const auto& s : ifs->thenBranch) emitStmt(s.get());
            indentLevel_--;
            if (!ifs->elseBranch.empty()) {
                body_ += indent() + "} else {\n";
                indentLevel_++;
                for (const auto& s : ifs->elseBranch) emitStmt(s.get());
                indentLevel_--;
            }
            body_ += indent() + "}\n";
            break;
        }
        default:
            break;
    }
}

std::string WgslShaderTranspiler::emitExpr(const Expr* expr) {
    if (!expr) return "0.0";

    switch (expr->kind) {
        case ExprKind::Number: {
            auto* num = static_cast<const NumberLiteral*>(expr);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(6) << num->value;
            std::string s = ss.str();
            if (s.find('.') == std::string::npos) s += ".0";
            return s;
        }
        case ExprKind::Boolean: {
            auto* b = static_cast<const BooleanLiteral*>(expr);
            return b->value ? "1.0" : "0.0";
        }
        case ExprKind::Identifier: {
            auto* id = static_cast<const IdentifierExpr*>(expr);
            if (id->name == "pi" || id->name == "PI") return "3.14159265";
            return id->name;
        }
        case ExprKind::Member: {
            auto* mem = static_cast<const MemberExpr*>(expr);
            if (mem->object->kind == ExprKind::Identifier) {
                auto* id = static_cast<const IdentifierExpr*>(mem->object.get());
                if (id->name == "math" && mem->member == "pi") return "3.14159265";
            }
            return "0.0";
        }
        case ExprKind::Unary: {
            auto* un = static_cast<const UnaryExpr*>(expr);
            std::string inner = emitExpr(un->operand.get());
            if (un->op == TokenType::Minus) return "(-(" + inner + "))";
            if (un->op == TokenType::Not) return "select(0.0, 1.0, !es_bool(" + inner + "))";
            return inner;
        }
        case ExprKind::Binary: {
            auto* bin = static_cast<const BinaryExpr*>(expr);
            std::string l = emitExpr(bin->left.get());
            std::string r = emitExpr(bin->right.get());
            switch (bin->op) {
                case TokenType::Plus: return "(" + l + " + " + r + ")";
                case TokenType::Minus: return "(" + l + " - " + r + ")";
                case TokenType::Multiply: return "(" + l + " * " + r + ")";
                case TokenType::Divide: return "(" + l + " / " + r + ")";
                case TokenType::Modulo: return "es_mod(" + l + ", " + r + ")";
                case TokenType::Power: return "pow(" + l + ", " + r + ")";
                case TokenType::Equals: return "select(0.0, 1.0, " + l + " == " + r + ")";
                case TokenType::NotEquals: return "select(0.0, 1.0, " + l + " != " + r + ")";
                case TokenType::Less: return "select(0.0, 1.0, " + l + " < " + r + ")";
                case TokenType::LessEqual: return "select(0.0, 1.0, " + l + " <= " + r + ")";
                case TokenType::Greater: return "select(0.0, 1.0, " + l + " > " + r + ")";
                case TokenType::GreaterEqual: return "select(0.0, 1.0, " + l + " >= " + r + ")";
                case TokenType::And: return "select(0.0, 1.0, es_bool(" + l + ") && es_bool(" + r + "))";
                case TokenType::Or: return "select(0.0, 1.0, es_bool(" + l + ") || es_bool(" + r + "))";
                default: return "0.0";
            }
        }
        case ExprKind::Call: {
            auto* call = static_cast<const CallExpr*>(expr);
            std::string fnName;
            if (call->callee->kind == ExprKind::Identifier) {
                fnName = static_cast<const IdentifierExpr*>(call->callee.get())->name;
            } else if (call->callee->kind == ExprKind::Member) {
                fnName = static_cast<const MemberExpr*>(call->callee.get())->member;
            }

            std::vector<std::string> args;
            args.reserve(call->args.size());
            for (const auto& a : call->args) args.push_back(emitExpr(a.get()));

            auto arg = [&](size_t idx, const std::string& def = "0.0") -> std::string {
                return idx < args.size() ? args[idx] : def;
            };

            if (fnName == "lerp") fnName = "mix";
            if (fnName == "noise" || fnName == "noise2d") fnName = "noise2d";
            if (fnName == "fbm2d") fnName = "fbm";

            if (fnName == "sin" || fnName == "cos" || fnName == "tanh" || fnName == "exp" ||
                fnName == "floor" || fnName == "ceil" || fnName == "round" || fnName == "sqrt" ||
                fnName == "abs" || fnName == "fract" || fnName == "sign") {
                return fnName + "(" + arg(0) + ")";
            }
            if (fnName == "min" || fnName == "max" || fnName == "atan2" || fnName == "pow" ||
                fnName == "step" || fnName == "hypot" || fnName == "noise2d" ||
                fnName == "sample_r" || fnName == "sample_g" || fnName == "sample_b" ||
                fnName == "sample_lum" || fnName == "bayer4x4") {
                return fnName + "(" + arg(0) + ", " + arg(1) + ")";
            }
            if (fnName == "length") {
                return "hypot(" + arg(0) + ", " + arg(1) + ")";
            }
            if (fnName == "clamp" || fnName == "mix" || fnName == "smoothstep" || fnName == "fbm") {
                return fnName + "(" + arg(0) + ", " + arg(1) + ", " + arg(2) + ")";
            }
            if (fnName == "random") {
                return "es_hash12(vec2<f32>(px + ufx.time * 13.7, py + ufx.time * 29.3))";
            }
            error_ = "Unsupported shader function in WGSL transpiler: " + fnName;
            return "0.0";
        }
        default:
            return "0.0";
    }
}

} // namespace eatsbits::eatscript
