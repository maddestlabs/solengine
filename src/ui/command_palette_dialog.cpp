#include "solengine/ui/command_palette_dialog.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/table_renderer.hpp"
#include "eatsbits/ui/glyph/font_engine.hpp"

#include <algorithm>
#include <cmath>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace solengine {

namespace {

constexpr float kRowHeight = 44.0f;
constexpr float kRowPad = 5.0f;
constexpr float kChipHeight = 22.0f;
constexpr float kChipPad = 10.0f;

void getCategoryTint(CommandCategory cat, float& r, float& g, float& b) noexcept {
    switch (cat) {
        case CommandCategory::Play:       r = 0.36f; g = 0.86f; b = 0.56f; break;
        case CommandCategory::Game:       r = 0.94f; g = 0.72f; b = 0.28f; break;
        case CommandCategory::Appearance: r = 0.45f; g = 0.78f; b = 0.96f; break;
        case CommandCategory::Settings:   r = 0.96f; g = 0.52f; b = 0.26f; break;
        case CommandCategory::View:       r = 0.76f; g = 0.62f; b = 0.96f; break;
        case CommandCategory::All:
        default:                          r = 0.86f; g = 0.82f; b = 0.72f; break;
    }
}

std::string truncateText(const std::string& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

} // namespace

void CommandPaletteDialog::open() {
    if (onBeforeOpen) {
        onBeforeOpen();
    }
    isOpen_ = true;
    isMouseDown_ = false;
    isDragging_ = false;
    searchQuery_.clear();
    selectedCategory_ = CommandCategory::All;
    highlightedIndex_ = 0;
    scrollOffsetY_ = 0.0f;
    cursorBlinkTimer_ = 0.0f;
    refreshFilter();
}

void CommandPaletteDialog::close() noexcept {
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

void CommandPaletteDialog::toggle() {
    if (isOpen_) {
        close();
    } else {
        open();
    }
}

void CommandPaletteDialog::setSearchQuery(const std::string& text) {
    searchQuery_ = text;
    scrollOffsetY_ = 0.0f;
    highlightedIndex_ = 0;
    cursorBlinkTimer_ = 0.0f;
    refreshFilter();
}

void CommandPaletteDialog::refreshFilter() {
    if (!registry_) {
        filteredMatches_.clear();
        highlightedIndex_ = 0;
        return;
    }
    filteredMatches_ = registry_->search(searchQuery_, selectedCategory_);
    if (filteredMatches_.empty()) {
        highlightedIndex_ = 0;
    } else {
        highlightedIndex_ = std::clamp(highlightedIndex_, 0, static_cast<int>(filteredMatches_.size()) - 1);
    }
}

void CommandPaletteDialog::ensureVisible(int index) {
    if (index < 0 || index >= static_cast<int>(filteredMatches_.size())) return;
    const float itemTop = static_cast<float>(index) * (kRowHeight + kRowPad);
    const float itemBottom = itemTop + kRowHeight;
    const float visibleH = std::max(10.0f, listAreaH_ - kRowPad * 2.0f);

    if (itemTop < scrollOffsetY_) {
        scrollOffsetY_ = itemTop;
    } else if (itemBottom > scrollOffsetY_ + visibleH) {
        scrollOffsetY_ = itemBottom - visibleH;
    }
    scrollOffsetY_ = std::clamp(scrollOffsetY_, 0.0f, maxScrollY_);
}

bool CommandPaletteDialog::executeHighlighted(bool keepOpen, AudioFoleyEngine& foley) {
    if (!registry_ || filteredMatches_.empty()) return false;
    if (highlightedIndex_ < 0 || highlightedIndex_ >= static_cast<int>(filteredMatches_.size())) {
        return false;
    }

    const CommandMatch match = filteredMatches_[static_cast<size_t>(highlightedIndex_)];
    if (!match.command) return false;
    const std::string cmdId = match.command->id;
    const std::string arg = match.extractedArg;

    if (!keepOpen) {
        close();
    }

    foley.triggerSound(FoleySound::WoodClick, 0.8f);
    registry_->executeById(cmdId, arg);

    if (keepOpen && isOpen_) {
        if (onBeforeOpen) {
            onBeforeOpen();
        }
        refreshFilter();
    }
    return true;
}

void CommandPaletteDialog::cycleCategory(int delta, AudioFoleyEngine& foley) {
    const int count = static_cast<int>(CommandCategory::Count);
    int next = (static_cast<int>(selectedCategory_) + delta) % count;
    if (next < 0) next += count;
    selectedCategory_ = static_cast<CommandCategory>(next);
    scrollOffsetY_ = 0.0f;
    highlightedIndex_ = 0;
    refreshFilter();
    foley.triggerSound(FoleySound::WoodClick, 0.55f);
}

void CommandPaletteDialog::render(
    eatsbits::ui::BatchRenderer2D& r,
    float screenW,
    float screenH,
    float mouseX,
    float mouseY,
    const ThemePalette& pal
) {
    if (!isOpen_) return;

    cursorBlinkTimer_ += 0.016f;

    // 1. Backdrop scrim
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 0.0f, 0.58f);

    // 2. Responsive Spotlight / M-x chassis (slightly above vertical center on desktop)
    const bool isMobile = (screenW < 640.0f) || (screenW < screenH);
    if (isMobile) {
        dialogX_ = 8.0f;
        dialogY_ = 12.0f;
        dialogW_ = std::max(280.0f, screenW - 16.0f);
        dialogH_ = std::min(screenH - 24.0f, 560.0f);
    } else {
        dialogW_ = std::clamp(screenW * 0.54f, 500.0f, 680.0f);
        dialogH_ = std::clamp(screenH * 0.68f, 380.0f, 540.0f);
        dialogX_ = (screenW - dialogW_) * 0.5f;
        dialogY_ = std::max(24.0f, (screenH - dialogH_) * 0.34f);
    }

    eatsbits::ui::LightSource2D light{screenW * 0.5f, -200.0f, 1.0f};
    r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 10.0f, 24.0f, light, 0.72f);
    r.drawRoundedRectGradient(
        dialogX_, dialogY_, dialogW_, dialogH_, 10.0f,
        pal.woodR0 + 0.06f, pal.woodG0 + 0.05f, pal.woodB0 + 0.04f,
        pal.woodR1 + 0.02f, pal.woodG1 + 0.02f, pal.woodB1 + 0.02f,
        0.98f, 6
    );
    r.drawRoundedRectOutline(dialogX_, dialogY_, dialogW_, dialogH_, 10.0f,
                             pal.trimR, pal.trimG, pal.trimB, 0.95f, 1.8f, 6);

