#ifndef EATS_MONOSPACE_FONT_8X16_HPP
#define EATS_MONOSPACE_FONT_8X16_HPP

#include <cstdint>
#include <cstddef>

namespace eatsbits::ui {

// Standard crisp 8x16 monospace console font (256 glyphs * 16 rows = 4096 bytes).
// Each byte represents one row of 8 horizontal pixels (MSB = leftmost pixel).
extern const uint8_t kMonospaceFont8x16[256 * 16];

inline const uint8_t* getGlyph8x16Bitmap(uint8_t c) noexcept {
    return &kMonospaceFont8x16[static_cast<size_t>(c) * 16];
}

} // namespace eatsbits::ui

#endif // EATS_MONOSPACE_FONT_8X16_HPP
