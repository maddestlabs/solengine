#ifndef EATS_SCORE_GLYPHS_HPP
#define EATS_SCORE_GLYPHS_HPP

#include <vector>
#include <cmath>
#include <numbers>
#include <cstdint>
#include "../batch_renderer_2d.hpp"

namespace eatsbits::ui {

/**
 * 2D Vector Path for tessellating and rendering complex cubic Bézier glyphs.
 */
class ScoreVectorPath {
public:
    struct Point { float x{0.0f}, y{0.0f}; };
    using Contour = std::vector<Point>;
    std::vector<Contour> contours;

    void moveTo(float x, float y) {
        contours.push_back({});
        contours.back().push_back({x, y});
    }

    void lineTo(float x, float y) {
        if (contours.empty()) moveTo(x, y);
        else contours.back().push_back({x, y});
    }

    void cubicTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y, int steps = 10) {
        if (contours.empty()) moveTo(cp1x, cp1y);
        Point p0 = contours.back().back();
        for (int i = 1; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            float u = 1.0f - t;
            float tt = t * t;
            float uu = u * u;
            float uuu = uu * u;
            float ttt = tt * t;

            float px = uuu * p0.x + 3.0f * uu * t * cp1x + 3.0f * u * tt * cp2x + ttt * x;
            float py = uuu * p0.y + 3.0f * uu * t * cp1y + 3.0f * u * tt * cp2y + ttt * y;
            contours.back().push_back({px, py});
        }
    }

    void close() {
        // Closed contour boundary
    }

    void drawFill(float r, float g, float b, float a = 1.0f) const;
    void drawStroke(float r, float g, float b, float a = 1.0f, float lineWidth = 1.5f) const;
};

/**
 * Musical glyph vector rendering using exact mathematical Bézier cubic splines
 * adapted from Steinberg's Bravura reference font (SMuFL specification under SIL OFL 1.1).
 */
class ScoreVectorGlyphs {
public:
    static constexpr float defaultStaffSpace = 13.0f;

    inline static BatchRenderer2D* s_batchRenderer = nullptr;
    static void setBatchRenderer(BatchRenderer2D* r) noexcept { s_batchRenderer = r; }
    static BatchRenderer2D* getBatchRenderer() noexcept { return s_batchRenderer; }

    // ---------------------------------------------------------------------------
    // Noteheads
    // ---------------------------------------------------------------------------

    /// Draws a standard filled (quarter/8th/16th) notehead centered at (cx, cy).
    /// Rotated approx -15 degrees for classical engraving aesthetic.
    static void drawBlackNotehead(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        BatchRenderer2D* renderer = s_batchRenderer;
        if (!renderer) return;

        const float rx = sp * 0.62f;
        const float ry = sp * 0.44f;
        constexpr float rad = -15.0f * 3.14159265f / 180.0f;
        const float cosR = std::cos(rad);
        const float sinR = std::sin(rad);

        constexpr int SEGMENTS = 32;
        float prevX = 0.0f, prevY = 0.0f;
        for (int i = 0; i <= SEGMENTS; ++i) {
            float theta = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(SEGMENTS);
            float ex = rx * std::cos(theta);
            float ey = ry * std::sin(theta);
            float curX = cx + ex * cosR - ey * sinR;
            float curY = cy + ex * sinR + ey * cosR;
            if (i > 0) {
                renderer->drawTriangle(cx, cy, prevX, prevY, curX, curY, r, g, b, a);
            }
            prevX = curX;
            prevY = curY;
        }
    }

    /// Draws a half-note notehead (open oval with classical angle) centered at (cx, cy).
    static void drawHalfNotehead(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        BatchRenderer2D* renderer = s_batchRenderer;
        if (!renderer) return;

        const float rx = sp * 0.68f;
        const float ry = sp * 0.44f;
        constexpr float rad = -15.0f * 3.14159265f / 180.0f;
        const float cosR = std::cos(rad);
        const float sinR = std::sin(rad);

        constexpr int SEGMENTS = 32;
        float prevX = 0.0f, prevY = 0.0f;
        for (int i = 0; i <= SEGMENTS; ++i) {
            float theta = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(SEGMENTS);
            float ex = rx * std::cos(theta);
            float ey = ry * std::sin(theta);
            float curX = cx + ex * cosR - ey * sinR;
            float curY = cy + ex * sinR + ey * cosR;
            if (i > 0) {
                renderer->drawLine(prevX, prevY, curX, curY, r, g, b, a, sp * 0.20f);
            }
            prevX = curX;
            prevY = curY;
        }
    }

    /// Draws a whole-note notehead centered at (cx, cy).
    static void drawWholeNotehead(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        BatchRenderer2D* renderer = s_batchRenderer;
        if (!renderer) return;

        const float rx = sp * 0.78f;
        const float ry = sp * 0.48f;

        constexpr int SEGMENTS = 24;
        float prevX = 0.0f, prevY = 0.0f;
        for (int i = 0; i <= SEGMENTS; ++i) {
            float theta = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(SEGMENTS);
            float curX = cx + rx * std::cos(theta);
            float curY = cy + ry * std::sin(theta);
            if (i > 0) {
                renderer->drawLine(prevX, prevY, curX, curY, r, g, b, a, sp * 0.22f);
            }
            prevX = curX;
            prevY = curY;
        }
    }

    // ---------------------------------------------------------------------------
    // Stems & Flags
    // ---------------------------------------------------------------------------

    static void drawStem(float noteX, float noteY, float sp, bool isUp, float r, float g, float b, float a = 1.0f, float lengthMult = 3.5f) {
        BatchRenderer2D* renderer = s_batchRenderer;
        if (!renderer) return;

        const float stemThickness = sp * 0.12f;
        const float xOffset = isUp ? (sp * 0.60f) : -(sp * 0.60f);
        const float startX = noteX + xOffset;
        const float startY = noteY;
        const float endY = isUp ? (noteY - sp * lengthMult) : (noteY + sp * lengthMult);

        renderer->drawLine(startX, startY, startX, endY, r, g, b, a, stemThickness);
    }

    static void drawStemUp(float noteX, float noteY, float sp, float r, float g, float b, float a = 1.0f, float lengthMult = 3.5f) {
        drawStem(noteX, noteY, sp, true, r, g, b, a, lengthMult);
    }

