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

    // 1. Optional hard-edged offset drop shadow (matches low-poly style, zero blur cost)
    if (drawShadow) {
        const float offX = 2.0f, offY = 2.5f;
        const int shadowSegs = std::clamp(teeth, 6, 12);
        for (int i = 0; i < shadowSegs; ++i) {
            float a0 = angleRad + static_cast<float>(i) * (kTwoPi / static_cast<float>(shadowSegs));
            float a1 = angleRad + static_cast<float>(i + 1) * (kTwoPi / static_cast<float>(shadowSegs));
            r.drawTriangle(
                cx + offX, cy + offY,
                cx + offX + outerRadius * 0.92f * std::cos(a0), cy + offY + outerRadius * 0.92f * std::sin(a0),
                cx + offX + outerRadius * 0.92f * std::cos(a1), cy + offY + outerRadius * 0.92f * std::sin(a1),
                0.0f, 0.0f, 0.0f, 0.30f * bodyA
            );
        }
    }

    // 2. Low-poly faceted annular ring between root radius and inner radius
    const int ringSegments = std::clamp(teeth, 6, 16);
    constexpr float kLx = -0.62f, kLy = -0.78f;
    for (int i = 0; i < ringSegments; ++i) {
        float a0 = angleRad + static_cast<float>(i) * (kTwoPi / static_cast<float>(ringSegments));
        float a1 = angleRad + static_cast<float>(i + 1) * (kTwoPi / static_cast<float>(ringSegments));
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        float am = (a0 + a1) * 0.5f;
        float shade = 1.0f + 0.14f * (std::cos(am) * kLx + std::sin(am) * kLy);

        r.drawTriangle(
            cx + innerRadius * c0, cy + innerRadius * s0,
            cx + rootRadius * c0,  cy + rootRadius * s0,
            cx + rootRadius * c1,  cy + rootRadius * s1,
            bodyR * shade, bodyG * shade, bodyB * shade, bodyA
        );
        r.drawTriangle(
            cx + innerRadius * c0, cy + innerRadius * s0,
            cx + rootRadius * c1,  cy + rootRadius * s1,
            cx + innerRadius * c1, cy + innerRadius * s1,
            bodyR * (shade * 0.92f), bodyG * (shade * 0.92f), bodyB * (shade * 0.92f), bodyA
        );
    }

    // 3. Faceted Trapezoidal Gear Teeth around perimeter
    for (int t = 0; t < teeth; ++t) {
        float baseA = angleRad + static_cast<float>(t) * toothAngle;
        float a0 = baseA;
        float a1 = baseA + toothAngle * 0.22f;
        float a2 = baseA + toothAngle * 0.52f;
        float a3 = baseA + toothAngle * 0.74f;
        float am = baseA + toothAngle * 0.37f;
        float light = std::cos(am) * kLx + std::sin(am) * kLy;
        float sHi = 1.04f + 0.14f * light;
        float sLo = 0.90f + 0.12f * light;

        float p0x = cx + rootRadius * std::cos(a0), p0y = cy + rootRadius * std::sin(a0);
        float p1x = cx + outerRadius * std::cos(a1), p1y = cy + outerRadius * std::sin(a1);
        float p2x = cx + outerRadius * std::cos(a2), p2y = cy + outerRadius * std::sin(a2);
        float p3x = cx + rootRadius * std::cos(a3), p3y = cy + rootRadius * std::sin(a3);

        r.drawTriangle(p0x, p0y, p1x, p1y, p2x, p2y, bodyR * sHi, bodyG * sHi, bodyB * sHi, bodyA);
        r.drawTriangle(p0x, p0y, p2x, p2y, p3x, p3y, bodyR * sLo, bodyG * sLo, bodyB * sLo, bodyA);
    }

    // 4. Radial Spokes inside wheel cutout
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

            r.drawTriangle(xInner0, yInner0, xOuter0, yOuter0, xOuter1, yOuter1, bodyR * 1.04f, bodyG * 1.02f, bodyB, bodyA);
            r.drawTriangle(xInner0, yInner0, xOuter1, yOuter1, xInner1, yInner1, bodyR * 0.84f, bodyG * 0.82f, bodyB * 0.80f, bodyA);
        }
    }

    // 5. Faceted Hexagonal Brass Boss & Axle Core
    constexpr int kHubSides = 6;
    const float axleR = std::max(1.5f, hubRadius * 0.36f);
    for (int i = 0; i < kHubSides; ++i) {
        float a0 = angleRad + static_cast<float>(i) * (kTwoPi / static_cast<float>(kHubSides));
        float a1 = angleRad + static_cast<float>(i + 1) * (kTwoPi / static_cast<float>(kHubSides));
        float am = (a0 + a1) * 0.5f;
        float light = std::cos(am) * kLx + std::sin(am) * kLy;
        float t = std::clamp(0.5f + 0.5f * light, 0.0f, 1.0f);
        float hr = bodyR * (1.0f - t) + rimR * t;
        float hg = bodyG * (1.0f - t) + rimG * t;
        float hb = bodyB * (1.0f - t) + rimB * t;
        r.drawTriangle(
            cx, cy,
            cx + hubRadius * std::cos(a0), cy + hubRadius * std::sin(a0),
            cx + hubRadius * std::cos(a1), cy + hubRadius * std::sin(a1),
            hr, hg, hb, bodyA
        );
        r.drawTriangle(
            cx, cy,
            cx + axleR * std::cos(a0), cy + axleR * std::sin(a0),
            cx + axleR * std::cos(a1), cy + axleR * std::sin(a1),
            0.14f, 0.12f, 0.10f, bodyA
        );
    }
}

