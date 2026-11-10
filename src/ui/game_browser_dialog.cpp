#include "solengine/ui/game_browser_dialog.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/settings_manager.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "eatsbits/ui/glyph/font_engine.hpp"
#include <algorithm>
#include <random>
#include <chrono>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace solengine {

GameBrowserDialog::GameBrowserDialog() = default;

void GameBrowserDialog::open(const std::string& activeGameId) {
    GameRegistry::instance().initialize();
    isOpen_ = true;
    searchFocused_ = true;
    isMouseDown_ = false;
    isDragging_ = false;
    activeGameId_ = activeGameId;
    searchQuery_ = "";
    selectedCategory_ = "ALL";
    scrollOffsetY_ = 0.0f;
    highlightedIndex_ = 0;
    cursorBlinkTimer_ = 0.0f;
    updateFilter();

    for (size_t i = 0; i < filteredGames_.size(); ++i) {
        if (filteredGames_[i].id == activeGameId_) {
            highlightedIndex_ = static_cast<int>(i);
            ensureVisible(highlightedIndex_);
            break;
        }
    }
}

void GameBrowserDialog::close() noexcept {
    isOpen_ = false;
    isMouseDown_ = false;
    isDragging_ = false;
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        if (window.closeMobileKeyboard) {
            window.closeMobileKeyboard();
        }
    });
#endif
}

void GameBrowserDialog::setSearchQuery(const std::string& text) {
    searchQuery_ = text;
    scrollOffsetY_ = 0.0f;
    highlightedIndex_ = 0;
    updateFilter();
}

void GameBrowserDialog::handleMouseMove(float mouseX, float mouseY) {
    if (!isOpen_) return;
    (void)mouseX;
    if (isMouseDown_) {
        float dy = mouseY - dragStartY_;
        if (!isDragging_ && std::abs(dy) > 4.0f) {
            isDragging_ = true;
        }
        if (isDragging_) {
            scrollOffsetY_ = std::clamp(initialScrollY_ - dy, 0.0f, maxScrollY_);
        }
    }
}

void GameBrowserDialog::updateFilter() {
    filteredGames_ = GameRegistry::instance().search(searchQuery_, selectedCategory_);
    if (highlightedIndex_ >= static_cast<int>(filteredGames_.size())) {
        highlightedIndex_ = std::max(0, static_cast<int>(filteredGames_.size()) - 1);
    }
}

void GameBrowserDialog::ensureVisible(int index) {
    if (index < 0 || index >= static_cast<int>(filteredGames_.size())) return;
    const float cardPad = 6.0f;
    const float cardH = 50.0f;
    const float cardTop = static_cast<float>(index) * (cardH + cardPad);
    const float cardBottom = cardTop + cardH;
    const float visibleH = std::max(10.0f, cardsAreaH_ - cardPad * 2.0f);

    if (cardTop < scrollOffsetY_) {
        scrollOffsetY_ = cardTop;
    } else if (cardBottom > scrollOffsetY_ + visibleH) {
        scrollOffsetY_ = cardBottom - visibleH;
    }
    scrollOffsetY_ = std::clamp(scrollOffsetY_, 0.0f, maxScrollY_);
}