    static void drawStemDown(float noteX, float noteY, float sp, float r, float g, float b, float a = 1.0f, float lengthMult = 3.5f) {
        drawStem(noteX, noteY, sp, false, r, g, b, a, lengthMult);
    }

    // ---------------------------------------------------------------------------
    // Ledger Lines
    // ---------------------------------------------------------------------------

    static void drawLedgerLine(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        BatchRenderer2D* renderer = s_batchRenderer;
        if (!renderer) return;

        const float width = sp * 1.9f;
        const float thickness = sp * 0.11f;

        renderer->drawLine(cx - width * 0.5f, cy, cx + width * 0.5f, cy, r, g, b, a, thickness);
    }

    // ---------------------------------------------------------------------------
    // Rests
    // ---------------------------------------------------------------------------

    static void drawBlockRest(float cx, float cy, float sp, bool isWhole, float r, float g, float b, float a = 1.0f) {
        BatchRenderer2D* renderer = s_batchRenderer;
        if (!renderer) return;

        const float width = sp * 1.2f;
        const float height = sp * 0.5f;
        const float y = isWhole ? cy : (cy - height);

        renderer->drawRect(cx - width * 0.5f, y, width, height, r, g, b, a);
    }

    // ---------------------------------------------------------------------------
    // Clefs (Exact SMuFL Bravura Béziers)
    // ---------------------------------------------------------------------------

    static ScoreVectorPath createTrebleClefPath(float x, float y, float sp) {
        ScoreVectorPath p;
        p.moveTo(x + 1.50400f * sp, y - 1.66000f * sp);
        p.cubicTo(x + 1.49600f * sp, y - 1.70800f * sp, x + 1.50400f * sp, y - 1.71200f * sp, x + 1.52800f * sp, y - 1.73600f * sp);
        p.cubicTo(x + 1.59200f * sp, y - 1.79600f * sp, x + 1.67600f * sp, y - 1.88000f * sp, x + 1.75200f * sp, y - 1.96400f * sp);
        p.cubicTo(x + 2.08800f * sp, y - 2.33200f * sp, x + 2.28800f * sp, y - 2.80800f * sp, x + 2.28800f * sp, y - 3.26000f * sp);
        p.cubicTo(x + 2.28800f * sp, y - 3.60800f * sp, x + 2.19200f * sp, y - 3.95200f * sp, x + 2.02800f * sp, y - 4.19200f * sp);
        p.cubicTo(x + 1.96800f * sp, y - 4.28000f * sp, x + 1.86400f * sp, y - 4.39200f * sp, x + 1.82000f * sp, y - 4.39200f * sp);
        p.cubicTo(x + 1.76400f * sp, y - 4.39200f * sp, x + 1.64000f * sp, y - 4.28800f * sp, x + 1.56000f * sp, y - 4.20000f * sp);
        p.cubicTo(x + 1.26400f * sp, y - 3.87200f * sp, x + 1.16800f * sp, y - 3.37200f * sp, x + 1.16800f * sp, y - 2.95600f * sp);
        p.cubicTo(x + 1.16800f * sp, y - 2.72400f * sp, x + 1.19600f * sp, y - 2.46400f * sp, x + 1.22400f * sp, y - 2.30000f * sp);
        p.cubicTo(x + 1.23200f * sp, y - 2.25200f * sp, x + 1.23600f * sp, y - 2.24400f * sp, x + 1.18800f * sp, y - 2.20400f * sp);
        p.cubicTo(x + 0.93200f * sp, y - 1.99200f * sp, x + 0.65600f * sp, y - 1.74800f * sp, x + 0.44800f * sp, y - 1.49200f * sp);
        p.cubicTo(x + 0.17200f * sp, y - 1.14800f * sp, x + 0.00000f * sp, y - 0.77600f * sp, x + 0.00000f * sp, y - 0.34800f * sp);
        p.cubicTo(x + 0.00000f * sp, y - -0.34800f * sp, x + 0.47600f * sp, y - -1.00800f * sp, x + 1.45600f * sp, y - -1.00800f * sp);
        p.cubicTo(x + 1.54800f * sp, y - -1.00800f * sp, x + 1.65200f * sp, y - -1.00000f * sp, x + 1.73200f * sp, y - -0.98400f * sp);
        p.cubicTo(x + 1.77600f * sp, y - -0.97600f * sp, x + 1.78400f * sp, y - -0.97200f * sp, x + 1.79200f * sp, y - -1.02000f * sp);
        p.cubicTo(x + 1.84000f * sp, y - -1.28800f * sp, x + 1.90000f * sp, y - -1.63600f * sp, x + 1.90000f * sp, y - -1.82400f * sp);
        p.cubicTo(x + 1.90000f * sp, y - -2.41600f * sp, x + 1.50000f * sp, y - -2.48800f * sp, x + 1.26400f * sp, y - -2.48800f * sp);
        p.cubicTo(x + 1.04800f * sp, y - -2.48800f * sp, x + 0.94400f * sp, y - -2.42400f * sp, x + 0.94400f * sp, y - -2.37200f * sp);
        p.cubicTo(x + 0.94400f * sp, y - -2.34400f * sp, x + 0.98000f * sp, y - -2.33200f * sp, x + 1.07200f * sp, y - -2.30400f * sp);
        p.cubicTo(x + 1.19600f * sp, y - -2.26800f * sp, x + 1.34000f * sp, y - -2.16000f * sp, x + 1.34000f * sp, y - -1.92800f * sp);
        p.cubicTo(x + 1.34000f * sp, y - -1.70800f * sp, x + 1.20000f * sp, y - -1.52000f * sp, x + 0.95600f * sp, y - -1.52000f * sp);
        p.cubicTo(x + 0.68800f * sp, y - -1.52000f * sp, x + 0.52800f * sp, y - -1.73200f * sp, x + 0.52800f * sp, y - -1.98000f * sp);
        p.cubicTo(x + 0.52800f * sp, y - -2.24000f * sp, x + 0.68400f * sp, y - -2.63200f * sp, x + 1.28800f * sp, y - -2.63200f * sp);
        p.cubicTo(x + 1.55600f * sp, y - -2.63200f * sp, x + 2.07600f * sp, y - -2.51200f * sp, x + 2.07600f * sp, y - -1.83200f * sp);
        p.cubicTo(x + 2.07600f * sp, y - -1.60400f * sp, x + 2.00400f * sp, y - -1.22400f * sp, x + 1.96000f * sp, y - -0.97600f * sp);
        p.cubicTo(x + 1.95200f * sp, y - -0.92800f * sp, x + 1.95600f * sp, y - -0.93200f * sp, x + 2.01200f * sp, y - -0.90800f * sp);
        p.cubicTo(x + 2.41600f * sp, y - -0.74800f * sp, x + 2.68400f * sp, y - -0.40800f * sp, x + 2.68400f * sp, y - 0.04400f * sp);
        p.cubicTo(x + 2.68400f * sp, y - 0.55600f * sp, x + 2.30800f * sp, y - 1.00800f * sp, x + 1.72000f * sp, y - 1.00800f * sp);
        p.cubicTo(x + 1.61600f * sp, y - 1.00800f * sp, x + 1.61600f * sp, y - 1.00800f * sp, x + 1.60400f * sp, y - 1.08000f * sp);
        return p;
    }

