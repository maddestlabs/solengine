#ifndef EATS_BATCH_RENDERER_2D_HPP
#define EATS_BATCH_RENDERER_2D_HPP

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include "eatsbits/ui/geometry.hpp"

namespace eatsbits::ui {

/**
 * @brief Represents a single 2D vertex for batched UI rendering.
 * Suitable for both desktop Modern OpenGL (VBO) and WebGPU (WGSL pipeline).
 */
struct alignas(16) Vertex2D {
    float x{0.0f};
    float y{0.0f};
    float u{0.0f};
    float v{0.0f};
    uint32_t color{0xFFFFFFFF}; // RGBA8 packed
    uint32_t mode{0};          // 0 = solid/gradient color, 1 = font atlas texture, 2 = analytical, 3 = rgba texture, 4 = monospace font atlas, 5 = opaque mesh, 6 = bg texture
    float pad[2]{0.0f, 0.0f};  // 16-byte aligned auxiliary parameters (scratch distortion / SDF bounds)
    float localUV[2]{0.0f, 0.0f}; // [0, 1] normalized element-relative coordinate for GPU fragment lighting & bevels
};

namespace VertexMode {
    inline constexpr uint32_t Color = 0;
    inline constexpr uint32_t FontAtlas = 1;
    inline constexpr uint32_t AnalyticalSdf = 2;
    inline constexpr uint32_t RgbaTexture = 3;
    inline constexpr uint32_t MonospaceAtlas = 4;
    inline constexpr uint32_t OpaqueMeshNoAA = 5;
    inline constexpr uint32_t BgTexture = 6;
}

enum class BlendMode : uint8_t {
    Normal = 0,
    Multiply,
    Screen,
    Add,
    Overlay,
    SoftLight
};

enum class RenderBackendType {
    Filament,
    WebGPU,
    Headless
};

enum class VectorPatternType : uint8_t {
    None = 0,
    HorizontalSlats, // 1D separable scanline ribs / milled slats
    DiagonalMesh,    // Carbon fiber / Kevlar diagonal cross-hatch
    HexHoneycomb,    // Perforated hex grid
    DotGrid,         // Synth breadboard / stipple matrix
    CircuitTraces,   // Silkscreen PCB / synth circuit traces
    BrushedSteel,    // Multi-pass hairline brushed metal with micro-abrasions
    WoodGrain,       // Organic wavy growth-ring lines (Walnut / Rosewood)
    SpeakerGrill,    // Vintage amplifier diamond / woven fabric grill
    VintagePatina    // Physical finish aging with deterministic dust specks and micro-scratches
};

class IBatchRenderBackend {
public:
    virtual ~IBatchRenderBackend() = default;
    virtual bool initialize(void* windowHandle, uint32_t width, uint32_t height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void beginPass(float width, float height) = 0;
    virtual void updateFontTexture(int x, int y, int w, int h, const unsigned char* data, int atlasW, int atlasH) = 0;
    virtual void renderBatch(const std::vector<Vertex2D>& vertices) = 0;
    virtual void endPass() = 0;
    virtual RenderBackendType getBackendType() const noexcept = 0;
    virtual const uint32_t* getFramebuffer() const noexcept { return nullptr; }
    virtual uint32_t getFramebufferWidth() const noexcept { return 0; }
    virtual uint32_t getFramebufferHeight() const noexcept { return 0; }
    virtual void setDirectPresent(bool /*enable*/) noexcept {}
    virtual void setAntiAliasingMode(int /*mode*/) {}
    virtual void setBlendMode(BlendMode /*mode*/) {}
    virtual void renderRgba(float /*x*/, float /*y*/, float /*w*/, float /*h*/, const uint8_t* /*rgba*/, int /*imgW*/, int /*imgH*/, float /*opacity*/) {}
    virtual void renderRgbaRegion(float dstX, float dstY, float dstW, float dstH,
                                  const uint8_t* rgba, int imgW, int imgH,
                                  float u0, float v0, float u1, float v1,
                                  float opacity = 1.0f) {
        if (std::abs(u0) < 0.001f && std::abs(v0) < 0.001f &&
            std::abs(u1 - 1.0f) < 0.001f && std::abs(v1 - 1.0f) < 0.001f) {
            renderRgba(dstX, dstY, dstW, dstH, rgba, imgW, imgH, opacity);
        }
    }
    virtual void applyBackdropBlur(float /*radius*/ = 3.0f, float /*dimFactor*/ = 0.50f) {}
    virtual void setGlobalLight(const LightSource2D& /*light*/,
                                float /*colR*/ = 1.04f, float /*colG*/ = 1.00f, float /*colB*/ = 0.94f,
                                float /*ambR*/ = 0.35f, float /*ambG*/ = 0.35f, float /*ambB*/ = 0.35f,
                                float /*sceneDarkness*/ = 0.0f, float /*lightStrength*/ = 1.0f) {}
    virtual void setTableSurfaceLighting(float /*lacquerStrength*/, float /*bumpStrength*/ = 2.4f) {}
    virtual void invalidateBgTexture() {}
    virtual void setShaderTime(float /*timeSeconds*/) {}
    virtual void setScissorRect(float /*x*/, float /*y*/, float /*w*/, float /*h*/) {}
    virtual void clearScissorRect() {}
    virtual void* getNativeDevice() const noexcept { return nullptr; }
    virtual void* getNativeSurface() const noexcept { return nullptr; }
    virtual void setCustomRenderTargetView(void* /*view*/) {}
    virtual void snapshotFramebuffer(std::vector<uint32_t>& outPixels, uint32_t& outW, uint32_t& outH) const {
        const uint32_t* fb = getFramebuffer();
        if (fb && outW > 0 && outH > 0) {
            outPixels.assign(fb, fb + (static_cast<size_t>(outW) * outH));
        }
    }
    virtual void restoreFramebuffer(const uint32_t* /*srcPixels*/, uint32_t /*w*/, uint32_t /*h*/, float /*opacity*/ = 1.0f) {}
    virtual void restoreFramebufferRegion(const uint32_t* /*srcPixels*/, uint32_t /*srcW*/, uint32_t /*srcH*/,
                                          float /*dstX*/, float /*dstY*/, float /*srcX*/, float /*srcY*/, float /*w*/, float /*h*/, float /*opacity*/ = 1.0f) {}
    virtual void renderConvexPolygon(const Point2D* /*pts*/, size_t /*count*/, uint32_t /*col0*/, uint32_t /*col1*/,
                                     Point2D /*gradP0*/, Point2D /*gradP1*/) {}
    virtual void renderTexturedConvexPolygon(
        const Point2D* /*inputPts*/, size_t /*count*/,
        const uint8_t* /*rgba*/, int /*imgW*/, int /*imgH*/,
        Point2D /*p0*/, Point2D /*p1*/, Point2D /*p2*/, Point2D /*p3*/,
        float /*w0*/, float /*w1*/, float /*w2*/, float /*w3*/,
        float /*lightShade*/ = 1.0f
    ) {}
    virtual void renderTexturedConvexPolygonARGB(
        const Point2D* /*inputPts*/, size_t /*count*/,
        const uint32_t* /*argb32*/, int /*imgW*/, int /*imgH*/,
        Point2D /*p0*/, Point2D /*p1*/, Point2D /*p2*/, Point2D /*p3*/,
        float /*w0*/, float /*w1*/, float /*w2*/, float /*w3*/,
        float /*lightShade*/ = 1.0f
    ) {}
    virtual void renderTexturedRoundedRect(
        float /*x*/, float /*y*/, float /*w*/, float /*h*/, float /*radius*/,
        const uint32_t* /*argb32*/, int /*imgW*/, int /*imgH*/,
        float /*opacity*/ = 1.0f
    ) {}
    virtual void updateRgbaTexture(int /*x*/, int /*y*/, int /*w*/, int /*h*/, const uint8_t* /*data*/, int /*atlasW*/, int /*atlasH*/) {}
};

class BatchRenderer2D;

/**
 * @brief RAII Scissor Scope for safe, exception-resilient nested clipping in BatchRenderer2D.
 * Pushes a scissor rect on construction, automatically intersects with active scissor stack,
 * and restores previous scissor state or clears it on destruction.
 */
class ScissorScope {
public:
    ScissorScope() noexcept = default;
    ScissorScope(BatchRenderer2D* renderer, float x, float y, float w, float h);
    ScissorScope(BatchRenderer2D* renderer, const Rect2D& r);
    ScissorScope(BatchRenderer2D& renderer, float x, float y, float w, float h);
    ScissorScope(BatchRenderer2D& renderer, const Rect2D& r);

