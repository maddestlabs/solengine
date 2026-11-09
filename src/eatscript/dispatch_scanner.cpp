#include "eatsbits/eatscript/dispatch_scanner.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include "eatsbits/eatscript/ast.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace eatsbits::eatscript {

static inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n\"'");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(start, end - start + 1);
}

static inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<ClapExportParam> DependencyScanResult::generateClapParamDefs() const {
    std::vector<ClapExportParam> defs;
    defs.reserve(parameters.size());

    uint32_t id = 0;
    for (const auto& p : parameters) {
        ClapExportParam cp;
        cp.id = id++;
        cp.name = p.name;
        cp.minValue = static_cast<double>(p.minVal);
        cp.maxValue = static_cast<double>(p.maxVal);
        cp.defaultValue = static_cast<double>(p.defaultVal);
        cp.flags = 0x20; // CLAP_PARAM_IS_AUTOMATABLE

        const std::string lower = toLower(p.name);
        if (lower.find("cutoff") != std::string::npos ||
            lower.find("reso") != std::string::npos ||
            lower.find("filter") != std::string::npos) {
            cp.module = "Filter";
        } else if (lower.find("decay") != std::string::npos ||
                   lower.find("attack") != std::string::npos ||
                   lower.find("sustain") != std::string::npos ||
                   lower.find("release") != std::string::npos ||
                   lower.find("env") != std::string::npos) {
            cp.module = "Envelope";
        } else if (lower.find("drive") != std::string::npos ||
                   lower.find("dist") != std::string::npos ||
                   lower.find("overdrive") != std::string::npos) {
            cp.module = "Distortion";
        } else if (lower.find("wave") != std::string::npos ||
                   lower.find("osc") != std::string::npos ||
                   lower.find("tune") != std::string::npos ||
                   lower.find("pitch") != std::string::npos) {
            cp.module = "Oscillator";
            if (lower.find("wave") != std::string::npos) {
                // Stepped enum parameter for waveforms
                cp.flags |= (1 << 0) | (1 << 16); // CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM
            }
        } else if (lower.find("vol") != std::string::npos ||
                   lower.find("gain") != std::string::npos ||
                   lower.find("master") != std::string::npos ||
                   lower.find("pan") != std::string::npos) {
            cp.module = "Output";
        } else {
            cp.module = "General";
        }

        defs.push_back(std::move(cp));
    }

    return defs;
}

static std::string getExprPath(const Expr* expr) {
    if (!expr) return "";
    if (expr->kind == ExprKind::Identifier) {
        return static_cast<const IdentifierExpr*>(expr)->name;
    }
    if (expr->kind == ExprKind::Member) {
        const auto* m = static_cast<const MemberExpr*>(expr);
        std::string base = getExprPath(m->object.get());
        if (!base.empty()) {
            return base + "." + m->member;
        }
        return m->member;
    }
    return "";
}

static void matchAndAddModule(const std::string& path, DependencyScanResult& result) {
    if (path.empty()) return;

    if (const auto* mod = ApiManifest::findModule(path)) {
        result.dependencies.addHeader(mod->cppHeader);
        for (const auto& s : mod->sources) {
            result.dependencies.addSource(s);
        }
        result.dependencies.addModule(mod->moduleKey);
        result.referencedSymbols.push_back(path);

        if (result.dispatchTarget == NativeDispatchTarget::None) {
            if (mod->moduleKey == "eats.dsp.TB303" || mod->moduleKey == "eats.dsp.DiodeLadder") {
                result.dispatchTarget = NativeDispatchTarget::TB303;
            } else if (mod->moduleKey == "eats.dsp.SID") {
                result.dispatchTarget = NativeDispatchTarget::SID;
            } else if (mod->moduleKey == "eats.dsp.DX7") {
                result.dispatchTarget = NativeDispatchTarget::DX7;
            } else if (mod->moduleKey == "eats.dsp.YM2612") {
                result.dispatchTarget = NativeDispatchTarget::YM2612;
            } else if (mod->moduleKey == "eats.dsp.PolySynth") {
                result.dispatchTarget = NativeDispatchTarget::PolySynth;
            } else if (mod->moduleKey == "eats.dsp.SNES") {
                result.dispatchTarget = NativeDispatchTarget::SNES;
            } else if (mod->moduleKey == "eats.fx.Convolver") {
                result.dispatchTarget = NativeDispatchTarget::Convolver;
            }
        }
    }
}

