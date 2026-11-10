#include "solengine/ui/settings_dialog.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/procedural_background.hpp"
#include "eatsbits/ui/glyph/font_engine.hpp"
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace solengine {

namespace {
const char* cornerStyleCleanName(CardCornerStyle style) noexcept {
    switch (style) {
        case CardCornerStyle::Auto:    return "AUTO (24px)";
        case CardCornerStyle::Sharp:   return "SHARP (0px)";
        case CardCornerStyle::Vintage: return "VINTAGE (8px)";
        case CardCornerStyle::Round:   return "ROUND (24px)";
        default: return "AUTO (24px)";
    }
}
} // namespace

SettingsDialog::SettingsDialog() = default;

void SettingsDialog::randomizeSeed() {
    std::random_device rd;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    uint32_t seed = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(now)) & 0x7FFFFFFFu;
    if (seed == 0) seed = 2026u;
    tempSettings_.lastSeed = seed;
    seedText_ = std::to_string(seed);
}

void SettingsDialog::open() {
    isOpen_ = true;
    tempSettings_ = SettingsManager::instance().settings();
    seedText_ = std::to_string(tempSettings_.lastSeed);
    editingSeed_ = false;
}

void SettingsDialog::close() {
    isOpen_ = false;
    editingSeed_ = false;
    draggingVolume_ = false;
    draggingDeviation_ = false;
}