    ~ScissorScope();

    // Non-copyable
    ScissorScope(const ScissorScope&) = delete;
    ScissorScope& operator=(const ScissorScope&) = delete;

    // Movable
    ScissorScope(ScissorScope&& other) noexcept;
    ScissorScope& operator=(ScissorScope&& other) noexcept;

    /// Early release of the scissor scope
    void reset() noexcept;

    [[nodiscard]] bool isActive() const noexcept { return renderer_ != nullptr; }

private:
    BatchRenderer2D* renderer_{nullptr};
};

/**
 * @brief RAII Scoped Anti-Aliasing Guard for temporarily overriding AA mode.
 * Flushes active batch on construction, sets new AA mode, and flushes and
 * restores previous AA mode on destruction.
 */
class ScopedAntiAliasing {
public:
    ScopedAntiAliasing() noexcept = default;
    ScopedAntiAliasing(BatchRenderer2D* renderer, int tempMode);
    ScopedAntiAliasing(BatchRenderer2D& renderer, int tempMode);
    ~ScopedAntiAliasing();

    ScopedAntiAliasing(const ScopedAntiAliasing&) = delete;
    ScopedAntiAliasing& operator=(const ScopedAntiAliasing&) = delete;

    ScopedAntiAliasing(ScopedAntiAliasing&& other) noexcept;
    ScopedAntiAliasing& operator=(ScopedAntiAliasing&& other) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool isActive() const noexcept { return renderer_ != nullptr; }

private:
    BatchRenderer2D* renderer_{nullptr};
    int previousMode_{2};
};

/**
 * @brief High-performance batched 2D vector renderer.
 * Collects 2D quads, circles, lines, and textured font glyphs into a single vertex buffer,
 * flushing them to the GPU in minimal draw passes.
 */
class BatchRenderer2D {
public:
    using ScissorScope = eatsbits::ui::ScissorScope;
    using ScopedAntiAliasing = eatsbits::ui::ScopedAntiAliasing;