static void extractParamFromCall(const CallExpr* call, const std::string& fallbackName, DependencyScanResult& result) {
    std::string name = fallbackName;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    float defaultVal = 0.5f;

    if (!call->args.empty()) {
        size_t argIdx = 0;
        if (call->args[0]->kind == ExprKind::String) {
            name = static_cast<const StringLiteral*>(call->args[0].get())->value;
            argIdx = 1;
        }

        std::vector<float> numArgs;
        for (size_t i = argIdx; i < call->args.size(); ++i) {
            if (call->args[i]->kind == ExprKind::Number) {
                numArgs.push_back(static_cast<float>(static_cast<const NumberLiteral*>(call->args[i].get())->value));
            }
        }

        if (numArgs.size() == 1) {
            defaultVal = numArgs[0];
            minVal = 0.0f;
            maxVal = (defaultVal > 0.0f) ? defaultVal * 2.0f : 1.0f;
        } else if (numArgs.size() == 2) {
            minVal = numArgs[0];
            maxVal = numArgs[1];
            defaultVal = (minVal + maxVal) * 0.5f;
        } else if (numArgs.size() >= 3) {
            // Check if (default, min, max) or (min, max, default)
            if (numArgs[0] >= numArgs[1] && numArgs[0] <= numArgs[2]) {
                defaultVal = numArgs[0];
                minVal = numArgs[1];
                maxVal = numArgs[2];
            } else {
                minVal = numArgs[0];
                maxVal = numArgs[1];
                defaultVal = numArgs[2];
            }
        }
    }

    if (!name.empty()) {
        auto it = std::find_if(result.parameters.begin(), result.parameters.end(),
            [&](const ExtractedParam& ep) { return ep.name == name; });
        if (it == result.parameters.end()) {
            result.parameters.push_back(ExtractedParam{
                .name = name,
                .minVal = minVal,
                .maxVal = maxVal,
                .defaultVal = defaultVal
            });
        }
    }
}

static void scanExpr(const Expr* expr, DependencyScanResult& result);

