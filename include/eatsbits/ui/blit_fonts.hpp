#ifndef EATSBITS_UI_BLIT_FONTS_HPP
#define EATSBITS_UI_BLIT_FONTS_HPP

/*
 * First-party Oswald-inspired condensed proportional 7px pixel bitmap font.
 * 95 printable ASCII glyphs (32..126), 1-5px glyph width + 1px kerning,
 * 7px cap height + 1px optional descender (~855 bytes, zero external assets).
 */

#include <cmath>
#include <cstdint>
#include <string>

namespace eatsbits::ui::blitfont {

inline constexpr int kNumGlyphs = 95; // Printable ASCII 32..126
inline constexpr int kCondensed7Height = 7; // Cap height (rows 0..6); row 7 is optional descender

struct CondensedGlyph {
    uint8_t width;   // Active pixel columns (1..5); advance = width + 1
    uint8_t rows[8]; // MSB-aligned to `width`: bit (width - 1 - col) is column `col`
};

inline constexpr CondensedGlyph kCondensed7Glyphs[kNumGlyphs] = {
    // 32 ' '
    {2, {0, 0, 0, 0, 0, 0, 0, 0}},
    // 33 '!'
    {1, {0b1, 0b1, 0b1, 0b1, 0b1, 0b0, 0b1, 0}},
    // 34 '"'
    {3, {0b101, 0b101, 0b000, 0b000, 0b000, 0b000, 0b000, 0}},
    // 35 '#'
    {5, {0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010, 0}},
    // 36 '$'
    {3, {0b010, 0b011, 0b110, 0b010, 0b011, 0b110, 0b010, 0}},
    // 37 '%'
    {4, {0b1001, 0b0001, 0b0010, 0b0100, 0b1000, 0b1001, 0b0000, 0}},
    // 38 '&'
    {4, {0b0110, 0b1001, 0b0110, 0b1010, 0b1001, 0b1001, 0b0111, 0}},
    // 39 '\''
    {1, {0b1, 0b1, 0b0, 0b0, 0b0, 0b0, 0b0, 0}},
    // 40 '('
    {2, {0b01, 0b10, 0b10, 0b10, 0b10, 0b10, 0b01, 0}},
    // 41 ')'
    {2, {0b10, 0b01, 0b01, 0b01, 0b01, 0b01, 0b10, 0}},
    // 42 '*'
    {3, {0b000, 0b101, 0b010, 0b111, 0b010, 0b101, 0b000, 0}},
    // 43 '+'
    {3, {0b000, 0b000, 0b010, 0b111, 0b010, 0b000, 0b000, 0}},
    // 44 ','
    {2, {0b00, 0b00, 0b00, 0b00, 0b00, 0b01, 0b01, 0b10}},
    // 45 '-'
    {3, {0b000, 0b000, 0b000, 0b111, 0b000, 0b000, 0b000, 0}},
    // 46 '.'
    {1, {0b0, 0b0, 0b0, 0b0, 0b0, 0b0, 0b1, 0}},
    // 47 '/'
    {3, {0b001, 0b001, 0b010, 0b010, 0b010, 0b100, 0b100, 0}},
    // 48 '0'
    {3, {0b010, 0b101, 0b101, 0b101, 0b101, 0b101, 0b010, 0}},
    // 49 '1'
    {2, {0b01, 0b11, 0b01, 0b01, 0b01, 0b01, 0b01, 0}},
    // 50 '2'
    {3, {0b110, 0b001, 0b001, 0b010, 0b100, 0b100, 0b111, 0}},
    // 51 '3'
    {3, {0b110, 0b001, 0b001, 0b010, 0b001, 0b001, 0b110, 0}},
    // 52 '4'
    {3, {0b101, 0b101, 0b101, 0b111, 0b001, 0b001, 0b001, 0}},
    // 53 '5'
    {3, {0b111, 0b100, 0b100, 0b110, 0b001, 0b001, 0b110, 0}},
    // 54 '6'
    {3, {0b011, 0b100, 0b100, 0b110, 0b101, 0b101, 0b010, 0}},
    // 55 '7'
    {3, {0b111, 0b001, 0b001, 0b010, 0b010, 0b010, 0b010, 0}},
    // 56 '8'
    {3, {0b010, 0b101, 0b101, 0b010, 0b101, 0b101, 0b010, 0}},
    // 57 '9'
    {3, {0b010, 0b101, 0b101, 0b011, 0b001, 0b001, 0b110, 0}},
    // 58 ':'
    {1, {0b0, 0b0, 0b1, 0b0, 0b0, 0b1, 0b0, 0}},
    // 59 ';'
    {2, {0b00, 0b00, 0b01, 0b00, 0b00, 0b01, 0b01, 0b10}},
    // 60 '<'
    {3, {0b000, 0b001, 0b010, 0b100, 0b010, 0b001, 0b000, 0}},
    // 61 '='
    {3, {0b000, 0b000, 0b111, 0b000, 0b111, 0b000, 0b000, 0}},
    // 62 '>'
    {3, {0b000, 0b100, 0b010, 0b001, 0b010, 0b100, 0b000, 0}},
    // 63 '?'
    {3, {0b110, 0b001, 0b001, 0b010, 0b010, 0b000, 0b010, 0}},
    // 64 '@'
    {5, {0b01110, 0b10001, 0b10111, 0b10101, 0b10111, 0b10000, 0b01110, 0}},
    // 65 'A'
    {3, {0b010, 0b101, 0b101, 0b111, 0b101, 0b101, 0b101, 0}},
    // 66 'B'
    {3, {0b110, 0b101, 0b101, 0b110, 0b101, 0b101, 0b110, 0}},
    // 67 'C'
    {3, {0b011, 0b100, 0b100, 0b100, 0b100, 0b100, 0b011, 0}},
    // 68 'D'
    {3, {0b110, 0b101, 0b101, 0b101, 0b101, 0b101, 0b110, 0}},
    // 69 'E'
    {3, {0b111, 0b100, 0b100, 0b110, 0b100, 0b100, 0b111, 0}},
    // 70 'F'
    {3, {0b111, 0b100, 0b100, 0b110, 0b100, 0b100, 0b100, 0}},
    // 71 'G'
    {4, {0b0110, 0b1001, 0b1000, 0b1011, 0b1001, 0b1001, 0b0110, 0}},
    // 72 'H'
    {3, {0b101, 0b101, 0b101, 0b111, 0b101, 0b101, 0b101, 0}},
    // 73 'I'
    {1, {0b1, 0b1, 0b1, 0b1, 0b1, 0b1, 0b1, 0}},
    // 74 'J'
    {3, {0b001, 0b001, 0b001, 0b001, 0b101, 0b101, 0b010, 0}},
    // 75 'K'
    {3, {0b101, 0b101, 0b101, 0b110, 0b101, 0b101, 0b101, 0}},
    // 76 'L'
    {3, {0b100, 0b100, 0b100, 0b100, 0b100, 0b100, 0b111, 0}},
    // 77 'M'
    {5, {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001, 0}},
    // 78 'N'
    {4, {0b1001, 0b1101, 0b1101, 0b1011, 0b1011, 0b1001, 0b1001, 0}},
    // 79 'O'
    {3, {0b010, 0b101, 0b101, 0b101, 0b101, 0b101, 0b010, 0}},
    // 80 'P'
    {3, {0b110, 0b101, 0b101, 0b110, 0b100, 0b100, 0b100, 0}},
    // 81 'Q'
    {4, {0b0110, 0b1001, 0b1001, 0b1001, 0b1011, 0b1001, 0b0111, 0}},
    // 82 'R'
    {3, {0b110, 0b101, 0b101, 0b110, 0b101, 0b101, 0b101, 0}},
    // 83 'S'
    {3, {0b011, 0b100, 0b100, 0b010, 0b001, 0b001, 0b110, 0}},
    // 84 'T'
    {3, {0b111, 0b010, 0b010, 0b010, 0b010, 0b010, 0b010, 0}},
    // 85 'U'
    {3, {0b101, 0b101, 0b101, 0b101, 0b101, 0b101, 0b010, 0}},
    // 86 'V'
    {3, {0b101, 0b101, 0b101, 0b101, 0b101, 0b010, 0b010, 0}},
    // 87 'W'
    {5, {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001, 0}},
    // 88 'X'
    {3, {0b101, 0b101, 0b101, 0b010, 0b101, 0b101, 0b101, 0}},
    // 89 'Y'
    {3, {0b101, 0b101, 0b101, 0b010, 0b010, 0b010, 0b010, 0}},
    // 90 'Z'
    {3, {0b111, 0b001, 0b001, 0b010, 0b100, 0b100, 0b111, 0}},
    // 91 '['
    {2, {0b11, 0b10, 0b10, 0b10, 0b10, 0b10, 0b11, 0}},
    // 92 '\\'
    {3, {0b100, 0b100, 0b010, 0b010, 0b010, 0b001, 0b001, 0}},
    // 93 ']'
    {2, {0b11, 0b01, 0b01, 0b01, 0b01, 0b01, 0b11, 0}},
    // 94 '^'
    {3, {0b010, 0b101, 0b000, 0b000, 0b000, 0b000, 0b000, 0}},
    // 95 '_'
    {3, {0b000, 0b000, 0b000, 0b000, 0b000, 0b000, 0b111, 0}},
    // 96 '`'
    {2, {0b10, 0b01, 0b00, 0b00, 0b00, 0b00, 0b00, 0}},
    // 97 'a'
    {3, {0b000, 0b000, 0b110, 0b001, 0b011, 0b101, 0b011, 0}},
    // 98 'b'
    {3, {0b100, 0b100, 0b110, 0b101, 0b101, 0b101, 0b110, 0}},
    // 99 'c'
    {3, {0b000, 0b000, 0b011, 0b100, 0b100, 0b100, 0b011, 0}},
    // 100 'd'
    {3, {0b001, 0b001, 0b011, 0b101, 0b101, 0b101, 0b011, 0}},
    // 101 'e'
    {3, {0b000, 0b000, 0b010, 0b101, 0b111, 0b100, 0b011, 0}},
    // 102 'f'
    {3, {0b011, 0b100, 0b110, 0b100, 0b100, 0b100, 0b100, 0}},
    // 103 'g'
    {3, {0b000, 0b000, 0b011, 0b101, 0b101, 0b011, 0b001, 0b110}},
    // 104 'h'
    {3, {0b100, 0b100, 0b110, 0b101, 0b101, 0b101, 0b101, 0}},
    // 105 'i'
    {1, {0b1, 0b0, 0b1, 0b1, 0b1, 0b1, 0b1, 0}},
    // 106 'j'
    {2, {0b01, 0b00, 0b01, 0b01, 0b01, 0b01, 0b01, 0b10}},
    // 107 'k'
    {3, {0b100, 0b100, 0b101, 0b101, 0b110, 0b101, 0b101, 0}},
    // 108 'l'
    {1, {0b1, 0b1, 0b1, 0b1, 0b1, 0b1, 0b1, 0}},
    // 109 'm'
    {5, {0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10101, 0b10101, 0}},
    // 110 'n'
    {3, {0b000, 0b000, 0b110, 0b101, 0b101, 0b101, 0b101, 0}},
    // 111 'o'
    {3, {0b000, 0b000, 0b010, 0b101, 0b101, 0b101, 0b010, 0}},
    // 112 'p'
    {3, {0b000, 0b000, 0b110, 0b101, 0b101, 0b110, 0b100, 0b100}},
    // 113 'q'
    {3, {0b000, 0b000, 0b011, 0b101, 0b101, 0b011, 0b001, 0b001}},
    // 114 'r'
    {3, {0b000, 0b000, 0b101, 0b110, 0b100, 0b100, 0b100, 0}},
    // 115 's'
    {3, {0b000, 0b000, 0b011, 0b100, 0b010, 0b001, 0b110, 0}},
    // 116 't'
    {3, {0b010, 0b010, 0b111, 0b010, 0b010, 0b010, 0b001, 0}},
    // 117 'u'
    {3, {0b000, 0b000, 0b101, 0b101, 0b101, 0b101, 0b011, 0}},
    // 118 'v'
    {3, {0b000, 0b000, 0b101, 0b101, 0b101, 0b010, 0b010, 0}},
    // 119 'w'
    {5, {0b00000, 0b00000, 0b10001, 0b10001, 0b10101, 0b10101, 0b01010, 0}},
    // 120 'x'
    {3, {0b000, 0b000, 0b101, 0b101, 0b010, 0b101, 0b101, 0}},
    // 121 'y'
    {3, {0b000, 0b000, 0b101, 0b101, 0b101, 0b011, 0b001, 0b110}},
    // 122 'z'
    {3, {0b000, 0b000, 0b111, 0b001, 0b010, 0b100, 0b111, 0}},
    // 123 '{'
    {3, {0b011, 0b010, 0b010, 0b100, 0b010, 0b010, 0b011, 0}},
    // 124 '|'
    {1, {0b1, 0b1, 0b1, 0b1, 0b1, 0b1, 0b1, 0}},
    // 125 '}'
    {3, {0b110, 0b010, 0b010, 0b001, 0b010, 0b010, 0b110, 0}},
    // 126 '~'
    {4, {0b0000, 0b0000, 0b0101, 0b1010, 0b0000, 0b0000, 0b0000, 0}}
};

inline constexpr const CondensedGlyph& condensedGlyph(char c) noexcept {
    const int idx = static_cast<int>(static_cast<unsigned char>(c)) - 32;
    if (idx < 0 || idx >= kNumGlyphs) return kCondensed7Glyphs[0];
    return kCondensed7Glyphs[idx];
}

inline float measureWidth(const std::string& text, float pixelSize) noexcept {
    if (text.empty()) return 0.0f;
    int totalCells = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        totalCells += static_cast<int>(condensedGlyph(text[i]).width);
        if (i + 1 < text.size()) totalCells += 1; // 1px kerning gap
    }
    return static_cast<float>(totalCells) * pixelSize;
}

