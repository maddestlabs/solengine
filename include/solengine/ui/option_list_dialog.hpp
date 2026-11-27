#ifndef SOLENGINE_OPTION_LIST_DIALOG_HPP
#define SOLENGINE_OPTION_LIST_DIALOG_HPP

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "solengine/ui/kinetic_scroller.hpp"
#include <string>
#include <vector>

namespace solengine {

class AudioFoleyEngine;
struct ThemePalette;

struct OptionListItem {
    std::string label;
    std::string badge; // Optional subtle right-aligned tag
};

/**
 * Compact, responsive modal dialog displaying a scrollable list of selectable
 * options. Designed to be opened when clicking/tapping the center text of any
 * "< option >" stepper control in SettingsDialog (or other dialogs).
 */
class OptionListDialog {
public:
    OptionListDialog() = default;
    ~OptionListDialog() = default;

    void open(
        std::string title,
        std::vector<OptionListItem> items,
        int selectedIndex
    );

    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    /// Advance kinetic scroll physics and indicator fade.
    void update(float dt) noexcept;

    /// Consumes a newly confirmed selection index (returns true once per selection).
    bool takeSelection(int& outIndex) noexcept;

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal,
        bool isClockwork,
        float timeSec = 0.0f
    );

    // Input routing (returns true if event was absorbed by the modal)
    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY,
        AudioFoleyEngine& foley
    );

    bool handleMouseMove(float mouseX, float mouseY);
    bool handleScroll(float yoffset);
    bool handleKey(int key, int action, AudioFoleyEngine& foley);

private:
    static constexpr float kHeaderH = 40.0f;
    static constexpr float kRowH = 30.0f;
    static constexpr float kRowGap = 4.0f;
    static constexpr float kListPadTop = 8.0f;
    static constexpr float kListPadBottom = 8.0f;
    static constexpr float kOuterPadX = 12.0f;
    static constexpr float kFooterPad = 10.0f;

    void scrollIndexIntoView(int index, bool center, bool animate = true) noexcept;
    [[nodiscard]] int hitTestRow(float mouseX, float mouseY) const noexcept;

    bool isOpen_{false};
    std::string title_;
    std::vector<OptionListItem> items_;
    int selectedIndex_{-1};
    int focusedIndex_{-1};
    int pendingSelection_{-1};
    bool hasPendingSelection_{false};
    bool scrollToSelectedPending_{false};

    // Layout bounds (updated during render)
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{340.0f};
    float dialogH_{240.0f};

    float listX_{0.0f};
    float listY_{0.0f};
    float listW_{316.0f};
    float listH_{180.0f};

    // Scroll state
    KineticScroller scroller_{};
    float sbTrackY_{0.0f};
    float sbTrackH_{0.0f};
    float sbThumbH_{24.0f};

    // Pointer / scrollbar drag state
    bool draggingScrollbar_{false};
    int pressedRow_{-1};
};

} // namespace solengine

#endif // SOLENGINE_OPTION_LIST_DIALOG_HPP
