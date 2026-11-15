#ifndef SOLENGINE_VECTOR_BINARY_HPP
#define SOLENGINE_VECTOR_BINARY_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace solengine {

enum class VectorVerbType : uint8_t {
    MoveTo = 0,
    LineTo = 1,
    CubicTo = 2
};

enum VectorShapeFlags : uint8_t {
    kVectorHasFill     = 1u << 0,
    kVectorHasStroke   = 1u << 1,
    kVectorEvenOddFill = 1u << 2,
    kVectorCapMask     = 0x18u, // bits 3..4: 0=butt, 1=round, 2=square
    kVectorJoinMask    = 0x60u  // bits 5..6: 0=miter, 1=round, 2=bevel
};

struct VectorVerb {
    VectorVerbType type{VectorVerbType::MoveTo};
    float x0{0.0f}, y0{0.0f};
    float x1{0.0f}, y1{0.0f};
    float x2{0.0f}, y2{0.0f};
};

struct VectorPath {
    bool closed{false};
    std::vector<VectorVerb> verbs;
};

struct VectorShape {
    uint32_t fillColor{0};   // 0xAARRGGBB
    uint32_t strokeColor{0}; // 0xAARRGGBB
    float strokeWidth{0.0f};
    uint8_t flags{0};
    float minX{0.0f}, minY{0.0f}, maxX{0.0f}, maxY{0.0f};
    std::vector<VectorPath> paths;
};

struct VectorImage {
    float width{0.0f};
    float height{0.0f};
    std::vector<VectorShape> shapes;

    [[nodiscard]] bool empty() const noexcept { return shapes.empty(); }

    /**
     * Parses raw uncompressed EVEC binary data.
     */
    static bool parse(const uint8_t* data, size_t size, VectorImage& out);

    /**
     * Parses either raw EVEC or zlib/deflate-compressed EVEC (evecz).
     */
    static bool parseAuto(const uint8_t* data, size_t size, VectorImage& out);

    /**
     * Decodes base64-encoded EVEC/EVECZ (or raw EVEC) with a per-thread LRU cache.
     * Returns nullptr on parse failure.
     */
    static const VectorImage* parseCached(const std::string& payload);
};

/**
 * Tight-bounds scanline rasterizer for VectorImage.
 */
class VectorRasterizer {
public:
    /**
     * Callback invoked once per visible shape with its tight-bounds RGBA buffer (straight alpha).
     * shapeIdx is in [0, shapeCount).
     */
    using ShapeEmitFn = std::function<void(int shapeIdx, int shapeCount,
                                           int dstX, int dstY, int w, int h,
                                           const uint8_t* rgba)>;

    /**
     * Rasterizes all visible shapes into tight-bounds buffers and invokes emitFn for each.
     */
    static void rasterizeShapesTight(const VectorImage& img,
                                     float dstX, float dstY, float dstW, float dstH,
                                     bool cover, int clipW, int clipH,
                                     const ShapeEmitFn& emitFn);

    /**
     * Rasterizes the entire VectorImage directly into an RGBA8 buffer of size (dstW * dstH * 4).
     */
    static void rasterizeToRgba(const VectorImage& img,
                                uint8_t* dstRgba, int dstW, int dstH,
                                float dstX, float dstY, float dstW_, float dstH_,
                                bool cover = false);
};

} // namespace solengine

#endif // SOLENGINE_VECTOR_BINARY_HPP
