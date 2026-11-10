#include "solengine/ui/seed_dialog.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/settings_manager.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/table_renderer.hpp"
#include "eatsbits/ui/glyph/font_engine.hpp"

#if !defined(__EMSCRIPTEN__)
#include <GLFW/glfw3.h>
#else
#include <emscripten.h>
#include <GLFW/glfw3.h>
#endif

#include <random>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace solengine {

SeedDialog::SeedDialog() = default;

void SeedDialog::open(uint32_t currentSeed, const std::string& variantName) {
    seed_ = currentSeed;
    seedText_ = std::to_string(currentSeed);
    variantName_ = variantName;
    isOpen_ = true;
    editingSeed_ = true;
    cursorBlink_ = 0.0f;
    copyFeedbackTimer_ = 0.0f;
}

void SeedDialog::close() noexcept {
    isOpen_ = false;
    editingSeed_ = false;
}

void SeedDialog::randomizeSeed() {
    std::random_device rd;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    uint32_t s = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(now)) & 0x7FFFFFFFu;
    if (s == 0) s = 2026u;
    seed_ = s;
    seedText_ = std::to_string(s);
}

void SeedDialog::copyToClipboard(void* nativeWindow) {
    if (seedText_.empty()) return;
#if !defined(__EMSCRIPTEN__)
    if (nativeWindow) {
        glfwSetClipboardString(static_cast<GLFWwindow*>(nativeWindow), seedText_.c_str());
    }
#else
    EM_ASM({
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(UTF8ToString($0));
        }
    }, seedText_.c_str());
#endif
    copyFeedbackTimer_ = 1.8f; // Show "COPIED!" feedback for 1.8s
}

void SeedDialog::pasteFromClipboard(void* nativeWindow) {
    std::string text;
#if !defined(__EMSCRIPTEN__)
    if (nativeWindow) {
        const char* clip = glfwGetClipboardString(static_cast<GLFWwindow*>(nativeWindow));
        if (clip) text = clip;
    }
#endif
    if (text.empty()) return;

    std::string digits;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
            if (digits.size() >= 10) break;
        }
    }
    if (!digits.empty()) {
        try {
            unsigned long val = std::stoul(digits);
            seed_ = static_cast<uint32_t>(val);
            seedText_ = std::to_string(seed_);
        } catch (...) {}
    }
}

void SeedDialog::commitDeal(SolitaireBoard& board, AudioFoleyEngine& foley) {
    try {
        if (!seedText_.empty()) {
            unsigned long val = std::stoul(seedText_);
            seed_ = static_cast<uint32_t>(val);
        } else {
            seed_ = 2026u;
        }
    } catch (...) {
        seed_ = 2026u;
    }

    SettingsManager::instance().settings().lastSeed = seed_;
    SettingsManager::instance().save();

    if (onDealSeed) {
        onDealSeed(seed_);
    } else {
        board.newDeal(seed_, board.variant());
    }

    foley.triggerSound(FoleySound::Shuffle, 0.9f);
    close();
}

