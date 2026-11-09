#include "eatsbits/eatscript/repl.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace eatsbits::eatscript {

ReplEngine::ReplEngine(abi::HostRegistry* externalRegistry) {
    if (externalRegistry) {
        hostRegistry_ = externalRegistry;
    } else {
        ownedRegistry_ = std::make_unique<abi::HostRegistry>();
        hostRegistry_ = ownedRegistry_.get();
    }
    initEnvironment();
}

void ReplEngine::initEnvironment() {
    if (hostRegistry_) {
        registerHostStdlib(*hostRegistry_);
        hostRegistry_->bindToEvaluator(evaluator_);
    }
    registerStandardLibrary(evaluator_, hostRegistry_);
}

bool ReplEngine::isStatementIncomplete(std::string_view code) const {
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (size_t i = 0; i < code.size(); ++i) {
        char ch = code[i];

        if (ch == '\\' && (inSingleQuote || inDoubleQuote)) {
            ++i; // skip escaped character
            continue;
        }

        if (ch == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
            continue;
        }
        if (ch == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
            continue;
        }

        if (!inSingleQuote && !inDoubleQuote) {
            if (ch == '(') parenDepth++;
            else if (ch == ')') parenDepth = std::max(0, parenDepth - 1);
            else if (ch == '[') bracketDepth++;
            else if (ch == ']') bracketDepth = std::max(0, bracketDepth - 1);
            else if (ch == '{') braceDepth++;
            else if (ch == '}') braceDepth = std::max(0, braceDepth - 1);
        }
    }

    if (inSingleQuote || inDoubleQuote || parenDepth > 0 || bracketDepth > 0 || braceDepth > 0) {
        return true;
    }

    // Check if line ends with ':' (e.g. def foo(): or if cond:)
    size_t lastNonWs = code.find_last_not_of(" \t\r\n");
    if (lastNonWs != std::string_view::npos) {
        char lastChar = code[lastNonWs];
        if (lastChar == ':' || lastChar == '\\' || lastChar == ',') {
            return true;
        }
    }

    return false;
}

std::string ReplEngine::getCurrentPrompt() const {
    return multilineBuffer_.empty() ? prompt_ : continuationPrompt_;
}

ReplResult ReplEngine::feedLine(const std::string& line) {
    std::string codeToRun;

    if (multilineBuffer_.empty()) {
        size_t nonWs = line.find_last_not_of(" \t\r\n");
        if (nonWs == std::string::npos) {
            return {ReplResult::Status::Complete, "", Value(), ""};
        }

        if (line == "exit" || line == "quit" || line == "exit()" || line == "quit()") {
            return {ReplResult::Status::Complete, "Exiting Eatscript.\n", Value(), ""};
        }

        if (isStatementIncomplete(line)) {
            multilineBuffer_ = line;
            size_t lastNonWs = line.find_last_not_of(" \t\r\n");
            inIndentedBlock_ = (lastNonWs != std::string::npos && line[lastNonWs] == ':');
            return {ReplResult::Status::Incomplete, "", Value(), ""};
        }

        codeToRun = line;
    } else {
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            // Blank line closes multi-line block
            codeToRun = multilineBuffer_;
            multilineBuffer_.clear();
            inIndentedBlock_ = false;
        } else {
            multilineBuffer_ += "\n" + line;
            if (inIndentedBlock_ || isStatementIncomplete(multilineBuffer_)) {
                return {ReplResult::Status::Incomplete, "", Value(), ""};
            }
            codeToRun = multilineBuffer_;
            multilineBuffer_.clear();
            inIndentedBlock_ = false;
        }
    }

    addHistory(codeToRun);
    evaluator_.clearLogs();

    Value result = evaluator_.evaluateSource(codeToRun);

    if (!evaluator_.getLastError().empty()) {
        std::string err = evaluator_.getLastError();
        return {ReplResult::Status::Error, "", Value(), std::move(err)};
    }

    std::string output;
    for (const auto& logMsg : evaluator_.getLogs()) {
        output += logMsg + "\n";
    }

    if (!result.isNil()) {
        output += prettyPrint(result, 0, colorOutput_);
    }

    return {ReplResult::Status::Complete, std::move(output), std::move(result), ""};
}

void ReplEngine::addHistory(const std::string& line) {
    if (line.empty()) return;
    if (!history_.empty() && history_.back() == line) return;
    history_.push_back(line);
    historyIndex_ = -1;
}

std::string ReplEngine::historyPrev() {
    if (history_.empty()) return "";
    if (historyIndex_ == -1) {
        historyIndex_ = static_cast<int>(history_.size()) - 1;
    } else if (historyIndex_ > 0) {
        historyIndex_--;
    }
    return history_[static_cast<size_t>(historyIndex_)];
}

std::string ReplEngine::historyNext() {
    if (history_.empty() || historyIndex_ == -1) return "";
    if (historyIndex_ + 1 < static_cast<int>(history_.size())) {
        historyIndex_++;
        return history_[static_cast<size_t>(historyIndex_)];
    }
    historyIndex_ = -1;
    return "";
}

