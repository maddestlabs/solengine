#include "solengine/ui/seed_dialog.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "solengine/settings_manager.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/deterministic_rng.hpp"
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

void SeedDialog::recordHistory(uint32_t seed, const std::string& gameId, const std::string& gameName, uint32_t layoutSeed) {
    if (seed == 0u) return;

    const std::string displayName = !gameName.empty() ? gameName : (!gameId.empty() ? gameId : std::string("Solitaire"));
    for (auto it = history_.begin(); it != history_.end(); ++it) {
        if (it->seed == seed && it->layoutSeed == layoutSeed && it->gameId == gameId) {
            SeedHistoryEntry existing = std::move(*it);
            if (!displayName.empty()) existing.gameName = displayName;
            history_.erase(it);
            history_.insert(history_.begin(), std::move(existing));
            clampHistoryScroll();
            return;
        }
    }

    history_.insert(history_.begin(), SeedHistoryEntry{seed, layoutSeed, gameId, displayName});
    if (history_.size() > kMaxHistory) {
        history_.resize(kMaxHistory);
    }
    clampHistoryScroll();
}

std::string SeedDialog::formatSeedText() const {
    if (layoutSeed_ != 0u) {
        return std::to_string(seed_) + ":" + std::to_string(layoutSeed_);
    }
    return std::to_string(seed_);
}

bool SeedDialog::parseSeedText(const std::string& text, uint32_t& outSeed, uint32_t& outLayoutSeed) {
    std::string left;
    std::string right;
    bool seenColon = false;
    for (char c : text) {
        if (c == ':' && !seenColon) {
            seenColon = true;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (!seenColon) {
                if (left.size() < 10) left.push_back(c);
            } else {
                if (right.size() < 10) right.push_back(c);
            }
        }
    }
    if (left.empty()) return false;
    try {
        outSeed = static_cast<uint32_t>(std::stoul(left));
        if (seenColon && !right.empty()) {
            outLayoutSeed = static_cast<uint32_t>(std::stoul(right));
        } else {
            outLayoutSeed = 0u;
        }
        return true;
    } catch (...) {
        return false;
    }
}

void SeedDialog::open(uint32_t currentSeed, const std::string& variantName, const std::string& gameId,
                      uint32_t currentLayoutSeed, bool supportsLayoutLock) {
    seed_ = currentSeed;
    layoutSeed_ = currentLayoutSeed;
    supportsLayoutLock_ = supportsLayoutLock;
    seedText_ = formatSeedText();
    variantName_ = variantName;
    targetGameId_ = gameId;
    activeBoardSeed_ = currentSeed;
    activeBoardGameId_ = gameId;
    isOpen_ = true;
    editingSeed_ = true;
    cursorBlink_ = 0.0f;
    copyFeedbackTimer_ = 0.0f;
    replayCopyFeedbackTimer_ = 0.0f;
    historyScroller_.reset(0.0f);
    pressedHistoryIdx_ = -1;
    pressedHistoryPlay_ = false;

    // Ensure the currently active deal is present in history without reordering existing entries
    bool found = false;
    for (const auto& entry : history_) {
        if (entry.seed == currentSeed && entry.layoutSeed == currentLayoutSeed && entry.gameId == gameId) {
            found = true;
            break;
        }
    }
    if (!found && currentSeed != 0u) {
        recordHistory(currentSeed, gameId, variantName, currentLayoutSeed);
    }
}

void SeedDialog::close() noexcept {
    isOpen_ = false;
    editingSeed_ = false;
    historyScroller_.cancelDrag();
    pressedHistoryIdx_ = -1;
    pressedHistoryPlay_ = false;
}

void SeedDialog::update(float dt) noexcept {
    if (!isOpen_) return;
    historyScroller_.update(dt);
}

void SeedDialog::randomizeSeed() {
    seed_ = generateRandomSeed();
    seedText_ = formatSeedText();
    recordHistory(seed_, targetGameId_, variantName_, layoutSeed_);
    historyScroller_.scrollTo(0.0f, true);
}

void SeedDialog::copyTextToClipboard(const std::string& text, void* nativeWindow) {
    if (text.empty()) return;
#if !defined(__EMSCRIPTEN__)
    if (nativeWindow) {
        glfwSetClipboardString(static_cast<GLFWwindow*>(nativeWindow), text.c_str());
    }
#else
    EM_ASM({
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(UTF8ToString($0));
        }
    }, text.c_str());
#endif
}

