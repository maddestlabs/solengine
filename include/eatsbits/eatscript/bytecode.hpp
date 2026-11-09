#ifndef EATS_BYTECODE_HPP
#define EATS_BYTECODE_HPP

#include <vector>
#include <cstdint>
#include <string>

namespace eatsbits::eatscript {

enum class OpCode : uint8_t {
    Constant,       // [op, const_idx]
    GetLocal,       // [op, slot]
    SetLocal,       // [op, slot]
    GetParam,       // [op, param_idx]
    
    Add,            // a + b
    Subtract,       // a - b
    Multiply,       // a * b
    Divide,         // a / b
    Modulo,         // a % b
    Power,          // a ** b
    Negate,         // -a

    Equal,          // a == b
    NotEqual,       // a != b
    Less,           // a < b
    LessEqual,      // a <= b
    Greater,        // a > b
    GreaterEqual,   // a >= b
    LogicAnd,       // a and b
    LogicOr,        // a or b
    LogicNot,       // not a

    MathSin,        // sin(a)
    MathCos,        // cos(a)
    MathTanh,       // tanh(a)
    MathExp,        // exp(a)
    MathFloor,      // floor(a)
    MathSqrt,       // sqrt(a)
    MathRandom,     // random()
    MathAbs,        // abs(a)
    MathFract,      // fract(a)
    MathSign,       // sign(a)
    MathMin,        // min(a, b)
    MathMax,        // max(a, b)
    MathAtan2,      // atan2(y, x)
    MathHypot,      // hypot(x, y) / length(x, y)
    MathStep,       // step(edge, x)
    MathClamp,      // clamp(x, lo, hi)
    MathMix,        // mix(a, b, t)
    MathSmoothstep, // smoothstep(edge0, edge1, x)
    MathNoise2D,    // noise(x, y)
    MathFbm2D,      // fbm(x, y)
    ShaderSdfSuit,  // sdf_suit(x, y, suit)
    ShaderSampleR,  // sample_r(u, v)
    ShaderSampleG,  // sample_g(u, v)
    ShaderSampleB,  // sample_b(u, v)
    ShaderSampleLum,// sample_lum(u, v)

    Jump,           // [op, offset_hi, offset_lo]
    JumpIfFalse,    // [op, offset_hi, offset_lo]
    Return          // returns top of stack
};

struct Chunk {
    std::vector<uint8_t> code;
    std::vector<double> constants;
    std::vector<std::string> localNames;

    size_t addConstant(double val) {
        constants.push_back(val);
        return constants.size() - 1;
    }

    void emitByte(uint8_t byte) {
        code.push_back(byte);
    }

    void emitOp(OpCode op) {
        code.push_back(static_cast<uint8_t>(op));
    }
};

inline std::string disassembleChunk(const Chunk& chunk, const std::string& name = "") {
    std::string out;
    if (!name.empty()) {
        out += "== Disassembly: " + name + " ==\n";
    }
    if (!chunk.constants.empty()) {
        out += "Constants (" + std::to_string(chunk.constants.size()) + "):\n";
        for (size_t i = 0; i < chunk.constants.size(); ++i) {
            out += "  [" + std::to_string(i) + "] " + std::to_string(chunk.constants[i]) + "\n";
        }
    }

    out += "Bytecode (" + std::to_string(chunk.code.size()) + " bytes):\n";
    size_t ip = 0;
    int instNum = 0;
    char lineBuf[128];

    while (ip < chunk.code.size()) {
        size_t offset = ip;
        uint8_t opByte = chunk.code[ip++];
        auto op = static_cast<OpCode>(opByte);
        std::string opName;
        std::string detail;

        switch (op) {
            case OpCode::Constant: {
                uint8_t cIdx = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                double val = (cIdx < chunk.constants.size()) ? chunk.constants[cIdx] : 0.0;
                opName = "OP_CONSTANT";
                detail = "[" + std::to_string(cIdx) + "] (" + std::to_string(val) + ")";
                break;
            }
            case OpCode::GetLocal: {
                uint8_t slot = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                opName = "OP_GETLOCAL";
                std::string locName = (slot < chunk.localNames.size()) ? chunk.localNames[slot] : "";
                detail = "slot " + std::to_string(slot) + (locName.empty() ? "" : " (" + locName + ")");
                break;
            }
            case OpCode::SetLocal: {
                uint8_t slot = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                opName = "OP_SETLOCAL";
                std::string locName = (slot < chunk.localNames.size()) ? chunk.localNames[slot] : "";
                detail = "slot " + std::to_string(slot) + (locName.empty() ? "" : " (" + locName + ")");
                break;
            }
            case OpCode::GetParam: {
                uint8_t p = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                opName = "OP_GETPARAM";
                detail = "param " + std::to_string(p);
                break;
            }
            case OpCode::Add: opName = "OP_ADD"; break;
            case OpCode::Subtract: opName = "OP_SUBTRACT"; break;
            case OpCode::Multiply: opName = "OP_MULTIPLY"; break;
            case OpCode::Divide: opName = "OP_DIVIDE"; break;
            case OpCode::Modulo: opName = "OP_MODULO"; break;
            case OpCode::Power: opName = "OP_POWER"; break;
            case OpCode::Negate: opName = "OP_NEGATE"; break;
            case OpCode::Equal: opName = "OP_EQUAL"; break;
            case OpCode::NotEqual: opName = "OP_NOTEQUAL"; break;
            case OpCode::Less: opName = "OP_LESS"; break;
            case OpCode::LessEqual: opName = "OP_LESSEQUAL"; break;
            case OpCode::Greater: opName = "OP_GREATER"; break;
            case OpCode::GreaterEqual: opName = "OP_GREATEREQUAL"; break;
            case OpCode::MathSin: opName = "OP_MATHSIN"; break;
            case OpCode::MathCos: opName = "OP_MATHCOS"; break;
            case OpCode::MathTanh: opName = "OP_MATHTANH"; break;
            case OpCode::MathExp: opName = "OP_MATHEXP"; break;
            case OpCode::MathFloor: opName = "OP_MATHFLOOR"; break;
            case OpCode::MathSqrt: opName = "OP_MATHSQRT"; break;
            case OpCode::MathRandom: opName = "OP_MATHRANDOM"; break;
            case OpCode::Jump: {
                uint8_t hi = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                uint8_t lo = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                opName = "OP_JUMP";
                detail = "offset " + std::to_string((hi << 8) | lo);
                break;
            }
            case OpCode::JumpIfFalse: {
                uint8_t hi = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                uint8_t lo = (ip < chunk.code.size()) ? chunk.code[ip++] : 0;
                opName = "OP_JUMPIFFALSE";
                detail = "offset " + std::to_string((hi << 8) | lo);
                break;
            }
            case OpCode::Return: opName = "OP_RETURN"; break;
            default: opName = "OP_UNKNOWN (" + std::to_string(opByte) + ")"; break;
        }

        std::snprintf(lineBuf, sizeof(lineBuf), "  %04zu: [%04d] %-16s %s\n", offset, instNum++, opName.c_str(), detail.c_str());
        out += lineBuf;
    }
    return out;
}

} // namespace eatsbits::eatscript

#endif // EATS_BYTECODE_HPP
