#ifndef EATS_EVALUATOR_HPP
#define EATS_EVALUATOR_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "ast.hpp"

namespace eatsbits::eatscript {

struct Value;

using HostFunction = std::function<Value(const std::vector<Value>& args,
                                         const std::map<std::string, Value>& kwargs)>;

/**
 * Dynamically-typed Value representation for Eatscript control-plane evaluation.
 * Used for presets, GUI definitions, tracker note structures, and DAW macros.
 */
struct Value {
    enum class Type {
        Nil,
        Number,
        String,
        Boolean,
        List,
        Dict,
        Function
    };

    Type type{Type::Nil};
    double numberVal{0.0};
    std::string stringVal;
    bool boolVal{false};
    std::vector<Value> listVal;
    std::map<std::string, Value> dictVal;
    HostFunction hostFn;
    std::shared_ptr<FunctionDef> funcDef;

    Value() noexcept : type(Type::Nil), numberVal(0.0), boolVal(false) {}
    explicit Value(double num) : type(Type::Number), numberVal(num) {}
    explicit Value(int num) : type(Type::Number), numberVal(static_cast<double>(num)) {}
    explicit Value(uint32_t num) : type(Type::Number), numberVal(static_cast<double>(num)) {}
    explicit Value(std::string str) : type(Type::String), stringVal(std::move(str)) {}
    explicit Value(const char* str) : type(Type::String), stringVal(str) {}
    explicit Value(bool b) : type(Type::Boolean), boolVal(b) {}
    explicit Value(std::vector<Value> list) : type(Type::List), listVal(std::move(list)) {}
    explicit Value(std::map<std::string, Value> dict) : type(Type::Dict), dictVal(std::move(dict)) {}
    explicit Value(HostFunction fn) : type(Type::Function), hostFn(std::move(fn)) {}
    explicit Value(std::shared_ptr<FunctionDef> fn) : type(Type::Function), funcDef(std::move(fn)) {}

    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    [[nodiscard]] bool isNil() const noexcept { return type == Type::Nil; }
    [[nodiscard]] bool isNumber() const noexcept { return type == Type::Number; }
    [[nodiscard]] bool isString() const noexcept { return type == Type::String; }
    [[nodiscard]] bool isBoolean() const noexcept { return type == Type::Boolean; }
    [[nodiscard]] bool isList() const noexcept { return type == Type::List; }
    [[nodiscard]] bool isDict() const noexcept { return type == Type::Dict; }
    [[nodiscard]] bool isFunction() const noexcept { return type == Type::Function; }

    [[nodiscard]] double asNumber(double fallback = 0.0) const noexcept {
        if (isNumber()) return numberVal;
        if (isBoolean()) return boolVal ? 1.0 : 0.0;
        return fallback;
    }

    [[nodiscard]] float asFloat(float fallback = 0.0f) const noexcept {
        return static_cast<float>(asNumber(fallback));
    }

    [[nodiscard]] int asInt(int fallback = 0) const noexcept {
        return static_cast<int>(asNumber(fallback));
    }

    [[nodiscard]] const std::string& asString() const noexcept {
        static const std::string empty;
        return isString() ? stringVal : empty;
    }

    [[nodiscard]] bool asBoolean(bool fallback = false) const noexcept {
        if (isBoolean()) return boolVal;
        if (isNumber()) return numberVal != 0.0;
        if (isString()) return !stringVal.empty();
        return fallback;
    }

    [[nodiscard]] std::string toString() const;

    [[nodiscard]] bool hasKey(const std::string& key) const {
        return isDict() && dictVal.find(key) != dictVal.end();
    }

    [[nodiscard]] Value get(const std::string& key) const {
        return get(key, Value{});
    }

    [[nodiscard]] Value get(const std::string& key, const Value& fallback) const {
        if (isDict()) {
            auto it = dictVal.find(key);
            if (it != dictVal.end()) return it->second;
        }
        return fallback;
    }
};

/**
 * Helper to resolve positional or keyword arguments from CallExpr execution.
 */
inline Value resolveArg(const std::vector<Value>& args,
                        const std::map<std::string, Value>& kwargs,
                        size_t pos,
                        const std::string& name,
                        Value fallback = Value()) {
    auto it = kwargs.find(name);
    if (it != kwargs.end()) return it->second;
    if (pos < args.size()) return args[pos];
    return fallback;
}

/**
 * Eatscript AST Evaluator for Control Plane operations (presets, GUI metadata, macros).
 * Completely safe for UI and background threads.
 */
class Evaluator {
public:
    Evaluator();

    // Variable and Function Registration
    void setVariable(const std::string& name, Value val);
    [[nodiscard]] Value getVariable(const std::string& name) const;
    [[nodiscard]] bool hasVariable(const std::string& name) const;

    void registerFunction(const std::string& name, HostFunction fn);
    void registerMemberFunction(const std::string& objectName, const std::string& methodName, HostFunction fn);

    // Evaluation
    Value evaluate(const Program& program);
    Value evaluateSource(const std::string& source);

    [[nodiscard]] bool hasFunction(const std::string& funcName) const;
    Value callFunction(const std::string& funcName,
                       const std::vector<Value>& args = {},
                       const std::map<std::string, Value>& kwargs = {});
    Value invokeCallable(const Value& callable,
                         const std::vector<Value>& args = {},
                         const std::map<std::string, Value>& kwargs = {});

    // Logging
    void addLog(std::string msg) { logs_.push_back(std::move(msg)); }
    [[nodiscard]] const std::vector<std::string>& getLogs() const noexcept { return logs_; }
    void clearLogs() noexcept { logs_.clear(); }

    [[nodiscard]] const std::string& getLastError() const noexcept { return lastError_; }

private:
    Value evalStmt(Stmt* stmt);
    Value evalExpr(Expr* expr);
    Value evalCall(CallExpr* call);

    void registerBuiltins();

    std::map<std::string, Value> globals_;
    std::map<std::string, std::map<std::string, HostFunction>> memberFunctions_;
    std::vector<std::string> logs_;
    std::string lastError_;
    bool returning_{false};
    Value returnValue_;
};

} // namespace eatsbits::eatscript

#endif // EATS_EVALUATOR_HPP