    // 3. Header bar
    float curY = dialogY_ + 13.0f;
    eatsbits::ui::glyph::drawVectorText(
        r, "COMMAND PALETTE", dialogX_ + 18.0f, curY, 13.5f,
        pal.accentR, pal.accentG, pal.accentB, 1.0f
    );

    // Match counter to the right of title
    std::string countLabel = std::to_string(filteredMatches_.size()) +
        (filteredMatches_.size() == 1 ? " command" : " commands");
    const float titleW = eatsbits::ui::glyph::getVectorTextWidth(13.5f, "COMMAND PALETTE");
    eatsbits::ui::glyph::drawVectorText(
        r, countLabel.c_str(), dialogX_ + 28.0f + titleW, curY + 2.5f, 9.5f,
        0.72f, 0.70f, 0.65f, 0.90f
    );

    // Close [X] button
    const float closeBtnSize = 24.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 14.0f;
    const float closeBtnY = dialogY_ + 11.0f;
    const bool closeHov = (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize &&
                           mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize);
    clockwork_ui::drawClockworkCloseButton(r, closeBtnX, closeBtnY, closeBtnSize, closeHov);

    // 4. Search input bar
    curY += 26.0f;
    searchBoxX_ = dialogX_ + 16.0f;
    searchBoxY_ = curY;
    searchBoxW_ = dialogW_ - 32.0f;
    searchBoxH_ = 34.0f;

    r.drawRoundedRect(searchBoxX_, searchBoxY_, searchBoxW_, searchBoxH_, 5.0f,
                      0.07f, 0.065f, 0.055f, 0.96f, 4);
    r.drawRoundedRectOutline(searchBoxX_, searchBoxY_, searchBoxW_, searchBoxH_, 5.0f,
                             pal.accentR, pal.accentG, pal.accentB, 0.95f, 1.5f, 4);

    // Prompt glyph '>'
    eatsbits::ui::glyph::drawVectorText(
        r, ">", searchBoxX_ + 11.0f, searchBoxY_ + 10.0f, 13.0f,
        pal.accentR, pal.accentG, pal.accentB, 0.95f
    );

