#ifndef SOLENGINE_GAME_BROWSER_DIALOG_HPP
#define SOLENGINE_GAME_BROWSER_DIALOG_HPP

#include "solengine/game_definition.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/auto_play_controller.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace solengine {

class AudioFoleyEngine;
struct ThemePalette;

/**
 * Searchable, filterable game selection dialog for SolEngine with live auto-playing mini-table preview.
 * Inspired by Eatsbits' reusable PluginSearchDialog (c:\git\eatsbits) and Solitaire Forever II's live demo list.
 * Supports category/mechanic/visibility chips, real-time typing search, keyboard arrow navigation,
 * responsive desktop split & mobile pinned-stage live demos, and instant deal loading.
 */
class GameBrowserDialog {
public:
    GameBrowserDialog();
    ~GameBrowserDialog();

    void open(const std::string& activeGameId = "");
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    // Advance live mini-table preview animations and auto-play solver/playback
    void update(float dt);

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal
    );

    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    void handleMouseMove(float mouseX, float mouseY);
    void setSearchQuery(const std::string& text);

    bool handleKey(
        int key,
        int action,
        int mods,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleChar(unsigned int codepoint);
    bool handleScroll(float yoffset);

    std::function<void(const SolitaireGameDef& def)> onGameSelected;
    std::function<void(const SolitaireGameDef& def)> onSolvableSelected;

private:
    struct RectF {
        float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f};
        [[nodiscard]] bool contains(float px, float py) const noexcept {
            return w > 0.0f && h > 0.0f && px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    void updateFilter();
    void ensureVisible(int index);
    bool launchSelectedGame(AudioFoleyEngine& foley);
    void syncPreviewToSelection(bool forceNextSeed = false);
    bool stepPreviewHeuristic();

    bool isOpen_{false};
    bool searchFocused_{true};
    bool isMouseDown_{false};
    bool isDragging_{false};
    float dragStartY_{0.0f};
    float initialScrollY_{0.0f};
    std::string activeGameId_{""};
    std::string searchQuery_{""};
    std::string selectedCategory_{"ALL"};
    std::string selectedMechanic_{"ALL"};
    std::string selectedVisibility_{"ALL"};
    int highlightedIndex_{0};
    int lastClickedIndex_{-1};
    std::chrono::steady_clock::time_point lastClickTime_{};
    float scrollOffsetY_{0.0f};
    float maxScrollY_{0.0f};
    float cursorBlinkTimer_{0.0f};

    std::vector<SolitaireGameDef> filteredGames_;

    // Live isolated mini-table preview state (never touches active game)
    SolitaireBoard previewBoard_;
    AutoPlayController previewAutoPlay_;
    SolitaireGameDef previewDef_{};
    std::string previewGameId_;
    bool previewHasBoard_{false};
    size_t previewSeedCursor_{0};
    uint32_t previewSeed_{0};
    float previewDebounceTimer_{0.0f};
    bool previewSolvePending_{false};
    bool previewUserPaused_{false};
    bool previewHeuristicMode_{false};
    float previewHeuristicTimer_{0.0f};
    float previewLoopTimer_{0.0f};
    bool previewCollapsedMobile_{false};
    AutoPlaySpeed previewSpeed_{AutoPlaySpeed::Fast};
    std::unordered_map<std::string, std::vector<SolverMove>> previewMoveCache_;

    // Layout bounds cached during render
    bool isMobileLayout_{false};
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{0.0f};
    float dialogH_{0.0f};
    float listX_{0.0f};
    float listW_{0.0f};
    float cardsAreaY_{0.0f};
    float cardsAreaH_{0.0f};
    RectF stageRect_{};
    RectF pauseBtnRect_{};
    RectF speedBtnRect_{};
    RectF nextSeedBtnRect_{};
    RectF collapseBtnRect_{};
};

} // namespace solengine

#endif // SOLENGINE_GAME_BROWSER_DIALOG_HPP

