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

GameBrowserDialog::~GameBrowserDialog() {
    close();
}

void GameBrowserDialog::open(const std::string& activeGameId) {
    GameRegistry::instance().initialize();
    isOpen_ = true;
    searchFocused_ = true;
    isMouseDown_ = false;
    isDragging_ = false;
    activeGameId_ = activeGameId;
    searchQuery_ = "";
    selectedCategory_ = "ALL";
    selectedMechanic_ = "ALL";
    selectedVisibility_ = "ALL";
    scrollOffsetY_ = 0.0f;
    highlightedIndex_ = 0;
    lastClickedIndex_ = -1;
    cursorBlinkTimer_ = 0.0f;
    previewGameId_.clear();
    previewSeedCursor_ = 0;
    previewUserPaused_ = false;
    updateFilter();

    for (size_t i = 0; i < filteredGames_.size(); ++i) {
        if (filteredGames_[i].id == activeGameId_) {
            highlightedIndex_ = static_cast<int>(i);
            ensureVisible(highlightedIndex_);
            break;
        }
    }
    syncPreviewToSelection(false);
}

void GameBrowserDialog::close() noexcept {
    isOpen_ = false;
    isMouseDown_ = false;
    isDragging_ = false;
    previewAutoPlay_.stop();
    previewSolvePending_ = false;
    previewHeuristicMode_ = false;
    previewHasBoard_ = false;
    previewGameId_.clear();
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        if (window.closeMobileKeyboard) {
            window.closeMobileKeyboard();
        }
    });
#endif
}

void GameBrowserDialog::syncPreviewToSelection(bool forceNextSeed) {
    if (filteredGames_.empty() || highlightedIndex_ < 0 ||
        highlightedIndex_ >= static_cast<int>(filteredGames_.size())) {
        previewAutoPlay_.stop();
        previewSolvePending_ = false;
        previewHeuristicMode_ = false;
        previewHasBoard_ = false;
        previewGameId_.clear();
        return;
    }

    const SolitaireGameDef& def = filteredGames_[static_cast<size_t>(highlightedIndex_)];
    const bool sameGame = (def.id == previewGameId_ && previewHasBoard_);
    if (sameGame && !forceNextSeed) {
        return;
    }

    if (!sameGame) {
        previewGameId_ = def.id;
        previewSeedCursor_ = 0;
    } else {
        ++previewSeedCursor_;
    }

    previewDef_ = def;
    if (!def.demoSeeds.empty()) {
        previewSeed_ = def.demoSeeds[previewSeedCursor_ % def.demoSeeds.size()];
    } else if (def.demoSeed != 0) {
        previewSeed_ = def.demoSeed + static_cast<uint32_t>(previewSeedCursor_);
    } else {
        previewSeed_ = 2026u + static_cast<uint32_t>(previewSeedCursor_);
    }

    previewAutoPlay_.stop();
    previewAutoPlay_.setSpeed(previewSpeed_);
    previewSolvePending_ = false;
    previewHeuristicMode_ = false;
    previewHeuristicTimer_ = 0.0f;
    previewLoopTimer_ = 0.0f;

    // Deal immediately on an isolated preview board (Frame 0 instant layout!)
    previewBoard_.setLayoutMode(ResponsiveLayoutMode::DesktopStandard);
    previewBoard_.newDeal(previewSeed_, def);
    previewBoard_.computeLayout(640.0f, 400.0f);
    previewBoard_.triggerDealAnimation();
    previewHasBoard_ = true;

    const std::string cacheKey = def.id + "#" + std::to_string(previewSeed_);
    auto it = previewMoveCache_.find(cacheKey);
    if (it != previewMoveCache_.end() && !it->second.empty()) {
        previewAutoPlay_.startCachedMoves(it->second, previewSeed_);
        previewAutoPlay_.setSpeed(previewSpeed_);
        if (previewUserPaused_) {
            previewAutoPlay_.pause();
        }
    } else {
        previewSolvePending_ = true;
        previewDebounceTimer_ = forceNextSeed ? 0.05f : 0.22f;
    }
}

bool GameBrowserDialog::stepPreviewHeuristic() {
    if (!previewHasBoard_ || previewBoard_.isVictory()) return false;

    const HintMove hint = previewBoard_.findBestHint();
    previewBoard_.clearHint();
    if (hint.valid) {
        const auto& piles = previewBoard_.piles();
        if (hint.fromPile < piles.size() && piles[hint.fromPile].type == PileType::Stock &&
            hint.fromPile == hint.toPile) {
            if (previewBoard_.drawFromStock()) return true;
        }
        if (previewBoard_.executeMove(hint.fromPile, hint.cardIndex, hint.toPile, true)) {
            return true;
        }
        if (previewBoard_.smartMove(hint.fromPile, hint.cardIndex)) {
            return true;
        }
    }

    if (previewBoard_.drawFromStock()) {
        return true;
    }
    return false;
}