    const float textStartX = searchBoxX_ + 26.0f;
    const bool showCursor = (static_cast<int>(cursorBlinkTimer_ * 2.0f) % 2 == 0);
    if (searchQuery_.empty()) {
        eatsbits::ui::glyph::drawVectorText(
            r, "Type a command, game, theme, deck, or setting (e.g. seed 2026)...",
            textStartX + 4.0f, searchBoxY_ + 11.0f, 10.5f,
            0.55f, 0.52f, 0.48f, 0.85f
        );
        if (showCursor) {
            r.drawRect(textStartX, searchBoxY_ + 8.5f, 1.6f, 17.0f,
                       pal.accentR, pal.accentG, pal.accentB, 0.95f);
        }
    } else {
        const float textW = eatsbits::ui::glyph::getVectorTextWidth(12.0f, searchQuery_.c_str());
        eatsbits::ui::glyph::drawVectorText(
            r, searchQuery_.c_str(), textStartX, searchBoxY_ + 10.0f, 12.0f,
            0.98f, 0.94f, 0.86f, 1.0f
        );
        if (showCursor) {
            r.drawRect(textStartX + textW + 2.0f, searchBoxY_ + 8.5f, 1.6f, 17.0f,
                       pal.accentR, pal.accentG, pal.accentB, 0.95f);
        }

        // Clear [X] button inside search box
        const float clearBtnSize = 20.0f;
        const float clearBtnX = searchBoxX_ + searchBoxW_ - clearBtnSize - 7.0f;
        const float clearBtnY = searchBoxY_ + 7.0f;
        const bool clearHov = (mouseX >= clearBtnX && mouseX <= clearBtnX + clearBtnSize &&
                               mouseY >= clearBtnY && mouseY <= clearBtnY + clearBtnSize);
        if (clearHov) {
            r.drawRoundedRect(clearBtnX, clearBtnY, clearBtnSize, clearBtnSize, 3.0f,
                              0.28f, 0.16f, 0.14f, 0.85f, 3);
        }
        eatsbits::ui::glyph::drawVectorText(
            r, "X", clearBtnX + 6.0f, clearBtnY + 4.0f, 10.5f,
            0.88f, 0.82f, 0.76f, 1.0f
        );
    }

    // 5. Category chips
    curY += searchBoxH_ + 9.0f;
    chipRowY_ = curY;
    float chipX = dialogX_ + 16.0f;
    for (int ci = 0; ci < static_cast<int>(CommandCategory::Count); ++ci) {
        const auto cat = static_cast<CommandCategory>(ci);
        const char* label = commandCategoryName(cat);
        const float tw = eatsbits::ui::glyph::getVectorTextWidth(9.2f, label);
        const float cw = tw + kChipPad * 2.0f;
        if (chipX + cw > dialogX_ + dialogW_ - 16.0f) break;

        const bool isSel = (selectedCategory_ == cat);
        const bool hov = (mouseX >= chipX && mouseX <= chipX + cw &&
                          mouseY >= chipRowY_ && mouseY <= chipRowY_ + kChipHeight);

        float cr = 0.0f, cg = 0.0f, cb = 0.0f;
        getCategoryTint(cat, cr, cg, cb);

        r.drawRoundedRect(
            chipX, chipRowY_, cw, kChipHeight, 11.0f,
            isSel ? (cr * 0.28f + pal.woodR0 * 0.72f) : (pal.woodR0 + (hov ? 0.08f : 0.02f)),
            isSel ? (cg * 0.28f + pal.woodG0 * 0.72f) : (pal.woodG0 + (hov ? 0.06f : 0.02f)),
            isSel ? (cb * 0.28f + pal.woodB0 * 0.72f) : (pal.woodB0 + (hov ? 0.05f : 0.01f)),
            0.94f, 4
        );
        r.drawRoundedRectOutline(
            chipX, chipRowY_, cw, kChipHeight, 11.0f,
            isSel ? cr : pal.trimR,
            isSel ? cg : pal.trimG,
            isSel ? cb : pal.trimB,
            isSel ? 0.98f : 0.42f, isSel ? 1.3f : 1.0f, 4
        );
        eatsbits::ui::glyph::drawVectorText(
            r, label, chipX + kChipPad, chipRowY_ + 6.0f, 9.2f,
            isSel ? cr : 0.86f,
            isSel ? cg : 0.83f,
            isSel ? cb : 0.78f, 1.0f
        );

        chipX += cw + 6.0f;
    }

