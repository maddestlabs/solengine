#include "eatsbits/ui/glyph/vector_font.hpp"
#include <algorithm>
#include <cmath>

namespace eatsbits::ui::glyph {

namespace {

struct GlyphContext {
    BatchRenderer2D& r;
    float ox, oy;
    float sx, sy;
    float stemW;
    float barH;
    float red, green, blue, alpha;

    inline void vStem(float x, float y0, float y1) const {
        float px = ox + x * sx;
        float py0 = oy + y0 * sy;
        float py1 = oy + y1 * sy;
        float h = std::max(1.0f, py1 - py0);
        r.drawRect(px - stemW * 0.5f, py0, stemW, h, red, green, blue, alpha);
    }

    inline void hBar(float x0, float x1, float y) const {
        float px0 = ox + x0 * sx;
        float px1 = ox + x1 * sx;
        float py = oy + y * sy;
        float w = std::max(1.0f, px1 - px0);
        r.drawRect(px0, py - barH * 0.5f, w, barH, red, green, blue, alpha);
    }

    inline void box(float x0, float y0, float x1, float y1) const {
        float px0 = ox + x0 * sx;
        float py0 = oy + y0 * sy;
        float px1 = ox + x1 * sx;
        float py1 = oy + y1 * sy;
        r.drawRect(px0, py0, px1 - px0, py1 - py0, red, green, blue, alpha);
    }

    inline void diag(float x0, float y0, float x1, float y1, float width = 0.0f) const {
        float w = (width > 0.0f) ? width : std::min(stemW, barH) * 1.10f;
        r.drawLine(ox + x0 * sx, oy + y0 * sy, ox + x1 * sx, oy + y1 * sy, red, green, blue, alpha, w);
    }

