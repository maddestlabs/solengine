#ifndef SOLENGINE_COMMAND_PALETTE_DIALOG_HPP
#define SOLENGINE_COMMAND_PALETTE_DIALOG_HPP

#include "solengine/command_registry.hpp"
#include "solengine/ui/kinetic_scroller.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <functional>
#include <string>
#include <vector>

namespace solengine {

class AudioFoleyEngine;
struct ThemePalette;

/**
 * Keyboard-first, fuzzy-searchable Command Palette for SolEngine (Ctrl+K / Ctrl+P).
 * Provides instant access to gameplay actions, game variants, themes, decks, and settings,
 * while surfacing keyboard shortcuts to aid discoverability and accessible workflows.
 */
class CommandPaletteDialog {
public:
    CommandPaletteDialog() = default;
    ~CommandPaletteDialog() = default;

    void setRegistry(CommandRegistry* registry) noexcept { registry_ = registry; }

    void open();
    void close() noexcept;
    void toggle();
    void update(float dt) noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    void setSearchQuery(const std::string& text);
    void refreshFilter();

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
        int mods,
        float mouseX,
        float mouseY,
        AudioFoleyEngine& foley
    );

    void handleMouseMove(float mouseX, float mouseY);

    bool handleKey(
        int key,
        int action,
        int mods,
        AudioFoleyEngine& foley
    );

    bool handleChar(unsigned int codepoint);
    bool handleScroll(float yoffset);

    // Optional hook called right before opening so host can refresh dynamic entries (games/themes/decks)
    std::function<void()> onBeforeOpen;

private:
    void ensureVisible(int index);
    bool executeHighlighted(bool keepOpen, AudioFoleyEngine& foley);
    void cycleCategory(int delta, AudioFoleyEngine& foley);

    CommandRegistry* registry_{nullptr};
    bool isOpen_{false};
    bool draggingScrollbar_{false};
    KineticScroller scroller_{};

    std::string searchQuery_;
    CommandCategory selectedCategory_{CommandCategory::All};
    int highlightedIndex_{0};
    float cursorBlinkTimer_{0.0f};

    std::vector<CommandMatch> filteredMatches_;

    // Layout bounds cached during render
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{0.0f};
    float dialogH_{0.0f};
    float searchBoxX_{0.0f};
    float searchBoxY_{0.0f};
    float searchBoxW_{0.0f};
    float searchBoxH_{34.0f};
    float chipRowY_{0.0f};
    float listAreaY_{0.0f};
    float listAreaH_{0.0f};
};

} // namespace solengine

#endif // SOLENGINE_COMMAND_PALETTE_DIALOG_HPP
