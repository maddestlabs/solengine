#ifndef SOLENGINE_SCRIPT_RULE_ENGINE_HPP
#define SOLENGINE_SCRIPT_RULE_ENGINE_HPP

#include "solengine/solitaire_types.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace solengine {

struct SolitaireGameDef;

struct PendingScriptAction {
    float remainingSec{0.0f};
    std::string fnName;
    bool lockInput{true};
};

/**
 * Mutable view of board state exposed to Eatscript rule hooks and BoardOp undo/redo.
 */
struct ScriptBoardContext {
    std::vector<Pile>* piles{nullptr};
    BoardBlackboard* blackboard{nullptr};
    int32_t* score{nullptr};
    uint32_t* rngState{nullptr};
    TurnTransaction* activeTx{nullptr};
    float cardW{96.0f};
    float cardH{136.0f};
    float viewportW{1280.0f};
    float viewportH{720.0f};
    float safeTop{0.0f};
    float safeRight{0.0f};
    float safeBottom{0.0f};
    float safeLeft{0.0f};
    std::vector<PendingScriptAction>* pendingActions{nullptr};
    bool suppressWidgets{false};
    uint32_t seed{2026u};
    uint32_t layoutSeed{2026u};
    SolitaireGameDef* gameDef{nullptr};
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
    bool loadScriptForGame(const std::string& source, const std::string& targetGameId);
    bool loadFileForGame(const std::string& filePath, const std::string& targetGameId);
    void reset() noexcept;

    [[nodiscard]] bool isLoaded() const noexcept { return loaded_; }
    [[nodiscard]] bool hasAnyHooks() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

    // Hook presence queries
    [[nodiscard]] bool hasOnConfigure() const noexcept { return hasOnConfigure_; }
    [[nodiscard]] bool hasOnDeal() const noexcept { return hasOnDeal_; }
    [[nodiscard]] bool hasCanPick() const noexcept { return hasCanPick_; }
    [[nodiscard]] bool hasCanDrop() const noexcept { return hasCanDrop_; }
    [[nodiscard]] bool hasOnDrop() const noexcept { return hasOnDrop_; }
    [[nodiscard]] bool hasCanTarget() const noexcept { return hasCanTarget_; }
    [[nodiscard]] bool hasOnTarget() const noexcept { return hasOnTarget_; }
    [[nodiscard]] bool hasOnCardClick() const noexcept { return hasOnCardClick_; }
    [[nodiscard]] bool hasOnStockClick() const noexcept { return hasOnStockClick_; }
    [[nodiscard]] bool hasOnTurnEnd() const noexcept { return hasOnTurnEnd_; }
    [[nodiscard]] bool hasOnVictory() const noexcept { return hasOnVictory_; }
    [[nodiscard]] bool hasOnDemoStep() const noexcept { return hasOnDemoStep_; }
    [[nodiscard]] bool hasOnLayout() const noexcept { return hasOnLayout_; }
    [[nodiscard]] bool hasGetStatus() const noexcept { return hasGetStatus_; }
    [[nodiscard]] bool hasEvalHeuristic() const noexcept { return hasEvalHeuristic_; }
    [[nodiscard]] const GameDocManifest& docManifest() const noexcept { return docManifest_; }
    [[nodiscard]] const VictoryChoreography& victoryChoreography() const noexcept { return victoryChoreography_; }

    // Rule hook invocations
    bool invokeOnConfigure(ScriptBoardContext& ctx, uint32_t layoutSeed);
    bool invokeOnDeal(ScriptBoardContext& ctx);
    [[nodiscard]] bool invokeCanPick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx);
    [[nodiscard]] bool invokeCanDrop(ScriptBoardContext& ctx, uint8_t fromZone, size_t cardIdx, uint8_t toZone);
    bool invokeOnDrop(ScriptBoardContext& ctx, uint8_t fromZone, size_t cardIdx, uint8_t toZone);
    [[nodiscard]] bool invokeCanTarget(ScriptBoardContext& ctx, uint8_t fromZone, size_t fromIdx, uint8_t toZone, size_t toIdx);
    bool invokeOnTarget(ScriptBoardContext& ctx, uint8_t fromZone, size_t fromIdx, uint8_t toZone, size_t toIdx);
    bool invokeOnCardClick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx);
    bool invokeOnStockClick(ScriptBoardContext& ctx);
    bool invokeOnTurnEnd(ScriptBoardContext& ctx);
    bool invokeOnVictory(ScriptBoardContext& ctx);
    bool invokeOnDemoStep(ScriptBoardContext& ctx);
    bool invokeOnLayout(ScriptBoardContext& ctx, float boardW, float boardH);
    bool invokeNamedAction(ScriptBoardContext& ctx, const std::string& fnName);
    [[nodiscard]] GameStatus invokeGetStatus(ScriptBoardContext& ctx);
    [[nodiscard]] int32_t invokeEvalHeuristic(ScriptBoardContext& ctx);

    // Deterministic atomic operation apply/revert for Undo & Redo
    static bool applyOp(ScriptBoardContext& ctx, const BoardOp& op);
    static bool revertOp(ScriptBoardContext& ctx, const BoardOp& op);
    static void applyTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx);
    static void revertTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx);
    [[nodiscard]] static bool isCardBlocked(const std::vector<Pile>& piles, size_t pileIdx, size_t cardIdx, float cardW, float cardH) noexcept;
    [[nodiscard]] static bool isCardBlockedByPile(const std::vector<Pile>& piles, size_t pileIdx, size_t cardIdx, size_t otherPileIdx, float cardW, float cardH) noexcept;

private:
    void registerHostApi();
    void bindContext(ScriptBoardContext* ctx) noexcept { boundCtx_ = ctx; }
    eatsbits::eatscript::Value invokeHook(const std::string& name,
                                          const std::vector<eatsbits::eatscript::Value>& args = {});

    eatsbits::eatscript::Evaluator evaluator_;
    ScriptBoardContext* boundCtx_{nullptr};
    std::string lastError_;
    bool loaded_{false};
    std::unordered_map<std::string, eatsbits::eatscript::Value> boundCallables_;

    bool hasOnConfigure_{false};
    bool hasOnDeal_{false};
    bool hasCanPick_{false};
    bool hasCanDrop_{false};
    bool hasOnDrop_{false};
    bool hasCanTarget_{false};
    bool hasOnTarget_{false};
    bool hasOnCardClick_{false};
    bool hasOnStockClick_{false};
    bool hasOnTurnEnd_{false};
    bool hasOnVictory_{false};
    bool hasOnDemoStep_{false};
    bool hasOnLayout_{false};
    bool hasGetStatus_{false};
    bool hasEvalHeuristic_{false};
    struct LayoutSlot {
        float ux{0.0f};
        float uy{0.0f};
        float deg{0.0f};
        int32_t z{0};
    };
    std::vector<LayoutSlot> layoutSlots_;
    GameDocManifest docManifest_;
    VictoryChoreography victoryChoreography_;
};

} // namespace solengine

#endif // SOLENGINE_SCRIPT_RULE_ENGINE_HPP
