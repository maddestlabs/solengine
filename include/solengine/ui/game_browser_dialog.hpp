#ifndef SOLENGINE_GAME_BROWSER_DIALOG_HPP
#define SOLENGINE_GAME_BROWSER_DIALOG_HPP

#include "solengine/game_definition.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <string>
#include <vector>
#include <functional>

namespace solengine {

class SolitaireBoard;
class AudioFoleyEngine;
struct ThemePalette;

/**
 * Searchable, filterable game selection dialog for SolEngine.
 * Inspired by Eatsbits' reusable PluginSearchDialog (c:\git\eatsbits).
 * Supports category chips, real-time typing search, keyboard arrow navigation,
 * variant rule badges, and instant one-click deal loading.
 */
class GameBrowserDialog {
public:
    GameBrowserDialog();
    ~GameBrowserDialog() = default;

    void open(const std::string& activeGameId = "");
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

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
    void updateFilter();
    void ensureVisible(int index);

    bool isOpen_{false};
    bool searchFocused_{true};
    bool isMouseDown_{false};
    bool isDragging_{false};
    float dragStartY_{0.0f};
    float initialScrollY_{0.0f};
    std::string activeGameId_{""};
    std::string searchQuery_{""};
    std::string selectedCategory_{"ALL"};
    int highlightedIndex_{0};
    float scrollOffsetY_{0.0f};
    float maxScrollY_{0.0f};
    float cursorBlinkTimer_{0.0f};

    std::vector<SolitaireGameDef> filteredGames_;

    // Layout bounds cached during render
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{0.0f};
    float dialogH_{0.0f};
    float cardsAreaY_{0.0f};
    float cardsAreaH_{0.0f};
};

} // namespace solengine

#endif // SOLENGINE_GAME_BROWSER_DIALOG_HPP