static void scanStmt(const Stmt* stmt, DependencyScanResult& result) {
    if (!stmt) return;

    switch (stmt->kind) {
        case StmtKind::Expr: {
            const auto* es = static_cast<const ExprStmt*>(stmt);
            scanExpr(es->expression.get(), result);
            break;
        }
        case StmtKind::Assign: {
            const auto* as = static_cast<const AssignStmt*>(stmt);
            if (as->value) {
                if (as->value->kind == ExprKind::Call) {
                    const auto* call = static_cast<const CallExpr*>(as->value.get());
                    const std::string path = getExprPath(call->callee.get());
                    if (path == "eat.param" || path == "param" || path.rfind(".param") != std::string::npos) {
                        extractParamFromCall(call, as->variableName, result);
                    }
                }
                scanExpr(as->value.get(), result);
            }
            break;
        }
        case StmtKind::IndexAssign: {
            const auto* ia = static_cast<const IndexAssignStmt*>(stmt);
            if (ia->variableName == "params" && ia->index && ia->index->kind == ExprKind::String && ia->value) {
                const auto* strIdx = static_cast<const StringLiteral*>(ia->index.get());
                float val = 0.0f;
                if (ia->value->kind == ExprKind::Number) {
                    val = static_cast<float>(static_cast<const NumberLiteral*>(ia->value.get())->value);
                }
                auto it = std::find_if(result.parameters.begin(), result.parameters.end(),
                    [&](const ExtractedParam& ep) { return ep.name == strIdx->value; });
                if (it == result.parameters.end()) {
                    result.parameters.push_back(ExtractedParam{
                        .name = strIdx->value,
                        .minVal = 0.0f,
                        .maxVal = (val > 0.0f) ? val * 2.0f : 1.0f,
                        .defaultVal = val
                    });
                }
            }
            if (ia->index) scanExpr(ia->index.get(), result);
            if (ia->value) scanExpr(ia->value.get(), result);
            break;
        }
        case StmtKind::Return: {
            const auto* rs = static_cast<const ReturnStmt*>(stmt);
            if (rs->value) {
                if (rs->value->kind == ExprKind::Dict) {
                    const auto* dict = static_cast<const DictLiteral*>(rs->value.get());
                    for (const auto& entry : dict->entries) {
                        if (entry.value && entry.value->kind == ExprKind::Call) {
                            const auto* call = static_cast<const CallExpr*>(entry.value.get());
                            const std::string path = getExprPath(call->callee.get());
                            if (path == "eat.param" || path == "param" || path.rfind(".param") != std::string::npos) {
                                extractParamFromCall(call, entry.key, result);
                            }
                        } else if (entry.value && entry.value->kind == ExprKind::Number) {
                            const float val = static_cast<float>(static_cast<const NumberLiteral*>(entry.value.get())->value);
                            auto it = std::find_if(result.parameters.begin(), result.parameters.end(),
                                [&](const ExtractedParam& ep) { return ep.name == entry.key; });
                            if (it == result.parameters.end()) {
                                result.parameters.push_back(ExtractedParam{
                                    .name = entry.key,
                                    .minVal = 0.0f,
                                    .maxVal = (val > 0.0f) ? val * 2.0f : 1.0f,
                                    .defaultVal = val
                                });
                            }
                        }
                    }
                }
                scanExpr(rs->value.get(), result);
            }
            break;
        }
        case StmtKind::If: {
            const auto* is = static_cast<const IfStmt*>(stmt);
            scanExpr(is->condition.get(), result);
            for (const auto& s : is->thenBranch) scanStmt(s.get(), result);
            for (const auto& s : is->elseBranch) scanStmt(s.get(), result);
            break;
        }
        case StmtKind::FunctionDef: {
            const auto* fs = static_cast<const FunctionDef*>(stmt);
            for (const auto& s : fs->body) {
                scanStmt(s.get(), result);
            }
            break;
        }
    }
}

static void scanExpr(const Expr* expr, DependencyScanResult& result) {
    if (!expr) return;

    switch (expr->kind) {
        case ExprKind::Call: {
            const auto* call = static_cast<const CallExpr*>(expr);
            const std::string path = getExprPath(call->callee.get());
            if (path == "eat.param" || path == "param" || path.rfind(".param") != std::string::npos) {
                extractParamFromCall(call, "", result);
            } else {
                matchAndAddModule(path, result);
            }
            scanExpr(call->callee.get(), result);
            for (const auto& a : call->args) {
                scanExpr(a.get(), result);
            }
            break;
        }
        case ExprKind::Member: {
            const auto* m = static_cast<const MemberExpr*>(expr);
            const std::string path = getExprPath(expr);
            matchAndAddModule(path, result);
            scanExpr(m->object.get(), result);
            break;
        }
        case ExprKind::Identifier: {
            const auto* id = static_cast<const IdentifierExpr*>(expr);
            matchAndAddModule(id->name, result);
            break;
        }
        case ExprKind::Binary: {
            const auto* b = static_cast<const BinaryExpr*>(expr);
            scanExpr(b->left.get(), result);
            scanExpr(b->right.get(), result);
            break;
        }
        case ExprKind::Unary: {
            const auto* u = static_cast<const UnaryExpr*>(expr);
            scanExpr(u->operand.get(), result);
            break;
        }
        case ExprKind::List: {
            const auto* l = static_cast<const ListLiteral*>(expr);
            for (const auto& el : l->elements) {
                scanExpr(el.get(), result);
            }
            break;
        }
        case ExprKind::Dict: {
            const auto* d = static_cast<const DictLiteral*>(expr);
            for (const auto& entry : d->entries) {
                scanExpr(entry.value.get(), result);
            }
            break;
        }
        case ExprKind::Index: {
            const auto* idx = static_cast<const IndexExpr*>(expr);
            if (idx->object) scanExpr(idx->object.get(), result);
            if (idx->index) scanExpr(idx->index.get(), result);
            break;
        }
        default:
            break;
    }
}

