#ifndef SOLENGINE_SCRIPT_RULE_ENGINE_HPP
#define SOLENGINE_SCRIPT_RULE_ENGINE_HPP

#include "solengine/solitaire_types.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace solengine {

/**
 * Mutable view of board state exposed to Eatscript rule hooks and BoardOp undo/redo.
 */
struct ScriptBoardContext {
    std::vector<Pile>* piles{nullptr};
    BoardBlackboard* blackboard{nullptr};
    int32_t* score{nullptr};
    uint32_t* rngState{nullptr};
    TurnTransaction* activeTx{nullptr};
};

/**
 * Eatscript-backed rule runtime for classic edge-case and modern/RPG solitaires.
 * Loads .eats hook functions and executes board mutations through atomic BoardOps.
 */
class ScriptRuleEngine {
public:
    ScriptRuleEngine() = default;
    ScriptRuleEngine(const ScriptRuleEngine& other);
    ScriptRuleEngine& operator=(const ScriptRuleEngine& other);
    ScriptRuleEngine(ScriptRuleEngine&& other) noexcept;
    ScriptRuleEngine& operator=(ScriptRuleEngine&& other) noexcept;

    bool loadScript(const std::string& source);
    bool loadScriptFile(const std::string& filePath);
    void reset() noexcept;

    [[nodiscard]] bool isLoaded() const noexcept { return loaded_; }
    [[nodiscard]] bool hasAnyHooks() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

    // Hook presence queries
    [[nodiscard]] bool hasOnDeal() const noexcept { return hasOnDeal_; }
    [[nodiscard]] bool hasCanPick() const noexcept { return hasCanPick_; }
    [[nodiscard]] bool hasCanDrop() const noexcept { return hasCanDrop_; }
    [[nodiscard]] bool hasCanTarget() const noexcept { return hasCanTarget_; }
    [[nodiscard]] bool hasOnTarget() const noexcept { return hasOnTarget_; }
    [[nodiscard]] bool hasOnCardClick() const noexcept { return hasOnCardClick_; }
    [[nodiscard]] bool hasOnStockClick() const noexcept { return hasOnStockClick_; }
    [[nodiscard]] bool hasOnTurnEnd() const noexcept { return hasOnTurnEnd_; }
    [[nodiscard]] bool hasGetStatus() const noexcept { return hasGetStatus_; }
    [[nodiscard]] bool hasEvalHeuristic() const noexcept { return hasEvalHeuristic_; }
    [[nodiscard]] const GameDocManifest& docManifest() const noexcept { return docManifest_; }

    // Rule hook invocations
    bool invokeOnDeal(ScriptBoardContext& ctx);
    [[nodiscard]] bool invokeCanPick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx);
    [[nodiscard]] bool invokeCanDrop(ScriptBoardContext& ctx, uint8_t fromZone, size_t cardIdx, uint8_t toZone);
    [[nodiscard]] bool invokeCanTarget(ScriptBoardContext& ctx, uint8_t fromZone, size_t fromIdx, uint8_t toZone, size_t toIdx);
    bool invokeOnTarget(ScriptBoardContext& ctx, uint8_t fromZone, size_t fromIdx, uint8_t toZone, size_t toIdx);
    bool invokeOnCardClick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx);
    bool invokeOnStockClick(ScriptBoardContext& ctx);
    bool invokeOnTurnEnd(ScriptBoardContext& ctx);
    [[nodiscard]] GameStatus invokeGetStatus(ScriptBoardContext& ctx);
    [[nodiscard]] int32_t invokeEvalHeuristic(ScriptBoardContext& ctx);

    // Deterministic atomic operation apply/revert for Undo & Redo
    static bool applyOp(ScriptBoardContext& ctx, const BoardOp& op);
    static bool revertOp(ScriptBoardContext& ctx, const BoardOp& op);
    static void applyTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx);
    static void revertTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx);

private:
    void registerHostApi();
    void bindContext(ScriptBoardContext* ctx) noexcept { boundCtx_ = ctx; }

    eatsbits::eatscript::Evaluator evaluator_;
    ScriptBoardContext* boundCtx_{nullptr};
    std::string lastError_;
    bool loaded_{false};

    bool hasOnDeal_{false};
    bool hasCanPick_{false};
    bool hasCanDrop_{false};
    bool hasCanTarget_{false};
    bool hasOnTarget_{false};
    bool hasOnCardClick_{false};
    bool hasOnStockClick_{false};
    bool hasOnTurnEnd_{false};
    bool hasGetStatus_{false};
    bool hasEvalHeuristic_{false};
    GameDocManifest docManifest_;
};

} // namespace solengine

#endif // SOLENGINE_SCRIPT_RULE_ENGINE_HPP