    // 6. Scrollable command list
    curY += kChipHeight + 9.0f;
    listAreaY_ = curY;
    const float footerH = 30.0f;
    listAreaH_ = std::max(60.0f, (dialogY_ + dialogH_ - footerH) - listAreaY_);

    r.drawRoundedRect(dialogX_ + 14.0f, listAreaY_, dialogW_ - 28.0f, listAreaH_, 6.0f,
                      pal.woodR0 + 0.015f, pal.woodG0 + 0.012f, pal.woodB0 + 0.010f, 0.90f, 4);
    r.drawRoundedRectOutline(dialogX_ + 14.0f, listAreaY_, dialogW_ - 28.0f, listAreaH_, 6.0f,
                             pal.trimR, pal.trimG, pal.trimB, 0.36f, 1.0f, 4);

    const float totalContentH = static_cast<float>(filteredMatches_.size()) * (kRowHeight + kRowPad) + kRowPad;
    maxScrollY_ = std::max(0.0f, totalContentH - listAreaH_);
    scrollOffsetY_ = std::clamp(scrollOffsetY_, 0.0f, maxScrollY_);

    const bool hasScrollbar = (maxScrollY_ > 1.0f);
    const float rowX = dialogX_ + 20.0f;
    const float rowW = dialogW_ - (hasScrollbar ? 48.0f : 40.0f);