    BatchRenderer2D();
    ~BatchRenderer2D();

    bool initialize(void* windowHandle, uint32_t width, uint32_t height, RenderBackendType backend = RenderBackendType::Filament);
    void shutdown();
    void resize(uint32_t width, uint32_t height);

    void beginFrame(float width, float height);
    void endFrame();
    void flush();
    void applyBackdropBlur(float radius = 3.0f, float dimFactor = 0.50f);

    // --- Blend Modes ---
    void setBlendMode(BlendMode mode);
    [[nodiscard]] BlendMode getBlendMode() const noexcept { return blendMode_; }

    // --- Dynamic Lighting & Shading ---
    void setGlobalLight(const LightSource2D& light,
                        float colR = 1.04f, float colG = 1.00f, float colB = 0.94f,
                        float ambR = 0.35f, float ambG = 0.35f, float ambB = 0.35f,
                        float sceneDarkness = 0.0f, float lightStrength = 1.0f);
    void setTableSurfaceLighting(float lacquerStrength, float bumpStrength = 2.4f) noexcept;
    void invalidateBgTexture() noexcept;
    [[nodiscard]] float getTableLacquer() const noexcept { return tableLacquer_; }
    [[nodiscard]] float getTableBump() const noexcept { return tableBump_; }
    void setSceneLightingActive(bool active) noexcept;
    [[nodiscard]] bool isSceneLightingActive() const noexcept { return sceneLightingActive_; }
    void setShaderTime(float timeSeconds);

