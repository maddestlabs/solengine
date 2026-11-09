#pragma once

#include <cstdint>
#include <string_view>
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"

namespace eatsbits::ui::glyph {

/**
 * @brief Discrete functional packs for vector glyphs.
 * Allows applications (e.g. terminal apps, text editors, live-coding engines, DAWs)
 * to selectively enable only the glyphs they require, preventing bloat.
 */
enum class GlyphPack : uint32_t {
    None       = 0,
    Terminal   = 1 << 0, // CP437, Box drawing (0x2500..0x257F), Block elements (0x2580..0x259F)
    Editor     = 1 << 1, // Code folding (▾, ▸), Breakpoints (●, ○), Diagnostics (✓, ✗, ⚠, ℹ), Pipeline arrows (→, ←)
    DspMath    = 1 << 2, // Audio math (π, τ, ±, °, ∞, μ, Ω, √, ≠, ≤, ≥), Waveforms (∿, ⊓, △, ⊿)
    Score      = 1 << 3, // Music score engraving (𝄞 Treble, 𝄢 Bass, ♭, ♯, ♮, 𝄽 rests, SMuFL noteheads)
    VectorFont = 1 << 4, // Procedural ShareTech-style monospace vector font (ASCII 32..126)
    All        = 0xFFFFFFFF
};

inline constexpr uint32_t operator|(GlyphPack a, GlyphPack b) noexcept {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}

inline constexpr uint32_t operator|(uint32_t a, GlyphPack b) noexcept {
    return a | static_cast<uint32_t>(b);
}

// ---------------------------------------------------------------------------
// Standalone Pack Renderers (Can be called directly with zero registry lookup)
// ---------------------------------------------------------------------------

/// Renders box-drawing, block, and basic terminal characters.
bool renderTerminalGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                         float red, float green, float blue, float alpha = 1.0f);

/// Renders code editor gutter markers, folding chevrons, breakpoints, and diagnostics.
bool renderEditorGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                       float red, float green, float blue, float alpha = 1.0f);

/// Renders mathematical DSP symbols, electronic engineering units, and audio waveforms.
bool renderDspMathGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                        float red, float green, float blue, float alpha = 1.0f);

/// Renders musical notation, accidentals, clefs, and noteheads (Bravura SMuFL splines).
bool renderScoreGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                      float red, float green, float blue, float alpha = 1.0f);

// ---------------------------------------------------------------------------
// Unified Modular Glyph Registry
// ---------------------------------------------------------------------------

/**
 * @brief Zero-overhead dispatch registry managing active glyph packs.
 */
class GlyphRegistry {
public:
    static GlyphRegistry& instance();

    /// Configures active packs using a bitmask of GlyphPack flags.
    void enablePacks(uint32_t packBitmask) noexcept { activePacks_ = packBitmask; }
    void enablePack(GlyphPack pack) noexcept { activePacks_ |= static_cast<uint32_t>(pack); }
    void disablePack(GlyphPack pack) noexcept { activePacks_ &= ~static_cast<uint32_t>(pack); }
    [[nodiscard]] bool isPackEnabled(GlyphPack pack) const noexcept {
        return (activePacks_ & static_cast<uint32_t>(pack)) != 0;
    }

    /// Dispatches rendering across currently enabled packs.
    /// Returns true if a pack successfully rendered the codepoint.
    bool render(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                float red, float green, float blue, float alpha = 1.0f) const;

private:
    GlyphRegistry();
    uint32_t activePacks_{static_cast<uint32_t>(GlyphPack::All)};
};

} // namespace eatsbits::ui::glyph