void SeedDialog::render(
    eatsbits::ui::BatchRenderer2D& r,
    float screenW,
    float screenH,
    float mouseX,
    float mouseY,
    const ThemePalette& /*pal*/,
    float timeSec,
    void* /*nativeWindow*/
) {
    if (!isOpen_) return;

    cursorBlink_ += 0.016f;
    if (copyFeedbackTimer_ > 0.0f) {
        copyFeedbackTimer_ -= 0.016f;
        if (copyFeedbackTimer_ < 0.0f) copyFeedbackTimer_ = 0.0f;
    }

    const auto& cPal = clockwork_ui::palette();

    // 1. Semi-translucent atmospheric backdrop darkening
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.02f, 0.02f, 0.02f, 0.72f);

    // 2. Dialog Placement
    dialogW_ = std::min(480.0f, screenW - 32.0f);
    dialogH_ = 280.0f;
    dialogX_ = (screenW - dialogW_) * 0.5f;
    dialogY_ = (screenH - dialogH_) * 0.5f;

    // Drop shadow
    r.drawRectDropShadow(
        dialogX_, dialogY_, dialogW_, dialogH_, 16.0f, 20.0f,
        eatsbits::ui::LightSource2D{dialogX_ + dialogW_ * 0.5f, dialogY_ - 200.0f, 500.0f, 0.90f}, 0.70f
    );

    // Chassis frame
    clockwork_ui::drawChassisFrame(r, dialogX_, dialogY_, dialogW_, dialogH_, 8.0f, true, false, timeSec);

    // 3. Header Title Bar
    const float headerH = 46.0f;
    r.drawRoundedRectGradient(
        dialogX_ + 4.0f, dialogY_ + 4.0f, dialogW_ - 8.0f, headerH, 6.0f,
        cPal.chassisDarkR + 0.05f, cPal.chassisDarkG + 0.05f, cPal.chassisDarkB + 0.04f,
        cPal.chassisDarkR, cPal.chassisDarkG, cPal.chassisDarkB,
        0.98f, 4
    );
    r.drawLine(dialogX_ + 4.0f, dialogY_ + 4.0f + headerH, dialogX_ + dialogW_ - 4.0f, dialogY_ + 4.0f + headerH,
               cPal.brassBrightR, cPal.brassBrightG, cPal.brassBrightB, 0.5f, 1.2f);

    // Decorative Steam Pipe behind Header
    clockwork_ui::drawPipe(r, dialogX_ + 14.0f, dialogY_ + 10.0f, dialogX_ + dialogW_ - 14.0f, dialogY_ + 10.0f, 5.0f, true, true);

    // Header Manometer Badge
    clockwork_ui::drawManometerGauge(r, dialogX_ + 32.0f, dialogY_ + 26.0f, 15.0f, timeSec * 0.6f, 0.65f, false, nullptr);

    // Title Text
    const char* title = "DEAL SEED TRANSMUTATION";
    eatsbits::ui::glyph::drawVectorText(
        r, title, dialogX_ + 56.0f, dialogY_ + 14.0f, 13.5f,
        cPal.brassHighlightR, cPal.brassHighlightG, cPal.brassHighlightB, 1.0f
    );

    // Subtitle (Variant Name)
    std::string subTitle = "TARGET GAME: " + variantName_;
    eatsbits::ui::glyph::drawVectorText(
        r, subTitle.c_str(), dialogX_ + 56.0f, dialogY_ + 30.0f, 10.0f,
        cPal.furnaceAmberR, cPal.furnaceAmberG, cPal.furnaceAmberB, 0.90f
    );

    // Close button 'X' in top-right
    const float closeBtnSize = 26.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 12.0f;
    const float closeBtnY = dialogY_ + 10.0f;
    const bool closeHov = (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize &&
                           mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize);

    clockwork_ui::drawClockworkCloseButton(r, closeBtnX, closeBtnY, closeBtnSize, closeHov, timeSec);

    // 4. Content Area
    float curY = dialogY_ + headerH + 24.0f;
    const float contentX = dialogX_ + 24.0f;
    const float contentW = dialogW_ - 48.0f;

    // Label above Seed Input
    eatsbits::ui::glyph::drawVectorText(
        r, "RANDOMIZATION SEED (31-BIT INTEGER):", contentX, curY, 11.0f,
        cPal.dialParchmentR, cPal.dialParchmentG, cPal.dialParchmentB, 0.90f
    );
    curY += 18.0f;

    // --- Seed Input Row ---
    const float inputH = 38.0f;
    const float stepBtnW = 34.0f;
    const float stepBtnGap = 6.0f;
    const float inputW = contentW - (stepBtnW * 2.0f + stepBtnGap * 2.0f);

    // Input box
    const bool inputHov = (mouseX >= contentX && mouseX <= contentX + inputW &&
                           mouseY >= curY && mouseY <= curY + inputH);
    r.drawRoundedRect(contentX, curY, inputW, inputH, 5.0f, 0.07f, 0.06f, 0.05f, 0.98f, 4);
    r.drawRoundedRectOutline(
        contentX, curY, inputW, inputH, 5.0f,
        editingSeed_ ? cPal.furnaceAmberR : (inputHov ? cPal.brassBrightR : cPal.brassDarkR),
        editingSeed_ ? cPal.furnaceAmberG : (inputHov ? cPal.brassBrightG : cPal.brassDarkG),
        editingSeed_ ? cPal.furnaceAmberB : (inputHov ? cPal.brassBrightB : cPal.brassDarkB),
        0.95f, editingSeed_ ? 1.6f : 1.2f, 4
    );

    // Inner shadow for inset depth
    r.drawLine(contentX + 2.0f, curY + 2.0f, contentX + inputW - 2.0f, curY + 2.0f, 0.02f, 0.02f, 0.02f, 0.6f, 1.5f);

    // Displayed seed text
    const bool showCursor = editingSeed_ && (std::fmod(cursorBlink_, 1.0f) < 0.5f);
    std::string dispStr = "# " + seedText_ + (showCursor ? "|" : "");
    eatsbits::ui::glyph::drawVectorText(
        r, dispStr.c_str(), contentX + 12.0f, curY + 11.0f, 15.0f,
        cPal.brassHighlightR, cPal.brassHighlightG, cPal.brassHighlightB, 1.0f
    );

    // [-] Step Button
    const float decX = contentX + inputW + stepBtnGap;
    const bool decHov = (mouseX >= decX && mouseX <= decX + stepBtnW && mouseY >= curY && mouseY <= curY + inputH);
    clockwork_ui::drawPewterButton(r, decX, curY, stepBtnW, inputH, 4.0f, "-", decHov, false, 16.0f);

    // [+] Step Button
    const float incX = decX + stepBtnW + stepBtnGap;
    const bool incHov = (mouseX >= incX && mouseX <= incX + stepBtnW && mouseY >= curY && mouseY <= curY + inputH);
    clockwork_ui::drawPewterButton(r, incX, curY, stepBtnW, inputH, 4.0f, "+", incHov, false, 16.0f);

    curY += inputH + 16.0f;

    // --- Action Button Row (COPY, PASTE, RANDOMIZE) ---
    const float btnRowH = 34.0f;
    const float copyW = (contentW - 16.0f) / 3.0f;
    const float pasteW = copyW;
    const float randW = contentW - copyW - pasteW - 16.0f;

    // 1. Copy Button
    const float copyX = contentX;
    const bool copyHov = (mouseX >= copyX && mouseX <= copyX + copyW && mouseY >= curY && mouseY <= curY + btnRowH);
    const char* copyLbl = (copyFeedbackTimer_ > 0.0f) ? "COPIED!" : "COPY";
    clockwork_ui::drawBrassButton(r, copyX, curY, copyW, btnRowH, 4.0f, copyLbl, copyHov, copyFeedbackTimer_ > 0.0f, 11.5f);

    // 2. Paste Button
    const float pasteX = copyX + copyW + 8.0f;
    const bool pasteHov = (mouseX >= pasteX && mouseX <= pasteX + pasteW && mouseY >= curY && mouseY <= curY + btnRowH);
    clockwork_ui::drawBrassButton(r, pasteX, curY, pasteW, btnRowH, 4.0f, "PASTE", pasteHov, false, 11.5f);

    // 3. Randomize Button
    const float randX = pasteX + pasteW + 8.0f;
    const bool randHov = (mouseX >= randX && mouseX <= randX + randW && mouseY >= curY && mouseY <= curY + btnRowH);
    clockwork_ui::drawBrassButton(r, randX, curY, randW, btnRowH, 4.0f, "RANDOMIZE", randHov, false, 11.5f);

    curY += btnRowH + 24.0f;

    // --- Bottom Row: [ CANCEL ] and [ DEAL NEW GAME ] ---
    const float btmH = 38.0f;
    const float cancelW = 120.0f;
    const float dealW = contentW - cancelW - 12.0f;

    // Cancel Button
    const float cancelX = contentX;
    const bool cancelHov = (mouseX >= cancelX && mouseX <= cancelX + cancelW && mouseY >= curY && mouseY <= curY + btmH);
    clockwork_ui::drawPewterButton(r, cancelX, curY, cancelW, btmH, 5.0f, "CANCEL", cancelHov, false, 12.0f);

    // Deal Button (Furnace Amber / Glowing)
    const float dealX = cancelX + cancelW + 12.0f;
    const bool dealHov = (mouseX >= dealX && mouseX <= dealX + dealW && mouseY >= curY && mouseY <= curY + btmH);
    clockwork_ui::drawFurnaceButton(r, dealX, curY, dealW, btmH, 5.0f, "DEAL NEW GAME", dealHov, false, 12.5f);
}