    static void drawTrebleClef(float x, float gLineY, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createTrebleClefPath(x, gLineY, sp);
        path.drawStroke(r, g, b, a, sp * 0.16f);
    }

    static ScoreVectorPath createBassClefPath(float x, float y, float sp) {
        ScoreVectorPath p;
        p.moveTo(x + 1.00800f * sp, y - 1.04800f * sp);
        p.cubicTo(x + 0.31200f * sp, y - 1.04800f * sp, x + 0.00000f * sp, y - 0.54000f * sp, x + 0.00000f * sp, y - 0.15600f * sp);
        p.cubicTo(x + 0.00000f * sp, y - -0.16400f * sp, x + 0.16800f * sp, y - -0.44000f * sp, x + 0.49200f * sp, y - -0.44000f * sp);
        p.cubicTo(x + 0.74400f * sp, y - -0.44000f * sp, x + 0.91600f * sp, y - -0.26400f * sp, x + 0.91600f * sp, y - -0.01600f * sp);
        p.cubicTo(x + 0.91600f * sp, y - 0.24000f * sp, x + 0.72800f * sp, y - 0.40000f * sp, x + 0.53200f * sp, y - 0.40000f * sp);
        p.cubicTo(x + 0.42400f * sp, y - 0.40000f * sp, x + 0.38400f * sp, y - 0.37200f * sp, x + 0.33200f * sp, y - 0.37200f * sp);
        p.cubicTo(x + 0.28000f * sp, y - 0.37200f * sp, x + 0.26800f * sp, y - 0.40400f * sp, x + 0.26800f * sp, y - 0.44400f * sp);
        p.cubicTo(x + 0.26800f * sp, y - 0.60400f * sp, x + 0.50800f * sp, y - 0.89600f * sp, x + 0.91600f * sp, y - 0.89600f * sp);
        p.cubicTo(x + 1.34000f * sp, y - 0.89600f * sp, x + 1.52400f * sp, y - 0.48000f * sp, x + 1.52400f * sp, y - -0.14800f * sp);
        p.cubicTo(x + 1.52400f * sp, y - -0.56000f * sp, x + 1.43600f * sp, y - -1.04000f * sp, x + 1.18800f * sp, y - -1.42400f * sp);
        p.cubicTo(x + 0.94800f * sp, y - -1.79600f * sp, x + 0.53600f * sp, y - -2.13600f * sp, x + 0.04000f * sp, y - -2.42000f * sp);
        p.cubicTo(x + 0.00400f * sp, y - -2.44000f * sp, x + -0.02000f * sp, y - -2.46000f * sp, x + -0.02000f * sp, y - -2.49200f * sp);
        p.cubicTo(x + -0.02000f * sp, y - -2.51600f * sp, x + -0.00400f * sp, y - -2.54000f * sp, x + 0.03200f * sp, y - -2.54000f * sp);
        p.cubicTo(x + 0.05200f * sp, y - -2.54000f * sp, x + 0.07600f * sp, y - -2.53200f * sp, x + 0.10000f * sp, y - -2.52000f * sp);
        p.cubicTo(x + 0.63200f * sp, y - -2.26000f * sp, x + 1.14400f * sp, y - -1.95600f * sp, x + 1.56800f * sp, y - -1.50000f * sp);
        p.cubicTo(x + 1.91600f * sp, y - -1.12400f * sp, x + 2.12400f * sp, y - -0.63600f * sp, x + 2.12400f * sp, y - -0.11200f * sp);
        p.cubicTo(x + 2.12400f * sp, y - 0.58400f * sp, x + 1.70000f * sp, y - 1.04800f * sp, x + 1.00800f * sp, y - 1.04800f * sp);
        p.close();

        // Two dots
        p.moveTo(x + 2.51600f * sp, y - 0.72000f * sp);
        p.cubicTo(x + 2.39200f * sp, y - 0.72000f * sp, x + 2.29600f * sp, y - 0.62400f * sp, x + 2.29600f * sp, y - 0.50000f * sp);
        p.cubicTo(x + 2.29600f * sp, y - 0.37600f * sp, x + 2.39200f * sp, y - 0.28000f * sp, x + 2.51600f * sp, y - 0.28000f * sp);
        p.cubicTo(x + 2.64000f * sp, y - 0.28000f * sp, x + 2.73600f * sp, y - 0.37600f * sp, x + 2.73600f * sp, y - 0.50000f * sp);
        p.cubicTo(x + 2.73600f * sp, y - 0.62400f * sp, x + 2.64000f * sp, y - 0.72000f * sp, x + 2.51600f * sp, y - 0.72000f * sp);
        p.close();

        p.moveTo(x + 2.52000f * sp, y - -0.28400f * sp);
        p.cubicTo(x + 2.39600f * sp, y - -0.28400f * sp, x + 2.30400f * sp, y - -0.37600f * sp, x + 2.30400f * sp, y - -0.50000f * sp);
        p.cubicTo(x + 2.30400f * sp, y - -0.62400f * sp, x + 2.39600f * sp, y - -0.71600f * sp, x + 2.52000f * sp, y - -0.71600f * sp);
        p.cubicTo(x + 2.64400f * sp, y - -0.71600f * sp, x + 2.73600f * sp, y - -0.62400f * sp, x + 2.73600f * sp, y - -0.50000f * sp);
        p.cubicTo(x + 2.73600f * sp, y - -0.37600f * sp, x + 2.64400f * sp, y - -0.28400f * sp, x + 2.52000f * sp, y - -0.28400f * sp);
        p.close();
        return p;
    }

