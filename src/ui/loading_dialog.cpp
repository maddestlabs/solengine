#include "solengine/ui/loading_dialog.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/classic_deck_symbols.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace solengine {

LoadingDialog::LoadingDialog() = default;

void LoadingDialog::open(const std::string& title, const std::string& initialStatus, bool cancellable) {
    isOpen_ = true;
    cancellable_ = cancellable;
    cancelRequested_ = false;
    progress_ = 0.0f;
    title_ = title.empty() ? "PREPARING PROCEDURAL ASSETS" : title;
    status_ = initialStatus.empty() ? "Initializing synthesis..." : initialStatus;
}

void LoadingDialog::close() {
    isOpen_ = false;
    cancelRequested_ = false;
    progress_ = 0.0f;
}

void LoadingDialog::setProgress(float progress) noexcept {
    progress_ = std::clamp(progress, 0.0f, 1.0f);
}

void LoadingDialog::setTitle(const std::string& title) {
    title_ = title;
}

void LoadingDialog::setStatus(const std::string& status) {
    status_ = status;
}

void LoadingDialog::render(
    eatsbits::ui::BatchRenderer2D& r,
    float screenW,
    float screenH,
    float mouseX,
    float mouseY,
    const ThemePalette& pal,
    double currentTime
) {
    if (!isOpen_) return;

    // 1. Semi-transparent backdrop wash
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.0f, 0.0f, 0.0f, 0.65f);

    // 2. Center modal dialog box (responsively bounded)
    dialogW_ = std::min(std::max(screenW - 24.0f, 280.0f), 580.0f);
    dialogH_ = std::min(screenH - 24.0f, 230.0f);
    dialogX_ = (screenW - dialogW_) * 0.5f;
    dialogY_ = (screenH - dialogH_) * 0.5f;

    // Drop shadow
    eatsbits::ui::LightSource2D light{screenW * 0.5f, -200.0f, 1.0f};
    r.drawRectDropShadow(dialogX_, dialogY_, dialogW_, dialogH_, 12.0f, 22.0f, light, 0.70f);

    // Main chassis background & gold rim
    r.drawRoundedRectGradient(
        dialogX_, dialogY_, dialogW_, dialogH_, 8.0f,
        pal.woodR0 + 0.05f, pal.woodG0 + 0.04f, pal.woodB0 + 0.03f,
        pal.woodR1 + 0.01f, pal.woodG1 + 0.01f, pal.woodB1 + 0.01f,
        0.98f, 6
    );
    r.drawRoundedRectOutline(dialogX_, dialogY_, dialogW_, dialogH_, 8.0f, pal.trimR, pal.trimG, pal.trimB, 0.95f, 1.6f, 6);

    // 3. Header Bar
    const float headerH = 42.0f;
    r.drawRoundedRect(dialogX_, dialogY_, dialogW_, headerH, 8.0f, pal.woodR0 + 0.02f, pal.woodG0 + 0.01f, pal.woodB0 + 0.01f, 0.96f, 6);
    r.drawLine(dialogX_, dialogY_ + headerH, dialogX_ + dialogW_, dialogY_ + headerH, pal.trimR, pal.trimG, pal.trimB, 0.55f, 1.0f);

    SvgIconRenderer::instance().renderIcon(
        r, SvgIconId::CardStock,
        dialogX_ + 16.0f, dialogY_ + 11.0f, 20.0f, 20.0f,
        pal.accentR, pal.accentG, pal.accentB, 1.0f
    );
    eatsbits::ui::glyph::drawVectorText(
        r, title_.c_str(),
        dialogX_ + 44.0f, dialogY_ + 14.5f, 13.0f,
        pal.accentR, pal.accentG, pal.accentB, 1.0f
    );

    // 4. Animated Suit Pips Row (♠ ♥ ♦ ♣)
    const float pipCenterY = dialogY_ + 76.0f;
    const float pipSpacing = 36.0f;
    const float pipStartX = dialogX_ + (dialogW_ - 3.0f * pipSpacing) * 0.5f;
    const int activeSuitIdx = static_cast<int>(currentTime * 3.5) % 4;

    const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
    for (int s = 0; s < 4; ++s) {
        float cx = pipStartX + static_cast<float>(s) * pipSpacing;
        bool isActive = (s == activeSuitIdx);
        float radius = isActive ? 10.0f : 8.0f;
        float alpha = isActive ? 1.0f : 0.35f;

        float pr, pg, pb;
        if (suits[s] == Suit::Hearts || suits[s] == Suit::Diamonds) {
            pr = 0.90f; pg = 0.22f; pb = 0.22f;
        } else {
            pr = 0.88f; pg = 0.86f; pb = 0.82f;
        }

        if (isActive) {
            // Subtle glowing ring behind active pip
            r.drawCircle(cx, pipCenterY, radius + 4.0f, pal.accentR, pal.accentG, pal.accentB, 0.18f, 16);
        }
        drawClassicSuitPip(r, suits[s], cx, pipCenterY, radius, radius, pr, pg, pb, alpha);
    }

    // 5. Status Text
    float statusY = dialogY_ + 110.0f;
    eatsbits::ui::glyph::drawVectorText(
        r, status_.c_str(),
        dialogX_ + 24.0f, statusY, 11.5f,
        0.94f, 0.90f, 0.82f, 0.95f
    );

    // 6. Progress Bar
    const float barX = dialogX_ + 24.0f;
    const float barY = dialogY_ + 134.0f;
    const float barW = dialogW_ - 48.0f;
    const float barH = 12.0f;

    // Track
    r.drawRoundedRect(barX, barY, barW, barH, 4.0f, 0.08f, 0.06f, 0.05f, 0.95f, 4);
    r.drawRoundedRectOutline(barX, barY, barW, barH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.45f, 1.0f, 4);

    // Fill
    float fillW = std::clamp(progress_, 0.0f, 1.0f) * (barW - 4.0f);
    if (fillW > 2.0f) {
        r.drawRoundedRect(
            barX + 2.0f, barY + 2.0f, fillW, barH - 4.0f, 2.5f,
            pal.accentR, pal.accentG, pal.accentB, 0.95f, 2
        );
    }

    // Percentage Label
    char pctStr[16];
    std::snprintf(pctStr, sizeof(pctStr), "%3d%%", static_cast<int>(std::round(progress_ * 100.0f)));
    eatsbits::ui::glyph::drawVectorText(
        r, pctStr,
        dialogX_ + dialogW_ - 60.0f, barY - 18.0f, 11.0f,
        pal.accentR, pal.accentG, pal.accentB, 0.95f
    );

    // 7. Cancel Button
    if (cancellable_) {
        cancelW_ = 150.0f;
        cancelH_ = 32.0f;
        cancelX_ = dialogX_ + (dialogW_ - cancelW_) * 0.5f;
        cancelY_ = dialogY_ + dialogH_ - 46.0f;

        bool hovered = (mouseX >= cancelX_ && mouseX <= cancelX_ + cancelW_ &&
                        mouseY >= cancelY_ && mouseY <= cancelY_ + cancelH_);

        float bgR = pal.woodR0 + (hovered ? 0.12f : 0.04f);
        float bgG = pal.woodG0 + (hovered ? 0.09f : 0.03f);
        float bgB = pal.woodB0 + (hovered ? 0.07f : 0.02f);

        r.drawRoundedRect(cancelX_, cancelY_, cancelW_, cancelH_, 4.0f, bgR, bgG, bgB, 0.95f, 4);
        r.drawRoundedRectOutline(
            cancelX_, cancelY_, cancelW_, cancelH_, 4.0f,
            pal.trimR, pal.trimG, pal.trimB,
            hovered ? 1.0f : 0.70f, hovered ? 1.4f : 1.0f, 4
        );

        SvgIconRenderer::instance().renderIcon(
            r, SvgIconId::Close,
            cancelX_ + 14.0f, cancelY_ + 8.0f, 16.0f, 16.0f,
            hovered ? pal.accentR : pal.trimR,
            hovered ? pal.accentG : pal.trimG,
            hovered ? pal.accentB : pal.trimB,
            1.0f
        );

        const char* cancelLabel = cancelRequested_ ? "CANCELLING..." : "CANCEL [ESC]";
        eatsbits::ui::glyph::drawVectorText(
            r, cancelLabel,
            cancelX_ + 36.0f, cancelY_ + 9.5f, 11.5f,
            hovered ? pal.accentR : pal.trimR,
            hovered ? pal.accentG : pal.trimG,
            hovered ? pal.accentB : pal.trimB,
            1.0f
        );
    }
}

bool LoadingDialog::handleMouseButton(int button, int action, float mouseX, float mouseY) {
    if (!isOpen_) return false;

    // Absorb clicks
    if (cancellable_ && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (mouseX >= cancelX_ && mouseX <= cancelX_ + cancelW_ &&
            mouseY >= cancelY_ && mouseY <= cancelY_ + cancelH_) {
            requestCancel();
            status_ = "Cancelling operation...";
        }
    }
    return true; // Modal absorbs all mouse events
}

bool LoadingDialog::handleKey(int key, int action, int /*mods*/) {
    if (!isOpen_) return false;

    if (cancellable_ && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        requestCancel();
        status_ = "Cancelling operation...";
    }
    return true; // Modal absorbs all keyboard events
}

} // namespace solengine

