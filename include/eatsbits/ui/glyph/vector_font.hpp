#pragma once

#include <cstdint>
#include <string_view>
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/theme.hpp"

namespace eatsbits::ui::glyph {

/**
 * @brief Procedural ShareTech-style monospace vector font renderer.
 * Encodes printable ASCII characters (32..126) as 45° chamfered vector strokes
 * with zero font file or texture atlas dependencies.
 *
 * Characteristics:
 * - 100% procedural vector stroke rendering via BatchRenderer2D.
 * - Ultra-low memory footprint (~1.2 KB packed coordinate data).
 * - Resolution independent: perfectly anti-aliased at 10px, 14px, 48px, or on 4K HiDPI.
 * - Single-pass GPU batching: no texture switches, no glyph atlas uploads.
 */

/// Renders a single printable ASCII character inside a cell of size (w, h).
/// Returns true if character was rendered (cp in 32..126), false otherwise.
bool renderVectorFontGlyph(BatchRenderer2D& r, uint32_t cp,
                           float x, float y, float w, float h,
                           float red, float green, float blue, float alpha = 1.0f);

/// High-level string drawing routine using the procedural vector font.
void drawVectorText(BatchRenderer2D& r, float x, float y, float size,
                    std::string_view text,
                    float red, float green, float blue, float alpha = 1.0f);

inline void drawVectorText(BatchRenderer2D& r, std::string_view text, float x, float y, float size,
                           float red, float green, float blue, float alpha = 1.0f) {
    drawVectorText(r, x, y, size, text, red, green, blue, alpha);
}

inline void drawVectorText(BatchRenderer2D& r, float x, float y, float size,
                           std::string_view text, const Color& color) {
    drawVectorText(r, x, y, size, text, color.r, color.g, color.b, color.a);
}

inline void drawVectorText(BatchRenderer2D& r, std::string_view text, float x, float y, float size,
                           const Color& color) {
    drawVectorText(r, x, y, size, text, color.r, color.g, color.b, color.a);
}

/// Returns the advance width of a string rendered with drawVectorText.
[[nodiscard]] float getVectorTextWidth(float size, std::string_view text) noexcept;

} // namespace eatsbits::ui::glyph
