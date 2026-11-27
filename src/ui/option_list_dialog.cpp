#include "solengine/ui/option_list_dialog.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/table_renderer.hpp"
#include "eatsbits/ui/glyph/font_engine.hpp"
#include <algorithm>
#include <cmath>

namespace solengine {

void OptionListDialog::open(
    std::string title,
    std::vector<OptionListItem> items,
    int selectedIndex
) {
    title_ = std::move(title);
    items_ = std::move(items);
    const int count = static_cast<int>(items_.size());
    if (count == 0) {
        selectedIndex_ = -1;
        focusedIndex_ = -1;
    } else {
        selectedIndex_ = (selectedIndex >= 0 && selectedIndex < count) ? selectedIndex : 0;
        focusedIndex_ = selectedIndex_;
    }
    pendingSelection_ = -1;
    hasPendingSelection_ = false;
    scroller_.reset(0.0f);
    scrollToSelectedPending_ = true;
    draggingScrollbar_ = false;
    pressedRow_ = -1;
    isOpen_ = true;
}

void OptionListDialog::close() noexcept {
    isOpen_ = false;
    scroller_.cancelDrag();
    draggingScrollbar_ = false;
    pressedRow_ = -1;
}

void OptionListDialog::update(float dt) noexcept {
    if (!isOpen_) return;
    scroller_.update(dt);
}

bool OptionListDialog::takeSelection(int& outIndex) noexcept {
    if (!hasPendingSelection_) return false;
    outIndex = pendingSelection_;
    hasPendingSelection_ = false;
    pendingSelection_ = -1;
    return true;
}

void OptionListDialog::scrollIndexIntoView(int index, bool center, bool animate) noexcept {
    if (index < 0 || index >= static_cast<int>(items_.size()) || listH_ <= 1.0f) return;
    const float rowTop = kListPadTop + static_cast<float>(index) * (kRowH + kRowGap);
    const float rowBottom = rowTop + kRowH;
    if (center) {
        const float target = rowTop + kRowH * 0.5f - listH_ * 0.5f;
        scroller_.scrollTo(target, animate);
    } else {
        scroller_.ensureRangeVisible(rowTop - kListPadTop, rowBottom + kListPadBottom, listH_, animate);
    }
}

int OptionListDialog::hitTestRow(float mouseX, float mouseY) const noexcept {
    if (mouseX < listX_ || mouseX > listX_ + listW_ ||
        mouseY < listY_ || mouseY > listY_ + listH_) {
        return -1;
    }
    // Exclude scrollbar hit area when scrollbar is active
    if (scroller_.maxOffset() > 1.0f && mouseX >= listX_ + listW_ - 16.0f) {
        return -1;
    }
    const float localY = (mouseY - listY_) + scroller_.offset() - kListPadTop;
    if (localY < 0.0f) return -1;
    const float stride = kRowH + kRowGap;
    const int idx = static_cast<int>(localY / stride);
    const float offsetInRow = localY - static_cast<float>(idx) * stride;
    if (idx < 0 || idx >= static_cast<int>(items_.size()) || offsetInRow > kRowH) {
        return -1;
    }
    return idx;
}

void OptionListDialog::render(
    eatsbits::ui::BatchRenderer2D& r,
    float screenW,
    float screenH,
    float mouseX,
    float mouseY,
    const ThemePalette& pal,
    bool isClockwork,
    float timeSec
) {
    if (!isOpen_) return;

    const auto& cpal = clockwork_ui::palette();

    // 1. Darkened backdrop over underlying settings dialog
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 0.0f, 0.52f);

    // 2. Compute compact responsive dialog dimensions
    const float maxDialogW = std::max(240.0f, screenW - 24.0f);
    dialogW_ = std::min(maxDialogW, std::clamp(screenW * 0.46f, 300.0f, 390.0f));

    const int count = static_cast<int>(items_.size());
    const float contentTotalH = (count > 0)
        ? (kListPadTop + kListPadBottom + static_cast<float>(count) * kRowH + static_cast<float>(std::max(0, count - 1)) * kRowGap)
        : 60.0f;

    const float maxListH = std::max(110.0f, std::min(screenH * 0.62f, 340.0f));
    listH_ = std::min(contentTotalH, maxListH);
    dialogH_ = kHeaderH + 8.0f + listH_ + kFooterPad;
    dialogX_ = (screenW - dialogW_) * 0.5f;
    dialogY_ = std::max(12.0f, (screenH - dialogH_) * 0.5f);

    listX_ = dialogX_ + kOuterPadX;
    listY_ = dialogY_ + kHeaderH + 8.0f;
    listW_ = dialogW_ - 2.0f * kOuterPadX;

