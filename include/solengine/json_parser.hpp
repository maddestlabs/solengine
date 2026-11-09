#ifndef SOLENGINE_JSON_PARSER_HPP
#define SOLENGINE_JSON_PARSER_HPP

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

namespace solengine::json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    Type type{Type::Null};
    std::variant<std::monostate, bool, double, std::string, Array, Object> data;

    Value() = default;
    Value(std::nullptr_t) : type(Type::Null) {}
    Value(bool b) : type(Type::Bool), data(b) {}
    Value(int n) : type(Type::Number), data(static_cast<double>(n)) {}
    Value(unsigned int n) : type(Type::Number), data(static_cast<double>(n)) {}
    Value(int64_t n) : type(Type::Number), data(static_cast<double>(n)) {}
    Value(uint64_t n) : type(Type::Number), data(static_cast<double>(n)) {}
    Value(double n) : type(Type::Number), data(n) {}
    Value(float n) : type(Type::Number), data(static_cast<double>(n)) {}
    Value(const char* s) : type(Type::String), data(std::string(s ? s : "")) {}
    Value(std::string s) : type(Type::String), data(std::move(s)) {}
    Value(Array a) : type(Type::Array), data(std::move(a)) {}
    Value(Object o) : type(Type::Object), data(std::move(o)) {}

    [[nodiscard]] bool isObject() const noexcept { return type == Type::Object; }
    [[nodiscard]] bool isArray() const noexcept { return type == Type::Array; }
    [[nodiscard]] bool isString() const noexcept { return type == Type::String; }
    [[nodiscard]] bool isNumber() const noexcept { return type == Type::Number; }
    [[nodiscard]] bool isBool() const noexcept { return type == Type::Bool; }
    [[nodiscard]] bool isNull() const noexcept { return type == Type::Null; }

    [[nodiscard]] const Object& asObject() const { return std::get<Object>(data); }
    [[nodiscard]] Object& asObject() { return std::get<Object>(data); }
    [[nodiscard]] const Array& asArray() const { return std::get<Array>(data); }
    [[nodiscard]] Array& asArray() { return std::get<Array>(data); }

    [[nodiscard]] const std::string& asString() const {
        static const std::string empty;
        return isString() ? std::get<std::string>(data) : empty;
    }
    [[nodiscard]] double asDouble(double def = 0.0) const noexcept {
        return isNumber() ? std::get<double>(data) : def;
    }
    [[nodiscard]] float asFloat(float def = 0.0f) const noexcept {
        return isNumber() ? static_cast<float>(std::get<double>(data)) : def;
    }
    [[nodiscard]] int asInt(int def = 0) const noexcept {
        return isNumber() ? static_cast<int>(std::get<double>(data)) : def;
    }
    [[nodiscard]] uint32_t asUInt(uint32_t def = 0u) const noexcept {
        return isNumber() ? static_cast<uint32_t>(std::get<double>(data)) : def;
    }
    [[nodiscard]] bool asBool(bool def = false) const noexcept {
        return isBool() ? std::get<bool>(data) : def;
    }

    [[nodiscard]] bool contains(const std::string& key) const noexcept {
        if (!isObject()) return false;
        const auto& obj = asObject();
        return obj.find(key) != obj.end();
    }

    [[nodiscard]] const Value& operator[](const std::string& key) const {
        static const Value nullVal;
        if (!isObject()) return nullVal;
        const auto& obj = asObject();
        auto it = obj.find(key);
        return (it != obj.end()) ? it->second : nullVal;
    }

    [[nodiscard]] Value& operator[](const std::string& key) {
        if (!isObject()) {
            type = Type::Object;
            data = Object{};
        }
        return std::get<Object>(data)[key];
    }

    [[nodiscard]] const Value& operator[](size_t idx) const {
        static const Value nullVal;
        if (!isArray()) return nullVal;
        const auto& arr = asArray();
        return (idx < arr.size()) ? arr[idx] : nullVal;
    }
};

inline std::string escapeString(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c));
                } else {
                    o << c;
                }
        }
    }
    o << '"';
    return o.str();
}

inline std::string stringify(const Value& val, int indent = 0) {
    std::string ind(indent * 2, ' ');
    switch (val.type) {
        case Type::Null: return "null";
        case Type::Bool: return val.asBool() ? "true" : "false";
        case Type::Number: {
            std::ostringstream ss;
            double d = val.asDouble();
            if (d == static_cast<double>(static_cast<int64_t>(d))) {
                ss << static_cast<int64_t>(d);
            } else {
                ss << std::fixed << std::setprecision(4) << d;
            }
            return ss.str();
        }
        case Type::String: return escapeString(val.asString());
        case Type::Array: {
            const auto& arr = val.asArray();
            if (arr.empty()) return "[]";
            std::string s = "[\n";
            for (size_t i = 0; i < arr.size(); ++i) {
                s += ind + "  " + stringify(arr[i], indent + 1);
                if (i + 1 < arr.size()) s += ",";
                s += "\n";
            }
            s += ind + "]";
            return s;
        }
        case Type::Object: {
            const auto& obj = val.asObject();
            if (obj.empty()) return "{}";
            std::string s = "{\n";
            size_t i = 0;
            for (const auto& [k, v] : obj) {
                s += ind + "  " + escapeString(k) + ": " + stringify(v, indent + 1);
                if (++i < obj.size()) s += ",";
                s += "\n";
            }
            s += ind + "}";
            return s;
        }
    }
    return "null";
}