    static void drawBassClef(float x, float fLineY, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createBassClefPath(x, fLineY, sp);
        path.drawStroke(r, g, b, a, sp * 0.15f);

        // Fill the two dots for clean traditional SMuFL F-clef appearance
        BatchRenderer2D* renderer = s_batchRenderer;
        if (renderer) {
            renderer->drawCircle(x + 2.52f * sp, fLineY - 0.50f * sp, sp * 0.22f, r, g, b, a);
            renderer->drawCircle(x + 2.52f * sp, fLineY + 0.50f * sp, sp * 0.22f, r, g, b, a);
        }
    }

    static ScoreVectorPath createGrandStaffBracePath(float x, float topY, float bottomY, float sp) {
        float totalH = bottomY - topY;
        float yScale = totalH / 1000.0f;
        float xScale = (sp * 1.35f) / 71.0f;
        ScoreVectorPath p;
        p.moveTo(x + 0.0f * xScale, bottomY - 500.0f * yScale);
        p.cubicTo(x + 24.0f * xScale, bottomY - 463.0f * yScale, x + 37.0f * xScale, bottomY - 435.0f * yScale, x + 37.0f * xScale, bottomY - 397.0f * yScale);
        p.cubicTo(x + 37.0f * xScale, bottomY - 330.0f * yScale, x + 2.0f * xScale, bottomY - 251.0f * yScale, x + 2.0f * xScale, bottomY - 175.0f * yScale);
        p.cubicTo(x + 2.0f * xScale, bottomY - 121.0f * yScale, x + 20.0f * xScale, bottomY - 48.0f * yScale, x + 61.0f * xScale, bottomY - 1.0f * yScale);
        p.cubicTo(x + 64.0f * xScale, bottomY - -3.0f * yScale, x + 71.0f * xScale, bottomY - 1.0f * yScale, x + 69.0f * xScale, bottomY - 6.0f * yScale);
        p.cubicTo(x + 45.0f * xScale, bottomY - 45.0f * yScale, x + 37.0f * xScale, bottomY - 91.0f * yScale, x + 37.0f * xScale, bottomY - 131.0f * yScale);
        p.cubicTo(x + 37.0f * xScale, bottomY - 212.0f * yScale, x + 69.0f * xScale, bottomY - 285.0f * yScale, x + 69.0f * xScale, bottomY - 354.0f * yScale);
        p.cubicTo(x + 69.0f * xScale, bottomY - 404.0f * yScale, x + 56.0f * xScale, bottomY - 452.0f * yScale, x + 16.0f * xScale, bottomY - 500.0f * yScale);
        p.cubicTo(x + 55.0f * xScale, bottomY - 547.0f * yScale, x + 69.0f * xScale, bottomY - 595.0f * yScale, x + 69.0f * xScale, bottomY - 645.0f * yScale);
        p.cubicTo(x + 69.0f * xScale, bottomY - 714.0f * yScale, x + 37.0f * xScale, bottomY - 788.0f * yScale, x + 37.0f * xScale, bottomY - 868.0f * yScale);
        p.cubicTo(x + 37.0f * xScale, bottomY - 909.0f * yScale, x + 44.0f * xScale, bottomY - 954.0f * yScale, x + 68.0f * xScale, bottomY - 993.0f * yScale);
        p.cubicTo(x + 71.0f * xScale, bottomY - 999.0f * yScale, x + 63.0f * xScale, bottomY - 1003.0f * yScale, x + 60.0f * xScale, bottomY - 999.0f * yScale);
        p.cubicTo(x + 19.0f * xScale, bottomY - 951.0f * yScale, x + 2.0f * xScale, bottomY - 879.0f * yScale, x + 2.0f * xScale, bottomY - 824.0f * yScale);
        p.cubicTo(x + 2.0f * xScale, bottomY - 748.0f * yScale, x + 37.0f * xScale, bottomY - 669.0f * yScale, x + 37.0f * xScale, bottomY - 602.0f * yScale);
        p.cubicTo(x + 37.0f * xScale, bottomY - 564.0f * yScale, x + 24.0f * xScale, bottomY - 537.0f * yScale, x + 0.0f * xScale, bottomY - 500.0f * yScale);
        p.close();
        return p;
    }

    static void drawGrandStaffBracket(float x, float topY, float bottomY, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createGrandStaffBracePath(x, topY, bottomY, sp);
        path.drawStroke(r, g, b, a, sp * 0.14f);

        BatchRenderer2D* renderer = s_batchRenderer;
        if (renderer) {
            renderer->drawLine(x + sp * 1.35f, topY, x + sp * 1.35f, bottomY, r, g, b, a, sp * 0.16f);
        }
    }

    static void drawBrace(float x, float topY, float totalH, float sp, float r, float g, float b, float a = 1.0f) {
        drawGrandStaffBracket(x, topY, topY + totalH, sp, r, g, b, a);
    }

    // ---------------------------------------------------------------------------
    // Accidentals (Exact SMuFL Bravura Béziers)
    // ---------------------------------------------------------------------------

