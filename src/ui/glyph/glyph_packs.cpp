#include "eatsbits/ui/glyph/glyph_packs.hpp"
#include "eatsbits/ui/score/score_glyphs.hpp"
#include <cmath>
#include <algorithm>

namespace eatsbits::ui::glyph {

// ---------------------------------------------------------------------------
// 1. Terminal Pack (CP437, Box-Drawing, Blocks, Basic Navigation)
// ---------------------------------------------------------------------------

bool renderTerminalGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                         float red, float green, float blue, float alpha) {
    auto seg = [&](float x0, float y0, float x1, float y1, float w = 1.4f) {
        r.drawLine(x0, y0, x1, y1, red, green, blue, alpha, w * scale);
    };

    float w = 7.5f * scale;
    float h = 8.0f * scale;
    float mx = x + w * 0.5f;
    float my = y + h * 0.5f;

    // Box-Drawing (0x2500 - 0x257F)
    switch (cp) {
        case 0x2500: // ─ Horizontal
            seg(x, my, x + w, my);
            return true;
        case 0x2502: // │ Vertical
            seg(mx, y, mx, y + h);
            return true;
        case 0x250C: // ┌ Top-Left
            seg(mx, my, x + w, my);
            seg(mx, my, mx, y + h);
            return true;
        case 0x2510: // ┐ Top-Right
            seg(x, my, mx, my);
            seg(mx, my, mx, y + h);
            return true;
        case 0x2514: // └ Bottom-Left
            seg(mx, y, mx, my);
            seg(mx, my, x + w, my);
            return true;
        case 0x2518: // ┘ Bottom-Right
            seg(x, my, mx, my);
            seg(mx, y, mx, my);
            return true;
        case 0x251C: // ├ Left Tee
            seg(mx, y, mx, y + h);
            seg(mx, my, x + w, my);
            return true;
        case 0x2524: // ┤ Right Tee
            seg(mx, y, mx, y + h);
            seg(x, my, mx, my);
            return true;
        case 0x252C: // ┬ Top Tee
            seg(x, my, x + w, my);
            seg(mx, my, mx, y + h);
            return true;
        case 0x2534: // ┴ Bottom Tee
            seg(x, my, x + w, my);
            seg(mx, y, mx, my);
            return true;
        case 0x253C: // ┼ Cross
            seg(x, my, x + w, my);
            seg(mx, y, mx, y + h);
            return true;

        // Block Elements (0x2580 - 0x259F)
        case 0x2588: // █ Full Block
            r.drawRect(x, y, w, h, red, green, blue, alpha);
            return true;
        case 0x2584: // ▄ Lower Half Block
            r.drawRect(x, my, w, h * 0.5f, red, green, blue, alpha);
            return true;
        case 0x2580: // ▀ Upper Half Block
            r.drawRect(x, y, w, h * 0.5f, red, green, blue, alpha);
            return true;
        case 0x258C: // ▌ Left Half Block
            r.drawRect(x, y, w * 0.5f, h, red, green, blue, alpha);
            return true;
        case 0x2590: // ▐ Right Half Block
            r.drawRect(mx, y, w * 0.5f, h, red, green, blue, alpha);
            return true;
        case 0x2591: // ░ 25% Shade
            r.drawRect(x, y, w, h, red, green, blue, alpha * 0.25f);
            return true;
        case 0x2592: // ▒ 50% Shade
            r.drawRect(x, y, w, h, red, green, blue, alpha * 0.50f);
            return true;
        case 0x2593: // ▓ 75% Shade
            r.drawRect(x, y, w, h, red, green, blue, alpha * 0.75f);
            return true;

        default:
            break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 2. Code Editor Pack (Folding, Breakpoints, Diagnostics, Arrows)
// ---------------------------------------------------------------------------

bool renderEditorGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                       float red, float green, float blue, float alpha) {
    auto seg = [&](float x0, float y0, float x1, float y1, float w = 1.5f) {
        r.drawLine(x0, y0, x1, y1, red, green, blue, alpha, w * scale);
    };

    float w = 7.5f * scale;
    float h = 8.0f * scale;
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;

    switch (cp) {
        // Code Folding
        case 0x25BE: // ▾ Expanded Fold (Down triangle)
            r.drawTriangle(x + 1.5f * scale, y + 2.5f * scale,
                           x + 6.0f * scale, y + 2.5f * scale,
                           cx, y + 6.5f * scale,
                           red, green, blue, alpha);
            return true;
        case 0x25B8: // ▸ Collapsed Fold (Right triangle)
            r.drawTriangle(x + 2.5f * scale, y + 1.5f * scale,
                           x + 6.5f * scale, cy,
                           x + 2.5f * scale, y + 6.5f * scale,
                           red, green, blue, alpha);
            return true;

        // Breakpoints & Execution
        case 0x25CF: // ● Active Breakpoint (Solid disc)
            r.drawCircle(cx, cy, 3.2f * scale, red, green, blue, alpha, 16);
            return true;
        case 0x25CB: // ○ Disabled Breakpoint (Hollow ring)
            r.drawCircleOutline(cx, cy, 3.0f * scale, red, green, blue, alpha, 1.4f * scale, 16);
            return true;
        case 0x25B6: // ▶ Execution Line Pointer
            r.drawTriangle(x + 1.5f * scale, y + 1.5f * scale,
                           x + 6.5f * scale, cy,
                           x + 1.5f * scale, y + 6.5f * scale,
                           red, green, blue, alpha);
            return true;

        // Diagnostics
        case 0x2713: // ✓ Success / Checkmark
            seg(x + 1.2f * scale, cy + 0.5f * scale, x + 3.2f * scale, y + 6.8f * scale, 1.8f);
            seg(x + 3.2f * scale, y + 6.8f * scale, x + 6.8f * scale, y + 1.4f * scale, 1.8f);
            return true;
        case 0x2717: // ✗ Error / Failure
        case 0x00D7: // × Multiplication / Close
            seg(x + 1.5f * scale, y + 1.5f * scale, x + 6.0f * scale, y + 6.5f * scale, 1.7f);
            seg(x + 6.0f * scale, y + 1.5f * scale, x + 1.5f * scale, y + 6.5f * scale, 1.7f);
            return true;
        case 0x26A0: // ⚠ Warning (Triangle with exclamation)
            r.drawTriangle(cx, y + 1.0f * scale,
                           x + 1.0f * scale, y + 7.0f * scale,
                           x + 6.5f * scale, y + 7.0f * scale,
                           red, green, blue, alpha);
            // Inset dark mark
            seg(cx, y + 3.0f * scale, cx, y + 5.0f * scale, 1.2f);
            r.drawCircle(cx, y + 6.2f * scale, 0.6f * scale, 0.08f, 0.08f, 0.10f, alpha);
            return true;
        case 0x2139: // ℹ Info
            r.drawCircleOutline(cx, cy, 3.2f * scale, red, green, blue, alpha, 1.2f * scale);
            r.drawCircle(cx, cy - 1.8f * scale, 0.6f * scale, red, green, blue, alpha);
            seg(cx, cy - 0.4f * scale, cx, cy + 1.8f * scale, 1.2f);
            return true;

        // Pipeline & Routing Arrows
        case 0x2192: // → Pipeline / Return Type
            seg(x + 1.0f * scale, cy, x + 6.5f * scale, cy, 1.5f);
            seg(x + 4.2f * scale, cy - 2.5f * scale, x + 6.5f * scale, cy, 1.5f);
            seg(x + 4.2f * scale, cy + 2.5f * scale, x + 6.5f * scale, cy, 1.5f);
            return true;
        case 0x2190: // ← Reverse Assignment
            seg(x + 1.0f * scale, cy, x + 6.5f * scale, cy, 1.5f);
            seg(x + 1.0f * scale, cy, x + 3.3f * scale, cy - 2.5f * scale, 1.5f);
            seg(x + 1.0f * scale, cy, x + 3.3f * scale, cy + 2.5f * scale, 1.5f);
            return true;

        default:
            break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 3. DSP Mathematics & Circuit Physics Pack
// ---------------------------------------------------------------------------

bool renderDspMathGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                        float red, float green, float blue, float alpha) {
    auto seg = [&](float x0, float y0, float x1, float y1, float w = 1.4f) {
        r.drawLine(x0, y0, x1, y1, red, green, blue, alpha, w * scale);
    };

    float w = 7.5f * scale;
    float h = 8.0f * scale;
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;

    switch (cp) {
        // Mathematical Constants
        case 0x03C0: // π Greek Pi
            seg(x + 1.0f * scale, y + 2.2f * scale, x + 6.5f * scale, y + 2.2f * scale, 1.5f);
            seg(x + 2.5f * scale, y + 2.2f * scale, x + 2.2f * scale, y + 7.5f * scale, 1.4f);
            seg(x + 5.0f * scale, y + 2.2f * scale, x + 5.2f * scale, y + 7.5f * scale, 1.4f);
            return true;
        case 0x03C4: // τ Greek Tau (2π)
            seg(x + 1.5f * scale, y + 2.2f * scale, x + 6.0f * scale, y + 2.2f * scale, 1.5f);
            seg(cx, y + 2.2f * scale, cx, y + 7.0f * scale, 1.4f);
            seg(cx, y + 7.0f * scale, cx + 1.5f * scale, y + 7.5f * scale, 1.4f);
            return true;
        case 0x00B1: // ± Plus-Minus (Bipolar modulation)
            seg(cx, y + 1.5f * scale, cx, y + 5.0f * scale, 1.4f);
            seg(x + 1.8f * scale, y + 3.25f * scale, x + 5.7f * scale, y + 3.25f * scale, 1.4f);
            seg(x + 1.8f * scale, y + 6.8f * scale, x + 5.7f * scale, y + 6.8f * scale, 1.4f);
            return true;
        case 0x00B0: // ° Degree (Phase angle)
            r.drawCircleOutline(x + 4.5f * scale, y + 2.2f * scale, 1.4f * scale, red, green, blue, alpha, 1.2f * scale);
            return true;
        case 0x221E: // ∞ Infinity (-∞ dB)
            r.drawCircleOutline(x + 2.4f * scale, cy, 1.8f * scale, red, green, blue, alpha, 1.2f * scale);
            r.drawCircleOutline(x + 5.1f * scale, cy, 1.8f * scale, red, green, blue, alpha, 1.2f * scale);
            return true;
        case 0x221A: // √ Square Root
            seg(x + 0.8f * scale, cy + 1.0f * scale, x + 2.2f * scale, y + 7.2f * scale, 1.4f);
            seg(x + 2.2f * scale, y + 7.2f * scale, x + 4.2f * scale, y + 1.2f * scale, 1.4f);
            seg(x + 4.2f * scale, y + 1.2f * scale, x + 6.8f * scale, y + 1.2f * scale, 1.4f);
            return true;

        // Electronic Units
        case 0x03BC: // μ Micro (μs, μF)
            seg(x + 1.8f * scale, cy - 1.5f * scale, x + 1.8f * scale, y + 7.8f * scale, 1.4f);
            seg(x + 1.8f * scale, cy + 2.0f * scale, x + 4.8f * scale, cy + 2.0f * scale, 1.4f);
            seg(x + 4.8f * scale, cy - 1.5f * scale, x + 4.8f * scale, cy + 2.0f * scale, 1.4f);
            return true;
        case 0x03A9: // Ω Ohm (Impedance)
            r.drawCircleOutline(cx, cy - 0.8f * scale, 2.6f * scale, red, green, blue, alpha, 1.3f * scale);
            r.drawRect(cx - 1.2f * scale, cy + 0.6f * scale, 2.4f * scale, 2.0f * scale, 0.08f, 0.09f, 0.12f, 1.0f);
            seg(x + 1.2f * scale, y + 7.0f * scale, x + 2.5f * scale, y + 7.0f * scale, 1.4f);
            seg(x + 5.0f * scale, y + 7.0f * scale, x + 6.3f * scale, y + 7.0f * scale, 1.4f);
            return true;

        // Comparison & Logic
        case 0x2260: // ≠ Not Equal
            seg(x + 1.5f * scale, cy - 1.2f * scale, x + 6.0f * scale, cy - 1.2f * scale, 1.3f);
            seg(x + 1.5f * scale, cy + 1.2f * scale, x + 6.0f * scale, cy + 1.2f * scale, 1.3f);
            seg(x + 2.2f * scale, y + 6.8f * scale, x + 5.3f * scale, y + 1.2f * scale, 1.3f);
            return true;
        case 0x2264: // ≤ Less Than or Equal
            seg(x + 5.5f * scale, cy - 2.5f * scale, x + 1.5f * scale, cy - 0.5f * scale, 1.3f);
            seg(x + 1.5f * scale, cy - 0.5f * scale, x + 5.5f * scale, cy + 1.5f * scale, 1.3f);
            seg(x + 1.5f * scale, cy + 3.2f * scale, x + 5.5f * scale, cy + 3.2f * scale, 1.3f);
            return true;
        case 0x2265: // ≥ Greater Than or Equal
            seg(x + 1.5f * scale, cy - 2.5f * scale, x + 5.5f * scale, cy - 0.5f * scale, 1.3f);
            seg(x + 5.5f * scale, cy - 0.5f * scale, x + 1.5f * scale, cy + 1.5f * scale, 1.3f);
            seg(x + 1.5f * scale, cy + 3.2f * scale, x + 5.5f * scale, cy + 3.2f * scale, 1.3f);
            return true;
        case 0x2248: // ≈ Approximately Equal
            seg(x + 1.5f * scale, cy - 1.4f * scale, cx, cy - 2.2f * scale, 1.3f);
            seg(cx, cy - 2.2f * scale, x + 6.0f * scale, cy - 1.4f * scale, 1.3f);
            seg(x + 1.5f * scale, cy + 1.6f * scale, cx, cy + 0.8f * scale, 1.3f);
            seg(cx, cy + 0.8f * scale, x + 6.0f * scale, cy + 1.6f * scale, 1.3f);
            return true;

        // Waveforms
        case 0x223F: // ∿ Sine Wave
            seg(x + 0.8f * scale, cy, x + 2.2f * scale, cy - 2.8f * scale, 1.5f);
            seg(x + 2.2f * scale, cy - 2.8f * scale, x + 5.3f * scale, cy + 2.8f * scale, 1.5f);
            seg(x + 5.3f * scale, cy + 2.8f * scale, x + 6.7f * scale, cy, 1.5f);
            return true;
        case 0x2293: // ⊓ Square Wave
            seg(x + 1.0f * scale, cy + 2.5f * scale, x + 1.0f * scale, cy - 2.5f * scale, 1.4f);
            seg(x + 1.0f * scale, cy - 2.5f * scale, cx, cy - 2.5f * scale, 1.4f);
            seg(cx, cy - 2.5f * scale, cx, cy + 2.5f * scale, 1.4f);
            seg(cx, cy + 2.5f * scale, x + 6.5f * scale, cy + 2.5f * scale, 1.4f);
            seg(x + 6.5f * scale, cy + 2.5f * scale, x + 6.5f * scale, cy - 2.5f * scale, 1.4f);
            return true;
        case 0x25B3: // △ Triangle Wave
            seg(x + 0.8f * scale, cy + 2.5f * scale, cx, cy - 2.8f * scale, 1.4f);
            seg(cx, cy - 2.8f * scale, x + 6.7f * scale, cy + 2.5f * scale, 1.4f);
            return true;
        case 0x22BF: // ⊿ Sawtooth Wave
            seg(x + 1.0f * scale, cy + 2.5f * scale, x + 6.5f * scale, cy - 2.5f * scale, 1.4f);
            seg(x + 6.5f * scale, cy - 2.5f * scale, x + 6.5f * scale, cy + 2.5f * scale, 1.4f);
            return true;

        default:
            break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 4. Music Score Notation Pack (SMuFL Bravura Béziers)
// ---------------------------------------------------------------------------

bool renderScoreGlyph(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                      float red, float green, float blue, float alpha) {
    ScoreVectorGlyphs::setBatchRenderer(&r);
    float sp = std::max(6.0f, scale * 5.0f);

    switch (cp) {
        // Clefs
        case 0x1D11E: // 𝄞 Treble G-Clef
            ScoreVectorGlyphs::drawTrebleClef(x + 1.5f * scale, y + 4.5f * scale, sp, red, green, blue, alpha);
            return true;
        case 0x1D122: // 𝄢 Bass F-Clef
            ScoreVectorGlyphs::drawBassClef(x + 1.5f * scale, y + 4.0f * scale, sp, red, green, blue, alpha);
            return true;

        // Accidentals
        case 0x266D: // ♭ Flat
            ScoreVectorGlyphs::drawFlat(x + 3.5f * scale, y + 4.0f * scale, sp * 0.9f, red, green, blue, alpha);
            return true;
        case 0x266F: // ♯ Sharp
            ScoreVectorGlyphs::drawSharp(x + 3.5f * scale, y + 4.0f * scale, sp * 0.9f, red, green, blue, alpha);
            return true;
        case 0x266E: // ♮ Natural
            ScoreVectorGlyphs::drawNatural(x + 3.5f * scale, y + 4.0f * scale, sp * 0.9f, red, green, blue, alpha);
            return true;

        // Rests
        case 0x1D13D: // 𝄽 Quarter Rest
            ScoreVectorGlyphs::drawQuarterRest(x + 3.5f * scale, y + 4.0f * scale, sp * 0.9f, red, green, blue, alpha);
            return true;

        // Traditional Note Glyphs
        case 0x2669: // ♩ Quarter note
            ScoreVectorGlyphs::drawBlackNotehead(x + 2.5f * scale, y + 6.0f * scale, sp * 0.8f, red, green, blue, alpha);
            r.drawLine(x + 4.2f * scale, y + 6.0f * scale, x + 4.2f * scale, y + 0.8f * scale, red, green, blue, alpha, 1.2f * scale);
            return true;
        case 0x266A: // ♪ Eighth note
            ScoreVectorGlyphs::drawBlackNotehead(x + 2.5f * scale, y + 6.0f * scale, sp * 0.8f, red, green, blue, alpha);
            r.drawLine(x + 4.2f * scale, y + 6.0f * scale, x + 4.2f * scale, y + 0.8f * scale, red, green, blue, alpha, 1.2f * scale);
            r.drawLine(x + 4.2f * scale, y + 0.8f * scale, x + 6.5f * scale, y + 2.8f * scale, red, green, blue, alpha, 1.4f * scale);
            return true;
        case 0x266B: // ♫ Beamed eighths
            ScoreVectorGlyphs::drawBlackNotehead(x + 1.8f * scale, y + 6.2f * scale, sp * 0.7f, red, green, blue, alpha);
            ScoreVectorGlyphs::drawBlackNotehead(x + 5.5f * scale, y + 5.2f * scale, sp * 0.7f, red, green, blue, alpha);
            r.drawLine(x + 3.2f * scale, y + 6.2f * scale, x + 3.2f * scale, y + 1.0f * scale, red, green, blue, alpha, 1.2f * scale);
            r.drawLine(x + 6.9f * scale, y + 5.2f * scale, x + 6.9f * scale, y + 0.0f * scale, red, green, blue, alpha, 1.2f * scale);
            r.drawLine(x + 3.2f * scale, y + 1.0f * scale, x + 6.9f * scale, y + 0.0f * scale, red, green, blue, alpha, 2.0f * scale);
            return true;

        default:
            break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Unified Glyph Registry Dispatcher
// ---------------------------------------------------------------------------

GlyphRegistry& GlyphRegistry::instance() {
    static GlyphRegistry s_instance;
    return s_instance;
}

GlyphRegistry::GlyphRegistry()
    : activePacks_(static_cast<uint32_t>(GlyphPack::All)) {}

bool GlyphRegistry::render(BatchRenderer2D& r, uint32_t cp, float x, float y, float scale,
                           float red, float green, float blue, float alpha) const {
    if (isPackEnabled(GlyphPack::Terminal)) {
        if (renderTerminalGlyph(r, cp, x, y, scale, red, green, blue, alpha)) return true;
    }
    if (isPackEnabled(GlyphPack::Editor)) {
        if (renderEditorGlyph(r, cp, x, y, scale, red, green, blue, alpha)) return true;
    }
    if (isPackEnabled(GlyphPack::DspMath)) {
        if (renderDspMathGlyph(r, cp, x, y, scale, red, green, blue, alpha)) return true;
    }
    if (isPackEnabled(GlyphPack::Score)) {
        if (renderScoreGlyph(r, cp, x, y, scale, red, green, blue, alpha)) return true;
    }
    if (isPackEnabled(GlyphPack::VectorFont) && cp >= 32 && cp <= 126) {
        float w = 7.5f * scale;
        float h = 13.0f * scale;
        if (renderVectorFontGlyph(r, cp, x, y, w, h, red, green, blue, alpha)) return true;
    }
    return false;
}

} // namespace eatsbits::ui::glyph