class Parser {
public:
    explicit Parser(std::string text) : src_(std::move(text)) {}

    Value parse() {
        skipWhitespace();
        return parseValue();
    }

private:
    void skipWhitespace() {
        while (pos_ < src_.size() && (std::isspace(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '\r' || src_[pos_] == '\n')) {
            pos_++;
        }
    }

    Value parseValue() {
        skipWhitespace();
        if (pos_ >= src_.size()) return Value{};

        char c = src_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

        pos_++;
        return Value{};
    }

    Value parseObject() {
        pos_++; // skip '{'
        Object obj;
        skipWhitespace();
        if (pos_ < src_.size() && src_[pos_] == '}') {
            pos_++;
            return Value{obj};
        }

        while (pos_ < src_.size()) {
            skipWhitespace();
            if (pos_ < src_.size() && src_[pos_] == '}') {
                pos_++;
                break;
            }
            if (src_[pos_] != '"') {
                pos_++;
                continue;
            }
            std::string key = parseString().asString();
            skipWhitespace();
            if (pos_ < src_.size() && src_[pos_] == ':') pos_++;
            skipWhitespace();
            Value val = parseValue();
            obj[std::move(key)] = std::move(val);

            skipWhitespace();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                pos_++;
            } else if (pos_ < src_.size() && src_[pos_] == '}') {
                pos_++;
                break;
            } else {
                if (pos_ < src_.size()) pos_++;
            }
        }
        return Value{obj};
    }

    Value parseArray() {
        pos_++; // skip '['
        Array arr;
        skipWhitespace();
        if (pos_ < src_.size() && src_[pos_] == ']') {
            pos_++;
            return Value{arr};
        }

        while (pos_ < src_.size()) {
            skipWhitespace();
            if (pos_ < src_.size() && src_[pos_] == ']') {
                pos_++;
                break;
            }
            arr.push_back(parseValue());
            skipWhitespace();
            if (pos_ < src_.size() && src_[pos_] == ',') {
                pos_++;
            } else if (pos_ < src_.size() && src_[pos_] == ']') {
                pos_++;
                break;
            } else {
                if (pos_ < src_.size()) pos_++;
            }
        }
        return Value{arr};
    }

    Value parseString() {
        pos_++; // skip opening '"'
        std::string s;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') break;
            if (c == '\\' && pos_ < src_.size()) {
                char esc = src_[pos_++];
                switch (esc) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 <= src_.size()) {
                            std::string hexStr = src_.substr(pos_, 4);
                            pos_ += 4;
                            try {
                                int code = std::stoi(hexStr, nullptr, 16);
                                if (code < 128) s += static_cast<char>(code);
                            } catch (...) {}
                        }
                        break;
                    }
                    default: s += esc; break;
                }
            } else {
                s += c;
            }
        }
        return Value{s};
    }

    Value parseNumber() {
        size_t start = pos_;
        if (pos_ < src_.size() && src_[pos_] == '-') pos_++;
        while (pos_ < src_.size() && (std::isdigit(static_cast<unsigned char>(src_[pos_])) ||
                                      src_[pos_] == '.' || src_[pos_] == 'e' ||
                                      src_[pos_] == 'E' || src_[pos_] == '+' ||
                                      src_[pos_] == '-')) {
            pos_++;
        }
        try {
            double num = std::stod(src_.substr(start, pos_ - start));
            return Value{num};
        } catch (...) {
            return Value{0.0};
        }
    }

    Value parseBool() {
        if (pos_ + 4 <= src_.size() && src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return Value{true};
        }
        if (pos_ + 5 <= src_.size() && src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return Value{false};
        }
        return Value{false};
    }

    Value parseNull() {
        if (pos_ + 4 <= src_.size() && src_.substr(pos_, 4) == "null") {
            pos_ += 4;
        }
        return Value{};
    }

    std::string src_;
    size_t pos_{0};
};

inline Value parse(std::string_view jsonStr) {
    Parser p{std::string(jsonStr)};
    return p.parse();
}

} // namespace solengine::json

#endif // SOLENGINE_JSON_PARSER_HPP