void DependencyScanner::scanAST(const Program& program, DependencyScanResult& result) {
    for (const auto& stmt : program.statements) {
        scanStmt(stmt.get(), result);
    }
}

void DependencyScanner::scanDirectives(const std::string& scriptSource, DependencyScanResult& result) {
    std::stringstream ss(scriptSource);
    std::string line;

    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        if (trimmed.rfind("# @", 0) == 0 || trimmed.rfind("// @", 0) == 0) {
            size_t prefixLen = (trimmed[0] == '#') ? 3 : 4;
            auto colon = trimmed.find(':');
            if (colon != std::string::npos && colon >= prefixLen) {
                std::string directive = toLower(trim(trimmed.substr(prefixLen, colon - prefixLen)));
                std::string val = trim(trimmed.substr(colon + 1));

                if (directive == "id") {
                    result.metadata.id = val;
                } else if (directive == "name") {
                    result.metadata.name = val;
                } else if (directive == "vendor") {
                    result.metadata.vendor = val;
                } else if (directive == "version") {
                    result.metadata.version = val;
                } else if (directive == "desc" || directive == "description") {
                    result.metadata.description = val;
                } else if (directive == "category") {
                    result.metadata.category = val;
                } else if (directive == "engine") {
                    result.metadata.engine = val;
                    matchAndAddModule(val, result);
                }
            }
        }
    }
}

DependencyScanResult DependencyScanner::scan(const std::string& scriptSource) {
    DependencyScanResult result;

    // 1. Scan metadata directives
    scanDirectives(scriptSource, result);

    // 2. Tokenize and parse into AST
    try {
        Lexer lexer(scriptSource);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        if (program) {
            scanAST(*program, result);
        }
    } catch (...) {
        // Fallback gracefully on parsing errors
    }

    // 3. Fallback scan line-based parameters to guarantee backward compatibility
    auto legacyParams = extractParameters(scriptSource);
    for (const auto& [name, param] : legacyParams) {
        auto it = std::find_if(result.parameters.begin(), result.parameters.end(),
            [&](const ExtractedParam& ep) { return ep.name == name; });
        if (it == result.parameters.end()) {
            result.parameters.push_back(param);
        }
    }

    // 4. Fallback legacy dispatch target detection
    if (result.dispatchTarget == NativeDispatchTarget::None) {
        result.dispatchTarget = detectTarget(scriptSource);
        if (result.dispatchTarget != NativeDispatchTarget::None) {
            std::string targetKey;
            switch (result.dispatchTarget) {
                case NativeDispatchTarget::TB303: targetKey = "eats.dsp.TB303"; break;
                case NativeDispatchTarget::PolySynth: targetKey = "eats.dsp.PolySynth"; break;
                case NativeDispatchTarget::SID: targetKey = "eats.dsp.SID"; break;
                case NativeDispatchTarget::DX7: targetKey = "eats.dsp.DX7"; break;
                case NativeDispatchTarget::SNES: targetKey = "eats.dsp.SNES"; break;
                case NativeDispatchTarget::YM2612: targetKey = "eats.dsp.YM2612"; break;
                case NativeDispatchTarget::Convolver: targetKey = "eats.fx.Convolver"; break;
                default: break;
            }
            if (!targetKey.empty()) {
                matchAndAddModule(targetKey, result);
            }
        }
    }

    // 5. Preprocessor definitions
    result.dependencies.addDefinition("EATS_EXPORT_CLAP=1");

    return result;
}

