#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"
#include "eatsbits/ui/glyph/font_engine.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace solengine::clockwork_ui {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
} // anonymous namespace

const ClockworkPalette& palette() noexcept {
    static ClockworkPalette pal;
    return pal;
}

void drawGear(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy,
    float outerRadius, float rootRadius, float innerRadius,
    int teeth, float angleRad,
    float bodyR, float bodyG, float bodyB, float bodyA,
    float rimR, float rimG, float rimB, float rimA,
    int spokeCount, float hubRadius,
    bool drawShadow
) {
    if (teeth < 3 || outerRadius <= 1.0f) return;
    teeth = std::clamp(teeth, 4, 48);

    if (rootRadius <= 0.0f) rootRadius = outerRadius * 0.78f;
    if (innerRadius <= 0.0f) innerRadius = rootRadius * 0.65f;
    if (hubRadius <= 0.0f) hubRadius = innerRadius * 0.45f;

    const float toothAngle = kTwoPi / static_cast<float>(teeth);

    // 1. Optional subtle drop shadow behind gear teeth (only when requested, using physically grounded light)
    if (drawShadow) {
        r.drawCircleDropShadow(cx, cy, outerRadius, 2.5f,
                               eatsbits::ui::LightSource2D{cx - 100.0f, cy - 200.0f, 400.0f, 0.85f}, 0.35f * bodyA);
    }

    // 2. Annular ring between root radius and inner radius (gear body plate)
    const int ringSegments = std::clamp(teeth * 3, 18, 64);
    for (int i = 0; i < ringSegments; ++i) {
        float a0 = static_cast<float>(i) * (kTwoPi / static_cast<float>(ringSegments));
        float a1 = static_cast<float>(i + 1) * (kTwoPi / static_cast<float>(ringSegments));
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);

        // Quad between innerRadius and rootRadius
        r.drawTriangle(
            cx + innerRadius * c0, cy + innerRadius * s0,
            cx + rootRadius * c0,  cy + rootRadius * s0,
            cx + rootRadius * c1,  cy + rootRadius * s1,
            bodyR, bodyG, bodyB, bodyA
        );
        r.drawTriangle(
            cx + innerRadius * c0, cy + innerRadius * s0,
            cx + rootRadius * c1,  cy + rootRadius * s1,
            cx + innerRadius * c1, cy + innerRadius * s1,
            bodyR, bodyG, bodyB, bodyA
        );
    }

    // 3. Trapezoidal Gear Teeth around perimeter
    for (int t = 0; t < teeth; ++t) {
        float baseA = angleRad + static_cast<float>(t) * toothAngle;
        float a0 = baseA;
        float a1 = baseA + toothAngle * 0.22f; // Tip start
        float a2 = baseA + toothAngle * 0.52f; // Tip end
        float a3 = baseA + toothAngle * 0.74f; // Root end

        float p0x = cx + rootRadius * std::cos(a0), p0y = cy + rootRadius * std::sin(a0);
        float p1x = cx + outerRadius * std::cos(a1), p1y = cy + outerRadius * std::sin(a1);
        float p2x = cx + outerRadius * std::cos(a2), p2y = cy + outerRadius * std::sin(a2);
        float p3x = cx + rootRadius * std::cos(a3), p3y = cy + rootRadius * std::sin(a3);

        // Fill tooth trapezoid
        r.drawTriangle(p0x, p0y, p1x, p1y, p2x, p2y, bodyR * 1.05f, bodyG * 1.04f, bodyB * 1.02f, bodyA);
        r.drawTriangle(p0x, p0y, p2x, p2y, p3x, p3y, bodyR * 0.95f, bodyG * 0.94f, bodyB * 0.92f, bodyA);

        // Bevel highlight on leading edge, shadow on trailing edge
        r.drawLine(p0x, p0y, p1x, p1y, rimR, rimG, rimB, rimA * 0.90f, 1.2f);
        r.drawLine(p1x, p1y, p2x, p2y, rimR, rimG, rimB, rimA * 0.95f, 1.2f);
        r.drawLine(p2x, p2y, p3x, p3y, bodyR * 0.45f, bodyG * 0.40f, bodyB * 0.20f, rimA * 0.85f, 1.0f);
    }

    // 4. Outer rim bevel stroke
    r.drawCircleOutline(cx, cy, rootRadius, rimR * 0.85f, rimG * 0.80f, rimB * 0.60f, rimA * 0.80f, 1.2f, ringSegments);
    r.drawCircleOutline(cx, cy, innerRadius, bodyR * 0.50f, bodyG * 0.45f, bodyB * 0.25f, rimA * 0.90f, 1.2f, ringSegments);

    // 5. Radial Spokes inside wheel cutout
    if (spokeCount > 0 && spokeCount <= 12) {
        float spokeAngle = kTwoPi / static_cast<float>(spokeCount);
        float halfThickness = std::max(1.5f, (innerRadius - hubRadius) * 0.16f);

        for (int s = 0; s < spokeCount; ++s) {
            float sa = angleRad + static_cast<float>(s) * spokeAngle;
            float cosS = std::cos(sa);
            float sinS = std::sin(sa);
            float perpX = -sinS * halfThickness;
            float perpY = cosS * halfThickness;

            float xInner0 = cx + hubRadius * cosS + perpX;
            float yInner0 = cy + hubRadius * sinS + perpY;
            float xInner1 = cx + hubRadius * cosS - perpX;
            float yInner1 = cy + hubRadius * sinS - perpY;

            float xOuter0 = cx + innerRadius * cosS + perpX * 0.85f;
            float yOuter0 = cy + innerRadius * sinS + perpY * 0.85f;
            float xOuter1 = cx + innerRadius * cosS - perpX * 0.85f;
            float yOuter1 = cy + innerRadius * sinS - perpY * 0.85f;

            r.drawTriangle(xInner0, yInner0, xOuter0, yOuter0, xOuter1, yOuter1, bodyR, bodyG, bodyB, bodyA);
            r.drawTriangle(xInner0, yInner0, xOuter1, yOuter1, xInner1, yInner1, bodyR * 0.92f, bodyG * 0.90f, bodyB * 0.88f, bodyA);

            // Spoke bevel highlights
            r.drawLine(xInner0, yInner0, xOuter0, yOuter0, rimR, rimG, rimB, rimA * 0.85f, 1.0f);
            r.drawLine(xInner1, yInner1, xOuter1, yOuter1, bodyR * 0.45f, bodyG * 0.40f, bodyB * 0.20f, rimA * 0.70f, 1.0f);
        }
    }

    // 6. Central Brass Boss & Pivot Rivet
    r.drawCircleRadial3StopGradient(
        cx, cy, hubRadius,
        rimR, rimG, rimB, bodyA,
        bodyR, bodyG, bodyB, bodyA,
        bodyR * 0.45f, bodyG * 0.38f, bodyB * 0.18f, bodyA,
        -hubRadius * 0.25f, -hubRadius * 0.30f, 0.45f, 24
    );
    r.drawCircleOutline(cx, cy, hubRadius, bodyR * 0.35f, bodyG * 0.30f, bodyB * 0.15f, bodyA, 1.2f, 24);

    // Center micro-rivet / axle hole
    r.drawCircle(cx, cy, std::max(1.5f, hubRadius * 0.35f), 0.15f, 0.12f, 0.10f, bodyA, 12);
    r.drawCircleOutline(cx, cy, std::max(1.5f, hubRadius * 0.35f), rimR, rimG, rimB, bodyA * 0.85f, 0.9f, 12);
}

