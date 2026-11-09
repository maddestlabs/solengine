#ifndef EATSTEXTURES_FIELD_OPS_HPP
#define EATSTEXTURES_FIELD_OPS_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eatstextures {

struct RGBAf {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};
};

struct ColorStop {
    float pos{0.0f};
    RGBAf color{};
};

struct FloatField {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<float> data;

    FloatField() = default;
    FloatField(uint32_t w, uint32_t h, float fillVal = 0.0f)
        : width(w), height(h), data(static_cast<size_t>(w) * h, fillVal) {}

    [[nodiscard]] float sampleWrapped(float u, float v) const noexcept;
    [[nodiscard]] float atWrapped(int x, int y) const noexcept;
};

struct ColorImage {
    uint32_t width{0};
    uint32_t height{0};
    std::vector<RGBAf> pixels;

    ColorImage() = default;
    ColorImage(uint32_t w, uint32_t h, RGBAf fillCol = {0.0f, 0.0f, 0.0f, 1.0f})
        : width(w), height(h), pixels(static_cast<size_t>(w) * h, fillCol) {}

    [[nodiscard]] RGBAf sampleWrapped(float u, float v) const noexcept;
    [[nodiscard]] std::vector<uint8_t> toRGBA8() const;
};

RGBAf parseHexColor(const std::string& hexStr, RGBAf fallback = {0.5f, 0.5f, 0.5f, 1.0f}) noexcept;

// ============================================================================
// 1. Noise & Pattern Generators
// ============================================================================

FloatField generateFbm(uint32_t width, uint32_t height,
                       float scaleX, float scaleY,
                       int octaves, float persistence, float lacunarity,
                       const std::string& mode, uint32_t seed);

FloatField generateWorley(uint32_t width, uint32_t height,
                          float scaleX, float scaleY,
                          float jitter, const std::string& mode,
                          const std::string& metric, uint32_t seed);

FloatField generateGabor(uint32_t width, uint32_t height,
                         float scaleX, float scaleY,
                         float angleDeg, float bandwidth, uint32_t seed);

FloatField generateWoodGrain(uint32_t width, uint32_t height,
                             float rings, float turbulence,
                             float ringSharpness, float poreDensity,
                             float fiberStrength, float angleDeg, uint32_t seed,
                             const FloatField* cellIdField = nullptr,
                             float centerU = -1.5f, float centerV = 0.5f);

FloatField generateBrickGrid(uint32_t width, uint32_t height,
                             int cols, int rows,
                             float mortarWidth, float bevelWidth,
                             float rowOffset, float heightVariance,
                             float slopeVariance, uint32_t seed,
                             const std::string& mode = "height");

FloatField generateWeave(uint32_t width, uint32_t height,
                         int scaleX, int scaleY,
                         const std::string& pattern,
                         float threadWidth, float slub, uint32_t seed,
                         const std::string& mode = "height");

FloatField generateScratches(uint32_t width, uint32_t height,
                             int count, float length, float widthNorm,
                             float angleDeg, float angleSpreadDeg, uint32_t seed);

// ============================================================================
// 2. Spatial Warping, Weathering & Filtering
// ============================================================================

FloatField opWarp(const FloatField& src,
                  const FloatField& warpX,
                  const FloatField& warpY,
                  float strength);

FloatField opDirectionalWarp(const FloatField& src,
                             const FloatField& intensityField,
                             float angleDeg,
                             float strength);

FloatField opSlopeBlur(const FloatField& src,
                       const FloatField& slopeField,
                       float strength,
                       int samples,
                       const std::string& mode);

FloatField opBlur(const FloatField& src, float radius);

FloatField opBevel(const FloatField& src, float widthNorm);

FloatField opMakeSeamless(const FloatField& src, float blendWidth = 0.15f);

ColorImage opMakeSeamlessColor(const ColorImage& src, float blendWidth = 0.15f);

// ============================================================================
// 3. Field Math, Remapping & Blending
// ============================================================================

FloatField opRemap(const FloatField& src, float inMin, float inMax, float outMin, float outMax);
FloatField opHistogramScan(const FloatField& src, float position, float contrast);
FloatField opHighpass(const FloatField& src, float radius, float strength = 1.0f);
FloatField opSmoothstep(const FloatField& src, float edge0, float edge1);
FloatField opPow(const FloatField& src, float exponent);
FloatField opInvert(const FloatField& src);
FloatField opNormalize(const FloatField& src);

FloatField opBlendFields(const FloatField& a,
                         const FloatField& b,
                         const std::string& mode,
                         float weight);

FloatField opHeightBlend(const FloatField& heightA,
                         const FloatField& heightB,
                         float offset,
                         float contrast);

// ============================================================================
// 4. Micro-Surface Analysis (Curvature, AO & Normals)
// ============================================================================

FloatField computeCurvature(const FloatField& heightField, float strength, int radius);
FloatField computeAmbientOcclusion(const FloatField& heightField, int radius, float intensity);
ColorImage computeNormalMap(const FloatField& heightField, float strength);
ColorImage floatFieldToGrayscaleImage(const FloatField& field);

// ============================================================================
// 5. Colorization, Blending, PBR Shading & Retro Pixel-Art
// ============================================================================

ColorImage applyGradientMap(const FloatField& field, const std::vector<ColorStop>& stops);

ColorImage blendColorImages(const ColorImage& a,
                            const ColorImage& b,
                            const std::string& mode,
                            const FloatField* mask,
                            float weight);

ColorImage adjustColorImage(const ColorImage& src,
                            float brightness,
                            float contrast,
                            float saturation,
                            float gamma);

ColorImage splatterSpecks(const ColorImage& src,
                          int count,
                          RGBAf speckColor,
                          float radiusPx,
                          float opacity,
                          uint32_t seed);

ColorImage shadePBR(const ColorImage& albedo,
                    const FloatField& heightField,
                    const FloatField* roughnessField,
                    float roughnessScalar,
                    float metallicScalar,
                    float normalStrength,
                    float aoIntensity,
                    float lightAzimuthDeg,
                    float lightElevationDeg,
                    float lightIntensity,
                    float ambientIntensity);

ColorImage quantizePixelArt(const ColorImage& src,
                            uint32_t targetW,
                            uint32_t targetH,
                            const std::vector<RGBAf>& palette,
                            const std::string& ditherMode,
                            float ditherStrength,
                            bool upscaleToCanvas);

std::vector<RGBAf> getBuiltinPalette(const std::string& name);

} // namespace eatstextures

#endif // EATSTEXTURES_FIELD_OPS_HPP