    inline void chamferLoop(float x0, float y0, float x1, float y1, float c) const {
        hBar(x0 + c, x1 - c, y0);
        hBar(x0 + c, x1 - c, y1);
        vStem(x0, y0 + c, y1 - c);
        vStem(x1, y0 + c, y1 - c);
        diag(x0, y0 + c, x0 + c, y0);
        diag(x1 - c, y0, x1, y0 + c);
        diag(x1, y1 - c, x1 - c, y1);
        diag(x0 + c, y1, x0, y1 - c);
    }
};

void renderGlyphInternal(const GlyphContext& ctx, uint32_t cp) {
    switch (cp) {
        // --- Numbers ---
        case '0':
            ctx.chamferLoop(2.0f, 2.5f, 8.0f, 13.5f, 1.8f);
            ctx.diag(3.2f, 11.8f, 6.8f, 4.2f, ctx.barH * 0.95f);
            break;
        case '1':
            ctx.diag(2.8f, 4.8f, 5.0f, 2.5f, ctx.stemW);
            ctx.vStem(5.0f, 2.5f, 13.5f);
            ctx.hBar(2.5f, 7.5f, 13.5f);
            break;
        case '2':
            ctx.hBar(3.5f, 6.5f, 2.5f);
            ctx.diag(2.0f, 4.0f, 3.5f, 2.5f);
            ctx.vStem(8.0f, 4.0f, 6.5f);
            ctx.diag(6.5f, 2.5f, 8.0f, 4.0f);
            ctx.diag(8.0f, 6.5f, 2.0f, 13.5f, ctx.stemW * 1.05f);
            ctx.hBar(2.0f, 8.0f, 13.5f);
            break;
        case '3':
            ctx.hBar(2.0f, 8.0f, 2.5f);
            ctx.diag(8.0f, 2.5f, 4.8f, 7.2f);
            ctx.hBar(4.8f, 6.5f, 7.2f);
            ctx.vStem(8.0f, 8.5f, 11.8f);
            ctx.diag(6.5f, 7.2f, 8.0f, 8.5f);
            ctx.diag(8.0f, 11.8f, 6.5f, 13.5f);
            ctx.hBar(2.8f, 6.5f, 13.5f);
            ctx.diag(2.8f, 13.5f, 2.0f, 12.0f);
            break;
        case '4':
            ctx.vStem(7.0f, 2.5f, 13.5f);
            ctx.diag(7.0f, 2.5f, 1.8f, 8.5f, ctx.stemW);
            ctx.hBar(1.8f, 8.2f, 8.5f);
            break;
        case '5':
            ctx.hBar(2.0f, 8.0f, 2.5f);
            ctx.vStem(2.0f, 2.5f, 7.2f);
            ctx.hBar(2.0f, 6.5f, 7.2f);
            ctx.vStem(8.0f, 8.5f, 11.8f);
            ctx.diag(6.5f, 7.2f, 8.0f, 8.5f);
            ctx.diag(8.0f, 11.8f, 6.5f, 13.5f);
            ctx.hBar(2.8f, 6.5f, 13.5f);
            ctx.diag(2.8f, 13.5f, 2.0f, 12.0f);
            break;
        case '6':
            ctx.vStem(2.0f, 4.5f, 12.0f);
            ctx.diag(4.0f, 2.5f, 2.0f, 4.5f);
            ctx.hBar(4.0f, 7.5f, 2.5f);
            ctx.chamferLoop(2.0f, 7.2f, 8.0f, 13.5f, 1.6f);
            break;
        case '7':
            ctx.hBar(2.0f, 8.0f, 2.5f);
            ctx.diag(8.0f, 2.5f, 4.5f, 13.5f, ctx.stemW * 1.1f);
            ctx.hBar(3.5f, 6.5f, 7.5f);
            break;
        case '8':
            ctx.chamferLoop(2.2f, 2.5f, 7.8f, 7.8f, 1.4f);
            ctx.chamferLoop(2.0f, 7.8f, 8.0f, 13.5f, 1.6f);
            break;
        case '9':
            ctx.chamferLoop(2.0f, 2.5f, 8.0f, 8.8f, 1.6f);
            ctx.vStem(8.0f, 4.5f, 11.5f);
            ctx.diag(8.0f, 11.5f, 6.0f, 13.5f);
            ctx.hBar(2.5f, 6.0f, 13.5f);
            break;

        // --- Uppercase Letters ---
        case 'A':
            ctx.vStem(2.0f, 5.0f, 13.5f);
            ctx.vStem(8.0f, 5.0f, 13.5f);
            ctx.hBar(4.0f, 6.0f, 2.5f);
            ctx.diag(2.0f, 5.0f, 4.0f, 2.5f);
            ctx.diag(6.0f, 2.5f, 8.0f, 5.0f);
            ctx.hBar(2.0f, 8.0f, 8.5f);
            break;
        case 'B':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 6.5f, 2.5f);
            ctx.vStem(7.8f, 4.0f, 6.5f);
            ctx.diag(6.5f, 2.5f, 7.8f, 4.0f);
            ctx.diag(7.8f, 6.5f, 6.5f, 7.8f);
            ctx.hBar(2.0f, 6.5f, 7.8f);
            ctx.vStem(8.0f, 9.3f, 12.0f);
            ctx.diag(6.5f, 7.8f, 8.0f, 9.3f);
            ctx.diag(8.0f, 12.0f, 6.5f, 13.5f);
            ctx.hBar(2.0f, 6.5f, 13.5f);
            break;
        case 'C':
            ctx.hBar(3.8f, 8.0f, 2.5f);
            ctx.hBar(3.8f, 8.0f, 13.5f);
            ctx.vStem(2.0f, 4.2f, 11.8f);
            ctx.diag(2.0f, 4.2f, 3.8f, 2.5f);
            ctx.diag(2.0f, 11.8f, 3.8f, 13.5f);
            break;
        case 'D':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 6.0f, 2.5f);
            ctx.vStem(8.0f, 4.5f, 11.5f);
            ctx.diag(6.0f, 2.5f, 8.0f, 4.5f);
            ctx.diag(8.0f, 11.5f, 6.0f, 13.5f);
            ctx.hBar(2.0f, 6.0f, 13.5f);
            break;
        case 'E':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 8.0f, 2.5f);
            ctx.hBar(2.0f, 6.8f, 8.0f);
            ctx.hBar(2.0f, 8.0f, 13.5f);
            break;
        case 'F':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 8.0f, 2.5f);
            ctx.hBar(2.0f, 6.8f, 8.0f);
            break;
        case 'G':
            ctx.hBar(3.8f, 8.0f, 2.5f);
            ctx.vStem(2.0f, 4.2f, 11.8f);
            ctx.diag(2.0f, 4.2f, 3.8f, 2.5f);
            ctx.diag(2.0f, 11.8f, 3.8f, 13.5f);
            ctx.hBar(3.8f, 8.0f, 13.5f);
            ctx.vStem(8.0f, 8.5f, 13.5f);
            ctx.hBar(5.0f, 8.0f, 8.5f);
            break;
        case 'H':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.vStem(8.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 8.0f, 8.0f);
            break;
        case 'I':
            ctx.hBar(3.0f, 7.0f, 2.5f);
            ctx.vStem(5.0f, 2.5f, 13.5f);
            ctx.hBar(3.0f, 7.0f, 13.5f);
            break;
        case 'J':
            ctx.hBar(4.0f, 8.0f, 2.5f);
            ctx.vStem(7.0f, 2.5f, 11.8f);
            ctx.diag(7.0f, 11.8f, 5.2f, 13.5f);
            ctx.hBar(3.5f, 5.2f, 13.5f);
            ctx.diag(3.5f, 13.5f, 2.0f, 12.0f);
            break;
        case 'K':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.diag(8.0f, 2.5f, 2.0f, 8.5f, ctx.stemW);
            ctx.diag(4.5f, 6.2f, 8.0f, 13.5f, ctx.stemW * 1.05f);
            break;
        case 'L':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 8.0f, 13.5f);
            break;
        case 'M':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.vStem(8.0f, 2.5f, 13.5f);
            ctx.diag(2.0f, 2.5f, 5.0f, 8.5f, ctx.stemW);
            ctx.diag(5.0f, 8.5f, 8.0f, 2.5f, ctx.stemW);
            break;
        case 'N':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.vStem(8.0f, 2.5f, 13.5f);
            ctx.diag(2.0f, 2.5f, 8.0f, 13.5f, ctx.stemW * 1.15f);
            break;
        case 'O':
            ctx.chamferLoop(2.0f, 2.5f, 8.0f, 13.5f, 1.8f);
            break;
        case 'P':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 6.5f, 2.5f);
            ctx.vStem(8.0f, 4.0f, 6.8f);
            ctx.diag(6.5f, 2.5f, 8.0f, 4.0f);
            ctx.diag(8.0f, 6.8f, 6.5f, 8.2f);
            ctx.hBar(2.0f, 6.5f, 8.2f);
            break;
        case 'Q':
            ctx.chamferLoop(2.0f, 2.5f, 8.0f, 13.5f, 1.8f);
            ctx.diag(5.2f, 11.2f, 8.8f, 14.8f, ctx.stemW * 1.15f);
            break;
        case 'R':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(2.0f, 6.5f, 2.5f);
            ctx.vStem(8.0f, 4.0f, 6.8f);
            ctx.diag(6.5f, 2.5f, 8.0f, 4.0f);
            ctx.diag(8.0f, 6.8f, 6.5f, 8.2f);
            ctx.hBar(2.0f, 6.5f, 8.2f);
            ctx.diag(5.0f, 8.2f, 8.2f, 13.5f, ctx.stemW * 1.15f);
            break;
        case 'S':
            ctx.hBar(3.5f, 7.5f, 2.5f);
            ctx.vStem(2.0f, 4.0f, 6.5f);
            ctx.diag(2.0f, 4.0f, 3.5f, 2.5f);
            ctx.diag(2.0f, 6.5f, 8.0f, 9.5f, ctx.stemW);
            ctx.vStem(8.0f, 9.5f, 12.0f);
            ctx.diag(8.0f, 12.0f, 6.5f, 13.5f);
            ctx.hBar(2.5f, 6.5f, 13.5f);
            break;
        case 'T':
            ctx.hBar(1.5f, 8.5f, 2.5f);
            ctx.vStem(5.0f, 2.5f, 13.5f);
            break;
        case 'U':
            ctx.vStem(2.0f, 2.5f, 11.8f);
            ctx.vStem(8.0f, 2.5f, 11.8f);
            ctx.diag(2.0f, 11.8f, 3.8f, 13.5f);
            ctx.diag(6.2f, 13.5f, 8.0f, 11.8f);
            ctx.hBar(3.8f, 6.2f, 13.5f);
            break;
        case 'V':
            ctx.diag(2.0f, 2.5f, 5.0f, 13.5f, ctx.stemW * 1.15f);
            ctx.diag(5.0f, 13.5f, 8.0f, 2.5f, ctx.stemW * 1.15f);
            break;
        case 'W':
            ctx.diag(1.8f, 2.5f, 3.4f, 13.5f, ctx.stemW);
            ctx.diag(3.4f, 13.5f, 5.0f, 6.8f, ctx.stemW);
            ctx.diag(5.0f, 6.8f, 6.6f, 13.5f, ctx.stemW);
            ctx.diag(6.6f, 13.5f, 8.2f, 2.5f, ctx.stemW);
            break;
        case 'X':
            ctx.diag(2.0f, 2.5f, 8.0f, 13.5f, ctx.stemW * 1.15f);
            ctx.diag(8.0f, 2.5f, 2.0f, 13.5f, ctx.stemW * 1.15f);
            break;
        case 'Y':
            ctx.diag(2.0f, 2.5f, 5.0f, 7.8f, ctx.stemW);
            ctx.diag(8.0f, 2.5f, 5.0f, 7.8f, ctx.stemW);
            ctx.vStem(5.0f, 7.8f, 13.5f);
            break;
        case 'Z':
            ctx.hBar(2.0f, 8.0f, 2.5f);
            ctx.diag(8.0f, 2.5f, 2.0f, 13.5f, ctx.stemW * 1.15f);
            ctx.hBar(2.0f, 8.0f, 13.5f);
            break;

        // --- Lowercase Letters ---
        case 'a':
            ctx.vStem(7.8f, 5.2f, 13.5f);
            ctx.hBar(3.5f, 6.5f, 5.2f);
            ctx.diag(2.0f, 6.8f, 3.5f, 5.2f);
            ctx.vStem(2.0f, 6.8f, 12.0f);
            ctx.diag(2.0f, 12.0f, 3.5f, 13.5f);
            ctx.hBar(3.5f, 7.8f, 13.5f);
            ctx.hBar(2.0f, 7.8f, 9.0f);
            break;
        case 'b':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.chamferLoop(2.0f, 5.5f, 8.0f, 13.5f, 1.6f);
            break;
        case 'c':
            ctx.hBar(3.8f, 8.0f, 5.5f);
            ctx.vStem(2.0f, 7.2f, 11.8f);
            ctx.diag(2.0f, 7.2f, 3.8f, 5.5f);
            ctx.diag(2.0f, 11.8f, 3.8f, 13.5f);
            ctx.hBar(3.8f, 8.0f, 13.5f);
            break;
        case 'd':
            ctx.vStem(8.0f, 2.5f, 13.5f);
            ctx.chamferLoop(2.0f, 5.5f, 8.0f, 13.5f, 1.6f);
            break;
        case 'e':
            ctx.hBar(2.0f, 8.0f, 9.0f);
            ctx.vStem(8.0f, 6.8f, 9.0f);
            ctx.diag(6.5f, 5.5f, 8.0f, 6.8f);
            ctx.hBar(3.5f, 6.5f, 5.5f);
            ctx.diag(2.0f, 7.0f, 3.5f, 5.5f);
            ctx.vStem(2.0f, 7.0f, 12.0f);
            ctx.diag(2.0f, 12.0f, 3.5f, 13.5f);
            ctx.hBar(3.5f, 8.0f, 13.5f);
            break;
        case 'f':
            ctx.hBar(2.5f, 7.0f, 6.5f);
            ctx.vStem(4.5f, 4.0f, 13.5f);
            ctx.diag(4.5f, 4.0f, 6.0f, 2.5f);
            ctx.hBar(6.0f, 7.5f, 2.5f);
            break;
        case 'g':
            ctx.chamferLoop(2.0f, 5.5f, 8.0f, 11.2f, 1.5f);
            ctx.vStem(8.0f, 11.2f, 14.8f);
            ctx.diag(8.0f, 14.8f, 6.5f, 16.0f);
            ctx.hBar(2.8f, 6.5f, 16.0f);
            break;
        case 'h':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.hBar(3.8f, 6.5f, 5.5f);
            ctx.diag(2.0f, 7.2f, 3.8f, 5.5f);
            ctx.diag(6.5f, 5.5f, 8.0f, 7.2f);
            ctx.vStem(8.0f, 7.2f, 13.5f);
            break;
        case 'i':
            ctx.box(4.2f, 2.5f, 5.8f, 3.8f); // Dot
            ctx.vStem(5.0f, 5.5f, 13.5f);
            break;
        case 'j':
            ctx.box(5.2f, 2.5f, 6.8f, 3.8f); // Dot
            ctx.vStem(6.0f, 5.5f, 14.5f);
            ctx.diag(6.0f, 14.5f, 4.2f, 16.0f);
            ctx.hBar(2.5f, 4.2f, 16.0f);
            break;
        case 'k':
            ctx.vStem(2.0f, 2.5f, 13.5f);
            ctx.diag(7.5f, 5.5f, 2.0f, 10.0f, ctx.stemW);
            ctx.diag(4.0f, 8.5f, 7.8f, 13.5f, ctx.stemW);
            break;
        case 'l':
            ctx.vStem(4.5f, 2.5f, 12.0f);
            ctx.diag(4.5f, 12.0f, 6.0f, 13.5f);
            ctx.hBar(6.0f, 7.2f, 13.5f);
            break;
        case 'm':
            ctx.vStem(1.8f, 5.5f, 13.5f);
            ctx.hBar(1.8f, 4.5f, 5.5f);
            ctx.vStem(5.0f, 6.5f, 13.5f);
            ctx.hBar(5.0f, 7.8f, 5.5f);
            ctx.vStem(8.2f, 6.5f, 13.5f);
            break;
        case 'n':
            ctx.vStem(2.0f, 5.5f, 13.5f);
            ctx.hBar(3.5f, 6.5f, 5.5f);
            ctx.diag(2.0f, 7.0f, 3.5f, 5.5f);
            ctx.diag(6.5f, 5.5f, 8.0f, 7.0f);
            ctx.vStem(8.0f, 7.0f, 13.5f);
            break;
        case 'o':
            ctx.chamferLoop(2.0f, 5.5f, 8.0f, 13.5f, 1.6f);
            break;
        case 'p':
            ctx.vStem(2.0f, 5.5f, 16.2f);
            ctx.chamferLoop(2.0f, 5.5f, 8.0f, 11.5f, 1.6f);
            break;
        case 'q':
            ctx.vStem(8.0f, 5.5f, 16.2f);
            ctx.chamferLoop(2.0f, 5.5f, 8.0f, 11.5f, 1.6f);
            break;
        case 'r':
            ctx.vStem(2.5f, 5.5f, 13.5f);
            ctx.hBar(2.5f, 6.0f, 5.5f);
            ctx.diag(6.0f, 5.5f, 7.8f, 7.0f);
            break;
        case 's':
            ctx.hBar(3.5f, 7.5f, 5.5f);
            ctx.vStem(2.0f, 6.8f, 8.5f);
            ctx.diag(2.0f, 6.8f, 3.5f, 5.5f);
            ctx.diag(2.0f, 8.5f, 8.0f, 10.8f, ctx.stemW * 0.95f);
            ctx.vStem(8.0f, 10.8f, 12.2f);
            ctx.diag(8.0f, 12.2f, 6.5f, 13.5f);
            ctx.hBar(2.5f, 6.5f, 13.5f);
            break;
        case 't':
            ctx.hBar(2.2f, 7.2f, 5.5f);
            ctx.vStem(4.5f, 3.0f, 12.0f);
            ctx.diag(4.5f, 12.0f, 6.0f, 13.5f);
            ctx.hBar(6.0f, 7.2f, 13.5f);
            break;
        case 'u':
            ctx.vStem(2.0f, 5.5f, 11.8f);
            ctx.vStem(8.0f, 5.5f, 13.5f);
            ctx.diag(2.0f, 11.8f, 3.8f, 13.5f);
            ctx.hBar(3.8f, 8.0f, 13.5f);
            break;
        case 'v':
            ctx.diag(2.0f, 5.5f, 5.0f, 13.5f, ctx.stemW * 1.1f);
            ctx.diag(5.0f, 13.5f, 8.0f, 5.5f, ctx.stemW * 1.1f);
            break;
        case 'w':
            ctx.diag(1.8f, 5.5f, 3.2f, 13.5f, ctx.stemW * 0.95f);
            ctx.diag(3.2f, 13.5f, 5.0f, 8.2f, ctx.stemW * 0.95f);
            ctx.diag(5.0f, 8.2f, 6.8f, 13.5f, ctx.stemW * 0.95f);
            ctx.diag(6.8f, 13.5f, 8.2f, 5.5f, ctx.stemW * 0.95f);
            break;
        case 'x':
            ctx.diag(2.0f, 5.5f, 8.0f, 13.5f, ctx.stemW * 1.05f);
            ctx.diag(8.0f, 5.5f, 2.0f, 13.5f, ctx.stemW * 1.05f);
            break;
        case 'y':
            ctx.diag(2.0f, 5.5f, 5.0f, 11.2f, ctx.stemW);
            ctx.diag(8.0f, 5.5f, 5.0f, 11.2f, ctx.stemW);
            ctx.vStem(5.0f, 11.2f, 14.8f);
            ctx.diag(5.0f, 14.8f, 3.2f, 16.0f);
            ctx.hBar(2.2f, 3.2f, 16.0f);
            break;
        case 'z':
            ctx.hBar(2.0f, 8.0f, 5.5f);
            ctx.diag(8.0f, 5.5f, 2.0f, 13.5f, ctx.stemW * 1.10f);
            ctx.hBar(2.0f, 8.0f, 13.5f);
            break;

        // --- Punctuation & Coding Symbols ---
        case '!':
            ctx.vStem(5.0f, 2.5f, 9.5f);
            ctx.box(4.2f, 12.0f, 5.8f, 13.5f);
            break;
        case '"':
            ctx.vStem(3.5f, 2.5f, 6.5f);
            ctx.vStem(6.5f, 2.5f, 6.5f);
            break;
        case '\'':
            ctx.vStem(5.0f, 2.5f, 6.5f);
            break;
        case '#':
            ctx.vStem(3.5f, 2.5f, 13.5f);
            ctx.vStem(6.5f, 2.5f, 13.5f);
            ctx.hBar(1.5f, 8.5f, 6.0f);
            ctx.hBar(1.5f, 8.5f, 10.0f);
            break;
        case '$':
            ctx.chamferLoop(2.5f, 3.8f, 7.5f, 12.2f, 1.4f);
            ctx.hBar(2.5f, 7.5f, 8.0f);
            ctx.vStem(5.0f, 1.5f, 14.5f);
            break;
        case '%':
            ctx.diag(8.0f, 2.5f, 2.0f, 13.5f, ctx.stemW);
            ctx.box(2.5f, 3.0f, 4.5f, 5.0f);
            ctx.box(5.5f, 11.0f, 7.5f, 13.0f);
            break;
        case '&':
            ctx.chamferLoop(3.5f, 2.5f, 6.5f, 6.5f, 1.2f);
            ctx.chamferLoop(2.0f, 6.5f, 8.0f, 13.5f, 1.5f);
            ctx.diag(6.5f, 9.0f, 8.5f, 13.5f, ctx.stemW);
            break;
        case '(':
            ctx.diag(6.5f, 1.5f, 3.8f, 5.0f, ctx.stemW);
            ctx.vStem(3.8f, 5.0f, 11.0f);
            ctx.diag(3.8f, 11.0f, 6.5f, 14.5f, ctx.stemW);
            break;
        case ')':
            ctx.diag(3.5f, 1.5f, 6.2f, 5.0f, ctx.stemW);
            ctx.vStem(6.2f, 5.0f, 11.0f);
            ctx.diag(6.2f, 11.0f, 3.5f, 14.5f, ctx.stemW);
            break;
        case '[':
            ctx.hBar(3.0f, 7.0f, 1.5f);
            ctx.vStem(3.0f, 1.5f, 14.5f);
            ctx.hBar(3.0f, 7.0f, 14.5f);
            break;
        case ']':
            ctx.hBar(3.0f, 7.0f, 1.5f);
            ctx.vStem(7.0f, 1.5f, 14.5f);
            ctx.hBar(3.0f, 7.0f, 14.5f);
            break;
        case '{':
            ctx.hBar(4.8f, 7.0f, 1.5f);
            ctx.vStem(4.8f, 1.5f, 6.5f);
            ctx.diag(4.8f, 6.5f, 2.5f, 8.0f, ctx.stemW);
            ctx.diag(2.5f, 8.0f, 4.8f, 9.5f, ctx.stemW);
            ctx.vStem(4.8f, 9.5f, 14.5f);
            ctx.hBar(4.8f, 7.0f, 14.5f);
            break;
        case '}':
            ctx.hBar(3.0f, 5.2f, 1.5f);
            ctx.vStem(5.2f, 1.5f, 6.5f);
            ctx.diag(5.2f, 6.5f, 7.5f, 8.0f, ctx.stemW);
            ctx.diag(7.5f, 8.0f, 5.2f, 9.5f, ctx.stemW);
            ctx.vStem(5.2f, 9.5f, 14.5f);
            ctx.hBar(3.0f, 5.2f, 14.5f);
            break;
        case '*':
            ctx.vStem(5.0f, 4.5f, 11.5f);
            ctx.diag(2.2f, 5.5f, 7.8f, 10.5f, ctx.barH);
            ctx.diag(7.8f, 5.5f, 2.2f, 10.5f, ctx.barH);
            break;
        case '+':
            ctx.vStem(5.0f, 4.0f, 12.0f);
            ctx.hBar(1.5f, 8.5f, 8.0f);
            break;
        case '-':
            ctx.hBar(2.0f, 8.0f, 8.0f);
            break;
        case '=':
            ctx.hBar(2.0f, 8.0f, 6.2f);
            ctx.hBar(2.0f, 8.0f, 9.8f);
            break;
        case '<':
            ctx.diag(7.5f, 3.5f, 2.5f, 8.0f, ctx.stemW);
            ctx.diag(2.5f, 8.0f, 7.5f, 12.5f, ctx.stemW);
            break;
        case '>':
            ctx.diag(2.5f, 3.5f, 7.5f, 8.0f, ctx.stemW);
            ctx.diag(7.5f, 8.0f, 2.5f, 12.5f, ctx.stemW);
            break;
        case ':':
            ctx.box(4.2f, 5.0f, 5.8f, 6.5f);
            ctx.box(4.2f, 11.0f, 5.8f, 12.5f);
            break;
        case ';':
            ctx.box(4.2f, 5.0f, 5.8f, 6.5f);
            ctx.vStem(5.0f, 11.0f, 13.0f);
            ctx.diag(5.0f, 13.0f, 3.2f, 15.0f, ctx.stemW * 0.85f);
            break;
        case ',':
            ctx.vStem(5.0f, 11.5f, 13.5f);
            ctx.diag(5.0f, 13.5f, 3.2f, 15.5f, ctx.stemW * 0.85f);
            break;
        case '.':
            ctx.box(4.2f, 12.0f, 5.8f, 13.5f);
            break;
        case '/':
            ctx.diag(2.0f, 14.5f, 8.0f, 1.5f, ctx.stemW);
            break;
        case '\\':
            ctx.diag(2.0f, 1.5f, 8.0f, 14.5f, ctx.stemW);
            break;
        case '|':
            ctx.vStem(5.0f, 1.0f, 15.0f);
            break;
        case '_':
            ctx.hBar(0.8f, 9.2f, 15.2f);
            break;
        case '^':
            ctx.diag(2.2f, 6.5f, 5.0f, 2.5f, ctx.stemW);
            ctx.diag(5.0f, 2.5f, 7.8f, 6.5f, ctx.stemW);
            break;
        case '~':
            ctx.diag(1.8f, 8.0f, 3.8f, 6.2f, ctx.barH);
            ctx.diag(3.8f, 6.2f, 6.2f, 9.8f, ctx.barH);
            ctx.diag(6.2f, 9.8f, 8.2f, 8.0f, ctx.barH);
            break;
        case '?':
            ctx.hBar(3.5f, 6.5f, 2.5f);
            ctx.diag(2.0f, 4.0f, 3.5f, 2.5f);
            ctx.vStem(8.0f, 4.0f, 6.2f);
            ctx.diag(6.5f, 2.5f, 8.0f, 4.0f);
            ctx.diag(8.0f, 6.2f, 5.0f, 8.5f, ctx.stemW);
            ctx.vStem(5.0f, 8.5f, 10.0f);
            ctx.box(4.2f, 12.0f, 5.8f, 13.5f);
            break;
        case '@':
            ctx.chamferLoop(3.5f, 5.5f, 7.0f, 10.5f, 1.2f); // Inner 'a'
            ctx.vStem(7.0f, 5.5f, 11.5f);
            ctx.diag(7.0f, 11.5f, 8.5f, 11.5f);
            ctx.vStem(8.8f, 4.0f, 11.5f);
            ctx.diag(7.0f, 2.0f, 8.8f, 4.0f);
            ctx.hBar(3.5f, 7.0f, 2.0f);
            ctx.diag(1.5f, 4.5f, 3.5f, 2.0f);
            ctx.vStem(1.5f, 4.5f, 12.0f);
            ctx.diag(1.5f, 12.0f, 4.0f, 14.5f);
            ctx.hBar(4.0f, 7.5f, 14.5f);
            break;
        case '`':
            ctx.diag(4.0f, 2.5f, 6.0f, 4.5f, ctx.stemW);
            break;

        default:
            break;
    }
}

} // anonymous namespace