    scroller_.setBounds(contentTotalH, listH_);
    if (scrollToSelectedPending_) {
        scrollToSelectedPending_ = false;
        scrollIndexIntoView(selectedIndex_, /*center=*/true, /*animate=*/false);
    }

    // 3. Dialog Chassis / Frame
    if (isClockwork) {
        eatsbits::ui::LightSource2D light{dialogX_ + dialogW_ * 0.5f, dialogY_ - 180.0f, 450.0f};
        r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 10.0f, 20.0f, light, 0.72f);
        clockwork_ui::drawChassisFrame(r, dialogX_, dialogY_, dialogW_, dialogH_, 8.0f, true, false, timeSec);
    } else {
        eatsbits::ui::LightSource2D light{dialogX_ + dialogW_ * 0.5f, dialogY_ - 180.0f, 450.0f};
        r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 10.0f, 20.0f, light, 0.68f);
        r.drawRoundedRectGradient(
            dialogX_, dialogY_, dialogW_, dialogH_, 8.0f,
            pal.woodR0 + 0.08f, pal.woodG0 + 0.06f, pal.woodB0 + 0.05f,
            pal.woodR1 + 0.03f, pal.woodG1 + 0.02f, pal.woodB1 + 0.02f,
            0.99f, 6
        );
        r.drawRoundedRectOutline(dialogX_, dialogY_, dialogW_, dialogH_, 8.0f, pal.trimR, pal.trimG, pal.trimB, 0.95f, 1.8f, 6);
    }

    // 4. Header Bar + Close [X]
    if (isClockwork) {
        r.drawRoundedRect(dialogX_ + 3.0f, dialogY_ + 3.0f, dialogW_ - 6.0f, kHeaderH - 3.0f, 6.0f,
                          cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB, 0.94f, 4);
        r.drawLine(dialogX_ + 4.0f, dialogY_ + kHeaderH, dialogX_ + dialogW_ - 4.0f, dialogY_ + kHeaderH,
                   cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.75f, 1.2f);
    } else {
        r.drawRoundedRect(dialogX_ + 2.0f, dialogY_ + 2.0f, dialogW_ - 4.0f, kHeaderH - 2.0f, 6.0f,
                          pal.woodR0 + 0.02f, pal.woodG0 + 0.01f, pal.woodB0 + 0.01f, 0.96f, 4);
        r.drawLine(dialogX_ + 4.0f, dialogY_ + kHeaderH, dialogX_ + dialogW_ - 4.0f, dialogY_ + kHeaderH,
                   pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.2f);
    }

    const float closeBtnSize = 24.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 10.0f;
    const float closeBtnY = dialogY_ + (kHeaderH - closeBtnSize) * 0.5f;
    const bool closeHov = (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize &&
                           mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize);
    clockwork_ui::drawClockworkCloseButton(r, closeBtnX, closeBtnY, closeBtnSize, closeHov, timeSec);

    // Title (truncated to fit cleanly before close button)
    auto& fe = eatsbits::ui::glyph::FontEngine::instance();
    const float maxTitleW = (closeBtnX - 10.0f) - (dialogX_ + 14.0f);
    std::string dispTitle = title_;
    if (maxTitleW > 30.0f && fe.measureText(dispTitle, 12.0f) > maxTitleW) {
        while (dispTitle.size() > 3 && fe.measureText(dispTitle + "...", 12.0f) > maxTitleW) {
            dispTitle.pop_back();
        }
        dispTitle += "...";
    }
    eatsbits::ui::glyph::drawVectorText(
        r, dispTitle.c_str(), dialogX_ + 14.0f, dialogY_ + 14.0f, 12.0f,
        isClockwork ? cpal.furnaceCoreR : pal.accentR,
        isClockwork ? cpal.furnaceCoreG : pal.accentG,
        isClockwork ? cpal.furnaceCoreB : pal.accentB,
        1.0f
    );

    // 5. Recessed List Panel Bed
    if (isClockwork) {
        r.drawRoundedRect(listX_, listY_, listW_, listH_, 5.0f, cpal.panelBedR, cpal.panelBedG, cpal.panelBedB, 0.96f, 4);
        r.drawRoundedRectOutline(listX_, listY_, listW_, listH_, 5.0f, cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 0.80f, 1.1f, 4);
    } else {
        r.drawRoundedRect(listX_, listY_, listW_, listH_, 5.0f, pal.woodR0 + 0.01f, pal.woodG0 + 0.01f, pal.woodB0 + 0.01f, 0.92f, 4);
        r.drawRoundedRectOutline(listX_, listY_, listW_, listH_, 5.0f, pal.trimR, pal.trimG, pal.trimB, 0.50f, 1.0f, 4);
    }

    // 6. Scrollable Items List
    {
        auto scissor = r.makeScissorScope(listX_ + 2.0f, listY_ + 2.0f, listW_ - 4.0f, listH_ - 4.0f);
        const bool hasScrollbar = (scroller_.maxOffset() > 1.0f);
        const float itemX = listX_ + 8.0f;
        const float itemW = listW_ - 16.0f - (hasScrollbar ? 12.0f : 0.0f);
        const int hoveredRow = (draggingScrollbar_ || scroller_.isDragging()) ? -1 : hitTestRow(mouseX, mouseY);
        const float scrollY = scroller_.offset();

        if (items_.empty()) {
            eatsbits::ui::glyph::drawVectorText(
                r, "No options available", itemX + 6.0f, listY_ + 22.0f, 11.0f,
                0.75f, 0.72f, 0.66f, 0.85f
            );
        } else {
            for (int i = 0; i < count; ++i) {
                const float rowY = listY_ + kListPadTop + static_cast<float>(i) * (kRowH + kRowGap) - scrollY;
                if (rowY + kRowH < listY_ - 2.0f || rowY > listY_ + listH_ + 2.0f) {
                    continue;
                }

                const bool isSelected = (i == selectedIndex_);
                const bool isFocused = (i == focusedIndex_);
                const bool isHovered = (i == hoveredRow);

                if (isClockwork) {
                    const float bgBoost = isSelected ? 0.09f : ((isHovered || isFocused) ? 0.05f : 0.0f);
                    r.drawRoundedRectGradient(
                        itemX, rowY, itemW, kRowH, 4.0f,
                        cpal.chassisLightR + bgBoost,
                        cpal.chassisLightG + bgBoost * 0.85f,
                        cpal.chassisLightB + bgBoost * 0.60f,
                        cpal.chassisDarkR + bgBoost * 0.45f,
                        cpal.chassisDarkG + bgBoost * 0.40f,
                        cpal.chassisDarkB + bgBoost * 0.30f,
                        0.96f, 4
                    );
                    r.drawRoundedRectOutline(
                        itemX, rowY, itemW, kRowH, 4.0f,
                        isSelected ? cpal.furnaceAmberR : ((isHovered || isFocused) ? cpal.brassBrightR : cpal.brassDarkR),
                        isSelected ? cpal.furnaceAmberG : ((isHovered || isFocused) ? cpal.brassBrightG : cpal.brassDarkG),
                        isSelected ? cpal.furnaceAmberB : ((isHovered || isFocused) ? cpal.brassBrightB : cpal.brassDarkB),
                        isSelected ? 1.0f : ((isHovered || isFocused) ? 0.88f : 0.50f),
                        isSelected ? 1.5f : 1.0f, 4
                    );
                } else {
                    const float bgBoost = isSelected ? 0.12f : ((isHovered || isFocused) ? 0.06f : 0.0f);
                    r.drawRoundedRect(
                        itemX, rowY, itemW, kRowH, 4.0f,
                        pal.woodR0 + 0.04f + bgBoost,
                        pal.woodG0 + 0.03f + bgBoost * 0.85f,
                        pal.woodB0 + 0.02f + bgBoost * 0.70f,
                        0.95f, 4
                    );
                    r.drawRoundedRectOutline(
                        itemX, rowY, itemW, kRowH, 4.0f,
                        isSelected ? pal.accentR : ((isHovered || isFocused) ? pal.accentR : pal.trimR),
                        isSelected ? pal.accentG : ((isHovered || isFocused) ? pal.accentG : pal.trimG),
                        isSelected ? pal.accentB : ((isHovered || isFocused) ? pal.accentB : pal.trimB),
                        isSelected ? 1.0f : ((isHovered || isFocused) ? 0.80f : 0.45f),
                        isSelected ? 1.5f : 1.0f, 4
                    );
                }

                // Active selection indicator dot on left
                float textX = itemX + 10.0f;
                if (isSelected) {
                    r.drawCircle(
                        itemX + 11.0f, rowY + kRowH * 0.5f, 3.4f,
                        isClockwork ? cpal.furnaceCoreR : pal.accentR,
                        isClockwork ? cpal.furnaceCoreG : pal.accentG,
                        isClockwork ? cpal.furnaceCoreB : pal.accentB,
                        1.0f, 10
                    );
                    textX = itemX + 21.0f;
                }

                // Optional right-aligned badge
                float rightEdgeX = itemX + itemW - 10.0f;
                const auto& item = items_[static_cast<size_t>(i)];
                if (!item.badge.empty()) {
                    const float badgeW = fe.measureText(item.badge, 9.0f);
                    const float badgeX = rightEdgeX - badgeW;
                    eatsbits::ui::glyph::drawVectorText(
                        r, item.badge.c_str(), badgeX, rowY + (kRowH - 9.0f) * 0.5f, 9.0f,
                        isClockwork ? cpal.brassBrightR : pal.trimR,
                        isClockwork ? cpal.brassBrightG : pal.trimG,
                        isClockwork ? cpal.brassBrightB : pal.trimB,
                        0.75f
                    );
                    rightEdgeX = badgeX - 8.0f;
                }

                // Item label (truncated cleanly if needed)
                std::string dispLabel = item.label;
                const float maxLblW = std::max(24.0f, rightEdgeX - textX);
                if (fe.measureText(dispLabel, 11.0f) > maxLblW) {
                    while (dispLabel.size() > 3 && fe.measureText(dispLabel + "...", 11.0f) > maxLblW) {
                        dispLabel.pop_back();
                    }
                    dispLabel += "...";
                }

                const float txtR = isSelected
                    ? (isClockwork ? cpal.furnaceCoreR : pal.accentR)
                    : ((isHovered || isFocused) ? (isClockwork ? cpal.brassHighlightR : 1.0f) : 0.93f);
                const float txtG = isSelected
                    ? (isClockwork ? cpal.furnaceCoreG : pal.accentG)
                    : ((isHovered || isFocused) ? (isClockwork ? cpal.brassHighlightG : 0.97f) : 0.90f);
                const float txtB = isSelected
                    ? (isClockwork ? cpal.furnaceCoreB : pal.accentB)
                    : ((isHovered || isFocused) ? (isClockwork ? cpal.brassHighlightB : 0.90f) : 0.84f);

                eatsbits::ui::glyph::drawVectorText(
                    r, dispLabel.c_str(), textX, rowY + (kRowH - 11.0f) * 0.5f, 11.0f,
                    txtR, txtG, txtB, 1.0f
                );
            }
        }
    }

    // 7. Scrollbar indicator when list overflows (auto-brightens & compresses on iOS overscroll)
    if (scroller_.maxOffset() > 1.0f) {
        const float sbTrackY = listY_ + 6.0f;
        const float sbTrackH = std::max(24.0f, listH_ - 12.0f);
        float thumbY = sbTrackY;
        float thumbH = 22.0f;
        scroller_.computeThumb(sbTrackY, sbTrackH, thumbY, thumbH);

        sbTrackY_ = sbTrackY;
        sbTrackH_ = sbTrackH;
        sbThumbH_ = thumbH;

        const bool sbHov = (mouseX >= listX_ + listW_ - 18.0f && mouseX <= listX_ + listW_ &&
                            mouseY >= sbTrackY && mouseY <= sbTrackY + sbTrackH) || draggingScrollbar_;
        const float activeAlpha = sbHov ? 1.0f : std::max(0.32f, scroller_.indicatorAlpha());
        const float sbW = sbHov ? 7.0f : 5.0f;
        const float sbX = listX_ + listW_ - sbW - 4.0f;

        r.drawRoundedRect(sbX, sbTrackY, sbW, sbTrackH, sbW * 0.5f, 0.08f, 0.07f, 0.06f, (sbHov ? 0.65f : 0.45f) * activeAlpha, 2);
        r.drawRoundedRect(
            sbX, thumbY, sbW, thumbH, sbW * 0.5f,
            isClockwork ? cpal.brassBrightR : pal.accentR,
            isClockwork ? cpal.brassBrightG : pal.accentG,
            isClockwork ? cpal.brassBrightB : pal.accentB,
            (sbHov ? 0.95f : 0.85f) * activeAlpha, 2
        );
    }
}

