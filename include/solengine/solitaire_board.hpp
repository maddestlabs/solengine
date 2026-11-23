#ifndef SOLENGINE_SOLITAIRE_BOARD_HPP
#define SOLENGINE_SOLITAIRE_BOARD_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/game_definition.hpp"
#include "solengine/script_rule_engine.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace solengine {

/**
 * Headless Solitaire Rule Engine, Smart-Tap Solver, Undo Stack & Spring Animator.
 * Models Hardwood Solitaire 3.0 ergonomics and Enshaded (Spectre) mechanics.
 */
class SolitaireBoard {
public:
    SolitaireBoard();

    void newDeal(uint32_t seed, GameVariant variant = GameVariant::KlondikeDraw1);
    void newDeal(uint32_t seed, const SolitaireGameDef& gameDef);
    void restartDeal();
    void setTableTheme(TableThemeId theme) noexcept { theme_ = theme; }
    [[nodiscard]] TableThemeId tableTheme() const noexcept { return theme_; }
    void setTableThemeSlug(const std::string& slug) { themeSlug_ = slug; }
    [[nodiscard]] const std::string& tableThemeSlug() const noexcept { return themeSlug_; }
    void setTableThemeOption(const std::string& opt) { themeOption_ = opt; }
    [[nodiscard]] const std::string& tableThemeOption() const noexcept { return themeOption_; }

    [[nodiscard]] GameVariant variant() const noexcept { return variant_; }
    [[nodiscard]] const SolitaireGameDef& activeGameDef() const noexcept { return activeDef_; }
    [[nodiscard]] bool isSpider() const noexcept { return isSpiderVariant(variant_) || activeDef_.family == "Spider" || activeDef_.id.find("spider") != std::string::npos; }
    [[nodiscard]] bool isSpiderette() const noexcept { return activeDef_.id == "spiderette" || activeDef_.name.find("Spiderette") != std::string::npos; }
    [[nodiscard]] bool isGolf() const noexcept { return isGolfVariant(variant_) || activeDef_.family == "Golf" || activeDef_.id == "golf"; }
    [[nodiscard]] uint32_t seed() const noexcept { return seed_; }
    [[nodiscard]] uint32_t rngState() const noexcept { return rngState_; }
    [[nodiscard]] int32_t score() const noexcept { return score_; }
    [[nodiscard]] uint32_t moveCount() const noexcept { return moveCount_; }
    [[nodiscard]] float elapsedSeconds() const noexcept { return elapsedSeconds_; }
    [[nodiscard]] float victorySeconds() const noexcept { return victorySeconds_; }
    [[nodiscard]] bool isVictory() const noexcept;
    [[nodiscard]] GameStatus gameStatus() const;
    [[nodiscard]] bool canAutoFinish() const noexcept;
    [[nodiscard]] const BoardBlackboard& blackboard() const noexcept { return blackboard_; }
    [[nodiscard]] bool hasScriptHooks() const noexcept { return scriptEngine_.isLoaded() && scriptEngine_.hasAnyHooks(); }
    [[nodiscard]] bool hasScriptCardClick() const noexcept { return scriptEngine_.hasOnCardClick(); }
    [[nodiscard]] bool hasScriptStockClick() const noexcept { return scriptEngine_.hasOnStockClick(); }
    [[nodiscard]] bool hasScriptCanPick() const noexcept { return scriptEngine_.hasCanPick(); }
    [[nodiscard]] bool hasScriptCanDrop() const noexcept { return scriptEngine_.hasCanDrop(); }
    [[nodiscard]] bool hasScriptCanTarget() const noexcept { return scriptEngine_.hasCanTarget(); }
    [[nodiscard]] bool canPickScriptCard(uint8_t pileIdx, size_t cardIdx) const;
    [[nodiscard]] const GameDocManifest& docManifest() const noexcept { return scriptEngine_.docManifest(); }
    [[nodiscard]] const VictoryChoreography& victoryChoreography() const noexcept { return scriptEngine_.victoryChoreography(); }
    [[nodiscard]] int32_t evaluateScriptHeuristic() const;