    static ScoreVectorPath createSharpPath(float x, float y, float sp) {
        ScoreVectorPath p;
        p.moveTo(x + 0.94800f * sp, y - 0.47200f * sp);
        p.cubicTo(x + 0.97600f * sp, y - 0.48400f * sp, x + 0.99600f * sp, y - 0.51600f * sp, x + 0.99600f * sp, y - 0.54000f * sp);
        p.lineTo(x + 0.99600f * sp, y - 0.82400f * sp);
        p.cubicTo(x + 0.99600f * sp, y - 0.84400f * sp, x + 0.98400f * sp, y - 0.85600f * sp, x + 0.96800f * sp, y - 0.85600f * sp);
        p.cubicTo(x + 0.96000f * sp, y - 0.85600f * sp, x + 0.95600f * sp, y - 0.85600f * sp, x + 0.94800f * sp, y - 0.85200f * sp);
        p.cubicTo(x + 0.94800f * sp, y - 0.85200f * sp, x + 0.86800f * sp, y - 0.82000f * sp, x + 0.84800f * sp, y - 0.81600f * sp);
        p.cubicTo(x + 0.82000f * sp, y - 0.81600f * sp, x + 0.79200f * sp, y - 0.83600f * sp, x + 0.79200f * sp, y - 0.86800f * sp);
        p.lineTo(x + 0.79200f * sp, y - 1.35600f * sp);
        p.cubicTo(x + 0.79200f * sp, y - 1.38000f * sp, x + 0.76800f * sp, y - 1.40000f * sp, x + 0.73600f * sp, y - 1.40000f * sp);
        p.cubicTo(x + 0.69600f * sp, y - 1.40000f * sp, x + 0.67200f * sp, y - 1.38000f * sp, x + 0.67200f * sp, y - 1.35600f * sp);
        p.lineTo(x + 0.67200f * sp, y - 0.83600f * sp);
        p.cubicTo(x + 0.66800f * sp, y - 0.79600f * sp, x + 0.65600f * sp, y - 0.74400f * sp, x + 0.62000f * sp, y - 0.72000f * sp);
        p.cubicTo(x + 0.57200f * sp, y - 0.69200f * sp, x + 0.43600f * sp, y - 0.63600f * sp, x + 0.36800f * sp, y - 0.62000f * sp);
        p.cubicTo(x + 0.33200f * sp, y - 0.62000f * sp, x + 0.32000f * sp, y - 0.66800f * sp, x + 0.32000f * sp, y - 0.70000f * sp);
        p.lineTo(x + 0.32000f * sp, y - 1.18000f * sp);
        p.cubicTo(x + 0.32000f * sp, y - 1.20400f * sp, x + 0.29200f * sp, y - 1.22400f * sp, x + 0.26400f * sp, y - 1.22400f * sp);
        p.cubicTo(x + 0.22400f * sp, y - 1.22400f * sp, x + 0.20000f * sp, y - 1.20400f * sp, x + 0.20000f * sp, y - 1.18000f * sp);
        p.lineTo(x + 0.20000f * sp, y - 0.64000f * sp);
        p.cubicTo(x + 0.20000f * sp, y - 0.58400f * sp, x + 0.17600f * sp, y - 0.54400f * sp, x + 0.15200f * sp, y - 0.53200f * sp);
        p.cubicTo(x + 0.12800f * sp, y - 0.52000f * sp, x + 0.04800f * sp, y - 0.48800f * sp, x + 0.04800f * sp, y - 0.48800f * sp);
        p.cubicTo(x + 0.02000f * sp, y - 0.48000f * sp, x + 0.00000f * sp, y - 0.44800f * sp, x + 0.00000f * sp, y - 0.42400f * sp);
        p.lineTo(x + 0.00000f * sp, y - 0.14000f * sp);
        p.cubicTo(x + 0.00000f * sp, y - 0.11600f * sp, x + 0.01200f * sp, y - 0.10400f * sp, x + 0.03200f * sp, y - 0.10400f * sp);
        p.cubicTo(x + 0.03600f * sp, y - 0.10400f * sp, x + 0.04400f * sp, y - 0.10800f * sp, x + 0.04800f * sp, y - 0.10800f * sp);
        p.cubicTo(x + 0.04800f * sp, y - 0.10800f * sp, x + 0.10800f * sp, y - 0.13200f * sp, x + 0.13600f * sp, y - 0.14800f * sp);
        p.cubicTo(x + 0.14000f * sp, y - 0.14800f * sp, x + 0.14400f * sp, y - 0.15200f * sp, x + 0.14800f * sp, y - 0.15200f * sp);
        p.cubicTo(x + 0.17600f * sp, y - 0.15200f * sp, x + 0.20000f * sp, y - 0.11200f * sp, x + 0.20000f * sp, y - 0.08000f * sp);
        p.lineTo(x + 0.20000f * sp, y - -0.31600f * sp);
        p.cubicTo(x + 0.20000f * sp, y - -0.36000f * sp, x + 0.18000f * sp, y - -0.39600f * sp, x + 0.15600f * sp, y - -0.40800f * sp);
        p.cubicTo(x + 0.13200f * sp, y - -0.41600f * sp, x + 0.04800f * sp, y - -0.45200f * sp, x + 0.04800f * sp, y - -0.45200f * sp);
        p.cubicTo(x + 0.02000f * sp, y - -0.46000f * sp, x + 0.00000f * sp, y - -0.49200f * sp, x + 0.00000f * sp, y - -0.51600f * sp);
        p.lineTo(x + 0.00000f * sp, y - -0.80000f * sp);
        p.cubicTo(x + 0.00000f * sp, y - -0.82400f * sp, x + 0.01200f * sp, y - -0.83600f * sp, x + 0.03200f * sp, y - -0.83600f * sp);
        p.cubicTo(x + 0.03600f * sp, y - -0.83600f * sp, x + 0.04400f * sp, y - -0.83200f * sp, x + 0.04800f * sp, y - -0.83200f * sp);
        p.cubicTo(x + 0.04800f * sp, y - -0.83200f * sp, x + 0.10400f * sp, y - -0.80800f * sp, x + 0.14000f * sp, y - -0.79600f * sp);
        p.cubicTo(x + 0.14400f * sp, y - -0.79200f * sp, x + 0.14800f * sp, y - -0.79200f * sp, x + 0.15200f * sp, y - -0.79200f * sp);
        p.cubicTo(x + 0.18000f * sp, y - -0.79200f * sp, x + 0.20000f * sp, y - -0.83600f * sp, x + 0.20000f * sp, y - -0.85600f * sp);
        p.lineTo(x + 0.20000f * sp, y - -1.34800f * sp);
        p.cubicTo(x + 0.20000f * sp, y - -1.37200f * sp, x + 0.22400f * sp, y - -1.39200f * sp, x + 0.25200f * sp, y - -1.39200f * sp);
        p.cubicTo(x + 0.29200f * sp, y - -1.39200f * sp, x + 0.32000f * sp, y - -1.37200f * sp, x + 0.32000f * sp, y - -1.34800f * sp);
        p.lineTo(x + 0.32000f * sp, y - -0.79200f * sp);
        p.cubicTo(x + 0.32000f * sp, y - -0.74000f * sp, x + 0.34000f * sp, y - -0.71200f * sp, x + 0.36000f * sp, y - -0.70400f * sp);
        p.lineTo(x + 0.60400f * sp, y - -0.60400f * sp);
        p.cubicTo(x + 0.60800f * sp, y - -0.60400f * sp, x + 0.61600f * sp, y - -0.60000f * sp, x + 0.62000f * sp, y - -0.60000f * sp);
        p.cubicTo(x + 0.65200f * sp, y - -0.60000f * sp, x + 0.67200f * sp, y - -0.64800f * sp, x + 0.67200f * sp, y - -0.67200f * sp);
        p.lineTo(x + 0.67200f * sp, y - -1.17200f * sp);
        p.cubicTo(x + 0.67200f * sp, y - -1.19600f * sp, x + 0.69600f * sp, y - -1.21600f * sp, x + 0.72400f * sp, y - -1.21600f * sp);
        p.cubicTo(x + 0.76800f * sp, y - -1.21600f * sp, x + 0.79200f * sp, y - -1.19600f * sp, x + 0.79200f * sp, y - -1.17200f * sp);
        p.lineTo(x + 0.79200f * sp, y - -0.60400f * sp);
        p.cubicTo(x + 0.79200f * sp, y - -0.57200f * sp, x + 0.80800f * sp, y - -0.52400f * sp, x + 0.83600f * sp, y - -0.51200f * sp);
        p.cubicTo(x + 0.86400f * sp, y - -0.50000f * sp, x + 0.94800f * sp, y - -0.46800f * sp, x + 0.94800f * sp, y - -0.46800f * sp);
        p.cubicTo(x + 0.97600f * sp, y - -0.45600f * sp, x + 0.99600f * sp, y - -0.42400f * sp, x + 0.99600f * sp, y - -0.40000f * sp);
        p.lineTo(x + 0.99600f * sp, y - -0.11600f * sp);
        p.cubicTo(x + 0.99600f * sp, y - -0.09600f * sp, x + 0.98400f * sp, y - -0.08400f * sp, x + 0.96800f * sp, y - -0.08400f * sp);
        p.cubicTo(x + 0.96000f * sp, y - -0.08400f * sp, x + 0.95600f * sp, y - -0.08400f * sp, x + 0.94800f * sp, y - -0.08800f * sp);
        p.lineTo(x + 0.84400f * sp, y - -0.12800f * sp);
        p.cubicTo(x + 0.82000f * sp, y - -0.12800f * sp, x + 0.79200f * sp, y - -0.10400f * sp, x + 0.79200f * sp, y - -0.05600f * sp);
        p.lineTo(x + 0.79200f * sp, y - 0.31600f * sp);
        p.cubicTo(x + 0.79200f * sp, y - 0.34400f * sp, x + 0.81200f * sp, y - 0.42000f * sp, x + 0.84400f * sp, y - 0.43200f * sp);
        p.close();
        return p;
    }