    {
        auto scissor = r.makeScissorScope(dialogX_ + 14.0f, listAreaY_ + 2.0f, dialogW_ - 28.0f, listAreaH_ - 4.0f);

        if (filteredMatches_.empty()) {
            eatsbits::ui::glyph::drawVectorText(
                r, "No matching commands. Try clearing the search or switching category with Tab.",
                rowX + 8.0f, listAreaY_ + 18.0f, 10.5f,
                0.70f, 0.67f, 0.62f, 0.90f
            );
        } else {
            for (size_t i = 0; i < filteredMatches_.size(); ++i) {
                const auto& m = filteredMatches_[i];
                if (!m.command) continue;
                const auto& cmd = *m.command;

                const float ry = listAreaY_ + kRowPad + static_cast<float>(i) * (kRowHeight + kRowPad) - scrollOffsetY_;
                if (ry + kRowHeight < listAreaY_ || ry > listAreaY_ + listAreaH_) continue;

                const bool isHighlighted = (static_cast<int>(i) == highlightedIndex_);
                const bool isHov = (!isDragging_ &&
                                    mouseX >= rowX && mouseX <= rowX + rowW &&
                                    mouseY >= ry && mouseY <= ry + kRowHeight &&
                                    mouseY >= listAreaY_ && mouseY <= listAreaY_ + listAreaH_);

                float bgR = pal.woodR0 + 0.045f;
                float bgG = pal.woodG0 + 0.038f;
                float bgB = pal.woodB0 + 0.030f;
                if (isHighlighted) {
                    bgR += 0.11f; bgG += 0.085f; bgB += 0.055f;
                } else if (isHov) {
                    bgR += 0.06f; bgG += 0.05f; bgB += 0.035f;
                }

                r.drawRoundedRect(rowX, ry, rowW, kRowHeight, 4.0f, bgR, bgG, bgB, 0.96f, 4);
                r.drawRoundedRectOutline(
                    rowX, ry, rowW, kRowHeight, 4.0f,
                    isHighlighted ? pal.accentR : pal.trimR,
                    isHighlighted ? pal.accentG : pal.trimG,
                    isHighlighted ? pal.accentB : pal.trimB,
                    isHighlighted ? 0.95f : (isHov ? 0.65f : 0.30f),
                    isHighlighted ? 1.4f : 1.0f, 4
                );

                // Category color bar on left edge
                float cr = 0.0f, cg = 0.0f, cb = 0.0f;
                getCategoryTint(cmd.category, cr, cg, cb);
                r.drawRoundedRect(
                    rowX + 2.0f, ry + 4.0f, isHighlighted ? 4.0f : 3.0f, kRowHeight - 8.0f, 2.0f,
                    isHighlighted ? pal.accentR : cr,
                    isHighlighted ? pal.accentG : cg,
                    isHighlighted ? pal.accentB : cb,
                    isHighlighted ? 1.0f : 0.72f
                );

                // Right-aligned pills: [Shortcut] and optional [Current Value]
                float rightEdge = rowX + rowW - 10.0f;

                if (!cmd.shortcut.empty()) {
                    const float scW = eatsbits::ui::glyph::getVectorTextWidth(9.0f, cmd.shortcut.c_str()) + 12.0f;
                    const float scH = 20.0f;
                    const float scX = rightEdge - scW;
                    const float scY = ry + (kRowHeight - scH) * 0.5f;
                    r.drawRoundedRect(scX, scY, scW, scH, 4.0f, 0.09f, 0.085f, 0.075f, 0.95f, 3);
                    r.drawRoundedRectOutline(scX, scY, scW, scH, 4.0f,
                                             pal.trimR, pal.trimG, pal.trimB, 0.55f, 1.0f, 3);
                    eatsbits::ui::glyph::drawVectorText(
                        r, cmd.shortcut.c_str(), scX + 6.0f, scY + 5.5f, 9.0f,
                        0.92f, 0.88f, 0.78f, 0.96f
                    );
                    rightEdge = scX - 7.0f;
                }

                if (cmd.valueProvider) {
                    const std::string val = cmd.valueProvider();
                    if (!val.empty()) {
                        const std::string valDisp = truncateText(val, 22);
                        const float vW = eatsbits::ui::glyph::getVectorTextWidth(9.0f, valDisp.c_str()) + 12.0f;
                        const float vH = 20.0f;
                        const float vX = rightEdge - vW;
                        const float vY = ry + (kRowHeight - vH) * 0.5f;
                        r.drawRoundedRect(vX, vY, vW, vH, 10.0f,
                                          cr * 0.22f + 0.08f, cg * 0.22f + 0.08f, cb * 0.22f + 0.08f, 0.95f, 3);
                        r.drawRoundedRectOutline(vX, vY, vW, vH, 10.0f, cr, cg, cb, 0.75f, 1.0f, 3);
                        eatsbits::ui::glyph::drawVectorText(
                            r, valDisp.c_str(), vX + 6.0f, vY + 5.5f, 9.0f,
                            std::min(1.0f, cr + 0.18f), std::min(1.0f, cg + 0.18f), std::min(1.0f, cb + 0.18f), 1.0f
                        );
                        rightEdge = vX - 8.0f;
                    }
                }

                // Title (plus inline argument preview if matched with argument)
                std::string titleText = cmd.title;
                if (!m.extractedArg.empty()) {
                    titleText += "  [" + truncateText(m.extractedArg, 18) + "]";
                }
                const float availTitleW = std::max(80.0f, rightEdge - (rowX + 14.0f));
                const size_t approxMaxTitleChars = static_cast<size_t>(std::max(12.0f, availTitleW / 6.5f));
                titleText = truncateText(titleText, approxMaxTitleChars);

                eatsbits::ui::glyph::drawVectorText(
                    r, titleText.c_str(), rowX + 14.0f, ry + 7.5f, 11.5f,
                    isHighlighted ? pal.accentR : 0.96f,
                    isHighlighted ? pal.accentG : 0.93f,
                    isHighlighted ? pal.accentB : 0.87f, 1.0f
                );

                // Subtitle
                if (!cmd.subtitle.empty()) {
                    const size_t approxMaxSubChars = static_cast<size_t>(std::max(16.0f, availTitleW / 5.2f));
                    const std::string sub = truncateText(cmd.subtitle, approxMaxSubChars);
                    eatsbits::ui::glyph::drawVectorText(
                        r, sub.c_str(), rowX + 14.0f, ry + 24.5f, 9.0f,
                        0.74f, 0.71f, 0.66f, 0.90f
                    );
                }
            }
        }
    }

    // Subtle scrollbar thumb when overflowing
    if (hasScrollbar) {
        const float trackX = dialogX_ + dialogW_ - 22.0f;
        const float trackY = listAreaY_ + 6.0f;
        const float trackH = std::max(20.0f, listAreaH_ - 12.0f);
        const float thumbH = std::max(22.0f, trackH * (listAreaH_ / totalContentH));
        const float ratio = scrollOffsetY_ / std::max(1.0f, maxScrollY_);
        const float thumbY = trackY + ratio * (trackH - thumbH);
        r.drawRoundedRect(trackX, trackY, 4.0f, trackH, 2.0f, 0.12f, 0.11f, 0.10f, 0.60f, 2);
        r.drawRoundedRect(trackX, thumbY, 4.0f, thumbH, 2.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 2);
    }