void SeedDialog::copyToClipboard(void* nativeWindow) {
    if (seedText_.empty()) return;
    copyTextToClipboard(seedText_, nativeWindow);
    copyFeedbackTimer_ = 1.8f; // Show "COPIED!" feedback for 1.8s
}

void SeedDialog::copyReplayToClipboard(const SolitaireBoard& board, void* nativeWindow) {
    ReplayTrack track = ReplayCodec::extractFromBoard(board, activeBoardGameId_, variantName_);
    const std::string code = ReplayCodec::encode(track);
    copyTextToClipboard(code, nativeWindow);
    replayCopyFeedbackTimer_ = 1.8f;
}

bool SeedDialog::pasteFromClipboard(void* nativeWindow, SolitaireBoard& board, AudioFoleyEngine& foley) {
    std::string text;
#if !defined(__EMSCRIPTEN__)
    if (nativeWindow) {
        const char* clip = glfwGetClipboardString(static_cast<GLFWwindow*>(nativeWindow));
        if (clip) text = clip;
    }
#endif
    if (text.empty()) return false;

    // Auto-detect SolEngine annotated replay / tutorial code ("sol1:...")
    if (ReplayCodec::looksLikeReplayCode(text)) {
        ReplayTrack track;
        if (ReplayCodec::decode(text, track)) {
            if (onLaunchReplay) {
                onLaunchReplay(track, track.isGuided());
            } else {
                board.newDeal(track.seed, board.variant());
            }
            foley.triggerSound(FoleySound::Shuffle, 0.9f);
            close();
            return true;
        }
    }

    uint32_t parsedSeed = 0u;
    uint32_t parsedLayout = 0u;
    if (parseSeedText(text, parsedSeed, parsedLayout)) {
        seed_ = parsedSeed;
        layoutSeed_ = parsedLayout;
        seedText_ = formatSeedText();
        return true;
    }
    return false;
}

void SeedDialog::commitDeal(SolitaireBoard& board, AudioFoleyEngine& foley) {
    uint32_t parsedSeed = 2026u;
    uint32_t parsedLayout = layoutSeed_;
    if (parseSeedText(seedText_, parsedSeed, parsedLayout)) {
        seed_ = parsedSeed;
        layoutSeed_ = parsedLayout;
    } else {
        seed_ = 2026u;
    }

    commitHistoryReplay(seed_, layoutSeed_, targetGameId_, variantName_, board, foley);
}

void SeedDialog::commitHistoryReplay(
    uint32_t seed,
    uint32_t layoutSeed,
    const std::string& gameId,
    const std::string& gameName,
    SolitaireBoard& board,
    AudioFoleyEngine& foley
) {
    seed_ = (seed == 0u) ? 2026u : seed;
    layoutSeed_ = layoutSeed;
    seedText_ = formatSeedText();
    recordHistory(seed_, gameId, gameName, layoutSeed_);

    board.setLayoutSeed(layoutSeed_);
    SettingsManager::instance().settings().lastSeed = seed_;
    SettingsManager::instance().save();

    if (!gameId.empty() && onReplayHistory) {
        onReplayHistory(seed_, gameId);
    } else if (onDealSeed) {
        onDealSeed(seed_);
    } else {
        board.newDeal(seed_, board.variant());
    }

    foley.triggerSound(FoleySound::Shuffle, 0.9f);
    close();
}

int SeedDialog::computeVisibleHistoryRows(float screenH) const noexcept {
    if (history_.empty()) return 0;
    const int maxRows = (screenH < 520.0f) ? 3 : 4;
    return std::min<int>(maxRows, static_cast<int>(history_.size()));
}

void SeedDialog::clampHistoryScroll() noexcept {
    const float histRowH = 30.0f;
    const float histRowGap = 4.0f;
    const int total = static_cast<int>(history_.size());
    const float totalH = (total > 0)
        ? (static_cast<float>(total) * histRowH + static_cast<float>(std::max(0, total - 1)) * histRowGap)
        : 0.0f;
    const float viewH = (visibleHistoryRows_ > 0)
        ? (static_cast<float>(visibleHistoryRows_) * histRowH + static_cast<float>(std::max(0, visibleHistoryRows_ - 1)) * histRowGap)
        : 0.0f;
    historyScroller_.setBounds(totalH, std::max(1.0f, viewH));
}