bool OptionListDialog::handleMouseButton(
    int button,
    int action,
    float mouseX,
    float mouseY,
    AudioFoleyEngine& foley
) {
    if (!isOpen_) return false;
    if (button != 0) return true;

    if (action == 0) {
        const bool wasDownInList = scroller_.isPointerDown();
        const bool suppressClick = scroller_.endDrag();
        const int prevPressedRow = pressedRow_;

        draggingScrollbar_ = false;
        pressedRow_ = -1;

        if (wasDownInList && !suppressClick && prevPressedRow >= 0) {
            const int releasedRow = hitTestRow(mouseX, mouseY);
            if (releasedRow == prevPressedRow && releasedRow >= 0 && releasedRow < static_cast<int>(items_.size())) {
                selectedIndex_ = releasedRow;
                focusedIndex_ = releasedRow;
                pendingSelection_ = releasedRow;
                hasPendingSelection_ = true;
                close();
                return true;
            }
        }
        return true;
    }

    if (action != 1) return true;

    // Click outside dialog -> close without changing selection
    if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ ||
        mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.65f);
        return true;
    }

    // Close [X] button
    const float closeBtnSize = 24.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 10.0f;
    const float closeBtnY = dialogY_ + (kHeaderH - closeBtnSize) * 0.5f;
    if (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize &&
        mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize) {
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    // Scrollbar drag grab
    if (scroller_.maxOffset() > 1.0f &&
        mouseX >= listX_ + listW_ - 18.0f && mouseX <= listX_ + listW_ &&
        mouseY >= sbTrackY_ && mouseY <= sbTrackY_ + sbTrackH_) {
        draggingScrollbar_ = true;
        const float denom = std::max(1.0f, sbTrackH_ - sbThumbH_);
        const float frac = std::clamp((mouseY - sbTrackY_ - sbThumbH_ * 0.5f) / denom, 0.0f, 1.0f);
        scroller_.setOffsetImmediate(frac * scroller_.maxOffset());
        return true;
    }

    // Inside list viewport: arm tap/click or kinetic touch drag-scroll
    if (mouseX >= listX_ && mouseX <= listX_ + listW_ &&
        mouseY >= listY_ && mouseY <= listY_ + listH_) {
        const bool caught = scroller_.beginDrag(mouseX, mouseY);
        pressedRow_ = caught ? -1 : hitTestRow(mouseX, mouseY);
        if (pressedRow_ >= 0) {
            focusedIndex_ = pressedRow_;
        }
        return true;
    }

    return true;
}