void drawRivet(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool isHexBolt,
    float brassR, float brassG, float brassB, float alpha
) {
    if (radius <= 0.5f || alpha <= 0.0f) return;

    // Subtle drop shadow cast toward bottom-right
    r.drawCircle(cx + 1.0f, cy + 1.2f, radius, 0.05f, 0.04f, 0.03f, alpha * 0.55f, 12);

    if (isHexBolt) {
        // Hexagonal bolt head
        constexpr int sides = 6;
        std::array<eatsbits::ui::Point2D, 6> pts;
        for (int i = 0; i < sides; ++i) {
            float ang = static_cast<float>(i) * (kTwoPi / 6.0f);
            pts[i] = eatsbits::ui::Point2D{cx + radius * std::cos(ang), cy + radius * std::sin(ang)};
        }
        r.drawConvexPolygon(pts.data(), sides, brassR * 0.88f, brassG * 0.84f, brassB * 0.75f, alpha);
        r.drawPath(pts.data(), sides, 1.0f, brassR * 1.15f, brassG * 1.12f, brassB * 1.05f, alpha, true);
        // Slotted drive line
        r.drawLine(cx - radius * 0.55f, cy, cx + radius * 0.55f, cy, 0.12f, 0.10f, 0.08f, alpha * 0.85f, 1.0f);
    } else {
        // Spherical dome rivet with directional specular highlight
        r.drawCircleRadial3StopGradient(
            cx, cy, radius,
            std::min(1.0f, brassR * 1.25f), std::min(1.0f, brassG * 1.20f), std::min(1.0f, brassB * 1.10f), alpha,
            brassR, brassG, brassB, alpha,
            brassR * 0.40f, brassG * 0.35f, brassB * 0.18f, alpha,
            -radius * 0.35f, -radius * 0.40f, 0.50f, 16
        );
        r.drawCircleOutline(cx, cy, radius, brassR * 0.35f, brassG * 0.30f, brassB * 0.15f, alpha * 0.80f, 0.8f, 16);
    }
}

void drawRivetedBorder(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    float spacing, float rivetRadius,
    bool isHexBolt
) {
    (void)radius;
    const auto& pal = palette();
    const float inset = rivetRadius + 4.0f;
    const float x0 = x + inset;
    const float y0 = y + inset;
    const float x1 = x + w - inset;
    const float y1 = y + h - inset;

    const float spanW = x1 - x0;
    const float spanH = y1 - y0;
    if (spanW <= spacing * 0.5f || spanH <= spacing * 0.5f) return;

    int stepsX = std::max(2, static_cast<int>(std::round(spanW / spacing)));
    int stepsY = std::max(2, static_cast<int>(std::round(spanH / spacing)));
    float dx = spanW / static_cast<float>(stepsX);
    float dy = spanH / static_cast<float>(stepsY);

    // Top and bottom edges
    for (int i = 0; i <= stepsX; ++i) {
        float rx = x0 + static_cast<float>(i) * dx;
        drawRivet(r, rx, y0, rivetRadius, isHexBolt, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.92f);
        drawRivet(r, rx, y1, rivetRadius, isHexBolt, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.92f);
    }

    // Left and right edges (excluding already stamped corners)
    for (int j = 1; j < stepsY; ++j) {
        float ry = y0 + static_cast<float>(j) * dy;
        drawRivet(r, x0, ry, rivetRadius, isHexBolt, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.92f);
        drawRivet(r, x1, ry, rivetRadius, isHexBolt, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.92f);
    }
}

void drawPipe(
    eatsbits::ui::BatchRenderer2D& r,
    float x0, float y0, float x1, float y1,
    float width,
    bool startFlange, bool endFlange
) {
    const auto& pal = palette();
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::hypot(dx, dy);
    if (len <= 1.0f || width <= 1.0f) return;

    float nx = -dy / len;
    float ny = dx / len;
    float halfW = width * 0.5f;

    // Drop shadow
    r.drawTriangle(
        x0 - nx * halfW + 1.5f, y0 - ny * halfW + 2.0f,
        x0 + nx * halfW + 1.5f, y0 + ny * halfW + 2.0f,
        x1 + nx * halfW + 1.5f, y1 + ny * halfW + 2.0f,
        0.05f, 0.04f, 0.03f, 0.40f
    );
    r.drawTriangle(
        x0 - nx * halfW + 1.5f, y0 - ny * halfW + 2.0f,
        x1 + nx * halfW + 1.5f, y1 + ny * halfW + 2.0f,
        x1 - nx * halfW + 1.5f, y1 - ny * halfW + 2.0f,
        0.05f, 0.04f, 0.03f, 0.40f
    );

    // Cylindrical copper body
    r.drawTriangle(
        x0 - nx * halfW, y0 - ny * halfW,
        x0 + nx * halfW, y0 + ny * halfW,
        x1 + nx * halfW, y1 + ny * halfW,
        pal.copperMidR, pal.copperMidG, pal.copperMidB, 0.98f
    );
    r.drawTriangle(
        x0 - nx * halfW, y0 - ny * halfW,
        x1 + nx * halfW, y1 + ny * halfW,
        x1 - nx * halfW, y1 - ny * halfW,
        pal.copperMidR * 0.88f, pal.copperMidG * 0.85f, pal.copperMidB * 0.82f, 0.98f
    );

    // Specular spine highlight along the top/center
    float spineOffset = halfW * 0.35f;
    r.drawLine(
        x0 - nx * spineOffset, y0 - ny * spineOffset,
        x1 - nx * spineOffset, y1 - ny * spineOffset,
        pal.copperSpineR, pal.copperSpineG, pal.copperSpineB, 0.85f, std::max(1.0f, width * 0.28f)
    );

    // Dark crease on lower boundary
    r.drawLine(
        x0 + nx * halfW, y0 + ny * halfW,
        x1 + nx * halfW, y1 + ny * halfW,
        pal.copperDarkR * 0.65f, pal.copperDarkG * 0.60f, pal.copperDarkB * 0.50f, 0.90f, 1.2f
    );

    auto drawCouplingFlange = [&](float fx, float fy) {
        float fHalfW = halfW * 1.55f;
        float fThick = std::max(3.5f, width * 0.55f);
        float tx = (dx / len) * (fThick * 0.5f);
        float ty = (dy / len) * (fThick * 0.5f);

        r.drawTriangle(
            fx - nx * fHalfW - tx, fy - ny * fHalfW - ty,
            fx + nx * fHalfW - tx, fy + ny * fHalfW - ty,
            fx + nx * fHalfW + tx, fy + ny * fHalfW + ty,
            pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 1.0f
        );
        r.drawTriangle(
            fx - nx * fHalfW - tx, fy - ny * fHalfW - ty,
            fx + nx * fHalfW + tx, fy + ny * fHalfW + ty,
            fx - nx * fHalfW + tx, fy - ny * fHalfW + ty,
            pal.brassMidR, pal.brassMidG, pal.brassMidB, 1.0f
        );

        r.drawLine(fx - nx * fHalfW - tx, fy - ny * fHalfW - ty,
                   fx + nx * fHalfW - tx, fy + ny * fHalfW - ty,
                   pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f, 1.0f);
        r.drawLine(fx - nx * fHalfW + tx, fy - ny * fHalfW + ty,
                   fx + nx * fHalfW + tx, fy + ny * fHalfW + ty,
                   pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 0.95f, 1.0f);

        // Flange hex bolt pins
        drawRivet(r, fx - nx * (fHalfW * 0.65f), fy - ny * (fHalfW * 0.65f), 1.6f, true,
                  pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);
        drawRivet(r, fx + nx * (fHalfW * 0.65f), fy + ny * (fHalfW * 0.65f), 1.6f, true,
                  pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);
    };

    if (startFlange) drawCouplingFlange(x0, y0);
    if (endFlange) drawCouplingFlange(x1, y1);
}

void drawPipeCorner(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    int cornerQuadrant,
    float width
) {
    if (radius <= 2.0f) return;
    const auto& pal = palette();

    float startA = 0.0f;
    if (cornerQuadrant == 0) startA = -kPi * 0.5f;     // Top-Right: -90 to 0 deg
    else if (cornerQuadrant == 1) startA = 0.0f;        // Bottom-Right: 0 to 90 deg
    else if (cornerQuadrant == 2) startA = kPi * 0.5f;  // Bottom-Left: 90 to 180 deg
    else if (cornerQuadrant == 3) startA = kPi;         // Top-Left: 180 to 270 deg

    constexpr int segs = 10;
    float step = (kPi * 0.5f) / static_cast<float>(segs);
    for (int i = 0; i < segs; ++i) {
        float a0 = startA + static_cast<float>(i) * step;
        float a1 = startA + static_cast<float>(i + 1) * step;
        float p0x = cx + radius * std::cos(a0);
        float p0y = cy + radius * std::sin(a0);
        float p1x = cx + radius * std::cos(a1);
        float p1y = cy + radius * std::sin(a1);
        drawPipe(r, p0x, p0y, p1x, p1y, width, false, false);
    }

    // Midpoint elbow joint collar
    float midA = startA + kPi * 0.25f;
    float midX = cx + radius * std::cos(midA);
    float midY = cy + radius * std::sin(midA);
    drawRivet(r, midX, midY, width * 0.55f, false, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 1.0f);
}