    struct ScriptedSnapshot {
        std::vector<Pile> piles;
        BoardBlackboard blackboard;
        int32_t score{0};
        uint32_t rngState{0};
        uint32_t moveCount{0};
    };
    [[nodiscard]] ScriptedSnapshot captureSnapshot() const;
    void restoreSnapshot(const ScriptedSnapshot& snap);

    // Full session state restoration
    bool restoreSessionState(
        uint32_t seed,
        uint32_t rngState,
        GameVariant variant,
        int32_t score,
        uint32_t moveCount,
        float elapsedSeconds,
        const std::vector<Pile>& piles,
        const std::vector<MoveCommand>& undoStack,
        const std::vector<MoveCommand>& redoStack,
        const std::string& gameId = ""
    );

    // Layout calculation for responsive desktop & mobile aspect ratios
    void computeLayout(float viewportWidth, float viewportHeight);
    void dealOrigin(float& outX, float& outY) const noexcept;
    void triggerDealAnimation() noexcept;

    // Advance spring-damper card flight trajectories, 2.5D flips, and elevations
    void updateAnimations(float dt);
    [[nodiscard]] bool isInputLocked() const noexcept;
    uint8_t consumePendingDealPans(float* outPans, uint8_t maxCount) noexcept;

    // Core interactions
    bool drawFromStock();
    bool canMoveCards(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx) const;
    bool executeMove(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx, bool recordUndo = true);
    bool executeCardTarget(uint8_t fromPileIdx, size_t fromCardIdx, uint8_t toPileIdx, size_t toCardIdx, bool recordUndo = true);

    // Hardwood 3.0 "Smart-Tap" / Right-Click automatic best legal destination
    bool smartMove(uint8_t fromPileIdx, size_t startCardIdx);

    // Auto-promote eligible top cards to Foundations
    bool stepAutoFoundation();
    bool collectCompletedSpiderRuns();

    // Nether Sight (Hint): finds highest-priority legal move and marks spectral glow
    HintMove findBestHint();

    // Unlimited Undo / Redo
    bool undo();
    bool redo();
    bool applyTransaction(const TurnTransaction& tx);

    // Hit testing & 2D occlusion helpers for pointer/touch input and planar graph layouts
    bool hitTestCard(float px, float py, int& outPileIdx, int& outCardIdx) const noexcept;
    int findBestDropPile(uint8_t fromPileIdx, size_t startCardIdx, float dropCenterX, float dropCenterY) const noexcept;
    [[nodiscard]] bool isCardBlocked(size_t pileIdx, size_t cardIdx) const noexcept;
    [[nodiscard]] bool isPileBlocked(size_t pileIdx) const noexcept;

    // Pairing solitaire helpers (Pyramid, Giza)
    [[nodiscard]] bool isPyramidGame() const noexcept;
    [[nodiscard]] bool isCardExposedInPyramid(size_t pileIdx) const noexcept;
    [[nodiscard]] bool isCardPlayableInPyramid(size_t pileIdx) const noexcept;
    bool executePyramidPair(uint8_t pileA, uint8_t pileB, uint8_t foundationPile);
    void clearSelection() noexcept;
    [[nodiscard]] int selectedPile() const noexcept { return selectedPileIdx_; }

    [[nodiscard]] const std::vector<Pile>& piles() const noexcept { return piles_; }
    [[nodiscard]] std::vector<Pile>& piles() noexcept { return piles_; }
    [[nodiscard]] const std::vector<MoveCommand>& undoStack() const noexcept { return undoStack_; }
    [[nodiscard]] const std::vector<MoveCommand>& redoStack() const noexcept { return redoStack_; }
    [[nodiscard]] const std::vector<TurnTransaction>& txUndoStack() const noexcept { return txUndoStack_; }
    [[nodiscard]] const HintMove& activeHint() const noexcept { return activeHint_; }
    void clearHint() noexcept;

