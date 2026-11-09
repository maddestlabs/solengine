#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include <cmath>
#include <numbers>
#include <sstream>
#include <random>
#include <iostream>

namespace eatsbits::eatscript {

std::string Value::toString() const {
    switch (type) {
        case Type::Nil: return "None";
        case Type::Number: {
            std::ostringstream ss;
            ss << numberVal;
            return ss.str();
        }
        case Type::String: return stringVal;
        case Type::Boolean: return boolVal ? "True" : "False";
        case Type::List: {
            std::string out = "[";
            for (size_t i = 0; i < listVal.size(); ++i) {
                if (i > 0) out += ", ";
                out += listVal[i].toString();
            }
            out += "]";
            return out;
        }
        case Type::Dict: {
            std::string out = "{";
            bool first = true;
            for (const auto& [k, v] : dictVal) {
                if (!first) out += ", ";
                first = false;
                out += "\"" + k + "\": " + v.toString();
            }
            out += "}";
            return out;
        }
        case Type::Function: return "<function>";
    }
    return "";
}

Evaluator::Evaluator() {
    registerBuiltins();
}

void Evaluator::registerBuiltins() {
    // Constants
    globals_["pi"] = Value(std::numbers::pi_v<double>);
    globals_["True"] = Value(true);
    globals_["False"] = Value(false);
    globals_["None"] = Value();

    // Math object namespace
    std::map<std::string, Value> mathDict;
    mathDict["pi"] = Value(std::numbers::pi_v<double>);
    globals_["math"] = Value(mathDict);

    registerMemberFunction("math", "sin", [](const std::vector<Value>& args, const auto&) {
        return Value(std::sin(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerMemberFunction("math", "cos", [](const std::vector<Value>& args, const auto&) {
        return Value(std::cos(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerMemberFunction("math", "tanh", [](const std::vector<Value>& args, const auto&) {
        return Value(std::tanh(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerMemberFunction("math", "floor", [](const std::vector<Value>& args, const auto&) {
        return Value(std::floor(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerMemberFunction("math", "sqrt", [](const std::vector<Value>& args, const auto&) {
        return Value(std::sqrt(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerMemberFunction("math", "exp", [](const std::vector<Value>& args, const auto&) {
        return Value(std::exp(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerMemberFunction("math", "random", [](const auto&, const auto&) {
        static std::mt19937 rng(42);
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        return Value(dist(rng));
    });

    // Global Math Helpers
    registerFunction("sin", [](const auto& args, const auto&) {
        return Value(std::sin(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerFunction("cos", [](const auto& args, const auto&) {
        return Value(std::cos(args.empty() ? 0.0 : args[0].asNumber()));
    });
    registerFunction("tanh", [](const auto& args, const auto&) {
        return Value(std::tanh(args.empty() ? 0.0 : args[0].asNumber()));
    });

    // eat.param(...) constructor
    auto eatParamFn = [](const std::vector<Value>& args, const std::map<std::string, Value>& kwargs) -> Value {
        std::string name = resolveArg(args, kwargs, 0, "name", Value("Param")).asString();
        double minVal = resolveArg(args, kwargs, 1, "min", Value(0.0)).asNumber();
        double maxVal = resolveArg(args, kwargs, 2, "max", Value(1.0)).asNumber();
        double defVal = resolveArg(args, kwargs, 3, "default", Value(minVal)).asNumber();
        double step = resolveArg(args, kwargs, 4, "step", Value(0.0)).asNumber();
        std::string unit = resolveArg(args, kwargs, 5, "unit", Value("")).asString();
        bool allowVar = resolveArg(args, kwargs, 6, "allow_variance", Value(true)).asBoolean(true);

        std::map<std::string, Value> p;
        p["name"] = Value(name);
        p["min"] = Value(minVal);
        p["max"] = Value(maxVal);
        p["default"] = Value(defVal);
        p["step"] = Value(step);
        p["unit"] = Value(unit);
        p["allow_variance"] = Value(allowVar);
        return Value(p);
    };

    registerMemberFunction("eat", "param", eatParamFn);
    registerFunction("param", eatParamFn);

    // Initial namespace objects
    globals_["eat"] = Value(std::map<std::string, Value>{});
    globals_["project"] = Value(std::map<std::string, Value>{});
}

void Evaluator::setVariable(const std::string& name, Value val) {
    globals_[name] = std::move(val);
}

Value Evaluator::getVariable(const std::string& name) const {
    auto it = globals_.find(name);
    if (it != globals_.end()) return it->second;
    return Value();
}

bool Evaluator::hasVariable(const std::string& name) const {
    return globals_.find(name) != globals_.end();
}

void Evaluator::registerFunction(const std::string& name, HostFunction fn) {
    globals_[name] = Value(std::move(fn));
}

void Evaluator::registerMemberFunction(const std::string& objectName, const std::string& methodName, HostFunction fn) {
    memberFunctions_[objectName][methodName] = std::move(fn);
}

Value Evaluator::evaluate(const Program& program) {
    returning_ = false;
    returnValue_ = Value();
    lastError_.clear();

    Value lastVal;
    try {
        for (const auto& stmt : program.statements) {
            lastVal = evalStmt(stmt.get());
            if (returning_) {
                returning_ = false;
                return returnValue_;
            }
        }
    } catch (const std::exception& e) {
        lastError_ = e.what();
    }
    return lastVal;
}

Value Evaluator::evaluateSource(const std::string& source) {
    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        auto prog = parser.parse();
        if (!prog) {
            lastError_ = "Failed to parse program";
            return Value();
        }
        return evaluate(*prog);
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return Value();
    }
}

bool Evaluator::hasFunction(const std::string& funcName) const {
    auto it = globals_.find(funcName);
    return it != globals_.end() && it->second.isFunction();
}

Value Evaluator::callFunction(const std::string& funcName,
                              const std::vector<Value>& args,
                              const std::map<std::string, Value>& kwargs) {
    auto it = globals_.find(funcName);
    if (it == globals_.end() || !it->second.isFunction()) {
        lastError_ = "Function not found: " + funcName;
        return Value();
    }

    return invokeCallable(it->second, args, kwargs);
}

Value Evaluator::invokeCallable(const Value& callable,
                                const std::vector<Value>& args,
                                const std::map<std::string, Value>& kwargs) {
    if (callable.hostFn) {
        return callable.hostFn(args, kwargs);
    }

    if (callable.funcDef) {
        auto* def = callable.funcDef.get();
        // Save current variable state for parameters
        std::map<std::string, Value> savedLocals;
        for (size_t i = 0; i < def->params.size(); ++i) {
            const std::string& pName = def->params[i];
            if (hasVariable(pName)) {
                savedLocals[pName] = getVariable(pName);
            }
            Value pVal = resolveArg(args, kwargs, i, pName, Value());
            setVariable(pName, pVal);
        }

        returning_ = false;
        returnValue_ = Value();
        for (const auto& stmt : def->body) {
            evalStmt(stmt.get());
            if (returning_) {
                break;
            }
        }
        returning_ = false;
        Value result = returnValue_;

        // Restore locals
        for (const auto& pName : def->params) {
            auto sIt = savedLocals.find(pName);
            if (sIt != savedLocals.end()) {
                setVariable(pName, sIt->second);
            } else {
                globals_.erase(pName);
            }
        }
        return result;
    }

    return Value();
}

Value Evaluator::evalStmt(Stmt* stmt) {
    if (!stmt) return Value();

    if (auto* e = ast_cast<ExprStmt>(stmt)) {
        return evalExpr(e->expression.get());
    }
    if (auto* a = ast_cast<AssignStmt>(stmt)) {
        Value val = evalExpr(a->value.get());
        setVariable(a->variableName, val);
        return val;
    }
    if (auto* ia = ast_cast<IndexAssignStmt>(stmt)) {
        Value idx = evalExpr(ia->index.get());
        Value val = evalExpr(ia->value.get());
        Value var = getVariable(ia->variableName);
        if (var.isDict()) {
            var.dictVal[idx.toString()] = val;
            setVariable(ia->variableName, var);
        } else if (var.isList()) {
            size_t i = static_cast<size_t>(idx.asNumber());
            if (i < var.listVal.size()) {
                var.listVal[i] = val;
                setVariable(ia->variableName, var);
            }
        } else {
            var = Value(std::map<std::string, Value>{{idx.toString(), val}});
            setVariable(ia->variableName, var);
        }
        return val;
    }
    if (auto* r = ast_cast<ReturnStmt>(stmt)) {
        returnValue_ = r->value ? evalExpr(r->value.get()) : Value();
        returning_ = true;
        return returnValue_;
    }
    if (auto* ifs = ast_cast<IfStmt>(stmt)) {
        Value cond = evalExpr(ifs->condition.get());
        if (cond.asBoolean()) {
            for (const auto& s : ifs->thenBranch) {
                evalStmt(s.get());
                if (returning_) return returnValue_;
            }
        } else {
            for (const auto& s : ifs->elseBranch) {
                evalStmt(s.get());
                if (returning_) return returnValue_;
            }
        }
        return Value();
    }
    if (auto* f = ast_cast<FunctionDef>(stmt)) {
        auto sharedDef = std::make_shared<FunctionDef>(f->name, f->params, f->body);
        setVariable(f->name, Value(sharedDef));
        return Value();
    }

    return Value();
}

Value Evaluator::evalExpr(Expr* expr) {
    if (!expr) return Value();

    if (auto* num = ast_cast<NumberLiteral>(expr)) {
        return Value(num->value);
    }
    if (auto* str = ast_cast<StringLiteral>(expr)) {
        return Value(str->value);
    }
    if (auto* b = ast_cast<BooleanLiteral>(expr)) {
        return Value(b->value);
    }
    if (auto* id = ast_cast<IdentifierExpr>(expr)) {
        return getVariable(id->name);
    }
    if (auto* list = ast_cast<ListLiteral>(expr)) {
        std::vector<Value> elements;
        elements.reserve(list->elements.size());
        for (const auto& elem : list->elements) {
            elements.push_back(evalExpr(elem.get()));
        }
        return Value(elements);
    }
    if (auto* dict = ast_cast<DictLiteral>(expr)) {
        std::map<std::string, Value> entries;
        for (const auto& entry : dict->entries) {
            entries[entry.key] = evalExpr(entry.value.get());
        }
        return Value(entries);
    }
    if (auto* mem = ast_cast<MemberExpr>(expr)) {
        // Namespace or property lookup: e.g. eat.daw, math.pi, dict["key"]
        if (auto* baseId = ast_cast<IdentifierExpr>(mem->object.get())) {
            // Check member functions first: e.g. math.sin
            auto objIt = memberFunctions_.find(baseId->name);
            if (objIt != memberFunctions_.end()) {
                auto methodIt = objIt->second.find(mem->member);
                if (methodIt != objIt->second.end()) {
                    return Value(methodIt->second);
                }
            }
        }

        Value obj = evalExpr(mem->object.get());
        if (obj.isDict()) {
            return obj.get(mem->member);
        }
        return Value();
    }
    if (auto* bin = ast_cast<BinaryExpr>(expr)) {
        Value left = evalExpr(bin->left.get());
        Value right = evalExpr(bin->right.get());

        switch (bin->op) {
            case TokenType::Plus:
                if (left.isString() || right.isString()) {
                    return Value(left.toString() + right.toString());
                }
                return Value(left.asNumber() + right.asNumber());
            case TokenType::Minus:
                return Value(left.asNumber() - right.asNumber());
            case TokenType::Multiply:
                return Value(left.asNumber() * right.asNumber());
            case TokenType::Divide: {
                double denom = right.asNumber();
                return Value(denom != 0.0 ? left.asNumber() / denom : 0.0);
            }
            case TokenType::Modulo: {
                double denom = right.asNumber();
                return Value(denom != 0.0 ? std::fmod(left.asNumber(), denom) : 0.0);
            }
            case TokenType::Power:
                return Value(std::pow(left.asNumber(), right.asNumber()));
            case TokenType::Equals:
                if (left.isNumber() && right.isNumber()) return Value(left.numberVal == right.numberVal);
                if (left.isString() && right.isString()) return Value(left.stringVal == right.stringVal);
                if (left.isBoolean() && right.isBoolean()) return Value(left.boolVal == right.boolVal);
                return Value(false);
            case TokenType::NotEquals:
                if (left.isNumber() && right.isNumber()) return Value(left.numberVal != right.numberVal);
                if (left.isString() && right.isString()) return Value(left.stringVal != right.stringVal);
                if (left.isBoolean() && right.isBoolean()) return Value(left.boolVal != right.boolVal);
                return Value(true);
            case TokenType::Less:
                return Value(left.asNumber() < right.asNumber());
            case TokenType::LessEqual:
                return Value(left.asNumber() <= right.asNumber());
            case TokenType::Greater:
                return Value(left.asNumber() > right.asNumber());
            case TokenType::GreaterEqual:
                return Value(left.asNumber() >= right.asNumber());
            case TokenType::And:
                return Value(left.asBoolean() && right.asBoolean());
            case TokenType::Or:
                return Value(left.asBoolean() || right.asBoolean());
            default:
                break;
        }
        return Value();
    }
    if (auto* un = ast_cast<UnaryExpr>(expr)) {
        Value op = evalExpr(un->operand.get());
        if (un->op == TokenType::Minus) return Value(-op.asNumber());
        if (un->op == TokenType::Not) return Value(!op.asBoolean());
        return op;
    }
    if (auto* call = ast_cast<CallExpr>(expr)) {
        return evalCall(call);
    }
    if (auto* idxExpr = ast_cast<IndexExpr>(expr)) {
        Value obj = evalExpr(idxExpr->object.get());
        Value idx = evalExpr(idxExpr->index.get());
        if (obj.isDict()) {
            return obj.get(idx.toString());
        }
        if (obj.isList()) {
            size_t i = static_cast<size_t>(idx.asNumber());
            if (i < obj.listVal.size()) {
                return obj.listVal[i];
            }
            return Value();
        }
        return Value();
    }

    return Value();
}

Value Evaluator::evalCall(CallExpr* call) {
    if (!call) return Value();

    // Evaluate arguments and map kwargs
    std::vector<Value> args;
    std::map<std::string, Value> kwargs;

    for (size_t i = 0; i < call->args.size(); ++i) {
        Value v = evalExpr(call->args[i].get());
        if (i < call->argNames.size() && !call->argNames[i].empty()) {
            kwargs[call->argNames[i]] = v;
        } else {
            args.push_back(v);
        }
    }

    // Special check for member method calls: e.g. eat.daw.set_tempo(...) or project.log(...)
    if (auto* mem = ast_cast<MemberExpr>(call->callee.get())) {
        std::string methodName = mem->member;

        // Check if object is a nested member like eat.daw
        if (auto* parentMem = ast_cast<MemberExpr>(mem->object.get())) {
            if (auto* rootId = ast_cast<IdentifierExpr>(parentMem->object.get())) {
                std::string fullObj = rootId->name + "." + parentMem->member; // e.g. "eat.daw"
                auto objIt = memberFunctions_.find(fullObj);
                if (objIt != memberFunctions_.end()) {
                    auto methodIt = objIt->second.find(methodName);
                    if (methodIt != objIt->second.end()) {
                        return methodIt->second(args, kwargs);
                    }
                }
            }
        }

        // Single object member: e.g. project.set_tempo or eat.param
        if (auto* rootId = ast_cast<IdentifierExpr>(mem->object.get())) {
            auto objIt = memberFunctions_.find(rootId->name);
            if (objIt != memberFunctions_.end()) {
                auto methodIt = objIt->second.find(methodName);
                if (methodIt != objIt->second.end()) {
                    return methodIt->second(args, kwargs);
                }
            }
        }
    }

    // Otherwise evaluate callee to a callable Value
    Value fnVal = evalExpr(call->callee.get());
    return invokeCallable(fnVal, args, kwargs);

    return Value();
}

} // namespace eatsbits::eatscript