void drawManometerGauge(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    float needleAngleRad,
    float valueRatio,
    bool hasCrossMark,
    const char* label
) {
    if (radius <= 4.0f) return;
    const auto& pal = palette();

    // 1. Directional drop shadow
    r.drawCircleDropShadow(cx, cy, radius, 3.0f,
                           eatsbits::ui::LightSource2D{cx - 100.0f, cy - 200.0f, 400.0f, 0.85f}, 0.50f);

    // 2. Heavy spun brass outer bezel (concentric rings)
    r.drawCircleRadial3StopGradient(
        cx, cy, radius,
        pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f,
        pal.brassMidR, pal.brassMidG, pal.brassMidB, 1.0f,
        pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 1.0f,
        -radius * 0.35f, -radius * 0.40f, 0.45f, 32
    );
    r.drawCircleOutline(cx, cy, radius, pal.brassDarkR * 0.70f, pal.brassDarkG * 0.65f, pal.brassDarkB * 0.45f, 0.95f, 1.4f, 32);

    // 4 cardinal bezel micro-screws
    for (int b = 0; b < 4; ++b) {
        float ang = static_cast<float>(b) * (kTwoPi / 4.0f) + kPi * 0.25f;
        float bx = cx + (radius - 3.2f) * std::cos(ang);
        float by = cy + (radius - 3.2f) * std::sin(ang);
        drawRivet(r, bx, by, 1.6f, true, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);
    }

    // 3. Recessed dial face
    const float dialRadius = radius - 6.0f;
    r.drawCircle(cx, cy, dialRadius, 0.10f, 0.08f, 0.07f, 0.95f, 28); // Inner crevice shadow
    r.drawCircleRadial3StopGradient(
        cx, cy, dialRadius - 1.2f,
        pal.dialParchmentR, pal.dialParchmentG, pal.dialParchmentB, 1.0f,
        pal.dialParchmentR * 0.92f, pal.dialParchmentG * 0.90f, pal.dialParchmentB * 0.85f, 1.0f,
        pal.dialParchmentR * 0.75f, pal.dialParchmentG * 0.72f, pal.dialParchmentB * 0.65f, 1.0f,
        dialRadius * 0.20f, dialRadius * 0.25f, 0.60f, 28
    );
    r.drawCircleOutline(cx, cy, dialRadius - 1.2f, 0.28f, 0.24f, 0.20f, 0.60f, 1.0f, 28);

    if (hasCrossMark) {
        // Red warning cross mark (as seen on bottom HUD manometer in mockup)
        float arm = dialRadius * 0.65f;
        r.drawLine(cx - arm, cy - arm, cx + arm, cy + arm, pal.needleRedR, pal.needleRedG, pal.needleRedB, 0.85f, 3.2f);
        r.drawLine(cx - arm, cy + arm, cx + arm, cy - arm, pal.needleRedR, pal.needleRedG, pal.needleRedB, 0.85f, 3.2f);
    } else {
        // Horological graduation dial ticks
        constexpr float startDialA = kPi * 0.75f;
        constexpr float sweepA = kPi * 1.50f;
        constexpr int tickCount = 21;
        float tickStep = sweepA / static_cast<float>(tickCount - 1);

        for (int t = 0; t < tickCount; ++t) {
            float ta = startDialA + static_cast<float>(t) * tickStep;
            bool isMajor = (t % 5 == 0);
            float tLen = isMajor ? (dialRadius * 0.32f) : (dialRadius * 0.18f);
            float rOuter = dialRadius - 2.5f;
            float rInner = rOuter - tLen;

            float cosT = std::cos(ta);
            float sinT = std::sin(ta);
            float tx0 = cx + rOuter * cosT;
            float ty0 = cy + rOuter * sinT;
            float tx1 = cx + rInner * cosT;
            float ty1 = cy + rInner * sinT;

            r.drawLine(tx0, ty0, tx1, ty1,
                       isMajor ? pal.dialTextR : (pal.dialTextR * 1.25f),
                       isMajor ? pal.dialTextG : (pal.dialTextG * 1.25f),
                       isMajor ? pal.dialTextB : (pal.dialTextB * 1.25f),
                       isMajor ? 0.95f : 0.65f, isMajor ? 1.4f : 0.9f);
        }

        // Calibrated indicator needle
        float needleA = needleAngleRad;
        if (needleA < -100.0f) {
            needleA = startDialA + std::clamp(valueRatio, 0.0f, 1.0f) * sweepA;
        }

        float needleLen = dialRadius * 0.78f;
        float tipX = cx + needleLen * std::cos(needleA);
        float tipY = cy + needleLen * std::sin(needleA);

        float perpX = -std::sin(needleA) * 1.8f;
        float perpY = std::cos(needleA) * 1.8f;
        float tailX = cx - (needleLen * 0.28f) * std::cos(needleA);
        float tailY = cy - (needleLen * 0.28f) * std::sin(needleA);

        // Needle body
        r.drawTriangle(tipX, tipY, cx + perpX, cy + perpY, tailX, tailY, pal.needleRedR, pal.needleRedG, pal.needleRedB, 0.98f);
        r.drawTriangle(tipX, tipY, tailX, tailY, cx - perpX, cy - perpY, pal.needleRedR * 0.85f, pal.needleRedG * 0.80f, pal.needleRedB * 0.80f, 0.98f);
    }

    // Central brass capstan boss
    float bossR = std::max(2.5f, dialRadius * 0.22f);
    r.drawCircleRadial3StopGradient(
        cx, cy, bossR,
        pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f,
        pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 1.0f,
        pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 1.0f,
        -bossR * 0.30f, -bossR * 0.35f, 0.45f, 16
    );
    r.drawCircleOutline(cx, cy, bossR, pal.brassDarkR * 0.75f, pal.brassDarkG * 0.70f, pal.brassDarkB * 0.50f, 0.95f, 1.0f, 16);

    // Optional tiny dial typography label (e.g. "PSI" or "SOL")
    if (label && label[0] != '\0') {
        float txtW = eatsbits::ui::glyph::getVectorTextWidth(7.5f, label);
        eatsbits::ui::glyph::drawVectorText(r, label, cx - txtW * 0.5f, cy + dialRadius * 0.45f, 7.5f,
                                            pal.dialTextR, pal.dialTextG, pal.dialTextB, 0.80f);
    }
}

