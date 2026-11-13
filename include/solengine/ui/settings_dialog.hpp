#ifndef SOLENGINE_SETTINGS_DIALOG_HPP
#define SOLENGINE_SETTINGS_DIALOG_HPP

#include "solengine/settings_manager.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <functional>
#include <string>

namespace solengine {

class SolitaireBoard;
class AudioFoleyEngine;
struct ThemePalette;

/**
 * General-purpose responsive modal dialog overlay with backdrop darkening,
 * tabbed settings navigation, seed editing/randomization, deck & theme customizer,
 * and audio controls. Leans into SolEngine's SVG icon system.
 */
class SettingsDialog {
public:
    SettingsDialog();
    ~SettingsDialog() = default;

    void open();
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }
    [[nodiscard]] const AppSettings& previewSettings() const noexcept { return tempSettings_; }

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal,
        float timeSec = 0.0f
    );

    // Input routing (returns true if event was absorbed by the modal)
    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleMouseMove(
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleKey(
        int key,
        int action,
        int mods,
        SolitaireBoard& board,
        AudioFoleyEngine& foley
    );

    bool handleChar(unsigned int codepoint);
    bool handleScroll(float yoffset);

    std::function<void()> onSettingsChanged;
    std::function<void()> onOpenGameBrowser;
    std::function<void(const std::string& gameId)> onSelectFavoriteGame;

private:
    void randomizeSeed();
    void revertLiveState(SolitaireBoard& board, AudioFoleyEngine& foley);

    bool isOpen_{false};
    int currentTab_{0}; // 0: Game & Seed, 1: Aesthetics, 2: Audio & Display
    AppSettings tempSettings_{};
    std::string seedText_{"2026"};
    bool editingSeed_{false};

    // Cached dialog bounds for hit testing
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{640.0f};
    float dialogH_{440.0f};

    // Slider dragging states
    bool draggingVolume_{false};
    bool draggingAmbienceVolume_{false};
    bool draggingDeviation_{false};
    bool draggingDarkness_{false};
    bool draggingLightStrength_{false};
    float volTrackX_{0.0f};
    float volTrackY_{0.0f};
    float volTrackW_{280.0f};
    float volTrackH_{26.0f};
    float ambVolTrackX_{0.0f};
    float ambVolTrackY_{0.0f};
    float ambVolTrackW_{220.0f};
    float ambVolTrackH_{26.0f};
    float devTrackX_{0.0f};
    float devTrackY_{0.0f};
    float devTrackW_{280.0f};
    float devTrackH_{26.0f};
    float darkTrackX_{0.0f};
    float darkTrackY_{0.0f};
    float darkTrackW_{280.0f};
    float darkTrackH_{26.0f};
    float lightStrTrackX_{0.0f};
    float lightStrTrackY_{0.0f};
    float lightStrTrackW_{280.0f};
    float lightStrTrackH_{26.0f};

    // Themes tab vertical scroll state (for mobile/compact viewports)
    float themeScrollY_{0.0f};
    float themeMaxScrollY_{0.0f};
    float contentRectX_{0.0f};
    float contentRectY_{0.0f};
    float contentRectW_{0.0f};
    float contentRectH_{0.0f};
    bool pointerDownInContent_{false};
    bool draggingThemeScroll_{false};
    float dragStartY_{0.0f};
    float dragLastY_{0.0f};
};

} // namespace solengine

#endif // SOLENGINE_SETTINGS_DIALOG_HPP