bool SeedDialog::handleMouseButton(
    int button,
    int action,
    float mouseX,
    float mouseY,
    SolitaireBoard& board,
    AudioFoleyEngine& foley,
    void* nativeWindow
) {
    if (!isOpen_) return false;
    if (button != 0 || action != 1) return true; // Absorb all clicks inside modal backdrop

    // Check outside dialog click -> Close
    if (mouseX < dialogX_ || mouseX > dialogX_ + dialogW_ ||
        mouseY < dialogY_ || mouseY > dialogY_ + dialogH_) {
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    const float headerH = 46.0f;
    const float closeBtnSize = 26.0f;
    const float closeBtnX = dialogX_ + dialogW_ - closeBtnSize - 12.0f;
    const float closeBtnY = dialogY_ + 10.0f;

    // Close 'X'
    if (mouseX >= closeBtnX && mouseX <= closeBtnX + closeBtnSize &&
        mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnSize) {
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    float curY = dialogY_ + headerH + 24.0f + 18.0f;
    const float contentX = dialogX_ + 24.0f;
    const float contentW = dialogW_ - 48.0f;
    const float inputH = 38.0f;
    const float stepBtnW = 34.0f;
    const float stepBtnGap = 6.0f;
    const float inputW = contentW - (stepBtnW * 2.0f + stepBtnGap * 2.0f);

    // Input box focus
    if (mouseX >= contentX && mouseX <= contentX + inputW &&
        mouseY >= curY && mouseY <= curY + inputH) {
        editingSeed_ = true;
        return true;
    }

    // Step Down [-]
    const float decX = contentX + inputW + stepBtnGap;
    if (mouseX >= decX && mouseX <= decX + stepBtnW && mouseY >= curY && mouseY <= curY + inputH) {
        if (seed_ > 1) {
            --seed_;
            seedText_ = std::to_string(seed_);
            foley.triggerSound(FoleySound::WoodClick, 0.6f);
        }
        return true;
    }

    // Step Up [+]
    const float incX = decX + stepBtnW + stepBtnGap;
    if (mouseX >= incX && mouseX <= incX + stepBtnW && mouseY >= curY && mouseY <= curY + inputH) {
        ++seed_;
        seedText_ = std::to_string(seed_);
        foley.triggerSound(FoleySound::WoodClick, 0.6f);
        return true;
    }

    curY += inputH + 16.0f;
    const float btnRowH = 34.0f;
    const float copyW = (contentW - 16.0f) / 3.0f;
    const float pasteW = copyW;
    const float randW = contentW - copyW - pasteW - 16.0f;

    // Copy Button
    const float copyX = contentX;
    if (mouseX >= copyX && mouseX <= copyX + copyW && mouseY >= curY && mouseY <= curY + btnRowH) {
        copyToClipboard(nativeWindow);
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    // Paste Button
    const float pasteX = copyX + copyW + 8.0f;
    if (mouseX >= pasteX && mouseX <= pasteX + pasteW && mouseY >= curY && mouseY <= curY + btnRowH) {
        pasteFromClipboard(nativeWindow);
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    // Randomize Button
    const float randX = pasteX + pasteW + 8.0f;
    if (mouseX >= randX && mouseX <= randX + randW && mouseY >= curY && mouseY <= curY + btnRowH) {
        randomizeSeed();
        foley.triggerSound(FoleySound::Shuffle, 0.8f);
        return true;
    }

    curY += btnRowH + 24.0f;
    const float btmH = 38.0f;
    const float cancelW = 120.0f;
    const float dealW = contentW - cancelW - 12.0f;

    // Cancel Button
    const float cancelX = contentX;
    if (mouseX >= cancelX && mouseX <= cancelX + cancelW && mouseY >= curY && mouseY <= curY + btmH) {
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    // Deal Button
    const float dealX = cancelX + cancelW + 12.0f;
    if (mouseX >= dealX && mouseX <= dealX + dealW && mouseY >= curY && mouseY <= curY + btmH) {
        commitDeal(board, foley);
        return true;
    }

    return true;
}

bool SeedDialog::handleMouseMove(float /*mouseX*/, float /*mouseY*/) {
    return isOpen_;
}

bool SeedDialog::handleKey(
    int key,
    int action,
    int mods,
    SolitaireBoard& board,
    AudioFoleyEngine& foley,
    void* nativeWindow
) {
    if (!isOpen_ || action == 0) return false;

    // Esc = Close
    if (key == 256) {
        close();
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    // Enter = Commit Deal
    if (key == 257 || key == 335) {
        commitDeal(board, foley);
        return true;
    }

    // Ctrl+C = Copy
    if ((key == 67 || key == 'C') && (mods & 2)) {
        copyToClipboard(nativeWindow);
        foley.triggerSound(FoleySound::WoodClick, 0.6f);
        return true;
    }

    // Ctrl+V = Paste
    if ((key == 86 || key == 'V') && (mods & 2)) {
        pasteFromClipboard(nativeWindow);
        foley.triggerSound(FoleySound::WoodClick, 0.6f);
        return true;
    }

    // Backspace
    if (key == 259 && editingSeed_) {
        if (!seedText_.empty()) {
            seedText_.pop_back();
            try {
                seed_ = seedText_.empty() ? 0u : static_cast<uint32_t>(std::stoul(seedText_));
            } catch (...) {}
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
        }
        return true;
    }

    return true;
}

bool SeedDialog::handleChar(unsigned int codepoint) {
    if (!isOpen_ || !editingSeed_) return false;

    if (codepoint >= '0' && codepoint <= '9' && seedText_.size() < 10) {
        seedText_.push_back(static_cast<char>(codepoint));
        try {
            seed_ = static_cast<uint32_t>(std::stoul(seedText_));
        } catch (...) {}
        return true;
    }

    return true;
}

} // namespace solengine