void drawChassisFrame(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    bool drawCornerGears,
    bool drawSideGauges,
    float timeSec
) {
    const auto& pal = palette();

    // 1. Outer deep drop shadow
    r.drawRectDropShadow(x, y, w, h, 14.0f, 16.0f, eatsbits::ui::LightSource2D{x + w * 0.5f, y - 200.0f, 500.0f, 0.90f}, 0.65f);

    // 2. Heavy Cast-Iron / Boilerplate Chassis Panel
    r.drawRoundedRectGradient(
        x, y, w, h, radius,
        pal.chassisLightR, pal.chassisLightG, pal.chassisLightB,
        pal.chassisDarkR, pal.chassisDarkG, pal.chassisDarkB,
        0.98f, 6
    );
    r.drawRoundedRectOutline(x, y, w, h, radius,
                             pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 0.92f, 2.2f, 6);
    r.drawRoundedRectOutline(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, radius - 2.0f,
                             pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.55f, 1.0f, 6);

    // 3. Perimeter dome rivets
    drawRivetedBorder(r, x, y, w, h, radius, 22.0f, 2.5f, false);

    // 4. Perimeter copper conduit piping armature framing the outer edges
    const float pipeInset = 10.0f;
    const float px0 = x + pipeInset;
    const float py0 = y + pipeInset;
    const float px1 = x + w - pipeInset;
    const float py1 = y + h - pipeInset;

    // Top, bottom, left, right pipe conduits
    drawPipe(r, px0 + 16.0f, py0, px1 - 16.0f, py0, 6.5f, true, true);
    drawPipe(r, px0 + 16.0f, py1, px1 - 16.0f, py1, 6.5f, true, true);
    drawPipe(r, px0, py0 + 16.0f, px0, py1 - 16.0f, 6.5f, true, true);
    drawPipe(r, px1, py0 + 16.0f, px1, py1 - 16.0f, 6.5f, true, true);

    // 5. Interlocking Corner Gears at the 4 modal corners
    if (drawCornerGears) {
        float rotSlow = timeSec * 0.40f;
        float rotFast = -rotSlow * 1.50f;

        // Top-Left corner gear pair
        drawGear(r, x + 6.0f, y + 6.0f, 22.0f, 17.0f, 11.0f, 14, rotSlow,
                 pal.brassMidR, pal.brassMidG, pal.brassMidB, 1.0f,
                 pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f, 5, 5.0f);
        drawGear(r, x + 26.0f, y - 4.0f, 15.0f, 11.5f, 7.5f, 10, rotFast,
                 pal.brassDarkR * 1.25f, pal.brassDarkG * 1.20f, pal.brassDarkB * 1.10f, 0.95f,
                 pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.95f, 4, 3.5f);

        // Top-Right corner gear pair
        drawGear(r, x + w - 6.0f, y + 6.0f, 22.0f, 17.0f, 11.0f, 14, -rotSlow,
                 pal.brassMidR, pal.brassMidG, pal.brassMidB, 1.0f,
                 pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f, 5, 5.0f);
        drawGear(r, x + w - 26.0f, y - 4.0f, 15.0f, 11.5f, 7.5f, 10, -rotFast,
                 pal.brassDarkR * 1.25f, pal.brassDarkG * 1.20f, pal.brassDarkB * 1.10f, 0.95f,
                 pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.95f, 4, 3.5f);

        // Bottom-Left corner gear pair
        drawGear(r, x + 6.0f, y + h - 6.0f, 22.0f, 17.0f, 11.0f, 14, -rotSlow + 0.3f,
                 pal.brassMidR, pal.brassMidG, pal.brassMidB, 1.0f,
                 pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f, 5, 5.0f);

        // Bottom-Right corner gear pair
        drawGear(r, x + w - 6.0f, y + h - 6.0f, 24.0f, 18.5f, 12.0f, 16, rotSlow + 0.5f,
                 pal.brassMidR, pal.brassMidG, pal.brassMidB, 1.0f,
                 pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f, 5, 5.5f);
        drawGear(r, x + w - 28.0f, y + h + 2.0f, 14.0f, 10.5f, 7.0f, 9, rotFast,
                 pal.copperMidR, pal.copperMidG, pal.copperMidB, 0.95f,
                 pal.copperBrightR, pal.copperBrightG, pal.copperBrightB, 0.95f, 4, 3.0f);
    }

    // 6. Side Manometers mounted onto pipe manifolds
    if (drawSideGauges) {
        float midY = y + h * 0.38f;
        // Left pressure gauge
        drawPipe(r, x - 18.0f, midY, x + pipeInset, midY, 6.0f, true, false);
        drawManometerGauge(r, x - 26.0f, midY, 20.0f, -100.0f, 0.62f, false, "PSI");

        // Right pressure gauge
        drawPipe(r, x + w - pipeInset, midY, x + w + 18.0f, midY, 6.0f, false, true);
        drawManometerGauge(r, x + w + 26.0f, midY, 20.0f, -100.0f, 0.48f, false, "BAR");
    }
}

void drawPewterButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    const char* text, bool hovered, bool active,
    float fontSize
) {
    const auto& pal = palette();
    const float boost = hovered ? 0.08f : 0.0f;
    const float push = active ? 1.0f : 0.0f;

    // Drop shadow
    r.drawRoundedRect(x + 1.0f, y + 2.0f - push, w, h, radius, 0.06f, 0.05f, 0.04f, 0.55f, 4);

    // Weathered iron/pewter face
    r.drawRoundedRectGradient(
        x, y + push, w, h, radius,
        pal.chassisLightR + 0.12f + boost, pal.chassisLightG + 0.10f + boost, pal.chassisLightB + 0.08f + boost,
        pal.chassisDarkR + 0.08f + boost, pal.chassisDarkG + 0.07f + boost, pal.chassisDarkB + 0.06f + boost,
        0.98f, 4
    );
    r.drawRoundedRectOutline(x, y + push, w, h, radius,
                             pal.brassBrightR, pal.brassBrightG, pal.brassBrightB,
                             hovered ? 0.95f : 0.65f, hovered ? 1.4f : 1.1f, 4);

    // 4 corner mounting rivets
    const float rInset = 4.5f;
    drawRivet(r, x + rInset, y + rInset + push, 1.8f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.90f);
    drawRivet(r, x + w - rInset, y + rInset + push, 1.8f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.90f);
    drawRivet(r, x + rInset, y + h - rInset + push, 1.8f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.90f);
    drawRivet(r, x + w - rInset, y + h - rInset + push, 1.8f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.90f);

    // Engraved label
    if (text && text[0] != '\0') {
        float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, text);
        float tx = x + (w - txtW) * 0.5f;
        float ty = y + (h - fontSize) * 0.5f + push;

        // Shadow deboss
        eatsbits::ui::glyph::drawVectorText(r, text, tx + 0.5f, ty + 1.0f, fontSize, 0.08f, 0.07f, 0.06f, 0.80f);
        // Foreground metallic text
        eatsbits::ui::glyph::drawVectorText(r, text, tx, ty, fontSize,
                                            hovered ? 1.00f : 0.92f,
                                            hovered ? 0.95f : 0.88f,
                                            hovered ? 0.85f : 0.80f, 1.0f);
    }
}

void drawFurnaceButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    const char* text, bool hovered, bool active,
    float fontSize
) {
    const auto& pal = palette();
    const float boost = hovered ? 0.12f : 0.0f;
    const float push = active ? 1.0f : 0.0f;

    // Glowing thermal drop shadow
    r.drawRoundedRect(x - 2.0f, y - 2.0f + push, w + 4.0f, h + 4.0f, radius + 2.0f,
                      pal.furnaceAmberR, pal.furnaceAmberG, pal.furnaceAmberB, hovered ? 0.35f : 0.20f, 4);

    // Molten boiler/furnace amber gradient face
    r.drawRoundedRectGradient(
        x, y + push, w, h, radius,
        std::min(1.0f, pal.furnaceCoreR + boost), std::min(1.0f, pal.furnaceCoreG + boost), std::min(1.0f, pal.furnaceCoreB + boost),
        pal.furnaceDarkR, pal.furnaceDarkG, pal.furnaceDarkB,
        0.98f, 4
    );

    // Polished heavy brass border with inner heat rim
    r.drawRoundedRectOutline(x, y + push, w, h, radius,
                             pal.brassBrightR, pal.brassBrightG, pal.brassBrightB,
                             hovered ? 1.0f : 0.90f, hovered ? 1.8f : 1.3f, 4);
    r.drawRoundedRectOutline(x + 1.5f, y + 1.5f + push, w - 3.0f, h - 3.0f, radius - 1.5f,
                             pal.furnaceCoreR, pal.furnaceCoreG, pal.furnaceCoreB, 0.70f, 1.0f, 4);

    // 4 corner bolts
    const float rInset = 5.0f;
    drawRivet(r, x + rInset, y + rInset + push, 1.8f, true, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);
    drawRivet(r, x + w - rInset, y + rInset + push, 1.8f, true, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);
    drawRivet(r, x + rInset, y + h - rInset + push, 1.8f, true, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);
    drawRivet(r, x + w - rInset, y + h - rInset + push, 1.8f, true, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 0.95f);

    // Radiant button text
    if (text && text[0] != '\0') {
        float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, text);
        float tx = x + (w - txtW) * 0.5f;
        float ty = y + (h - fontSize) * 0.5f + push;

        // Shadow deboss
        eatsbits::ui::glyph::drawVectorText(r, text, tx + 0.5f, ty + 1.0f, fontSize, 0.25f, 0.08f, 0.02f, 0.90f);
        // Glowing gold/amber text
        eatsbits::ui::glyph::drawVectorText(r, text, tx, ty, fontSize,
                                            hovered ? 1.00f : 0.98f,
                                            hovered ? 1.00f : 0.95f,
                                            hovered ? 0.90f : 0.85f, 1.0f);
    }
}

void drawBrassButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    const char* text, bool hovered, bool highlighted,
    float fontSize
) {
    const auto& pal = palette();
    const float bgBoost = hovered ? 0.08f : 0.0f;

    // Beveled brass plate background
    r.drawRoundedRectGradient(
        x, y, w, h, radius,
        highlighted ? (pal.furnaceAmberR * 0.75f + bgBoost) : (pal.chassisLightR + 0.08f + bgBoost),
        highlighted ? (pal.furnaceAmberG * 0.65f + bgBoost) : (pal.chassisLightG + 0.07f + bgBoost),
        highlighted ? (pal.furnaceAmberB * 0.55f + bgBoost) : (pal.chassisLightB + 0.06f + bgBoost),
        pal.chassisDarkR + 0.04f, pal.chassisDarkG + 0.03f, pal.chassisDarkB + 0.03f,
        0.96f, 4
    );

    const float borderR = (hovered || highlighted) ? pal.brassBrightR : pal.brassMidR;
    const float borderG = (hovered || highlighted) ? pal.brassBrightG : pal.brassMidG;
    const float borderB = (hovered || highlighted) ? pal.brassBrightB : pal.brassMidB;
    r.drawRoundedRectOutline(x, y, w, h, radius, borderR, borderG, borderB,
                             (hovered || highlighted) ? 0.95f : 0.65f, (hovered || highlighted) ? 1.6f : 1.1f, 4);

    if (text && text[0] != '\0') {
        float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, text);
        float tx = x + (w <= 38.0f ? (w - txtW) * 0.5f : 10.0f);
        float ty = y + (h - fontSize) * 0.5f;

        eatsbits::ui::glyph::drawVectorText(
            r, text, tx, ty, fontSize,
            highlighted ? pal.furnaceCoreR : (hovered ? 0.98f : 0.92f),
            highlighted ? pal.furnaceCoreG : (hovered ? 0.92f : 0.86f),
            highlighted ? pal.furnaceCoreB : (hovered ? 0.78f : 0.72f),
            1.0f
        );
    }
}

void drawStepperBar(
    eatsbits::ui::BatchRenderer2D& r,
    float rowX, float curY, float rowW, float rowH,
    const std::string& text,
    bool prevHov, bool nextHov, bool centerHov
) {
    const auto& pal = palette();
    const float btnW = 32.0f;
    const float labelX = rowX + btnW + 6.0f;
    const float labelW = rowW - 2.0f * (btnW + 6.0f);
    const float nextX = rowX + rowW - btnW;

    // Left [ < ] Stepper Button
    drawPewterButton(r, rowX, curY, btnW, rowH, 4.0f, "<", prevHov, false, 12.0f);

    // Center Display Bar (machined brass/iron bar with corner hex bolts)
    r.drawRoundedRectGradient(
        labelX, curY, labelW, rowH, 4.0f,
        pal.chassisLightR + 0.08f + (centerHov ? 0.05f : 0.0f),
        pal.chassisLightG + 0.06f + (centerHov ? 0.04f : 0.0f),
        pal.chassisLightB + 0.04f + (centerHov ? 0.03f : 0.0f),
        pal.chassisDarkR + 0.03f, pal.chassisDarkG + 0.02f, pal.chassisDarkB + 0.02f,
        0.96f, 4
    );
    r.drawRoundedRectOutline(labelX, curY, labelW, rowH, 4.0f,
                             centerHov ? pal.brassBrightR : pal.brassMidR,
                             centerHov ? pal.brassBrightG : pal.brassMidG,
                             centerHov ? pal.brassBrightB : pal.brassMidB,
                             centerHov ? 0.95f : 0.70f, centerHov ? 1.4f : 1.1f, 4);

    // Decorative corner bolts on bar
    drawRivet(r, labelX + 4.5f, curY + 4.5f, 1.5f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.85f);
    drawRivet(r, labelX + labelW - 4.5f, curY + 4.5f, 1.5f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.85f);
    drawRivet(r, labelX + 4.5f, curY + rowH - 4.5f, 1.5f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.85f);
    drawRivet(r, labelX + labelW - 4.5f, curY + rowH - 4.5f, 1.5f, true, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.85f);

    // Bar label text
    float labelTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, text);
    eatsbits::ui::glyph::drawVectorText(
        r, text.c_str(),
        labelX + (labelW - labelTxtW) * 0.5f,
        curY + (rowH - 11.0f) * 0.5f,
        11.0f,
        centerHov ? pal.furnaceCoreR : pal.brassHighlightR,
        centerHov ? pal.furnaceCoreG : pal.brassBrightG,
        centerHov ? pal.furnaceCoreB : pal.brassBrightB,
        1.0f
    );

    // Right [ > ] Stepper Button
    drawPewterButton(r, nextX, curY, btnW, rowH, 4.0f, ">", nextHov, false, 12.0f);
}

void drawGlassSlider(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    float valueRatio,
    const char* labelText,
    bool isKnobHovered,
    float timeSec
) {
    const auto& pal = palette();
    valueRatio = std::clamp(valueRatio, 0.0f, 1.0f);

    // 1. Heavy brass outer slide casing
    r.drawRoundedRect(x, y, w, h, h * 0.5f,
                      pal.chassisDarkR * 0.80f, pal.chassisDarkG * 0.75f, pal.chassisDarkB * 0.70f, 0.98f, 6);
    r.drawRoundedRectOutline(x, y, w, h, h * 0.5f,
                             pal.brassMidR, pal.brassMidG, pal.brassMidB, 0.85f, 1.3f, 6);

    // 2. Inner cylindrical glass tube
    const float tubeInset = 2.5f;
    const float tx = x + tubeInset;
    const float ty = y + tubeInset;
    const float tw = w - 2.0f * tubeInset;
    const float th = h - 2.0f * tubeInset;
    const float tr = th * 0.5f;

    r.drawRoundedRect(tx, ty, tw, th, tr, 0.08f, 0.07f, 0.06f, 0.95f, 4);

    // 3. Molten amber level fill inside glass tube
    float fillW = std::max(tr * 2.0f, tw * valueRatio);
    r.drawRoundedRectGradient(
        tx, ty, fillW, th, tr,
        pal.furnaceCoreR, pal.furnaceCoreG, pal.furnaceCoreB,
        pal.furnaceDarkR, pal.furnaceDarkG, pal.furnaceDarkB,
        0.92f, 4
    );

    // Specular glass highlight reflection across top of tube
    r.drawLine(tx + tr, ty + 1.2f, tx + tw - tr, ty + 1.2f, 1.0f, 1.0f, 1.0f, 0.40f, 1.0f);

    // Calibrated glass tick marks
    constexpr int numTicks = 9;
    float tickSpacing = (tw - 2.0f * tr) / static_cast<float>(numTicks - 1);
    for (int k = 0; k < numTicks; ++k) {
        float kx = tx + tr + static_cast<float>(k) * tickSpacing;
        r.drawLine(kx, ty + 2.0f, kx, ty + th - 2.0f,
                   pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.45f, 0.8f);
    }

    // 4. Brass Cogwheel Thumb-Knob
    float knobX = tx + tr + (tw - 2.0f * tr) * valueRatio;
    float knobY = y + h * 0.5f;
    float knobRadius = h * 0.68f;

    drawGear(r, knobX, knobY, knobRadius, knobRadius * 0.78f, knobRadius * 0.55f, 8,
             valueRatio * kTwoPi * 2.0f + timeSec * 0.2f,
             isKnobHovered ? pal.brassBrightR : pal.brassMidR,
             isKnobHovered ? pal.brassBrightG : pal.brassMidG,
             isKnobHovered ? pal.brassBrightB : pal.brassMidB, 1.0f,
             pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f, 4, knobRadius * 0.35f);

    // Label text beside or above slider
    if (labelText && labelText[0] != '\0') {
        (void)eatsbits::ui::glyph::getVectorTextWidth(10.5f, labelText);
        eatsbits::ui::glyph::drawVectorText(
            r, labelText, x + w + 12.0f, y + (h - 10.5f) * 0.5f, 10.5f,
            pal.furnaceCoreR, pal.furnaceCoreG, pal.furnaceCoreB, 1.0f
        );
    }
}