    static void drawSharp(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createSharpPath(cx - 0.50f * sp, cy, sp);
        path.drawFill(r, g, b, a);
    }

    static ScoreVectorPath createFlatPath(float x, float y, float sp) {
        ScoreVectorPath p;
        p.moveTo(x + 0.04800f * sp, y - -0.68000f * sp);
        p.cubicTo(x + 0.06000f * sp, y - -0.69600f * sp, x + 0.07200f * sp, y - -0.70000f * sp, x + 0.08400f * sp, y - -0.70000f * sp);
        p.cubicTo(x + 0.09600f * sp, y - -0.70000f * sp, x + 0.10800f * sp, y - -0.69200f * sp, x + 0.10800f * sp, y - -0.69200f * sp);
        p.cubicTo(x + 0.22800f * sp, y - -0.62400f * sp, x + 0.32400f * sp, y - -0.51600f * sp, x + 0.42400f * sp, y - -0.44800f * sp);
        p.cubicTo(x + 0.78000f * sp, y - -0.20000f * sp, x + 0.90400f * sp, y - 0.04400f * sp, x + 0.90400f * sp, y - 0.22800f * sp);
        p.cubicTo(x + 0.90400f * sp, y - 0.45600f * sp, x + 0.72800f * sp, y - 0.60000f * sp, x + 0.54400f * sp, y - 0.61200f * sp);
        p.cubicTo(x + 0.51600f * sp, y - 0.61200f * sp, x + 0.48800f * sp, y - 0.60800f * sp, x + 0.46000f * sp, y - 0.60000f * sp);
        p.cubicTo(x + 0.41600f * sp, y - 0.58800f * sp, x + 0.36800f * sp, y - 0.57200f * sp, x + 0.32400f * sp, y - 0.54400f * sp);
        p.cubicTo(x + 0.30000f * sp, y - 0.52400f * sp, x + 0.25600f * sp, y - 0.48800f * sp, x + 0.23600f * sp, y - 0.48800f * sp);
        p.cubicTo(x + 0.22800f * sp, y - 0.48800f * sp, x + 0.22400f * sp, y - 0.48800f * sp, x + 0.21600f * sp, y - 0.49200f * sp);
        p.cubicTo(x + 0.18800f * sp, y - 0.50400f * sp, x + 0.17200f * sp, y - 0.53200f * sp, x + 0.17200f * sp, y - 0.56000f * sp);
        p.cubicTo(x + 0.17600f * sp, y - 0.64800f * sp, x + 0.20000f * sp, y - 1.60800f * sp, x + 0.20000f * sp, y - 1.68800f * sp);
        p.cubicTo(x + 0.20000f * sp, y - 1.73200f * sp, x + 0.16400f * sp, y - 1.75600f * sp, x + 0.12400f * sp, y - 1.75600f * sp);
        p.cubicTo(x + 0.06800f * sp, y - 1.75600f * sp, x + 0.00400f * sp, y - 1.71600f * sp, x + 0.00000f * sp, y - 1.64400f * sp);
        p.cubicTo(x + 0.00000f * sp, y - 1.64400f * sp, x + 0.01600f * sp, y - -0.64000f * sp, x + 0.04800f * sp, y - -0.68000f * sp);
        p.close();
        return p;
    }

