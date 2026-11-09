#pragma once

#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/abi/host_registry.hpp"
#include "eatsbits/eatscript/stdlib.hpp"
#include <string>
#include <vector>
#include <memory>
#include <string_view>

namespace eatsbits::eatscript {

struct ReplResult {
    enum class Status {
        Complete,    // Statement successfully evaluated; output and resultValue are valid
        Incomplete,  // Statement is incomplete (e.g. open block or brackets); prompt continuation
        Error        // Evaluation or parsing error occurred; error message is in error field
    };

    Status status{Status::Complete};
    std::string output;
    Value resultValue;
    std::string error;
};

/**
 * @brief Interactive Eatscript Shell & REPL Runtime Engine.
 * Features:
 * - Multi-line statement continuation & block detection
 * - Command history navigation
 * - Auto-completion for keywords, evaluator symbols, and host ABI reflection descriptors
 * - Rich, structured pretty-printing of Value objects (primitives, lists, dicts, host functions)
 * - Built-in standard library & host ABI reflection wiring
 */
class ReplEngine {
public:
    explicit ReplEngine(abi::HostRegistry* externalRegistry = nullptr);
    ~ReplEngine() = default;

    // Evaluates a line (accumulating multi-line blocks when incomplete)
    ReplResult feedLine(const std::string& line);

    // Multi-line State
    [[nodiscard]] bool isIncomplete() const noexcept { return !multilineBuffer_.empty(); }
    void resetMultiline() noexcept { multilineBuffer_.clear(); }
    [[nodiscard]] std::string getCurrentPrompt() const;

    // Auto-completion
    [[nodiscard]] std::vector<std::string> complete(std::string_view prefix) const;

    // History Navigation
    void addHistory(const std::string& line);
    [[nodiscard]] const std::vector<std::string>& getHistory() const noexcept { return history_; }
    void clearHistory() noexcept { history_.clear(); historyIndex_ = -1; }
    [[nodiscard]] std::string historyPrev();
    [[nodiscard]] std::string historyNext();
    void resetHistoryIndex() noexcept { historyIndex_ = -1; }

    // Value Pretty-Printing
    static std::string prettyPrint(const Value& val, int indent = 0, bool color = true);

    // Configuration & State Access
    [[nodiscard]] Evaluator& getEvaluator() noexcept { return evaluator_; }
    [[nodiscard]] const Evaluator& getEvaluator() const noexcept { return evaluator_; }
    [[nodiscard]] abi::HostRegistry* getHostRegistry() noexcept { return hostRegistry_; }

    void setPrompt(std::string prompt) { prompt_ = std::move(prompt); }
    void setContinuationPrompt(std::string prompt) { continuationPrompt_ = std::move(prompt); }
    void setColorOutput(bool enable) noexcept { colorOutput_ = enable; }
    [[nodiscard]] bool isColorOutput() const noexcept { return colorOutput_; }

private:
    Evaluator evaluator_;
    abi::HostRegistry* hostRegistry_{nullptr};
    std::unique_ptr<abi::HostRegistry> ownedRegistry_;

    std::string multilineBuffer_;
    std::vector<std::string> history_;
    int historyIndex_{-1};

    std::string prompt_{">>> "};
    std::string continuationPrompt_{"... "};
    bool colorOutput_{true};
    bool inIndentedBlock_{false};

    [[nodiscard]] bool isStatementIncomplete(std::string_view code) const;
    void initEnvironment();
};

} // namespace eatsbits::eatscript