NativeDispatchTarget DependencyScanner::detectTarget(const std::string& scriptSource) noexcept {
    std::stringstream ss(scriptSource);
    std::string line;

    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.substr(0, 2) == "--" || trimmed.substr(0, 2) == "//") {
            continue;
        }

        // Support Eatscript metadata header directives (e.g. # @id: eats_303 or # @engine: tb303)
        if (trimmed.rfind("# @", 0) == 0) {
            auto colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string directive = toLower(trim(trimmed.substr(3, colon - 3)));
                std::string val = toLower(trim(trimmed.substr(colon + 1)));
                if (directive == "engine" || directive == "id") {
                    if (val == "tb303" || val == "eats_303") return NativeDispatchTarget::TB303;
                    if (val == "sid" || val == "c64_sid_synth") return NativeDispatchTarget::SID;
                    if (val == "dx7" || val == "dx7_epiano") return NativeDispatchTarget::DX7;
                    if (val == "snes" || val == "snes_dsp") return NativeDispatchTarget::SNES;
                    if (val == "ym2612" || val == "genesis_ym2612") return NativeDispatchTarget::YM2612;
                    if (val == "drum_808" || val == "analog_808_kick") return NativeDispatchTarget::DrumKit808;
                    if (val == "drum_909" || val == "analog_909_snare") return NativeDispatchTarget::DrumKit909;
                    if (val == "piano_physical" || val == "concert_grand_piano") return NativeDispatchTarget::GrandPiano;
                    if (val == "upright_bass") return NativeDispatchTarget::UprightBass;
                    if (val == "spanish_guitar") return NativeDispatchTarget::SpanishGuitar;
                    if (val == "acoustic_steel_guitar") return NativeDispatchTarget::SteelGuitar;
                    if (val == "convolver" || val == "convolver_space") return NativeDispatchTarget::Convolver;
                    if (val == "delay" || val == "stereo_delay") return NativeDispatchTarget::StereoDelay;
                    if (val == "polysynth" || val == "polyblep") return NativeDispatchTarget::PolySynth;
                }
            }
            continue;
        }
        if (trimmed[0] == '#') continue;

        // Stop scanning top-level flags once inside function definitions
        if (trimmed.rfind("def ", 0) == 0 || trimmed.rfind("function ", 0) == 0) {
            break;
        }

        auto eqPos = trimmed.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = trim(trimmed.substr(0, eqPos));
        std::string val = toLower(trim(trimmed.substr(eqPos + 1)));

        bool isTrue = (val == "true" || val == "1" || val == "yes");
        if (!isTrue) continue;

        std::string lkey = toLower(key);
        if (lkey == "eats303" || lkey == "jc303" || lkey == "tb303") {
            return NativeDispatchTarget::TB303;
        }
        if (lkey == "sidsynth" || lkey == "sid" || lkey == "mos6581" || lkey == "mos8580" || lkey == "c64sid") {
            return NativeDispatchTarget::SID;
        }
        if (lkey == "dx7epiano" || lkey == "dx7" || lkey == "yamahadx7" || lkey == "msfa") {
            return NativeDispatchTarget::DX7;
        }
        if (lkey == "snesdsp" || lkey == "snes" || lkey == "spc700") {
            return NativeDispatchTarget::SNES;
        }
        if (lkey == "ym2612" || lkey == "opn2" || lkey == "genesisfm") {
            return NativeDispatchTarget::YM2612;
        }
        if (lkey == "analog808kick" || lkey == "procedural808" || lkey == "tr808") {
            return NativeDispatchTarget::DrumKit808;
        }
        if (lkey == "analog909snare" || lkey == "procedural909" || lkey == "tr909") {
            return NativeDispatchTarget::DrumKit909;
        }
        if (lkey == "concertgrandpiano" || lkey == "steinwaygrand" || lkey == "grandpiano") {
            return NativeDispatchTarget::GrandPiano;
        }
        if (lkey == "doublebass" || lkey == "uprightbass") {
            return NativeDispatchTarget::UprightBass;
        }
        if (lkey == "spanishguitar" || lkey == "classicalguitar") {
            return NativeDispatchTarget::SpanishGuitar;
        }
        if (lkey == "steelacousticguitar" || lkey == "steelguitar") {
            return NativeDispatchTarget::SteelGuitar;
        }
        if (lkey == "convolver" || lkey == "cabdesigner" || lkey == "impulseresponse") {
            return NativeDispatchTarget::Convolver;
        }
        if (lkey == "stereodelay" || lkey == "tapeecho") {
            return NativeDispatchTarget::StereoDelay;
        }
        if (lkey == "polysynth" || lkey == "polyblep") {
            return NativeDispatchTarget::PolySynth;
        }
    }

    return NativeDispatchTarget::None;
}