bool SeedDialog::handleScroll(float yoffset) {
    if (!isOpen_ || history_.empty()) return false;
    if (visibleHistoryRows_ <= 0) return true;
    historyScroller_.handleWheel(yoffset, 34.0f);
    return true;
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
    if (replayCopyFeedbackTimer_ > 0.0f) {
        replayCopyFeedbackTimer_ -= 0.016f;
        if (replayCopyFeedbackTimer_ < 0.0f) replayCopyFeedbackTimer_ = 0.0f;
    }

    const auto& cPal = clockwork_ui::palette();

    // 1. Semi-translucent atmospheric backdrop darkening
    r.drawRect(0.0f, 0.0f, screenW, screenH, 0.02f, 0.02f, 0.02f, 0.72f);

    // 2. Dialog Placement
    visibleHistoryRows_ = computeVisibleHistoryRows(screenH);
    clampHistoryScroll();

    const float headerH = 46.0f;
    const float inputH = 38.0f;
    const float btnRowH = 34.0f;
    const float lockRowH = supportsLayoutLock_ ? 32.0f : 0.0f;
    const float lockRowGap = supportsLayoutLock_ ? 10.0f : 0.0f;
    const float histRowH = 30.0f;
    const float histRowGap = 4.0f;
    const float histHeaderH = 22.0f;
    const float btmH = 38.0f;

    float histSectionH = 0.0f;
    if (visibleHistoryRows_ > 0) {
        const float rowsH = static_cast<float>(visibleHistoryRows_) * histRowH
                          + static_cast<float>(std::max(0, visibleHistoryRows_ - 1)) * histRowGap;
        histSectionH = 10.0f + histHeaderH + 4.0f + rowsH + 16.0f;
    } else {
        histSectionH = 8.0f;
    }

    dialogW_ = std::min(500.0f, screenW - 24.0f);
    dialogH_ = headerH + 20.0f + 18.0f + inputH + 14.0f + btnRowH + lockRowGap + lockRowH + 16.0f + histSectionH + btmH + 18.0f;
    dialogX_ = (screenW - dialogW_) * 0.5f;
    dialogY_ = std::max(12.0f, (screenH - dialogH_) * 0.5f);

    // Drop shadow
    r.drawRectDropShadow(
        dialogX_, dialogY_, dialogW_, dialogH_, 16.0f, 20.0f,
        eatsbits::ui::LightSource2D{dialogX_ + dialogW_ * 0.5f, dialogY_ - 200.0f, 500.0f, 0.90f}, 0.70f
    );

    // Chassis frame
    clockwork_ui::drawChassisFrame(r, dialogX_, dialogY_, dialogW_, dialogH_, 8.0f, true, false, timeSec);

    // 3. Header Title Bar
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

    // Header Low-Poly Seed Icon
    vector_icon::drawSeedIcon(
        r, dialogX_ + 32.0f, dialogY_ + 26.0f, 13.5f,
        cPal.brassBrightR, cPal.brassBrightG, cPal.brassBrightB,
        cPal.brassHighlightR, cPal.brassHighlightG, cPal.brassHighlightB,
        1.0f, true
    );

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
    float curY = dialogY_ + headerH + 20.0f;
    const float contentX = dialogX_ + 24.0f;
    const float contentW = dialogW_ - 48.0f;

    // Label above Seed Input
    const char* seedLabel = supportsLayoutLock_
        ? "DEAL SEED (OR DEAL:LAYOUT FOR LOCKED SHAPE):"
        : "RANDOMIZATION SEED (31-BIT INTEGER):";
    eatsbits::ui::glyph::drawVectorText(
        r, seedLabel, contentX, curY, 11.0f,
        cPal.dialParchmentR, cPal.dialParchmentG, cPal.dialParchmentB, 0.90f
    );
    curY += 18.0f;

    // --- Seed Input Row ---
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

    curY += inputH + 14.0f;

    // --- Action Button Row (COPY, REPLAY, PASTE, RANDOM) ---
    const float btnGap = 6.0f;
    const float copyW = (contentW - btnGap * 3.0f) * 0.25f;
    const float replayW = copyW;
    const float pasteW = copyW;
    const float randW = contentW - copyW - replayW - pasteW - btnGap * 3.0f;

    // 1. Copy Seed Button
    const float copyX = contentX;
    const bool copyHov = (mouseX >= copyX && mouseX <= copyX + copyW && mouseY >= curY && mouseY <= curY + btnRowH);
    const char* copyLbl = (copyFeedbackTimer_ > 0.0f) ? "COPIED!" : "COPY";
    clockwork_ui::drawBrassButton(r, copyX, curY, copyW, btnRowH, 4.0f, copyLbl, copyHov, copyFeedbackTimer_ > 0.0f, 11.0f);

    // 2. Copy Replay Code Button (bit-packed moves + notes)
    const float replayX = copyX + copyW + btnGap;
    const bool replayHov = (mouseX >= replayX && mouseX <= replayX + replayW && mouseY >= curY && mouseY <= curY + btnRowH);
    const char* replayLbl = (replayCopyFeedbackTimer_ > 0.0f) ? "COPIED!" : "REPLAY";
    clockwork_ui::drawBrassButton(r, replayX, curY, replayW, btnRowH, 4.0f, replayLbl, replayHov, replayCopyFeedbackTimer_ > 0.0f, 11.0f);

    // 3. Paste Button (auto-detects numeric seed OR sol1: replay code)
    const float pasteX = replayX + replayW + btnGap;
    const bool pasteHov = (mouseX >= pasteX && mouseX <= pasteX + pasteW && mouseY >= curY && mouseY <= curY + btnRowH);
    clockwork_ui::drawBrassButton(r, pasteX, curY, pasteW, btnRowH, 4.0f, "PASTE", pasteHov, false, 11.0f);

    // 4. Randomize Button
    const float randX = pasteX + pasteW + btnGap;
    const bool randHov = (mouseX >= randX && mouseX <= randX + randW && mouseY >= curY && mouseY <= curY + btnRowH);
    clockwork_ui::drawBrassButton(r, randX, curY, randW, btnRowH, 4.0f, "RANDOM", randHov, false, 11.0f);

    curY += btnRowH;

    // --- Optional Procedural Layout Lock Row ---
    if (supportsLayoutLock_) {
        curY += lockRowGap;
        const bool isLocked = (layoutSeed_ != 0u);
        const float lockW = (contentW - btnGap) * 0.65f;
        const float newLayW = contentW - lockW - btnGap;
        const float lockX = contentX;
        const float newLayX = lockX + lockW + btnGap;
        const bool lockHov = (mouseX >= lockX && mouseX <= lockX + lockW && mouseY >= curY && mouseY <= curY + lockRowH);
        const bool newLayHov = (mouseX >= newLayX && mouseX <= newLayX + newLayW && mouseY >= curY && mouseY <= curY + lockRowH);

        std::string lockLbl = isLocked
            ? ("LAYOUT LOCKED (#" + std::to_string(layoutSeed_) + ")")
            : "LOCK CURRENT LAYOUT";
        if (isLocked) {
            clockwork_ui::drawFurnaceButton(r, lockX, curY, lockW, lockRowH, 4.0f, lockLbl.c_str(), lockHov, true, 10.5f);
        } else {
            clockwork_ui::drawPewterButton(r, lockX, curY, lockW, lockRowH, 4.0f, lockLbl.c_str(), lockHov, false, 10.5f);
        }
        clockwork_ui::drawBrassButton(r, newLayX, curY, newLayW, lockRowH, 4.0f, "NEW LAYOUT", newLayHov, false, 10.5f);
        curY += lockRowH;
    }

    curY += 16.0f;

    // --- Session Seed History Section ---
    if (visibleHistoryRows_ > 0) {
        r.drawLine(contentX, curY, contentX + contentW, curY,
                   cPal.brassDarkR, cPal.brassDarkG, cPal.brassDarkB, 0.55f, 1.0f);
        curY += 10.0f;

        // History Header + Optional Up/Down Scroll Controls
        std::string histTitle = "SESSION HISTORY (" + std::to_string(history_.size()) + ")";
        eatsbits::ui::glyph::drawVectorText(
            r, histTitle.c_str(), contentX, curY + 4.0f, 10.5f,
            cPal.dialParchmentR, cPal.dialParchmentG, cPal.dialParchmentB, 0.88f
        );

        const int totalHist = static_cast<int>(history_.size());
        const float stride = histRowH + histRowGap;
        const float rowsH = static_cast<float>(visibleHistoryRows_) * histRowH
                          + static_cast<float>(std::max(0, visibleHistoryRows_ - 1)) * histRowGap;
        const float totalHistH = static_cast<float>(totalHist) * histRowH
                               + static_cast<float>(std::max(0, totalHist - 1)) * histRowGap;
        historyScroller_.setBounds(totalHistH, rowsH);
        const float scrollY = historyScroller_.offset();
        const int firstVisIdx = std::clamp(static_cast<int>(std::floor((historyScroller_.clampedOffset() + stride * 0.45f) / stride)),
                                           0, std::max(0, totalHist - visibleHistoryRows_));

        if (totalHist > visibleHistoryRows_) {
            const float navW = 24.0f;
            const float navH = 20.0f;
            const float dnX = contentX + contentW - navW;
            const float upX = dnX - navW - 4.0f;
            const bool canUp = historyScroller_.clampedOffset() > 1.0f;
            const bool canDn = historyScroller_.clampedOffset() < historyScroller_.maxOffset() - 1.0f;
            const bool upHov = canUp && (mouseX >= upX && mouseX <= upX + navW && mouseY >= curY && mouseY <= curY + navH);
            const bool dnHov = canDn && (mouseX >= dnX && mouseX <= dnX + navW && mouseY >= curY && mouseY <= curY + navH);

            clockwork_ui::drawPewterButton(r, upX, curY, navW, navH, 3.0f, "^", upHov, false, 11.0f);
            clockwork_ui::drawPewterButton(r, dnX, curY, navW, navH, 3.0f, "v", dnHov, false, 11.0f);

            std::string rangeStr = std::to_string(firstVisIdx + 1) + "-" +
                                   std::to_string(std::min(totalHist, firstVisIdx + visibleHistoryRows_)) +
                                   "/" + std::to_string(totalHist);
            const float rangeW = eatsbits::ui::glyph::FontEngine::instance().measureText(rangeStr, 9.5f);
            eatsbits::ui::glyph::drawVectorText(
                r, rangeStr.c_str(), upX - rangeW - 8.0f, curY + 5.0f, 9.5f,
                cPal.brassBrightR, cPal.brassBrightG, cPal.brassBrightB, 0.72f
            );
        }

        curY += histHeaderH + 4.0f;

        const bool hasHistSb = (historyScroller_.maxOffset() > 1.0f);
        const float rowListW = contentW - (hasHistSb ? 10.0f : 0.0f);
        histViewX_ = contentX;
        histViewY_ = curY;
        histViewW_ = contentW;
        histViewH_ = rowsH;

        const float playBtnW = 54.0f;
        const float playBtnH = 22.0f;
        const float playBtnPad = 6.0f;

        {
            auto scissor = r.makeScissorScope(contentX - 1.0f, curY - 1.0f, contentW + 2.0f, rowsH + 2.0f);
            const bool inView = (mouseX >= contentX && mouseX <= contentX + rowListW &&
                                 mouseY >= curY && mouseY <= curY + rowsH);

            for (int idx = 0; idx < totalHist; ++idx) {
                const auto& entry = history_[static_cast<size_t>(idx)];
                const float rowY = curY + static_cast<float>(idx) * stride - scrollY;
                if (rowY + histRowH < curY - 2.0f || rowY > curY + rowsH + 2.0f) continue;

                const bool isSelected = (entry.seed == seed_ && entry.gameId == targetGameId_);
                const bool isCurrent = (entry.seed == activeBoardSeed_ && entry.gameId == activeBoardGameId_);
                const float playX = contentX + rowListW - playBtnW - playBtnPad;
                const float playY = rowY + (histRowH - playBtnH) * 0.5f;
                const bool playHov = !historyScroller_.isDragging() && inView &&
                                     (mouseX >= playX && mouseX <= playX + playBtnW &&
                                      mouseY >= playY && mouseY <= playY + playBtnH);
                const bool rowHov = !historyScroller_.isDragging() && inView && !playHov &&
                                    (mouseX >= contentX && mouseX <= contentX + rowListW &&
                                     mouseY >= rowY && mouseY <= rowY + histRowH);

                // Row background plate
                const float bgBoost = isSelected ? 0.05f : (rowHov ? 0.03f : 0.0f);
                r.drawRoundedRect(
                    contentX, rowY, rowListW, histRowH, 4.0f,
                    0.08f + bgBoost, 0.07f + bgBoost * 0.8f, 0.06f + bgBoost * 0.5f, 0.94f, 4
                );
                r.drawRoundedRectOutline(
                    contentX, rowY, rowListW, histRowH, 4.0f,
                    isSelected ? cPal.furnaceAmberR : (rowHov ? cPal.brassBrightR : cPal.brassDarkR),
                    isSelected ? cPal.furnaceAmberG : (rowHov ? cPal.brassBrightG : cPal.brassDarkG),
                    isSelected ? cPal.furnaceAmberB : (rowHov ? cPal.brassBrightB : cPal.brassDarkB),
                    isSelected ? 0.92f : (rowHov ? 0.80f : 0.45f),
                    isSelected ? 1.3f : 1.0f, 4
                );

                // Active deal indicator dot
                float textX = contentX + 10.0f;
                if (isCurrent) {
                    r.drawCircle(contentX + 10.0f, rowY + histRowH * 0.5f, 3.2f,
                                 cPal.furnaceAmberR, cPal.furnaceAmberG, cPal.furnaceAmberB, 0.95f, 10);
                    textX = contentX + 19.0f;
                }

                // Seed number
                std::string seedLbl = "#" + std::to_string(entry.seed);
                if (entry.layoutSeed != 0u) {
                    seedLbl += ":" + std::to_string(entry.layoutSeed);
                }
                eatsbits::ui::glyph::drawVectorText(
                    r, seedLbl.c_str(), textX, rowY + 9.0f, 11.5f,
                    cPal.brassHighlightR, cPal.brassHighlightG, cPal.brassHighlightB, 1.0f
                );

                // Game name / ID reference (truncated to fit cleanly before PLAY button)
                const float seedLblW = eatsbits::ui::glyph::FontEngine::instance().measureText(seedLbl, 11.5f);
                const float gameTextX = textX + std::max(88.0f, seedLblW + 10.0f);
                const float maxGameW = (playX - 8.0f) - gameTextX;
                if (maxGameW > 24.0f) {
                    std::string gLabel = !entry.gameName.empty() ? entry.gameName : entry.gameId;
                    auto& fe = eatsbits::ui::glyph::FontEngine::instance();
                    if (fe.measureText(gLabel, 10.5f) > maxGameW) {
                        while (gLabel.size() > 3 && fe.measureText(gLabel + "...", 10.5f) > maxGameW) {
                            gLabel.pop_back();
                        }
                        gLabel += "...";
                    }
                    eatsbits::ui::glyph::drawVectorText(
                        r, gLabel.c_str(), gameTextX, rowY + 9.5f, 10.5f,
                        cPal.furnaceAmberR, cPal.furnaceAmberG, cPal.furnaceAmberB, 0.88f
                    );
                }

                // Compact 1-tap PLAY button
                clockwork_ui::drawBrassButton(r, playX, playY, playBtnW, playBtnH, 3.5f, "PLAY", playHov, false, 10.0f);
            }
        }

        // Subtle iOS-style scrollbar indicator
        if (hasHistSb) {
            const float sbX = contentX + contentW - 5.0f;
            const float sbTrackY = curY + 2.0f;
            const float sbTrackH = std::max(20.0f, rowsH - 4.0f);
            float thumbY = sbTrackY;
            float thumbH = 20.0f;
            historyScroller_.computeThumb(sbTrackY, sbTrackH, thumbY, thumbH);
            const float activeAlpha = std::max(0.32f, historyScroller_.indicatorAlpha());
            r.drawRoundedRect(sbX, sbTrackY, 4.0f, sbTrackH, 2.0f, 0.08f, 0.07f, 0.06f, 0.45f * activeAlpha, 2);
            r.drawRoundedRect(sbX, thumbY, 4.0f, thumbH, 2.0f,
                              cPal.brassBrightR, cPal.brassBrightG, cPal.brassBrightB, 0.85f * activeAlpha, 2);
        }

        curY += rowsH + 16.0f;
    } else {
        curY += 8.0f;
    }

    // --- Bottom Row: [ CANCEL ] and [ DEAL NEW GAME ] ---
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
    if (button != 0) return true;

    if (action == 0) {
        if (historyScroller_.isPointerDown()) {
            const bool suppressClick = historyScroller_.endDrag();
            const int prevIdx = pressedHistoryIdx_;
            const bool wasPlay = pressedHistoryPlay_;
            pressedHistoryIdx_ = -1;
            pressedHistoryPlay_ = false;
            if (!suppressClick && prevIdx >= 0 && prevIdx < static_cast<int>(history_.size())) {
                const auto entry = history_[static_cast<size_t>(prevIdx)];
                if (wasPlay) {
                    commitHistoryReplay(entry.seed, entry.layoutSeed, entry.gameId, entry.gameName, board, foley);
                } else {
                    seed_ = entry.seed;
                    layoutSeed_ = entry.layoutSeed;
                    seedText_ = formatSeedText();
                    if (!entry.gameId.empty()) targetGameId_ = entry.gameId;
                    if (!entry.gameName.empty()) variantName_ = entry.gameName;
                    editingSeed_ = true;
                    foley.triggerSound(FoleySound::WoodClick, 0.65f);
                }
            }
        }
        return true;
    }

    if (action != 1) return true; // Absorb all clicks inside modal backdrop

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

    float curY = dialogY_ + headerH + 20.0f + 18.0f;
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
            seedText_ = formatSeedText();
            foley.triggerSound(FoleySound::WoodClick, 0.6f);
        }
        return true;
    }

    // Step Up [+]
    const float incX = decX + stepBtnW + stepBtnGap;
    if (mouseX >= incX && mouseX <= incX + stepBtnW && mouseY >= curY && mouseY <= curY + inputH) {
        ++seed_;
        seedText_ = formatSeedText();
        foley.triggerSound(FoleySound::WoodClick, 0.6f);
        return true;
    }

    curY += inputH + 14.0f;
    const float btnRowH = 34.0f;
    const float btnGap = 6.0f;
    const float copyW = (contentW - btnGap * 3.0f) * 0.25f;
    const float replayW = copyW;
    const float pasteW = copyW;
    const float randW = contentW - copyW - replayW - pasteW - btnGap * 3.0f;

    // 1. Copy Seed Button
    const float copyX = contentX;
    if (mouseX >= copyX && mouseX <= copyX + copyW && mouseY >= curY && mouseY <= curY + btnRowH) {
        copyToClipboard(nativeWindow);
        foley.triggerSound(FoleySound::WoodClick, 0.7f);
        return true;
    }

    // 2. Copy Replay Code Button
    const float replayX = copyX + copyW + btnGap;
    if (mouseX >= replayX && mouseX <= replayX + replayW && mouseY >= curY && mouseY <= curY + btnRowH) {
        copyReplayToClipboard(board, nativeWindow);
        foley.triggerSound(FoleySound::WoodClick, 0.75f);
        return true;
    }

    // 3. Paste Button (seed or sol1: replay code)
    const float pasteX = replayX + replayW + btnGap;
    if (mouseX >= pasteX && mouseX <= pasteX + pasteW && mouseY >= curY && mouseY <= curY + btnRowH) {
        if (!pasteFromClipboard(nativeWindow, board, foley)) {
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
        } else if (isOpen_) {
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
        }
        return true;
    }

    // 4. Randomize Button
    const float randX = pasteX + pasteW + btnGap;
    if (mouseX >= randX && mouseX <= randX + randW && mouseY >= curY && mouseY <= curY + btnRowH) {
        randomizeSeed();
        foley.triggerSound(FoleySound::Shuffle, 0.8f);
        return true;
    }

    curY += btnRowH;

    // Optional Procedural Layout Lock Row hit testing
    if (supportsLayoutLock_) {
        const float lockRowH = 32.0f;
        const float lockRowGap = 10.0f;
        curY += lockRowGap;
        const float lockW = (contentW - btnGap) * 0.65f;
        const float newLayW = contentW - lockW - btnGap;
        const float lockX = contentX;
        const float newLayX = lockX + lockW + btnGap;

        if (mouseX >= lockX && mouseX <= lockX + lockW && mouseY >= curY && mouseY <= curY + lockRowH) {
            if (layoutSeed_ != 0u) {
                layoutSeed_ = 0u;
            } else {
                layoutSeed_ = (seed_ != 0u) ? seed_ : 2026u;
            }
            seedText_ = formatSeedText();
            foley.triggerSound(FoleySound::WoodClick, 0.75f);
            return true;
        }
        if (mouseX >= newLayX && mouseX <= newLayX + newLayW && mouseY >= curY && mouseY <= curY + lockRowH) {
            layoutSeed_ = generateRandomSeed();
            seedText_ = formatSeedText();
            foley.triggerSound(FoleySound::Shuffle, 0.8f);
            return true;
        }
        curY += lockRowH;
    }

    curY += 16.0f;

    // Session Seed History hit testing
    if (visibleHistoryRows_ > 0) {
        const float histHeaderH = 22.0f;
        const float histRowH = 30.0f;
        const float histRowGap = 4.0f;
        const int totalHist = static_cast<int>(history_.size());

        curY += 10.0f;

        if (totalHist > visibleHistoryRows_) {
            const float navW = 24.0f;
            const float navH = 20.0f;
            const float dnX = contentX + contentW - navW;
            const float upX = dnX - navW - 4.0f;
            const float stride = histRowH + histRowGap;

            if (mouseX >= upX && mouseX <= upX + navW && mouseY >= curY && mouseY <= curY + navH) {
                if (historyScroller_.clampedOffset() > 1.0f) {
                    historyScroller_.scrollTo(historyScroller_.clampedOffset() - stride, true);
                    foley.triggerSound(FoleySound::WoodClick, 0.55f);
                }
                return true;
            }
            if (mouseX >= dnX && mouseX <= dnX + navW && mouseY >= curY && mouseY <= curY + navH) {
                if (historyScroller_.clampedOffset() < historyScroller_.maxOffset() - 1.0f) {
                    historyScroller_.scrollTo(historyScroller_.clampedOffset() + stride, true);
                    foley.triggerSound(FoleySound::WoodClick, 0.55f);
                }
                return true;
            }
        }

        curY += histHeaderH + 4.0f;

        const float stride = histRowH + histRowGap;
        const float rowsH = static_cast<float>(visibleHistoryRows_) * histRowH
                          + static_cast<float>(std::max(0, visibleHistoryRows_ - 1)) * histRowGap;
        const bool hasHistSb = (historyScroller_.maxOffset() > 1.0f);
        const float rowListW = contentW - (hasHistSb ? 10.0f : 0.0f);
        const float playBtnW = 54.0f;
        const float playBtnH = 22.0f;
        const float playBtnPad = 6.0f;

        if (mouseX >= contentX && mouseX <= contentX + contentW &&
            mouseY >= curY && mouseY <= curY + rowsH) {
            const bool caught = historyScroller_.beginDrag(mouseX, mouseY);
            pressedHistoryIdx_ = -1;
            pressedHistoryPlay_ = false;
            if (!caught) {
                const float scrollY = historyScroller_.offset();
                for (int idx = 0; idx < totalHist; ++idx) {
                    const float rowY = curY + static_cast<float>(idx) * stride - scrollY;
                    if (mouseY >= rowY && mouseY <= rowY + histRowH &&
                        mouseX >= contentX && mouseX <= contentX + rowListW) {
                        pressedHistoryIdx_ = idx;
                        const float playX = contentX + rowListW - playBtnW - playBtnPad;
                        const float playY = rowY + (histRowH - playBtnH) * 0.5f;
                        pressedHistoryPlay_ = (mouseX >= playX && mouseX <= playX + playBtnW &&
                                               mouseY >= playY && mouseY <= playY + playBtnH);
                        break;
                    }
                }
            }
            return true;
        }

        curY += rowsH + 16.0f;
    } else {
        curY += 8.0f;
    }

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

