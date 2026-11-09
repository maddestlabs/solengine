#include "eatsbits/eatscript/stdlib.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/abi/host_registry.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <numbers>

namespace eatsbits::eatscript {

void registerStandardLibrary(Evaluator& evaluator, abi::HostRegistry* hostRegistry) {
    // ==========================================
    // 1. Console I/O
    // ==========================================
    auto printFn = [&evaluator](const std::vector<Value>& args, const auto&) -> Value {
        std::ostringstream ss;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) ss << " ";
            ss << args[i].toString();
        }
        std::string out = ss.str();
        std::cout << out << std::endl;
        evaluator.addLog(out);
        return Value();
    };

    evaluator.registerFunction("print", printFn);
    evaluator.registerFunction("println", printFn);

    evaluator.setVariable("console", Value(std::map<std::string, Value>{}));
    evaluator.registerMemberFunction("console", "log", printFn);
    evaluator.registerMemberFunction("console", "info", printFn);

    evaluator.registerMemberFunction("console", "warn", [&evaluator](const std::vector<Value>& args, const auto&) -> Value {
        std::ostringstream ss;
        ss << "[WARN] ";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) ss << " ";
            ss << args[i].toString();
        }
        std::string out = ss.str();
        std::cerr << out << std::endl;
        evaluator.addLog(out);
        return Value();
    });

    evaluator.registerMemberFunction("console", "error", [&evaluator](const std::vector<Value>& args, const auto&) -> Value {
        std::ostringstream ss;
        ss << "[ERROR] ";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) ss << " ";
            ss << args[i].toString();
        }
        std::string out = ss.str();
        std::cerr << out << std::endl;
        evaluator.addLog(out);
        return Value();
    });

    // ==========================================
    // 2. Math Extensions
    // ==========================================
    auto mathAbs = [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::abs(args.empty() ? 0.0 : args[0].asNumber()));
    };
    evaluator.registerFunction("abs", mathAbs);
    evaluator.registerMemberFunction("math", "abs", mathAbs);

    auto mathMin = [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value(0.0);
        if (args.size() == 1) return args[0];
        double m = args[0].asNumber();
        for (size_t i = 1; i < args.size(); ++i) {
            m = std::min(m, args[i].asNumber());
        }
        return Value(m);
    };
    evaluator.registerFunction("min", mathMin);
    evaluator.registerMemberFunction("math", "min", mathMin);

    auto mathMax = [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value(0.0);
        if (args.size() == 1) return args[0];
        double m = args[0].asNumber();
        for (size_t i = 1; i < args.size(); ++i) {
            m = std::max(m, args[i].asNumber());
        }
        return Value(m);
    };
    evaluator.registerFunction("max", mathMax);
    evaluator.registerMemberFunction("math", "max", mathMax);

    evaluator.registerMemberFunction("math", "clamp", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 3) return args.empty() ? Value(0.0) : args[0];
        double val = args[0].asNumber();
        double low = args[1].asNumber();
        double high = args[2].asNumber();
        return Value(std::clamp(val, low, high));
    });

    evaluator.registerMemberFunction("math", "round", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::round(args.empty() ? 0.0 : args[0].asNumber()));
    });
    evaluator.registerMemberFunction("math", "ceil", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::ceil(args.empty() ? 0.0 : args[0].asNumber()));
    });
    evaluator.registerMemberFunction("math", "pow", [](const std::vector<Value>& args, const auto&) -> Value {
        double base = args.size() > 0 ? args[0].asNumber() : 0.0;
        double exp = args.size() > 1 ? args[1].asNumber() : 1.0;
        return Value(std::pow(base, exp));
    });
    evaluator.registerMemberFunction("math", "log", [](const std::vector<Value>& args, const auto&) -> Value {
        double v = args.empty() ? 1.0 : args[0].asNumber();
        return Value(v > 0.0 ? std::log(v) : -std::numeric_limits<double>::infinity());
    });
    evaluator.registerMemberFunction("math", "log10", [](const std::vector<Value>& args, const auto&) -> Value {
        double v = args.empty() ? 1.0 : args[0].asNumber();
        return Value(v > 0.0 ? std::log10(v) : -std::numeric_limits<double>::infinity());
    });
    evaluator.registerMemberFunction("math", "tan", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::tan(args.empty() ? 0.0 : args[0].asNumber()));
    });
    evaluator.registerMemberFunction("math", "asin", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::asin(std::clamp(args.empty() ? 0.0 : args[0].asNumber(), -1.0, 1.0)));
    });
    evaluator.registerMemberFunction("math", "acos", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::acos(std::clamp(args.empty() ? 0.0 : args[0].asNumber(), -1.0, 1.0)));
    });
    evaluator.registerMemberFunction("math", "atan", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(std::atan(args.empty() ? 0.0 : args[0].asNumber()));
    });
    evaluator.registerMemberFunction("math", "atan2", [](const std::vector<Value>& args, const auto&) -> Value {
        double y = args.size() > 0 ? args[0].asNumber() : 0.0;
        double x = args.size() > 1 ? args[1].asNumber() : 0.0;
        return Value(std::atan2(y, x));
    });
    evaluator.registerMemberFunction("math", "lerp", [](const std::vector<Value>& args, const auto&) -> Value {
        double a = args.size() > 0 ? args[0].asNumber() : 0.0;
        double b = args.size() > 1 ? args[1].asNumber() : 0.0;
        double t = args.size() > 2 ? args[2].asNumber() : 0.0;
        return Value(a + (b - a) * t);
    });

    // ==========================================
    // 3. String & Polymorphic Collections
    // ==========================================
    evaluator.registerFunction("str", [](const std::vector<Value>& args, const auto&) -> Value {
        return Value(args.empty() ? "" : args[0].toString());
    });

    auto lenFn = [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value(0.0);
        const auto& v = args[0];
        if (v.isString()) return Value(static_cast<double>(v.stringVal.length()));
        if (v.isList()) return Value(static_cast<double>(v.listVal.size()));
        if (v.isDict()) return Value(static_cast<double>(v.dictVal.size()));
        return Value(0.0);
    };
    evaluator.registerFunction("len", lenFn);

    evaluator.setVariable("string", Value(std::map<std::string, Value>{}));
    evaluator.registerMemberFunction("string", "len", lenFn);

    evaluator.registerMemberFunction("string", "upper", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value("");
        std::string s = args[0].asString();
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value(s);
    });

    evaluator.registerMemberFunction("string", "lower", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value("");
        std::string s = args[0].asString();
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Value(s);
    });

    evaluator.registerMemberFunction("string", "trim", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value("");
        std::string s = args[0].asString();
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return Value("");
        size_t end = s.find_last_not_of(" \t\r\n");
        return Value(s.substr(start, end - start + 1));
    });

    evaluator.registerMemberFunction("string", "starts_with", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 2) return Value(false);
        const std::string& s = args[0].asString();
        const std::string& prefix = args[1].asString();
        return Value(s.rfind(prefix, 0) == 0);
    });

    evaluator.registerMemberFunction("string", "ends_with", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 2) return Value(false);
        const std::string& s = args[0].asString();
        const std::string& suffix = args[1].asString();
        if (s.length() < suffix.length()) return Value(false);
        return Value(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0);
    });

    evaluator.registerMemberFunction("string", "contains", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 2) return Value(false);
        return Value(args[0].asString().find(args[1].asString()) != std::string::npos);
    });

    evaluator.registerMemberFunction("string", "find", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 2) return Value(-1.0);
        auto pos = args[0].asString().find(args[1].asString());
        return Value(pos != std::string::npos ? static_cast<double>(pos) : -1.0);
    });

    evaluator.registerMemberFunction("string", "replace", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 3) return args.empty() ? Value("") : args[0];
        std::string s = args[0].asString();
        const std::string& target = args[1].asString();
        const std::string& repl = args[2].asString();
        if (target.empty()) return Value(s);
        size_t pos = 0;
        while ((pos = s.find(target, pos)) != std::string::npos) {
            s.replace(pos, target.length(), repl);
            pos += repl.length();
        }
        return Value(s);
    });

    evaluator.registerMemberFunction("string", "split", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value(std::vector<Value>{});
        std::string s = args[0].asString();
        std::string delim = args.size() > 1 ? args[1].asString() : " ";
        std::vector<Value> tokens;
        if (delim.empty()) {
            for (char c : s) tokens.emplace_back(std::string(1, c));
            return Value(tokens);
        }
        size_t start = 0;
        size_t end = s.find(delim);
        while (end != std::string::npos) {
            tokens.emplace_back(s.substr(start, end - start));
            start = end + delim.length();
            end = s.find(delim, start);
        }
        tokens.emplace_back(s.substr(start));
        return Value(tokens);
    });

    evaluator.registerMemberFunction("string", "join", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 2) return Value("");
        std::string delim = args[0].asString();
        const auto& listVal = args[1].listVal;
        std::string result;
        for (size_t i = 0; i < listVal.size(); ++i) {
            if (i > 0) result += delim;
            result += listVal[i].toString();
        }
        return Value(result);
    });

    evaluator.registerMemberFunction("string", "substr", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value("");
        const std::string& s = args[0].asString();
        size_t start = args.size() > 1 ? static_cast<size_t>(std::max(0.0, args[1].asNumber())) : 0;
        if (start >= s.length()) return Value("");
        size_t count = args.size() > 2 ? static_cast<size_t>(std::max(0.0, args[2].asNumber())) : std::string::npos;
        return Value(s.substr(start, count));
    });

    // ==========================================
    // 4. System Utilities (sys)
    // ==========================================
    std::map<std::string, Value> sysDict;
    sysDict["version"] = Value("1.0.0");