const char* DependencyScanner::targetToString(NativeDispatchTarget target) noexcept {
    switch (target) {
        case NativeDispatchTarget::TB303:         return "Roland TB-303 (Diode Ladder SIMD)";
        case NativeDispatchTarget::SID:           return "Commodore 64 SID (MOS 6581/8580)";
        case NativeDispatchTarget::DX7:           return "Yamaha DX7 6-Op FM Sound Engine";
        case NativeDispatchTarget::SNES:          return "Super Nintendo S-DSP (SPC700)";
        case NativeDispatchTarget::YM2612:        return "Sega Genesis YM2612 (OPN2)";
        case NativeDispatchTarget::DrumKit808:    return "Roland TR-808 Rhythm Composer";
        case NativeDispatchTarget::DrumKit909:    return "Roland TR-909 Rhythm Composer";
        case NativeDispatchTarget::GrandPiano:    return "Bank-Bensa Concert Grand Piano";
        case NativeDispatchTarget::UprightBass:   return "Upright Double Bass (Growl/Slap)";
        case NativeDispatchTarget::SpanishGuitar: return "Spanish Classical Nylon Guitar";
        case NativeDispatchTarget::SteelGuitar:   return "Steel Acoustic Guitar (Waveguide)";
        case NativeDispatchTarget::Convolver:     return "Real-Time Convolution Reverb & Cab";
        case NativeDispatchTarget::StereoDelay:   return "Stereo Tape Delay & Echo";
        case NativeDispatchTarget::PolySynth:     return "16-Voice PolyBLEP Multi-Wave Synth";
        case NativeDispatchTarget::None:          return "Bytecode Virtual Machine (Eatscript VM)";
    }
    return "Unknown Target";
}