    [[nodiscard]] static const char* variantName(GameVariant v) noexcept;
    [[nodiscard]] static const char* themeName(TableThemeId t) noexcept;
    [[nodiscard]] static const char* layoutModeName(ResponsiveLayoutMode m) noexcept;
    [[nodiscard]] static const char* stockPositionName(StockPosition p) noexcept;

    void setLayoutMode(ResponsiveLayoutMode mode) noexcept;
    [[nodiscard]] ResponsiveLayoutMode layoutMode() const noexcept { return layoutMode_; }
    void setStockPosition(StockPosition pos) noexcept;
    [[nodiscard]] StockPosition stockPosition() const noexcept { return stockPosition_; }
    void setSafeAreaInsets(float top, float right, float bottom, float left) noexcept;
    [[nodiscard]] float safeTop() const noexcept { return safeTop_; }
    [[nodiscard]] float safeRight() const noexcept { return safeRight_; }
    [[nodiscard]] float safeBottom() const noexcept { return safeBottom_; }
    [[nodiscard]] float safeLeft() const noexcept { return safeLeft_; }
    [[nodiscard]] float viewportWidth() const noexcept { return viewportW_; }
    [[nodiscard]] float viewportHeight() const noexcept { return viewportH_; }
    [[nodiscard]] bool isMobileLayout() const noexcept;

private:
    uint32_t nextRandom() noexcept;
    void updateCardTargets();
    void finalizeScriptTurn(ScriptBoardContext& ctx, TurnTransaction& tx, bool recordUndo = true);
    ScriptBoardContext makeScriptContext(TurnTransaction* tx = nullptr) noexcept;
    ScriptBoardContext makeConstScriptContext() const noexcept;

    uint32_t seed_{2026u};
    uint32_t rngState_{2026u};
    GameVariant variant_{GameVariant::KlondikeDraw1};
    SolitaireGameDef activeDef_;
    TableThemeId theme_{TableThemeId::PastelTan};
    std::string themeSlug_;
    std::string themeOption_;
    ResponsiveLayoutMode layoutMode_{ResponsiveLayoutMode::Auto};
    StockPosition stockPosition_{StockPosition::Auto};

    int32_t score_{0};
    uint32_t moveCount_{0};
    int selectedPileIdx_{-1};
    float elapsedSeconds_{0.0f};
    float victorySeconds_{0.0f};
    bool victoryHookFired_{false};
    float viewportW_{1280.0f};
    float viewportH_{720.0f};
    float safeTop_{0.0f};
    float safeRight_{0.0f};
    float safeBottom_{0.0f};
    float safeLeft_{0.0f};
    float cardW_{96.0f};
    float cardH_{136.0f};
    float cardDeviation_{0.25f};

    std::vector<Pile> piles_;
    std::vector<MoveCommand> undoStack_;
    std::vector<MoveCommand> redoStack_;
    std::vector<TurnTransaction> txUndoStack_;
    std::vector<TurnTransaction> txRedoStack_;
    BoardBlackboard blackboard_;
    mutable ScriptRuleEngine scriptEngine_;
    std::vector<PendingScriptAction> pendingActions_;
    mutable GameStatus cachedGameStatus_{GameStatus::Playing};
    mutable bool statusCacheDirty_{true};
    HintMove activeHint_{};
    std::array<float, 32> pendingDealPans_{};
    uint8_t pendingDealCount_{0};

public:
    [[nodiscard]] float cardWidth() const noexcept { return cardW_; }
    [[nodiscard]] float cardHeight() const noexcept { return cardH_; }
    [[nodiscard]] float cardDeviation() const noexcept { return cardDeviation_; }
    void setCardDeviation(float dev) noexcept;
};

} // namespace solengine

#endif // SOLENGINE_SOLITAIRE_BOARD_HPP
