#pragma once

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "solengine/solitaire_types.hpp"
#include <string>

namespace solengine::clockwork_ui {

/**
 * Visual color palette defining the SolEngine ClockworkEngine aesthetic.
 * Combines weathered cast iron, boiler-plate steel, hammered copper tubing,
 * polished Viktoria brass, and glowing furnace amber.
 */
struct ClockworkPalette {
    // 1. Cast Iron / Boilerplate Chassis
    float chassisDarkR{0.13f}, chassisDarkG{0.12f}, chassisDarkB{0.11f};
    float chassisLightR{0.20f}, chassisLightG{0.18f}, chassisLightB{0.16f};
    float panelBedR{0.15f}, panelBedG{0.14f}, panelBedB{0.12f};
    float seamShadowR{0.07f}, seamShadowG{0.06f}, seamShadowB{0.05f};

    // 2. Polished Viktoria Brass & Horological Gears
    float brassBrightR{0.94f}, brassBrightG{0.80f}, brassBrightB{0.36f};
    float brassMidR{0.78f}, brassMidG{0.62f}, brassMidB{0.22f};
    float brassDarkR{0.46f}, brassDarkG{0.34f}, brassDarkB{0.12f};
    float brassHighlightR{1.00f}, brassHighlightG{0.92f}, brassHighlightB{0.64f};

    // 3. Hammered Copper & Steam Conduit Piping
    float copperBrightR{0.88f}, copperBrightG{0.54f}, copperBrightB{0.34f};
    float copperMidR{0.72f}, copperMidG{0.40f}, copperMidB{0.22f};
    float copperDarkR{0.42f}, copperDarkG{0.22f}, copperDarkB{0.12f};
    float copperSpineR{0.96f}, copperSpineG{0.72f}, copperSpineB{0.52f};

    // 4. Molten Furnace Amber & Ember Indicators
    float furnaceAmberR{0.95f}, furnaceAmberG{0.46f}, furnaceAmberB{0.08f};
    float furnaceCoreR{1.00f}, furnaceCoreG{0.70f}, furnaceCoreB{0.22f};
    float furnaceDarkR{0.55f}, furnaceDarkG{0.16f}, furnaceDarkB{0.04f};

    // 5. Dial Parchment & Gauge Indicators
    float dialParchmentR{0.91f}, dialParchmentG{0.88f}, dialParchmentB{0.80f};
    float dialTextR{0.24f}, dialTextG{0.21f}, dialTextB{0.18f};
    float needleRedR{0.86f}, needleRedG{0.18f}, needleRedB{0.14f};
};

const ClockworkPalette& palette() noexcept;

/**
 * Procedural Vector Drawing Primitives for the ClockworkEngine GUI
 */

void drawGear(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy,
    float outerRadius, float rootRadius, float innerRadius,
    int teeth, float angleRad = 0.0f,
    float bodyR = 0.78f, float bodyG = 0.62f, float bodyB = 0.22f, float bodyA = 1.0f,
    float rimR = 0.94f, float rimG = 0.80f, float rimB = 0.36f, float rimA = 1.0f,
    int spokeCount = 5, float hubRadius = -1.0f,
    bool drawShadow = false
);

void drawRivet(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius = 2.5f,
    bool isHexBolt = false,
    float brassR = 0.84f, float brassG = 0.68f, float brassB = 0.28f, float alpha = 1.0f
);

void drawRivetedBorder(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    float spacing = 20.0f, float rivetRadius = 2.5f,
    bool isHexBolt = false
);

void drawPipe(
    eatsbits::ui::BatchRenderer2D& r,
    float x0, float y0, float x1, float y1,
    float width = 8.0f,
    bool startFlange = true, bool endFlange = true
);

void drawPipeCorner(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    int cornerQuadrant, // 0 = Top-Right, 1 = Bottom-Right, 2 = Bottom-Left, 3 = Top-Left
    float width = 8.0f
);

void drawManometerGauge(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    float needleAngleRad,
    float valueRatio = 0.5f,
    bool hasCrossMark = false,
    const char* label = nullptr
);

void drawChassisFrame(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    bool drawCornerGears = true,
    bool drawSideGauges = false,
    float timeSec = 0.0f
);

void drawPewterButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    const char* text, bool hovered, bool active,
    float fontSize = 11.0f
);

void drawFurnaceButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    const char* text, bool hovered, bool active,
    float fontSize = 11.5f
);

void drawBrassButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    const char* text, bool hovered, bool highlighted,
    float fontSize = 10.5f
);

void drawStepperBar(
    eatsbits::ui::BatchRenderer2D& r,
    float rowX, float curY, float rowW, float rowH,
    const std::string& text,
    bool prevHov, bool nextHov, bool centerHov
);

void drawGlassSlider(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    float valueRatio,
    const char* labelText,
    bool isKnobHovered,
    float timeSec = 0.0f
);

void drawHudClockworkChassis(
    eatsbits::ui::BatchRenderer2D& r,
    float popX, float popY, float popW, float popH,
    float moveCountOrTime = 0.0f,
    float timeSec = 0.0f
);

void drawClockworkMenuOrb(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool hovered, bool menuOpen,
    float timeSec = 0.0f
);

void drawClockworkMedallion(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    bool hovered, bool active,
    const char* glyph,
    float timeSec = 0.0f
);

void drawClockworkConduitBar(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    bool isVertical,
    float timeSec = 0.0f
);

void drawClockworkTooltip(
    eatsbits::ui::BatchRenderer2D& r,
    float targetX, float targetY, float targetW, float targetH,
    const char* text,
    int placement // 0 = above, 1 = below, 2 = left, 3 = right
);

void drawClockworkCloseButton(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float size,
    bool hovered,
    float timeSec = 0.0f
);

} // namespace solengine::clockwork_ui

