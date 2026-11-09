#pragma once

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/theme.hpp"
#include "eatsbits/ui/glyph/ebf_font.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"
#include "eatsbits/ui/glyph/glyph_packs.hpp"
#include <string_view>

namespace eatsbits::ui::glyph {

/**
 * @brief Unified Zero-Dependency Native Typography & Glyph Engine (Eatsbits Standard).
 *
 * Implements the Eatsbits 3-Tier Vector Typography Architecture (eatsbits-glyphs.md §13 & §15)
 * with zero FreeType or HarfBuzz bloat:
 *   - Tier 1: Fast pre-tessellated binary .ebf files (assets/fonts/Oswald-Medium.ebf, ShareTechMono-Regular.ebf)
 *   - Tier 2: Zero-file embedded ROM fonts in .rodata (kOswaldEmbeddedData, kShareTechEmbeddedData)
 *   - Tier 3: Procedural 45° chamfered ShareTech vector stroke font (VectorFont) & GlyphRegistry packs
 */
class FontEngine {
public:
    static FontEngine& instance() {
        static FontEngine s_engine;
        return s_engine;
    }

    /// Proportional UI header/label font (Oswald Condensed Grotesque .ebf)
    [[nodiscard]] const EbfFont& oswald() const noexcept {
        return oswaldDisk_.isValid() ? oswaldDisk_ : EbfFont::getEmbeddedOswaldCondensed();
    }

    /// Monospace code/stat/log font (ShareTech Mono .ebf)
    [[nodiscard]] const EbfFont& shareTech() const noexcept {
        return shareTechDisk_.isValid() ? shareTechDisk_ : EbfFont::getEmbeddedShareTechMono();
    }

    /// Measure proportional text width in pixels
    [[nodiscard]] float measureText(std::string_view text, float fontSize, bool monospace = false) const {
        return monospace ? shareTech().measureText(text, fontSize)
                         : oswald().measureText(text, fontSize);
    }

    /// Draw proportional Oswald text at top-left (x, y)
    void drawText(BatchRenderer2D& r, float x, float y, float fontSize,
                  std::string_view text, const Color& col, bool monospace = false) const {
        const EbfFont& f = monospace ? shareTech() : oswald();
        f.drawText(r, x, y, fontSize, text, col.r, col.g, col.b, col.a);
    }

    /// Draw proportional Oswald text with crisp 1px drop shadow for high contrast over HD-2D scenes
    void drawTextShadowed(BatchRenderer2D& r, float x, float y, float fontSize,
                          std::string_view text, const Color& col,
                          const Color& shadowCol = Color(0.04f, 0.03f, 0.05f, 0.88f),
                          bool monospace = false) const {
        const EbfFont& f = monospace ? shareTech() : oswald();
        f.drawText(r, x + 1.2f, y + 1.2f, fontSize, text, shadowCol.r, shadowCol.g, shadowCol.b, shadowCol.a * col.a);
        f.drawText(r, x, y, fontSize, text, col.r, col.g, col.b, col.a);
    }

    /// Draw horizontally and vertically centered text inside a bounding Rect (Eatsbits §15.1 formula)
    void drawTextCentered(BatchRenderer2D& r, const Rect& bounds, float fontSize,
                          std::string_view text, const Color& col,
                          bool shadowed = true, bool monospace = false) const {
        const EbfFont& f = monospace ? shareTech() : oswald();
        float textW = f.measureText(text, fontSize);
        float capH = static_cast<float>(f.getHeader().capHeight) * (fontSize / 1000.0f);
        float asc  = f.getAscender(fontSize);
        float tx = bounds.x + (bounds.w - textW) * 0.5f;
        // Baseline centered on cap-height, converted to drawText top-Y (baselineY - ascender)
        float baselineY = bounds.y + (bounds.h + capH) * 0.5f;
        float ty = baselineY - asc;
        if (shadowed) {
            drawTextShadowed(r, tx, ty, fontSize, text, col, Color(0.04f, 0.03f, 0.05f, 0.88f), monospace);
        } else {
            drawText(r, tx, ty, fontSize, text, col, monospace);
        }
    }

    /// Draw right-aligned text ending at (rightX, y)
    void drawTextRight(BatchRenderer2D& r, float rightX, float y, float fontSize,
                       std::string_view text, const Color& col,
                       bool shadowed = true, bool monospace = false) const {
        float w = measureText(text, fontSize, monospace);
        if (shadowed) {
            drawTextShadowed(r, rightX - w, y, fontSize, text, col, Color(0.04f, 0.03f, 0.05f, 0.88f), monospace);
        } else {
            drawText(r, rightX - w, y, fontSize, text, col, monospace);
        }
    }

private:
    FontEngine() {
        // Tier 1: Optional fast binary .ebf load from disk; automatically falls back to Tier 2 ROM
        if (!oswaldDisk_.loadFromFile("assets/fonts/Oswald-Medium.ebf")) {
            oswaldDisk_.loadFromFile("../assets/fonts/Oswald-Medium.ebf");
        }
        if (!shareTechDisk_.loadFromFile("assets/fonts/ShareTechMono-Regular.ebf")) {
            shareTechDisk_.loadFromFile("../assets/fonts/ShareTechMono-Regular.ebf");
        }
    }

    EbfFont oswaldDisk_;
    EbfFont shareTechDisk_;
};

} // namespace eatsbits::ui::glyph
