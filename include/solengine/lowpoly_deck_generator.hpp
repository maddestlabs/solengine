#pragma once

#include "solengine/card_texture_cache.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace solengine {

enum class LowPolyStyle : uint8_t {
    Classic   = 0,
    Scoundrel = 1,
    Clockwork = 2,
    Mobile    = 3
};

enum class LowPolyBg : uint8_t {
    White = 0,
    Ivory = 1,
    Dark  = 2
};

struct LowPolyDeckOptions {
    LowPolyStyle style{LowPolyStyle::Classic};
    LowPolyBg bg{LowPolyBg::White};
    bool clearBottomRight{false};
    uint32_t seed{42};
};

/// Procedural 3D-projected, flat-shaded low-poly playing card generator.
/// Builds native VectorImage geometry directly in memory and rasterizes via VectorRasterizer
/// (zero SVG string formatting, zero XML parsing), with full-bleed square corners.
class LowPolyDeckGenerator {
public:
    using ProgressCallback = std::function<bool(float progress, const std::string& status)>;

    /// Rasterize all 52 faces + 2 jokers + card back directly into outBase via VectorRasterizer.
    static bool generateDeckTextures(
        const LowPolyDeckOptions& opts,
        int cardW,
        int cardH,
        std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
        const ProgressCallback& onProgress = nullptr
    );
};

} // namespace solengine