void drawHudClockworkChassis(
    eatsbits::ui::BatchRenderer2D& r,
    float popX, float popY, float popW, float popH,
    float moveCountOrTime,
    float timeSec
) {
    const auto& pal = palette();

    // 1. Drop shadow behind entire HUD assembly
    r.drawRectDropShadow(popX, popY, popW, popH, 12.0f, 16.0f,
                         eatsbits::ui::LightSource2D{popX + popW * 0.5f, popY - 200.0f, 500.0f, 0.90f}, 0.65f);

    // 2. Weathered cast-iron chassis plate with bevel and rivets
    r.drawRoundedRectGradient(
        popX, popY, popW, popH, 10.0f,
        pal.chassisLightR, pal.chassisLightG, pal.chassisLightB,
        pal.chassisDarkR, pal.chassisDarkG, pal.chassisDarkB,
        0.98f, 4
    );
    r.drawRoundedRectOutline(popX, popY, popW, popH, 10.0f,
                             pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 0.95f, 1.8f, 4);
    r.drawRoundedRectOutline(popX + 2.0f, popY + 2.0f, popW - 4.0f, popH - 4.0f, 8.0f,
                             pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.45f, 1.0f, 4);

    // Corner bolts
    drawRivet(r, popX + 8.0f, popY + 8.0f, 2.2f, true);
    drawRivet(r, popX + popW - 8.0f, popY + 8.0f, 2.2f, true);
    drawRivet(r, popX + 8.0f, popY + popH - 8.0f, 2.2f, true);
    drawRivet(r, popX + popW - 8.0f, popY + popH - 8.0f, 2.2f, true);

    // 3. Top Analog Steam Pressure Manometer with Connecting Copper Pipe Loop
    const float gaugeRadius = 26.0f;
    const float topGaugeCX = popX + popW * 0.5f;
    const float topGaugeCY = popY - gaugeRadius + 6.0f;

    // Copper pipe armature looping around top gauge
    drawPipe(r, popX + 20.0f, popY, topGaugeCX - gaugeRadius + 2.0f, topGaugeCY, 6.0f, true, true);
    drawPipe(r, topGaugeCX + gaugeRadius - 2.0f, topGaugeCY, popX + popW - 20.0f, popY, 6.0f, true, true);

    float needleVal = std::fmod(moveCountOrTime * 0.05f + timeSec * 0.1f, 1.0f);
    drawManometerGauge(r, topGaugeCX, topGaugeCY, gaugeRadius, -100.0f, needleVal, false, "STEAM");

    // 4. Bottom Manometer with Red Crossed Gauge (as seen in mockup)
    const float btmGaugeRadius = 24.0f;
    const float btmGaugeCX = popX + popW * 0.5f;
    const float btmGaugeCY = popY + popH + btmGaugeRadius - 6.0f;

    drawPipe(r, popX + 24.0f, popY + popH, btmGaugeCX - btmGaugeRadius + 2.0f, btmGaugeCY, 6.0f, true, true);
    drawPipe(r, btmGaugeCX + btmGaugeRadius - 2.0f, btmGaugeCY, popX + popW - 24.0f, popY + popH, 6.0f, true, true);
    drawManometerGauge(r, btmGaugeCX, btmGaugeCY, btmGaugeRadius, 0.0f, 0.0f, true, nullptr);

    // Left and right vertical copper conduits flanking HUD buttons
    drawPipe(r, popX + 4.0f, popY + 18.0f, popX + 4.0f, popY + popH - 18.0f, 5.0f, true, true);
    drawPipe(r, popX + popW - 4.0f, popY + 18.0f, popX + popW - 4.0f, popY + popH - 18.0f, 5.0f, true, true);
}

void drawClockworkMenuOrb(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool hovered, bool menuOpen,
    float timeSec
) {
    const auto& pal = palette();
    const float alpha = (hovered || menuOpen) ? 0.98f : 0.70f;
    const float rot = timeSec * 0.5f;

    // Single tight, physically grounded drop shadow right underneath orb
    r.drawCircleDropShadow(cx, cy, radius, 3.0f,
                           eatsbits::ui::LightSource2D{cx - 120.0f, cy - 240.0f, 400.0f, 0.85f}, alpha * 0.45f);

    // Outer interlocking gear teeth rim
    drawGear(r, cx, cy, radius, radius * 0.82f, radius * 0.65f, 16, rot,
             pal.brassMidR, pal.brassMidG, pal.brassMidB, alpha,
             pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 6, radius * 0.40f);

    // Spun brass domed capstan
    r.drawCircleRadial3StopGradient(
        cx, cy, radius * 0.58f,
        pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha,
        pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, alpha,
        pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, alpha,
        -radius * 0.20f, -radius * 0.25f, 0.45f, 24
    );

    if (menuOpen) {
        // Crisp steam cross / release indicator when menu is pinned/toggled open
        const float xSize = radius * 0.32f;
        r.drawLine(cx - xSize, cy - xSize, cx + xSize, cy + xSize,
                   pal.furnaceAmberR, pal.furnaceAmberG, pal.furnaceAmberB, alpha, 2.8f);
        r.drawLine(cx - xSize, cy + xSize, cx + xSize, cy - xSize,
                   pal.furnaceAmberR, pal.furnaceAmberG, pal.furnaceAmberB, alpha, 2.8f);
        r.drawCircleOutline(cx, cy, radius * 0.42f,
                            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha * 0.8f, 1.2f, 24);
    } else {
        // Recessed furnace well
        r.drawCircle(cx, cy, radius * 0.42f, 0.10f, 0.08f, 0.06f, alpha * 0.95f, 20);

        // Warm molten furnace core glow radiating outwards
        r.drawCircleRadialGradient(
            cx, cy, radius * 0.40f,
            pal.furnaceCoreR, pal.furnaceCoreG, pal.furnaceCoreB, alpha * (hovered ? 0.90f : 0.65f),
            0.08f, 0.06f, 0.04f, 0.0f,
            0.0f, 0.0f, 20
        );

        // Mini planetary brass cog (counter-rotating)
        drawGear(r, cx + radius * 0.18f, cy - radius * 0.14f,
                 radius * 0.14f, radius * 0.10f, radius * 0.035f, 7, -rot * 2.2f,
                 pal.brassMidR, pal.brassMidG, pal.brassMidB, alpha * 0.85f,
                 pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha * 0.85f,
                 3, radius * 0.06f);

        // Mini pressure manometer dial
        drawManometerGauge(r, cx - radius * 0.20f, cy + radius * 0.16f, radius * 0.12f, 0.72f);

        // Signature SolEngine Victorian embossed brass 'S' insignia
        const float fontSize = radius * 0.68f;
        const float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, "S");

        // Embossed drop shadow
        eatsbits::ui::glyph::drawVectorText(
            r, "S", cx - txtW * 0.5f + 1.2f, cy - fontSize * 0.5f + 1.2f, fontSize,
            0.04f, 0.03f, 0.02f, alpha * 0.90f
        );

        // Burnished brass / furnace highlight core
        eatsbits::ui::glyph::drawVectorText(
            r, "S", cx - txtW * 0.5f, cy - fontSize * 0.5f, fontSize,
            hovered ? pal.brassBrightR : pal.brassHighlightR,
            hovered ? pal.brassBrightG : pal.brassHighlightG,
            hovered ? pal.brassBrightB : pal.brassHighlightB,
            alpha
        );
    }
}