    // --- Vector Primitives ---
    void drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f);
    void drawTriangleTextured(float x0, float y0, float u0, float v0,
                              float x1, float y1, float u1, float v1,
                              float x2, float y2, float u2, float v2,
                              float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    void drawTexturedPolygon(const Point2D* points, const Point2D* uvs, size_t count,
                             float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f,
                             float scratchDistortion = 0.0f,
                             float conditioningParam = 0.0f,
                             const Point2D* localUVs = nullptr);
    void drawConvexPolygon(const Point2D* points, size_t count, float r, float g, float b, float a = 1.0f);
    void drawConvexPolygonGradient(const Point2D* points, size_t count,
                                   float r0, float g0, float b0,
                                   float r1, float g1, float b1,
                                   Point2D gradP0, Point2D gradP1,
                                   float a = 1.0f);
    void drawTexturedConvexPolygon(
        const Point2D* inputPts, size_t count,
        const uint8_t* rgba, int imgW, int imgH,
        Point2D p0, Point2D p1, Point2D p2, Point2D p3,
        float w0 = 1.0f, float w1 = 1.0f, float w2 = 1.0f, float w3 = 1.0f,
        float lightShade = 1.0f
    );
    void drawTexturedConvexPolygon(
        const Point2D* inputPts, size_t count,
        const uint32_t* argb32, int imgW, int imgH,
        Point2D p0, Point2D p1, Point2D p2, Point2D p3,
        float w0 = 1.0f, float w1 = 1.0f, float w2 = 1.0f, float w3 = 1.0f,
        float lightShade = 1.0f
    );
    void drawTexturedRoundedRect(
        float x, float y, float w, float h, float radius,
        const uint32_t* argb32, int imgW, int imgH,
        float opacity = 1.0f
    );
    void updateRgbaTexture(int x, int y, int w, int h, const uint8_t* data, int atlasW, int atlasH);
    void drawTexturedRoundedRectGpu(
        float x, float y, float w, float h, float radius,
        float u0, float v0, float u1, float v1,
        float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f,
        float scratchDistortion = 0.0f,
        float conditioningParam = 0.0f
    );
    void drawQuadTextured3D(
        float x0, float y0, float u0, float v0,
        float x1, float y1, float u1, float v1,
        float x2, float y2, float u2, float v2,
        float x3, float y3, float u3, float v3,
        float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f
    );
    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
    void drawRectGradient(float x, float y, float w, float h, float r0, float g0, float b0, float r1, float g1, float b1, float a = 1.0f);
    void drawRectOutline(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f, float lineWidth = 1.0f);
    void drawRoundedRect(float x, float y, float w, float h, float radius, float r, float g, float b, float a = 1.0f, int cornerSegments = 8);
    void drawRoundedRectGradient(float x, float y, float w, float h, float radius, float r0, float g0, float b0, float r1, float g1, float b1, float a = 1.0f, int cornerSegments = 8);
    void drawRoundedRectOutline(float x, float y, float w, float h, float radius, float r, float g, float b, float a = 1.0f, float lineWidth = 1.0f, int cornerSegments = 8);
    void drawCircle(float cx, float cy, float radius, float r, float g, float b, float a = 1.0f, int segments = 36);
    void drawCircleRadialGradient(float cx, float cy, float radius,
                                  float innerR, float innerG, float innerB, float innerA,
                                  float outerR, float outerG, float outerB, float outerA,
                                  float offX = 0.0f, float offY = 0.0f, int segments = 36);
    void drawCircleRadial3StopGradient(float cx, float cy, float radius,
                                       float innerR, float innerG, float innerB, float innerA,
                                       float midR, float midG, float midB, float midA,
                                       float outerR, float outerG, float outerB, float outerA,
                                       float offX = 0.0f, float offY = 0.0f, float midStop = 0.50f,
                                       int segments = 36);
    void drawCircleLinearGradient(float cx, float cy, float radius,
                                  float r0, float g0, float b0, float a0,
                                  float r1, float g1, float b1, float a1,
                                  float angleRad = 1.5707963f, int segments = 36);
    void drawDeclinedScoopGradient(float cx, float cy, float capRadius,
                                   float currentAngle, float chordDistRatio,
                                   float crestR, float crestG, float crestB, float crestA,
                                   float midR, float midG, float midB, float midA,
                                   float rimR, float rimG, float rimB, float rimA,
                                   float midStop = 0.45f, int segments = 24);
    void drawCircleOutline(float cx, float cy, float radius, float r, float g, float b, float a = 1.0f, float lineWidth = 1.5f, int segments = 36);
    void drawArc(float cx, float cy, float radius, float startAngle, float endAngle, float r, float g, float b, float a = 1.0f, float lineWidth = 2.0f, int segments = 36);
    void drawLine(float x0, float y0, float x1, float y1, float r, float g, float b, float a = 1.0f, float lineWidth = 1.5f);
    void drawCatenaryBezier(Point2D p0, Point2D cp0, Point2D cp1, Point2D p1, float r, float g, float b, float a, float lineWidth);
    void drawPath(const Point2D* points, size_t count, float lineWidth,
                  float r, float g, float b, float a = 1.0f, bool closed = false);
    void drawPathGradient(const Point2D* points, size_t count, float lineWidth,
                          float r0, float g0, float b0, float a0,
                          float r1, float g1, float b1, float a1,
                          bool closed = false, bool parametric = false);
    void drawPathBezier(const Point2D* controlPoints, size_t count, float lineWidth,
                        float r, float g, float b, float a = 1.0f, int segmentsPerCurve = 16);
    void drawPatternedRectGradient(float x, float y, float w, float h,
                                   float r0, float g0, float b0,
                                   float r1, float g1, float b1, float a,
                                   VectorPatternType pattern,
                                   float patternIntensity = 0.08f,
                                   float patternScale = 8.0f,
                                   float cornerRadius = 0.0f);
    void drawRectVignette(float x, float y, float w, float h,
                          float innerAlpha = 0.0f, float outerAlpha = 0.55f,
                          float r = 0.0f, float g = 0.0f, float b = 0.0f);
    void drawDustSpecks(float x, float y, float w, float h,
                        int count = 60, uint32_t seed = 42,
                        float r = 0.85f, float g = 0.85f, float b = 0.90f,
                        float maxAlpha = 0.35f);
    void drawRgbaBitmap(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH, float opacity = 1.0f);
    void drawRgbaBitmapRegion(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH,
                              float u0, float v0, float u1, float v1, float opacity = 1.0f);

    /// Directly append pre-tessellated vector cache vertices with an optional translation offset.
    void drawCachedVertices(const std::vector<Vertex2D>& vertices, float offsetX = 0.0f, float offsetY = 0.0f);

    /// Extracts active uncommitted vertices into an output vector without clearing or flushing.
    void copyActiveVertices(std::vector<Vertex2D>& outVertices) const;

    // --- In-Memory Texture Assets & Filmstrip Knobs/Sliders ---
    struct TextureInfo {
        uint32_t handle{0};
        int width{0};
        int height{0};
        int channels{4};
        bool valid{false};
    };

    uint32_t createTexture(int width, int height, const uint8_t* rgbaPixels);
    uint32_t createTextureFromMemory(const void* data, size_t len);
    uint32_t loadTextureFromUri(std::string_view uriOrPath);
    bool destroyTexture(uint32_t handle);
    [[nodiscard]] bool hasTexture(uint32_t handle) const noexcept;
    [[nodiscard]] TextureInfo getTextureInfo(uint32_t handle) const noexcept;
    [[nodiscard]] const uint8_t* getTexturePixels(uint32_t handle) const noexcept;

    void drawTexturedQuad(uint32_t textureHandle, float x, float y, float w, float h,
                          float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
                          float opacity = 1.0f);

    void drawFilmstripFrame(uint32_t textureHandle, float x, float y, float w, float h,
                            int frameIndex, int totalFrames, bool isHorizontal = false,
                            float opacity = 1.0f);

    static void setActiveBatchRenderer(BatchRenderer2D* renderer);
    [[nodiscard]] static BatchRenderer2D* getActiveBatchRenderer();

    // --- Render Scale (HiDPI / UI Scaling Transform) ---
    void setRenderScale(float sx, float sy) noexcept {
        renderScaleX_ = (sx > 0.001f) ? sx : 1.0f;
        renderScaleY_ = (sy > 0.001f) ? sy : 1.0f;
    }
    [[nodiscard]] float getRenderScaleX() const noexcept { return renderScaleX_; }
    [[nodiscard]] float getRenderScaleY() const noexcept { return renderScaleY_; }
    [[nodiscard]] float getCurrentWidth() const noexcept { return currentWidth_; }
    [[nodiscard]] float getCurrentHeight() const noexcept { return currentHeight_; }

    // --- 2D Rotation Transform ---
    void setRotation(float angleDegrees, float originX, float originY);
    void resetRotation() noexcept;
    [[nodiscard]] bool isTransformActive() const noexcept { return transformActive_; }

    // --- Scissor / Viewport Clipping ---
    void pushScissor(float x, float y, float w, float h);
    void popScissor() noexcept;

    [[nodiscard]] bool hasScissor() const noexcept { return !scissorStack_.empty(); }
    [[nodiscard]] size_t getScissorDepth() const noexcept { return scissorStack_.size(); }
    [[nodiscard]] Rect2D getActiveScissor() const noexcept {
        return scissorStack_.empty() ? Rect2D{0.0f, 0.0f, 0.0f, 0.0f} : scissorStack_.back();
    }
    [[nodiscard]] ScissorScope makeScissorScope(float x, float y, float w, float h);
    [[nodiscard]] ScissorScope makeScissorScope(const Rect2D& r);

    // --- Static Chrome Offscreen Texture (FBO) Caching ---
    void setChromeCachingEnabled(bool enable) noexcept { chromeCacheEnabled_ = enable; }
    [[nodiscard]] bool isChromeCachingEnabled() const noexcept { return chromeCacheEnabled_; }
    [[nodiscard]] bool isChromeCacheValid() const noexcept { return chromeCacheValid_; }
    void invalidateChromeCache() noexcept;
    bool beginChromeRecording();
    void endChromeRecording();
    void drawCachedChrome(float opacity = 1.0f);
    void drawCachedChromeRegion(float dstX, float dstY, float srcX, float srcY, float w, float h, float opacity = 1.0f);
    [[nodiscard]] const uint32_t* getChromeCachePixels() const noexcept { return chromeCachePixels_.data(); }
    [[nodiscard]] size_t getChromeCachePixelCount() const noexcept { return chromeCachePixels_.size(); }
    [[nodiscard]] uint32_t getChromeCacheWidth() const noexcept { return chromeCacheWidth_; }
    [[nodiscard]] uint32_t getChromeCacheHeight() const noexcept { return chromeCacheHeight_; }
    [[nodiscard]] uint64_t getChromeDrawCallsAvoided() const noexcept { return chromeDrawCallsAvoided_; }

    // --- Contextual Shadows & Lighting ---
    void drawCircleDropShadow(float cx, float cy, float radius, float elevation, const LightSource2D& light, float opacity = 0.45f);
    void drawRectDropShadow(float x, float y, float w, float h, float cornerRadius, float elevation, const LightSource2D& light, float opacity = 0.40f,
                            float prevCardX = 99999.0f, float prevCardY = 99999.0f);
    void drawOrientedDropShadowQuad(float x0, float y0, float x1, float y1,
                                    float x2, float y2, float x3, float y3,
                                    float halfW, float halfH, float cornerRadius,
                                    float feather, float opacity);
    void drawKnurledRing(float cx, float cy, float rInner, float rOuter, int numTeeth, float rotationAngle,
                         const LightSource2D& light, float baseR, float baseG, float baseB,
                         float highlightR = 1.0f, float highlightG = 1.0f, float highlightB = 1.0f,
                         float shadowR = 0.08f, float shadowG = 0.09f, float shadowB = 0.11f);
    void drawFlutedRing(float cx, float cy, float rInner, float rOuter, int numFlutes,
                        float fluteDepthRatio, float rotationAngle,
                        const LightSource2D& light, float baseR, float baseG, float baseB,
                        float highlightR = 1.0f, float highlightG = 1.0f, float highlightB = 1.0f,
                        float shadowR = 0.08f, float shadowG = 0.09f, float shadowB = 0.11f);
    void drawChickenHeadContour(float cx, float cy, float radius, float rotationAngle,
                                const LightSource2D& light,
                                float baseR, float baseG, float baseB,
                                float highlightR, float highlightG, float highlightB,
                                float shadowR, float shadowG, float shadowB,
                                float pointerR = 1.0f, float pointerG = 1.0f, float pointerB = 1.0f);

    // --- Font & Texture Atlas ---
    void updateFontAtlas(int x, int y, int w, int h, const unsigned char* data, int atlasW, int atlasH);
    void drawTexturedTriangles(const float* verts, const float* tcoords, const unsigned int* colors, int nverts);
    [[nodiscard]] uint32_t getFontAtlasVersion() const noexcept { return fontAtlasVersion_; }
    void setFontAtlasVersion(uint32_t v) noexcept { fontAtlasVersion_ = v; }

    // --- Monospace Text & High-Throughput Terminal Glyph Pipeline ---
    void initDefaultMonospaceAtlas();
    [[nodiscard]] bool isMonospaceAtlasReady() const noexcept { return monospaceAtlasReady_; }
    void setMonospaceAtlasReady(bool ready) noexcept { monospaceAtlasReady_ = ready; }

    void drawMonospaceCell(float x, float y, float w, float h,
                           char32_t codepoint,
                           uint32_t fgColor,
                           uint32_t bgColor = 0,
                           uint8_t attrs = 0,
                           float subpixelOffsetX = 0.0f,
                           float subpixelOffsetY = 0.0f);

    void drawMonospaceText(float x, float y, float charW, float charH,
                           std::string_view text,
                           uint32_t fgColor,
                           uint32_t bgColor = 0,
                           uint8_t attrs = 0,
                           float subpixelOffsetX = 0.0f,
                           float subpixelOffsetY = 0.0f);

    [[nodiscard]] static bool isBoxDrawingOrBlock(char32_t cp) noexcept;

    void drawTerminalGrid(float startX, float startY, float cellW, float cellH,
                          const void* cells, int cols, int rows,
                          float subpixelOffsetX = 0.0f, float subpixelOffsetY = 0.0f);

    using MonospaceGlyphHook = std::function<bool(float x, float y, float w, float h, char32_t codepoint, uint32_t fgColor)>;
    void setMonospaceGlyphHook(MonospaceGlyphHook hook) noexcept { monospaceGlyphHook_ = std::move(hook); }
    [[nodiscard]] const MonospaceGlyphHook& getMonospaceGlyphHook() const noexcept { return monospaceGlyphHook_; }

    [[nodiscard]] size_t getVertexCount() const noexcept { return vertices_.size(); }
    [[nodiscard]] const std::vector<Vertex2D>& getVertices() const noexcept { return vertices_; }
    void clear() noexcept { vertices_.clear(); }
    void clearVertices() noexcept { vertices_.clear(); }
    [[nodiscard]] RenderBackendType getBackendType() const noexcept;
    [[nodiscard]] const uint32_t* getFramebuffer() const noexcept { return backend_ ? backend_->getFramebuffer() : nullptr; }
    [[nodiscard]] uint32_t getFramebufferWidth() const noexcept {
        if (backend_) {
            uint32_t bw = backend_->getFramebufferWidth();
            if (bw > 0) return bw;
        }
        return physicalWidth_;
    }
    [[nodiscard]] uint32_t getFramebufferHeight() const noexcept {
        if (backend_) {
            uint32_t bh = backend_->getFramebufferHeight();
            if (bh > 0) return bh;
        }
        return physicalHeight_;
    }
    [[nodiscard]] uint32_t getPhysicalWidth() const noexcept { return physicalWidth_; }
    [[nodiscard]] uint32_t getPhysicalHeight() const noexcept { return physicalHeight_; }
    void snapshotFramebuffer(std::vector<uint32_t>& outPixels, uint32_t& outW, uint32_t& outH) const {
        if (backend_) {
            backend_->snapshotFramebuffer(outPixels, outW, outH);
        } else {
            outPixels.clear();
            outW = 0;
            outH = 0;
        }
    }
    void restoreFramebuffer(const uint32_t* srcPixels, uint32_t w, uint32_t h, float opacity = 1.0f) {
        if (backend_) {
            backend_->restoreFramebuffer(srcPixels, w, h, opacity);
        }
    }
    void setDirectPresent(bool enable) noexcept { if (backend_) backend_->setDirectPresent(enable); }
    void setAntiAliasingMode(int mode) noexcept {
        if (antiAliasingMode_ != mode) {
            flush();
            antiAliasingMode_ = mode;
        }
        if (backend_) backend_->setAntiAliasingMode(mode);
    }
    [[nodiscard]] int getAntiAliasingMode() const noexcept { return antiAliasingMode_; }
    [[nodiscard]] ScopedAntiAliasing makeAntiAliasingScope(int tempMode);
    [[nodiscard]] void* getNativeDevice() const noexcept { return backend_ ? backend_->getNativeDevice() : nullptr; }
    [[nodiscard]] void* getNativeSurface() const noexcept { return backend_ ? backend_->getNativeSurface() : nullptr; }
    void setCustomRenderTargetView(void* view) noexcept { if (backend_) backend_->setCustomRenderTargetView(view); }

    // Helper for packing RGBA floats into uint32_t (ABGR/RGBA order matching shader layout)
    static inline uint32_t packColor(float r, float g, float b, float a) noexcept {
        auto clamp01 = [](float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        uint32_t ur = static_cast<uint32_t>(clamp01(r) * 255.0f) & 0xFF;
        uint32_t ug = static_cast<uint32_t>(clamp01(g) * 255.0f) & 0xFF;
        uint32_t ub = static_cast<uint32_t>(clamp01(b) * 255.0f) & 0xFF;
        uint32_t ua = static_cast<uint32_t>(clamp01(a) * 255.0f) & 0xFF;
        return (ua << 24) | (ub << 16) | (ug << 8) | ur;
    }

private:
    std::unique_ptr<IBatchRenderBackend> backend_;
    std::vector<Vertex2D> vertices_;
    float currentWidth_{1280.0f};
    float currentHeight_{800.0f};
    uint32_t physicalWidth_{1280};
    uint32_t physicalHeight_{800};
    float renderScaleX_{1.0f};
    float renderScaleY_{1.0f};
    int antiAliasingMode_{2};
    BlendMode blendMode_{BlendMode::Normal};
    bool inFrame_{false};
    bool monospaceAtlasReady_{false};
    bool sceneLightingActive_{false};
    size_t sceneLitStartIdx_{0};
    float tableLacquer_{0.0f};
    float tableBump_{2.4f};

    // 2D Rotation Transform State
    bool transformActive_{false};
    float transformCos_{1.0f};
    float transformSin_{0.0f};
    float transformOriginX_{0.0f};
    float transformOriginY_{0.0f};

    std::vector<Rect2D> scissorStack_{};
    MonospaceGlyphHook monospaceGlyphHook_{nullptr};

    // Static Chrome Caching State
    bool chromeCacheEnabled_{true};
    bool chromeCacheValid_{false};
    bool inChromeRecording_{false};
    uint32_t chromeCacheWidth_{0};
    uint32_t chromeCacheHeight_{0};
    std::vector<uint32_t> chromeCachePixels_{};
    uint64_t chromeDrawCallsAvoided_{0};
    uint32_t fontAtlasVersion_{0};

    // In-Memory GPU/Software Texture Store
    struct ImageTexture {
        uint32_t handle{0};
        int width{0};
        int height{0};
        int channels{4};
        std::vector<uint8_t> pixels;
    };
    std::unordered_map<uint32_t, ImageTexture> textures_;
    uint32_t nextTextureId_{1};

    inline void applyTransform(float& x, float& y) const noexcept {
        if (transformActive_) {
            float dx = x - transformOriginX_;
            float dy = y - transformOriginY_;
            x = transformOriginX_ + transformCos_ * dx - transformSin_ * dy;
            y = transformOriginY_ + transformSin_ * dx + transformCos_ * dy;
        }
    }
};

} // namespace eatsbits::ui

#endif // EATS_BATCH_RENDERER_2D_HPP