template <typename Renderer>
inline void drawPixelText(Renderer& r, const std::string& text,
                          float x, float y, float pixelSize,
                          float cr, float cg, float cb, float ca,
                          float snapGrid = 0.0f) {
    if (text.empty() || pixelSize <= 0.0f) return;

    float originX = x;
    float originY = y;
    if (snapGrid > 0.0f) {
        originX = std::round(x / snapGrid) * snapGrid;
        originY = std::round(y / snapGrid) * snapGrid;
    }

    float penX = originX;
    for (char ch : text) {
        const CondensedGlyph& g = condensedGlyph(ch);
        const int gw = static_cast<int>(g.width);
        if (ch != ' ') {
            for (int row = 0; row < 8; ++row) {
                const uint8_t rowBits = g.rows[row];
                if (rowBits == 0u) continue;
                int col = 0;
                while (col < gw) {
                    if (((rowBits >> (gw - 1 - col)) & 1u) == 0u) {
                        ++col;
                        continue;
                    }
                    const int runStart = col;
                    while (col < gw && (((rowBits >> (gw - 1 - col)) & 1u) != 0u)) {
                        ++col;
                    }
                    const float rx = penX + static_cast<float>(runStart) * pixelSize;
                    const float ry = originY + static_cast<float>(row) * pixelSize;
                    const float rw = static_cast<float>(col - runStart) * pixelSize;
                    r.drawRect(rx, ry, rw, pixelSize, cr, cg, cb, ca);
                }
            }
        }
        penX += static_cast<float>(gw + 1) * pixelSize;
    }
}

} // namespace eatsbits::ui::blitfont

#endif // EATSBITS_UI_BLIT_FONTS_HPP