void drawClockworkMedallion(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool hovered, bool active,
    const char* glyph,
    float timeSec
) {
    const auto& pal = palette();
    const float alpha = hovered ? 1.0f : 0.88f;

    if (active) {
        cx += 0.8f;
        cy += 1.2f;
    }

    // 1. Single tight, physically grounded drop shadow right underneath medallion
    r.drawCircleDropShadow(
        cx, cy, radius, 3.0f,
        eatsbits::ui::LightSource2D{cx - 120.0f, cy - 240.0f, 400.0f, 0.85f},
        alpha * 0.45f
    );

    // 2. Hover under-glow (furnace amber radiance)
    if (hovered) {
        r.drawCircleRadialGradient(
            cx, cy, radius * 1.30f,
            pal.furnaceCoreR, pal.furnaceCoreG, pal.furnaceCoreB, 0.65f,
            0.08f, 0.06f, 0.04f, 0.0f,
            0.0f, 0.0f, 24
        );
    }

    // 3. Outer gear-rim / riveted brass bezel
    const float gearRot = hovered ? (timeSec * 0.35f) : 0.0f;
    drawGear(
        r, cx, cy, radius, radius * 0.88f, radius * 0.74f, 16, gearRot,
        pal.brassMidR, pal.brassMidG, pal.brassMidB, alpha,
        hovered ? pal.brassHighlightR : pal.brassBrightR,
        hovered ? pal.brassHighlightG : pal.brassBrightG,
        hovered ? pal.brassHighlightB : pal.brassBrightB,
        alpha, 4, radius * 0.5f
    );

    // Rivets on bezel perimeter
    for (int i = 0; i < 4; ++i) {
        float angle = static_cast<float>(i) * (kTwoPi / 4.0f) + (gearRot * 0.5f);
        float rx = cx + std::cos(angle) * (radius * 0.80f);
        float ry = cy + std::sin(angle) * (radius * 0.80f);
        drawRivet(r, rx, ry, 1.6f, false, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha * 0.9f);
    }

    // 4. Domed concentric copper/bronze disc
    const float faceRadius = radius * 0.70f;
    r.drawCircleRadial3StopGradient(
        cx, cy, faceRadius,
        pal.copperBrightR, pal.copperBrightG, pal.copperBrightB, alpha,
        pal.copperMidR, pal.copperMidG, pal.copperMidB, alpha,
        pal.copperDarkR, pal.copperDarkG, pal.copperDarkB, alpha,
        -faceRadius * 0.22f, -faceRadius * 0.25f, 0.48f, 24
    );

    // Fine concentric etched rings
    r.drawCircleOutline(
        cx, cy, faceRadius * 0.72f,
        pal.copperDarkR, pal.copperDarkG, pal.copperDarkB, alpha * 0.35f, 0.8f, 24
    );
    r.drawCircleOutline(
        cx, cy, faceRadius * 0.70f,
        pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha * 0.22f, 0.6f, 24
    );

    // 5. Embossed Glyph & Procedural Vector Icons
    if (!glyph || glyph[0] == '\0') return;

    const bool isStar = (glyph[0] == '*' && glyph[1] == '\0');
    const bool isAutoPlay = (std::string_view(glyph) == "▶" ||
                             std::string_view(glyph) == "\xE2\x96\xB6" ||
                             std::string_view(glyph) == "PLAY");
    const bool isRestart = (std::string_view(glyph) == "↺" ||
                            std::string_view(glyph) == "\xE2\x86\xBA" ||
                            (glyph[0] == 'R' && glyph[1] == '\0'));

    if (isStar) {
        // Guaranteed Solvable Deal: 5-point golden vector star
        const float starRad = faceRadius * 0.60f;
        // Ambient drop shadow
        vector_icon::drawStar(
            r, cx + 1.4f, cy + 1.4f, starRad,
            0.04f, 0.03f, 0.02f, alpha * 0.90f,
            0.04f, 0.03f, 0.02f, alpha * 0.90f, 1.0f
        );
        // Golden star
        vector_icon::drawStar(
            r, cx, cy, starRad,
            hovered ? pal.brassHighlightR : pal.brassBrightR,
            hovered ? pal.brassHighlightG : pal.brassBrightG,
            hovered ? pal.brassHighlightB : pal.brassBrightB,
            alpha,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha,
            1.2f
        );
    } else if (isAutoPlay) {
        // Auto-Play: solid vector play triangle pointing right
        const float triHalfH = faceRadius * 0.48f;
        const float triW = faceRadius * 0.55f;
        const float x0 = cx - triW * 0.40f;
        const float y0 = cy - triHalfH;
        const float x1 = cx + triW * 0.60f;
        const float y1 = cy;
        const float x2 = cx - triW * 0.40f;
        const float y2 = cy + triHalfH;

        // Drop shadow
        r.drawTriangle(x0 + 1.4f, y0 + 1.4f, x1 + 1.4f, y1 + 1.4f, x2 + 1.4f, y2 + 1.4f,
                       0.04f, 0.03f, 0.02f, alpha * 0.90f);
        // Face
        r.drawTriangle(x0, y0, x1, y1, x2, y2,
                       hovered ? pal.brassHighlightR : pal.brassBrightR,
                       hovered ? pal.brassHighlightG : pal.brassBrightG,
                       hovered ? pal.brassHighlightB : pal.brassBrightB,
                       alpha);
        // Outer outline
        r.drawLine(x0, y0, x1, y1, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 1.2f);
        r.drawLine(x1, y1, x2, y2, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 1.2f);
        r.drawLine(x2, y2, x0, y0, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 1.2f);
    } else if (isRestart) {
        // Procedural Circular Rewind Arrow (anti-clockwise)
        const float arcR = faceRadius * 0.48f;
        const float ahL = faceRadius * 0.32f;
        const float ahW = faceRadius * 0.24f;
        constexpr int numSegs = 22;
        constexpr float startTheta = 1.10f * kPi;
        constexpr float endTheta = -0.50f * kPi; // 12 o'clock (top)

        // Drop shadow pass
        for (int i = 0; i < numSegs; ++i) {
            float t0 = startTheta + (endTheta - startTheta) * (static_cast<float>(i) / static_cast<float>(numSegs));
            float t1 = startTheta + (endTheta - startTheta) * (static_cast<float>(i + 1) / static_cast<float>(numSegs));
            r.drawLine(cx + arcR * std::cos(t0) + 1.4f, cy + arcR * std::sin(t0) + 1.4f,
                       cx + arcR * std::cos(t1) + 1.4f, cy + arcR * std::sin(t1) + 1.4f,
                       0.04f, 0.03f, 0.02f, alpha * 0.90f, 2.6f);
        }
        // Shadow arrowhead at 12 o'clock pointing left
        r.drawTriangle(cx - ahL + 1.4f, cy - arcR + 1.4f,
                       cx + 1.4f, cy - arcR - ahW + 1.4f,
                       cx + 1.4f, cy - arcR + ahW + 1.4f,
                       0.04f, 0.03f, 0.02f, alpha * 0.90f);

        // Polished brass stroke
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;

        for (int i = 0; i < numSegs; ++i) {
            float t0 = startTheta + (endTheta - startTheta) * (static_cast<float>(i) / static_cast<float>(numSegs));
            float t1 = startTheta + (endTheta - startTheta) * (static_cast<float>(i + 1) / static_cast<float>(numSegs));
            r.drawLine(cx + arcR * std::cos(t0), cy + arcR * std::sin(t0),
                       cx + arcR * std::cos(t1), cy + arcR * std::sin(t1),
                       brR, brG, brB, alpha, 2.4f);
        }
        // Brass arrowhead at top pointing left
        r.drawTriangle(cx - ahL, cy - arcR,
                       cx, cy - arcR - ahW,
                       cx, cy - arcR + ahW,
                       brR, brG, brB, alpha);
        r.drawLine(cx - ahL, cy - arcR, cx, cy - arcR - ahW, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 1.0f);
        r.drawLine(cx, cy - arcR - ahW, cx, cy - arcR + ahW, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 1.0f);
        r.drawLine(cx, cy - arcR + ahW, cx - ahL, cy - arcR, pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha, 1.0f);
    } else {
        // Vector typography glyphs: "<", ">", "+", "?", "G", "#"
        const float fontSize = (glyph[0] == '+' || glyph[0] == '#' || glyph[0] == '<' || glyph[0] == '>')
            ? (faceRadius * 1.15f) : (faceRadius * 0.95f);
        const float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, glyph);
        const float txtX = cx - txtW * 0.5f;
        const float txtY = cy - fontSize * 0.5f;

        // 1. Subtle warm ambient glow behind glyph
        eatsbits::ui::glyph::drawVectorText(
            r, glyph, txtX, txtY, fontSize,
            pal.furnaceCoreR, pal.furnaceCoreG, pal.furnaceCoreB,
            hovered ? 0.60f : 0.25f
        );

        // 2. Embossed drop shadow
        eatsbits::ui::glyph::drawVectorText(
            r, glyph, txtX + 1.4f, txtY + 1.4f, fontSize,
            0.04f, 0.03f, 0.02f, alpha * 0.90f
        );

        // 3. Polished brass core glyph relief
        eatsbits::ui::glyph::drawVectorText(
            r, glyph, txtX, txtY, fontSize,
            hovered ? pal.brassHighlightR : pal.brassBrightR,
            hovered ? pal.brassHighlightG : pal.brassBrightG,
            hovered ? pal.brassHighlightB : pal.brassBrightB,
            alpha
        );
    }
}

