#include "solengine/ui/settings_dialog.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/procedural_background.hpp"
#include "solengine/postfx_system.hpp"
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
    CardTextureCache::instance().updatePreviewCard(tempSettings_.deckTheme, tempSettings_.cardWear, tempSettings_.cardStock);
}

void SettingsDialog::close() {
    isOpen_ = false;
    editingSeed_ = false;
    draggingVolume_ = false;
    draggingAmbienceVolume_ = false;
    draggingDeviation_ = false;
    draggingPostFxIntensity_ = false;
    peekingPostFx_ = false;
    draggingDarkness_ = false;
    draggingLightStrength_ = false;
}

void SettingsDialog::revertLiveState(SolitaireBoard& board, AudioFoleyEngine& foley) {
    const auto& s = SettingsManager::instance().settings();
    board.setCardDeviation(s.cardDeviation);
    foley.setMasterVolume(s.masterVolume);
    foley.setMuted(s.muted);
    foley.setAmbienceVolume(s.ambienceVolume);
    if (tempSettings_.soundPackId != s.soundPackId) {
        foley.setSoundPackId(s.soundPackId);
    }
    if (tempSettings_.ambienceId != s.ambienceId) {
        foley.setAmbience(s.ambienceId);
    }

    if (tempSettings_.cardBackId != s.cardBackId) {
        CardTextureCache::instance().setActiveCardBack(s.cardBackId);
    }
    if (tempSettings_.deckTheme != s.deckTheme || tempSettings_.cardWear != s.cardWear || tempSettings_.cardStock != s.cardStock) {
        CardTextureCache::instance().updatePreviewCard(s.deckTheme, s.cardWear, s.cardStock);
    }

    bool themeChanged = (board.tableThemeSlug() != s.tableThemeSlug || board.tableTheme() != s.tableTheme);
    if (tempSettings_.tableTint != s.tableTint) {
        ProceduralBackgroundSystem::instance().setTableTint(s.tableTint);
        ProceduralBackgroundSystem::instance().invalidateCache();
    }
    if (tempSettings_.tableThemeOption != s.tableThemeOption) {
        board.setTableThemeOption(s.tableThemeOption);
        ProceduralBackgroundSystem::instance().setThemeOption(s.tableThemeOption);
        ProceduralBackgroundSystem::instance().invalidateCache();
    }
    if (themeChanged) {
        board.setTableTheme(s.tableTheme);
        board.setTableThemeSlug(s.tableThemeSlug);
        ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
        if (!s.tableThemeSlug.empty()) {
            size_t origIdx = ProceduralBackgroundSystem::instance().findThemeIndex(s.tableThemeSlug, s.tableTheme);
            const auto* def = ProceduralBackgroundSystem::instance().getThemeDef(origIdx);
            if (def && !def->filePath.empty()) {
                ProceduralBackgroundSystem::instance().loadCustomEatsTheme(def->filePath);
            }
        }
        ProceduralBackgroundSystem::instance().invalidateCache();
    }
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

        // SolEngine Signature Clockwork Logo emblem at top-left
        const float logoR = 15.5f;
        const float logoX = dialogX_ + 25.0f;
        const float logoY = dialogY_ + 24.0f;
        clockwork_ui::drawClockworkLogo(r, logoX, logoY, logoR, timeSec);

        eatsbits::ui::glyph::drawVectorText(
            r, "SOLITAIRE ENGINE PREFERENCES & GAME SETTINGS", dialogX_ + 47.0f, dialogY_ + 17.5f, 13.0f,
            cpal.furnaceCoreR, cpal.furnaceCoreG, cpal.furnaceCoreB, 1.0f
        );
    } else {
        r.drawRoundedRect(dialogX_, dialogY_, dialogW_, headerH, 10.0f, pal.woodR0 + 0.02f, pal.woodG0 + 0.01f, pal.woodB0 + 0.01f, 0.95f, 6);
        r.drawLine(dialogX_, dialogY_ + headerH, dialogX_ + dialogW_, dialogY_ + headerH, pal.trimR, pal.trimG, pal.trimB, 0.60f, 1.2f);

        const float logoR = 14.5f;
        const float logoX = dialogX_ + 24.0f;
        const float logoY = dialogY_ + 24.0f;
        clockwork_ui::drawClockworkLogo(r, logoX, logoY, logoR, timeSec);

        eatsbits::ui::glyph::drawVectorText(
            r, "SOLITAIRE ENGINE PREFERENCES & GAME SETTINGS", dialogX_ + 46.0f, dialogY_ + 17.0f, 14.0f,
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
    contentRectX_ = contentX;
    contentRectY_ = contentY;
    contentRectW_ = contentW;
    contentRectH_ = contentH;

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

        // [+] step button
        const float plusX = minusX + stepBtnW + 6.0f;
        bool plusHov = (mouseX >= plusX && mouseX <= plusX + stepBtnW && mouseY >= curY && mouseY <= curY + seedBoxH);

        // [RANDOMIZE] button
        const float randBtnX = plusX + stepBtnW + 12.0f;
        const float randBtnW = 140.0f;
        bool randHov = (mouseX >= randBtnX && mouseX <= randBtnX + randBtnW && mouseY >= curY && mouseY <= curY + seedBoxH);

        if (isClockwork) {
            clockwork_ui::drawPewterButton(r, minusX, curY, stepBtnW, seedBoxH, 4.0f, "-", minusHov, false, 14.0f);
            clockwork_ui::drawPewterButton(r, plusX, curY, stepBtnW, seedBoxH, 4.0f, "+", plusHov, false, 14.0f);
            clockwork_ui::drawBrassButton(r, randBtnX, curY, randBtnW, seedBoxH, 4.0f, "* RANDOMIZE", randHov, false, 11.5f);
        } else {
            r.drawRoundedRect(minusX, curY, stepBtnW, seedBoxH, 4.0f, pal.woodR0 + 0.05f + (minusHov ? 0.08f : 0.0f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
            r.drawRoundedRectOutline(minusX, curY, stepBtnW, seedBoxH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            eatsbits::ui::glyph::drawVectorText(r, "-", minusX + 10.0f, curY + 9.0f, 14.0f, 0.95f, 0.92f, 0.85f, 1.0f);

            r.drawRoundedRect(plusX, curY, stepBtnW, seedBoxH, 4.0f, pal.woodR0 + 0.05f + (plusHov ? 0.08f : 0.0f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
            r.drawRoundedRectOutline(plusX, curY, stepBtnW, seedBoxH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            eatsbits::ui::glyph::drawVectorText(r, "+", plusX + 9.0f, curY + 9.0f, 14.0f, 0.95f, 0.92f, 0.85f, 1.0f);

            r.drawRoundedRect(randBtnX, curY, randBtnW, seedBoxH, 4.0f, pal.woodR0 + 0.08f + (randHov ? 0.10f : 0.0f), pal.woodG0 + 0.07f, pal.woodB0 + 0.05f, 0.95f, 4);
            r.drawRoundedRectOutline(randBtnX, curY, randBtnW, seedBoxH, 4.0f, randHov ? pal.accentR : pal.trimR, randHov ? pal.accentG : pal.trimG, randHov ? pal.accentB : pal.trimB, 0.90f, 1.3f, 4);
            float randTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.5f, "* RANDOMIZE");
            eatsbits::ui::glyph::drawVectorText(r, "* RANDOMIZE", randBtnX + (randBtnW - randTxtW) * 0.5f, curY + 9.0f, 11.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }

        curY += 42.0f;

        // "Randomize seed on New Deal" toggle
        const float checkSize = 18.0f;
        const float checkX = contentX + 16.0f;
        r.drawRoundedRect(checkX, curY, checkSize, checkSize, 3.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
        r.drawRoundedRectOutline(checkX, curY, checkSize, checkSize, 3.0f,
                                 isClockwork ? cpal.brassBrightR : pal.trimR,
                                 isClockwork ? cpal.brassBrightG : pal.trimG,
                                 isClockwork ? cpal.brassBrightB : pal.trimB, 0.80f, 1.2f, 4);
        if (tempSettings_.randomizeOnNewDeal) {
            eatsbits::ui::glyph::drawVectorText(r, "X", checkX + 5.0f, curY + 2.5f, 12.0f,
                                                isClockwork ? cpal.brassHighlightR : pal.accentR,
                                                isClockwork ? cpal.brassHighlightG : pal.accentG,
                                                isClockwork ? cpal.brassHighlightB : pal.accentB, 1.0f);
        }
        eatsbits::ui::glyph::drawVectorText(r, "Auto-randomize seed on every 'New Deal' action", checkX + 26.0f, curY + 4.0f, 11.5f, 0.92f, 0.90f, 0.85f, 1.0f);

        curY += 28.0f;

        // "Guarantee Solvable Deals" toggle
        r.drawRoundedRect(checkX, curY, checkSize, checkSize, 3.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
        r.drawRoundedRectOutline(checkX, curY, checkSize, checkSize, 3.0f,
                                 isClockwork ? cpal.brassBrightR : pal.trimR,
                                 isClockwork ? cpal.brassBrightG : pal.trimG,
                                 isClockwork ? cpal.brassBrightB : pal.trimB, 0.80f, 1.2f, 4);
        if (tempSettings_.guaranteeSolvable) {
            eatsbits::ui::glyph::drawVectorText(r, "X", checkX + 5.0f, curY + 2.5f, 12.0f,
                                                isClockwork ? cpal.brassHighlightR : pal.accentR,
                                                isClockwork ? cpal.brassHighlightG : pal.accentG,
                                                isClockwork ? cpal.brassHighlightB : pal.accentB, 1.0f);
        }
        eatsbits::ui::glyph::drawVectorText(r, "Guarantee solvable deals (analyzes deal before starting)", checkX + 26.0f, curY + 4.0f, 11.5f, 0.92f, 0.90f, 0.85f, 1.0f);

        curY += 34.0f;

        // "GAME LIBRARY & FAVORITES" section
        drawSectionHeader("GAME LIBRARY & FAVORITE GAMES");

        const float libBtnW = std::min(220.0f, contentW - 32.0f);
        const float libBtnH = 30.0f;
        const float libBtnX = contentX + 16.0f;
        bool libHov = (mouseX >= libBtnX && mouseX <= libBtnX + libBtnW && mouseY >= curY && mouseY <= curY + libBtnH);

        if (isClockwork) {
            clockwork_ui::drawBrassButton(r, libBtnX, curY, libBtnW, libBtnH, 4.0f, "BROWSE ALL GAMES...", libHov, false, 11.5f);
        } else {
            r.drawRoundedRect(libBtnX, curY, libBtnW, libBtnH, 4.0f,
                              pal.woodR0 + 0.08f + (libHov ? 0.10f : 0.0f),
                              pal.woodG0 + 0.06f + (libHov ? 0.08f : 0.0f),
                              pal.woodB0 + 0.04f + (libHov ? 0.06f : 0.0f), 0.96f, 4);
            r.drawRoundedRectOutline(libBtnX, curY, libBtnW, libBtnH, 4.0f,
                                     libHov ? pal.accentR : pal.trimR,
                                     libHov ? pal.accentG : pal.trimG,
                                     libHov ? pal.accentB : pal.trimB, 0.90f, 1.3f, 4);
            float libTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.5f, "BROWSE ALL GAMES...");
            eatsbits::ui::glyph::drawVectorText(r, "BROWSE ALL GAMES...", libBtnX + (libBtnW - libTxtW) * 0.5f, curY + 9.0f, 11.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }

        const auto* activeDef = GameRegistry::instance().findByVariant(tempSettings_.variant);
        std::string activeLabel = std::string("ACTIVE: ") + (activeDef ? activeDef->name : "Klondike (Draw 1)");
        eatsbits::ui::glyph::drawVectorText(r, activeLabel.c_str(), libBtnX + libBtnW + 14.0f, curY + 9.0f, 10.5f, 0.85f, 0.82f, 0.76f, 0.95f);

        curY += libBtnH + 12.0f;

        // Sleek grid of favorited games (up to 6)
        const auto& favIds = SettingsManager::instance().settings().favorites;
        std::vector<const SolitaireGameDef*> favDefs;
        favDefs.reserve(6);
        for (const auto& id : favIds) {
            if (const auto* def = GameRegistry::instance().findById(id)) {
                favDefs.push_back(def);
                if (favDefs.size() >= 6) break;
            }
        }

        if (favDefs.empty()) {
            eatsbits::ui::glyph::drawVectorText(
                r, "No favorite games pinned yet. Star games in Browse All Games!",
                contentX + 16.0f, curY + 6.0f, 10.5f, 0.72f, 0.70f, 0.65f, 0.85f
            );
        } else {
            const float favBtnW = (contentW - 48.0f) * 0.5f;
            const float favBtnH = 30.0f;
            for (size_t i = 0; i < favDefs.size(); ++i) {
                const auto* def = favDefs[i];
                float fx = contentX + 16.0f + (static_cast<int>(i % 2) * (favBtnW + 16.0f));
                float fy = curY + static_cast<int>(i / 2) * (favBtnH + 8.0f);
                bool isSel = (def->nativeVariant == tempSettings_.variant);
                bool fHov = (mouseX >= fx && mouseX <= fx + favBtnW && mouseY >= fy && mouseY <= fy + favBtnH);

                std::string label = "* " + def->name;
                if (isClockwork) {
                    clockwork_ui::drawPewterButton(r, fx, fy, favBtnW, favBtnH, 4.0f, label.c_str(), fHov, isSel, 10.5f);
                } else {
                    r.drawRoundedRect(fx, fy, favBtnW, favBtnH, 4.0f,
                                      isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (fHov ? 0.06f : 0.0f)),
                                      pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                    r.drawRoundedRectOutline(fx, fy, favBtnW, favBtnH, 4.0f,
                                             isSel ? pal.accentR : pal.trimR,
                                             isSel ? pal.accentG : pal.trimG,
                                             isSel ? pal.accentB : pal.trimB,
                                             isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                    eatsbits::ui::glyph::drawVectorText(r, label.c_str(), fx + 10.0f, fy + 9.0f, 10.5f,
                                                        isSel ? pal.accentR : 0.92f,
                                                        isSel ? pal.accentG : 0.88f,
                                                        isSel ? pal.accentB : 0.80f, 1.0f);
                }
            }
        }
    } else if (currentTab_ == 1) {
        // --- TAB 1: THEMES & PHYSICAL FINISHES (Scrollable on compact/mobile viewports) ---
        auto scissor = r.makeScissorScope(contentX + 2.0f, contentY + 2.0f, contentW - 4.0f, contentH - 4.0f);
        const bool mouseInContent = (mouseX >= contentX && mouseX <= contentX + contentW && mouseY >= contentY && mouseY <= contentY + contentH);
        const float startCurY = curY;
        curY -= themeScrollY_;

        const bool useTwoCol = (dialogW_ >= 420.0f);
        const float col1W = useTwoCol ? (contentW * (dialogW_ < 540.0f ? 0.60f : 0.56f)) : contentW;
        const float rowX = contentX + 16.0f;
        const float rowW = useTwoCol ? (col1W - 12.0f) : (contentW - 36.0f);
        const float rowH = 28.0f;
        const float btnW = 32.0f;
        const float rowGap = 5.0f;
        const float labelX = rowX + btnW + 6.0f;
        const float labelW = rowW - 2.0f * (btnW + 6.0f);
        const float nextX = rowX + rowW - btnW;

        auto drawStepper = [&](const std::string& text) {
            bool prevHov = mouseInContent && (mouseX >= rowX && mouseX <= rowX + btnW && mouseY >= curY && mouseY <= curY + rowH);
            bool nextHov = mouseInContent && (mouseX >= nextX && mouseX <= nextX + btnW && mouseY >= curY && mouseY <= curY + rowH);
            bool centerHov = mouseInContent && (mouseX >= labelX && mouseX <= labelX + labelW && mouseY >= curY && mouseY <= curY + rowH);

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

        // 1b. Table Theme Option (displays sub-theme options if provided by the theme)
        const TableThemeDef* curThemeDef = (curThemeIdx < availableThemes.size()) ? &availableThemes[curThemeIdx] : nullptr;
        bool hasThemeOptions = (curThemeDef != nullptr && !curThemeDef->options.empty());
        if (hasThemeOptions) {
            std::string optLabel = curThemeDef->optionLabel.empty() ? "Option" : curThemeDef->optionLabel;
            size_t optIdx = 0;
            const auto& opts = curThemeDef->options;
            for (size_t i = 0; i < opts.size(); ++i) {
                if (opts[i].id == tempSettings_.tableThemeOption) {
                    optIdx = i;
                    break;
                }
            }
            std::string optStr = optLabel + ": " + opts[optIdx].name;
            drawStepper(optStr);
            curY += rowH + rowGap;
        }

        // 2. Deck Theme
        std::string deckStr = "Deck: " + std::string(CardTextureCache::deckThemeName(tempSettings_.deckTheme));
        drawStepper(deckStr);
        curY += rowH + rowGap;

        // 2a. Card Back
        std::string backName = CardTextureCache::instance().getCardBackDisplayName(tempSettings_.cardBackId);
        std::string backStr = "Card Back: " + backName;
        drawStepper(backStr);
        curY += rowH + rowGap;

        // 2b. UI Theme
        std::string uiThemeStr = "UI Theme: " + std::string(tempSettings_.guiTheme == GuiThemeStyle::ClockworkEngine ? "CLOCKWORK ENGINE" : "CLASSIC");
        drawStepper(uiThemeStr);
        curY += rowH + 8.0f;

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
        curY += rowH + 8.0f;

        // Section: Card Deviation
        drawSectionHeader("Card Deviation");
        devTrackX_ = rowX;
        devTrackW_ = useTwoCol ? (rowW - 74.0f) : (contentW - 148.0f);
        devTrackH_ = 28.0f;
        devTrackY_ = curY;

        int devPct = static_cast<int>(std::round(tempSettings_.cardDeviation * 100.0f));
        const float devBtnX = devTrackX_ + devTrackW_ + (useTwoCol ? 6.0f : 12.0f);
        const float devBtnW = useTwoCol ? 68.0f : 104.0f;
        bool devBtnHov = (mouseX >= devBtnX && mouseX <= devBtnX + devBtnW && mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_);
        const char* devBtnLabel = (tempSettings_.cardDeviation <= 0.005f) ? (useTwoCol ? "25%" : "PRESET 25%") : (useTwoCol ? "0%" : "ZERO 0%");

        if (isClockwork) {
            bool knobHovered = (mouseX >= devTrackX_ && mouseX <= devTrackX_ + devTrackW_ && mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_);
            clockwork_ui::drawGlassSlider(r, devTrackX_, devTrackY_, devTrackW_, devTrackH_, tempSettings_.cardDeviation, nullptr, knobHovered, 0.0f);

            char devStr[80];
            if (tempSettings_.cardDeviation <= 0.005f) {
                std::snprintf(devStr, sizeof(devStr), "DEV: 0%% (NEAT)");
            } else {
                std::snprintf(devStr, sizeof(devStr), "DEV: %d%%", devPct);
            }
            eatsbits::ui::glyph::drawVectorText(r, devStr, devTrackX_ + 10.0f, devTrackY_ + 8.5f, 10.5f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
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
                std::snprintf(devStr, sizeof(devStr), "DEV: 0%% (NEAT)");
            } else {
                std::snprintf(devStr, sizeof(devStr), "DEV: %d%%", devPct);
            }
            eatsbits::ui::glyph::drawVectorText(r, devStr, devTrackX_ + 8.0f, devTrackY_ + 8.5f, 10.5f, 0.98f, 0.95f, 0.88f, 1.0f);

            r.drawRoundedRect(devBtnX, devTrackY_, devBtnW, devTrackH_, 4.0f,
                              pal.woodR0 + 0.06f + (devBtnHov ? 0.08f : 0.0f), pal.woodG0 + 0.05f, pal.woodB0 + 0.04f, 0.95f, 4);
            r.drawRoundedRectOutline(devBtnX, devTrackY_, devBtnW, devTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float devBtnTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, devBtnLabel);
            eatsbits::ui::glyph::drawVectorText(r, devBtnLabel, devBtnX + (devBtnW - devBtnTxtW) * 0.5f, devTrackY_ + 8.5f, 10.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        }
        curY += devTrackH_ + 10.0f;

        // Section: Post-Processing
        drawSectionHeader("Post-Processing");
        (void)PostFxSystem::instance().availableEffects();
        size_t curPfxIdx = PostFxSystem::instance().findEffectIndex(tempSettings_.postFxId);
        const PostFxDef* curPfxDef = PostFxSystem::instance().getEffectDef(curPfxIdx);
        std::string pfxTitle = curPfxDef ? curPfxDef->name : "None (Off)";
        drawStepper("Post-FX: " + pfxTitle);
        curY += rowH + rowGap;

        if (curPfxDef && !curPfxDef->options.empty()) {
            std::string optLabel = curPfxDef->optionLabel.empty() ? "Preset" : curPfxDef->optionLabel;
            size_t optIdx = 0;
            for (size_t i = 0; i < curPfxDef->options.size(); ++i) {
                if (curPfxDef->options[i].id == tempSettings_.postFxOption) {
                    optIdx = i;
                    break;
                }
            }
            drawStepper(optLabel + ": " + curPfxDef->options[optIdx].name);
            curY += rowH + rowGap;
        }

        if (curPfxDef && curPfxDef->id != "none") {
            postFxTrackX_ = rowX;
            postFxTrackW_ = useTwoCol ? (rowW - 74.0f) : (contentW - 148.0f);
            postFxTrackH_ = 26.0f;
            postFxTrackY_ = curY;
            const float peekBtnX = postFxTrackX_ + postFxTrackW_ + (useTwoCol ? 6.0f : 12.0f);
            const float peekBtnW = useTwoCol ? 68.0f : 104.0f;
            bool peekHov = (mouseX >= peekBtnX && mouseX <= peekBtnX + peekBtnW &&
                            mouseY >= postFxTrackY_ && mouseY <= postFxTrackY_ + postFxTrackH_);
            int pfxPct = static_cast<int>(std::round(tempSettings_.postFxIntensity * 100.0f));
            char pfxStr[48];
            std::snprintf(pfxStr, sizeof(pfxStr), "MIX: %d%%", pfxPct);
            if (isClockwork) {
                bool knobHov = (mouseX >= postFxTrackX_ && mouseX <= postFxTrackX_ + postFxTrackW_ &&
                                mouseY >= postFxTrackY_ && mouseY <= postFxTrackY_ + postFxTrackH_);
                clockwork_ui::drawGlassSlider(r, postFxTrackX_, postFxTrackY_, postFxTrackW_, postFxTrackH_,
                                              tempSettings_.postFxIntensity, nullptr, knobHov, 0.0f);
                eatsbits::ui::glyph::drawVectorText(r, pfxStr, postFxTrackX_ + 10.0f, postFxTrackY_ + 7.5f, 10.5f,
                                                    cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
                clockwork_ui::drawPewterButton(r, peekBtnX, postFxTrackY_, peekBtnW, postFxTrackH_, 4.0f,
                                               peekingPostFx_ ? "RAW" : "PEEK", peekHov, peekingPostFx_, 10.5f);
            } else {
                r.drawRoundedRect(postFxTrackX_, postFxTrackY_, postFxTrackW_, postFxTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
                r.drawRoundedRectOutline(postFxTrackX_, postFxTrackY_, postFxTrackW_, postFxTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
                float fillW = (postFxTrackW_ - 4.0f) * tempSettings_.postFxIntensity;
                if (fillW > 0.0f) {
                    r.drawRoundedRect(postFxTrackX_ + 2.0f, postFxTrackY_ + 2.0f, fillW, postFxTrackH_ - 4.0f, 3.0f,
                                      pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
                }
                eatsbits::ui::glyph::drawVectorText(r, pfxStr, postFxTrackX_ + 8.0f, postFxTrackY_ + 7.5f, 10.5f, 0.98f, 0.95f, 0.88f, 1.0f);
                r.drawRoundedRect(peekBtnX, postFxTrackY_, peekBtnW, postFxTrackH_, 4.0f,
                                  pal.woodR0 + 0.06f + (peekHov ? 0.08f : 0.0f), pal.woodG0 + 0.05f, pal.woodB0 + 0.04f, 0.95f, 4);
                r.drawRoundedRectOutline(peekBtnX, postFxTrackY_, peekBtnW, postFxTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
                const char* peekLbl = peekingPostFx_ ? "RAW" : "PEEK";
                float peekTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, peekLbl);
                eatsbits::ui::glyph::drawVectorText(r, peekLbl, peekBtnX + (peekBtnW - peekTxtW) * 0.5f, postFxTrackY_ + 7.5f, 10.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
            }
            curY += postFxTrackH_ + rowGap;

            postFxAffectUiY_ = curY;
            bool affHov = (mouseX >= rowX && mouseX <= rowX + rowW && mouseY >= curY && mouseY <= curY + rowH);
            std::string affStr = std::string("Affect UI / Menus: ") + (tempSettings_.postFxAffectUi ? "ON" : "OFF");
            if (isClockwork) {
                clockwork_ui::drawPewterButton(r, rowX, curY, rowW, rowH, 4.0f, affStr.c_str(), affHov, tempSettings_.postFxAffectUi, 10.5f);
            } else {
                r.drawRoundedRect(rowX, curY, rowW, rowH, 4.0f,
                                  pal.woodR0 + 0.04f + (affHov ? 0.06f : 0.0f), pal.woodG0 + 0.03f, pal.woodB0 + 0.03f, 0.92f, 4);
                r.drawRoundedRectOutline(rowX, curY, rowW, rowH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, affStr.c_str(), rowX + 10.0f, curY + 8.0f, 10.5f, 0.92f, 0.88f, 0.82f, 1.0f);
            }
            curY += rowH + rowGap;
        }

        // --- SAMPLE AREA (Real-time Shader Stock & Wear) ---
        const float col2X = useTwoCol ? (contentX + col1W + 4.0f) : (contentX + 16.0f);
        const float col2W = useTwoCol ? (contentW - col1W - 8.0f) : (contentW - 36.0f);
        const float col2Y = useTwoCol ? (startCurY - 2.0f - themeScrollY_) : (curY + 38.0f);

        // Header: "Sample"
        const char* titleStr = "Sample";
        float titleW = eatsbits::ui::glyph::getVectorTextWidth(11.5f, titleStr);
        eatsbits::ui::glyph::drawVectorText(
            r, titleStr, col2X + (col2W - titleW) * 0.5f, col2Y + 4.0f,
            11.5f, isClockwork ? cpal.brassHighlightR : pal.accentR,
            isClockwork ? cpal.brassHighlightG : pal.accentG,
            isClockwork ? cpal.brassHighlightB : pal.accentB, 1.0f
        );

        // Preview Card: Centered single deck card (Queen of Spades)
        const float previewCardW = std::clamp(col2W * (useTwoCol ? 0.48f : 0.28f), 52.0f, 88.0f);
        const float previewCardH = std::round(previewCardW * 1.40f);
        const float cardX = col2X + (col2W - previewCardW) * 0.5f;
        const float cardY = col2Y + 24.0f;

        float cornerR = 24.0f;
        switch (tempSettings_.cardCorners) {
            case CardCornerStyle::Sharp:   cornerR = 0.0f; break;
            case CardCornerStyle::Vintage: cornerR = 8.0f; break;
            case CardCornerStyle::Round:   cornerR = 24.0f; break;
            case CardCornerStyle::Auto:
            default:
                cornerR = CardTextureCache::instance().deckCornerRadius();
                break;
        }
        const float effRu = cornerR / static_cast<float>(CardTextureCache::kDefaultCardW);
        const float previewRadius = std::max(0.0f, effRu * previewCardW);

        const float previewCond = static_cast<float>(tempSettings_.cardWear) + 10.0f * static_cast<float>(tempSettings_.cardStock);
        const float previewScratch = (tempSettings_.cardWear == CardWearLevel::Played) ? 0.0018f
                                   : (tempSettings_.cardWear == CardWearLevel::Vintage) ? 0.0035f : 0.0f;

        eatsbits::ui::LightSource2D pLight{cardX + previewCardW * 0.5f, cardY - 60.0f, 300.0f};

        // Draw Staggered Card Back (+3 deg) and Card Front (-3 deg)
        // 1. Card Back in back (staggered slightly right/up, rotated +3 deg)
        const float backX = cardX + 14.0f;
        const float backY = cardY - 2.0f;
        const float backCx = backX + previewCardW * 0.5f;
        const float backCy = backY + previewCardH * 0.5f;
        eatsbits::ui::LightSource2D pLightBack{backCx, backY - 60.0f, 300.0f};

        r.setRotation(3.0f, backCx, backCy);
        r.drawRectDropShadow(backX, backY, previewCardW, previewCardH, previewRadius, 6.0f, pLightBack, 0.45f);
        if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().hasGpuAtlas()) {
            const auto& slotBack = CardTextureCache::instance().getBackAtlasSlot();
            r.drawTexturedRoundedRectGpu(backX, backY, previewCardW, previewCardH, previewRadius,
                                         slotBack.u0, slotBack.v0, slotBack.u1, slotBack.v1,
                                         1.0f, 1.0f, 1.0f, 1.0f, previewScratch, previewCond);
        } else {
            const CardTexture* bTex = CardTextureCache::instance().getCardBackTexture();
            if (bTex && !bTex->argb32.empty()) {
                r.drawTexturedRoundedRect(backX, backY, previewCardW, previewCardH, previewRadius,
                                          bTex->argb32.data(), bTex->width, bTex->height, 1.0f);
            }
        }
        r.drawRoundedRectOutline(backX, backY, previewCardW, previewCardH, previewRadius,
                                 0.20f, 0.18f, 0.14f, 0.75f, 1.1f, 4);
        r.resetRotation();

        // 2. Card Front in front (staggered slightly left/down, rotated -3 deg)
        const float frontX = cardX - 10.0f;
        const float frontY = cardY + 2.0f;
        const float frontCx = frontX + previewCardW * 0.5f;
        const float frontCy = frontY + previewCardH * 0.5f;
        eatsbits::ui::LightSource2D pLightFront{frontCx, frontY - 60.0f, 300.0f};

        r.setRotation(-3.0f, frontCx, frontCy);
        r.drawRectDropShadow(frontX, frontY, previewCardW, previewCardH, previewRadius, 8.0f, pLightFront, 0.55f);
        if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().hasGpuAtlas()) {
            const auto& slotFront = CardTextureCache::instance().getPreviewFaceAtlasSlot();
            r.drawTexturedRoundedRectGpu(frontX, frontY, previewCardW, previewCardH, previewRadius,
                                         slotFront.u0, slotFront.v0, slotFront.u1, slotFront.v1,
                                         1.0f, 1.0f, 1.0f, 1.0f, previewScratch, previewCond);
        } else {
            const CardTexture* fTex = CardTextureCache::instance().getPreviewFaceTexture();
            if (fTex && !fTex->argb32.empty()) {
                r.drawTexturedRoundedRect(frontX, frontY, previewCardW, previewCardH, previewRadius,
                                          fTex->argb32.data(), fTex->width, fTex->height, 1.0f);
            }
        }
        r.drawRoundedRectOutline(frontX, frontY, previewCardW, previewCardH, previewRadius,
                                 0.20f, 0.18f, 0.14f, 0.75f, 1.1f, 4);
        r.resetRotation();

        // Centered Details Badges below card
        float infoY = cardY + previewCardH + 12.0f;
        std::string badgeStock = "STOCK: " + std::string(CardTextureCache::cardStockName(tempSettings_.cardStock));
        std::string badgeWear  = "WEAR: " + std::string(CardTextureCache::cardWearLevelName(tempSettings_.cardWear));
        std::string badgeCorn  = "CORNERS: " + std::string(cornerStyleCleanName(tempSettings_.cardCorners));

        float bStockW = eatsbits::ui::glyph::getVectorTextWidth(8.5f, badgeStock.c_str());
        float bWearW  = eatsbits::ui::glyph::getVectorTextWidth(8.5f, badgeWear.c_str());
        float bCornW  = eatsbits::ui::glyph::getVectorTextWidth(8.5f, badgeCorn.c_str());

        eatsbits::ui::glyph::drawVectorText(r, badgeStock.c_str(), col2X + (col2W - bStockW) * 0.5f, infoY, 8.5f,
                                            isClockwork ? cpal.furnaceAmberR : pal.accentR,
                                            isClockwork ? cpal.furnaceAmberG : pal.accentG,
                                            isClockwork ? cpal.furnaceAmberB : pal.accentB, 0.95f);
        eatsbits::ui::glyph::drawVectorText(r, badgeWear.c_str(),  col2X + (col2W - bWearW) * 0.5f,  infoY + 12.0f, 8.5f,
                                            0.88f, 0.85f, 0.80f, 0.90f);
        eatsbits::ui::glyph::drawVectorText(r, badgeCorn.c_str(),  col2X + (col2W - bCornW) * 0.5f,  infoY + 24.0f, 8.5f,
                                            0.80f, 0.78f, 0.74f, 0.85f);

        // Compute scrollable height & clamp themeScrollY_
        const float col1Bottom = curY + 34.0f + themeScrollY_;
        const float col2Bottom = infoY + 42.0f + themeScrollY_;
        const float totalBottom = std::max(col1Bottom, col2Bottom);
        themeMaxScrollY_ = std::max(0.0f, totalBottom - (contentY + contentH));
        themeScrollY_ = std::clamp(themeScrollY_, 0.0f, themeMaxScrollY_);

        // Subtle vertical scrollbar indicator when content overflows
        if (themeMaxScrollY_ > 1.0f) {
            const float sbTrackY = contentY + 8.0f;
            const float sbTrackH = std::max(24.0f, contentH - 16.0f);
            const float visibleRatio = std::clamp(contentH / std::max(1.0f, totalBottom - contentY), 0.15f, 1.0f);
            const float thumbH = std::max(24.0f, sbTrackH * visibleRatio);
            const float scrollFrac = themeScrollY_ / themeMaxScrollY_;
            const float thumbY = sbTrackY + (sbTrackH - thumbH) * scrollFrac;

            themeSbTrackY_ = sbTrackY;
            themeSbTrackH_ = sbTrackH;
            themeSbThumbH_ = thumbH;

            const bool sbHov = (mouseX >= contentX + contentW - 22.0f && mouseX <= contentX + contentW &&
                                mouseY >= sbTrackY && mouseY <= sbTrackY + sbTrackH) || draggingThemeScrollbar_;
            const float sbW = sbHov ? 7.0f : 5.0f;
            const float sbX = contentX + contentW - sbW - 4.0f;
            r.drawRoundedRect(sbX, sbTrackY, sbW, sbTrackH, sbW * 0.5f, 0.08f, 0.07f, 0.06f, sbHov ? 0.65f : 0.45f, 2);
            r.drawRoundedRect(sbX, thumbY, sbW, thumbH, sbW * 0.5f,
                              isClockwork ? cpal.brassBrightR : pal.accentR,
                              isClockwork ? cpal.brassBrightG : pal.accentG,
                              isClockwork ? cpal.brassBrightB : pal.accentB, sbHov ? 0.95f : 0.78f, 2);
        }
    } else if (currentTab_ == 2) {
        // --- TAB 2: DISPLAY & RESPONSIVE ---
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
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, lx, curY, lBtnW, 28.0f, 4.0f, layoutModes[m], lHov, isSel, 10.5f);
            } else {
                r.drawRoundedRect(lx, curY, lBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (lHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(lx, curY, lBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, layoutModes[m], lx + 12.0f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
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
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, sx, curY, sBtnW, 28.0f, 4.0f, stockPosLabels[s], sHov, isSel, 9.5f);
            } else {
                r.drawRoundedRect(sx, curY, sBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (sHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(sx, curY, sBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, stockPosLabels[s], sx + 6.0f, curY + 8.0f, 9.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }

        curY += 38.0f;

        drawSectionHeader("HUD DOCK ANCHOR");
        const char* anchors[4] = { "BOT-LEFT", "BOT-RIGHT", "TOP-LEFT", "TOP-RIGHT" };
        const float anchorBtnW = (contentW - 48.0f) / 4.0f;
        for (int a = 0; a < 4; ++a) {
            float ax = contentX + 16.0f + a * (anchorBtnW + 8.0f);
            bool isSel = (static_cast<int>(tempSettings_.menuAnchor) == a);
            bool aHov = (mouseX >= ax && mouseX <= ax + anchorBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, ax, curY, anchorBtnW, 28.0f, 4.0f, anchors[a], aHov, isSel, 10.5f);
            } else {
                r.drawRoundedRect(ax, curY, anchorBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (aHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(ax, curY, anchorBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, anchors[a], ax + 14.0f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }

        curY += 38.0f;

        drawSectionHeader("TABLETOP GUIDES & PILE MARKINGS");
        const char* guideModes[2] = { "GUIDES & RUNES (DEFAULT)", "BARE TABLETOP (CARDS ONLY)" };
        const float gBtnW = (contentW - 40.0f) * 0.5f;
        for (int g = 0; g < 2; ++g) {
            float gx = contentX + 16.0f + g * (gBtnW + 8.0f);
            bool isSel = (g == 0 ? tempSettings_.showTableMarkings : !tempSettings_.showTableMarkings);
            bool gHov = (mouseX >= gx && mouseX <= gx + gBtnW && mouseY >= curY && mouseY <= curY + 28.0f);
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, gx, curY, gBtnW, 28.0f, 4.0f, guideModes[g], gHov, isSel, 10.5f);
            } else {
                r.drawRoundedRect(gx, curY, gBtnW, 28.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (gHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(gx, curY, gBtnW, 28.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                float gTextW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, guideModes[g]);
                eatsbits::ui::glyph::drawVectorText(r, guideModes[g], gx + (gBtnW - gTextW) * 0.5f, curY + 8.0f, 10.5f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }

        curY += 34.0f;
        eatsbits::ui::glyph::drawVectorText(r, "Hides pile wells, suit runes, and board guides for a pure real-world card table feel.", contentX + 16.0f, curY, 9.5f, 0.70f, 0.68f, 0.62f, 0.85f);
    } else {
        // --- TAB 3: AUDIO & LIGHT ---
        drawSectionHeader("PROCEDURAL FOLEY & AMBIENCE MIX");

        // Master Volume, Ambience Volume, and Mute toggle on one clean row
        const float muteBtnW = 92.0f;
        const float mixGap = 10.0f;
        const float availMixW = contentW - 32.0f - muteBtnW - mixGap * 2.0f;
        const float eachSliderW = availMixW * 0.5f;

        volTrackX_ = contentX + 16.0f;
        volTrackW_ = eachSliderW;
        volTrackH_ = 24.0f;
        volTrackY_ = curY;

        ambVolTrackX_ = volTrackX_ + volTrackW_ + mixGap;
        ambVolTrackW_ = eachSliderW;
        ambVolTrackH_ = 24.0f;
        ambVolTrackY_ = curY;

        int volPct = static_cast<int>(std::round(tempSettings_.masterVolume * 100.0f));
        std::string volStr = "MASTER: " + std::to_string(volPct) + "%";

        int ambPct = static_cast<int>(std::round(tempSettings_.ambienceVolume * 100.0f));
        std::string ambStr = "AMBIENCE: " + std::to_string(ambPct) + "%";

        if (isClockwork) {
            bool volHov = (mouseX >= volTrackX_ && mouseX <= volTrackX_ + volTrackW_ && mouseY >= volTrackY_ && mouseY <= volTrackY_ + volTrackH_);
            clockwork_ui::drawGlassSlider(r, volTrackX_, volTrackY_, volTrackW_, volTrackH_, tempSettings_.masterVolume, nullptr, volHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, volStr.c_str(), volTrackX_ + 12.0f, volTrackY_ + 6.5f, 10.0f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);

            bool ambHov = (mouseX >= ambVolTrackX_ && mouseX <= ambVolTrackX_ + ambVolTrackW_ && mouseY >= ambVolTrackY_ && mouseY <= ambVolTrackY_ + ambVolTrackH_);
            clockwork_ui::drawGlassSlider(r, ambVolTrackX_, ambVolTrackY_, ambVolTrackW_, ambVolTrackH_, tempSettings_.ambienceVolume, nullptr, ambHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, ambStr.c_str(), ambVolTrackX_ + 12.0f, ambVolTrackY_ + 6.5f, 10.0f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
        } else {
            r.drawRoundedRect(volTrackX_, volTrackY_, volTrackW_, volTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(volTrackX_, volTrackY_, volTrackW_, volTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float fillW = (volTrackW_ - 4.0f) * tempSettings_.masterVolume;
            r.drawRoundedRect(volTrackX_ + 2.0f, volTrackY_ + 2.0f, fillW, volTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, volStr.c_str(), volTrackX_ + 10.0f, volTrackY_ + 6.5f, 10.0f, 0.98f, 0.95f, 0.88f, 1.0f);

            r.drawRoundedRect(ambVolTrackX_, ambVolTrackY_, ambVolTrackW_, ambVolTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(ambVolTrackX_, ambVolTrackY_, ambVolTrackW_, ambVolTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float ambFillW = (ambVolTrackW_ - 4.0f) * tempSettings_.ambienceVolume;
            r.drawRoundedRect(ambVolTrackX_ + 2.0f, ambVolTrackY_ + 2.0f, ambFillW, ambVolTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, ambStr.c_str(), ambVolTrackX_ + 10.0f, ambVolTrackY_ + 6.5f, 10.0f, 0.98f, 0.95f, 0.88f, 1.0f);
        }

        // Mute toggle button
        const float muteBtnX = ambVolTrackX_ + ambVolTrackW_ + mixGap;
        bool muteHov = (mouseX >= muteBtnX && mouseX <= muteBtnX + muteBtnW && mouseY >= curY && mouseY <= curY + volTrackH_);
        const char* muteLabel = tempSettings_.muted ? "MUTED" : "UNMUTED";
        if (isClockwork) {
            clockwork_ui::drawPewterButton(r, muteBtnX, curY, muteBtnW, volTrackH_, 4.0f, muteLabel, muteHov, tempSettings_.muted, 10.0f);
        } else {
            r.drawRoundedRect(muteBtnX, curY, muteBtnW, volTrackH_, 4.0f, pal.woodR0 + 0.06f + (muteHov ? 0.08f : 0.0f), pal.woodG0 + 0.05f, pal.woodB0 + 0.04f, 0.95f, 4);
            r.drawRoundedRectOutline(muteBtnX, curY, muteBtnW, volTrackH_, 4.0f, tempSettings_.muted ? 0.85f : pal.trimR, tempSettings_.muted ? 0.35f : pal.trimG, tempSettings_.muted ? 0.35f : pal.trimB, 0.80f, 1.2f, 4);
            float muteTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, muteLabel);
            eatsbits::ui::glyph::drawVectorText(r, muteLabel, muteBtnX + (muteBtnW - muteTxtW) * 0.5f, curY + (volTrackH_ - 10.0f) * 0.5f, 10.0f, tempSettings_.muted ? 0.90f : 0.95f, tempSettings_.muted ? 0.45f : 0.92f, tempSettings_.muted ? 0.45f : 0.85f, 1.0f);
        }

        curY += 32.0f;

        // UI Sound Set (/sounds/*.eats)
        drawSectionHeader("UI FOLEY SOUND SET (/SOUNDS)");
        struct SoundPackOption { const char* id; const char* label; };
        const SoundPackOption soundPacks[9] = {
            { "default",         "DEFAULT" },
            { "authentic_place", "AUTHENTIC" },
            { "copag_plastic",   "COPAG" },
            { "vintage_bicycle", "BICYCLE" },
            { "vegas_linen",     "LINEN" },
            { "bass_heavy",      "BASS" },
            { "glass_crystal",   "CRYSTAL" },
            { "futuristic",      "FUTURISTIC" },
            { "retro_8bit",      "8-BIT" }
        };
        const float packBtnW = (contentW - 64.0f) / 5.0f;
        for (int i = 0; i < 9; ++i) {
            int row = i / 5;
            int col = i % 5;
            float px = contentX + 16.0f + col * (packBtnW + 8.0f);
            float py = curY + row * 27.0f;
            bool isSel = (tempSettings_.soundPackId == soundPacks[i].id);
            bool pHov = (mouseX >= px && mouseX <= px + packBtnW && mouseY >= py && mouseY <= py + 23.0f);
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, px, py, packBtnW, 23.0f, 4.0f, soundPacks[i].label, pHov, isSel, 9.0f);
            } else {
                r.drawRoundedRect(px, py, packBtnW, 23.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (pHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(px, py, packBtnW, 23.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, soundPacks[i].label, px + 8.0f, py + 6.5f, 9.0f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }
        curY += 58.0f;

        // Procedural Environmental Ambience (/ambience/*.eats)
        drawSectionHeader("PROCEDURAL AMBIENCE ENVIRONMENT (/AMBIENCE)");
        struct AmbienceOption { const char* id; const char* label; };
        const AmbienceOption ambOptions[8] = {
            { "off",        "OFF" },
            { "rain",       "RAIN" },
            { "fireplace",  "FIREPLACE" },
            { "campfire",   "CAMPFIRE" },
            { "wind",       "WIND" },
            { "woodlands",  "WOODLANDS" },
            { "riverbed",   "RIVERBED" },
            { "whitenoise", "WHITE NOISE" }
        };
        const float ambBtnW = (contentW - 56.0f) / 4.0f;
        for (int i = 0; i < 8; ++i) {
            int row = i / 4;
            int col = i % 4;
            float ax = contentX + 16.0f + col * (ambBtnW + 8.0f);
            float ay = curY + row * 27.0f;
            bool isSel = (tempSettings_.ambienceId == ambOptions[i].id);
            bool aHov = (mouseX >= ax && mouseX <= ax + ambBtnW && mouseY >= ay && mouseY <= ay + 23.0f);
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, ax, ay, ambBtnW, 23.0f, 4.0f, ambOptions[i].label, aHov, isSel, 9.0f);
            } else {
                r.drawRoundedRect(ax, ay, ambBtnW, 23.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (aHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(ax, ay, ambBtnW, 23.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, ambOptions[i].label, ax + 8.0f, ay + 6.5f, 9.0f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }
        curY += 58.0f;

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
            bool lHov = (mouseX >= lx && mouseX <= lx + lightBtnW && mouseY >= curY && mouseY <= curY + 24.0f);
            if (isClockwork) {
                clockwork_ui::drawBrassButton(r, lx, curY, lightBtnW, 24.0f, 4.0f, lightModes[l], lHov, isSel, 9.0f);
            } else {
                r.drawRoundedRect(lx, curY, lightBtnW, 24.0f, 4.0f, isSel ? (pal.woodR0 + 0.12f) : (pal.woodR0 + 0.04f + (lHov ? 0.06f : 0.0f)), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
                r.drawRoundedRectOutline(lx, curY, lightBtnW, 24.0f, 4.0f, isSel ? pal.accentR : pal.trimR, isSel ? pal.accentG : pal.trimG, isSel ? pal.accentB : pal.trimB, isSel ? 1.0f : 0.55f, isSel ? 1.4f : 1.0f, 4);
                eatsbits::ui::glyph::drawVectorText(r, lightModes[l], lx + 8.0f, curY + 7.0f, 9.0f, isSel ? pal.accentR : 0.90f, isSel ? pal.accentG : 0.88f, isSel ? pal.accentB : 0.80f, 1.0f);
            }
        }

        curY += 32.0f;
        drawSectionHeader("SCENE DARKNESS & LIGHT SOURCE STRENGTH");

        const float sliderGap = 16.0f;
        const float halfSliderW = (contentW - 32.0f - sliderGap) * 0.5f;

        // Scene Darkness slider (0% .. 85%)
        darkTrackX_ = contentX + 16.0f;
        darkTrackY_ = curY;
        darkTrackW_ = halfSliderW;
        darkTrackH_ = 24.0f;

        float darkNorm = std::clamp(tempSettings_.sceneDarkness / 0.85f, 0.0f, 1.0f);
        int darkPct = static_cast<int>(std::round(tempSettings_.sceneDarkness * 100.0f));
        std::string darkStr = "SCENE DARKNESS: " + std::to_string(darkPct) + "%";

        // Light Strength slider (0% .. 200%)
        lightStrTrackX_ = darkTrackX_ + darkTrackW_ + sliderGap;
        lightStrTrackY_ = curY;
        lightStrTrackW_ = halfSliderW;
        lightStrTrackH_ = 24.0f;

        float lightNorm = std::clamp(tempSettings_.lightStrength / 2.0f, 0.0f, 1.0f);
        int lightPct = static_cast<int>(std::round(tempSettings_.lightStrength * 100.0f));
        std::string lightStr = "LIGHT STRENGTH: " + std::to_string(lightPct) + "%";

        if (isClockwork) {
            bool darkHov = (mouseX >= darkTrackX_ && mouseX <= darkTrackX_ + darkTrackW_ && mouseY >= darkTrackY_ && mouseY <= darkTrackY_ + darkTrackH_);
            clockwork_ui::drawGlassSlider(r, darkTrackX_, darkTrackY_, darkTrackW_, darkTrackH_, darkNorm, nullptr, darkHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, darkStr.c_str(), darkTrackX_ + 12.0f, darkTrackY_ + 6.5f, 10.0f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);

            bool lightHov = (mouseX >= lightStrTrackX_ && mouseX <= lightStrTrackX_ + lightStrTrackW_ && mouseY >= lightStrTrackY_ && mouseY <= lightStrTrackY_ + lightStrTrackH_);
            clockwork_ui::drawGlassSlider(r, lightStrTrackX_, lightStrTrackY_, lightStrTrackW_, lightStrTrackH_, lightNorm, nullptr, lightHov, 0.0f);
            eatsbits::ui::glyph::drawVectorText(r, lightStr.c_str(), lightStrTrackX_ + 12.0f, lightStrTrackY_ + 6.5f, 10.0f, cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);
        } else {
            r.drawRoundedRect(darkTrackX_, darkTrackY_, darkTrackW_, darkTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(darkTrackX_, darkTrackY_, darkTrackW_, darkTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float darkFillW = (darkTrackW_ - 4.0f) * darkNorm;
            r.drawRoundedRect(darkTrackX_ + 2.0f, darkTrackY_ + 2.0f, darkFillW, darkTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, darkStr.c_str(), darkTrackX_ + 10.0f, darkTrackY_ + 6.5f, 10.0f, 0.98f, 0.95f, 0.88f, 1.0f);

            r.drawRoundedRect(lightStrTrackX_, lightStrTrackY_, lightStrTrackW_, lightStrTrackH_, 4.0f, 0.10f, 0.09f, 0.08f, 0.95f, 4);
            r.drawRoundedRectOutline(lightStrTrackX_, lightStrTrackY_, lightStrTrackW_, lightStrTrackH_, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.1f, 4);
            float lightFillW = (lightStrTrackW_ - 4.0f) * lightNorm;
            r.drawRoundedRect(lightStrTrackX_ + 2.0f, lightStrTrackY_ + 2.0f, lightFillW, lightStrTrackH_ - 4.0f, 3.0f, pal.accentR * 0.8f, pal.accentG * 0.8f, pal.accentB * 0.8f, 0.90f, 3);
            eatsbits::ui::glyph::drawVectorText(r, lightStr.c_str(), lightStrTrackX_ + 10.0f, lightStrTrackY_ + 6.5f, 10.0f, 0.98f, 0.95f, 0.88f, 1.0f);
        }
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
        draggingAmbienceVolume_ = false;
        draggingDeviation_ = false;
        draggingPostFxIntensity_ = false;
        peekingPostFx_ = false;
    draggingPostFxIntensity_ = false;
    peekingPostFx_ = false;
        draggingDarkness_ = false;
        draggingLightStrength_ = false;
        pointerDownInContent_ = false;
        draggingThemeScrollbar_ = false;
        if (draggingThemeScroll_) {
            draggingThemeScroll_ = false;
            return true;
        }
        return true;
    }
    if (action != 1) return true;

    // On mobile, tapping bottom pull handle pill closes drawer
    if (dialogY_ == 0.0f && mouseY >= dialogH_ - 14.0f && mouseY <= dialogH_ + 8.0f) {
        float handleW = 60.0f;
        float handleX = (dialogW_ - handleW) * 0.5f;
        if (mouseX >= handleX && mouseX <= handleX + handleW) {
            revertLiveState(board, foley);
            close();
            return true;
        }
    }

    // Check outside dialog click -> close
    if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ || mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
        revertLiveState(board, foley);
        close();
        return true;
    }

    // Close button [X]
    const float headerH = 48.0f;
    const float closeBtnSize = 28.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 12.0f;
    const float closeBtnY = dialogY_ + (headerH - closeBtnSize) * 0.5f;
    if (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize && mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize) {
        revertLiveState(board, foley);
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
        const float libBtnW = std::min(220.0f, contentW - 32.0f);
        const float libBtnH = 30.0f;
        const float libBtnX = contentX + 16.0f;
        if (mouseX >= libBtnX && mouseX <= libBtnX + libBtnW && mouseY >= curY && mouseY <= curY + libBtnH) {
            foley.triggerSound(FoleySound::WoodClick, 0.8f);
            close();
            if (onOpenGameBrowser) onOpenGameBrowser();
            return true;
        }

        curY += libBtnH + 12.0f;
        const auto& favIds = SettingsManager::instance().settings().favorites;
        std::vector<const SolitaireGameDef*> favDefs;
        favDefs.reserve(6);
        for (const auto& id : favIds) {
            if (const auto* def = GameRegistry::instance().findById(id)) {
                favDefs.push_back(def);
                if (favDefs.size() >= 6) break;
            }
        }
        if (!favDefs.empty()) {
            const float favBtnW = (contentW - 48.0f) * 0.5f;
            const float favBtnH = 30.0f;
            for (size_t i = 0; i < favDefs.size(); ++i) {
                float fx = contentX + 16.0f + (static_cast<int>(i % 2) * (favBtnW + 16.0f));
                float fy = curY + static_cast<int>(i / 2) * (favBtnH + 8.0f);
                if (mouseX >= fx && mouseX <= fx + favBtnW && mouseY >= fy && mouseY <= fy + favBtnH) {
                    const std::string chosenId = favDefs[i]->id;
                    foley.triggerSound(FoleySound::WoodClick, 0.8f);
                    close();
                    if (onSelectFavoriteGame) onSelectFavoriteGame(chosenId);
                    return true;
                }
            }
        }
    } else if (currentTab_ == 1) {
        if (mouseY < contentRectY_ || mouseY > contentRectY_ + contentRectH_ ||
            mouseX < contentRectX_ || mouseX > contentRectX_ + contentRectW_) {
            // Outside scrollable content bounds — fall through to footer buttons
        } else {
            // Direct scrollbar track/thumb grab (wide 22px hit zone for mouse & touch)
            if (themeMaxScrollY_ > 1.0f &&
                mouseX >= contentX + contentW - 22.0f && mouseX <= contentX + contentW &&
                mouseY >= themeSbTrackY_ && mouseY <= themeSbTrackY_ + themeSbTrackH_) {
                draggingThemeScrollbar_ = true;
                float denom = std::max(1.0f, themeSbTrackH_ - themeSbThumbH_);
                float frac = std::clamp((mouseY - themeSbTrackY_ - themeSbThumbH_ * 0.5f) / denom, 0.0f, 1.0f);
                themeScrollY_ = frac * themeMaxScrollY_;
                return true;
            }
            // Track pointer press for touch/empty-space drag scrolling
            const bool onDevSlider = (mouseX >= devTrackX_ && mouseX <= devTrackX_ + devTrackW_ &&
                                      mouseY >= devTrackY_ && mouseY <= devTrackY_ + devTrackH_);
            const bool onPfxSlider = (mouseX >= postFxTrackX_ && mouseX <= postFxTrackX_ + postFxTrackW_ &&
                                      mouseY >= postFxTrackY_ && mouseY <= postFxTrackY_ + postFxTrackH_);
            if (themeMaxScrollY_ > 1.0f && !onDevSlider && !onPfxSlider) {
                pointerDownInContent_ = true;
                draggingThemeScroll_ = false;
                dragStartY_ = mouseY;
                dragLastY_ = mouseY;
            }
            float curY = tabContentY + 16.0f - themeScrollY_;
            const bool useTwoCol = (dialogW_ >= 420.0f);
            const float col1W = useTwoCol ? (contentW * (dialogW_ < 540.0f ? 0.60f : 0.56f)) : contentW;
            const float rowX = contentX + 16.0f;
            const float rowW = useTwoCol ? (col1W - 12.0f) : (contentW - 36.0f);
        const float rowH = 28.0f;
        const float btnW = 32.0f;
        const float rowGap = 5.0f;
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

                board.setTableThemeSlug(tempSettings_.tableThemeSlug);
                board.setTableTheme(tempSettings_.tableTheme);
                ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
                if (!availableThemes[curIdx].filePath.empty()) {
                    ProceduralBackgroundSystem::instance().loadCustomEatsTheme(availableThemes[curIdx].filePath);
                }

                // Synchronize active option for the newly selected theme
                const auto& newDef = availableThemes[curIdx];
                if (!newDef.options.empty()) {
                    auto it = tempSettings_.tableThemeOptions.find(newDef.id);
                    if (it != tempSettings_.tableThemeOptions.end()) {
                        tempSettings_.tableThemeOption = it->second;
                    } else {
                        tempSettings_.tableThemeOption = newDef.options[0].id;
                        tempSettings_.tableThemeOptions[newDef.id] = newDef.options[0].id;
                    }
                } else {
                    tempSettings_.tableThemeOption.clear();
                }
                board.setTableThemeOption(tempSettings_.tableThemeOption);
                ProceduralBackgroundSystem::instance().setThemeOption(tempSettings_.tableThemeOption);
                ProceduralBackgroundSystem::instance().invalidateCache();
            }
            foley.triggerSound(FoleySound::WoodClick, 0.8f);
            return true;
        }
        curY += rowH + rowGap;

        // 1b. Table Theme Option (displays sub-theme options if provided by the theme)
        const auto& availableThemesForOpt = ProceduralBackgroundSystem::instance().availableThemes();
        size_t curThemeIdxForOpt = ProceduralBackgroundSystem::instance().findThemeIndex(tempSettings_.tableThemeSlug, tempSettings_.tableTheme);
        const TableThemeDef* curThemeDefForOpt = (curThemeIdxForOpt < availableThemesForOpt.size()) ? &availableThemesForOpt[curThemeIdxForOpt] : nullptr;
        bool hasThemeOptionsForOpt = (curThemeDefForOpt != nullptr && !curThemeDefForOpt->options.empty());
        if (hasThemeOptionsForOpt) {
            if (testStepper(stepDelta)) {
                const auto& opts = curThemeDefForOpt->options;
                size_t optIdx = 0;
                for (size_t i = 0; i < opts.size(); ++i) {
                    if (opts[i].id == tempSettings_.tableThemeOption) {
                        optIdx = i;
                        break;
                    }
                }
                size_t optCount = opts.size();
                if (stepDelta < 0) {
                    optIdx = (optIdx + optCount - 1u) % optCount;
                } else {
                    optIdx = (optIdx + 1u) % optCount;
                }
                tempSettings_.tableThemeOption = opts[optIdx].id;
                tempSettings_.tableThemeOptions[curThemeDefForOpt->id] = opts[optIdx].id;
                board.setTableThemeOption(tempSettings_.tableThemeOption);
                ProceduralBackgroundSystem::instance().setThemeOption(tempSettings_.tableThemeOption);
                ProceduralBackgroundSystem::instance().invalidateCache();
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
            curY += rowH + rowGap;
        }

        // 2. Deck Theme
        if (testStepper(stepDelta)) {
            constexpr uint8_t count = static_cast<uint8_t>(DeckThemeId::Count);
            if (stepDelta < 0) {
                tempSettings_.deckTheme = static_cast<DeckThemeId>((static_cast<uint8_t>(tempSettings_.deckTheme) + count - 1u) % count);
            } else {
                tempSettings_.deckTheme = static_cast<DeckThemeId>((static_cast<uint8_t>(tempSettings_.deckTheme) + 1u) % count);
            }
            CardTextureCache::instance().updatePreviewCard(tempSettings_.deckTheme, tempSettings_.cardWear, tempSettings_.cardStock);
            if (tempSettings_.cardBackId == "auto") {
                CardTextureCache::instance().setActiveCardBack("auto");
            }
            foley.triggerSound(FoleySound::CardSlide, 0.8f);
            return true;
        }
        curY += rowH + rowGap;

        // 2a. Card Back
        if (testStepper(stepDelta)) {
            CardTextureCache::instance().cycleCardBack(tempSettings_.cardBackId, stepDelta);
            CardTextureCache::instance().setActiveCardBack(tempSettings_.cardBackId);
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
        curY += rowH + 8.0f + 24.0f; // Row 2 + spacing + header

        // 3. Stock
        if (testStepper(stepDelta)) {
            constexpr uint8_t count = static_cast<uint8_t>(CardStock::Count);
            if (stepDelta < 0) {
                tempSettings_.cardStock = static_cast<CardStock>((static_cast<uint8_t>(tempSettings_.cardStock) + count - 1u) % count);
            } else {
                tempSettings_.cardStock = static_cast<CardStock>((static_cast<uint8_t>(tempSettings_.cardStock) + 1u) % count);
            }
            CardTextureCache::instance().updatePreviewCard(tempSettings_.deckTheme, tempSettings_.cardWear, tempSettings_.cardStock);
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
            CardTextureCache::instance().updatePreviewCard(tempSettings_.deckTheme, tempSettings_.cardWear, tempSettings_.cardStock);
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
        curY += rowH + 8.0f + 24.0f; // Row 5 + spacing + header

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
        const float devBtnX = devTrackX_ + devTrackW_ + (useTwoCol ? 6.0f : 12.0f);
        const float devBtnW = useTwoCol ? 68.0f : 104.0f;
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
        curY += devTrackH_ + 10.0f + 22.0f; // advance past Card Deviation + "Post-Processing" header

        // Post-FX Stepper
        const auto& availablePfx = PostFxSystem::instance().availableEffects();
        size_t curPfxIdx = PostFxSystem::instance().findEffectIndex(tempSettings_.postFxId);
        const PostFxDef* curPfxDef = PostFxSystem::instance().getEffectDef(curPfxIdx);
        if (testStepper(stepDelta)) {
            if (!availablePfx.empty()) {
                size_t count = availablePfx.size();
                curPfxIdx = (stepDelta < 0) ? ((curPfxIdx + count - 1u) % count) : ((curPfxIdx + 1u) % count);
                tempSettings_.postFxId = availablePfx[curPfxIdx].id;
                const auto& newPfx = availablePfx[curPfxIdx];
                if (!newPfx.options.empty()) {
                    auto it = tempSettings_.postFxOptions.find(newPfx.id);
                    if (it != tempSettings_.postFxOptions.end()) {
                        tempSettings_.postFxOption = it->second;
                    } else {
                        tempSettings_.postFxOption = newPfx.options[0].id;
                        tempSettings_.postFxOptions[newPfx.id] = newPfx.options[0].id;
                    }
                } else {
                    tempSettings_.postFxOption.clear();
                }
            }
            foley.triggerSound(FoleySound::WoodClick, 0.8f);
            return true;
        }
        curY += rowH + rowGap;

        if (curPfxDef && !curPfxDef->options.empty()) {
            if (testStepper(stepDelta)) {
                const auto& opts = curPfxDef->options;
                size_t optIdx = 0;
                for (size_t i = 0; i < opts.size(); ++i) {
                    if (opts[i].id == tempSettings_.postFxOption) {
                        optIdx = i;
                        break;
                    }
                }
                size_t optCount = opts.size();
                optIdx = (stepDelta < 0) ? ((optIdx + optCount - 1u) % optCount) : ((optIdx + 1u) % optCount);
                tempSettings_.postFxOption = opts[optIdx].id;
                tempSettings_.postFxOptions[curPfxDef->id] = opts[optIdx].id;
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
            curY += rowH + rowGap;
        }

        if (curPfxDef && curPfxDef->id != "none") {
            if (mouseX >= postFxTrackX_ && mouseX <= postFxTrackX_ + postFxTrackW_ &&
                mouseY >= postFxTrackY_ && mouseY <= postFxTrackY_ + postFxTrackH_) {
                draggingPostFxIntensity_ = true;
                tempSettings_.postFxIntensity = std::clamp((mouseX - postFxTrackX_) / postFxTrackW_, 0.0f, 1.0f);
                return true;
            }
            const float peekBtnX = postFxTrackX_ + postFxTrackW_ + (useTwoCol ? 6.0f : 12.0f);
            const float peekBtnW = useTwoCol ? 68.0f : 104.0f;
            if (mouseX >= peekBtnX && mouseX <= peekBtnX + peekBtnW &&
                mouseY >= postFxTrackY_ && mouseY <= postFxTrackY_ + postFxTrackH_) {
                peekingPostFx_ = true;
                foley.triggerSound(FoleySound::WoodClick, 0.65f);
                return true;
            }
            if (mouseX >= rowX && mouseX <= rowX + rowW &&
                mouseY >= postFxAffectUiY_ && mouseY <= postFxAffectUiY_ + rowH) {
                tempSettings_.postFxAffectUi = !tempSettings_.postFxAffectUi;
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
        }
        if (pointerDownInContent_) {
            return true;
        }
        } // end else inside scrollable content bounds
    } else if (currentTab_ == 2) {
        float curY = tabContentY + 16.0f + 24.0f;
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

        if (mouseX >= ambVolTrackX_ && mouseX <= ambVolTrackX_ + ambVolTrackW_ &&
            mouseY >= ambVolTrackY_ && mouseY <= ambVolTrackY_ + ambVolTrackH_) {
            float pct = std::clamp((mouseX - ambVolTrackX_) / ambVolTrackW_, 0.0f, 1.0f);
            tempSettings_.ambienceVolume = pct;
            foley.setAmbienceVolume(pct);
            draggingAmbienceVolume_ = true;
            foley.triggerSound(FoleySound::WoodClick, 0.6f);
            return true;
        }

        const float muteBtnW = 92.0f;
        const float mixGap = 10.0f;
        const float muteBtnX = ambVolTrackX_ + ambVolTrackW_ + mixGap;
        if (mouseX >= muteBtnX && mouseX <= muteBtnX + muteBtnW && mouseY >= curY && mouseY <= curY + volTrackH_) {
            tempSettings_.muted = !tempSettings_.muted;
            foley.setMuted(tempSettings_.muted);
            foley.triggerSound(FoleySound::WoodClick, 0.75f);
            return true;
        }

        curY += 32.0f + 24.0f;
        const char* soundPackIds[9] = {
            "default", "authentic_place", "copag_plastic", "vintage_bicycle", "vegas_linen",
            "bass_heavy", "glass_crystal", "futuristic", "retro_8bit"
        };
        const float packBtnW = (contentW - 64.0f) / 5.0f;
        for (int i = 0; i < 9; ++i) {
            int row = i / 5;
            int col = i % 5;
            float px = contentX + 16.0f + col * (packBtnW + 8.0f);
            float py = curY + row * 27.0f;
            if (mouseX >= px && mouseX <= px + packBtnW && mouseY >= py && mouseY <= py + 23.0f) {
                tempSettings_.soundPackId = soundPackIds[i];
                foley.setSoundPackId(tempSettings_.soundPackId);
                foley.triggerSound(FoleySound::Deal, 0.8f);
                return true;
            }
        }

        curY += 58.0f + 24.0f;
        const char* ambIds[8] = {
            "off", "rain", "fireplace", "campfire",
            "wind", "woodlands", "riverbed", "whitenoise"
        };
        const float ambBtnW = (contentW - 56.0f) / 4.0f;
        for (int i = 0; i < 8; ++i) {
            int row = i / 4;
            int col = i % 4;
            float ax = contentX + 16.0f + col * (ambBtnW + 8.0f);
            float ay = curY + row * 27.0f;
            if (mouseX >= ax && mouseX <= ax + ambBtnW && mouseY >= ay && mouseY <= ay + 23.0f) {
                tempSettings_.ambienceId = ambIds[i];
                foley.setAmbience(tempSettings_.ambienceId);
                foley.triggerSound(FoleySound::WoodClick, 0.7f);
                return true;
            }
        }

        curY += 58.0f + 24.0f;
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
            if (mouseX >= lx && mouseX <= lx + lightBtnW && mouseY >= curY && mouseY <= curY + 24.0f) {
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
        foley.setSoundPackId(tempSettings_.soundPackId);
        foley.setAmbienceVolume(tempSettings_.ambienceVolume);
        foley.setAmbience(tempSettings_.ambienceId);
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
    if (draggingAmbienceVolume_ && ambVolTrackW_ > 1.0f) {
        float pct = std::clamp((mouseX - ambVolTrackX_) / ambVolTrackW_, 0.0f, 1.0f);
        tempSettings_.ambienceVolume = pct;
        foley.setAmbienceVolume(pct);
        return true;
    }
    if (draggingDeviation_ && devTrackW_ > 1.0f) {
        float dev = std::clamp((mouseX - devTrackX_) / devTrackW_, 0.0f, 1.0f);
        tempSettings_.cardDeviation = dev;
        board.setCardDeviation(dev);
        return true;
    }
    if (draggingPostFxIntensity_ && postFxTrackW_ > 1.0f) {
        tempSettings_.postFxIntensity = std::clamp((mouseX - postFxTrackX_) / postFxTrackW_, 0.0f, 1.0f);
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
    if (draggingThemeScrollbar_ && currentTab_ == 1 && themeMaxScrollY_ > 1.0f) {
        float denom = std::max(1.0f, themeSbTrackH_ - themeSbThumbH_);
        float frac = std::clamp((mouseY - themeSbTrackY_ - themeSbThumbH_ * 0.5f) / denom, 0.0f, 1.0f);
        themeScrollY_ = frac * themeMaxScrollY_;
        return true;
    }
    if (pointerDownInContent_ && currentTab_ == 1 && themeMaxScrollY_ > 1.0f) {
        float dy = mouseY - dragLastY_;
        if (!draggingThemeScroll_ && std::abs(mouseY - dragStartY_) > 4.0f) {
            draggingThemeScroll_ = true;
        }
        if (draggingThemeScroll_) {
            themeScrollY_ = std::clamp(themeScrollY_ - dy, 0.0f, themeMaxScrollY_);
        }
        dragLastY_ = mouseY;
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
        revertLiveState(board, foley);
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

bool SettingsDialog::handleScroll(float yoffset) {
    if (!isOpen_) return false;
    if (currentTab_ == 1 && themeMaxScrollY_ > 1.0f) {
        themeScrollY_ = std::clamp(themeScrollY_ - yoffset * 28.0f, 0.0f, themeMaxScrollY_);
        return true;
    }
    return true;
}

} // namespace solengine
