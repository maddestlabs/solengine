#ifndef SOLENGINE_TABLE_RENDERER_HPP
#define SOLENGINE_TABLE_RENDERER_HPP

#include "solengine/solitaire_board.hpp"
#include "solengine/audio_foley.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <cstdint>
#include <string>

namespace solengine {

/**
 * Screen anchor position for the consolidated floating hover menu.
 */
enum class MenuAnchor : uint8_t {
    BottomLeft = 0,
    BottomRight = 1,
    TopLeft = 2,
    TopRight = 3
};

struct ThemePalette {
    float woodR0{0.30f}, woodG0{0.16f}, woodB0{0.08f};
    float woodR1{0.16f}, woodG1{0.08f}, woodB1{0.04f};
    float trimR{0.85f}, trimG{0.72f}, trimB{0.30f};
    float feltR0{0.07f}, feltG0{0.25f}, feltB0{0.13f};
    float feltR1{0.03f}, feltG1{0.14f}, feltB1{0.07f};
    float cardBackR0{0.08f}, cardBackG0{0.22f}, cardBackB0{0.12f};
    float cardBackR1{0.04f}, cardBackG1{0.12f}, cardBackB1{0.06f};
    float accentR{0.40f}, accentG{0.90f}, accentB{0.50f};
};

ThemePalette getPalette(TableThemeId theme) noexcept;

/**
 * 100% Procedural Hardwood 3.0 & Enshaded Solitaire Renderer.
 * Synthesizes carved hardwood frames, baize/obsidian surfaces, directional drop shadows,
 * parchment cards, 2.5D card flips, vector suit pips, geometric card backs, and interactive HUD chrome.
 */
class TableRenderer {
public:
    TableRenderer() = default;
    ~TableRenderer() = default;

    static void renderBoard(
        eatsbits::ui::BatchRenderer2D& r,
        const SolitaireBoard& board,
        float width,
        float height,
        int draggedPile = -1,
        int draggedCardIdx = -1,
        float dragOffsetX = 0.0f,
        float dragOffsetY = 0.0f,
        int hoveredButtonId = -1,
        bool menuOpen = false,
        MenuAnchor menuAnchor = MenuAnchor::BottomLeft,
        bool isGesturing = false,
        float gestureStartX = 0.0f,
        float gestureStartY = 0.0f,
        float gestureCurX = 0.0f,
        float gestureCurY = 0.0f,
        const eatsbits::ui::LightSource2D* dynamicLight = nullptr,
        const class AutoPlayController* autoPlay = nullptr
    );

    static bool renderToPng(
        SolitaireBoard& board,
        uint32_t width,
        uint32_t height,
        const std::string& outputPngPath,
        int aaMode = 4
    );

    static int runInteractiveWindow(
        SolitaireBoard& board,
        AudioFoleyEngine& foley,
        uint32_t width = 1280,
        uint32_t height = 720,
        int initialAaMode = 4
    );

    static bool writePngFile(
        const std::string& filePath,
        uint32_t width,
        uint32_t height,
        const uint8_t* rgbaPixels
    );
};

/**
 * Load a card deck (.eats, .png, .jpg, .jpeg, .svg) or custom table theme (.eats) into the active interactive session.
 */
bool loadFile(const std::string& filePath);

} // namespace solengine

#endif // SOLENGINE_TABLE_RENDERER_HPP