void drawRivet(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool isHexBolt,
    float brassR, float brassG, float brassB, float alpha
) {
    if (radius <= 0.5f || alpha <= 0.0f) return;

    // Enforce neutral steel grayscale for all rivets/bolts (prevents color overflow or unwanted tint)
    const float gray = std::clamp(0.299f * brassR + 0.587f * brassG + 0.114f * brassB, 0.42f, 0.76f);

    // Subtle drop shadow cast toward bottom-right
    r.drawCircle(cx + 1.0f, cy + 1.2f, radius, 0.05f, 0.05f, 0.05f, alpha * 0.55f, 12);

    if (isHexBolt) {
        // Hexagonal bolt head
        constexpr int sides = 6;
        std::array<eatsbits::ui::Point2D, 6> pts;
        for (int i = 0; i < sides; ++i) {
            float ang = static_cast<float>(i) * (kTwoPi / 6.0f);
            pts[i] = eatsbits::ui::Point2D{cx + radius * std::cos(ang), cy + radius * std::sin(ang)};
        }
        r.drawConvexPolygon(pts.data(), sides, gray * 0.84f, gray * 0.84f, gray * 0.84f, alpha);
        r.drawPath(pts.data(), sides, 1.0f, std::min(1.0f, gray * 1.15f), std::min(1.0f, gray * 1.15f), std::min(1.0f, gray * 1.15f), alpha, true);
        // Slotted drive line
        r.drawLine(cx - radius * 0.55f, cy, cx + radius * 0.55f, cy, 0.12f, 0.12f, 0.12f, alpha * 0.85f, 1.0f);
    } else {
        // Spherical dome rivet with directional specular highlight
        const float hi = std::min(1.0f, gray * 1.25f);
        const float lo = gray * 0.36f;
        r.drawCircleRadial3StopGradient(
            cx, cy, radius,
            hi, hi, hi, alpha,
            gray, gray, gray, alpha,
            lo, lo, lo, alpha,
            -radius * 0.35f, -radius * 0.40f, 0.50f, 16
        );
        r.drawCircleOutline(cx, cy, radius, gray * 0.32f, gray * 0.32f, gray * 0.32f, alpha * 0.80f, 0.8f, 16);
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
    [[maybe_unused]] float timeSec
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

    // 5. Interlocking Top Corner Gears (static, bottom gears removed per design)
    if (drawCornerGears) {
        constexpr float rotSlow = 0.25f;
        constexpr float rotFast = -0.40f;

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
    bool prevHov, bool nextHov, bool centerHov,
    bool showDropdownHint
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
    const float textRightPad = showDropdownHint ? 18.0f : 8.0f;
    const float textLeftPad = 8.0f;
    const float availTextW = std::max(16.0f, labelW - textLeftPad - textRightPad);
    std::string dispText = text;
    float labelTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, dispText);
    if (labelTxtW > availTextW) {
        while (dispText.size() > 3 && eatsbits::ui::glyph::getVectorTextWidth(11.0f, dispText + "...") > availTextW) {
            dispText.pop_back();
        }
        dispText += "...";
        labelTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, dispText);
    }
    const float centeredX = labelX + (labelW - labelTxtW) * 0.5f;
    const float textX = std::clamp(centeredX, labelX + textLeftPad, labelX + labelW - textRightPad - labelTxtW);
    eatsbits::ui::glyph::drawVectorText(
        r, dispText.c_str(),
        textX,
        curY + (rowH - 11.0f) * 0.5f,
        11.0f,
        centerHov ? pal.furnaceCoreR : pal.brassHighlightR,
        centerHov ? pal.furnaceCoreG : pal.brassBrightG,
        centerHov ? pal.furnaceCoreB : pal.brassBrightB,
        1.0f
    );

    // Subtle dropdown chevron indicating click/tap opens a scrollable list
    if (showDropdownHint && labelW > 48.0f) {
        const float cx = labelX + labelW - 13.5f;
        const float cy = curY + rowH * 0.5f - 1.5f;
        const float cr = centerHov ? pal.furnaceCoreR : pal.brassBrightR;
        const float cg = centerHov ? pal.furnaceCoreG : pal.brassBrightG;
        const float cb = centerHov ? pal.furnaceCoreB : pal.brassBrightB;
        const float ca = centerHov ? 0.98f : 0.72f;
        r.drawLine(cx - 3.2f, cy, cx, cy + 3.2f, cr, cg, cb, ca, 1.4f);
        r.drawLine(cx, cy + 3.2f, cx + 3.2f, cy, cr, cg, cb, ca, 1.4f);
    }

    // Right [ > ] Stepper Button
    drawPewterButton(r, nextX, curY, btnW, rowH, 4.0f, ">", nextHov, false, 12.0f);
}