    static void drawFlat(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createFlatPath(cx - 0.45f * sp, cy, sp);
        path.drawFill(r, g, b, a);
    }

    static ScoreVectorPath createNaturalPath(float x, float y, float sp) {
        ScoreVectorPath p;
        p.moveTo(x + 0.56400f * sp, y - 0.72400f * sp);
        p.cubicTo(x + 0.55600f * sp, y - 0.72400f * sp, x + 0.55200f * sp, y - 0.72000f * sp, x + 0.54800f * sp, y - 0.72000f * sp);
        p.cubicTo(x + 0.54800f * sp, y - 0.72000f * sp, x + 0.29200f * sp, y - 0.62800f * sp, x + 0.18800f * sp, y - 0.62800f * sp);
        p.cubicTo(x + 0.16400f * sp, y - 0.62800f * sp, x + 0.14800f * sp, y - 0.63200f * sp, x + 0.14800f * sp, y - 0.64800f * sp);
        p.lineTo(x + 0.14800f * sp, y - 1.31600f * sp);
        p.cubicTo(x + 0.14800f * sp, y - 1.34400f * sp, x + 0.12400f * sp, y - 1.36400f * sp, x + 0.10000f * sp, y - 1.36400f * sp);
        p.lineTo(x + 0.04800f * sp, y - 1.36400f * sp);
        p.cubicTo(x + 0.02000f * sp, y - 1.34400f * sp, x + 0.00000f * sp, y - 1.31600f * sp, x + 0.00000f * sp, y - 1.31600f * sp);
        p.lineTo(x + 0.00000f * sp, y - -0.74400f * sp);
        p.cubicTo(x + 0.00000f * sp, y - -0.76800f * sp, x + 0.01200f * sp, y - -0.78000f * sp, x + 0.03200f * sp, y - -0.78000f * sp);
        p.cubicTo(x + 0.03600f * sp, y - -0.78000f * sp, x + 0.04400f * sp, y - -0.77600f * sp, x + 0.04800f * sp, y - -0.77600f * sp);
        p.cubicTo(x + 0.04800f * sp, y - -0.77600f * sp, x + 0.05600f * sp, y - -0.77600f * sp, x + 0.06000f * sp, y - -0.77200f * sp);
        p.cubicTo(x + 0.11600f * sp, y - -0.74800f * sp, x + 0.34000f * sp, y - -0.65200f * sp, x + 0.45600f * sp, y - -0.65200f * sp);
        p.cubicTo(x + 0.49600f * sp, y - -0.65200f * sp, x + 0.52400f * sp, y - -0.66400f * sp, x + 0.52400f * sp, y - -0.69600f * sp);
        p.lineTo(x + 0.52400f * sp, y - -1.29200f * sp);
        p.cubicTo(x + 0.52400f * sp, y - -1.32000f * sp, x + 0.54400f * sp, y - -1.34000f * sp, x + 0.57200f * sp, y - -1.34000f * sp);
        p.lineTo(x + 0.62400f * sp, y - -1.34000f * sp);
        p.cubicTo(x + 0.64800f * sp, y - -1.34000f * sp, x + 0.67200f * sp, y - -1.32000f * sp, x + 0.67200f * sp, y - -1.29200f * sp);
        p.lineTo(x + 0.67200f * sp, y - 0.71600f * sp);
        p.cubicTo(x + 0.67200f * sp, y - 0.73600f * sp, x + 0.65600f * sp, y - 0.74800f * sp, x + 0.64000f * sp, y - 0.74800f * sp);
        p.cubicTo(x + 0.63600f * sp, y - 0.74800f * sp, x + 0.62800f * sp, y - 0.74800f * sp, x + 0.62400f * sp, y - 0.74400f * sp);
        p.close();
        return p;
    }

    static void drawNatural(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createNaturalPath(cx - 0.34f * sp, cy, sp);
        path.drawFill(r, g, b, a);
    }

    static ScoreVectorPath createQuarterRestPath(float x, float y, float sp) {
        ScoreVectorPath p;
        p.moveTo(x + 0.31200f * sp, y - -0.15200f * sp);
        p.cubicTo(x + 0.37600f * sp, y - -0.23200f * sp, x + 0.43200f * sp, y - -0.30800f * sp, x + 0.48400f * sp, y - -0.39200f * sp);
        p.cubicTo(x + 0.49200f * sp, y - -0.40800f * sp, x + 0.50800f * sp, y - -0.44000f * sp, x + 0.50800f * sp, y - -0.44800f * sp);
        p.cubicTo(x + 0.50800f * sp, y - -0.45200f * sp, x + 0.50800f * sp, y - -0.46000f * sp, x + 0.50400f * sp, y - -0.46400f * sp);
        p.cubicTo(x + 0.49600f * sp, y - -0.48000f * sp, x + 0.48000f * sp, y - -0.48400f * sp, x + 0.46000f * sp, y - -0.48400f * sp);
        p.cubicTo(x + 0.44400f * sp, y - -0.48400f * sp, x + 0.41200f * sp, y - -0.47600f * sp, x + 0.39600f * sp, y - -0.47200f * sp);
        p.cubicTo(x + 0.37600f * sp, y - -0.47200f * sp, x + 0.35200f * sp, y - -0.46000f * sp, x + 0.33200f * sp, y - -0.46000f * sp);
        p.cubicTo(x + 0.16000f * sp, y - -0.46000f * sp, x + 0.00400f * sp, y - -0.63200f * sp, x + 0.00400f * sp, y - -0.84400f * sp);
        p.cubicTo(x + 0.00400f * sp, y - -1.04400f * sp, x + 0.17600f * sp, y - -1.24000f * sp, x + 0.46800f * sp, y - -1.46400f * sp);
        p.cubicTo(x + 0.50000f * sp, y - -1.48800f * sp, x + 0.54000f * sp, y - -1.50000f * sp, x + 0.57200f * sp, y - -1.50000f * sp);
        p.cubicTo(x + 0.60000f * sp, y - -1.50000f * sp, x + 0.62800f * sp, y - -1.49200f * sp, x + 0.63200f * sp, y - -1.47600f * sp);
        p.cubicTo(x + 0.63600f * sp, y - -1.46400f * sp, x + 0.64000f * sp, y - -1.45600f * sp, x + 0.64000f * sp, y - -1.44800f * sp);
        p.cubicTo(x + 0.64000f * sp, y - -1.41200f * sp, x + 0.60800f * sp, y - -1.38000f * sp, x + 0.57600f * sp, y - -1.35200f * sp);
        p.cubicTo(x + 0.52400f * sp, y - -1.35200f * sp, x + 0.48000f * sp, y - -1.24400f * sp, x + 0.47200f * sp, y - -1.20800f * sp);
        p.cubicTo(x + 0.46000f * sp, y - -1.17600f * sp, x + 0.45600f * sp, y - -1.14000f * sp, x + 0.45600f * sp, y - -1.10400f * sp);
        p.cubicTo(x + 0.45600f * sp, y - -0.98000f * sp, x + 0.51600f * sp, y - -0.84000f * sp, x + 0.64400f * sp, y - -0.81600f * sp);
        p.cubicTo(x + 0.66400f * sp, y - -0.81200f * sp, x + 0.68400f * sp, y - -0.81200f * sp, x + 0.70800f * sp, y - -0.81200f * sp);
        p.cubicTo(x + 0.82400f * sp, y - -0.81200f * sp, x + 0.95600f * sp, y - -0.85600f * sp, x + 1.02000f * sp, y - -0.88000f * sp);
        p.close();
        return p;
    }

