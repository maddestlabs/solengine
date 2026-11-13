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
    float brassR = 0.66f, float brassG = 0.66f, float brassB = 0.66f, float alpha = 1.0f
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

/**
 * Configuration options for the SolEngine Clockwork Logo procedural renderer.
 */
struct ClockworkLogoConfig {
    bool animated{true};           // Enables subtle horological cog rotation and furnace pulsing
    bool showGauges{true};          // Mini manometer pressure gauge on collar and serif
    bool showPipes{true};           // Copper conduit piping and brackets
    bool showGlow{true};            // Warm amber furnace core radiance
    bool highlightWhite{false};     // Crisp white interior highlight on hover
    float alpha{1.0f};              // Master opacity multiplier [0, 1]
    float brassTint{1.0f};          // Brightness multiplier for brass plates
};

/**
 * Procedural Vector API for rendering the SolEngine Clockwork Logo.
 * Renders the layered horological emblem: outer weathered cast-iron gear rim,
 * riveted Viktoria brass collar with radial brackets, recessed dark boiler well
 * with glowing amber furnace embers, interlocking secondary cogs, manometer gauge,
 * and the iconic Victorian riveted brass 'S' insignia.
 *
 * @param r Batch renderer reference
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param radius Outer bounding radius of the emblem
 * @param timeSec Wall-clock time in seconds for idle animations
 * @param config Rendering configuration options
 */
void drawClockworkLogo(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float radius,
    float timeSec = 0.0f,
    const ClockworkLogoConfig& config = {}
);

/**
 * Software CPU rasterizer for baking the SolEngine Clockwork Logo into raw RGBA32 buffers.
 * Primarily designed for baking zero-runtime-cost card backs in CardTextureCache.
 *
 * @param outRgba Destination buffer formatted as packed RGBA8
 * @param width Target canvas width
 * @param height Target canvas height
 * @param strideBytes Row stride in bytes (typically width * 4)
 * @param centerX Center X coordinate in pixels
 * @param centerY Center Y coordinate in pixels
 * @param radius Outer radius of the emblem in pixels
 * @param alpha Master opacity multiplier [0, 1]
 */
void bakeClockworkLogoRgba(
    uint8_t* outRgba,
    int width, int height,
    int strideBytes,
    float centerX, float centerY, float radius,
    float alpha = 1.0f
);

} // namespace solengine::clockwork_ui

