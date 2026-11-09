#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <span>
#include "eatsbits/ui/batch_renderer_2d.hpp"

namespace eatsbits::ui::glyph {

#pragma pack(push, 1)

/// @brief Binary header for the .ebf (Eatsbits Font) format (32 bytes).
struct EbfHeader {
    char     magic[4]{'E', 'B', 'F', '1'}; ///< Magic bytes "EBF1"
    uint16_t version{1};                   ///< Format version (1)
    uint16_t flags{0};                     ///< Bit 0: Monospace, Bit 1: Condensed, Bit 2: Kerning
    uint16_t unitsPerEm{1000};             ///< Coordinate units per EM square (e.g. 1000)
    uint16_t glyphCount{0};                ///< Total number of glyph records
    int16_t  ascender{800};                ///< Typographic ascender in font units
    int16_t  descender{-200};              ///< Typographic descender (typically negative)
    int16_t  lineGap{100};                 ///< Recommended line gap
    int16_t  capHeight{720};               ///< Capital letter height
    int16_t  xHeight{520};                 ///< Lowercase x-height
    int16_t  maxAdvance{600};              ///< Maximum horizontal advance
    uint8_t  reserved[10]{0};              ///< Future extension padding
};

/// @brief Command opcodes for the compact vector contour bytecode stream.
enum class EbfOpcode : uint8_t {
    End         = 0x00,
    MoveTo      = 0x01, // 2x int16 (x, y)
    LineTo      = 0x02, // 2x int16 (x, y)
    QuadTo      = 0x03, // 4x int16 (cx, cy, x, y)
    CubicTo     = 0x04, // 6x int16 (c1x, c1y, c2x, c2y, x, y)
    Close       = 0x05, // Closes current path
    Rect        = 0x06, // 4x int16 (x, y, w, h)
    ChamferRect = 0x07, // 5x int16 (x, y, w, h, chamfer)
    Triangle    = 0x08, // 6x int16 (x0, y0, x1, y1, x2, y2)
    VStem       = 0x09, // 3x int16 (x, y0, y1)
    HBar        = 0x0A, // 3x int16 (x0, x1, y)
    Diag        = 0x0B  // 4x int16 (x0, y0, x1, y1)
};

/// @brief Metadata and metrics record for a single glyph (16 bytes).
struct EbfGlyphRecord {
    uint32_t codepoint{0};     ///< UTF-32 codepoint
    int16_t  advanceX{0};      ///< Horizontal advance in font units
    int16_t  bearingX{0};      ///< Left side bearing (horizontal offset from origin)
    int16_t  bearingY{0};      ///< Top side bearing (vertical offset from baseline)
    uint16_t width{0};         ///< Bounding box width
    uint16_t height{0};        ///< Bounding box height
    uint32_t commandOffset{0}; ///< Byte offset into the contour bytecode buffer
    uint16_t commandLength{0}; ///< Length in bytes of the glyph's command sequence
    uint16_t flags{0};         ///< Bit 0: Hollow stroke, Bit 1: Filled polygon
};

/// @brief Kerning pair entry for proportional typographic adjustments.
struct EbfKerningPair {
    uint32_t leftCodepoint{0};
    uint32_t rightCodepoint{0};
    int16_t  kerningAmount{0}; ///< Offset added to advance in font units
};

#pragma pack(pop)

/// @brief Text measurement dimensions.
struct EbfTextMetrics {
    float width{0.0f};
    float height{0.0f};
    float ascender{0.0f};
    float descender{0.0f};
};

/**
 * @brief High-performance, zero-external-dependency proportional vector font engine.
 * 
 * Supports reading pre-compiled binary .ebf fonts, embedded ROM fonts (such as
 * Oswald Condensed and ShareTech Mono), measuring proportional text, and tessellating
 * vector glyphs directly into BatchRenderer2D without FreeType or HarfBuzz.
 */
class EbfFont {
public:
    EbfFont();
    ~EbfFont() = default;

    EbfFont(const EbfFont&) = delete;
    EbfFont& operator=(const EbfFont&) = delete;
    EbfFont(EbfFont&&) noexcept;
    EbfFont& operator=(EbfFont&&) noexcept;

    /// Loads an .ebf font file from disk.
    bool loadFromFile(const std::string& filePath);

    /// Loads an .ebf font directly from an in-memory buffer.
    bool loadFromMemory(const uint8_t* data, size_t size);

    /// Exports the current font data to an .ebf binary file.
    bool saveToFile(const std::string& filePath) const;

    /// Converts a TTF/OTF font file on-the-fly and loads it into this instance.
    bool loadFromTtfFile(const std::string& ttfPath);

    /// Converts a TTF/OTF font memory buffer on-the-fly and loads it into this instance.
    bool loadFromTtfMemory(const uint8_t* data, size_t size);

    /// Returns true if a valid font header and glyph table are loaded.
    [[nodiscard]] bool isValid() const noexcept { return valid_; }

    /// Typographic font properties.
    [[nodiscard]] const EbfHeader& getHeader() const noexcept { return header_; }
    [[nodiscard]] float getAscender(float fontSize) const noexcept;
    [[nodiscard]] float getDescender(float fontSize) const noexcept;
    [[nodiscard]] float getLineHeight(float fontSize) const noexcept;
    [[nodiscard]] size_t getGlyphCount() const noexcept { return glyphs_.size(); }
    [[nodiscard]] const std::vector<uint8_t>& getCommandData() const noexcept { return commandData_; }

    /// Look up a glyph record by codepoint (binary search). Returns nullptr if missing.
    [[nodiscard]] const EbfGlyphRecord* findGlyph(uint32_t codepoint) const noexcept;

    /// Look up kerning adjustment between two codepoints.
    [[nodiscard]] float getKerning(uint32_t leftCp, uint32_t rightCp, float fontSize) const noexcept;

    /// Measures the horizontal pixel width of a UTF-8 text string.
    [[nodiscard]] float measureText(std::string_view text, float fontSize) const;

    /// Measures detailed bounds of a UTF-8 text string.
    [[nodiscard]] EbfTextMetrics measureTextDetailed(std::string_view text, float fontSize) const;

    /// Renders a single proportional glyph at the given baseline position.
    bool renderGlyph(BatchRenderer2D& r, uint32_t codepoint, float x, float baselineY,
                     float fontSize, float red, float green, float blue, float alpha = 1.0f) const;

    /// Renders a multi-character proportional string with optional kerning and tracking.
    void drawText(BatchRenderer2D& r, float x, float y, float fontSize,
                  std::string_view text,
                  float red, float green, float blue, float alpha = 1.0f) const;

    // --- Embedded Built-in Fonts (Zero File Dependencies) ---

    /// Returns the built-in Oswald-like Condensed Grotesque proportional vector font.
    static const EbfFont& getEmbeddedOswaldCondensed();

    /// Returns the built-in ShareTech-style Monospace vector font.
    static const EbfFont& getEmbeddedShareTechMono();

private:
    EbfHeader header_{};
    std::vector<EbfGlyphRecord> glyphs_;
    std::vector<uint8_t> commandData_;
    std::vector<EbfKerningPair> kerningPairs_;
    bool valid_{false};

    void parseFromBuffer(const uint8_t* data, size_t size);
};

} // namespace eatsbits::ui::glyph
