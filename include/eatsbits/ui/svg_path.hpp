#ifndef EATS_SVG_PATH_HPP
#define EATS_SVG_PATH_HPP

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace eatsbits::ui {

/**
 * 2D vector coordinate primitive.
 */
struct SvgPoint {
    float x{0.0f};
    float y{0.0f};

    constexpr SvgPoint() = default;
    constexpr SvgPoint(float px, float py) : x(px), y(py) {}

    bool operator==(const SvgPoint& o) const noexcept {
        return std::abs(x - o.x) < 1e-5f && std::abs(y - o.y) < 1e-5f;
    }
};

/**
 * 2D Axis-Aligned Bounding Box.
 */
struct SvgRect {
    float minX{1e9f};
    float minY{1e9f};
    float maxX{-1e9f};
    float maxY{-1e9f};

    [[nodiscard]] constexpr float width() const noexcept { return isEmpty() ? 0.0f : maxX - minX; }
    [[nodiscard]] constexpr float height() const noexcept { return isEmpty() ? 0.0f : maxY - minY; }
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return minX > maxX || minY > maxY; }
    [[nodiscard]] constexpr SvgPoint center() const noexcept {
        return {(minX + maxX) * 0.5f, (minY + maxY) * 0.5f};
    }

    void expandToInclude(const SvgPoint& p) noexcept {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
};

/**
 * Declarative vector layer definition matching Eatsbeats' SvgLayerDef.
 * Used for custom instrument/FX panel watermarks, vector skins, and logos.
 */
struct SvgLayerDef {
    std::string path;
    uint32_t color{0xFFFFFFFF}; // RGBA8 packed
    float opacity{1.0f};
    bool isFill{true};
    float strokeWidth{1.0f};

    SvgLayerDef() = default;
    SvgLayerDef(std::string p, uint32_t c, float op = 1.0f, bool fill = true, float stroke = 1.0f)
        : path(std::move(p)), color(c), opacity(op), isFill(fill), strokeWidth(stroke) {}
};

/**
 * High-performance, zero-allocation SVG path parser, polygon tessellator,
 * and software rasterizer.
 * Supports standard SVG path syntax: M, m, L, l, H, h, V, v, C, c, S, s, Q, q, T, t, A, a, Z, z.
 */
class SvgPath {
public:
    SvgPath() = default;

    /**
     * Parses an SVG path data string (`d="..."`) into native discretized contours.
     * Caches parsed results in-memory so identical path definitions incur zero overhead.
     */
    static SvgPath parse(std::string_view d);

    /**
     * Clears the in-memory path cache.
     */
    static void clearCache();

    [[nodiscard]] const std::vector<std::vector<SvgPoint>>& getContours() const noexcept { return contours_; }
    [[nodiscard]] const SvgRect& getBounds() const noexcept { return bounds_; }
    [[nodiscard]] bool empty() const noexcept { return contours_.empty(); }

    /**
     * Tessellates all closed contours into triangles using ear-clipping.
     * Output is a flat vector of vertices where every 3 points form a triangle.
     */
    [[nodiscard]] std::vector<SvgPoint> triangulate() const;

    /**
     * Rasterizes the path into a 32-bit RGBA pixel buffer at the specified dimensions.
     * Scaled and centered to fit within (width, height) with optional padding.
     */
    [[nodiscard]] std::vector<uint8_t> rasterize(int width, int height,
                                                 uint32_t color = 0xFFFFFFFF,
                                                 bool isFill = true,
                                                 float strokeWidth = 1.0f,
                                                 float padding = 0.0f,
                                                 const SvgRect* customBounds = nullptr) const;

    /**
     * Adds an explicit contour of points.
     */
    void addContour(std::vector<SvgPoint> contour);

private:
    std::vector<std::vector<SvgPoint>> contours_;
    SvgRect bounds_{0.0f, 0.0f, 0.0f, 0.0f};

    void computeBounds();
};


/**
 * Canonical Eatsbits Logo geometry represented in 100% pure SVG paths.
 * No <rect> or <circle> elements used.
 */
struct SvgLogo {
    // 1. Amber Chassis Square with rx=40 rounded corners (256x256 coordinate system)
    static constexpr const char* kAmberChassisPath =
        "M 56 16 L 200 16 A 40 40 0 0 1 240 56 L 240 200 A 40 40 0 0 1 200 240 "
        "L 56 240 A 40 40 0 0 1 16 200 L 16 56 A 40 40 0 0 1 56 16 Z";

    // 2. Weathered Dark Monster Head, Open Mouth & Two Eating Bits
    static constexpr const char* kMonsterHeadAndBitsPath =
        "M 50 50 L 190 50 L 190 110 L 110 110 L 110 170 L 190 170 L 190 205 L 50 205 Z "
        "M 203.5 115 L 220.5 115 A 3.5 3.5 0 0 1 224 118.5 L 224 135.5 A 3.5 3.5 0 0 1 220.5 139 L 203.5 139 A 3.5 3.5 0 0 1 200 135.5 L 200 118.5 A 3.5 3.5 0 0 1 203.5 115 Z "
        "M 203 160 L 215 160 A 3 3 0 0 1 218 163 L 218 175 A 3 3 0 0 1 215 178 L 203 178 A 3 3 0 0 1 200 175 L 200 163 A 3 3 0 0 1 203 160 Z";

    // 3. Amber Monster Eye circular arc (center 130, 80, radius 16)
    static constexpr const char* kMonsterEyePath =
        "M 114 80 A 16 16 0 1 0 146 80 A 16 16 0 1 0 114 80 Z";

    // 4. Authentic Unboxed Silk-Screened Faceplate Logo (4 contours: Head, Eye cutout, Bit 1, Bit 2)
    static constexpr const char* kPlainLogoPath =
        "M 50 50 L 190 50 L 190 110 L 110 110 L 110 170 L 190 170 L 190 205 L 50 205 Z "
        "M 114 80 A 16 16 0 1 0 146 80 A 16 16 0 1 0 114 80 Z "
        "M 203.5 115 L 220.5 115 A 3.5 3.5 0 0 1 224 118.5 L 224 135.5 A 3.5 3.5 0 0 1 220.5 139 L 203.5 139 A 3.5 3.5 0 0 1 200 135.5 L 200 118.5 A 3.5 3.5 0 0 1 203.5 115 Z "
        "M 203 160 L 215 160 A 3 3 0 0 1 218 163 L 218 175 A 3 3 0 0 1 215 178 L 203 178 A 3 3 0 0 1 200 175 L 200 163 A 3 3 0 0 1 203 160 Z";

    // 5. Two Eating Bits
    static constexpr const char* kMonsterBitsPath =
        "M 203.5 115 L 220.5 115 A 3.5 3.5 0 0 1 224 118.5 L 224 135.5 A 3.5 3.5 0 0 1 220.5 139 L 203.5 139 A 3.5 3.5 0 0 1 200 135.5 L 200 118.5 A 3.5 3.5 0 0 1 203.5 115 Z "
        "M 203 160 L 215 160 A 3 3 0 0 1 218 163 L 218 175 A 3 3 0 0 1 215 178 L 203 178 A 3 3 0 0 1 200 175 L 200 163 A 3 3 0 0 1 203 160 Z";

    /**
     * Color definitions:
     * Warm Amber: #FF8C00 (R=255, G=140, B=0, A=255)
     * Weathered Dark: #141210 (R=20, G=18, B=16, A=255)
     */
    static constexpr uint32_t kColorAmber = 0xFF008CFF; // ABGR/RGBA packed (R=0xFF, G=0x8C, B=0x00, A=0xFF)
    static constexpr uint32_t kColorDark  = 0xFF101214; // ABGR/RGBA packed (R=0x14, G=0x12, B=0x10, A=0xFF)

    /**
     * Returns the 3-layer vector stack for the Eatsbits brand logo.
     */
    static std::vector<SvgLayerDef> getLayers();

    /**
     * Returns the inverted vector stack for hover state.
     */
    static std::vector<SvgLayerDef> getInvertedLayers();

    /**
     * Returns the unboxed plain vector stack matching the vintage hardware faceplate silk-screen.
     */
    static std::vector<SvgLayerDef> getPlainLayers(uint32_t color = kColorAmber);

    /**
     * Returns the inverted unboxed plain vector stack for hover state.
     */
    static std::vector<SvgLayerDef> getPlainInvertedLayers(uint32_t accentColor = kColorAmber, uint32_t darkColor = kColorDark);
};

} // namespace eatsbits::ui

#endif // EATS_SVG_PATH_HPP