#if defined(_WIN32)
    sysDict["platform"] = Value("windows");
#elif defined(__APPLE__)
    sysDict["platform"] = Value("darwin");
#elif defined(__EMSCRIPTEN__)
    sysDict["platform"] = Value("wasm");
#else
    sysDict["platform"] = Value("linux");
#endif
    evaluator.setVariable("sys", Value(sysDict));

    evaluator.registerMemberFunction("sys", "time", [](const auto&, const auto&) -> Value {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return Value(duration<double>(now).count());
    });

    evaluator.registerMemberFunction("sys", "clock", [](const auto&, const auto&) -> Value {
        using namespace std::chrono;
        auto now = steady_clock::now().time_since_epoch();
        return Value(duration<double>(now).count());
    });

    evaluator.registerMemberFunction("sys", "sleep", [](const std::vector<Value>& args, const auto&) -> Value {
        double sec = args.empty() ? 0.0 : args[0].asNumber();
        if (sec > 0.0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sec));
        }
        return Value(true);
    });

    evaluator.registerMemberFunction("sys", "getenv", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value("");
        const char* val = std::getenv(args[0].asString().c_str());
        if (val) return Value(std::string(val));
        return args.size() > 1 ? args[1] : Value("");
    });

    evaluator.registerMemberFunction("sys", "cwd", [](const auto&, const auto&) -> Value {
        std::error_code ec;
        auto p = std::filesystem::current_path(ec);
        return Value(ec ? "" : p.string());
    });

    // ==========================================
    // 5. File I/O (fs)
    // ==========================================
    evaluator.setVariable("fs", Value(std::map<std::string, Value>{}));

    evaluator.registerMemberFunction("fs", "exists", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value(false);
        std::error_code ec;
        return Value(std::filesystem::exists(args[0].asString(), ec));
    });

    evaluator.registerMemberFunction("fs", "file_size", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value(-1.0);
        std::error_code ec;
        auto size = std::filesystem::file_size(args[0].asString(), ec);
        return Value(ec ? -1.0 : static_cast<double>(size));
    });

    evaluator.registerMemberFunction("fs", "read_file", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value("");
        std::ifstream file(args[0].asString(), std::ios::in | std::ios::binary);
        if (!file.is_open()) return Value("");
        std::ostringstream ss;
        ss << file.rdbuf();
        return Value(ss.str());
    });

    evaluator.registerMemberFunction("fs", "write_file", [](const std::vector<Value>& args, const auto&) -> Value {
        if (args.size() < 2) return Value(false);
        std::ofstream file(args[0].asString(), std::ios::out | std::ios::binary);
        if (!file.is_open()) return Value(false);
        file << args[1].asString();
        return Value(true);
    });

    evaluator.registerMemberFunction("fs", "list_dir", [](const std::vector<Value>& args, const auto&) -> Value {
        std::string dirPath = args.empty() ? "." : args[0].asString();
        std::error_code ec;
        std::vector<Value> files;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            files.emplace_back(entry.path().filename().string());
        }
        return Value(files);
    });

    // If a HostRegistry is passed, bind its functions as well
    if (hostRegistry) {
        hostRegistry->bindToEvaluator(evaluator);
    }
}