std::map<std::string, ExtractedParam> DependencyScanner::extractParameters(const std::string& scriptSource) {
    std::map<std::string, ExtractedParam> params;
    std::stringstream ss(scriptSource);
    std::string line;
    bool inInit = false;

    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.rfind("def init", 0) == 0 || trimmed.rfind("function init", 0) == 0) {
            inInit = true;
            continue;
        }
        if (inInit && (trimmed.rfind("def ", 0) == 0 || trimmed.rfind("function ", 0) == 0)) {
            break;
        }
        if (!inInit) continue;

        // Check for params['Name'] = Value
        auto pOpen = trimmed.find("params[");
        if (pOpen != std::string::npos) {
            auto pClose = trimmed.find(']', pOpen);
            auto eqPos = trimmed.find('=', pClose);
            if (pClose != std::string::npos && eqPos != std::string::npos) {
                std::string key = trim(trimmed.substr(pOpen + 7, pClose - pOpen - 7));
                std::string valStr = trim(trimmed.substr(eqPos + 1));
                try {
                    float v = std::stof(valStr);
                    ExtractedParam ep;
                    ep.name = key;
                    ep.defaultVal = v;
                    ep.minVal = 0.0f;
                    ep.maxVal = (v != 0.0f) ? std::abs(v) * 2.0f : 1.0f;
                    params[key] = ep;
                } catch (...) {}
                continue;
            }
        }

        // Search for eat.param("Name", min, max, default) or Param.add("Name", min, max, default)
        auto paramPos = trimmed.find(".param(");
        if (paramPos == std::string::npos) {
            paramPos = trimmed.find(".add(");
        }
        if (paramPos != std::string::npos) {
            auto openParen = trimmed.find('(', paramPos);
            auto closeParen = trimmed.find(')', openParen);
            if (openParen != std::string::npos && closeParen != std::string::npos) {
                std::string argsStr = trimmed.substr(openParen + 1, closeParen - openParen - 1);
                std::stringstream argStream(argsStr);
                std::string pName, pMin, pMax, pDef;
                std::getline(argStream, pName, ',');
                std::getline(argStream, pMin, ',');
                std::getline(argStream, pMax, ',');
                std::getline(argStream, pDef, ',');

                pName = trim(pName);
                if (!pName.empty()) {
                    ExtractedParam ep;
                    ep.name = pName;
                    try { if (!pMin.empty()) ep.minVal = std::stof(trim(pMin)); } catch (...) {}
                    try { if (!pMax.empty()) ep.maxVal = std::stof(trim(pMax)); } catch (...) {}
                    try { if (!pDef.empty()) ep.defaultVal = std::stof(trim(pDef)); } catch (...) {}
                    params[ep.name] = ep;
                }
            }
        }
    }

    return params;
}

std::map<std::string, float> DependencyScanner::extractParamsFromInit(const std::string& scriptSource) {
    std::map<std::string, float> result;
    std::stringstream ss(scriptSource);
    std::string line;
    bool inInit = false;

    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.rfind("def init", 0) == 0 || trimmed.rfind("function init", 0) == 0) {
            inInit = true;
            continue;
        }
        if (inInit && (trimmed.rfind("def ", 0) == 0 || trimmed.rfind("function ", 0) == 0)) {
            break;
        }
        if (!inInit) continue;

        // Check for params['Name'] = Value or params["Name"] = Value
        auto pOpen = trimmed.find("params[");
        if (pOpen != std::string::npos) {
            auto pClose = trimmed.find(']', pOpen);
            auto eqPos = trimmed.find('=', pClose);
            if (pClose != std::string::npos && eqPos != std::string::npos) {
                std::string key = trim(trimmed.substr(pOpen + 7, pClose - pOpen - 7));
                std::string valStr = trim(trimmed.substr(eqPos + 1));
                try {
                    result[key] = std::stof(valStr);
                } catch (...) {}
                continue;
            }
        }

        // Check for "Name": eat.param("Name", min, max, default) or "Name": 123.4
        auto colonPos = trimmed.find(':');
        if (colonPos != std::string::npos) {
            std::string key = trim(trimmed.substr(0, colonPos));
            std::string rhs = trim(trimmed.substr(colonPos + 1));
            if (!key.empty() && !rhs.empty()) {
                auto paramPos = rhs.find(".param(");
                if (paramPos != std::string::npos) {
                    auto openP = rhs.find('(', paramPos);
                    auto closeP = rhs.find(')', openP);
                    if (openP != std::string::npos && closeP != std::string::npos) {
                        std::string args = rhs.substr(openP + 1, closeP - openP - 1);
                        std::stringstream as(args);
                        std::string n, minV, maxV, defV;
                        std::getline(as, n, ',');
                        std::getline(as, minV, ',');
                        std::getline(as, maxV, ',');
                        std::getline(as, defV, ',');
                        try {
                            if (!defV.empty()) result[key] = std::stof(trim(defV));
                            else if (!minV.empty()) result[key] = std::stof(trim(minV));
                        } catch (...) {}
                    }
                } else {
                    if (rhs.back() == ',') rhs.pop_back();
                    try {
                        result[key] = std::stof(trim(rhs));
                    } catch (...) {}
                }
            }
        }
    }
    return result;
}

} // namespace eatsbits::eatscript