bool SeedDialog::handleMouseMove(float mouseX, float mouseY) {
    if (!isOpen_) return false;
    if (historyScroller_.isPointerDown()) {
        if (historyScroller_.moveDrag(mouseX, mouseY)) {
            pressedHistoryIdx_ = -1;
            pressedHistoryPlay_ = false;
        }
    }
    return true;
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

    // Ctrl+V = Paste (seed or sol1: replay code)
    if ((key == 86 || key == 'V') && (mods & 2)) {
        if (pasteFromClipboard(nativeWindow, board, foley) && isOpen_) {
            foley.triggerSound(FoleySound::WoodClick, 0.6f);
        }
        return true;
    }

    // Backspace
    if (key == 259 && editingSeed_) {
        if (!seedText_.empty()) {
            seedText_.pop_back();
            uint32_t ps = 0u;
            uint32_t pl = 0u;
            if (parseSeedText(seedText_, ps, pl)) {
                seed_ = ps;
                layoutSeed_ = pl;
            } else {
                seed_ = 0u;
                layoutSeed_ = 0u;
            }
            foley.triggerSound(FoleySound::WoodClick, 0.5f);
        }
        return true;
    }

    return true;
}

bool SeedDialog::handleChar(unsigned int codepoint) {
    if (!isOpen_ || !editingSeed_) return false;

    const bool isDigit = (codepoint >= '0' && codepoint <= '9');
    const bool isColon = (codepoint == ':' && supportsLayoutLock_ && seedText_.find(':') == std::string::npos && !seedText_.empty());
    if ((isDigit || isColon) && seedText_.size() < 21) {
        seedText_.push_back(static_cast<char>(codepoint));
        uint32_t ps = 0u;
        uint32_t pl = 0u;
        if (parseSeedText(seedText_, ps, pl)) {
            seed_ = ps;
            layoutSeed_ = pl;
        }
        return true;
    }

    return true;
}

} // namespace solengine