bool OptionListDialog::handleMouseMove(float mouseX, float mouseY) {
    if (!isOpen_) return false;

    if (draggingScrollbar_ && scroller_.maxOffset() > 1.0f) {
        const float denom = std::max(1.0f, sbTrackH_ - sbThumbH_);
        const float frac = std::clamp((mouseY - sbTrackY_ - sbThumbH_ * 0.5f) / denom, 0.0f, 1.0f);
        scroller_.setOffsetImmediate(frac * scroller_.maxOffset());
        return true;
    }

    if (scroller_.isPointerDown()) {
        if (scroller_.moveDrag(mouseX, mouseY)) {
            pressedRow_ = -1;
        }
        return true;
    }

    const int hovRow = hitTestRow(mouseX, mouseY);
    if (hovRow >= 0) {
        focusedIndex_ = hovRow;
    }
    return true;
}

bool OptionListDialog::handleScroll(float yoffset) {
    if (!isOpen_) return false;
    scroller_.handleWheel(yoffset, 42.0f);
    return true;
}

bool OptionListDialog::handleKey(int key, int action, AudioFoleyEngine& foley) {
    if (!isOpen_) return false;
    if (action != 1 && action != 2) return true;

    const int count = static_cast<int>(items_.size());

    if (key == 256) { // GLFW_KEY_ESCAPE
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.65f);
        return true;
    }

    if (count > 0) {
        if (key == 265) { // GLFW_KEY_UP
            focusedIndex_ = (focusedIndex_ <= 0) ? (count - 1) : (focusedIndex_ - 1);
            scrollIndexIntoView(focusedIndex_, /*center=*/false);
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
            return true;
        }
        if (key == 264) { // GLFW_KEY_DOWN
            focusedIndex_ = (focusedIndex_ + 1) % count;
            scrollIndexIntoView(focusedIndex_, /*center=*/false);
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
            return true;
        }
        if (key == 268) { // GLFW_KEY_HOME
            focusedIndex_ = 0;
            scrollIndexIntoView(focusedIndex_, /*center=*/false);
            return true;
        }
        if (key == 269) { // GLFW_KEY_END
            focusedIndex_ = count - 1;
            scrollIndexIntoView(focusedIndex_, /*center=*/false);
            return true;
        }
        if (key == 257 || key == 335 || key == 32) { // ENTER / KP_ENTER / SPACE
            const int chosen = (focusedIndex_ >= 0 && focusedIndex_ < count) ? focusedIndex_ : selectedIndex_;
            if (chosen >= 0 && chosen < count) {
                selectedIndex_ = chosen;
                pendingSelection_ = chosen;
                hasPendingSelection_ = true;
                close();
            }
            return true;
        }
    }

    return true;
}

} // namespace solengine