    static void drawQuarterRest(float cx, float cy, float sp, float r, float g, float b, float a = 1.0f) {
        auto path = createQuarterRestPath(cx - 0.54f * sp, cy, sp);
        path.drawFill(r, g, b, a);
    }
};

inline bool isPointInScoreTriangle(ScoreVectorPath::Point p, ScoreVectorPath::Point a, ScoreVectorPath::Point b, ScoreVectorPath::Point c) {
    auto sign = [](ScoreVectorPath::Point p1, ScoreVectorPath::Point p2, ScoreVectorPath::Point p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    float d1 = sign(p, a, b);
    float d2 = sign(p, b, c);
    float d3 = sign(p, c, a);
    bool has_neg = (d1 < -1e-5f) || (d2 < -1e-5f) || (d3 < -1e-5f);
    bool has_pos = (d1 > 1e-5f) || (d2 > 1e-5f) || (d3 > 1e-5f);
    return !(has_neg && has_pos);
}

inline void triangulateScorePolygon(const std::vector<ScoreVectorPath::Point>& pts, std::vector<uint32_t>& indices) {
    size_t n = pts.size();
    if (n < 3) return;

    float area = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        area += pts[i].x * pts[j].y - pts[j].x * pts[i].y;
    }
    bool ccw = (area > 0.0f);

    std::vector<size_t> V(n);
    for (size_t i = 0; i < n; ++i) V[i] = i;

    int count = 2 * static_cast<int>(n);
    for (size_t v = n - 1; n > 2;) {
        if (--count <= 0) {
            for (size_t i = 1; i + 1 < n; ++i) {
                indices.push_back(static_cast<uint32_t>(V[0]));
                indices.push_back(static_cast<uint32_t>(V[i]));
                indices.push_back(static_cast<uint32_t>(V[i + 1]));
            }
            break;
        }

        size_t u = v;
        if (n <= u) u = 0;
        v = u + 1;
        if (n <= v) v = 0;
        size_t w = v + 1;
        if (n <= w) w = 0;

        ScoreVectorPath::Point A = pts[V[u]];
        ScoreVectorPath::Point B = pts[V[v]];
        ScoreVectorPath::Point C = pts[V[w]];

        float cross = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        bool isConvex = ccw ? (cross > 1e-6f) : (cross < -1e-6f);

        if (isConvex) {
            bool hasPointInside = false;
            for (size_t p = 0; p < n; ++p) {
                if (p == u || p == v || p == w) continue;
                if (isPointInScoreTriangle(pts[V[p]], A, B, C)) {
                    hasPointInside = true;
                    break;
                }
            }
            if (!hasPointInside) {
                indices.push_back(static_cast<uint32_t>(V[u]));
                indices.push_back(static_cast<uint32_t>(V[v]));
                indices.push_back(static_cast<uint32_t>(V[w]));
                V.erase(V.begin() + v);
                --n;
                count = 2 * static_cast<int>(n);
                continue;
            }
        }
    }
}

inline void ScoreVectorPath::drawFill(float r, float g, float b, float a) const {
    BatchRenderer2D* renderer = ScoreVectorGlyphs::getBatchRenderer();
    if (!renderer) return;

    for (const auto& c : contours) {
        if (c.size() < 3) continue;
        std::vector<uint32_t> indices;
        triangulateScorePolygon(c, indices);
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const auto& p0 = c[indices[i]];
            const auto& p1 = c[indices[i + 1]];
            const auto& p2 = c[indices[i + 2]];
            renderer->drawTriangle(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, r, g, b, a);
        }
    }
}

inline void ScoreVectorPath::drawStroke(float r, float g, float b, float a, float lineWidth) const {
    BatchRenderer2D* renderer = ScoreVectorGlyphs::getBatchRenderer();
    if (!renderer) return;

    const float capRadius = lineWidth * 0.5f;
    for (const auto& c : contours) {
        if (c.size() < 2) continue;
        for (size_t i = 0; i + 1 < c.size(); ++i) {
            renderer->drawLine(c[i].x, c[i].y, c[i + 1].x, c[i + 1].y, r, g, b, a, lineWidth);
            renderer->drawCircle(c[i].x, c[i].y, capRadius, r, g, b, a, 10);
        }
        renderer->drawCircle(c.back().x, c.back().y, capRadius, r, g, b, a, 10);
    }
}

} // namespace eatsbits::ui

#endif // EATS_SCORE_GLYPHS_HPP