void GameBrowserDialog::render(
    eatsbits::ui::BatchRenderer2D& r,
    float screenW,
    float screenH,
    float mouseX,
    float mouseY,
    const ThemePalette& pal
) {
    if (!isOpen_) return;

    cursorBlinkTimer_ += 0.016f;

    // 1. Semi-transparent backdrop overlay
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 0.0f, 0.65f);

    // 2. Responsive chassis sizing (single-column layout with room for future auto-play sidebar)
    const bool isMobile = (screenW < 680.0f) || (screenW < screenH);
    if (isMobile) {
        dialogX_ = 0.0f;
        dialogY_ = 0.0f;
        dialogW_ = screenW;
        dialogH_ = std::min(screenH * 0.96f, 660.0f);
    } else {
        dialogW_ = std::clamp(screenW * 0.72f, 540.0f, 760.0f);
        dialogH_ = std::clamp(screenH * 0.84f, 480.0f, 640.0f);
        dialogX_ = (screenW - dialogW_) * 0.5f;
        dialogY_ = (screenH - dialogH_) * 0.5f;
    }

    eatsbits::ui::LightSource2D light{screenW * 0.5f, -200.0f, 1.0f};
    r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, 22.0f, light, 0.68f);
    r.drawRoundedRectGradient(
        dialogX_, dialogY_, dialogW_, dialogH_, 10.0f,
        pal.woodR0 + 0.06f, pal.woodG0 + 0.05f, pal.woodB0 + 0.04f,
        pal.woodR1 + 0.02f, pal.woodG1 + 0.02f, pal.woodB1 + 0.02f,
        0.98f, 6
    );
    r.drawRoundedRectOutline(dialogX_, dialogY_, dialogW_, dialogH_, 10.0f, pal.trimR, pal.trimG, pal.trimB, 0.95f, 1.8f, 6);

    // 3. Header title & subtitle
    float curY = dialogY_ + 14.0f;
    eatsbits::ui::glyph::drawVectorText(r, "SOLITAIRE GAME LIBRARY", dialogX_ + 20.0f, curY, 15.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
    curY += 20.0f;
    eatsbits::ui::glyph::drawVectorText(r, "Searchable catalog of classical, tournament & imported patience variants", dialogX_ + 20.0f, curY, 10.0f, 0.85f, 0.82f, 0.75f, 0.90f);

    // Close [X] button top right (Clockwork circled X icon)
    const float closeBtnSize = 26.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 16.0f;
    const float closeBtnY = dialogY_ + 14.0f;
    bool closeHov = (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize && mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize);
    clockwork_ui::drawClockworkCloseButton(r, closeBtnX, closeBtnY, closeBtnSize, closeHov);

    // 4. Search bar
    curY += 18.0f;
    const float searchBoxX = dialogX_ + 20.0f;
    const float searchBoxW = dialogW_ - 40.0f;
    const float searchBoxH = 32.0f;
    r.drawRoundedRect(searchBoxX, curY, searchBoxW, searchBoxH, 4.0f, 0.08f, 0.07f, 0.06f, 0.95f, 4);
    r.drawRoundedRectOutline(searchBoxX, curY, searchBoxW, searchBoxH, 4.0f,
                             searchFocused_ ? pal.accentR : pal.trimR,
                             searchFocused_ ? pal.accentG : pal.trimG,
                             searchFocused_ ? pal.accentB : pal.trimB,
                             searchFocused_ ? 1.0f : 0.75f,
                             searchFocused_ ? 1.6f : 1.2f, 4);

    const bool showCursor = searchFocused_ && (static_cast<int>(cursorBlinkTimer_ * 2.0f) % 2 == 0);
    if (searchQuery_.empty()) {
        // Place hint text at a fixed position so the cursor never pushes it
        eatsbits::ui::glyph::drawVectorText(r, "Type to search by name or family (e.g. Golf, Spider, Yukon, FreeCell)...", searchBoxX + 15.0f, curY + 9.5f, 11.0f, 0.55f, 0.52f, 0.48f, 0.85f);
        if (showCursor) {
            r.drawRect(searchBoxX + 11.0f, curY + 7.5f, 1.5f, 17.0f, pal.accentR, pal.accentG, pal.accentB, 0.95f);
        }
    } else {
        float textW = eatsbits::ui::glyph::getVectorTextWidth(11.5f, searchQuery_.c_str());
        eatsbits::ui::glyph::drawVectorText(r, searchQuery_.c_str(), searchBoxX + 12.0f, curY + 9.5f, 11.5f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        if (showCursor) {
            r.drawRect(searchBoxX + 14.0f + textW, curY + 7.5f, 1.5f, 17.0f, pal.accentR, pal.accentG, pal.accentB, 0.95f);
        }

        // Clear [X] button inside search box
        const float clearBtnW = 20.0f;
        const float clearBtnX = searchBoxX + searchBoxW - clearBtnW - 8.0f;
        const float clearBtnY = curY + 6.0f;
        bool clearHov = (mouseX >= clearBtnX && mouseX <= clearBtnX + clearBtnW && mouseY >= clearBtnY && mouseY <= clearBtnY + clearBtnW);
        if (clearHov) {
            r.drawRoundedRect(clearBtnX, clearBtnY, clearBtnW, clearBtnW, 3.0f, 0.25f, 0.15f, 0.15f, 0.80f, 3);
        }
        eatsbits::ui::glyph::drawVectorText(r, "X", clearBtnX + 6.0f, clearBtnY + 4.0f, 11.0f, 0.85f, 0.80f, 0.75f, 1.0f);
    }

    // 5. Category filter chips
    curY += searchBoxH + 10.0f;
    const auto categories = GameRegistry::instance().categories();
    float chipX = dialogX_ + 20.0f;
    const float chipH = 24.0f;
    const float chipPad = 12.0f;

    for (const auto& cat : categories) {
        bool isFavTab = (cat == "FAVORITES");
        float extraW = isFavTab ? 14.0f : 0.0f;
        float textW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, cat.c_str());
        float chipW = textW + chipPad * 2.0f + extraW;
        if (chipX + chipW > dialogX_ + dialogW_ - 20.0f) break; // Don't overflow row

        bool isSel = (selectedCategory_ == cat);
        bool chipHov = (mouseX >= chipX && mouseX <= chipX + chipW && mouseY >= curY && mouseY <= curY + chipH);

        r.drawRoundedRect(chipX, curY, chipW, chipH, 12.0f,
                          isSel ? (pal.accentR * 0.35f + pal.woodR0 * 0.65f) : (pal.woodR0 + (chipHov ? 0.08f : 0.03f)),
                          isSel ? (pal.accentG * 0.35f + pal.woodG0 * 0.65f) : (pal.woodG0 + (chipHov ? 0.06f : 0.02f)),
                          isSel ? (pal.accentB * 0.35f + pal.woodB0 * 0.65f) : (pal.woodB0 + (chipHov ? 0.05f : 0.02f)),
                          0.95f, 4);
        r.drawRoundedRectOutline(chipX, curY, chipW, chipH, 12.0f,
                                 isSel ? pal.accentR : pal.trimR,
                                 isSel ? pal.accentG : pal.trimG,
                                 isSel ? pal.accentB : pal.trimB,
                                 isSel ? 1.0f : 0.50f, isSel ? 1.4f : 1.0f, 4);

        float textX = chipX + chipPad;
        if (isFavTab) {
            vector_icon::drawStar(r, chipX + chipPad + 4.0f, curY + chipH * 0.5f, 4.5f,
                                  pal.accentR, pal.accentG, pal.accentB, isSel ? 1.0f : 0.70f,
                                  pal.accentR, pal.accentG, pal.accentB, isSel ? 1.0f : 0.70f, 1.0f);
            textX += 13.0f;
        }

        eatsbits::ui::glyph::drawVectorText(r, cat.c_str(), textX, curY + 6.5f, 10.0f,
                                            isSel ? pal.accentR : 0.90f,
                                            isSel ? pal.accentG : 0.88f,
                                            isSel ? pal.accentB : 0.82f, 1.0f);

        chipX += chipW + 8.0f;
    }

    // 6. Cards area (Single Column Layout)
    curY += chipH + 12.0f;
    cardsAreaY_ = curY;
    const float footerH = 46.0f;
    cardsAreaH_ = (dialogY_ + dialogH_ - footerH) - cardsAreaY_;

    // Inner background panel for game cards
    r.drawRoundedRect(dialogX_ + 16.0f, cardsAreaY_, dialogW_ - 32.0f, cardsAreaH_, 6.0f,
                      pal.woodR0 + 0.02f, pal.woodG0 + 0.015f, pal.woodB0 + 0.01f, 0.90f, 4);
    r.drawRoundedRectOutline(dialogX_ + 16.0f, cardsAreaY_, dialogW_ - 32.0f, cardsAreaH_, 6.0f,
                             pal.trimR, pal.trimG, pal.trimB, 0.40f, 1.0f, 4);

    const float cardPad = 6.0f;
    const float cardW = dialogW_ - 48.0f;
    const float cardH = 50.0f;

    const size_t totalCards = filteredGames_.size();
    const float totalContentH = static_cast<float>(totalCards) * (cardH + cardPad) + cardPad;
    maxScrollY_ = std::max(0.0f, totalContentH - cardsAreaH_);
    scrollOffsetY_ = std::clamp(scrollOffsetY_, 0.0f, maxScrollY_);

    // STRICT SCISSOR CLIPPING: Absolutely no cards can overflow or bleed outside the list bounds
    {
        auto scissor = r.makeScissorScope(dialogX_ + 16.0f, cardsAreaY_ + 2.0f, dialogW_ - 32.0f, cardsAreaH_ - 4.0f);

        for (size_t i = 0; i < totalCards; ++i) {
            const auto& g = filteredGames_[i];
            const float cx = dialogX_ + 24.0f;
            const float cy = cardsAreaY_ + cardPad + static_cast<float>(i) * (cardH + cardPad) - scrollOffsetY_;

            // Skip cards outside the visible clipping area
            if (cy + cardH < cardsAreaY_ || cy > cardsAreaY_ + cardsAreaH_) continue;

            bool isActive = (g.id == activeGameId_);
            bool isHighlighted = (static_cast<int>(i) == highlightedIndex_);
            bool isHov = (!isDragging_ && mouseX >= cx && mouseX <= cx + cardW && mouseY >= cy && mouseY <= cy + cardH &&
                          mouseY >= cardsAreaY_ && mouseY <= cardsAreaY_ + cardsAreaH_);

            // Hover syncs highlightedIndex_
            if (isHov && highlightedIndex_ != static_cast<int>(i)) {
                highlightedIndex_ = static_cast<int>(i);
            }

            // Card background
            float bgR = pal.woodR0 + 0.05f;
            float bgG = pal.woodG0 + 0.04f;
            float bgB = pal.woodB0 + 0.03f;
            if (isActive) {
                bgR += 0.08f;
                bgG += 0.06f;
            } else if (isHighlighted || isHov) {
                bgR += 0.09f;
                bgG += 0.07f;
                bgB += 0.05f;
            }
            r.drawRoundedRect(cx, cy, cardW, cardH, 4.0f, bgR, bgG, bgB, 0.96f, 4);

            // Card outline
            float outR = pal.trimR;
            float outG = pal.trimG;
            float outB = pal.trimB;
            float outA = 0.50f;
            float outThick = 1.0f;
            if (isActive) {
                outR = 0.35f; outG = 0.95f; outB = 0.45f;
                outA = 1.0f; outThick = 1.6f;
            } else if (isHighlighted || isHov) {
                outR = pal.accentR; outG = pal.accentG; outB = pal.accentB;
                outA = 0.95f; outThick = 1.4f;
            }
            r.drawRoundedRectOutline(cx, cy, cardW, cardH, 4.0f, outR, outG, outB, outA, outThick, 4);

            // Left edge active/selection accent bar
            if (isHighlighted || isActive) {
                r.drawRoundedRect(cx, cy + 3.0f, 4.0f, cardH - 6.0f, 2.0f,
                                  isActive ? 0.35f : pal.accentR,
                                  isActive ? 0.95f : pal.accentG,
                                  isActive ? 0.45f : pal.accentB, 1.0f);
            }

            // Title
            eatsbits::ui::glyph::drawVectorText(r, g.name.c_str(), cx + 14.0f, cy + 9.5f, 12.0f,
                                                isActive ? 0.40f : ((isHighlighted || isHov) ? pal.accentR : 0.96f),
                                                isActive ? 1.0f : ((isHighlighted || isHov) ? pal.accentG : 0.93f),
                                                isActive ? 0.50f : ((isHighlighted || isHov) ? pal.accentB : 0.88f), 1.0f);

            // Description summary
            if (!g.description.empty()) {
                std::string desc = g.description;
                if (desc.size() > 70) desc = desc.substr(0, 67) + "...";
                eatsbits::ui::glyph::drawVectorText(r, desc.c_str(), cx + 14.0f, cy + 28.5f, 9.2f, 0.78f, 0.76f, 0.70f, 0.90f);
            }

            // Right-aligned badges and Favorite Star
            const float starBtnW = 28.0f;
            const float starBtnX = cx + cardW - starBtnW - 8.0f;
            const float starBtnY = cy + (cardH - starBtnW) * 0.5f;
            const bool isFav = SettingsManager::instance().isFavorite(g.id);
            const bool starHov = (mouseX >= starBtnX && mouseX <= starBtnX + starBtnW && mouseY >= starBtnY && mouseY <= starBtnY + starBtnW);

            if (starHov) {
                r.drawRoundedRect(starBtnX, starBtnY, starBtnW, starBtnW, 4.0f, pal.woodR0 + 0.14f, pal.woodG0 + 0.11f, pal.woodB0 + 0.08f, 0.90f, 3);
            }
            const float starCx = starBtnX + starBtnW * 0.5f;
            const float starCy = starBtnY + starBtnW * 0.5f;
            if (isFav) {
                vector_icon::drawStar(r, starCx, starCy, 8.5f,
                                      pal.accentR, pal.accentG, pal.accentB, 1.0f,
                                      pal.accentR, pal.accentG, pal.accentB, 1.0f, 1.2f);
            } else {
                vector_icon::drawStar(r, starCx, starCy, 8.5f,
                                      0.0f, 0.0f, 0.0f, 0.0f,
                                      pal.trimR, pal.trimG, pal.trimB, starHov ? 0.95f : 0.45f, 1.4f);
            }

            float badgeRightX = starBtnX - 10.0f;

            // 1. Family pill badge
            float famTxtW = eatsbits::ui::glyph::getVectorTextWidth(8.5f, g.family.c_str());
            float famBadgeW = famTxtW + 10.0f;
            badgeRightX -= famBadgeW;
            r.drawRoundedRect(badgeRightX, cy + 7.0f, famBadgeW, 16.0f, 8.0f, pal.woodR0 + 0.10f, pal.woodG0 + 0.08f, pal.woodB0 + 0.06f, 0.95f, 3);
            r.drawRoundedRectOutline(badgeRightX, cy + 7.0f, famBadgeW, 16.0f, 8.0f, pal.trimR, pal.trimG, pal.trimB, 0.60f, 1.0f, 3);
            eatsbits::ui::glyph::drawVectorText(r, g.family.c_str(), badgeRightX + 5.0f, cy + 3.5f + 7.0f, 8.5f, pal.accentR, pal.accentG, pal.accentB, 0.95f);

            // 2. Metadata: Decks & Columns
            std::string metaStr = std::to_string(g.decks) + (g.decks == 1 ? " Deck" : " Decks") + " * " +
                                  std::to_string(g.tableaus) + " Cols";
            if (g.freecells > 0) metaStr += " * " + std::to_string(g.freecells) + " Cells";
            float metaTxtW = eatsbits::ui::glyph::getVectorTextWidth(9.0f, metaStr.c_str());
            badgeRightX -= (metaTxtW + 14.0f);
            eatsbits::ui::glyph::drawVectorText(r, metaStr.c_str(), badgeRightX, cy + 10.5f, 9.0f, pal.accentR, pal.accentG, pal.accentB, 0.85f);

            // 3. Active status tag
            if (isActive) {
                float actTxtW = eatsbits::ui::glyph::getVectorTextWidth(8.5f, "ACTIVE");
                badgeRightX -= (actTxtW + 12.0f);
                r.drawRoundedRect(badgeRightX, cy + 7.0f, actTxtW + 8.0f, 16.0f, 3.0f, 0.12f, 0.35f, 0.15f, 0.90f, 2);
                eatsbits::ui::glyph::drawVectorText(r, "ACTIVE", badgeRightX + 4.0f, cy + 10.5f, 8.5f, 0.45f, 1.0f, 0.55f, 1.0f);
            }
        }

        if (filteredGames_.empty()) {
            const char* emptyMsg = (selectedCategory_ == "FAVORITES")
                ? "No favorites yet. Click the star on any game to pin it to your Quick Switcher!"
                : "No games found matching your search.";
            float emptyTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, emptyMsg);
            eatsbits::ui::glyph::drawVectorText(r, emptyMsg, dialogX_ + (dialogW_ - emptyTxtW) * 0.5f, cardsAreaY_ + cardsAreaH_ * 0.45f, 11.0f, 0.70f, 0.65f, 0.58f, 0.85f);
        }
    }

    // Scrollbar indicator
    if (maxScrollY_ > 0.0f) {
        const float trackX = dialogX_ + dialogW_ - 20.0f;
        const float trackY = cardsAreaY_ + 4.0f;
        const float trackW = 4.0f;
        const float trackH = cardsAreaH_ - 8.0f;
        r.drawRoundedRect(trackX, trackY, trackW, trackH, 2.0f, 0.08f, 0.07f, 0.06f, 0.50f, 2);

        const float thumbH = std::max(24.0f, trackH * (cardsAreaH_ / totalContentH));
        const float thumbY = trackY + (scrollOffsetY_ / maxScrollY_) * (trackH - thumbH);
        r.drawRoundedRect(trackX, thumbY, trackW, thumbH, 2.0f, pal.accentR, pal.accentG, pal.accentB, 0.70f, 2);
    }

    // 7. Footer bar
    const float footerY = dialogY_ + dialogH_ - 38.0f;
    std::string countStr = "Showing " + std::to_string(filteredGames_.size()) + " variants (Enter: Play, [Space]: Auto-Play Demo)";
    eatsbits::ui::glyph::drawVectorText(r, countStr.c_str(), dialogX_ + 20.0f, footerY + 7.0f, 10.0f, 0.80f, 0.78f, 0.72f, 0.85f);

    // Footer buttons from right to left
    // [ CLOSE ] button
    const float closeFooterBtnW = 74.0f;
    const float closeFooterBtnX = dialogX_ + dialogW_ - closeFooterBtnW - 16.0f;
    const float randBtnH = 26.0f;
    bool closeFootHov = (mouseX >= closeFooterBtnX && mouseX <= closeFooterBtnX + closeFooterBtnW && mouseY >= footerY && mouseY <= footerY + randBtnH);
    r.drawRoundedRect(closeFooterBtnX, footerY, closeFooterBtnW, randBtnH, 4.0f, pal.woodR0 + (closeFootHov ? 0.08f : 0.04f), pal.woodG0 + 0.03f, pal.woodB0 + 0.02f, 0.95f, 4);
    r.drawRoundedRectOutline(closeFooterBtnX, footerY, closeFooterBtnW, randBtnH, 4.0f, closeFootHov ? pal.accentR : pal.trimR, closeFootHov ? pal.accentG : pal.trimG, closeFootHov ? pal.accentB : pal.trimB, 0.70f, 1.0f, 4);
    float closeTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, "CLOSE");
    eatsbits::ui::glyph::drawVectorText(r, "CLOSE", closeFooterBtnX + (closeFooterBtnW - closeTxtW) * 0.5f, footerY + 7.0f, 10.0f, 0.90f, 0.88f, 0.82f, 1.0f);

    // [ * SOLVABLE ] button
    const float solvBtnW = 120.0f;
    const float solvBtnX = closeFooterBtnX - solvBtnW - 8.0f;
    bool solvHov = (mouseX >= solvBtnX && mouseX <= solvBtnX + solvBtnW && mouseY >= footerY && mouseY <= footerY + randBtnH);
    r.drawRoundedRect(solvBtnX, footerY, solvBtnW, randBtnH, 4.0f, pal.woodR0 + (solvHov ? 0.12f : 0.06f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
    r.drawRoundedRectOutline(solvBtnX, footerY, solvBtnW, randBtnH, 4.0f, solvHov ? pal.accentR : pal.trimR, solvHov ? pal.accentG : pal.trimG, solvHov ? pal.accentB : pal.trimB, 0.85f, 1.2f, 4);
    float solvTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, "* SOLVABLE");
    eatsbits::ui::glyph::drawVectorText(r, "* SOLVABLE", solvBtnX + (solvBtnW - solvTxtW) * 0.5f, footerY + 7.0f, 10.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);

    // [ RANDOM GAME ] button
    const float randBtnW = 120.0f;
    const float randBtnX = solvBtnX - randBtnW - 8.0f;
    bool randHov = (mouseX >= randBtnX && mouseX <= randBtnX + randBtnW && mouseY >= footerY && mouseY <= footerY + randBtnH);
    r.drawRoundedRect(randBtnX, footerY, randBtnW, randBtnH, 4.0f, pal.woodR0 + (randHov ? 0.10f : 0.05f), pal.woodG0 + 0.04f, pal.woodB0 + 0.03f, 0.95f, 4);
    r.drawRoundedRectOutline(randBtnX, footerY, randBtnW, randBtnH, 4.0f, randHov ? pal.accentR : pal.trimR, randHov ? pal.accentG : pal.trimG, randHov ? pal.accentB : pal.trimB, 0.85f, 1.2f, 4);
    float randTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, "RANDOM");
    eatsbits::ui::glyph::drawVectorText(r, "RANDOM", randBtnX + (randBtnW - randTxtW) * 0.5f, footerY + 7.0f, 10.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
}