std::vector<std::string> ReplEngine::complete(std::string_view prefix) const {
    std::vector<std::string> matches;

    size_t dotPos = prefix.rfind('.');
    if (dotPos != std::string_view::npos) {
        std::string objName(prefix.substr(0, dotPos));
        std::string memberPrefix(prefix.substr(dotPos + 1));

        // Built-in module members
        static const std::vector<std::pair<std::string, std::vector<std::string>>> kModuleMembers = {
            {"math", {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
                      "sqrt", "pow", "exp", "log", "log10", "abs", "min", "max",
                      "clamp", "round", "floor", "ceil", "lerp"}},
            {"sys", {"platform", "version", "time", "clock", "sleep", "cwd", "getenv"}},
            {"fs", {"exists", "read_file", "write_file", "file_size", "list_dir"}},
            {"string", {"upper", "lower", "trim", "split", "join", "len"}},
            {"console", {"log", "warn", "error"}}
        };

        for (const auto& [mod, members] : kModuleMembers) {
            if (mod == objName) {
                for (const auto& mem : members) {
                    if (mem.rfind(memberPrefix, 0) == 0) {
                        matches.push_back(objName + "." + mem);
                    }
                }
            }
        }

        // Dict variables in Evaluator
        if (evaluator_.hasVariable(objName)) {
            Value val = evaluator_.getVariable(objName);
            if (val.isDict()) {
                for (const auto& [k, v] : val.dictVal) {
                    if (k.rfind(memberPrefix, 0) == 0) {
                        matches.push_back(objName + "." + k);
                    }
                }
            }
        }
    } else {
        // Keywords and standard identifiers
        static const char* kKeywords[] = {
            "def", "return", "if", "elif", "else", "while", "for", "in",
            "and", "or", "not", "true", "false", "nil", "var", "let", "fn",
            "import", "print", "println", "len", "str", "math", "sys", "fs",
            "string", "console", "break", "continue"
        };

        for (const char* kw : kKeywords) {
            if (std::string_view(kw).rfind(prefix, 0) == 0) {
                matches.push_back(kw);
            }
        }

        // Host ABI functions
        if (hostRegistry_) {
            for (const auto& fnName : hostRegistry_->getRegisteredFunctionNames()) {
                if (std::string_view(fnName).rfind(prefix, 0) == 0) {
                    matches.push_back(fnName);
                }
            }
            for (const auto& structName : hostRegistry_->getRegisteredStructNames()) {
                if (std::string_view(structName).rfind(prefix, 0) == 0) {
                    matches.push_back(structName);
                }
            }
        }
    }

    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

std::string ReplEngine::prettyPrint(const Value& val, int indent, bool color) {
    auto style = [&](const std::string& text, const char* sgr) -> std::string {
        if (!color || !sgr) return text;
        return std::string(sgr) + text + "\x1b[0m";
    };

    if (val.isNil()) {
        return style("nil", "\x1b[90m");
    }

    if (val.isBoolean()) {
        return style(val.boolVal ? "true" : "false", "\x1b[35m\x1b[1m");
    }

    if (val.isNumber()) {
        double d = val.numberVal;
        if (std::floor(d) == d && !std::isinf(d) && !std::isnan(d) && std::abs(d) < 1e15) {
            return style(std::to_string(static_cast<int64_t>(d)), "\x1b[33m");
        }
        std::ostringstream ss;
        ss << std::setprecision(6) << d;
        return style(ss.str(), "\x1b[33m");
    }

    if (val.isString()) {
        std::string escaped;
        escaped.reserve(val.stringVal.size() + 2);
        for (char ch : val.stringVal) {
            if (ch == '\n') escaped += "\\n";
            else if (ch == '\r') escaped += "\\r";
            else if (ch == '\t') escaped += "\\t";
            else if (ch == '"') escaped += "\\\"";
            else escaped += ch;
        }
        return style("\"" + escaped + "\"", "\x1b[32m");
    }

    if (val.isList()) {
        if (val.listVal.empty()) {
            return style("[]", "\x1b[90m");
        }
        // Small primitive lists inline
        if (val.listVal.size() <= 4 && indent == 0) {
            std::string out = "[";
            for (size_t i = 0; i < val.listVal.size(); ++i) {
                if (i > 0) out += ", ";
                out += prettyPrint(val.listVal[i], indent + 1, color);
            }
            out += "]";
            return out;
        }

        std::string pad(static_cast<size_t>((indent + 1) * 2), ' ');
        std::string endPad(static_cast<size_t>(indent * 2), ' ');
        std::string out = "[\n";
        for (size_t i = 0; i < val.listVal.size(); ++i) {
            out += pad + prettyPrint(val.listVal[i], indent + 1, color);
            if (i + 1 < val.listVal.size()) out += ",";
            out += "\n";
        }
        out += endPad + "]";
        return out;
    }

    if (val.isDict()) {
        if (val.dictVal.empty()) {
            return style("{}", "\x1b[90m");
        }

        std::string pad(static_cast<size_t>((indent + 1) * 2), ' ');
        std::string endPad(static_cast<size_t>(indent * 2), ' ');
        std::string out = "{\n";
        size_t count = 0;
        for (const auto& [k, v] : val.dictVal) {
            out += pad + style("\"" + k + "\"", "\x1b[36m") + ": " + prettyPrint(v, indent + 1, color);
            if (++count < val.dictVal.size()) out += ",";
            out += "\n";
        }
        out += endPad + "}";
        return out;
    }

    if (val.isFunction()) {
        if (val.funcDef) {
            return style("<function '" + val.funcDef->name + "'>", "\x1b[36m\x1b[3m");
        }
        return style("<native host_fn>", "\x1b[36m\x1b[3m");
    }

    return style("<unknown>", "\x1b[90m");
}

} // namespace eatsbits::eatscript