void registerHostStdlib(abi::HostRegistry& hostRegistry) {
    // Register standard math functions in the C-ABI host registry
    hostRegistry.registerHostFunction("math_clamp", EATS_ABI_TYPE_F64,
        {EATS_ABI_TYPE_F64, EATS_ABI_TYPE_F64, EATS_ABI_TYPE_F64},
        [](const std::vector<EatsAbiValue>& args) -> EatsAbiValue {
            double v = args.size() > 0 ? args[0].as.f64 : 0.0;
            double lo = args.size() > 1 ? args[1].as.f64 : 0.0;
            double hi = args.size() > 2 ? args[2].as.f64 : 0.0;
            return eats_abi_value_f64(std::clamp(v, lo, hi));
        }, "Clamps value between low and high limits");

    hostRegistry.registerHostFunction("math_lerp", EATS_ABI_TYPE_F64,
        {EATS_ABI_TYPE_F64, EATS_ABI_TYPE_F64, EATS_ABI_TYPE_F64},
        [](const std::vector<EatsAbiValue>& args) -> EatsAbiValue {
            double a = args.size() > 0 ? args[0].as.f64 : 0.0;
            double b = args.size() > 1 ? args[1].as.f64 : 0.0;
            double t = args.size() > 2 ? args[2].as.f64 : 0.0;
            return eats_abi_value_f64(a + (b - a) * t);
        }, "Linearly interpolates between a and b by factor t");
}

} // namespace eatsbits::eatscript