bool GameBrowserDialog::handleMouseButton(
    int button,
    int action,
    float mouseX,
    float mouseY,
    SolitaireBoard& board,
    AudioFoleyEngine& foley
) {
    if (!isOpen_) return false;
    (void)board;
    if (button != 0) return true; // Only respond to primary button

    if (action == 1) { // Mouse down / Touch down
        // 0. Auto-close if clicked/tapped outside dialog modal bounds
        if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ ||
            mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }

        // 1. Check close button [X]
        const float closeBtnSize = 26.0f;
        const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 16.0f;
        const float closeBtnY = dialogY_ + 14.0f;
        if (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize && mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize) {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }

        // 2. Check search bar & clear button
        const float searchBoxX = dialogX_ + 20.0f;
        const float searchBoxW = dialogW_ - 40.0f;
        const float searchBoxH = 32.0f;
        const float searchBoxY = dialogY_ + 14.0f + 20.0f + 18.0f;

        if (mouseX >= searchBoxX && mouseX <= searchBoxX + searchBoxW && mouseY >= searchBoxY && mouseY <= searchBoxY + searchBoxH) {
            searchFocused_ = true;
            cursorBlinkTimer_ = 0.0f;

#if defined(__EMSCRIPTEN__)
            EM_ASM({
                if (window.openMobileKeyboard) {
                    window.openMobileKeyboard(UTF8ToString($0));
                }
            }, searchQuery_.c_str());
#endif

            if (!searchQuery_.empty()) {
                const float clearBtnW = 20.0f;
                const float clearBtnX = searchBoxX + searchBoxW - clearBtnW - 8.0f;
                const float clearBtnY = searchBoxY + 6.0f;
                if (mouseX >= clearBtnX && mouseX <= clearBtnX + clearBtnW && mouseY >= clearBtnY && mouseY <= clearBtnY + clearBtnW) {
                    searchQuery_.clear();
                    scrollOffsetY_ = 0.0f;
                    highlightedIndex_ = 0;
                    updateFilter();
                    foley.triggerSound(FoleySound::WoodClick, 0.6f);
                    return true;
                }
            }
            return true;
        }

        // 3. Check category filter chips
        const float chipY = searchBoxY + searchBoxH + 10.0f;
        const float chipH = 24.0f;
        const float chipPad = 12.0f;
        const auto categories = GameRegistry::instance().categories();
        float chipX = dialogX_ + 20.0f;

        for (const auto& cat : categories) {
            float textW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, cat.c_str());
            float chipW = textW + chipPad * 2.0f;
            if (chipX + chipW > dialogX_ + dialogW_ - 20.0f) break;

            if (mouseX >= chipX && mouseX <= chipX + chipW && mouseY >= chipY && mouseY <= chipY + chipH) {
                selectedCategory_ = cat;
                scrollOffsetY_ = 0.0f;
                highlightedIndex_ = 0;
                updateFilter();
                foley.triggerSound(FoleySound::WoodClick, 0.75f);
                return true;
            }
            chipX += chipW + 8.0f;
        }

        // 4. Check footer buttons
        const float footerY = dialogY_ + dialogH_ - 38.0f;
        const float closeFooterBtnW = 74.0f;
        const float closeFooterBtnX = dialogX_ + dialogW_ - closeFooterBtnW - 16.0f;
        const float randBtnH = 26.0f;
        const float solvBtnW = 120.0f;
        const float solvBtnX = closeFooterBtnX - solvBtnW - 8.0f;
        const float randBtnW = 120.0f;
        const float randBtnX = solvBtnX - randBtnW - 8.0f;

        // [ CLOSE ]
        if (mouseX >= closeFooterBtnX && mouseX <= closeFooterBtnX + closeFooterBtnW && mouseY >= footerY && mouseY <= footerY + randBtnH) {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }

        // [ * SOLVABLE ]
        if (mouseX >= solvBtnX && mouseX <= solvBtnX + solvBtnW && mouseY >= footerY && mouseY <= footerY + randBtnH) {
            if (!filteredGames_.empty() && highlightedIndex_ >= 0 && highlightedIndex_ < static_cast<int>(filteredGames_.size())) {
                const auto& sel = filteredGames_[static_cast<size_t>(highlightedIndex_)];
                activeGameId_ = sel.id;
                close();
                foley.triggerSound(FoleySound::Shuffle, 0.9f);
                if (onSolvableSelected) onSolvableSelected(sel);
                return true;
            }
        }

        // [ RANDOM GAME ]
        if (mouseX >= randBtnX && mouseX <= randBtnX + randBtnW && mouseY >= footerY && mouseY <= footerY + randBtnH) {
            if (!filteredGames_.empty()) {
                std::random_device rd;
                auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                uint32_t idx = static_cast<uint32_t>((rd() ^ nowVal) % filteredGames_.size());
                const auto& sel = filteredGames_[idx];
                activeGameId_ = sel.id;
                highlightedIndex_ = static_cast<int>(idx);
                close();
                foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
                if (onGameSelected) onGameSelected(sel);
                return true;
            }
        }

        // 5. Initiate drag-to-scroll tracking in cards area
        if (mouseY >= cardsAreaY_ && mouseY <= cardsAreaY_ + cardsAreaH_) {
            isMouseDown_ = true;
            isDragging_ = false;
            dragStartY_ = mouseY;
            initialScrollY_ = scrollOffsetY_;
            return true;
        }
    } else if (action == 0) { // Mouse up / Touch up
        if (isMouseDown_) {
            isMouseDown_ = false;
            if (isDragging_) {
                isDragging_ = false;
                return true; // Scroll gesture completed, do not select item
            }

            // Clean click / tap without drag: select the card under cursor
            if (mouseY >= cardsAreaY_ && mouseY <= cardsAreaY_ + cardsAreaH_) {
                const float cardPad = 6.0f;
                const float cardW = dialogW_ - 48.0f;
                const float cardH = 50.0f;
                const float cx = dialogX_ + 24.0f;

                for (size_t i = 0; i < filteredGames_.size(); ++i) {
                    const float cy = cardsAreaY_ + cardPad + static_cast<float>(i) * (cardH + cardPad) - scrollOffsetY_;

                    if (mouseX >= cx && mouseX <= cx + cardW && mouseY >= cy && mouseY <= cy + cardH) {
                        const auto& sel = filteredGames_[i];

                        // Check if star favorite button was clicked
                        const float starBtnW = 28.0f;
                        const float starBtnX = cx + cardW - starBtnW - 8.0f;
                        const float starBtnY = cy + (cardH - starBtnW) * 0.5f;
                        if (mouseX >= starBtnX && mouseX <= starBtnX + starBtnW &&
                            mouseY >= starBtnY && mouseY <= starBtnY + starBtnW) {
                            SettingsManager::instance().toggleFavorite(sel.id);
                            foley.triggerSound(FoleySound::WoodClick, 0.85f);
                            if (selectedCategory_ == "FAVORITES") {
                                updateFilter();
                            }
                            return true;
                        }

                        activeGameId_ = sel.id;
                        highlightedIndex_ = static_cast<int>(i);
                        close();
                        foley.triggerSound(FoleySound::Deal, 0.85f);
                        if (onGameSelected) onGameSelected(sel);
                        return true;
                    }
                }
            }
        }
    }

    return true; // Click consumed by modal dialog
}

