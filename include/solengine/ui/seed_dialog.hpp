#ifndef SOLENGINE_SEED_DIALOG_HPP
#define SOLENGINE_SEED_DIALOG_HPP

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <functional>
#include <string>
#include <cstdint>

namespace solengine {

class SolitaireBoard;
class AudioFoleyEngine;
struct ThemePalette;

class SeedDialog {
public:
    SeedDialog();
    ~SeedDialog() = default;

    void open(uint32_t currentSeed, const std::string& variantName);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal,
        float timeSec,
        void* nativeWindow = nullptr
    );

    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley,
        void* nativeWindow = nullptr
    );

    bool handleMouseMove(float mouseX, float mouseY);

    bool handleKey(
        int key,
        int action,
        int mods,
        SolitaireBoard& board,
        AudioFoleyEngine& foley,
        void* nativeWindow = nullptr
    );

    bool handleChar(unsigned int codepoint);

    std::function<void(uint32_t newSeed)> onDealSeed;

private:
    void randomizeSeed();
    void copyToClipboard(void* nativeWindow);
    void pasteFromClipboard(void* nativeWindow);
    void commitDeal(SolitaireBoard& board, AudioFoleyEngine& foley);

    bool isOpen_{false};
    uint32_t seed_{2026u};
    std::string seedText_{"2026"};
    std::string variantName_{"Klondike"};
    bool editingSeed_{true};
    float cursorBlink_{0.0f};
    float copyFeedbackTimer_{0.0f};

    // Dialog layout bounds
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{480.0f};
    float dialogH_{290.0f};
};

} // namespace solengine

#endif // SOLENGINE_SEED_DIALOG_HPP