void drawClockworkConduitBar(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    bool isVertical,
    float timeSec
) {
    const auto& pal = palette();

    // 1. Drop shadow behind the entire chassis
    r.drawRectDropShadow(
        x, y, w, h, 8.0f, 14.0f,
        eatsbits::ui::LightSource2D{x + w * 0.5f, -200.0f, 1.0f}, 0.65f
    );

    // 2. Weathered cast-iron backing bed
    r.drawRoundedRectGradient(
        x, y, w, h, 8.0f,
        pal.chassisLightR, pal.chassisLightG, pal.chassisLightB,
        pal.chassisDarkR, pal.chassisDarkG, pal.chassisDarkB,
        0.96f, 4
    );

    // Outer and inner brass trim
    r.drawRoundedRectOutline(x, y, w, h, 8.0f, pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 0.95f, 1.6f, 4);
    r.drawRoundedRectOutline(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, 6.0f, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.35f, 0.9f, 4);

    // Corner hex rivets
    drawRivet(r, x + 6.0f, y + 6.0f, 2.0f, true);
    drawRivet(r, x + w - 6.0f, y + 6.0f, 2.0f, true);
    drawRivet(r, x + 6.0f, y + h - 6.0f, 2.0f, true);
    drawRivet(r, x + w - 6.0f, y + h - 6.0f, 2.0f, true);

    // 3. Twin steam pipes passing behind medallions
    if (!isVertical) {
        // Horizontal pipes along top and bottom edges
        const float pipeY0 = y + 7.0f;
        const float pipeY1 = y + h - 7.0f;
        drawPipe(r, x + 10.0f, pipeY0, x + w - 10.0f, pipeY0, 4.5f, true, true);
        drawPipe(r, x + 10.0f, pipeY1, x + w - 10.0f, pipeY1, 4.5f, true, true);

        // Saddle clamp brackets at 25%, 50%, 75%
        for (float ratio : {0.25f, 0.50f, 0.75f}) {
            float bx = x + w * ratio;
            r.drawRect(bx - 3.0f, y + 3.0f, 6.0f, h - 6.0f, pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 0.85f);
            drawRivet(r, bx, y + 5.0f, 1.4f, false);
            drawRivet(r, bx, y + h - 5.0f, 1.4f, false);
        }

        // Mini accent gauge at end
        drawManometerGauge(r, x + w - 7.0f, y + h * 0.5f, 10.0f, timeSec * 0.4f, 0.6f, false, nullptr);
    } else {
        // Vertical pipes along left and right edges
        const float pipeX0 = x + 7.0f;
        const float pipeX1 = x + w - 7.0f;
        drawPipe(r, pipeX0, y + 10.0f, pipeX0, y + h - 10.0f, 4.5f, true, true);
        drawPipe(r, pipeX1, y + 10.0f, pipeX1, y + h - 10.0f, 4.5f, true, true);

        // Saddle clamp brackets
        for (float ratio : {0.25f, 0.50f, 0.75f}) {
            float by = y + h * ratio;
            r.drawRect(x + 3.0f, by - 3.0f, w - 6.0f, 6.0f, pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, 0.85f);
            drawRivet(r, x + 5.0f, by, 1.4f, false);
            drawRivet(r, x + w - 5.0f, by, 1.4f, false);
        }

        // Mini accent gauge at end
        drawManometerGauge(r, x + w * 0.5f, y + h - 7.0f, 10.0f, timeSec * 0.4f, 0.6f, false, nullptr);
    }
}

void drawClockworkTooltip(
    eatsbits::ui::BatchRenderer2D& r,
    float targetX, float targetY, float targetW, float targetH,
    const char* text,
    int placement
) {
    if (!text || text[0] == '\0') return;

    const auto& pal = palette();
    const float fontSize = 10.5f;
    const float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, text);
    const float padX = 8.0f;
    const float padY = 5.0f;
    const float tipW = txtW + padX * 2.0f;
    const float tipH = fontSize + padY * 2.0f;

    float tipX = 0.0f;
    float tipY = 0.0f;

    if (placement == 0) { // Above
        tipX = targetX + (targetW - tipW) * 0.5f;
        tipY = targetY - tipH - 8.0f;
    } else if (placement == 1) { // Below
        tipX = targetX + (targetW - tipW) * 0.5f;
        tipY = targetY + targetH + 8.0f;
    } else if (placement == 2) { // Left
        tipX = targetX - tipW - 8.0f;
        tipY = targetY + (targetH - tipH) * 0.5f;
    } else { // Right
        tipX = targetX + targetW + 8.0f;
        tipY = targetY + (targetH - tipH) * 0.5f;
    }

    // Drop shadow
    r.drawRectDropShadow(
        tipX, tipY, tipW, tipH, 4.0f, 6.0f,
        eatsbits::ui::LightSource2D{tipX + tipW * 0.5f, tipY - 100.0f, 400.0f, 0.85f}, 0.55f
    );

    // Pill background (dark iron with subtle parchment/amber warm core)
    r.drawRoundedRect(tipX, tipY, tipW, tipH, 4.0f, 0.10f, 0.09f, 0.08f, 0.96f, 4);
    r.drawRoundedRectOutline(tipX, tipY, tipW, tipH, 4.0f, pal.brassBrightR, pal.brassBrightG, pal.brassBrightB, 0.90f, 1.2f, 4);

    // Text
    eatsbits::ui::glyph::drawVectorText(
        r, text, tipX + padX, tipY + padY - 0.5f, fontSize,
        pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, 1.0f
    );
}

void drawClockworkCloseButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float size,
    bool hovered,
    float /*timeSec*/
) {
    const auto& cPal = palette();
    const float radius = size * 0.48f;
    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.5f;

    // Outer circle face
    r.drawCircle(cx, cy, radius,
                 hovered ? cPal.furnaceDarkR : cPal.chassisLightR,
                 hovered ? cPal.furnaceDarkG : cPal.chassisLightG,
                 hovered ? cPal.furnaceDarkB : cPal.chassisLightB,
                 0.95f, 16);

    // Bezel outline
    r.drawCircleOutline(cx, cy, radius,
                        hovered ? cPal.furnaceAmberR : cPal.brassBrightR,
                        hovered ? cPal.furnaceAmberG : cPal.brassBrightG,
                        hovered ? cPal.furnaceAmberB : cPal.brassBrightB,
                        0.95f, 1.2f, 16);

    // Vector 'X' glyph centered
    const float xFontSize = size * 0.46f;
    const float xTxtW = eatsbits::ui::glyph::getVectorTextWidth(xFontSize, "X");
    const float xTxtX = x + (size - xTxtW) * 0.5f;
    const float xTxtY = y + (size - xFontSize) * 0.5f;

    // Crisp drop shadow
    eatsbits::ui::glyph::drawVectorText(
        r, "X", xTxtX + 1.2f, xTxtY + 1.2f, xFontSize,
        0.04f, 0.03f, 0.02f, 0.90f
    );
    // Polished brass 'X'
    eatsbits::ui::glyph::drawVectorText(
        r, "X", xTxtX, xTxtY, xFontSize,
        hovered ? 1.0f : cPal.brassHighlightR,
        hovered ? 0.95f : cPal.brassHighlightG,
        hovered ? 0.90f : cPal.brassHighlightB,
        1.0f
    );
}

} // namespace solengine::clockwork_ui