void drawGlassSlider(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    float valueRatio,
    const char* labelText,
    bool isKnobHovered,
    [[maybe_unused]] float timeSec
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
             valueRatio * kTwoPi * 2.0f,
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

    ClockworkLogoConfig cfg;
    cfg.animated = hovered && !menuOpen;
    cfg.showGauges = (radius >= 22.0f);
    cfg.showPipes = (radius >= 18.0f);
    cfg.showGlow = true;
    cfg.highlightWhite = false;
    cfg.alpha = alpha;
    cfg.brassTint = hovered ? 1.15f : 1.0f;

    if (menuOpen) {
        cfg.alpha = alpha * 0.75f;
        drawClockworkLogo(r, cx, cy, radius, timeSec, cfg);

        // Crisp steam cross / release indicator when menu is pinned/toggled open
        const float xSize = radius * 0.32f;
        r.drawLine(cx - xSize, cy - xSize, cx + xSize, cy + xSize,
                   pal.furnaceAmberR, pal.furnaceAmberG, pal.furnaceAmberB, alpha, 2.8f);
        r.drawLine(cx - xSize, cy + xSize, cx + xSize, cy - xSize,
                   pal.furnaceAmberR, pal.furnaceAmberG, pal.furnaceAmberB, alpha, 2.8f);
        r.drawCircleOutline(cx, cy, radius * 0.42f,
                            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB, alpha * 0.8f, 1.2f, 24);
    } else {
        drawClockworkLogo(r, cx, cy, radius, timeSec, cfg);
    }
}