void GameBrowserDialog::update(float dt) {
    if (!isOpen_) return;
    const float clampedDt = std::clamp(dt, 0.0f, 0.05f);

    if (!previewHasBoard_) {
        syncPreviewToSelection(false);
    }
    if (!previewHasBoard_) return;

    previewBoard_.updateAnimations(clampedDt);

    if (previewSolvePending_) {
        previewDebounceTimer_ -= clampedDt;
        if (previewDebounceTimer_ <= 0.0f) {
            previewSolvePending_ = false;
            if (previewDef_.supportsSolver || previewBoard_.hasScriptDemoStep()) {
                previewAutoPlay_.setSpeed(previewSpeed_);
                const uint32_t maxStates = (previewDef_.decks >= 2) ? 20000u : 12000u;
                previewAutoPlay_.startPreview(previewBoard_, maxStates, 450.0f);
                if (previewUserPaused_ && previewAutoPlay_.isPlaying()) {
                    previewAutoPlay_.pause();
                }
            } else {
                previewHeuristicMode_ = true;
                previewHeuristicTimer_ = 0.35f;
            }
        }
        return;
    }

    if (previewHeuristicMode_) {
        if (previewUserPaused_) return;
        const float iv = previewAutoPlay_.moveInterval();
        previewHeuristicTimer_ += clampedDt;
        if (previewHeuristicTimer_ >= iv) {
            previewHeuristicTimer_ = 0.0f;
            if (!stepPreviewHeuristic() || previewBoard_.isVictory()) {
                previewLoopTimer_ += iv;
                if (previewLoopTimer_ >= 1.8f) {
                    syncPreviewToSelection(true);
                }
            } else {
                previewLoopTimer_ = 0.0f;
            }
        }
        return;
    }

    if (previewAutoPlay_.isActive()) {
        const AutoPlayState prevState = previewAutoPlay_.state();
        previewAutoPlay_.update(clampedDt, previewBoard_, /*foley=*/nullptr);
        const AutoPlayState curState = previewAutoPlay_.state();

        if (prevState == AutoPlayState::Solving && curState == AutoPlayState::Playing) {
            if (!previewAutoPlay_.moves().empty()) {
                if (previewMoveCache_.size() >= 64) {
                    previewMoveCache_.clear();
                }
                const std::string cacheKey = previewGameId_ + "#" + std::to_string(previewSeed_);
                previewMoveCache_[cacheKey] = previewAutoPlay_.moves();
            }
            previewAutoPlay_.setSpeed(previewSpeed_);
            if (previewUserPaused_) {
                previewAutoPlay_.pause();
            }
        } else if (curState == AutoPlayState::Failed) {
            previewAutoPlay_.stop();
            previewHeuristicMode_ = true;
            previewHeuristicTimer_ = 0.35f;
        } else if (curState == AutoPlayState::Completed || previewBoard_.isVictory()) {
            if (!previewUserPaused_) {
                previewLoopTimer_ += clampedDt;
                if (previewLoopTimer_ >= 1.8f) {
                    syncPreviewToSelection(true);
                }
            }
        }
    }
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
    filteredGames_ = GameRegistry::instance().search(
        searchQuery_, selectedCategory_, selectedMechanic_, selectedVisibility_);
    if (highlightedIndex_ >= static_cast<int>(filteredGames_.size())) {
        highlightedIndex_ = std::max(0, static_cast<int>(filteredGames_.size()) - 1);
    }
    syncPreviewToSelection(false);
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

bool GameBrowserDialog::launchSelectedGame(AudioFoleyEngine& foley) {
    if (filteredGames_.empty() || highlightedIndex_ < 0 ||
        highlightedIndex_ >= static_cast<int>(filteredGames_.size())) {
        return false;
    }

    const auto sel = filteredGames_[static_cast<size_t>(highlightedIndex_)];
    activeGameId_ = sel.id;
    lastClickedIndex_ = -1;
    close();

    if (SettingsManager::instance().settings().guaranteeSolvable && onSolvableSelected) {
        foley.triggerSound(FoleySound::Shuffle, 0.9f);
        onSolvableSelected(sel);
    } else {
        foley.triggerSound(FoleySound::Deal, 0.85f);
        if (onGameSelected) onGameSelected(sel);
    }
    return true;
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

    // 2. Responsive chassis sizing (Desktop split master-detail / Mobile pinned top stage)
    isMobileLayout_ = (screenW < 720.0f) || (screenW < screenH);
    const bool isMobile = isMobileLayout_;
    if (isMobile) {
        dialogX_ = 0.0f;
        dialogY_ = 0.0f;
        dialogW_ = screenW;
        dialogH_ = std::min(screenH * 0.96f, 760.0f);
    } else {
        dialogW_ = std::clamp(screenW * 0.86f, 760.0f, 1080.0f);
        dialogH_ = std::clamp(screenH * 0.86f, 520.0f, 680.0f);
        dialogX_ = (screenW - dialogW_) * 0.5f;
        dialogY_ = (screenH - dialogH_) * 0.5f;
    }

    stageRect_ = {};
    pauseBtnRect_ = {};
    speedBtnRect_ = {};
    nextSeedBtnRect_ = {};
    collapseBtnRect_ = {};

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
        eatsbits::ui::glyph::drawVectorText(r, "Search by name, family, mechanic, visibility, or tag (e.g. open, clearing, radial)...", searchBoxX + 15.0f, curY + 9.5f, 11.0f, 0.55f, 0.52f, 0.48f, 0.85f);
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

    // 5b. Secondary classification bar: Core Mechanic (left) + Visibility (right)
    curY += chipH + 6.0f;
    const float subChipH = 20.0f;
    const float subChipPad = 9.0f;
    {
        struct FilterOpt { const char* label; const char* value; };
        static constexpr FilterOpt kMechOpts[] = {
            {"ALL", "ALL"}, {"BUILDING", "building"}, {"CLEARING", "clearing"},
            {"PAIRING", "pairing"}, {"MATCHING", "matching"}, {"RPG", "rpg"}
        };
        static constexpr FilterOpt kVisOpts[] = {
            {"ANY INFO", "ALL"}, {"OPEN", "open"}, {"HALF-OPEN", "half_open"}, {"CLOSED", "closed"}
        };

        // Compute right-aligned visibility group width first so mechanic chips don't collide
        float visTotalW = 0.0f;
        for (const auto& v : kVisOpts) {
            visTotalW += eatsbits::ui::glyph::getVectorTextWidth(8.5f, v.label) + subChipPad * 2.0f + 5.0f;
        }
        const float visStartX = dialogX_ + dialogW_ - 20.0f - visTotalW + 5.0f;

        float mx = dialogX_ + 20.0f;
        for (const auto& m : kMechOpts) {
            float tw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, m.label);
            float cw = tw + subChipPad * 2.0f;
            if (mx + cw + 10.0f > visStartX) break;
            bool isSel = (selectedMechanic_ == m.value);
            bool hov = (mouseX >= mx && mouseX <= mx + cw && mouseY >= curY && mouseY <= curY + subChipH);
            r.drawRoundedRect(mx, curY, cw, subChipH, 10.0f,
                              isSel ? (pal.accentR * 0.28f + pal.woodR0 * 0.72f) : (pal.woodR0 + (hov ? 0.07f : 0.02f)),
                              isSel ? (pal.accentG * 0.28f + pal.woodG0 * 0.72f) : (pal.woodG0 + (hov ? 0.05f : 0.01f)),
                              isSel ? (pal.accentB * 0.28f + pal.woodB0 * 0.72f) : (pal.woodB0 + (hov ? 0.04f : 0.01f)),
                              0.92f, 3);
            r.drawRoundedRectOutline(mx, curY, cw, subChipH, 10.0f,
                                     isSel ? pal.accentR : pal.trimR,
                                     isSel ? pal.accentG : pal.trimG,
                                     isSel ? pal.accentB : pal.trimB,
                                     isSel ? 0.95f : 0.35f, 1.0f, 3);
            eatsbits::ui::glyph::drawVectorText(r, m.label, mx + subChipPad, curY + 5.5f, 8.5f,
                                                isSel ? pal.accentR : 0.80f,
                                                isSel ? pal.accentG : 0.78f,
                                                isSel ? pal.accentB : 0.74f, 0.95f);
            mx += cw + 5.0f;
        }

        float vx = visStartX;
        for (const auto& v : kVisOpts) {
            float tw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, v.label);
            float cw = tw + subChipPad * 2.0f;
            bool isSel = (selectedVisibility_ == v.value);
            bool hov = (mouseX >= vx && mouseX <= vx + cw && mouseY >= curY && mouseY <= curY + subChipH);
            r.drawRoundedRect(vx, curY, cw, subChipH, 10.0f,
                              isSel ? 0.12f : (pal.woodR0 + (hov ? 0.07f : 0.02f)),
                              isSel ? 0.26f : (pal.woodG0 + (hov ? 0.05f : 0.01f)),
                              isSel ? 0.28f : (pal.woodB0 + (hov ? 0.04f : 0.01f)),
                              0.92f, 3);
            r.drawRoundedRectOutline(vx, curY, cw, subChipH, 10.0f,
                                     isSel ? 0.42f : pal.trimR,
                                     isSel ? 0.86f : pal.trimG,
                                     isSel ? 0.88f : pal.trimB,
                                     isSel ? 0.95f : 0.35f, 1.0f, 3);
            eatsbits::ui::glyph::drawVectorText(r, v.label, vx + subChipPad, curY + 5.5f, 8.5f,
                                                isSel ? 0.62f : 0.80f,
                                                isSel ? 0.96f : 0.78f,
                                                isSel ? 0.98f : 0.74f, 0.95f);
            vx += cw + 5.0f;
        }
    }

    // 6. Body layout: Desktop Master-Detail Split vs Mobile Pinned Top Demo Stage
    curY += subChipH + 10.0f;
    const float bodyY = curY;
    const float footerH = 46.0f;
    const float bodyH = std::max(120.0f, (dialogY_ + dialogH_ - footerH) - bodyY);

    auto drawMiniCtrlButton = [&](const RectF& rc, const char* label, bool activeHighlight) {
        const bool hov = rc.contains(mouseX, mouseY);
        r.drawRoundedRect(
            rc.x, rc.y, rc.w, rc.h, 4.0f,
            activeHighlight ? (pal.accentR * 0.30f + 0.06f) : (hov ? 0.18f : 0.08f),
            activeHighlight ? (pal.accentG * 0.30f + 0.05f) : (hov ? 0.16f : 0.07f),
            activeHighlight ? (pal.accentB * 0.30f + 0.04f) : (hov ? 0.14f : 0.06f),
            0.88f, 3
        );
        r.drawRoundedRectOutline(
            rc.x, rc.y, rc.w, rc.h, 4.0f,
            (hov || activeHighlight) ? pal.accentR : pal.trimR,
            (hov || activeHighlight) ? pal.accentG : pal.trimG,
            (hov || activeHighlight) ? pal.accentB : pal.trimB,
            hov ? 0.95f : 0.60f, 1.0f, 3
        );
        const float tw = eatsbits::ui::glyph::getVectorTextWidth(9.0f, label);
        eatsbits::ui::glyph::drawVectorText(
            r, label, rc.x + (rc.w - tw) * 0.5f, rc.y + (rc.h - 9.0f) * 0.5f, 9.0f,
            (hov || activeHighlight) ? pal.accentR : 0.92f,
            (hov || activeHighlight) ? pal.accentG : 0.90f,
            (hov || activeHighlight) ? pal.accentB : 0.84f, 0.98f
        );
    };

    auto getPreviewStatusLabel = [&]() -> const char* {
        if (!previewHasBoard_) return "PREVIEW";
        if (previewUserPaused_ || previewAutoPlay_.isPaused()) return "PAUSED";
        if (previewSolvePending_ || previewAutoPlay_.isSolving()) return "SOLVING...";
        return "LIVE DEMO";
    };

    const bool hasSelection = !filteredGames_.empty() && highlightedIndex_ >= 0 &&
                              highlightedIndex_ < static_cast<int>(filteredGames_.size());

    if (isMobile) {
        // Mobile: Pinned Top Live Demo Stage + Scrollable List below
        listX_ = dialogX_ + 12.0f;
        listW_ = dialogW_ - 24.0f;

        if (previewCollapsedMobile_) {
            const float barH = 30.0f;
            stageRect_ = {listX_, bodyY, listW_, barH};
            r.drawRoundedRect(stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, 6.0f,
                              pal.woodR0 + 0.03f, pal.woodG0 + 0.025f, pal.woodB0 + 0.02f, 0.94f, 4);
            r.drawRoundedRectOutline(stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, 6.0f,
                                     pal.trimR, pal.trimG, pal.trimB, 0.55f, 1.0f, 4);
            std::string barTxt = hasSelection
                ? ("Live Demo: " + filteredGames_[static_cast<size_t>(highlightedIndex_)].name)
                : "Live Demo";
            eatsbits::ui::glyph::drawVectorText(r, barTxt.c_str(), stageRect_.x + 10.0f, stageRect_.y + 9.5f, 10.0f,
                                                pal.accentR, pal.accentG, pal.accentB, 0.95f);
            collapseBtnRect_ = {stageRect_.x + stageRect_.w - 64.0f, stageRect_.y + 4.0f, 58.0f, 22.0f};
            drawMiniCtrlButton(collapseBtnRect_, "SHOW", false);

            cardsAreaY_ = bodyY + barH + 8.0f;
            cardsAreaH_ = std::max(80.0f, (dialogY_ + dialogH_ - footerH) - cardsAreaY_);
        } else {
            const float stageH = std::clamp(bodyH * 0.40f, 136.0f, 210.0f);
            stageRect_ = {listX_, bodyY, listW_, stageH};
            if (previewHasBoard_) {
                TableRenderer::renderMiniBoard(
                    r, previewBoard_, stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, pal,
                    previewHeuristicMode_ ? nullptr : &previewAutoPlay_
                );
            } else {
                r.drawRoundedRect(stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, 6.0f,
                                  pal.feltR1, pal.feltG1, pal.feltB1, 0.90f, 4);
                r.drawRoundedRectOutline(stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, 6.0f,
                                         pal.trimR, pal.trimG, pal.trimB, 0.50f, 1.0f, 4);
            }

            // Compact top-left status & game name pill
            std::string pillStr = std::string(getPreviewStatusLabel());
            if (hasSelection) {
                pillStr += "  |  " + filteredGames_[static_cast<size_t>(highlightedIndex_)].name;
            }
            const float maxPillW = std::max(80.0f, stageRect_.w - 154.0f);
            float pillTw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, pillStr.c_str());
            while (pillTw > maxPillW - 14.0f && pillStr.size() > 8) {
                pillStr.resize(pillStr.size() - 4);
                pillStr += "...";
                pillTw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, pillStr.c_str());
            }
            r.drawRoundedRect(stageRect_.x + 6.0f, stageRect_.y + 6.0f, pillTw + 12.0f, 18.0f, 9.0f,
                              0.06f, 0.05f, 0.04f, 0.80f, 3);
            eatsbits::ui::glyph::drawVectorText(r, pillStr.c_str(), stageRect_.x + 12.0f, stageRect_.y + 10.5f, 8.5f,
                                                pal.accentR, pal.accentG, pal.accentB, 0.96f);

            // Top-right micro-controls (Pause/Play, Next Seed, Hide)
            const float ctrlH = 20.0f;
            const float ctrlY = stageRect_.y + 6.0f;
            collapseBtnRect_ = {stageRect_.x + stageRect_.w - 46.0f, ctrlY, 40.0f, ctrlH};
            nextSeedBtnRect_ = {collapseBtnRect_.x - 42.0f, ctrlY, 38.0f, ctrlH};
            pauseBtnRect_ = {nextSeedBtnRect_.x - 36.0f, ctrlY, 32.0f, ctrlH};
            drawMiniCtrlButton(pauseBtnRect_, (previewUserPaused_ || previewAutoPlay_.isPaused()) ? "PLAY" : "II", previewUserPaused_);
            drawMiniCtrlButton(nextSeedBtnRect_, "NEXT", false);
            drawMiniCtrlButton(collapseBtnRect_, "HIDE", false);

            cardsAreaY_ = bodyY + stageH + 8.0f;
            cardsAreaH_ = std::max(80.0f, (dialogY_ + dialogH_ - footerH) - cardsAreaY_);
        }
    } else {
        // Desktop: Left Scrollable List (~45%) + Right Live Demo & Details Pane (~55%)
        const float innerX = dialogX_ + 16.0f;
        const float innerW = dialogW_ - 32.0f;
        const float splitGap = 12.0f;
        listX_ = innerX;
        listW_ = std::floor((innerW - splitGap) * 0.45f);
        cardsAreaY_ = bodyY;
        cardsAreaH_ = bodyH;

        const float paneX = listX_ + listW_ + splitGap;
        const float paneW = (innerX + innerW) - paneX;
        const float paneY = bodyY;
        const float paneH = bodyH;

        // Right pane backing panel
        r.drawRoundedRect(paneX, paneY, paneW, paneH, 6.0f,
                          pal.woodR0 + 0.02f, pal.woodG0 + 0.015f, pal.woodB0 + 0.01f, 0.90f, 4);
        r.drawRoundedRectOutline(paneX, paneY, paneW, paneH, 6.0f,
                                 pal.trimR, pal.trimG, pal.trimB, 0.42f, 1.0f, 4);

        const float stagePad = 8.0f;
        const float stageH = std::clamp(paneH * 0.62f, 175.0f, 340.0f);
        stageRect_ = {paneX + stagePad, paneY + stagePad, paneW - stagePad * 2.0f, stageH};

        if (previewHasBoard_) {
            TableRenderer::renderMiniBoard(
                r, previewBoard_, stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, pal,
                previewHeuristicMode_ ? nullptr : &previewAutoPlay_
            );
        } else {
            r.drawRoundedRect(stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, 6.0f,
                              pal.feltR1, pal.feltG1, pal.feltB1, 0.90f, 4);
            r.drawRoundedRectOutline(stageRect_.x, stageRect_.y, stageRect_.w, stageRect_.h, 6.0f,
                                     pal.trimR, pal.trimG, pal.trimB, 0.50f, 1.0f, 4);
        }

        // Top-left live status badge inside stage
        const char* statusLbl = getPreviewStatusLabel();
        const float statusTw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, statusLbl);
        r.drawRoundedRect(stageRect_.x + 8.0f, stageRect_.y + 8.0f, statusTw + 14.0f, 18.0f, 9.0f,
                          0.06f, 0.05f, 0.04f, 0.80f, 3);
        r.drawRoundedRectOutline(stageRect_.x + 8.0f, stageRect_.y + 8.0f, statusTw + 14.0f, 18.0f, 9.0f,
                                 pal.accentR, pal.accentG, pal.accentB, 0.65f, 1.0f, 3);
        eatsbits::ui::glyph::drawVectorText(r, statusLbl, stageRect_.x + 15.0f, stageRect_.y + 12.5f, 8.5f,
                                            pal.accentR, pal.accentG, pal.accentB, 0.98f);

        // Bottom-right micro transport controls inside stage
        const float ctrlH = 22.0f;
        const float ctrlY = stageRect_.y + stageRect_.h - ctrlH - 8.0f;
        nextSeedBtnRect_ = {stageRect_.x + stageRect_.w - 48.0f, ctrlY, 40.0f, ctrlH};
        speedBtnRect_ = {nextSeedBtnRect_.x - 36.0f, ctrlY, 32.0f, ctrlH};
        pauseBtnRect_ = {speedBtnRect_.x - 42.0f, ctrlY, 38.0f, ctrlH};
        drawMiniCtrlButton(pauseBtnRect_, (previewUserPaused_ || previewAutoPlay_.isPaused()) ? "PLAY" : "II", previewUserPaused_);
        drawMiniCtrlButton(speedBtnRect_, previewAutoPlay_.speedLabel(), false);
        drawMiniCtrlButton(nextSeedBtnRect_, "NEXT", false);

        // Selected game details below the stage
        if (hasSelection) {
            const auto& sel = filteredGames_[static_cast<size_t>(highlightedIndex_)];
            const float infoX = paneX + 12.0f;
            float infoY = stageRect_.y + stageRect_.h + 10.0f;
            const float infoW = paneW - 24.0f;
            const float infoBottom = paneY + paneH - 8.0f;

            // Title + seed badge on right
            eatsbits::ui::glyph::drawVectorText(r, sel.name.c_str(), infoX, infoY, 13.5f,
                                                pal.accentR, pal.accentG, pal.accentB, 1.0f);

            std::string seedTag = "Seed #" + std::to_string(previewSeed_);
            if (sel.demoSeeds.size() > 1) {
                seedTag += " (" + std::to_string((previewSeedCursor_ % sel.demoSeeds.size()) + 1) +
                           "/" + std::to_string(sel.demoSeeds.size()) + ")";
            }
            const float seedTw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, seedTag.c_str());
            eatsbits::ui::glyph::drawVectorText(r, seedTag.c_str(), infoX + infoW - seedTw, infoY + 3.0f, 8.5f,
                                                0.72f, 0.70f, 0.65f, 0.88f);
            infoY += 20.0f;

            // Metadata line
            std::string metaLine = sel.family + "  *  " + std::to_string(sel.decks) +
                                   (sel.decks == 1 ? " Deck" : " Decks");
            if (sel.deckCards > 0) metaLine += " (" + std::to_string(sel.deckCards) + " Cards)";
            metaLine += "  *  " + std::to_string(sel.tableaus) + " Cols";
            if (sel.freecells > 0) metaLine += "  *  " + std::to_string(sel.freecells) + " Cells";
            if (!sel.visibility.empty()) {
                metaLine += "  *  ";
                if (sel.visibility == "open") metaLine += "Open Info";
                else if (sel.visibility == "closed") metaLine += "Hidden Info";
                else metaLine += "Half-Open";
            }
            eatsbits::ui::glyph::drawVectorText(r, metaLine.c_str(), infoX, infoY, 9.2f,
                                                pal.trimR, pal.trimG, pal.trimB, 0.90f);
            infoY += 17.0f;

            // Word-wrapped description
            if (!sel.description.empty() && infoY + 12.0f < infoBottom) {
                auto descScissor = r.makeScissorScope(infoX - 2.0f, infoY - 2.0f, infoW + 4.0f, infoBottom - infoY + 2.0f);
                const float fontSize = 9.8f;
                const float lineH = 14.0f;
                std::string line;
                size_t pos = 0;
                while (pos < sel.description.size() && infoY + lineH <= infoBottom + 2.0f) {
                    size_t nextSp = sel.description.find(' ', pos);
                    std::string word = (nextSp == std::string::npos)
                        ? sel.description.substr(pos)
                        : sel.description.substr(pos, nextSp - pos);
                    std::string cand = line.empty() ? word : (line + " " + word);
                    if (!line.empty() && eatsbits::ui::glyph::getVectorTextWidth(fontSize, cand.c_str()) > infoW) {
                        eatsbits::ui::glyph::drawVectorText(r, line.c_str(), infoX, infoY, fontSize,
                                                            0.86f, 0.84f, 0.78f, 0.92f);
                        infoY += lineH;
                        line = word;
                    } else {
                        line = std::move(cand);
                    }
                    if (nextSp == std::string::npos) break;
                    pos = nextSp + 1;
                }
                if (!line.empty() && infoY + lineH <= infoBottom + 2.0f) {
                    eatsbits::ui::glyph::drawVectorText(r, line.c_str(), infoX, infoY, fontSize,
                                                        0.86f, 0.84f, 0.78f, 0.92f);
                }
            }
        }
    }

    // Inner background panel for game list
    r.drawRoundedRect(listX_, cardsAreaY_, listW_, cardsAreaH_, 6.0f,
                      pal.woodR0 + 0.02f, pal.woodG0 + 0.015f, pal.woodB0 + 0.01f, 0.90f, 4);
    r.drawRoundedRectOutline(listX_, cardsAreaY_, listW_, cardsAreaH_, 6.0f,
                             pal.trimR, pal.trimG, pal.trimB, 0.40f, 1.0f, 4);

    const float cardPad = 6.0f;
    const float cardW = listW_ - 16.0f;
    const float cardH = 50.0f;

    const size_t totalCards = filteredGames_.size();
    const float totalContentH = static_cast<float>(totalCards) * (cardH + cardPad) + cardPad;
    maxScrollY_ = std::max(0.0f, totalContentH - cardsAreaH_);
    scrollOffsetY_ = std::clamp(scrollOffsetY_, 0.0f, maxScrollY_);

    // STRICT SCISSOR CLIPPING: Absolutely no cards can overflow or bleed outside the list bounds
    {
        auto scissor = r.makeScissorScope(listX_, cardsAreaY_ + 2.0f, listW_, cardsAreaH_ - 4.0f);

        for (size_t i = 0; i < totalCards; ++i) {
            const auto& g = filteredGames_[i];
            const float cx = listX_ + 8.0f;
            const float cy = cardsAreaY_ + cardPad + static_cast<float>(i) * (cardH + cardPad) - scrollOffsetY_;

            // Skip cards outside the visible clipping area
            if (cy + cardH < cardsAreaY_ || cy > cardsAreaY_ + cardsAreaH_) continue;

            bool isActive = (g.id == activeGameId_);
            bool isHighlighted = (static_cast<int>(i) == highlightedIndex_);
            bool isHov = (!isDragging_ && mouseX >= cx && mouseX <= cx + cardW && mouseY >= cy && mouseY <= cy + cardH &&
                          mouseY >= cardsAreaY_ && mouseY <= cardsAreaY_ + cardsAreaH_);

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

            // Right-aligned Favorite Star & Family badge first so we know max title width
            const float starBtnW = 28.0f;
            const float starBtnX = cx + cardW - starBtnW - 6.0f;
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

            float badgeRightX = starBtnX - 8.0f;

            // 1. Family pill badge
            float famTxtW = eatsbits::ui::glyph::getVectorTextWidth(8.2f, g.family.c_str());
            float famBadgeW = famTxtW + 10.0f;
            badgeRightX -= famBadgeW;
            r.drawRoundedRect(badgeRightX, cy + 7.0f, famBadgeW, 16.0f, 8.0f, pal.woodR0 + 0.10f, pal.woodG0 + 0.08f, pal.woodB0 + 0.06f, 0.95f, 3);
            r.drawRoundedRectOutline(badgeRightX, cy + 7.0f, famBadgeW, 16.0f, 8.0f, pal.trimR, pal.trimG, pal.trimB, 0.60f, 1.0f, 3);
            eatsbits::ui::glyph::drawVectorText(r, g.family.c_str(), badgeRightX + 5.0f, cy + 10.5f, 8.2f, pal.accentR, pal.accentG, pal.accentB, 0.95f);

            // 2. Active status tag (compact)
            if (isActive) {
                float actTxtW = eatsbits::ui::glyph::getVectorTextWidth(8.0f, "ACTIVE");
                badgeRightX -= (actTxtW + 12.0f);
                r.drawRoundedRect(badgeRightX, cy + 7.0f, actTxtW + 8.0f, 16.0f, 3.0f, 0.12f, 0.35f, 0.15f, 0.90f, 2);
                eatsbits::ui::glyph::drawVectorText(r, "ACTIVE", badgeRightX + 4.0f, cy + 10.5f, 8.0f, 0.45f, 1.0f, 0.55f, 1.0f);
            }

            // Title (trimmed cleanly to never collide with badges)
            const float titleMaxW = std::max(40.0f, badgeRightX - (cx + 12.0f) - 6.0f);
            std::string titleStr = g.name;
            while (titleStr.size() > 6 && eatsbits::ui::glyph::getVectorTextWidth(11.5f, titleStr.c_str()) > titleMaxW) {
                titleStr.resize(titleStr.size() - 4);
                titleStr += "...";
            }
            eatsbits::ui::glyph::drawVectorText(r, titleStr.c_str(), cx + 12.0f, cy + 9.0f, 11.5f,
                                                isActive ? 0.40f : ((isHighlighted || isHov) ? pal.accentR : 0.96f),
                                                isActive ? 1.0f : ((isHighlighted || isHov) ? pal.accentG : 0.93f),
                                                isActive ? 0.50f : ((isHighlighted || isHov) ? pal.accentB : 0.88f), 1.0f);

            // Subtitle line: compact metadata on desktop (full description is in right pane), or short description on mobile
            std::string subStr;
            if (!isMobile) {
                subStr = std::to_string(g.decks) + (g.decks == 1 ? " Deck" : " Decks") +
                         " * " + std::to_string(g.tableaus) + " Cols";
                if (g.freecells > 0) subStr += " * " + std::to_string(g.freecells) + " Cells";
                if (!g.visibility.empty() && g.visibility != "half_open") {
                    subStr += (g.visibility == "open") ? " * Open" : " * Closed";
                }
            } else if (!g.description.empty()) {
                subStr = g.description;
            }
            const float subMaxW = std::max(40.0f, starBtnX - (cx + 12.0f) - 8.0f);
            while (subStr.size() > 6 && eatsbits::ui::glyph::getVectorTextWidth(8.8f, subStr.c_str()) > subMaxW) {
                subStr.resize(subStr.size() - 4);
                subStr += "...";
            }
            if (!subStr.empty()) {
                eatsbits::ui::glyph::drawVectorText(r, subStr.c_str(), cx + 12.0f, cy + 29.0f, 8.8f, 0.78f, 0.76f, 0.70f, 0.88f);
            }
        }

        if (filteredGames_.empty()) {
            const char* emptyMsg = (selectedCategory_ == "FAVORITES")
                ? "No favorites yet. Tap the star on any game!"
                : "No games found matching your search.";
            float emptyTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, emptyMsg);
            eatsbits::ui::glyph::drawVectorText(r, emptyMsg, listX_ + (listW_ - emptyTxtW) * 0.5f, cardsAreaY_ + cardsAreaH_ * 0.45f, 10.5f, 0.70f, 0.65f, 0.58f, 0.85f);
        }
    }

    // Scrollbar indicator
    if (maxScrollY_ > 0.0f) {
        const float trackX = listX_ + listW_ - 5.0f;
        const float trackY = cardsAreaY_ + 4.0f;
        const float trackW = 4.0f;
        const float trackH = cardsAreaH_ - 8.0f;
        r.drawRoundedRect(trackX, trackY, trackW, trackH, 2.0f, 0.08f, 0.07f, 0.06f, 0.50f, 2);

        const float thumbH = std::max(24.0f, trackH * (cardsAreaH_ / totalContentH));
        const float thumbY = trackY + (scrollOffsetY_ / maxScrollY_) * (trackH - thumbH);
        r.drawRoundedRect(trackX, thumbY, trackW, thumbH, 2.0f, pal.accentR, pal.accentG, pal.accentB, 0.70f, 2);
    }

    // 7. Footer bar
    const float footerY = dialogY_ + dialogH_ - 39.0f;
    const float btnH = 28.0f;

    // [ ► Play ] button (bottom right)
    const float playBtnW = 92.0f;
    const float playBtnX = dialogX_ + dialogW_ - playBtnW - 16.0f;
    const bool playHov = hasSelection && (mouseX >= playBtnX && mouseX <= playBtnX + playBtnW &&
                                          mouseY >= footerY && mouseY <= footerY + btnH);
    const float playAlpha = hasSelection ? 0.98f : 0.45f;

    r.drawRoundedRect(
        playBtnX, footerY, playBtnW, btnH, 5.0f,
        hasSelection ? (pal.accentR * (playHov ? 0.36f : 0.24f) + pal.woodR0 * 0.72f) : pal.woodR0 + 0.02f,
        hasSelection ? (pal.accentG * (playHov ? 0.36f : 0.24f) + pal.woodG0 * 0.72f) : pal.woodG0 + 0.02f,
        hasSelection ? (pal.accentB * (playHov ? 0.36f : 0.24f) + pal.woodB0 * 0.72f) : pal.woodB0 + 0.02f,
        playAlpha, 4
    );
    r.drawRoundedRectOutline(
        playBtnX, footerY, playBtnW, btnH, 5.0f,
        (hasSelection && playHov) ? pal.accentR : pal.trimR,
        (hasSelection && playHov) ? pal.accentG : pal.trimG,
        (hasSelection && playHov) ? pal.accentB : pal.trimB,
        hasSelection ? (playHov ? 1.0f : 0.85f) : 0.35f,
        playHov ? 1.5f : 1.2f, 4
    );

    const float playTxtW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, "Play");
    const float triW = 8.0f;
    const float triGap = 7.0f;
    const float contentTotalW = triW + triGap + playTxtW;
    const float contentStartX = playBtnX + (playBtnW - contentTotalW) * 0.5f;
    const float triCy = footerY + btnH * 0.5f;
    const float triHalfH = 4.5f;
    r.drawTriangle(
        contentStartX, triCy - triHalfH,
        contentStartX, triCy + triHalfH,
        contentStartX + triW, triCy,
        pal.accentR, pal.accentG, pal.accentB, playAlpha
    );
    eatsbits::ui::glyph::drawVectorText(
        r, "Play", contentStartX + triW + triGap, footerY + 8.0f, 11.0f,
        pal.accentR, pal.accentG, pal.accentB, playAlpha
    );

    // "Solvable" toggle switch to the left of Play
    const bool solvableOn = SettingsManager::instance().settings().guaranteeSolvable;
    const float labelTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, "Solvable");
    const float trackW = 36.0f;
    const float trackH = 18.0f;
    const float labelGap = 8.0f;
    const float toggleTotalW = labelTxtW + labelGap + trackW;
    const float toggleX = playBtnX - toggleTotalW - 16.0f;
    const bool toggleHov = (mouseX >= toggleX - 4.0f && mouseX <= toggleX + toggleTotalW + 4.0f &&
                            mouseY >= footerY && mouseY <= footerY + btnH);

    eatsbits::ui::glyph::drawVectorText(
        r, "Solvable", toggleX, footerY + 8.5f, 10.5f,
        solvableOn ? pal.accentR : (toggleHov ? 0.95f : 0.82f),
        solvableOn ? pal.accentG : (toggleHov ? 0.93f : 0.80f),
        solvableOn ? pal.accentB : (toggleHov ? 0.88f : 0.74f),
        0.95f
    );

    const float trackX = toggleX + labelTxtW + labelGap;
    const float trackY = footerY + (btnH - trackH) * 0.5f;
    r.drawRoundedRect(
        trackX, trackY, trackW, trackH, trackH * 0.5f,
        solvableOn ? (pal.accentR * 0.40f + pal.woodR0 * 0.60f) : (pal.woodR0 + (toggleHov ? 0.06f : 0.02f)),
        solvableOn ? (pal.accentG * 0.40f + pal.woodG0 * 0.60f) : (pal.woodG0 + (toggleHov ? 0.05f : 0.02f)),
        solvableOn ? (pal.accentB * 0.40f + pal.woodB0 * 0.60f) : (pal.woodB0 + (toggleHov ? 0.04f : 0.01f)),
        0.96f, 6
    );
    r.drawRoundedRectOutline(
        trackX, trackY, trackW, trackH, trackH * 0.5f,
        (solvableOn || toggleHov) ? pal.accentR : pal.trimR,
        (solvableOn || toggleHov) ? pal.accentG : pal.trimG,
        (solvableOn || toggleHov) ? pal.accentB : pal.trimB,
        solvableOn ? 0.95f : 0.65f, 1.2f, 6
    );

    const float knobRadius = 6.5f;
    const float knobCx = solvableOn ? (trackX + trackW - 9.0f) : (trackX + 9.0f);
    const float knobCy = trackY + trackH * 0.5f;
    r.drawCircle(
        knobCx, knobCy, knobRadius,
        solvableOn ? pal.accentR : 0.78f,
        solvableOn ? pal.accentG : 0.75f,
        solvableOn ? pal.accentB : 0.70f,
        1.0f, 16
    );

    // Left-aligned variant count / hint (auto-shortens if narrow)
    const float countX = dialogX_ + 20.0f;
    const float availCountW = toggleX - countX - 12.0f;
    std::string longCountStr = "Showing " + std::to_string(filteredGames_.size()) + " variants (Double-click: Play)";
    std::string shortCountStr = std::to_string(filteredGames_.size()) + " variants";
    const std::string& countStr = (eatsbits::ui::glyph::getVectorTextWidth(10.0f, longCountStr.c_str()) <= availCountW)
        ? longCountStr : shortCountStr;
    eatsbits::ui::glyph::drawVectorText(r, countStr.c_str(), countX, footerY + 8.5f, 10.0f, 0.80f, 0.78f, 0.72f, 0.85f);
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

        // 3b. Check secondary Mechanic & Visibility filter chips
        {
            const float subChipY = chipY + chipH + 6.0f;
            const float subChipH = 20.0f;
            const float subChipPad = 9.0f;
            if (mouseY >= subChipY && mouseY <= subChipY + subChipH) {
                struct FilterOpt { const char* label; const char* value; };
                static constexpr FilterOpt kMechOpts[] = {
                    {"ALL", "ALL"}, {"BUILDING", "building"}, {"CLEARING", "clearing"},
                    {"PAIRING", "pairing"}, {"MATCHING", "matching"}, {"RPG", "rpg"}
                };
                static constexpr FilterOpt kVisOpts[] = {
                    {"ANY INFO", "ALL"}, {"OPEN", "open"}, {"HALF-OPEN", "half_open"}, {"CLOSED", "closed"}
                };

                float visTotalW = 0.0f;
                for (const auto& v : kVisOpts) {
                    visTotalW += eatsbits::ui::glyph::getVectorTextWidth(8.5f, v.label) + subChipPad * 2.0f + 5.0f;
                }
                const float visStartX = dialogX_ + dialogW_ - 20.0f - visTotalW + 5.0f;

                float mx = dialogX_ + 20.0f;
                for (const auto& m : kMechOpts) {
                    float cw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, m.label) + subChipPad * 2.0f;
                    if (mx + cw + 10.0f > visStartX) break;
                    if (mouseX >= mx && mouseX <= mx + cw) {
                        selectedMechanic_ = m.value;
                        scrollOffsetY_ = 0.0f;
                        highlightedIndex_ = 0;
                        updateFilter();
                        foley.triggerSound(FoleySound::WoodClick, 0.75f);
                        return true;
                    }
                    mx += cw + 5.0f;
                }

                float vx = visStartX;
                for (const auto& v : kVisOpts) {
                    float cw = eatsbits::ui::glyph::getVectorTextWidth(8.5f, v.label) + subChipPad * 2.0f;
                    if (mouseX >= vx && mouseX <= vx + cw) {
                        selectedVisibility_ = v.value;
                        scrollOffsetY_ = 0.0f;
                        highlightedIndex_ = 0;
                        updateFilter();
                        foley.triggerSound(FoleySound::WoodClick, 0.75f);
                        return true;
                    }
                    vx += cw + 5.0f;
                }
            }
        }

        // 4. Check footer controls ("Solvable" toggle + "► Play" button)
        const float footerY = dialogY_ + dialogH_ - 39.0f;
        const float btnH = 28.0f;
        const float playBtnW = 92.0f;
        const float playBtnX = dialogX_ + dialogW_ - playBtnW - 16.0f;

        const float labelTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, "Solvable");
        const float trackW = 36.0f;
        const float labelGap = 8.0f;
        const float toggleTotalW = labelTxtW + labelGap + trackW;
        const float toggleX = playBtnX - toggleTotalW - 16.0f;

        // "Solvable" toggle switch
        if (mouseX >= toggleX - 4.0f && mouseX <= toggleX + toggleTotalW + 4.0f &&
            mouseY >= footerY && mouseY <= footerY + btnH) {
            auto& s = SettingsManager::instance().settings();
            s.guaranteeSolvable = !s.guaranteeSolvable;
            SettingsManager::instance().save();
            foley.triggerSound(FoleySound::WoodClick, 0.75f);
            return true;
        }

        // [ ► Play ] button
        if (mouseX >= playBtnX && mouseX <= playBtnX + playBtnW &&
            mouseY >= footerY && mouseY <= footerY + btnH) {
            launchSelectedGame(foley);
            return true;
        }

        // 4b. Check Live Demo micro-controls (Pause/Play, Speed, Next Seed, Mobile Hide/Show)
        if (pauseBtnRect_.contains(mouseX, mouseY)) {
            previewUserPaused_ = !previewUserPaused_;
            if (previewUserPaused_) {
                previewAutoPlay_.pause();
            } else {
                previewAutoPlay_.play();
            }
            foley.triggerSound(FoleySound::WoodClick, 0.65f);
            return true;
        }
        if (speedBtnRect_.contains(mouseX, mouseY)) {
            previewAutoPlay_.cycleSpeed();
            previewSpeed_ = previewAutoPlay_.speed();
            foley.triggerSound(FoleySound::WoodClick, 0.65f);
            return true;
        }
        if (nextSeedBtnRect_.contains(mouseX, mouseY)) {
            previewUserPaused_ = false;
            syncPreviewToSelection(true);
            foley.triggerSound(FoleySound::WoodClick, 0.70f);
            return true;
        }
        if (collapseBtnRect_.contains(mouseX, mouseY)) {
            previewCollapsedMobile_ = !previewCollapsedMobile_;
            foley.triggerSound(FoleySound::WoodClick, 0.65f);
            return true;
        }

        // 5. Initiate drag-to-scroll tracking in cards area
        if (mouseX >= listX_ && mouseX <= listX_ + listW_ &&
            mouseY >= cardsAreaY_ && mouseY <= cardsAreaY_ + cardsAreaH_) {
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
            if (mouseX >= listX_ && mouseX <= listX_ + listW_ &&
                mouseY >= cardsAreaY_ && mouseY <= cardsAreaY_ + cardsAreaH_) {
                const float cardPad = 6.0f;
                const float cardW = listW_ - 16.0f;
                const float cardH = 50.0f;
                const float cx = listX_ + 8.0f;

                for (size_t i = 0; i < filteredGames_.size(); ++i) {
                    const float cy = cardsAreaY_ + cardPad + static_cast<float>(i) * (cardH + cardPad) - scrollOffsetY_;

                    if (mouseX >= cx && mouseX <= cx + cardW && mouseY >= cy && mouseY <= cy + cardH) {
                        const auto& sel = filteredGames_[i];

                        // Check if star favorite button was clicked
                        const float starBtnW = 28.0f;
                        const float starBtnX = cx + cardW - starBtnW - 6.0f;
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

                        const int clickedIdx = static_cast<int>(i);
                        const auto now = std::chrono::steady_clock::now();
                        const bool isDoubleClick = (clickedIdx == lastClickedIndex_) &&
                            (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime_).count() <= 400);

                        highlightedIndex_ = clickedIdx;
                        if (isDoubleClick) {
                            launchSelectedGame(foley);
                        } else {
                            lastClickedIndex_ = clickedIdx;
                            lastClickTime_ = now;
                            syncPreviewToSelection(false);
                            foley.triggerSound(FoleySound::WoodClick, 0.65f);
                        }
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
        if (launchSelectedGame(foley)) {
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
            syncPreviewToSelection(false);
            foley.triggerSound(FoleySound::WoodClick, 0.4f);
            return true;
        }
    }

    if (key == 264) { // GLFW_KEY_DOWN
        if (highlightedIndex_ + 1 < static_cast<int>(filteredGames_.size())) {
            ++highlightedIndex_;
            ensureVisible(highlightedIndex_);
            syncPreviewToSelection(false);
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
