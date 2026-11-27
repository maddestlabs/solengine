#ifndef SOLENGINE_SETTINGS_DIALOG_HPP
#define SOLENGINE_SETTINGS_DIALOG_HPP

#include "solengine/settings_manager.hpp"
#include "solengine/ui/option_list_dialog.hpp"
#include "solengine/ui/kinetic_scroller.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <functional>
#include <string>
#include <vector>

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

    void open(const std::string& activeGameId = "", const std::string& activeGameName = "");
    void close();
    void update(float dt) noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }
    [[nodiscard]] bool isPeekingPostFx() const noexcept { return peekingPostFx_; }
    void setLowCapabilityHardware(bool lowCap) noexcept { lowCapabilityHardware_ = lowCap; }
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
    std::function<void(const AppSettings&)> onPreviewDeck;
    std::function<void()> onOpenGameBrowser;
    std::function<void(const std::string& gameId)> onSelectFavoriteGame;

private:
    void randomizeSeed();
    void applyLivePreview(SolitaireBoard& board, AudioFoleyEngine& foley, const AppSettings& s, bool updatePreviewCard = true);
    void revertLiveState(SolitaireBoard& board, AudioFoleyEngine& foley);
    void commitSettings();
    void openOptionPicker(
        std::string title,
        std::vector<OptionListItem> items,
        int selectedIndex,
        std::function<void(int, SolitaireBoard&, AudioFoleyEngine&)> onApply
    );

    bool isOpen_{false};
    bool livePreviewDirty_{false};
    int currentTab_{0}; // 0: Game, 1: Deck, 2: Scene, 3: Display, 4: Audio
    std::string activeGameId_;
    std::string activeGameName_;
    std::string selectedPresetId_;
    bool perGameScope_{false};
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
    bool draggingPostFxIntensity_{false};
    bool peekingPostFx_{false};
    bool lowCapabilityHardware_{false};
    bool draggingDarkness_{false};
    bool draggingLightStrength_{false};
    bool draggingGoboSoftness_{false};
    bool draggingDustMotes_{false};
    bool draggingLightWaver_{false};
    bool draggingLightWaverSpeed_{false};
    bool draggingLightFlicker_{false};
    bool draggingCloudOpacity_{false};
    float goboSoftTrackX_{0.0f}, goboSoftTrackY_{0.0f}, goboSoftTrackW_{280.0f}, goboSoftTrackH_{24.0f};
    float dustMotesTrackX_{0.0f}, dustMotesTrackY_{0.0f}, dustMotesTrackW_{280.0f}, dustMotesTrackH_{24.0f};
    float lightWaverTrackX_{0.0f}, lightWaverTrackY_{0.0f}, lightWaverTrackW_{280.0f}, lightWaverTrackH_{24.0f};
    float lightWaverSpeedTrackX_{0.0f}, lightWaverSpeedTrackY_{0.0f}, lightWaverSpeedTrackW_{280.0f}, lightWaverSpeedTrackH_{24.0f};
    float lightFlickerTrackX_{0.0f}, lightFlickerTrackY_{0.0f}, lightFlickerTrackW_{280.0f}, lightFlickerTrackH_{24.0f};
    float cloudOpacityTrackX_{0.0f}, cloudOpacityTrackY_{0.0f}, cloudOpacityTrackW_{280.0f}, cloudOpacityTrackH_{24.0f};
    float postFxTrackX_{0.0f};
    float postFxTrackY_{0.0f};
    float postFxTrackW_{280.0f};
    float postFxTrackH_{26.0f};
    float postFxAffectUiY_{0.0f};
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

    // Scrollable tab content state (kinetic iOS-style scrolling across tabs 0-3)
    KineticScroller contentScroller_{};
    float contentRectX_{0.0f};
    float contentRectY_{0.0f};
    float contentRectW_{0.0f};
    float contentRectH_{0.0f};
    bool draggingThemeScrollbar_{false};
    float themeSbTrackY_{0.0f};
    float themeSbTrackH_{0.0f};
    float themeSbThumbH_{20.0f};

    // Nested scrollable list picker for "< option >" steppers
    OptionListDialog optionPicker_;
    std::function<void(int, SolitaireBoard&, AudioFoleyEngine&)> pendingOptionApply_;
};

} // namespace solengine

#endif // SOLENGINE_SETTINGS_DIALOG_HPP