void drawClockworkMedallion(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool hovered, bool active,
    const char* glyph,
    [[maybe_unused]] float timeSec
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

    // 3. Sleek elegant circular brass bezel (clean concentric rims replacing cogwheel)
    r.drawCircle(
        cx, cy, radius,
        pal.brassDarkR * 0.7f, pal.brassDarkG * 0.7f, pal.brassDarkB * 0.7f, alpha * 0.95f, 28
    );
    r.drawCircleOutline(
        cx, cy, radius,
        pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, alpha * 0.8f, 1.2f, 28
    );
    r.drawCircleOutline(
        cx, cy, radius * 0.92f,
        hovered ? pal.brassBrightR : pal.brassMidR,
        hovered ? pal.brassBrightG : pal.brassMidG,
        hovered ? pal.brassBrightB : pal.brassMidB,
        alpha, 1.6f, 28
    );
    r.drawCircleOutline(
        cx, cy, radius * 0.82f,
        pal.brassDarkR, pal.brassDarkG, pal.brassDarkB, alpha * 0.6f, 1.0f, 28
    );

    // Subtle cardinal brass rivet accents
    for (int i = 0; i < 4; ++i) {
        float angle = static_cast<float>(i) * (kTwoPi / 4.0f);
        float rx = cx + std::cos(angle) * (radius * 0.87f);
        float ry = cy + std::sin(angle) * (radius * 0.87f);
        drawRivet(r, rx, ry, 1.2f, false,
                  hovered ? pal.brassHighlightR : pal.brassMidR,
                  hovered ? pal.brassHighlightG : pal.brassMidG,
                  hovered ? pal.brassHighlightB : pal.brassMidB,
                  alpha * 0.85f);
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
    const bool isPlay = (std::string_view(glyph) == "▶" ||
                         std::string_view(glyph) == "\xE2\x96\xB6" ||
                         std::string_view(glyph) == "PLAY");
    const bool isAutoPlay = (std::string_view(glyph) == "AUTO" ||
                             std::string_view(glyph) == ">>" ||
                             std::string_view(glyph) == "▶▶");
    const bool isRestart = (std::string_view(glyph) == "↺" ||
                            std::string_view(glyph) == "\xE2\x86\xBA" ||
                            (glyph[0] == 'R' && glyph[1] == '\0'));
    const bool isResize = (std::string_view(glyph) == "⤢" ||
                           std::string_view(glyph) == "\xE2\xA4\xA2" ||
                           std::string_view(glyph) == "SIZE");
    const bool isSeed = (glyph[0] == '#' && glyph[1] == '\0');
    const bool isGameLibrary = (glyph[0] == 'G' && glyph[1] == '\0');
    const bool isUndo = (glyph[0] == '<' && glyph[1] == '\0');
    const bool isRedo = (glyph[0] == '>' && glyph[1] == '\0');
    const bool isHelp = (glyph[0] == '?' && glyph[1] == '\0');
    const bool isHint = (glyph[0] == 'H' && glyph[1] == '\0');

    if (isStar) {
        // Guaranteed Solvable Deal: 8-facet chiseled compass star gem
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawSolvableDealIcon(
            r, cx, cy, faceRadius * 0.64f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isPlay) {
        // New Game / Play: 4-facet chiseled low-poly prism
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawNewGameIcon(
            r, cx, cy, faceRadius * 0.56f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isSeed) {
        // Deal Seed: faceted hexagonal seed crystal
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawSeedIcon(
            r, cx, cy, faceRadius * 0.60f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isGameLibrary) {
        // Game Library: 3 fanned low-poly cards with diamond pip
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawGameLibraryIcon(
            r, cx, cy, faceRadius * 0.62f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isAutoPlay) {
        // Auto-Play: twin faceted fast-forward prisms
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawAutoPlayIcon(
            r, cx, cy, faceRadius * 0.58f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isUndo) {
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawUndoIcon(
            r, cx, cy, faceRadius * 0.58f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isRedo) {
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawRedoIcon(
            r, cx, cy, faceRadius * 0.58f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isHelp) {
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawHelpIcon(
            r, cx, cy, faceRadius * 0.58f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isHint) {
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawHintIcon(
            r, cx, cy, faceRadius * 0.58f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
    } else if (isResize) {
        // Diagonal scale / resize arrows pointing outward top-right and bottom-left
        const float arm = faceRadius * 0.44f;
        const float head = faceRadius * 0.22f;
        const float x0 = cx - arm;
        const float y0 = cy + arm;
        const float x1 = cx + arm;
        const float y1 = cy - arm;

        // Shadow pass
        r.drawLine(x0 + 1.2f, y0 + 1.2f, x1 + 1.2f, y1 + 1.2f, 0.04f, 0.03f, 0.02f, alpha * 0.90f, 2.4f);
        r.drawLine(x1 + 1.2f, y1 + 1.2f, x1 - head + 1.2f, y1 + 1.2f, 0.04f, 0.03f, 0.02f, alpha * 0.90f, 2.4f);
        r.drawLine(x1 + 1.2f, y1 + 1.2f, x1 + 1.2f, y1 + head + 1.2f, 0.04f, 0.03f, 0.02f, alpha * 0.90f, 2.4f);
        r.drawLine(x0 + 1.2f, y0 + 1.2f, x0 + head + 1.2f, y0 + 1.2f, 0.04f, 0.03f, 0.02f, alpha * 0.90f, 2.4f);
        r.drawLine(x0 + 1.2f, y0 + 1.2f, x0 + 1.2f, y0 - head + 1.2f, 0.04f, 0.03f, 0.02f, alpha * 0.90f, 2.4f);

        // Core polished brass stroke
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;

        r.drawLine(x0, y0, x1, y1, brR, brG, brB, alpha, 2.2f);
        r.drawLine(x1, y1, x1 - head, y1, brR, brG, brB, alpha, 2.2f);
        r.drawLine(x1, y1, x1, y1 + head, brR, brG, brB, alpha, 2.2f);
        r.drawLine(x0, y0, x0 + head, y0, brR, brG, brB, alpha, 2.2f);
        r.drawLine(x0, y0, x0, y0 - head, brR, brG, brB, alpha, 2.2f);
    } else if (isRestart) {
        // Restart Game: chunky faceted low-poly rewind ring + chiseled arrowhead
        const float brR = hovered ? pal.brassHighlightR : pal.brassBrightR;
        const float brG = hovered ? pal.brassHighlightG : pal.brassBrightG;
        const float brB = hovered ? pal.brassHighlightB : pal.brassBrightB;
        vector_icon::drawRestartIcon(
            r, cx, cy, faceRadius * 0.60f,
            brR, brG, brB,
            pal.brassHighlightR, pal.brassHighlightG, pal.brassHighlightB,
            alpha, true
        );
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

namespace {

enum class SolLogoMat : uint8_t {
    Gunmetal = 0,
    Hearth   = 1,
    Steel    = 2,
    GoldS    = 3
};

struct SolLogoTri {
    float x0, y0, x1, y1, x2, y2;
    float bias;       // facet bias for Gunmetal/Steel, or base ramp position for GoldS
    float nx, ny;     // precomputed outward bevel direction for GoldS (unused for radial facets)
    SolLogoMat mat;
};

struct SolRgb { float r, g, b; };

static constexpr SolRgb kGunmetal[5] = {
    {36.0f/255.0f, 36.0f/255.0f, 38.0f/255.0f},
    {62.0f/255.0f, 62.0f/255.0f, 65.0f/255.0f},
    {96.0f/255.0f, 96.0f/255.0f, 100.0f/255.0f},
    {140.0f/255.0f, 140.0f/255.0f, 145.0f/255.0f},
    {190.0f/255.0f, 190.0f/255.0f, 196.0f/255.0f}
};
static constexpr SolRgb kSteel[5] = {
    {56.0f/255.0f, 56.0f/255.0f, 58.0f/255.0f},
    {92.0f/255.0f, 92.0f/255.0f, 96.0f/255.0f},
    {138.0f/255.0f, 138.0f/255.0f, 142.0f/255.0f},
    {184.0f/255.0f, 184.0f/255.0f, 188.0f/255.0f},
    {224.0f/255.0f, 224.0f/255.0f, 228.0f/255.0f}
};
static constexpr SolRgb kGoldS[5] = {
    {164.0f/255.0f, 62.0f/255.0f, 10.0f/255.0f},
    {218.0f/255.0f, 104.0f/255.0f, 16.0f/255.0f},
    {242.0f/255.0f, 152.0f/255.0f, 24.0f/255.0f},
    {248.0f/255.0f, 204.0f/255.0f, 68.0f/255.0f},
    {255.0f/255.0f, 234.0f/255.0f, 154.0f/255.0f}
};

constexpr float kDefaultLogoLightX = -0.62f;
constexpr float kDefaultLogoLightY = -0.78f;

inline SolRgb sampleRamp(const SolRgb* ramp, int count, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    float scaled = t * static_cast<float>(count - 1);
    int idx = std::min(count - 2, static_cast<int>(scaled));
    float f = scaled - static_cast<float>(idx);
    return {
        ramp[idx].r + (ramp[idx + 1].r - ramp[idx].r) * f,
        ramp[idx].g + (ramp[idx + 1].g - ramp[idx].g) * f,
        ramp[idx].b + (ramp[idx + 1].b - ramp[idx].b) * f
    };
}

inline SolRgb facetLight(float mx, float my, const SolRgb* ramp, int count, float bias, float lx, float ly) noexcept {
    float len = std::hypot(mx, my);
    float nx = (len > 1e-6f) ? (mx / len) : 0.0f;
    float ny = (len > 1e-6f) ? (my / len) : -1.0f;
    float d = nx * lx + ny * ly;
    return sampleRamp(ramp, count, 0.50f + 0.44f * d + bias);
}

inline SolRgb shadeSolLogoTri(
    const SolLogoTri& tri,
    float x0, float y0, float x1, float y1, float x2, float y2,
    float lx, float ly,
    float brassTint,
    bool highlightWhite
) noexcept {
    switch (tri.mat) {
        case SolLogoMat::Gunmetal: {
            float mx = (x0 + x1 + x2) / 3.0f;
            float my = (y0 + y1 + y2) / 3.0f;
            return facetLight(mx, my, kGunmetal, 5, tri.bias, lx, ly);
        }
        case SolLogoMat::Hearth:
            return {20.0f/255.0f, 19.0f/255.0f, 18.0f/255.0f};
        case SolLogoMat::Steel: {
            float mx = (x0 + x1 + x2) / 3.0f;
            float my = (y0 + y1 + y2) / 3.0f;
            SolRgb c = facetLight(mx, my, kSteel, 5, tri.bias, lx, ly);
            if (highlightWhite) {
                c.r = std::min(1.0f, c.r * 1.15f);
                c.g = std::min(1.0f, c.g * 1.15f);
                c.b = std::min(1.0f, c.b * 1.15f);
            }
            return c;
        }
        case SolLogoMat::GoldS: {
            // Blend classic top-down metallic warmth with directional bevel response to (lx, ly)
            float mx = (x0 + x1 + x2) / 3.0f;
            float my = (y0 + y1 + y2) / 3.0f;
            float bevelDot = tri.nx * lx + tri.ny * ly;
            float posDot = mx * lx + my * ly;
            float t = std::clamp(tri.bias + 0.16f * bevelDot + 0.10f * posDot, 0.05f, 1.0f);
            SolRgb c = sampleRamp(kGoldS, 5, t);
            c.r = std::min(1.0f, c.r * brassTint);
            c.g = std::min(1.0f, c.g * brassTint);
            c.b = std::min(1.0f, c.b * brassTint);
            return c;
        }
    }
    return {1.0f, 1.0f, 1.0f};
}

// Shared normalized [-1, 1] low-poly Sol-Gear + core-font 'S' geometry
int buildSolLogoTriangles(SolLogoTri* out, int maxOut, float rotRad) noexcept {
    int n = 0;
    auto pushTri = [&](float x0, float y0, float x1, float y1, float x2, float y2,
                       SolLogoMat mat, float bias = 0.0f, float nx = 0.0f, float ny = 0.0f) {
        if (n < maxOut) {
            out[n++] = {x0, y0, x1, y1, x2, y2, bias, nx, ny, mat};
        }
    };
    auto pol = [](float r, float a, float& ox, float& oy) {
        ox = r * std::cos(a);
        oy = r * std::sin(a);
    };

    // 1. Outer 8-tooth chamfered gunmetal gear (normalized to R_TIP = 1.0)
    constexpr float kInvTip = 1.0f / 240.0f;
    constexpr float kRBore = 164.0f * kInvTip;
    constexpr float kRRoot = 196.0f * kInvTip;
    constexpr float kRMid  = 218.0f * kInvTip;
    constexpr float kRTip  = 1.0f;
    constexpr float kStep  = kTwoPi / 8.0f;
    constexpr float kHalfBase = 0.235f;
    constexpr float kHalfTip  = 0.135f;

    for (int i = 0; i < 8; ++i) {
        float ac = -kPi * 0.5f + static_cast<float>(i) * kStep + rotRad;
        float an = ac + kStep;
        float av = ac + kStep * 0.5f;

        float bcx, bcy, brx, bry, bvx, bvy, bnlx, bnly, bnx, bny;
        float rlx, rly, rcx, rcy, rrx, rry, rvx, rvy, rnlx, rnly, rnx, rny;
        pol(kRBore, ac, bcx, bcy);
        pol(kRBore, ac + kHalfBase, brx, bry);
        pol(kRBore, av, bvx, bvy);
        pol(kRBore, an - kHalfBase, bnlx, bnly);
        pol(kRBore, an, bnx, bny);

        pol(kRRoot, ac - kHalfBase, rlx, rly);
        pol(kRRoot, ac, rcx, rcy);
        pol(kRRoot, ac + kHalfBase, rrx, rry);
        pol(kRRoot, av, rvx, rvy);
        pol(kRRoot, an - kHalfBase, rnlx, rnly);
        pol(kRRoot, an, rnx, rny);

        auto emitG = [&](float x0, float y0, float x1, float y1, float x2, float y2, float bias) {
            pushTri(x0, y0, x1, y1, x2, y2, SolLogoMat::Gunmetal, bias);
        };
        emitG(bcx, bcy, rcx, rcy, rrx, rry, 0.04f);
        emitG(bcx, bcy, rrx, rry, brx, bry, -0.04f);
        emitG(brx, bry, rrx, rry, rvx, rvy, 0.06f);
        emitG(brx, bry, rvx, rvy, bvx, bvy, -0.06f);
        emitG(bvx, bvy, rvx, rvy, rnlx, rnly, 0.05f);
        emitG(bvx, bvy, rnlx, rnly, bnlx, bnly, -0.05f);
        emitG(bnlx, bnly, rnlx, rnly, rnx, rny, 0.04f);
        emitG(bnlx, bnly, rnx, rny, bnx, bny, -0.04f);

        float tlx, tly, trx, try_, tax, tay;
        pol(kRTip, ac - kHalfTip, tlx, tly);
        pol(kRTip, ac + kHalfTip, trx, try_);
        pol(kRMid, ac, tax, tay);

        emitG(rlx, rly, tlx, tly, tax, tay, 0.16f);
        emitG(rlx, rly, tax, tay, rcx, rcy, 0.06f);
        emitG(tlx, tly, trx, try_, tax, tay, 0.22f);
        emitG(rcx, rcy, tax, tay, rrx, rry, -0.10f);
        emitG(tax, tay, trx, try_, rrx, rry, -0.18f);
    }

    // 2. Inner octagonal dark hearth recess
    constexpr float kRecess = 180.0f * kInvTip;
    for (int i = 0; i < 8; ++i) {
        float a0 = -kPi / 8.0f + static_cast<float>(i) * (kPi * 0.25f);
        float a1 = a0 + kPi * 0.25f;
        float x0, y0, x1, y1;
        pol(kRecess, a0, x0, y0);
        pol(kRecess, a1, x1, y1);
        pushTri(0.0f, 0.0f, x0, y0, x1, y1, SolLogoMat::Hearth);
    }

    // 3. Thin 16px octagonal faceted steel ring
    constexpr float kRRin  = 134.0f * kInvTip;
    constexpr float kRRMid = 142.0f * kInvTip;
    constexpr float kRRout = 150.0f * kInvTip;
    for (int i = 0; i < 8; ++i) {
        float a0 = -kPi / 8.0f + static_cast<float>(i) * (kPi * 0.25f);
        float a1 = a0 + kPi * 0.25f;
        float am = a0 + kPi / 8.0f;
        float pi0x, pi0y, pi1x, pi1y, po0x, po0y, po1x, po1y, pmx, pmy;
        pol(kRRin, a0, pi0x, pi0y);
        pol(kRRin, a1, pi1x, pi1y);
        pol(kRRout, a0, po0x, po0y);
        pol(kRRout, a1, po1x, po1y);
        pol(kRRMid, am, pmx, pmy);

        auto emitS = [&](float x0, float y0, float x1, float y1, float x2, float y2, float bias) {
            pushTri(x0, y0, x1, y1, x2, y2, SolLogoMat::Steel, bias);
        };
        emitS(pi0x, pi0y, po0x, po0y, pmx, pmy, 0.10f);
        emitS(po0x, po0y, po1x, po1y, pmx, pmy, 0.16f);
        emitS(pmx, pmy, po1x, po1y, pi1x, pi1y, -0.06f);
        emitS(pi0x, pi0y, pmx, pmy, pi1x, pi1y, -0.12f);
    }

    // 4. Four cardinal compass-sunburst spikes
    const float rInnerFlat = kRRin * std::cos(kPi / 8.0f);
    constexpr float kTipDist = 112.0f * kInvTip;
    constexpr float kHalfSpikeW = 9.0f * kInvTip;
    for (int i = 0; i < 4; ++i) {
        float a = -kPi * 0.5f + static_cast<float>(i) * (kPi * 0.5f);
        float ux = std::cos(a), uy = std::sin(a);
        float vx = -uy, vy = ux;
        float bd = rInnerFlat - (1.0f * kInvTip);
        float tx = ux * kTipDist, ty = uy * kTipDist;
        float mx = ux * bd, my = uy * bd;
        float lx = mx - vx * kHalfSpikeW, ly = my - vy * kHalfSpikeW;
        float rx = mx + vx * kHalfSpikeW, ry = my + vy * kHalfSpikeW;
        pushTri(tx, ty, mx, my, lx, ly, SolLogoMat::Steel, 0.22f);
        pushTri(tx, ty, rx, ry, mx, my, SolLogoMat::Steel, -0.18f);
    }

    // 5. Core-font 'S' overlay (exact centerline from vector_font.cpp / ebf_font.cpp)
    constexpr float kFontScale = 17.6f * kInvTip;
    constexpr float kHw = 18.5f * kInvTip;
    constexpr float kSpinePts[8][2] = {
        {7.5f, 2.5f}, {3.5f, 2.5f}, {2.0f, 4.0f}, {2.0f, 6.5f},
        {8.0f, 9.5f}, {8.0f, 12.0f}, {6.5f, 13.5f}, {2.5f, 13.5f}
    };
    float sx[8], sy[8], lrx[8], lry[8], rrx[8], rry[8];
    for (int k = 0; k < 8; ++k) {
        sx[k] = (kSpinePts[k][0] - 5.0f) * kFontScale;
        sy[k] = (kSpinePts[k][1] - 8.0f) * kFontScale;
    }
    auto segNorm = [&](int a, int b, float& nx, float& ny) {
        float dx = sx[b] - sx[a], dy = sy[b] - sy[a];
        float len = std::max(1e-6f, std::hypot(dx, dy));
        nx = -dy / len; ny = dx / len;
    };
    for (int k = 0; k < 8; ++k) {
        if (k == 0) {
            float nx, ny; segNorm(0, 1, nx, ny);
            float dx = sx[0] - sx[1], dy = sy[0] - sy[1];
            float len = std::max(1e-6f, std::hypot(dx, dy));
            float tx = dx / len, ty = dy / len;
            lrx[0] = sx[0] + (nx + tx * 0.65f) * kHw; lry[0] = sy[0] + (ny + ty * 0.65f) * kHw;
            rrx[0] = sx[0] - (nx + tx * 0.65f) * kHw; rry[0] = sy[0] - (ny + ty * 0.65f) * kHw;
        } else if (k == 7) {
            float nx, ny; segNorm(6, 7, nx, ny);
            float dx = sx[7] - sx[6], dy = sy[7] - sy[6];
            float len = std::max(1e-6f, std::hypot(dx, dy));
            float tx = dx / len, ty = dy / len;
            lrx[7] = sx[7] + (nx + tx * 0.65f) * kHw; lry[7] = sy[7] + (ny + ty * 0.65f) * kHw;
            rrx[7] = sx[7] - (nx + tx * 0.65f) * kHw; rry[7] = sy[7] - (ny + ty * 0.65f) * kHw;
        } else {
            float n0x, n0y, n1x, n1y;
            segNorm(k - 1, k, n0x, n0y);
            segNorm(k, k + 1, n1x, n1y);
            float mx = n0x + n1x, my = n0y + n1y;
            float mlen = std::max(1e-6f, std::hypot(mx, my));
            mx /= mlen; my /= mlen;
            float dot = std::max(0.35f, mx * n0x + my * n0y);
            float mDist = kHw / dot;
            lrx[k] = sx[k] + mx * mDist; lry[k] = sy[k] + my * mDist;
            rrx[k] = sx[k] - mx * mDist; rry[k] = sy[k] - my * mDist;
        }
    }
    for (int k = 0; k < 7; ++k) {
        float tPos = 1.0f - (static_cast<float>(k) / 6.0f) * 0.68f;
        float tL = std::min(1.0f, tPos + 0.12f);
        float tR = std::max(0.06f, tPos - 0.24f);
        float snx, sny;
        segNorm(k, k + 1, snx, sny);
        // Left bevel faces (+snx, +sny); right bevel faces (-snx, -sny)
        pushTri(lrx[k], lry[k], lrx[k + 1], lry[k + 1], sx[k + 1], sy[k + 1],
                SolLogoMat::GoldS, tL, snx, sny);
        pushTri(lrx[k], lry[k], sx[k + 1], sy[k + 1], sx[k], sy[k],
                SolLogoMat::GoldS, std::min(1.0f, tL - 0.06f), snx, sny);
        pushTri(sx[k], sy[k], sx[k + 1], sy[k + 1], rrx[k + 1], rry[k + 1],
                SolLogoMat::GoldS, tR, -snx, -sny);
        pushTri(sx[k], sy[k], rrx[k + 1], rry[k + 1], rrx[k], rry[k],
                SolLogoMat::GoldS, std::max(0.05f, tR - 0.08f), -snx, -sny);
    }
    return n;
}

} // anonymous namespace

void drawClockworkLogo(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    float timeSec,
    const ClockworkLogoConfig& config
) {
    if (radius <= 2.0f) return;

    const float alpha = std::clamp(config.alpha, 0.0f, 1.0f);
    if (alpha <= 0.001f) return;

    const float rot = config.animated ? timeSec * 0.45f : 0.0f;
    const float bTint = std::clamp(config.brassTint, 0.5f, 2.0f);

    // Cache the static unrotated unit mesh once (zero per-frame trig/allocation overhead)
    constexpr int kOuterGearTris = 8 * 13; // 8 teeth * (8 ring + 5 tooth) triangles
    struct CachedSolMesh {
        std::array<SolLogoTri, 192> tris{};
        int count{0};
    };
    static const CachedSolMesh kStaticMesh = []() {
        CachedSolMesh m;
        m.count = buildSolLogoTriangles(m.tris.data(), static_cast<int>(m.tris.size()), 0.0f);
        return m;
    }();

    // Resolve directional light vector and subtle color/intensity tint from the active game board light
    float lx = kDefaultLogoLightX;
    float ly = kDefaultLogoLightY;
    float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
    if (config.useSceneLight && r.hasGlobalLight()) {
        const auto& light = r.getGlobalLight();
        const float dx = light.x - cx;
        const float dy = light.y - cy;
        const float dz = std::max(80.0f, light.z);
        const float len3 = std::hypot(std::hypot(dx, dy), dz);
        if (len3 > 1e-4f) {
            lx = (dx / len3) * 1.35f;
            ly = (dy / len3) * 1.35f;
            const float l2d = std::hypot(lx, ly);
            if (l2d > 1.0f) {
                lx /= l2d;
                ly /= l2d;
            }
        }
        const float strength = std::clamp(r.getGlobalLightStrength(), 0.0f, 1.5f);
        const float colBlend = std::clamp(0.32f * strength, 0.0f, 0.45f);
        const float intenMod = std::clamp(0.78f + 0.22f * light.intensity, 0.58f, 1.22f);
        tintR = (1.0f + (r.getGlobalLightColorR() - 1.0f) * colBlend) * intenMod;
        tintG = (1.0f + (r.getGlobalLightColorG() - 1.0f) * colBlend) * intenMod;
        tintB = (1.0f + (r.getGlobalLightColorB() - 1.0f) * colBlend) * intenMod;
    }

    const float cosR = (rot != 0.0f) ? std::cos(rot) : 1.0f;
    const float sinR = (rot != 0.0f) ? std::sin(rot) : 0.0f;
    auto xform = [&](int idx, float& ox0, float& oy0, float& ox1, float& oy1, float& ox2, float& oy2) {
        const auto& t = kStaticMesh.tris[idx];
        if (rot != 0.0f && idx < kOuterGearTris) {
            ox0 = t.x0 * cosR - t.y0 * sinR; oy0 = t.x0 * sinR + t.y0 * cosR;
            ox1 = t.x1 * cosR - t.y1 * sinR; oy1 = t.x1 * sinR + t.y1 * cosR;
            ox2 = t.x2 * cosR - t.y2 * sinR; oy2 = t.x2 * sinR + t.y2 * cosR;
        } else {
            ox0 = t.x0; oy0 = t.y0;
            ox1 = t.x1; oy1 = t.y1;
            ox2 = t.x2; oy2 = t.y2;
        }
    };

    if (radius > 12.0f) {
        const float shadowScale = std::max(2.2f, radius * 0.055f);
        const float offX = -lx * shadowScale;
        const float offY = -ly * shadowScale;
        for (int i = 0; i < kStaticMesh.count; ++i) {
            float x0, y0, x1, y1, x2, y2;
            xform(i, x0, y0, x1, y1, x2, y2);
            r.drawTriangle(
                cx + x0 * radius + offX, cy + y0 * radius + offY,
                cx + x1 * radius + offX, cy + y1 * radius + offY,
                cx + x2 * radius + offX, cy + y2 * radius + offY,
                0.0f, 0.0f, 0.0f, 0.28f * alpha
            );
        }
    }

    for (int i = 0; i < kStaticMesh.count; ++i) {
        const auto& t = kStaticMesh.tris[i];
        float x0, y0, x1, y1, x2, y2;
        xform(i, x0, y0, x1, y1, x2, y2);
        // Shade using post-rotation coordinates so turning gear teeth catch the live board light
        SolRgb c = shadeSolLogoTri(t, x0, y0, x1, y1, x2, y2, lx, ly, bTint, config.highlightWhite);
        float cr = std::min(1.0f, c.r * tintR);
        float cg = std::min(1.0f, c.g * tintG);
        float cb = std::min(1.0f, c.b * tintB);
        r.drawTriangle(
            cx + x0 * radius, cy + y0 * radius,
            cx + x1 * radius, cy + y1 * radius,
            cx + x2 * radius, cy + y2 * radius,
            cr, cg, cb, alpha
        );
    }
}

void bakeClockworkLogoRgba(
    uint8_t* outRgba,
    int width, int height,
    int strideBytes,
    float centerX, float centerY, float radius,
    float alpha
) {
    if (!outRgba || width <= 0 || height <= 0 || radius <= 2.0f || alpha <= 0.001f) return;

    std::array<SolLogoTri, 192> tris;
    const int count = buildSolLogoTriangles(tris.data(), static_cast<int>(tris.size()), 0.0f);

    auto edgeFn = [](float ax, float ay, float bx, float by, float px, float py) noexcept {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };

    for (int i = 0; i < count; ++i) {
        const auto& t = tris[i];
        const SolRgb c = shadeSolLogoTri(t, t.x0, t.y0, t.x1, t.y1, t.x2, t.y2,
                                         kDefaultLogoLightX, kDefaultLogoLightY, 1.0f, false);
        float x0 = centerX + t.x0 * radius, y0 = centerY + t.y0 * radius;
        float x1 = centerX + t.x1 * radius, y1 = centerY + t.y1 * radius;
        float x2 = centerX + t.x2 * radius, y2 = centerY + t.y2 * radius;
        float area = edgeFn(x0, y0, x1, y1, x2, y2);
        if (std::abs(area) < 1e-4f) continue;
        float sign = (area > 0.0f) ? 1.0f : -1.0f;

        int minX = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
        int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
        int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));

        for (int y = minY; y <= maxY; ++y) {
            float py = static_cast<float>(y) + 0.5f;
            for (int x = minX; x <= maxX; ++x) {
                float px = static_cast<float>(x) + 0.5f;
                if (edgeFn(x0, y0, x1, y1, px, py) * sign >= 0.0f &&
                    edgeFn(x1, y1, x2, y2, px, py) * sign >= 0.0f &&
                    edgeFn(x2, y2, x0, y0, px, py) * sign >= 0.0f) {
                    size_t pixelOffset = static_cast<size_t>(y * strideBytes + x * 4);
                    float invA = 1.0f - alpha;
                    outRgba[pixelOffset + 0] = static_cast<uint8_t>(std::clamp(c.r * 255.0f * alpha + outRgba[pixelOffset + 0] * invA, 0.0f, 255.0f));
                    outRgba[pixelOffset + 1] = static_cast<uint8_t>(std::clamp(c.g * 255.0f * alpha + outRgba[pixelOffset + 1] * invA, 0.0f, 255.0f));
                    outRgba[pixelOffset + 2] = static_cast<uint8_t>(std::clamp(c.b * 255.0f * alpha + outRgba[pixelOffset + 2] * invA, 0.0f, 255.0f));
                    outRgba[pixelOffset + 3] = static_cast<uint8_t>(std::clamp(255.0f * alpha + outRgba[pixelOffset + 3] * invA, 0.0f, 255.0f));
                }
            }
        }
    }
}

} // namespace solengine::clockwork_ui