    // 7. Footer keyboard legend
    const float footerY = dialogY_ + dialogH_ - 21.0f;
    eatsbits::ui::glyph::drawVectorText(
        r, "Up/Down Navigate   |   Enter Run   |   Shift+Enter Keep Open   |   Tab Category   |   Esc Close",
        dialogX_ + 18.0f, footerY, 8.8f,
        0.68f, 0.65f, 0.60f, 0.90f
    );
}

bool CommandPaletteDialog::handleMouseButton(
    int button,
    int action,
    int mods,
    float mouseX,
    float mouseY,
    AudioFoleyEngine& foley
) {
    if (!isOpen_) return false;
    if (button != 0) return true;

    if (action == 1) { // Press
        // Click outside dialog closes it
        if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ ||
            mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.6f);
            return true;
        }

        // Close [X] button
        const float closeBtnSize = 24.0f;
        const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 14.0f;
        const float closeBtnY = dialogY_ + 11.0f;
        if (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize &&
            mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize) {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
            return true;
        }

        // Search box & clear [X]
        if (mouseX >= searchBoxX_ && mouseX <= searchBoxX_ + searchBoxW_ &&
            mouseY >= searchBoxY_ && mouseY <= searchBoxY_ + searchBoxH_) {
            if (!searchQuery_.empty()) {
                const float clearBtnSize = 20.0f;
                const float clearBtnX = searchBoxX_ + searchBoxW_ - clearBtnSize - 7.0f;
                const float clearBtnY = searchBoxY_ + 7.0f;
                if (mouseX >= clearBtnX && mouseX <= clearBtnX + clearBtnSize &&
                    mouseY >= clearBtnY && mouseY <= clearBtnY + clearBtnSize) {
                    setSearchQuery("");
                    foley.triggerSound(FoleySound::WoodClick, 0.55f);
                    return true;
                }
            }
#if defined(__EMSCRIPTEN__)
            EM_ASM({
                if (window.openMobileKeyboard) {
                    window.openMobileKeyboard(UTF8ToString($0));
                }
            }, searchQuery_.c_str());
#endif
            return true;
        }

        // Category chips
        if (mouseY >= chipRowY_ && mouseY <= chipRowY_ + kChipHeight) {
            float chipX = dialogX_ + 16.0f;
            for (int ci = 0; ci < static_cast<int>(CommandCategory::Count); ++ci) {
                const auto cat = static_cast<CommandCategory>(ci);
                const float cw = eatsbits::ui::glyph::getVectorTextWidth(9.2f, commandCategoryName(cat)) + kChipPad * 2.0f;
                if (chipX + cw > dialogX_ + dialogW_ - 16.0f) break;
                if (mouseX >= chipX && mouseX <= chipX + cw) {
                    selectedCategory_ = cat;
                    scrollOffsetY_ = 0.0f;
                    highlightedIndex_ = 0;
                    refreshFilter();
                    foley.triggerSound(FoleySound::WoodClick, 0.65f);
                    return true;
                }
                chipX += cw + 6.0f;
            }
        }

        // List area drag/click start
        if (mouseY >= listAreaY_ && mouseY <= listAreaY_ + listAreaH_) {
            isMouseDown_ = true;
            isDragging_ = false;
            dragStartY_ = mouseY;
            initialScrollY_ = scrollOffsetY_;
            return true;
        }
    } else if (action == 0) { // Release
        if (isMouseDown_) {
            isMouseDown_ = false;
            if (isDragging_) {
                isDragging_ = false;
                return true;
            }

            if (mouseY >= listAreaY_ && mouseY <= listAreaY_ + listAreaH_) {
                const bool hasScrollbar = (maxScrollY_ > 1.0f);
                const float rowX = dialogX_ + 20.0f;
                const float rowW = dialogW_ - (hasScrollbar ? 48.0f : 40.0f);
                for (size_t i = 0; i < filteredMatches_.size(); ++i) {
                    const float ry = listAreaY_ + kRowPad + static_cast<float>(i) * (kRowHeight + kRowPad) - scrollOffsetY_;
                    if (mouseX >= rowX && mouseX <= rowX + rowW &&
                        mouseY >= ry && mouseY <= ry + kRowHeight) {
                        highlightedIndex_ = static_cast<int>(i);
                        const bool keepOpen = (mods & 1) != 0; // Shift+Click keeps open
                        executeHighlighted(keepOpen, foley);
                        return true;
                    }
                }
            }
        }
    }

    return true;
}