bool renderVectorFontGlyph(BatchRenderer2D& r, uint32_t cp,
                           float x, float y, float w, float h,
                           float red, float green, float blue, float alpha) {
    if (cp < 32 || cp > 126) return false;
    if (cp == 32) return true; // Space

    GlyphContext ctx{
        r,
        x, y,
        w / 10.0f,
        h / 16.0f,
        std::max(1.25f, w * 0.175f), // Optical vertical stem with subpixel smooth sizing
        std::max(1.15f, h * 0.095f), // Optical horizontal bar with subpixel smooth sizing
        red, green, blue, alpha
    };

    renderGlyphInternal(ctx, cp);
    return true;
}

void drawVectorText(BatchRenderer2D& r, float x, float y, float size,
                    std::string_view text,
                    float red, float green, float blue, float alpha) {
    float curX = x;
    float curY = y;
    float cellW = size * 0.588f;
    float cellH = size;

    for (char c : text) {
        if (c == '\n') {
            curY += cellH * 1.25f;
            curX = x;
            continue;
        }
        if (c == '\r') {
            curX = x;
            continue;
        }
        if (c == '\t') {
            float col = (curX - x) / cellW;
            int nextCol = ((static_cast<int>(col) / 4) + 1) * 4;
            curX = x + static_cast<float>(nextCol) * cellW;
            continue;
        }
        renderVectorFontGlyph(r, static_cast<uint32_t>(c), curX, curY, cellW, cellH, red, green, blue, alpha);
        curX += cellW;
    }
}

float getVectorTextWidth(float size, std::string_view text) noexcept {
    float cellW = size * 0.588f;
    float maxW = 0.0f;
    float curW = 0.0f;
    for (char c : text) {
        if (c == '\n') {
            if (curW > maxW) maxW = curW;
            curW = 0.0f;
            continue;
        }
        curW += cellW;
    }
    return std::max(maxW, curW);
}

} // namespace eatsbits::ui::glyph