bool GameBrowserDialog::handleKey(
    int key,
    int action,
    int mods,
    SolitaireBoard& board,
    AudioFoleyEngine& foley
) {
    (void)mods;
    (void)board;
    if (!isOpen_) return false;
    if (action != 1 && action != 2) return true; // Key down or repeat

    if (key == 256) { // GLFW_KEY_ESCAPE
        if (!searchQuery_.empty()) {
            searchQuery_.clear();
            scrollOffsetY_ = 0.0f;
            highlightedIndex_ = 0;
            updateFilter();
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
        } else {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
        }
        return true;
    }

    if (key == 257) { // GLFW_KEY_ENTER
        if (!filteredGames_.empty() && highlightedIndex_ >= 0 && highlightedIndex_ < static_cast<int>(filteredGames_.size())) {
            const auto& sel = filteredGames_[static_cast<size_t>(highlightedIndex_)];
            activeGameId_ = sel.id;
            close();
            foley.triggerSound(FoleySound::Deal, 0.85f);
            if (onGameSelected) onGameSelected(sel);
            return true;
        }
    }

    if (key == 259) { // GLFW_KEY_BACKSPACE
        if (!searchQuery_.empty()) {
            searchQuery_.pop_back();
            scrollOffsetY_ = 0.0f;
            highlightedIndex_ = 0;
            updateFilter();
            cursorBlinkTimer_ = 0.0f;
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
            return true;
        }
    }

    if (key == 265) { // GLFW_KEY_UP
        if (highlightedIndex_ > 0) {
            --highlightedIndex_;
            ensureVisible(highlightedIndex_);
            foley.triggerSound(FoleySound::WoodClick, 0.4f);
            return true;
        }
    }

    if (key == 264) { // GLFW_KEY_DOWN
        if (highlightedIndex_ + 1 < static_cast<int>(filteredGames_.size())) {
            ++highlightedIndex_;
            ensureVisible(highlightedIndex_);
            foley.triggerSound(FoleySound::WoodClick, 0.4f);
            return true;
        }
    }

    return true;
}

bool GameBrowserDialog::handleChar(unsigned int codepoint) {
    if (!isOpen_) return false;
    if (codepoint >= 32 && codepoint <= 126) {
        searchQuery_ += static_cast<char>(codepoint);
        searchFocused_ = true;
        cursorBlinkTimer_ = 0.0f;
        scrollOffsetY_ = 0.0f;
        highlightedIndex_ = 0;
        updateFilter();
        return true;
    }
    return false;
}

bool GameBrowserDialog::handleScroll(float yoffset) {
    if (!isOpen_) return false;
    scrollOffsetY_ = std::clamp(scrollOffsetY_ - yoffset * 32.0f, 0.0f, maxScrollY_);
    return true;
}

} // namespace solengine