void SettingsDialog::render(
    eatsbits::ui::BatchRenderer2D& r,
    float screenW,
    float screenH,
    float mouseX,
    float mouseY,
    const ThemePalette& pal,
    float timeSec
) {
    if (!isOpen_) return;

    // 1. Semi-transparent darkened backdrop covering entire window
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 0.0f, 0.60f);

    // 2. Responsive dialog chassis dimensions (centered modal on desktop; full-width pull-down drawer on mobile)
    const bool isMobile = (screenW < 640.0f) || (screenW < screenH);
    const bool isClockwork = (tempSettings_.guiTheme == GuiThemeStyle::ClockworkEngine);
    const auto& cpal = clockwork_ui::palette();

    if (isMobile) {
        dialogX_ = 0.0f;
        dialogY_ = 0.0f;
        dialogW_ = screenW;
        dialogH_ = std::min(screenH * 0.94f, 620.0f);

        if (isClockwork) {
            clockwork_ui::drawChassisFrame(r, dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, true, false, timeSec);
        } else {
            eatsbits::ui::LightSource2D light{screenW * 0.5f, -200.0f, 500.0f};
            r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, 22.0f, light, 0.70f);
            r.drawRoundedRectGradient(
                dialogX_, dialogY_, dialogW_, dialogH_, 12.0f,
                pal.woodR0 + 0.06f, pal.woodG0 + 0.05f, pal.woodB0 + 0.04f,
                pal.woodR1 + 0.02f, pal.woodG1 + 0.02f, pal.woodB1 + 0.02f,
                0.98f, 6
            );
            r.drawRoundedRectOutline(dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, pal.trimR, pal.trimG, pal.trimB, 0.95f, 1.8f, 6);
        }

        // Ergonomic mobile pull-down / swipe-up handle pill at bottom edge
        const float handleW = 46.0f;
        const float handleH = 4.0f;
        const float handleX = (screenW - handleW) * 0.5f;
        const float handleY = dialogH_ - 8.0f;
        r.drawRoundedRect(handleX, handleY, handleW, handleH, 2.0f, isClockwork ? cpal.brassBrightR : pal.trimR, isClockwork ? cpal.brassBrightG : pal.trimG, isClockwork ? cpal.brassBrightB : pal.trimB, 0.70f, 2);
    } else {
        dialogW_ = std::clamp(screenW * 0.72f, 540.0f, 740.0f);
        dialogH_ = std::clamp(screenH * 0.78f, 440.0f, 580.0f);
        dialogX_ = (screenW - dialogW_) * 0.5f;
        dialogY_ = (screenH - dialogH_) * 0.5f;

        if (isClockwork) {
            clockwork_ui::drawChassisFrame(r, dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, true, true, timeSec);
        } else {
            eatsbits::ui::LightSource2D light{screenW * 0.5f, -200.0f, 500.0f};
            r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, 22.0f, light, 0.65f);
            r.drawRoundedRectGradient(
                dialogX_, dialogY_, dialogW_, dialogH_, 10.0f,
                pal.woodR0 + 0.06f, pal.woodG0 + 0.05f, pal.woodB0 + 0.04f,
                pal.woodR1 + 0.02f, pal.woodG1 + 0.02f, pal.woodB1 + 0.02f,
                0.98f, 6
            );
            r.drawRoundedRectOutline(dialogX_, dialogY_, dialogW_, dialogH_, 10.0f, pal.trimR, pal.trimG, pal.trimB, 0.95f, 1.8f, 6);
        }
    }

    // 3. Header Bar with SVG Gear Icon and Close Button [X]
    const float headerH = 48.0f;
    if (isClockwork) {
        r.drawRoundedRect(dialogX_, dialogY_, dialogW_, headerH, 10.0f, cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB, 0.90f, 6);
        r.drawLine(dialogX_, dialogY_ + headerH, dialogX_ + dialogW_, dialogY_ + headerH, cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.75f, 1.2f);

        // Spherical brass ornament boss at top-left
        r.drawCircleRadial3StopGradient(
            dialogX_ + 24.0f, dialogY_ + 24.0f, 8.5f,
            cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f,
            cpal.brassBrightR, cpal.brassBrightG, cpal.brassBrightB, 1.0f,
            cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 1.0f,
            -2.5f, -2.5f, 0.45f, 20
        );

        eatsbits::ui::glyph::drawVectorText(
            r, "SOLITAIRE ENGINE PREFERENCES & GAME SETTINGS", dialogX_ + 40.0f, dialogY_ + 17.5f, 13.0f,
            cpal.furnaceCoreR, cpal.furnaceCoreG, cpal.furnaceCoreB, 1.0f
        );
    } else {
        r.drawRoundedRect(dialogX_, dialogY_, dialogW_, headerH, 10.0f, pal.woodR0 + 0.02f, pal.woodG0 + 0.01f, pal.woodB0 + 0.01f, 0.95f, 6);
        r.drawLine(dialogX_, dialogY_ + headerH, dialogX_ + dialogW_, dialogY_ + headerH, pal.trimR, pal.trimG, pal.trimB, 0.60f, 1.2f);

        eatsbits::ui::glyph::drawVectorText(r, "O", dialogX_ + 16.0f, dialogY_ + 17.0f, 14.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        eatsbits::ui::glyph::drawVectorText(
            r, "SOLITAIRE ENGINE PREFERENCES & GAME SETTINGS", dialogX_ + 36.0f, dialogY_ + 17.0f, 14.0f,
            pal.accentR, pal.accentG, pal.accentB, 1.0f
        );
    }

    // Close Button [X] in top-right
    const float closeBtnSize = 28.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 12.0f;
    const float closeBtnY = dialogY_ + (headerH - closeBtnSize) * 0.5f;
    bool closeHovered = (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize && mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize);
    clockwork_ui::drawClockworkCloseButton(r, closeBtnX, closeBtnY, closeBtnSize, closeHovered, timeSec);

    // 4. Tab Navigation Bar (4 tabs)
    const float tabY = dialogY_ + headerH + 10.0f;
    const float tabH = 32.0f;
    const float tabW = (dialogW_ - 32.0f) / 4.0f;
    const char* tabNames[4] = { "GAME", "THEMES", "DISPLAY", "AUDIO" };

    for (int t = 0; t < 4; ++t) {
        float tx = dialogX_ + 16.0f + t * tabW;
        bool isActive = (currentTab_ == t);
        bool isHovered = (mouseX >= tx && mouseX <= tx + tabW - 4.0f && mouseY >= tabY && mouseY <= tabY + tabH);

        if (isClockwork) {
            if (isActive) {
                // Active tab has copper pipe contour looping over it as seen in mockup
                clockwork_ui::drawPipe(r, tx, tabY - 2.0f, tx + tabW - 4.0f, tabY - 2.0f, 4.5f, true, true);
                r.drawRoundedRectGradient(
                    tx, tabY, tabW - 4.0f, tabH, 5.0f,
                    cpal.chassisLightR + 0.14f, cpal.chassisLightG + 0.12f, cpal.chassisLightB + 0.10f,
                    cpal.chassisDarkR + 0.08f, cpal.chassisDarkG + 0.07f, cpal.chassisDarkB + 0.06f,
                    0.98f, 4
                );
                r.drawRoundedRectOutline(
                    tx, tabY, tabW - 4.0f, tabH, 5.0f,
                    cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 1.0f, 1.8f, 4
                );
            } else {
                r.drawRoundedRectGradient(
                    tx, tabY, tabW - 4.0f, tabH, 5.0f,
                    cpal.chassisDarkR + (isHovered ? 0.08f : 0.03f),
                    cpal.chassisDarkG + (isHovered ? 0.07f : 0.02f),
                    cpal.chassisDarkB + (isHovered ? 0.06f : 0.02f),
                    cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB,
                    0.95f, 4
                );
                r.drawRoundedRectOutline(
                    tx, tabY, tabW - 4.0f, tabH, 5.0f,
                    cpal.brassMidR, cpal.brassMidG, cpal.brassMidB,
                    isHovered ? 0.90f : 0.50f, isHovered ? 1.4f : 1.0f, 4
                );
            }

            float tabTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, tabNames[t]);
            eatsbits::ui::glyph::drawVectorText(
                r, tabNames[t], tx + (tabW - 4.0f - tabTxtW) * 0.5f, tabY + 10.0f, 11.0f,
                isActive ? cpal.furnaceCoreR : (isHovered ? cpal.brassHighlightR : cpal.brassBrightR),
                isActive ? cpal.furnaceCoreG : (isHovered ? cpal.brassHighlightG : cpal.brassBrightG),
                isActive ? cpal.furnaceCoreB : (isHovered ? cpal.brassHighlightB : cpal.brassBrightB), 1.0f
            );
        } else {
            r.drawRoundedRectGradient(
                tx, tabY, tabW - 4.0f, tabH, 5.0f,
                isActive ? (pal.woodR0 + 0.14f) : (pal.woodR0 + 0.03f + (isHovered ? 0.05f : 0.0f)),
                isActive ? (pal.woodG0 + 0.12f) : (pal.woodG0 + 0.02f + (isHovered ? 0.04f : 0.0f)),
                isActive ? (pal.woodB0 + 0.10f) : (pal.woodB0 + 0.02f + (isHovered ? 0.03f : 0.0f)),
                pal.woodR1, pal.woodG1, pal.woodB1,
                0.95f, 4
            );
            r.drawRoundedRectOutline(
                tx, tabY, tabW - 4.0f, tabH, 5.0f,
                isActive ? pal.accentR : pal.trimR,
                isActive ? pal.accentG : pal.trimG,
                isActive ? pal.accentB : pal.trimB,
                isActive ? 1.0f : 0.50f, isActive ? 1.6f : 1.0f, 4
            );

            float tabTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, tabNames[t]);
            eatsbits::ui::glyph::drawVectorText(
                r, tabNames[t], tx + (tabW - 4.0f - tabTxtW) * 0.5f, tabY + 10.0f, 11.0f,
                isActive ? pal.accentR : 0.88f,
                isActive ? pal.accentG : 0.85f,
                isActive ? pal.accentB : 0.80f, 1.0f
            );
        }
    }

    // 5. Main Tab Content Area
    const float contentX = dialogX_ + 16.0f;
    const float contentY = tabY + tabH + 12.0f;
    const float contentW = dialogW_ - 32.0f;
    const float contentH = dialogH_ - (headerH + tabH + 74.0f);

    if (isClockwork) {
        r.drawRoundedRect(contentX, contentY, contentW, contentH, 6.0f, cpal.panelBedR, cpal.panelBedG, cpal.panelBedB, 0.94f, 4);
        r.drawRoundedRectOutline(contentX, contentY, contentW, contentH, 6.0f, cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 0.75f, 1.2f, 4);
    } else {
        r.drawRoundedRect(contentX, contentY, contentW, contentH, 6.0f, pal.woodR0 + 0.02f, pal.woodG0 + 0.015f, pal.woodB0 + 0.01f, 0.90f, 4);
        r.drawRoundedRectOutline(contentX, contentY, contentW, contentH, 6.0f, pal.trimR, pal.trimG, pal.trimB, 0.45f, 1.0f, 4);
    }

    float curY = contentY + 16.0f;
    auto drawSectionHeader = [&](const char* title) {
        if (isClockwork) {
            eatsbits::ui::glyph::drawVectorText(r, title, contentX + 16.0f, curY, 12.0f, cpal.furnaceCoreR, cpal.furnaceCoreG, cpal.furnaceCoreB, 1.0f);
        } else {
            eatsbits::ui::glyph::drawVectorText(r, title, contentX + 16.0f, curY, 12.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }
        curY += 24.0f;
    };

    if (currentTab_ == 0) {
        // --- TAB 0: GAME & SEED ---
        drawSectionHeader("DEAL SEED & PROCEDURAL RANDOMIZATION");

        // Seed edit box
        const float seedBoxX = contentX + 16.0f;
        const float seedBoxW = 200.0f;
        const float seedBoxH = 30.0f;
        r.drawRoundedRect(seedBoxX, curY, seedBoxW, seedBoxH, 4.0f, 0.08f, 0.07f, 0.06f, 0.95f, 4);
        r.drawRoundedRectOutline(seedBoxX, curY, seedBoxW, seedBoxH, 4.0f, editingSeed_ ? pal.accentR : pal.trimR, editingSeed_ ? pal.accentG : pal.trimG, editingSeed_ ? pal.accentB : pal.trimB, 0.85f, 1.4f, 4);
        std::string displaySeed = seedText_ + (editingSeed_ ? "_" : "");
        eatsbits::ui::glyph::drawVectorText(r, displaySeed.c_str(), seedBoxX + 10.0f, curY + 9.0f, 12.0f, 0.98f, 0.95f, 0.88f, 1.0f);

        // [-] step button
        const float stepBtnW = 28.0f;
        const float minusX = seedBoxX + seedBoxW + 8.0f;
        bool minusHov = (mouseX >= minusX && mouseX <= minusX + stepBtnW && mouseY >= curY && mouseY <= curY + seedBoxH);
        r.drawRoundedRect(minusX, curY, stepBtnW, seedBoxH, 4.0f, pal.woodR0 + 0.05f + (minusHov ? 0.08f : 0.0f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
        r.drawRoundedRectOutline(minusX, curY, stepBtnW, seedBoxH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
        eatsbits::ui::glyph::drawVectorText(r, "-", minusX + 10.0f, curY + 9.0f, 14.0f, 0.95f, 0.92f, 0.85f, 1.0f);

        // [+] step button
        const float plusX = minusX + stepBtnW + 6.0f;
        bool plusHov = (mouseX >= plusX && mouseX <= plusX + stepBtnW && mouseY >= curY && mouseY <= curY + seedBoxH);
        r.drawRoundedRect(plusX, curY, stepBtnW, seedBoxH, 4.0f, pal.woodR0 + 0.05f + (plusHov ? 0.08f : 0.0f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
        r.drawRoundedRectOutline(plusX, curY, stepBtnW, seedBoxH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
        eatsbits::ui::glyph::drawVectorText(r, "+", plusX + 9.0f, curY + 9.0f, 14.0f, 0.95f, 0.92f, 0.85f, 1.0f);

        // [RANDOMIZE] button
        const float randBtnX = plusX + stepBtnW + 12.0f;
        const float randBtnW = 140.0f;
        bool randHov = (mouseX >= randBtnX && mouseX <= randBtnX + randBtnW && mouseY >= curY && mouseY <= curY + seedBoxH);
        r.drawRoundedRect(randBtnX, curY, randBtnW, seedBoxH, 4.0f, pal.woodR0 + 0.08f + (randHov ? 0.10f : 0.0f), pal.woodG0 + 0.07f, pal.woodB0 + 0.05f, 0.95f, 4);
        r.drawRoundedRectOutline(randBtnX, curY, randBtnW, seedBoxH, 4.0f, randHov ? pal.accentR : pal.trimR, randHov ? pal.accentG : pal.trimG, randHov ? pal.accentB : pal.trimB, 0.90f, 1.3f, 4);
        float randTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.5f, "* RANDOMIZE");
        eatsbits::ui::glyph::drawVectorText(r, "* RANDOMIZE", randBtnX + (randBtnW - randTxtW) * 0.5f, curY + 9.0f, 11.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);

        curY += 42.0f;

        // "Randomize seed on New Deal" toggle
        const float checkSize = 18.0f;
        const float checkX = contentX + 16.0f;
        r.drawRoundedRect(checkX, curY, checkSize, checkSize, 3.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
        r.drawRoundedRectOutline(checkX, curY, checkSize, checkSize, 3.0f, pal.trimR, pal.trimG, pal.trimB, 0.80f, 1.2f, 4);
        if (tempSettings_.randomizeOnNewDeal) {
            eatsbits::ui::glyph::drawVectorText(r, "X", checkX + 5.0f, curY + 2.5f, 12.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }
        eatsbits::ui::glyph::drawVectorText(r, "Auto-randomize seed on every 'New Deal' action", checkX + 26.0f, curY + 4.0f, 11.5f, 0.92f, 0.90f, 0.85f, 1.0f);

        curY += 28.0f;

        // "Guarantee Solvable Deals" toggle
        r.drawRoundedRect(checkX, curY, checkSize, checkSize, 3.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
        r.drawRoundedRectOutline(checkX, curY, checkSize, checkSize, 3.0f, pal.trimR, pal.trimG, pal.trimB, 0.80f, 1.2f, 4);
        if (tempSettings_.guaranteeSolvable) {
            eatsbits::ui::glyph::drawVectorText(r, "X", checkX + 5.0f, curY + 2.5f, 12.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }
        eatsbits::ui::glyph::drawVectorText(r, "Guarantee solvable deals (analyzes deal before starting)", checkX + 26.0f, curY + 4.0f, 11.5f, 0.92f, 0.90f, 0.85f, 1.0f);

        curY += 34.0f;

        // "GAME VARIANT" selector
        drawSectionHeader("GAME RULES & VARIANT");
        const char* variants[8] = {
            "KLONDIKE (DRAW 1)", "KLONDIKE (DRAW 3)",
            "FREECELL",          "SPIDER (1 SUIT)",
            "SPIDER (2 SUITS)",  "SPIDER (3 SUITS)",
            "SPIDER (4 SUITS)",  "GOLF"
        };
        const float varBtnW = (contentW - 48.0f) * 0.5f;
        const float varBtnH = 28.0f;

        for (int v = 0; v < 8; ++v) {
            float curW = varBtnW;
            float vx = contentX + 16.0f + ((v % 2) * (varBtnW + 16.0f));
            float vy = curY + (v / 2) * (varBtnH + 8.0f);
            bool isSel = (static_cast<int>(tempSettings_.variant) == v);
            bool vHov = (mouseX >= vx && mouseX <= vx + curW && mouseY >= vy && mouseY <= vy + varBtnH);

            r.drawRoundedRect(vx, vy, curW, varBtnH, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (vHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
            r.drawRoundedRectOutline(vx, vy, curW, varBtnH, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
            float vTextW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, variants[v]);
            eatsbits::ui::glyph::drawVectorText(r, variants[v], vx + (curW - vTextW) * 0.5f, vy + 8.0f, 11.0f, isSel ? pal.accentR : 0.92f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
        }
    } else if (currentTab_ == 1) {
        // --- TAB 1: THEMES & PHYSICAL FINISHES ---
        const float rowX = contentX + 16.0f;
        const float rowW = contentW - 32.0f;
        const float rowH = 28.0f;
        const float btnW = 32.0f;
        const float rowGap = 6.0f;
        const float labelX = rowX + btnW + 6.0f;
        const float labelW = rowW - 2.0f * (btnW + 6.0f);
        const float nextX = rowX + rowW - btnW;

        auto drawStepper = [&](const std::string& text) {
            bool prevHov = (mouseX >= rowX && mouseX <= rowX + btnW && mouseY >= curY && mouseY <= curY + rowH);
            bool nextHov = (mouseX >= nextX && mouseX <= nextX + btnW && mouseY >= curY && mouseY <= curY + rowH);
            bool centerHov = (mouseX >= labelX && mouseX <= labelX + labelW && mouseY >= curY && mouseY <= curY + rowH);

            if (isClockwork) {
                clockwork_ui::drawStepperBar(r, rowX, curY, rowW, rowH, text, prevHov, nextHov, centerHov);
                return;
            }

            // Left [ < ] button
            r.drawRoundedRect(rowX, curY, btnW, rowH, 4.0f,
                              pal.woodR0 + 0.05f + (prevHov ? 0.08f : 0.0f),
                              pal.woodG0 + 0.04f + (prevHov ? 0.06f : 0.0f),
                              pal.woodB0 + 0.03f + (prevHov ? 0.05f : 0.0f),
                              0.95f, 4);
            r.drawRoundedRectOutline(rowX, curY, btnW, rowH, 4.0f,
                                     prevHov ? pal.accentR : pal.trimR,
                                     prevHov ? pal.accentG : pal.trimG,
                                     prevHov ? pal.accentB : pal.trimB,
                                     prevHov ? 1.0f : 0.65f, prevHov ? 1.4f : 1.1f, 4);
            float prevTxtW = eatsbits::ui::glyph::getVectorTextWidth(12.0f, "<");
            eatsbits::ui::glyph::drawVectorText(r, "<",
                                                rowX + (btnW - prevTxtW) * 0.5f,
                                                curY + (rowH - 12.0f) * 0.5f,
                                                12.0f,
                                                prevHov ? pal.accentR : 0.92f,
                                                prevHov ? pal.accentG : 0.88f,
                                                prevHov ? pal.accentB : 0.82f, 1.0f);

            // Center display box
            r.drawRoundedRect(labelX, curY, labelW, rowH, 4.0f,
                              pal.woodR0 + 0.07f + (centerHov ? 0.04f : 0.0f),
                              pal.woodG0 + 0.05f + (centerHov ? 0.03f : 0.0f),
                              pal.woodB0 + 0.04f + (centerHov ? 0.02f : 0.0f),
                              0.95f, 4);
            r.drawRoundedRectOutline(labelX, curY, labelW, rowH, 4.0f,
                                     centerHov ? pal.accentR : pal.trimR,
                                     centerHov ? pal.accentG : pal.trimG,
                                     centerHov ? pal.accentB : pal.trimB,
                                     centerHov ? 0.90f : 0.60f, centerHov ? 1.3f : 1.0f, 4);
            float labelTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, text);
            eatsbits::ui::glyph::drawVectorText(r, text.c_str(),
                                                labelX + (labelW - labelTxtW) * 0.5f,
                                                curY + (rowH - 11.0f) * 0.5f,
                                                11.0f,
                                                centerHov ? pal.accentR : 0.96f,
                                                centerHov ? pal.accentG : 0.93f,
                                                centerHov ? pal.accentB : 0.87f, 1.0f);

            // Right [ > ] button
            r.drawRoundedRect(nextX, curY, btnW, rowH, 4.0f,
                              pal.woodR0 + 0.05f + (nextHov ? 0.08f : 0.0f),
                              pal.woodG0 + 0.04f + (nextHov ? 0.06f : 0.0f),
                              pal.woodB0 + 0.03f + (nextHov ? 0.05f : 0.0f),
                              0.95f, 4);
            r.drawRoundedRectOutline(nextX, curY, btnW, rowH, 4.0f,
                                     nextHov ? pal.accentR : pal.trimR,
                                     nextHov ? pal.accentG : pal.trimG,
                                     nextHov ? pal.accentB : pal.trimB,
                                     nextHov ? 1.0f : 0.65f, nextHov ? 1.4f : 1.1f, 4);
            float nextTxtW = eatsbits::ui::glyph::getVectorTextWidth(12.0f, ">");
            eatsbits::ui::glyph::drawVectorText(r, ">",
                                                nextX + (btnW - nextTxtW) * 0.5f,
                                                curY + (rowH - 12.0f) * 0.5f,
                                                12.0f,
                                                nextHov ? pal.accentR : 0.92f,
                                                nextHov ? pal.accentG : 0.88f,
                                                nextHov ? pal.accentB : 0.82f, 1.0f);
        };

        // 1. Table Theme
        const auto& availableThemes = ProceduralBackgroundSystem::instance().availableThemes();
        size_t curThemeIdx = ProceduralBackgroundSystem::instance().findThemeIndex(tempSettings_.tableThemeSlug, tempSettings_.tableTheme);
        std::string themeTitle = (curThemeIdx < availableThemes.size()) ? availableThemes[curThemeIdx].name : SolitaireBoard::themeName(tempSettings_.tableTheme);
        std::string themeStr = "Table Theme: " + themeTitle;
        drawStepper(themeStr);
        curY += rowH + rowGap;

        // 2. Deck Theme
        std::string deckStr = "Deck: " + std::string(CardTextureCache::deckThemeName(tempSettings_.deckTheme));
        drawStepper(deckStr);
        curY += rowH + rowGap;

        // 2b. UI Theme
        std::string uiThemeStr = "UI Theme: " + std::string(tempSettings_.guiTheme == GuiThemeStyle::ClockworkEngine ? "CLOCKWORK ENGINE" : "CLASSIC");
        drawStepper(uiThemeStr);
        curY += rowH + 10.0f;

        // Section: Card Materials and Condition
        drawSectionHeader("Card Materials and Condition");

        // 3. Stock
        std::string stockStr = "Stock: " + std::string(CardTextureCache::cardStockName(tempSettings_.cardStock));
        drawStepper(stockStr);
        curY += rowH + rowGap;

        // 4. Wear
        std::string wearStr = "Wear: " + std::string(CardTextureCache::cardWearLevelName(tempSettings_.cardWear));
        drawStepper(wearStr);
        curY += rowH + rowGap;

        // 5. Corners
        std::string cornersStr = "Corners: " + std::string(cornerStyleCleanName(tempSettings_.cardCorners));
        drawStepper(cornersStr);
        curY += rowH + 10.0f;

        // Section: Organic Card Deviation
        drawSectionHeader("Organic Card Deviation");
        devTrackX_ = contentX + 16.0f;
        devTrackW_ = contentW - 148.0f;
        devTrackH_ = 28.0f;
        devTrackY_ = curY;

        int devPct = static_cast<int>(std::round(tempSettings_.cardDeviation * 100.0f));
        const float devBtnX = devTrackX_ + devTrackW_ + 12.0f;
        const float devBtnW = 104.0f;
        bool devBtnHov = (mouseX >= devBtnX && mouseX <= devBtnX + devBtnW && mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_);
        const char* devBtnLabel = (tempSettings_.cardDeviation <= 0.005f) ? "PRESET 25%" : "ZERO 0%";

        if (isClockwork) {
            bool knobHovered = (mouseX >= devTrackX_ && mouseX <= devTrackX_ + devTrackW_ && mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_);
            clockwork_ui::drawGlassSlider(r, devTrackX_, devTrackY_, devTrackW_, devTrackH_, tempSettings_.cardDeviation, nullptr, knobHovered, 0.0f);

            char devStr[80];
            if (tempSettings_.cardDeviation <= 0.005f) {
                std::snprintf(devStr, sizeof(devStr), "DEVIATION: 0%% (NEAT)");
            } else {
                float posPx = tempSettings_.cardDeviation * 10.0f;
                float angDeg = tempSettings_.cardDeviation * 5.0f;
                std::snprintf(devStr, sizeof(devStr), "DEVIATION: %d%% (+-%.1fpx, +-%.1f deg)", devPct, posPx, angDeg);
            }
            eatsbits::ui::glyph::drawVectorText(r, devStr, devTrackX_ + 14.0f, devTrackY_ + 8.5f, 10.5f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
            clockwork_ui::drawPewterButton(r, devBtnX, devTrackY_, devBtnW, devTrackH_, 4.0f, devBtnLabel, devBtnHov, false, 11.0f);
        } else {
            r.drawRoundedRect(devTrackX_, devTrackY_, devTrackW_, devTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(devTrackX_, devTrackY_, devTrackW_, devTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);

            float devFillW = (devTrackW_ - 4.0f) * tempSettings_.cardDeviation;
            if (devFillW > 0.0f) {
                r.drawRoundedRect(devTrackX_ + 2.0f, devTrackY_ + 2.0f, devFillW, devTrackH_ - 4.0f, 3.0f,
                                  pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            }

            // Distinctive thumb indicator knob
            float thumbX = devTrackX_ + 2.0f + devFillW;
            r.drawRoundedRect(thumbX - 4.0f, devTrackY_ - 1.0f, 8.0f, devTrackH_ + 2.0f, 3.0f,
                              pal.accentR, pal.accentG, pal.accentB, 0.98f, 3);
            r.drawRoundedRectOutline(thumbX - 4.0f, devTrackY_ - 1.0f, 8.0f, devTrackH_ + 2.0f, 3.0f,
                                     pal.trimR, pal.trimG, pal.trimB, 0.90f, 1.0f, 3);

            char devStr[80];
            if (tempSettings_.cardDeviation <= 0.005f) {
                std::snprintf(devStr, sizeof(devStr), "DEVIATION: 0%% (NEAT: 0px, 0.0 deg)");
            } else {
                float posPx = tempSettings_.cardDeviation * 10.0f;
                float angDeg = tempSettings_.cardDeviation * 5.0f;
                std::snprintf(devStr, sizeof(devStr), "DEVIATION: %d%% (+-%.1fpx, +-%.1f deg)", devPct, posPx, angDeg);
            }
            eatsbits::ui::glyph::drawVectorText(r, devStr, devTrackX_ + 10.0f, devTrackY_ + 8.5f, 11.0f, 0.98f, 0.95f, 0.88f, 1.0f);

            r.drawRoundedRect(devBtnX, devTrackY_, devBtnW, devTrackH_, 4.0f,
                              pal.woodR0 + 0.06f + (devBtnHov ? 0.08f : 0.0f), pal.woodG0 + 0.05f, pal.woodB0 + 0.04f, 0.95f, 4);
            r.drawRoundedRectOutline(devBtnX, devTrackY_, devBtnW, devTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float devBtnTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, devBtnLabel);
            eatsbits::ui::glyph::drawVectorText(r, devBtnLabel, devBtnX + (devBtnW - devBtnTxtW) * 0.5f, devTrackY_ + 8.5f, 11.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }

        // Subtext explaining scale
        curY += 34.0f;
        eatsbits::ui::glyph::drawVectorText(
            r, "0% = Crisp alignment (0px, 0 deg)   ---   100% = Maximum organic skew (+-10px, +-5 deg)",
            devTrackX_ + 4.0f, curY, 9.5f, pal.trimR, pal.trimG, pal.trimB, 0.85f
        );
    } else if (currentTab_ == 2) {
        // --- TAB 2: DISPLAY & RESPONSIVE ---
        drawSectionHeader("HUD MENU PRESENTATION STYLE");
        const char* menuModes[3] = { "AUTO (ADAPTIVE)", "ICON + TEXT", "ICONS ONLY" };
        const MenuDisplayMode menuVals[3] = {
            MenuDisplayMode::Auto,
            MenuDisplayMode::IconAndText,
            MenuDisplayMode::IconOnly
        };
        const float mmBtnW = (contentW - 48.0f) / 3.0f;
        for (int m = 0; m < 3; ++m) {
            float mmx = contentX + 16.0f + m * (mmBtnW + 8.0f);
            bool isSel = (tempSettings_.menuDisplayMode == menuVals[m]);
            bool mHov = (mouseX >= mmx && mouseX <= mmx + mmBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            r.drawRoundedRect(mmx, curY, mmBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (mHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
            r.drawRoundedRectOutline(mmx, curY, mmBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
            eatsbits::ui::glyph::drawVectorText(r, menuModes[m], mmx + 12.0f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
        }

        curY += 36.0f;

        drawSectionHeader("RESPONSIVE LAYOUT & ASPECT-RATIO ADAPTATION");
        const char* layoutModes[3] = { "AUTO (ADAPTIVE)", "DESKTOP (CLASSIC)", "MOBILE (COMPACT)" };
        const ResponsiveLayoutMode layoutVals[3] = {
            ResponsiveLayoutMode::Auto,
            ResponsiveLayoutMode::DesktopStandard,
            ResponsiveLayoutMode::MobileCompact
        };
        const float lBtnW = (contentW - 48.0f) / 3.0f;
        for (int m = 0; m < 3; ++m) {
            float lx = contentX + 16.0f + m * (lBtnW + 8.0f);
            bool isSel = (tempSettings_.layoutMode == layoutVals[m]);
            bool lHov = (mouseX >= lx && mouseX <= lx + lBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            r.drawRoundedRect(lx, curY, lBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (lHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
            r.drawRoundedRectOutline(lx, curY, lBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
            eatsbits::ui::glyph::drawVectorText(r, layoutModes[m], lx + 12.0f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
        }

        curY += 38.0f;

        drawSectionHeader("MOBILE DECK & STOCK POSITION");
        const char* stockPosLabels[4] = { "AUTO (SMART)", "TOP-LEFT", "BOT-RIGHT (THUMB)", "BOT-LEFT (LEFTY)" };
        const StockPosition stockPosVals[4] = {
            StockPosition::Auto,
            StockPosition::TopLeft,
            StockPosition::BottomRight,
            StockPosition::BottomLeft
        };
        const float sBtnW = (contentW - 48.0f) / 4.0f;
        for (int s = 0; s < 4; ++s) {
            float sx = contentX + 16.0f + s * (sBtnW + 8.0f);
            bool isSel = (tempSettings_.stockPosition == stockPosVals[s]);
            bool sHov = (mouseX >= sx && mouseX <= sx + sBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            r.drawRoundedRect(sx, curY, sBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (sHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
            r.drawRoundedRectOutline(sx, curY, sBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
            eatsbits::ui::glyph::drawVectorText(r, stockPosLabels[s], sx + 6.0f, curY + 8.0f, 9.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
        }

        curY += 38.0f;

        drawSectionHeader("HUD DOCK ANCHOR");
        const char* anchors[4] = { "BOT-LEFT", "BOT-RIGHT", "TOP-LEFT", "TOP-RIGHT" };
        const float anchorBtnW = (contentW - 48.0f) / 4.0f;
        for (int a = 0; a < 4; ++a) {
            float ax = contentX + 16.0f + a * (anchorBtnW + 8.0f);
            bool isSel = (static_cast<int>(tempSettings_.menuAnchor) == a);
            bool aHov = (mouseX >= ax && mouseX <= ax + anchorBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            r.drawRoundedRect(ax, curY, anchorBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (aHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
            r.drawRoundedRectOutline(ax, curY, anchorBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
            eatsbits::ui::glyph::drawVectorText(r, anchors[a], ax + 14.0f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
        }

        curY += 38.0f;

        drawSectionHeader("TABLETOP GUIDES & PILE MARKINGS");
        const char* guideModes[2] = { "GUIDES & RUNES (DEFAULT)", "BARE TABLETOP (CARDS ONLY)" };
        const float gBtnW = (contentW - 40.0f) * 0.5f;
        for (int g = 0; g < 2; ++g) {
            float gx = contentX + 16.0f + g * (gBtnW + 8.0f);
            bool isSel = (g == 0 ? tempSettings_.showTableMarkings : !tempSettings_.showTableMarkings);
            bool gHov = (mouseX >= gx && mouseX <= gx + gBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            r.drawRoundedRect(gx, curY, gBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (gHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
            r.drawRoundedRectOutline(gx, curY, gBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
            float gTextW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, guideModes[g]);
            eatsbits::ui::glyph::drawVectorText(r, guideModes[g], gx + (gBtnW - gTextW) * 0.5f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
        }

        curY += 34.0f;
        eatsbits::ui::glyph::drawVectorText(r, "Hides pile wells, suit runes, and board guides for a pure real-world card table feel.", contentX + 16.0f, curY, 9.5f, 0.70f, 0.68f, 0.62f, 0.85f);
    } else {
        // --- TAB 3: AUDIO & LIGHT ---
        drawSectionHeader("PROCEDURAL FOLEY AUDIO SYNTHESIZER");

        // Master Volume slider row
        volTrackX_ = contentX + 16.0f;
        volTrackW_ = 280.0f;
        volTrackH_ = 26.0f;
        volTrackY_ = curY;

        int volPct = static_cast<int>(std::round(tempSettings_.masterVolume * 100.0f));
        std::string volStr = "VOLUME: " + std::to_string(volPct) + "%";

        if (isClockwork) {
            bool volHov = (mouseX >= volTrackX_ && mouseX <= volTrackX_ + volTrackW_ && mouseY >= volTrackY_ && mouseY <= volTrackY_ + volTrackH_);
            clockwork_ui::drawGlassSlider(r, volTrackX_, volTrackY_, volTrackW_, volTrackH_, tempSettings_.masterVolume, nullptr, volHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, volStr.c_str(), volTrackX_ + 14.0f, volTrackY_ + 7.5f, 11.0f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
        } else {
            r.drawRoundedRect(volTrackX_, volTrackY_, volTrackW_, volTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(volTrackX_, volTrackY_, volTrackW_, volTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);

            float fillW = (volTrackW_ - 4.0f) * tempSettings_.masterVolume;
            r.drawRoundedRect(volTrackX_ + 2.0f, volTrackY_ + 2.0f, fillW, volTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, volStr.c_str(), volTrackX_ + 10.0f, volTrackY_ + 7.5f, 11.0f, 0.98f, 0.95f, 0.88f, 1.0f);
        }

        // Mute toggle button
        const float muteBtnX = volTrackX_ + volTrackW_ + 12.0f;
        const float muteBtnW = 100.0f;
        bool muteHov = (mouseX >= muteBtnX && mouseX <= muteBtnX + muteBtnW && mouseY >= curY && mouseY <= curY + volTrackH_);
        const char* muteLabel = tempSettings_.muted ? "MUTED" : "UNMUTED";
        if (isClockwork) {
            clockwork_ui::drawPewterButton(r, muteBtnX, curY, muteBtnW, volTrackH_, 4.0f, muteLabel, muteHov, tempSettings_.muted, 11.0f);
        } else {
            r.drawRoundedRect(muteBtnX, curY, muteBtnW, volTrackH_, 4.0f, pal.woodR0 + 0.06f + (muteHov ? 0.08f : 0.0f), pal.woodG0 + 0.05f, pal.woodB0 + 0.04f, 0.95f, 4);
            r.drawRoundedRectOutline(muteBtnX, curY, muteBtnW, volTrackH_, 4.0f, tempSettings_.muted ? 0.85f : pal.trimR, tempSettings_.muted ? 0.35f : pal.trimG, tempSettings_.muted ? 0.35f : pal.trimB, 0.80f, 1.2f, 4);
            float muteTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, muteLabel);
            eatsbits::ui::glyph::drawVectorText(r, muteLabel, muteBtnX + (muteBtnW - muteTxtW) * 0.5f, curY + (volTrackH_ - 11.0f) * 0.5f, 11.0f, tempSettings_.muted ? 0.90f : 0.95f, tempSettings_.muted ? 0.45f : 0.92f, tempSettings_.muted ? 0.45f : 0.85f, 1.0f);
        }

        curY += 40.0f;

        drawSectionHeader("PHYSICAL ILLUMINATION & DYNAMIC LIGHT (OFF = RECOMMENDED)");
        const char* lightModes[5] = { "OFF (STEADY)", "CHANDELIER", "CANDLE", "HAUNTED", "CYBERPUNK" };
        const LightingPreset lightVals[5] = {
            LightingPreset::StudioSteady,
            LightingPreset::GentleChandelier,
            LightingPreset::Candlelight,
            LightingPreset::HauntedManor,
            LightingPreset::CyberpunkNeon
        };
        const float lightBtnW = (contentW - 64.0f) / 5.0f;
        for (int l = 0; l < 5; ++l) {
            float lx = contentX + 16.0f + l * (lightBtnW + 8.0f);
            bool isSel = (tempSettings_.lightingPreset == lightVals[l]);
            bool lHov = (mouseX >= lx && mouseX <= lx + lightBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, lx, curY, lightBtnW, 28.0f, 4.0f, lightModes[l], lHov, isSel, 9.5f);
            } else {
                r.drawRoundedRect(lx, curY, lightBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (lHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(lx, curY, lightBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, lightModes[l], lx + 8.0f, curY + 8.0f, 9.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }

        curY += 40.0f;
        drawSectionHeader("SCENE DARKNESS & LIGHT SOURCE STRENGTH (NATURAL VIGNETTE)");

        const float sliderGap = 16.0f;
        const float halfSliderW = (contentW - 32.0f - sliderGap) * 0.5f;

        // Scene Darkness slider (0% .. 85%)
        darkTrackX_ = contentX + 16.0f;
        darkTrackY_ = curY;
        darkTrackW_ = halfSliderW;
        darkTrackH_ = 26.0f;

        float darkNorm = std::clamp(tempSettings_.sceneDarkness / 0.85f, 0.0f, 1.0f);
        int darkPct = static_cast<int>(std::round(tempSettings_.sceneDarkness * 100.0f));
        std::string darkStr = "SCENE DARKNESS: " + std::to_string(darkPct) + "%";

        // Light Strength slider (0% .. 200%)
        lightStrTrackX_ = darkTrackX_ + darkTrackW_ + sliderGap;
        lightStrTrackY_ = curY;
        lightStrTrackW_ = halfSliderW;
        lightStrTrackH_ = 26.0f;

        float lightNorm = std::clamp(tempSettings_.lightStrength / 2.0f, 0.0f, 1.0f);
        int lightPct = static_cast<int>(std::round(tempSettings_.lightStrength * 100.0f));
        std::string lightStr = "LIGHT STRENGTH: " + std::to_string(lightPct) + "%";

        if (isClockwork) {
            bool darkHov = (mouseX >= darkTrackX_ && mouseX <= darkTrackX_ + darkTrackW_ && mouseY >= darkTrackY_ && mouseY <= darkTrackY_ + darkTrackH_);
            clockwork_ui::drawGlassSlider(r, darkTrackX_, darkTrackY_, darkTrackW_, darkTrackH_, darkNorm, nullptr, darkHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, darkStr.c_str(), darkTrackX_ + 14.0f, darkTrackY_ + 7.5f, 10.5f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);

            bool lightHov = (mouseX >= lightStrTrackX_ && mouseX <= lightStrTrackX_ + lightStrTrackW_ && mouseY >= lightStrTrackY_ && mouseY <= lightStrTrackY_ + lightStrTrackH_);
            clockwork_ui::drawGlassSlider(r, lightStrTrackX_, lightStrTrackY_, lightStrTrackW_, lightStrTrackH_, lightNorm, nullptr, lightHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, lightStr.c_str(), lightStrTrackX_ + 14.0f, lightStrTrackY_ + 7.5f, 10.5f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
        } else {
            r.drawRoundedRect(darkTrackX_, darkTrackY_, darkTrackW_, darkTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(darkTrackX_, darkTrackY_, darkTrackW_, darkTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float darkFillW = (darkTrackW_ - 4.0f) * darkNorm;
            r.drawRoundedRect(darkTrackX_ + 2.0f, darkTrackY_ + 2.0f, darkFillW, darkTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, darkStr.c_str(), darkTrackX_ + 10.0f, darkTrackY_ + 7.5f, 10.5f, 0.98f, 0.95f, 0.88f, 1.0f);

            r.drawRoundedRect(lightStrTrackX_, lightStrTrackY_, lightStrTrackW_, lightStrTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(lightStrTrackX_, lightStrTrackY_, lightStrTrackW_, lightStrTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float lightFillW = (lightStrTrackW_ - 4.0f) * lightNorm;
            r.drawRoundedRect(lightStrTrackX_ + 2.0f, lightStrTrackY_ + 2.0f, lightFillW, lightStrTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, lightStr.c_str(), lightStrTrackX_ + 10.0f, lightStrTrackY_ + 7.5f, 10.5f, 0.98f, 0.95f, 0.88f, 1.0f);
        }

        curY += 34.0f;
        eatsbits::ui::glyph::drawVectorText(r, "Subtle scene darkness + elevated light strength creates a warm, natural overhead vignette.", contentX + 16.0f, curY, 9.5f, 0.70f, 0.68f, 0.62f, 0.85f);
    }

    // 6. Action Footer: [RESET TO DEFAULTS] and [APPLY & CLOSE]
    const float footerY = dialogY_ + dialogH_ - 44.0f;
    const float resetBtnW = 150.0f;
    const float resetBtnH = 32.0f;
    const float resetBtnX = dialogX_ + 16.0f;
    bool resetHov = (mouseX >= resetBtnX && mouseX <= resetBtnX + resetBtnW && mouseY >= footerY && mouseY <= footerY + resetBtnH);

    const float applyBtnW = 160.0f;
    const float applyBtnH = 32.0f;
    const float applyBtnX = dialogX_ + dialogW_ - applyBtnW - 16.0f;
    bool applyHov = (mouseX >= applyBtnX && mouseX <= applyBtnX + applyBtnW && mouseY >= footerY && mouseY <= footerY + applyBtnH);

    if (isClockwork) {
        clockwork_ui::drawPewterButton(r, resetBtnX, footerY, resetBtnW, resetBtnH, 4.0f, "RESET DEFAULTS", resetHov, false, 11.0f);
        clockwork_ui::drawFurnaceButton(r, applyBtnX, footerY, applyBtnW, applyBtnH, 4.0f, "APPLY & CLOSE", applyHov, false, 11.5f);
    } else {
        r.drawRoundedRect(resetBtnX, footerY, resetBtnW, resetBtnH, 4.0f, pal.woodR0 + 0.05f + (resetHov ? 0.07f : 0.0f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
        r.drawRoundedRectOutline(resetBtnX, footerY, resetBtnW, resetBtnH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
        float resetTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, "RESET DEFAULTS");
        eatsbits::ui::glyph::drawVectorText(r, "RESET DEFAULTS", resetBtnX + (resetBtnW - resetTxtW) * 0.5f, footerY + (resetBtnH - 11.0f) * 0.5f, 11.0f, 0.88f, 0.85f, 0.80f, 1.0f);

        r.drawRoundedRect(applyBtnX, footerY, applyBtnW, applyBtnH, 4.0f, pal.woodR0 + 0.12f + (applyHov ? 0.10f : 0.0f), pal.woodG0 + 0.10f, pal.woodB0 + 0.08f, 0.98f, 4);
        r.drawRoundedRectOutline(applyBtnX, footerY, applyBtnW, applyBtnH, 4.0f, pal.accentR, pal.accentG, pal.accentB, 0.95f, 1.6f, 4);
        float applyTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.5f, "APPLY & CLOSE");
        eatsbits::ui::glyph::drawVectorText(r, "APPLY & CLOSE", applyBtnX + (applyBtnW - applyTxtW) * 0.5f, footerY + (applyBtnH - 11.5f) * 0.5f, 11.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
    }
}

bool SettingsDialog::handleMouseButton(
    int button,
    int action,
    float mouseX,
    float mouseY,
    SolitaireBoard& board,
    AudioFoleyEngine& foley
) {
    if (!isOpen_) return false;
    if (button != 0) return true;
    if (action == 0) {
        draggingVolume_ = false;
        draggingDeviation_ = false;
        draggingDarkness_ = false;
        draggingLightStrength_ = false;
        return true;
    }

    // Tapping bottom handle or backdrop outside bounds closes drawer
    if (mouseY >= dialogH_ - 14.0f && mouseY <= dialogH_ + 8.0f) {
        close();
        return true;
    }
    if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ || mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
        close();
        return true;
    }
    if (action != 1) return true;

    // Check outside dialog click -> close
    if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ || mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
        board.setCardDeviation(SettingsManager::instance().settings().cardDeviation);
        foley.setMasterVolume(SettingsManager::instance().settings().masterVolume);
        close();
        return true;
    }

    // Close button [X]
    const float headerH = 48.0f;
    const float closeBtnSize = 28.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 12.0f;
    const float closeBtnY = dialogY_ + (headerH - closeBtnSize) * 0.5f;
    if (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize && mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize) {
        board.setCardDeviation(SettingsManager::instance().settings().cardDeviation);
        foley.setMasterVolume(SettingsManager::instance().settings().masterVolume);
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.8f);
        return true;
    }

    // Tab buttons (4 tabs)
    const float tabY = dialogY_ + headerH + 10.0f;
    const float tabH = 32.0f;
    const float tabW = (dialogW_ - 32.0f) / 4.0f;
    for (int t = 0; t < 4; ++t) {
        float tx = dialogX_ + 16.0f + t * tabW;
        if (mouseX >= tx && mouseX <= tx + tabW - 4.0f && mouseY >= tabY && mouseY <= tabY + tabH) {
            currentTab_ = t;
            editingSeed_ = false;
            foley.triggerSound(FoleySound::WoodClick, 0.75f);
            return true;
        }
    }

    const float contentX = dialogX_ + 16.0f;
    const float contentW = dialogW_ - 32.0f;
    const float tabContentY = tabY + tabH + 12.0f;

    if (currentTab_ == 0) {
        float curY = tabContentY + 16.0f + 24.0f;
        const float seedBoxX = contentX + 16.0f;
        const float seedBoxW = 200.0f;
        const float seedBoxH = 30.0f;

        if (mouseX >= seedBoxX && mouseX <= seedBoxX + seedBoxW && mouseY >= curY && mouseY <= curY + seedBoxH) {
            editingSeed_ = true;
            return true;
        }

        const float stepBtnW = 28.0f;
        const float minusX = seedBoxX + seedBoxW + 8.0f;
        if (mouseX >= minusX && mouseX <= minusX + stepBtnW && mouseY >= curY && mouseY <= curY + seedBoxH) {
            if (tempSettings_.lastSeed > 1) {
                --tempSettings_.lastSeed;
                seedText_ = std::to_string(tempSettings_.lastSeed);
                foley.triggerSound(FoleySound::WoodClick, 0.6f);
            }
            return true;
        }

        const float plusX = minusX + stepBtnW + 6.0f;
        if (mouseX >= plusX && mouseX <= plusX + stepBtnW && mouseY >= curY && mouseY <= curY + seedBoxH) {
            ++tempSettings_.lastSeed;
            seedText_ = std::to_string(tempSettings_.lastSeed);
            foley.triggerSound(FoleySound::WoodClick, 0.6f);
            return true;
        }

        const float randBtnX = plusX + stepBtnW + 12.0f;
        const float randBtnW = 140.0f;
        if (mouseX >= randBtnX && mouseX <= randBtnX + randBtnW && mouseY >= curY && mouseY <= curY + seedBoxH) {
            randomizeSeed();
            foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
            return true;
        }

        curY += 42.0f;
        const float checkSize = 18.0f;
        if (mouseX >= seedBoxX && mouseX <= seedBoxX + 340.0f && mouseY >= curY && mouseY <= curY + checkSize + 4.0f) {
            tempSettings_.randomizeOnNewDeal = !tempSettings_.randomizeOnNewDeal;
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }

        curY += 28.0f;
        if (mouseX >= seedBoxX && mouseX <= seedBoxX + 380.0f && mouseY >= curY && mouseY <= curY + checkSize + 4.0f) {
            tempSettings_.guaranteeSolvable = !tempSettings_.guaranteeSolvable;
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }

        curY += 34.0f + 24.0f;
        const float varBtnW = (contentW - 48.0f) * 0.5f;
        const float varBtnH = 28.0f;
        for (int v = 0; v < 8; ++v) {
            float curW = varBtnW;
            float vx = contentX + 16.0f + ((v % 2) * (varBtnW + 16.0f));
            float vy = curY + (v / 2) * (varBtnH + 8.0f);
            if (mouseX >= vx && mouseX <= vx + curW && mouseY >= vy && mouseY <= vy + varBtnH) {
                tempSettings_.variant = static_cast<GameVariant>(v);
                foley.triggerSound(FoleySound::WoodClick, 0.8f);
                return true;
            }
        }
    } else if (currentTab_ == 1) {
        float curY = tabContentY + 16.0f;
        const float rowX = contentX + 16.0f;
        const float rowW = contentW - 32.0f;
        const float rowH = 28.0f;
        const float btnW = 32.0f;
        const float rowGap = 6.0f;
        const float labelX = rowX + btnW + 6.0f;
        const float labelW = rowW - 2.0f * (btnW + 6.0f);
        const float nextX = rowX + rowW - btnW;

        auto testStepper = [&](int& delta) -> bool {
            if (mouseY >= curY && mouseY <= curY + rowH) {
                if (mouseX >= rowX && mouseX <= rowX + btnW) {
                    delta = -1; // Previous
                    return true;
                }
                if (mouseX >= nextX && mouseX <= nextX + btnW) {
                    delta = +1; // Next
                    return true;
                }
                if (mouseX >= labelX && mouseX <= labelX + labelW) {
                    delta = +1; // Center cycles forward
                    return true;
                }
            }
            return false;
        };

        int stepDelta = 0;

        // 1. Table Theme
        if (testStepper(stepDelta)) {
            const auto& availableThemes = ProceduralBackgroundSystem::instance().availableThemes();
            if (!availableThemes.empty()) {
                size_t count = availableThemes.size();
                size_t curIdx = ProceduralBackgroundSystem::instance().findThemeIndex(tempSettings_.tableThemeSlug, tempSettings_.tableTheme);
                if (stepDelta < 0) {
                    curIdx = (curIdx + count - 1u) % count;
                } else {
                    curIdx = (curIdx + 1u) % count;
                }
                tempSettings_.tableThemeSlug = availableThemes[curIdx].id;
                tempSettings_.tableTheme = availableThemes[curIdx].legacyId;
            }
            foley.triggerSound(FoleySound::WoodClick, 0.8f);
            return true;
        }
        curY += rowH + rowGap;

        // 2. Deck Theme
        if (testStepper(stepDelta)) {
            constexpr uint8_t count = static_cast<uint8_t>(DeckThemeId::Count);
            if (stepDelta < 0) {
                tempSettings_.deckTheme = static_cast<DeckThemeId>((static_cast<uint8_t>(tempSettings_.deckTheme) + count - 1u) % count);
            } else {
                tempSettings_.deckTheme = static_cast<DeckThemeId>((static_cast<uint8_t>(tempSettings_.deckTheme) + 1u) % count);
            }
            foley.triggerSound(FoleySound::CardSlide, 0.8f);
            return true;
        }
        curY += rowH + rowGap;

        // 2b. UI Theme
        if (testStepper(stepDelta)) {
            tempSettings_.guiTheme = (tempSettings_.guiTheme == GuiThemeStyle::ClockworkEngine)
                ? GuiThemeStyle::Classic
                : GuiThemeStyle::ClockworkEngine;
            foley.triggerSound(FoleySound::WoodClick, 0.8f);
            return true;
        }
        curY += rowH + 10.0f + 24.0f; // Row 2 + spacing + header

        // 3. Stock
        if (testStepper(stepDelta)) {
            constexpr uint8_t count = static_cast<uint8_t>(CardStock::Count);
            if (stepDelta < 0) {
                tempSettings_.cardStock = static_cast<CardStock>((static_cast<uint8_t>(tempSettings_.cardStock) + count - 1u) % count);
            } else {
                tempSettings_.cardStock = static_cast<CardStock>((static_cast<uint8_t>(tempSettings_.cardStock) + 1u) % count);
            }
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }
        curY += rowH + rowGap;

        // 4. Wear
        if (testStepper(stepDelta)) {
            constexpr uint8_t count = static_cast<uint8_t>(CardWearLevel::Count);
            if (stepDelta < 0) {
                tempSettings_.cardWear = static_cast<CardWearLevel>((static_cast<uint8_t>(tempSettings_.cardWear) + count - 1u) % count);
            } else {
                tempSettings_.cardWear = static_cast<CardWearLevel>((static_cast<uint8_t>(tempSettings_.cardWear) + 1u) % count);
            }
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }
        curY += rowH + rowGap;

        // 5. Corners
        if (testStepper(stepDelta)) {
            constexpr uint8_t count = static_cast<uint8_t>(CardCornerStyle::Count);
            if (stepDelta < 0) {
                tempSettings_.cardCorners = static_cast<CardCornerStyle>((static_cast<uint8_t>(tempSettings_.cardCorners) + count - 1u) % count);
            } else {
                tempSettings_.cardCorners = static_cast<CardCornerStyle>((static_cast<uint8_t>(tempSettings_.cardCorners) + 1u) % count);
            }
            CardTextureCache::instance().setCornerStyle(tempSettings_.cardCorners);
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }
        curY += rowH + 10.0f + 24.0f; // Row 5 + spacing + header

        // 6. Deviation slider hit test
        if (mouseX >= devTrackX_ && mouseX <= devTrackX_ + devTrackW_ &&
            mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_) {
            float pct = std::clamp((mouseX - devTrackX_) / devTrackW_, 0.0f, 1.0f);
            tempSettings_.cardDeviation = pct;
            board.setCardDeviation(pct);
            draggingDeviation_ = true;
            foley.triggerSound(FoleySound::CardSlide, 0.55f);
            return true;
        }

        // Deviation Preset / Zero button hit test
        const float devBtnX = devTrackX_ + devTrackW_ + 12.0f;
        const float devBtnW = 104.0f;
        if (mouseX >= devBtnX && mouseX <= devBtnX + devBtnW &&
            mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_) {
            if (tempSettings_.cardDeviation <= 0.005f) {
                tempSettings_.cardDeviation = 0.25f; // Jump to subtle default
            } else {
                tempSettings_.cardDeviation = 0.0f; // Zero it out
            }
            board.setCardDeviation(tempSettings_.cardDeviation);
            foley.triggerSound(FoleySound::WoodClick, 0.75f);
            return true;
        }
    } else if (currentTab_ == 2) {
        float curY = tabContentY + 16.0f + 24.0f;
        const MenuDisplayMode menuVals[3] = {
            MenuDisplayMode::Auto,
            MenuDisplayMode::IconAndText,
            MenuDisplayMode::IconOnly
        };
        const float mmBtnW = (contentW - 48.0f) / 3.0f;
        for (int m = 0; m < 3; ++m) {
            float mmx = contentX + 16.0f + m * (mmBtnW + 8.0f);
            if (mouseX >= mmx && mouseX <= mmx + mmBtnW && mouseY >= curY && mouseY <= curY + 28.0f) {
                tempSettings_.menuDisplayMode = menuVals[m];
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }

        curY += 36.0f + 24.0f;
        const ResponsiveLayoutMode layoutVals[3] = {
            ResponsiveLayoutMode::Auto,
            ResponsiveLayoutMode::DesktopStandard,
            ResponsiveLayoutMode::MobileCompact
        };
        const float lBtnW = (contentW - 48.0f) / 3.0f;
        for (int m = 0; m < 3; ++m) {
            float lx = contentX + 16.0f + m * (lBtnW + 8.0f);
            if (mouseX >= lx && mouseX <= lx + lBtnW && mouseY >= curY && mouseY <= curY + 28.0f) {
                tempSettings_.layoutMode = layoutVals[m];
                board.setLayoutMode(tempSettings_.layoutMode);
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }

        curY += 38.0f + 24.0f;
        const StockPosition stockPosVals[4] = {
            StockPosition::Auto,
            StockPosition::TopLeft,
            StockPosition::BottomRight,
            StockPosition::BottomLeft
        };
        const float sBtnW = (contentW - 48.0f) / 4.0f;
        for (int s = 0; s < 4; ++s) {
            float sx = contentX + 16.0f + s * (sBtnW + 8.0f);
            if (mouseX >= sx && mouseX <= sx + sBtnW && mouseY >= curY && mouseY <= curY + 28.0f) {
                tempSettings_.stockPosition = stockPosVals[s];
                board.setStockPosition(tempSettings_.stockPosition);
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }

        curY += 38.0f + 24.0f;
        const float anchorBtnW = (contentW - 48.0f) / 4.0f;
        for (int a = 0; a < 4; ++a) {
            float ax = contentX + 16.0f + a * (anchorBtnW + 8.0f);
            if (mouseX >= ax && mouseX <= ax + anchorBtnW && mouseY >= curY && mouseY <= curY + 28.0f) {
                tempSettings_.menuAnchor = static_cast<MenuAnchor>(a);
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }

        curY += 38.0f + 24.0f;
        const float gBtnW = (contentW - 40.0f) * 0.5f;
        for (int g = 0; g < 2; ++g) {
            float gx = contentX + 16.0f + g * (gBtnW + 8.0f);
            if (mouseX >= gx && mouseX <= gx + gBtnW && mouseY >= curY && mouseY <= curY + 28.0f) {
                tempSettings_.showTableMarkings = (g == 0);
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }
    } else {
        float curY = tabContentY + 16.0f + 24.0f;
        if (mouseX >= volTrackX_ && mouseX <= volTrackX_ + volTrackW_ &&
            mouseY >= volTrackY_ && mouseY <= volTrackY_ + volTrackH_) {
            float pct = std::clamp((mouseX - volTrackX_) / volTrackW_, 0.0f, 1.0f);
            tempSettings_.masterVolume = pct;
            foley.setMasterVolume(pct);
            draggingVolume_ = true;
            foley.triggerSound(FoleySound::FoundationChime, 0.8f);
            return true;
        }

        const float muteBtnX = volTrackX_ + volTrackW_ + 12.0f;
        const float muteBtnW = 100.0f;
        if (mouseX >= muteBtnX && mouseX <= muteBtnX + muteBtnW && mouseY >= curY && mouseY <= curY + volTrackH_) {
            tempSettings_.muted = !tempSettings_.muted;
            foley.setMuted(tempSettings_.muted);
            foley.triggerSound(FoleySound::WoodClick, 0.75f);
            return true;
        }

        curY += 40.0f + 24.0f;
        const LightingPreset lightVals[5] = {
            LightingPreset::StudioSteady,
            LightingPreset::GentleChandelier,
            LightingPreset::Candlelight,
            LightingPreset::HauntedManor,
            LightingPreset::CyberpunkNeon
        };
        const float lightBtnW = (contentW - 64.0f) / 5.0f;
        for (int l = 0; l < 5; ++l) {
            float lx = contentX + 16.0f + l * (lightBtnW + 8.0f);
            if (mouseX >= lx && mouseX <= lx + lightBtnW && mouseY >= curY && mouseY <= curY + 28.0f) {
                tempSettings_.lightingPreset = lightVals[l];
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }

        if (mouseX >= darkTrackX_ && mouseX <= darkTrackX_ + darkTrackW_ &&
            mouseY >= darkTrackY_ && mouseY <= darkTrackY_ + darkTrackH_) {
            float pct = std::clamp((mouseX - darkTrackX_) / darkTrackW_, 0.0f, 1.0f);
            tempSettings_.sceneDarkness = pct * 0.85f;
            draggingDarkness_ = true;
            foley.triggerSound(FoleySound::Flip, 0.65f);
            return true;
        }

        if (mouseX >= lightStrTrackX_ && mouseX <= lightStrTrackX_ + lightStrTrackW_ &&
            mouseY >= lightStrTrackY_ && mouseY <= lightStrTrackY_ + lightStrTrackH_) {
            float pct = std::clamp((mouseX - lightStrTrackX_) / lightStrTrackW_, 0.0f, 1.0f);
            tempSettings_.lightStrength = pct * 2.0f;
            draggingLightStrength_ = true;
            foley.triggerSound(FoleySound::Flip, 0.65f);
            return true;
        }
    }

    // Footer actions
    const float footerY = dialogY_ + dialogH_ - 44.0f;
    const float resetBtnW = 150.0f;
    const float resetBtnH = 32.0f;
    const float resetBtnX = dialogX_ + 16.0f;
    if (mouseX >= resetBtnX && mouseX <= resetBtnX + resetBtnW && mouseY >= footerY && mouseY <= footerY + resetBtnH) {
        tempSettings_ = AppSettings{};
        seedText_ = std::to_string(tempSettings_.lastSeed);
        board.setCardDeviation(tempSettings_.cardDeviation);
        foley.setMasterVolume(tempSettings_.masterVolume);
        foley.setMuted(tempSettings_.muted);
        foley.triggerSound(FoleySound::WoodClick, 0.8f);
        return true;
    }

    const float applyBtnW = 160.0f;
    const float applyBtnH = 32.0f;
    const float applyBtnX = dialogX_ + dialogW_ - applyBtnW - 16.0f;
    if (mouseX >= applyBtnX && mouseX <= applyBtnX + applyBtnW && mouseY >= footerY && mouseY <= footerY + applyBtnH) {
        try {
            tempSettings_.lastSeed = static_cast<uint32_t>(std::stoul(seedText_));
        } catch (...) {
            tempSettings_.lastSeed = 2026u;
        }
        SettingsManager::instance().settings() = tempSettings_;
        SettingsManager::instance().save();
        if (onSettingsChanged) onSettingsChanged();
        close();
        foley.triggerSound(FoleySound::Sweep, 0.9f);
        return true;
    }

    return true;
}

bool SettingsDialog::handleMouseMove(
    float mouseX,
    float mouseY,
    SolitaireBoard& board,
    AudioFoleyEngine& foley
) {
    if (!isOpen_) return false;
    (void)mouseY;
    if (draggingVolume_ && volTrackW_ > 1.0f) {
        float pct = std::clamp((mouseX - volTrackX_) / volTrackW_, 0.0f, 1.0f);
        tempSettings_.masterVolume = pct;
        foley.setMasterVolume(pct);
        return true;
    }
    if (draggingDeviation_ && devTrackW_ > 1.0f) {
        float dev = std::clamp((mouseX - devTrackX_) / devTrackW_, 0.0f, 1.0f);
        tempSettings_.cardDeviation = dev;
        board.setCardDeviation(dev);
        return true;
    }
    if (draggingDarkness_ && darkTrackW_ > 1.0f) {
        float pct = std::clamp((mouseX - darkTrackX_) / darkTrackW_, 0.0f, 1.0f);
        tempSettings_.sceneDarkness = pct * 0.85f;
        return true;
    }
    if (draggingLightStrength_ && lightStrTrackW_ > 1.0f) {
        float pct = std::clamp((mouseX - lightStrTrackX_) / lightStrTrackW_, 0.0f, 1.0f);
        tempSettings_.lightStrength = pct * 2.0f;
        return true;
    }
    return false;
}

bool SettingsDialog::handleKey(
    int key,
    int action,
    int mods,
    SolitaireBoard& board,
    AudioFoleyEngine& foley
) {
    (void)mods;
    if (!isOpen_) return false;
    if (action != 1 && action != 2) return true; // Press or repeat

    if (key == 256) { // GLFW_KEY_ESCAPE
        board.setCardDeviation(SettingsManager::instance().settings().cardDeviation);
        foley.setMasterVolume(SettingsManager::instance().settings().masterVolume);
        foley.setMuted(SettingsManager::instance().settings().muted);
        close();
        return true;
    }

    if (key == 257 || key == 335) { // GLFW_KEY_ENTER / KP_ENTER
        try {
            tempSettings_.lastSeed = static_cast<uint32_t>(std::stoul(seedText_));
        } catch (...) {}
        SettingsManager::instance().settings() = tempSettings_;
        SettingsManager::instance().save();
        if (onSettingsChanged) onSettingsChanged();
        close();
        foley.triggerSound(FoleySound::Sweep, 0.9f);
        return true;
    }

    if (editingSeed_) {
        if (key == 259) { // GLFW_KEY_BACKSPACE
            if (!seedText_.empty()) {
                seedText_.pop_back();
            }
            return true;
        }
    }

    if (key == 258) { // GLFW_KEY_TAB
        currentTab_ = (currentTab_ + 1) % 4;
        editingSeed_ = false;
        return true;
    }

    return true;
}

bool SettingsDialog::handleChar(unsigned int codepoint) {
    if (!isOpen_ || !editingSeed_) return false;
    if (codepoint >= '0' && codepoint <= '9' && seedText_.size() < 10) {
        seedText_.push_back(static_cast<char>(codepoint));
        return true;
    }
    return false;
}

} // namespace solengine