void CommandPaletteDialog::handleMouseMove(float mouseX, float mouseY) {
    if (!isOpen_) return;
    (void)mouseX;
    if (isMouseDown_) {
        const float dy = mouseY - dragStartY_;
        if (!isDragging_ && std::abs(dy) > 4.0f) {
            isDragging_ = true;
        }
        if (isDragging_) {
            scrollOffsetY_ = std::clamp(initialScrollY_ - dy, 0.0f, maxScrollY_);
        }
    }
}

bool CommandPaletteDialog::handleKey(
    int key,
    int action,
    int mods,
    AudioFoleyEngine& foley
) {
    if (!isOpen_) return false;
    if (action != 1 && action != 2) return true;

    const bool isShift = (mods & 1) != 0;
    const bool isCtrl = (mods & 2) != 0;

    if (key == 256) { // GLFW_KEY_ESCAPE
        if (!searchQuery_.empty()) {
            setSearchQuery("");
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
        } else {
            close();
            foley.triggerSound(FoleySound::WoodClick, 0.65f);
        }
        return true;
    }

    // Ctrl+K or Ctrl+P toggles closed
    if (isCtrl && (key == 75 || key == 80)) { // K or P
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.6f);
        return true;
    }

    if (key == 257 || key == 335) { // GLFW_KEY_ENTER / GLFW_KEY_KP_ENTER
        executeHighlighted(isShift, foley);
        return true;
    }

    if (key == 259) { // GLFW_KEY_BACKSPACE
        if (!searchQuery_.empty()) {
            if (isCtrl) {
                searchQuery_.clear();
            } else {
                searchQuery_.pop_back();
            }
            scrollOffsetY_ = 0.0f;
            highlightedIndex_ = 0;
            cursorBlinkTimer_ = 0.0f;
            refreshFilter();
            foley.triggerSound(FoleySound::WoodClick, 0.45f);
        }
        return true;
    }

    if (key == 258) { // GLFW_KEY_TAB
        cycleCategory(isShift ? -1 : 1, foley);
        return true;
    }

    if (key == 265) { // GLFW_KEY_UP
        if (!filteredMatches_.empty()) {
            if (highlightedIndex_ > 0) {
                --highlightedIndex_;
            } else {
                highlightedIndex_ = static_cast<int>(filteredMatches_.size()) - 1;
            }
            ensureVisible(highlightedIndex_);
            foley.triggerSound(FoleySound::WoodClick, 0.35f);
        }
        return true;
    }

    if (key == 264) { // GLFW_KEY_DOWN
        if (!filteredMatches_.empty()) {
            if (highlightedIndex_ + 1 < static_cast<int>(filteredMatches_.size())) {
                ++highlightedIndex_;
            } else {
                highlightedIndex_ = 0;
            }
            ensureVisible(highlightedIndex_);
            foley.triggerSound(FoleySound::WoodClick, 0.35f);
        }
        return true;
    }

    if (key == 266) { // GLFW_KEY_PAGE_UP
        if (!filteredMatches_.empty()) {
            highlightedIndex_ = std::max(0, highlightedIndex_ - 6);
            ensureVisible(highlightedIndex_);
            foley.triggerSound(FoleySound::WoodClick, 0.35f);
        }
        return true;
    }

    if (key == 267) { // GLFW_KEY_PAGE_DOWN
        if (!filteredMatches_.empty()) {
            highlightedIndex_ = std::min(static_cast<int>(filteredMatches_.size()) - 1, highlightedIndex_ + 6);
            ensureVisible(highlightedIndex_);
            foley.triggerSound(FoleySound::WoodClick, 0.35f);
        }
        return true;
    }

    return true;
}

bool CommandPaletteDialog::handleChar(unsigned int codepoint) {
    if (!isOpen_) return false;
    if (codepoint >= 32 && codepoint <= 126) {
        searchQuery_ += static_cast<char>(codepoint);
        cursorBlinkTimer_ = 0.0f;
        scrollOffsetY_ = 0.0f;
        highlightedIndex_ = 0;
        refreshFilter();
        return true;
    }
    return false;
}

bool CommandPaletteDialog::handleScroll(float yoffset) {
    if (!isOpen_) return false;
    scrollOffsetY_ = std::clamp(scrollOffsetY_ - yoffset * 34.0f, 0.0f, maxScrollY_);
    return true;
}

} // namespace solengine
