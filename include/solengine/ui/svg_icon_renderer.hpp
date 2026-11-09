#ifndef SOLENGINE_SVG_ICON_RENDERER_HPP
#define SOLENGINE_SVG_ICON_RENDERER_HPP

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace solengine {

namespace vector_icon {

// High-performance procedural 2D vector primitives for WebGPU & native rendering
void drawStar(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float outerRadius,
    float fillR, float fillG, float fillB, float fillA,
    float outlineR, float outlineG, float outlineB, float outlineA,
    float strokeWidth = 1.2f
);

void drawChevron(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float size,
    int direction, // 0 = left, 1 = right, 2 = up, 3 = down
    float red, float green, float blue, float alpha,
    float strokeWidth = 1.8f
);

} // namespace vector_icon

enum class SvgIconId : uint16_t {
    None = 0,
    NewDeal,
    Restart,
    Variant,
    Theme,
    Deck,
    CardStock,
    CardCorners,
    CardWear,
    Undo,
    Redo,
    Hint,
    AutoFinish,
    MenuAnchor,
    Settings,
    Dice,
    VolumeHigh,
    VolumeMute,
    Close,
    Check,
    Copy,
    ChevronLeft,
    ChevronRight,
    Star,
    StarOutline,
    Play,
    Pause,
    FastForward,
    StepForward,
    StepBackward,
    AutoPlay,
    Count
};

class SvgIconRenderer {
public:
    static SvgIconRenderer& instance();

    // Render a predefined stock SVG icon by ID, tinted with (r, g, b, a)
    void renderIcon(
        eatsbits::ui::BatchRenderer2D& r,
        SvgIconId iconId,
        float x,
        float y,
        float w,
        float h,
        float red,
        float green,
        float blue,
        float alpha = 1.0f
    );

    // Render an arbitrary SVG XML fragment directly onto the batch renderer
    void renderSvgXml(
        eatsbits::ui::BatchRenderer2D& r,
        const std::string& svgXml,
        float x,
        float y,
        float w,
        float h,
        float red,
        float green,
        float blue,
        float alpha = 1.0f
    );

    // Get the SVG XML source code for a stock icon
    [[nodiscard]] static const char* getStockSvgXml(SvgIconId iconId) noexcept;

private:
    SvgIconRenderer();
    ~SvgIconRenderer();

    struct CachedIconTexture {
        int width{0};
        int height{0};
        std::vector<uint8_t> alphaMask; // Single-channel 8-bit coverage mask
    };

    const CachedIconTexture* getOrRasterize(SvgIconId id, int targetW, int targetH);
    const CachedIconTexture* getOrRasterizeXml(const std::string& xml, int targetW, int targetH);

    std::unordered_map<uint64_t, CachedIconTexture> stockCache_;
    std::unordered_map<std::string, CachedIconTexture> xmlCache_;
    std::vector<uint32_t> blitBuffer_;
    void* rasterizer_{nullptr};
};

} // namespace solengine

#endif // SOLENGINE_SVG_ICON_RENDERER_HPP

