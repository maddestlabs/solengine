#include "solengine/table_renderer.hpp"
#include "solengine/auto_play_controller.hpp"
#include "solengine/dynamic_light.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/classic_deck_symbols.hpp"
#include "solengine/procedural_background.hpp"
#include "solengine/postfx_system.hpp"
#include "solengine/settings_manager.hpp"
#include "solengine/session_manager.hpp"
#include "solengine/ui/svg_icon_renderer.hpp"
#include "solengine/ui/settings_dialog.hpp"
#include "solengine/ui/seed_dialog.hpp"
#include "solengine/ui/loading_dialog.hpp"
#include "solengine/ui/game_browser_dialog.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "solengine/ui/app_icon_data.hpp"
#include "solengine/game_definition.hpp"
#include "solengine/eats_package_loader.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include "stb_image.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <random>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#if !defined(__EMSCRIPTEN__)
#include <winhttp.h>
#endif
#include <io.h>
#include <fcntl.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLFW/glfw3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#else
#include <GLFW/glfw3.h>
#endif

namespace solengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.5707963267948966f;
using eatsbits::ui::Point2D;

enum HudButtonId : int {
    BtnNone       = -1,
    BtnNewDeal    = 0,
    BtnRestart    = 1,
    BtnVariant    = 2,
    BtnTheme      = 3,
    BtnDeck       = 4,
    BtnUndo       = 5,
    BtnRedo       = 6,
    BtnHint       = 7,
    BtnMenuToggle = 9,
    BtnMenuAnchor = 10,
    BtnCardWear   = 11,
    BtnCardStock  = 12,
    BtnCardCorners= 13,
    BtnSettings   = 14,
    BtnDeviation  = 15,
    BtnVariantPrev= 16,
    BtnVariantNext= 17,
    BtnRollSeed   = 18,
    BtnAutoPlay   = 19,
    // Transport controls
    BtnPlayRestart= 20,
    BtnStepBack   = 21,
    BtnPlayPause  = 22,
    BtnStepFwd    = 23,
    BtnSpeedToggle= 24,
    BtnTakeOver   = 25,
    BtnSolvableDeal=26,
    BtnSeedDialog = 28,
    BtnToolbarResize = 29,
    BtnHelp       = 30,
    BtnGameTitlePill = 31
};

struct HudButtonRect {
    int id;
    float x;
    float y;
    float w;
    float h;
    const char* label;
    const char* glyph{""};
    SvgIconId iconId{SvgIconId::None};
};

struct ClockworkBarLayout {
    float barX{0.0f};
    float barY{0.0f};
    float barW{0.0f};
    float barH{0.0f};
    bool isVertical{false};
    std::vector<HudButtonRect> buttons;
};

ClockworkBarLayout computeClockworkBarLayout(
    float width,
    float height,
    const SolitaireBoard& board,
    MenuAnchor anchor = MenuAnchor::BottomLeft,
    bool isVertical = false
) {
    const auto& s = SettingsManager::instance().settings();
    float orbSize = 38.0f;
    float orbGap = 5.0f;
    float padX = 8.0f;
    float padY = 6.0f;
    constexpr float margin = 14.0f;

    if (s.toolbarScale == ToolbarScale::Compact) {
        orbSize = 30.0f;
        orbGap = 4.0f;
        padX = 6.0f;
        padY = 5.0f;
    } else if (s.toolbarScale == ToolbarScale::Large) {
        orbSize = 46.0f;
        orbGap = 6.0f;
        padX = 10.0f;
        padY = 7.0f;
    }

    struct BtnDef {
        HudButtonId id;
        const char* label;
        const char* glyph;
    };
    std::vector<BtnDef> sDefs;
    sDefs.reserve(12);

    const uint32_t flags = s.toolbarItems;

    // Master orb always first
    if (flags & ToolbarItem_Settings) {
        sDefs.push_back({ BtnMenuToggle,   "[O] Settings / Drag",     "S" });
    }
    if (flags & ToolbarItem_Play) {
        sDefs.push_back({ BtnNewDeal,      "[N]ew Game (Hold for Options)", "▶" });
    }
    if (flags & ToolbarItem_Undo) {
        sDefs.push_back({ BtnUndo,         "[z]Undo",                 "<" });
    }
    if (flags & ToolbarItem_Redo) {
        sDefs.push_back({ BtnRedo,         "[y]Redo",                 ">" });
    }
    if (flags & ToolbarItem_AutoPlay) {
        sDefs.push_back({ BtnAutoPlay,     "[A]uto-Play",             ">>" });
    }
    if (flags & ToolbarItem_Restart) {
        sDefs.push_back({ BtnRestart,      "[R]estart",               "↺" });
    }
    if (flags & ToolbarItem_Solvable) {
        sDefs.push_back({ BtnSolvableDeal, "[Shift+N] Solvable Deal", "*" });
    }
    if (flags & ToolbarItem_Seed) {
        sDefs.push_back({ BtnSeedDialog,   "[#] Deal Seed",           "#" });
    }
    if (flags & ToolbarItem_GameSelect) {
        sDefs.push_back({ BtnVariant,      "[G]ame Library",           "G" });
    }
    // Always place Help (?) last before Scale/Configure
    sDefs.push_back({ BtnHelp,             "[F1] Help & Rules",       "?" });
    if (flags & ToolbarItem_Resize) {
        sDefs.push_back({ BtnToolbarResize,"[Scale] Tap: Size / Hold: Config", "⤢" });
    }

    if (sDefs.empty()) {
        sDefs.push_back({ BtnMenuToggle, "[O] Settings / Drag", "S" });
    }

    const int kNumButtons = static_cast<int>(sDefs.size());

    ClockworkBarLayout layout;
    layout.isVertical = isVertical;

    if (!isVertical) {
        layout.barW = static_cast<float>(kNumButtons) * orbSize + static_cast<float>(kNumButtons - 1) * orbGap + 2.0f * padX;
        layout.barH = orbSize + 2.0f * padY;
    } else {
        layout.barW = orbSize + 2.0f * padX;
        layout.barH = static_cast<float>(kNumButtons) * orbSize + static_cast<float>(kNumButtons - 1) * orbGap + 2.0f * padY;
    }

    const float insetLeft = board.safeLeft() * 0.45f;
    const float insetRight = board.safeRight() * 0.45f;
    const float insetTop = board.safeTop() * 0.75f;
    const float insetBottom = board.safeBottom() * 0.55f;

    const float marginLeft = std::max(margin, insetLeft);
    const float marginRight = std::max(margin, insetRight);
    const float marginTop = std::max(margin, insetTop);
    const float marginBottom = std::max(margin, insetBottom);

    if (anchor == MenuAnchor::BottomLeft) {
        layout.barX = marginLeft;
        layout.barY = height - marginBottom - layout.barH;
    } else if (anchor == MenuAnchor::BottomRight) {
        layout.barX = width - marginRight - layout.barW;
        layout.barY = height - marginBottom - layout.barH;
    } else if (anchor == MenuAnchor::TopLeft) {
        layout.barX = marginLeft;
        layout.barY = marginTop;
    } else if (anchor == MenuAnchor::TopRight) {
        layout.barX = width - marginRight - layout.barW;
        layout.barY = marginTop;
    }

    // Ergonometric clearance: avoid overlapping bottom stock/waste piles if present
    if ((board.variant() == GameVariant::KlondikeDraw1 || board.variant() == GameVariant::KlondikeDraw3) && !board.piles().empty()) {
        if (board.piles()[0].y > height * 0.5f) {
            const bool stockOnRight = (board.piles()[0].x > width * 0.5f);
            if (!isVertical) {
                if (anchor == MenuAnchor::BottomRight && stockOnRight) {
                    layout.barY = std::max(6.0f + insetTop, board.piles()[0].y - layout.barH - 8.0f);
                } else if (anchor == MenuAnchor::BottomLeft && !stockOnRight) {
                    layout.barY = std::max(6.0f + insetTop, board.piles()[0].y - layout.barH - 8.0f);
                }
            }
        }
    }

    const float minBarX = std::max(4.0f, insetLeft);
    const float maxBarX = std::max(minBarX, width - layout.barW - std::max(4.0f, insetRight));
    const float minBarY = std::max(4.0f, insetTop);
    const float maxBarY = std::max(minBarY, height - layout.barH - std::max(4.0f, insetBottom));
    layout.barX = std::clamp(layout.barX, minBarX, maxBarX);
    layout.barY = std::clamp(layout.barY, minBarY, maxBarY);

    layout.buttons.reserve(kNumButtons);
    for (int i = 0; i < kNumButtons; ++i) {
        float btnX = 0.0f;
        float btnY = 0.0f;

        if (!isVertical) {
            btnY = layout.barY + padY;
            if (anchor == MenuAnchor::BottomLeft || anchor == MenuAnchor::TopLeft) {
                btnX = layout.barX + padX + static_cast<float>(i) * (orbSize + orbGap);
            } else {
                btnX = layout.barX + layout.barW - padX - orbSize - static_cast<float>(i) * (orbSize + orbGap);
            }
        } else {
            btnX = layout.barX + padX;
            if (anchor == MenuAnchor::TopLeft || anchor == MenuAnchor::TopRight) {
                btnY = layout.barY + padY + static_cast<float>(i) * (orbSize + orbGap);
            } else {
                btnY = layout.barY + layout.barH - padY - orbSize - static_cast<float>(i) * (orbSize + orbGap);
            }
        }

        layout.buttons.push_back(HudButtonRect{
            sDefs[i].id,
            btnX,
            btnY,
            orbSize,
            orbSize,
            sDefs[i].label,
            sDefs[i].glyph,
            SvgIconId::None
        });
    }

    // 1-tap Game Title Pill anchored directly above (or below, for top anchors) the toolbar
    {
        float pillH = 26.0f;
        float fontSz = 11.0f;
        float pillPadX = 10.0f;
        float gapY = 5.0f;
        if (s.toolbarScale == ToolbarScale::Compact) {
            pillH = 22.0f;
            fontSz = 9.5f;
            pillPadX = 8.0f;
            gapY = 4.0f;
        } else if (s.toolbarScale == ToolbarScale::Large) {
            pillH = 30.0f;
            fontSz = 12.5f;
            pillPadX = 12.0f;
            gapY = 6.0f;
        }

        std::string gameTitle = board.activeGameDef().name;
        if (gameTitle.empty()) {
            if (const auto* def = GameRegistry::instance().findByVariant(board.variant())) {
                gameTitle = def->name;
            }
        }
        if (gameTitle.empty()) gameTitle = "Solitaire";

        const float textW = eatsbits::ui::glyph::getVectorTextWidth(fontSz, gameTitle.c_str());
        const float iconSize = fontSz + 2.5f;
        const float iconSpace = iconSize + 5.0f;
        const float chevronSpace = 14.0f;
        const float maxPillW = std::min(std::max(layout.barW, 180.0f), std::max(80.0f, width - 28.0f));
        const float pillW = std::clamp(textW + pillPadX * 2.0f + iconSpace + chevronSpace, 86.0f, maxPillW);

        const bool isRight = (anchor == MenuAnchor::BottomRight || anchor == MenuAnchor::TopRight);
        const bool isTop = (anchor == MenuAnchor::TopLeft || anchor == MenuAnchor::TopRight);

        float pillX = isRight ? (layout.barX + layout.barW - padX - pillW) : (layout.barX + padX);
        float pillY = isTop ? (layout.barY + layout.barH - padY + gapY) : (layout.barY + padY - gapY - pillH);

        const float minPillX = std::max(4.0f, insetLeft);
        const float maxPillX = std::max(minPillX, width - pillW - std::max(4.0f, insetRight));
        const float minPillY = std::max(4.0f, insetTop);
        const float maxPillY = std::max(minPillY, height - pillH - std::max(4.0f, insetBottom));
        pillX = std::clamp(pillX, minPillX, maxPillX);
        pillY = std::clamp(pillY, minPillY, maxPillY);

        layout.buttons.push_back(HudButtonRect{
            BtnGameTitlePill,
            pillX,
            pillY,
            pillW,
            pillH,
            "[G]ame Library",
            "",
            SvgIconId::None
        });
    }

    return layout;
}

std::vector<HudButtonRect> buildHudButtons(
    float width,
    float height,
    const SolitaireBoard& board,
    bool /*menuOpen*/ = false,
    MenuAnchor anchor = MenuAnchor::BottomLeft,
    bool isVertical = false
) {
    return computeClockworkBarLayout(width, height, board, anchor, isVertical).buttons;
}

std::vector<HudButtonRect> buildTransportButtons(
    float width,
    float height,
    const AutoPlayController& controller,
    float safeBottom = 0.0f
) {
    std::vector<HudButtonRect> btns;
    if (!controller.isActive()) {
        return btns;
    }

    constexpr float btnW = 38.0f;
    constexpr float btnH = 32.0f;
    constexpr float gap = 6.0f;
    const float takeOverW = btnW;
    const float totalW = 5.0f * btnW + takeOverW + 5.0f * gap + 24.0f;
    const float barX = (width - totalW) * 0.5f;
    const float barY = height - 52.0f - safeBottom;

    auto makeBtn = [](HudButtonId id, float x, float y, float w, float h, const char* label, SvgIconId iconId) -> HudButtonRect {
        return HudButtonRect{id, x, y, w, h, label, "", iconId};
    };

    float curX = barX + 12.0f;
    float curY = barY + 5.0f;

    // 1. Restart Deal
    btns.push_back(makeBtn(BtnPlayRestart, curX, curY, btnW, btnH, "", SvgIconId::Restart));
    curX += btnW + gap;

    // 2. Step Backward (Rewind)
    btns.push_back(makeBtn(BtnStepBack, curX, curY, btnW, btnH, "", SvgIconId::StepBackward));
    curX += btnW + gap;

    // 3. Play / Pause
    SvgIconId playPauseIcon = controller.isPlaying() ? SvgIconId::Pause : SvgIconId::Play;
    btns.push_back(makeBtn(BtnPlayPause, curX, curY, btnW, btnH, "", playPauseIcon));
    curX += btnW + gap;

    // 4. Step Forward
    btns.push_back(makeBtn(BtnStepFwd, curX, curY, btnW, btnH, "", SvgIconId::StepForward));
    curX += btnW + gap;

    // 5. Speed Toggle (1x, 2x, 4x)
    btns.push_back(makeBtn(BtnSpeedToggle, curX, curY, btnW, btnH, controller.speedLabel(), SvgIconId::None));
    curX += btnW + gap;

    // 6. Stop / Take Over
    btns.push_back(makeBtn(BtnTakeOver, curX, curY, takeOverW, btnH, "", SvgIconId::Stop));

    return btns;
}

int hitTestTransportButtons(
    float mx,
    float my,
    float width,
    float height,
    const AutoPlayController& controller,
    float safeBottom = 0.0f
) {
    if (!controller.isActive()) return BtnNone;
    auto btns = buildTransportButtons(width, height, controller, safeBottom);
    for (const auto& b : btns) {
        if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
            return b.id;
        }
    }
    return BtnNone;
}

int hitTestHudButtons(
    float mx,
    float my,
    float width,
    float height,
    const SolitaireBoard& board,
    MenuAnchor anchor = MenuAnchor::BottomLeft,
    bool isVertical = false
) {
    auto btns = buildHudButtons(width, height, board, true, anchor, isVertical);
    for (const auto& b : btns) {
        if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h) {
            return b.id;
        }
    }
    return BtnNone;
}

bool isPointInsideMenuPopover(
    float mx,
    float my,
    float width,
    float height,
    const SolitaireBoard& board,
    MenuAnchor anchor = MenuAnchor::BottomLeft,
    bool isVertical = false
) {
    auto layout = computeClockworkBarLayout(width, height, board, anchor, isVertical);
    return (mx >= layout.barX && mx <= layout.barX + layout.barW &&
            my >= layout.barY && my <= layout.barY + layout.barH);
}

} // namespace

ThemePalette getPalette([[maybe_unused]] TableThemeId theme) noexcept {
    // Single unified core palette harmonized with the SolEngine ClockworkEngine aesthetic:
    // Cast iron boilerplate chassis (wood), polished Viktoria brass (trim), and molten furnace amber (accent).
    static const ThemePalette kCorePalette{
        0.20f, 0.18f, 0.16f,  // woodR0, woodG0, woodB0: Cast iron / boilerplate chassis
        0.13f, 0.12f, 0.11f,  // woodR1, woodG1, woodB1: Deep seam shadow
        0.94f, 0.80f, 0.36f,  // trimR, trimG, trimB: Polished Viktoria brass
        0.07f, 0.25f, 0.13f,  // feltR0, feltG0, feltB0: Rich tournament baize
        0.03f, 0.14f, 0.07f,  // feltR1, feltG1, feltB1: Deep shadow baize
        0.18f, 0.15f, 0.12f,  // cardBackR0, cardBackG0, cardBackB0: Dark iron cardstock
        0.10f, 0.08f, 0.06f,  // cardBackR1, cardBackG1, cardBackB1
        0.95f, 0.52f, 0.12f   // accentR, accentG, accentB: Molten furnace amber glow
    };
    return kCorePalette;
}

namespace {
const char* rankString(Rank r) noexcept {
    switch (r) {
        case Rank::Ace:   return "A";
        case Rank::Two:   return "2";
        case Rank::Three: return "3";
        case Rank::Four:  return "4";
        case Rank::Five:  return "5";
        case Rank::Six:   return "6";
        case Rank::Seven: return "7";
        case Rank::Eight: return "8";
        case Rank::Nine:  return "9";
        case Rank::Ten:   return "10";
        case Rank::Jack:  return "J";
        case Rank::Queen: return "Q";
        case Rank::King:  return "K";
        default:          return "?";
    }
}

void drawSuitPip(
    eatsbits::ui::BatchRenderer2D& r,
    Suit suit,
    float cx,
    float cy,
    float radiusX,
    float radiusY,
    float red,
    float green,
    float blue,
    float alpha = 1.0f
) {
    drawClassicSuitPip(r, suit, cx, cy, radiusX, radiusY, red, green, blue, alpha);
}

struct Card3DTransform {
    float cx{0.0f};
    float cy{0.0f};
    float cz{0.0f};
    float baseW{0.0f};
    float baseH{0.0f};
    float yaw{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    float thickness{2.4f};
    float fovDist{720.0f};

    Point2D projectLocal(float lx, float ly, float lz) const {
        // 1. Yaw around Y (local 3D card turn-over)
        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);
        const float yx = lx * cosY + lz * sinY;
        const float yy = ly;
        const float yz = -lx * sinY + lz * cosY;

        // 2. Pitch around X (subtle 3D lift tilt)
        const float cosP = std::cos(pitch);
        const float sinP = std::sin(pitch);
        const float px = yx;
        const float py = yy * cosP - yz * sinP;
        const float pz = yy * sinP + yz * cosP;

        // 3. Roll around Z in table plane (matches 2D card angle at both yaw=pi and yaw=0)
        const float cosR = std::cos(roll);
        const float sinR = std::sin(roll);
        const float wx = px * cosR - py * sinR;
        const float wy = px * sinR + py * cosR;
        const float wz = cz + pz;

        // 4. Perspective projection
        const float dist = std::max(60.0f, fovDist - wz);
        const float s = fovDist / dist;

        return Point2D{
            cx + wx * s,
            cy + wy * s
        };
    }

    Point2D projectUV(float u, float v, float zSign = 1.0f) const {
        const float lx = (u - 0.5f) * baseW;
        const float ly = (v - 0.5f) * baseH;
        const float lz = zSign * (thickness * 0.5f);
        return projectLocal(lx, ly, lz);
    }

    float getScaleUV(float u, float v, float zSign = 1.0f) const {
        const float lx = (u - 0.5f) * baseW;
        const float ly = (v - 0.5f) * baseH;
        const float lz = zSign * (thickness * 0.5f);

        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);
        const float yy = ly;
        const float yz = -lx * sinY + lz * cosY;

        const float cosP = std::cos(pitch);
        const float sinP = std::sin(pitch);
        const float pz = yy * sinP + yz * cosP;

        const float wz = cz + pz;
        const float dist = std::max(60.0f, fovDist - wz);
        return fovDist / dist;
    }

    Point2D projectShadow(float u, float v, const eatsbits::ui::LightSource2D& light) const {
        const float lx = (u - 0.5f) * baseW;
        const float ly = (v - 0.5f) * baseH;
        const float lz = 0.0f;

        const float cosY = std::cos(yaw);
        const float sinY = std::sin(yaw);
        const float yx = lx * cosY + lz * sinY;
        const float yy = ly;
        const float yz = -lx * sinY + lz * cosY;

        const float cosP = std::cos(pitch);
        const float sinP = std::sin(pitch);
        const float px = yx;
        const float py = yy * cosP - yz * sinP;
        const float pz = yy * sinP + yz * cosP;

        const float cosR = std::cos(roll);
        const float sinR = std::sin(roll);
        const float wx = px * cosR - py * sinR;
        const float wy = px * sinR + py * cosR;

        const float vx = cx + wx;
        const float vy = cy + wy;
        // Total elevation off table plane: base elevation cz + 3D tilt pz + ground clearance (2.5px)
        const float wz = std::max(0.0f, cz + pz) + 2.5f;

        // Perspective shadow projection from light (light.x, light.y, light.z) to table plane (z=0)
        const float lightZ = std::max(100.0f, light.z);
        const float t = wz / std::max(50.0f, lightZ - wz);
        const float sOffX = (vx - light.x) * t;
        const float sOffY = (vy - light.y) * t;

        return Point2D{
            vx + sOffX,
            vy + sOffY
        };
    }

    float frontArea() const {
        Point2D p0 = projectUV(0.0f, 0.0f, 1.0f);
        Point2D p1 = projectUV(1.0f, 0.0f, 1.0f);
        Point2D p2 = projectUV(1.0f, 1.0f, 1.0f);
        return (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
    }

    bool isFrontFacing() const {
        return frontArea() > 0.0f;
    }
};

inline std::array<Point2D, 16> getCardPerimeterUV(float ru = 0.065f, float rv = 0.046f, float marginU = 0.0f, float marginV = 0.0f) {
    const float l = marginU;
    const float r = 1.0f - marginU;
    const float t = marginV;
    const float b = 1.0f - marginV;

    if (ru <= 0.001f || rv <= 0.001f) {
        return {{
            Point2D{r, t}, Point2D{r, t}, Point2D{r, t}, Point2D{r, t},
            Point2D{r, b}, Point2D{r, b}, Point2D{r, b}, Point2D{r, b},
            Point2D{l, b}, Point2D{l, b}, Point2D{l, b}, Point2D{l, b},
            Point2D{l, t}, Point2D{l, t}, Point2D{l, t}, Point2D{l, t}
        }};
    }

    constexpr float kC30 = 0.8660254f;
    constexpr float kS30 = 0.5000000f;

    return {{
        Point2D{r - ru, t},
        Point2D{r - ru + ru * kS30, t + rv - rv * kC30},
        Point2D{r - ru + ru * kC30, t + rv - rv * kS30},
        Point2D{r, t + rv},

        Point2D{r, b - rv},
        Point2D{r - ru + ru * kC30, b - rv + rv * kS30},
        Point2D{r - ru + ru * kS30, b - rv + rv * kC30},
        Point2D{r - ru, b},

        Point2D{l + ru, b},
        Point2D{l + ru - ru * kS30, b - rv + rv * kC30},
        Point2D{l + ru - ru * kC30, b - rv + rv * kS30},
        Point2D{l, b - rv},

        Point2D{l, t + rv},
        Point2D{l + ru - ru * kC30, t + rv - rv * kS30},
        Point2D{l + ru - ru * kS30, t + rv - rv * kC30},
        Point2D{l + ru, t}
    }};
}

inline uint32_t packColorU32(float r, float g, float b, float a = 1.0f) {
    uint32_t ur = static_cast<uint32_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
    uint32_t ug = static_cast<uint32_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
    uint32_t ub = static_cast<uint32_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
    uint32_t ua = static_cast<uint32_t>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
    return ur | (ug << 8) | (ub << 16) | (ua << 24);
}

void draw3DRoundedSurface(
    eatsbits::ui::BatchRenderer2D& r,
    const Card3DTransform& xf,
    const std::array<Point2D, 16>& uvs,
    float zSign,
    float r0, float g0, float b0,
    float r1, float g1, float b1,
    float alpha = 1.0f
) {
    std::array<Point2D, 16> pts;
    for (size_t i = 0; i < 16; ++i) {
        pts[i] = xf.projectUV(uvs[i].x, uvs[i].y, zSign);
    }

    if (std::abs(r0 - r1) < 0.005f && std::abs(g0 - g1) < 0.005f && std::abs(b0 - b1) < 0.005f) {
        r.drawConvexPolygon(pts.data(), 16, r0, g0, b0, alpha);
    } else {
        Point2D gp0 = xf.projectUV(0.5f, uvs[0].y, zSign);
        Point2D gp1 = xf.projectUV(0.5f, uvs[7].y, zSign);
        r.drawConvexPolygonGradient(pts.data(), 16, r0, g0, b0, r1, g1, b1, gp0, gp1, alpha);
    }
}

void draw3DRoundedOutline(
    eatsbits::ui::BatchRenderer2D& r,
    const Card3DTransform& xf,
    const std::array<Point2D, 16>& uvs,
    float zSign,
    float cr, float cg, float cb, float ca = 1.0f, float lineWidth = 1.0f
) {
    std::array<Point2D, 16> pts;
    for (size_t i = 0; i < 16; ++i) {
        pts[i] = xf.projectUV(uvs[i].x, uvs[i].y, zSign);
    }
    for (size_t i = 0; i < 16; ++i) {
        size_t next = (i + 1) % 16;
        r.drawLine(pts[i].x, pts[i].y, pts[next].x, pts[next].y, cr, cg, cb, ca, lineWidth);
    }
}

enum class CardRenderPass : uint8_t {
    ShadowAndBody = 0,
    ShadowOnly = 1,
    BodyOnly = 2
};

void render3DCard(
    eatsbits::ui::BatchRenderer2D& r,
    const Card& c,
    const Card3DTransform& xf,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light,
    CardRenderPass pass = CardRenderPass::ShadowAndBody
) {
    // 16-point perimeter polygons with deck or user-configured corner radius
    const float effRu = CardTextureCache::instance().effectiveRu();
    const float effRv = CardTextureCache::instance().effectiveRv();

    // 1. Dynamic Detached 3D Gradient Shadow with Rounded Corners
    if (pass != CardRenderPass::BodyOnly) {
        Point2D s0 = xf.projectShadow(0.0f, 0.0f, light);
        Point2D s1 = xf.projectShadow(1.0f, 0.0f, light);
        Point2D s2 = xf.projectShadow(1.0f, 1.0f, light);
        Point2D s3 = xf.projectShadow(0.0f, 1.0f, light);

        const float scx = 0.25f * (s0.x + s1.x + s2.x + s3.x);
        const float scy = 0.25f * (s0.y + s1.y + s2.y + s3.y);

        const float ux = 0.25f * ((s1.x - s0.x) + (s2.x - s3.x));
        const float uy = 0.25f * ((s1.y - s0.y) + (s2.y - s3.y));
        const float vx = 0.25f * ((s3.x - s0.x) + (s2.x - s1.x));
        const float vy = 0.25f * ((s3.y - s0.y) + (s2.y - s1.y));

        const float halfWProj = std::max(3.0f, std::hypot(ux, uy));
        const float halfHProj = std::max(3.0f, std::hypot(vx, vy));
        const float invW = 1.0f / halfWProj;
        const float invH = 1.0f / halfHProj;
        const float uHatX = ux * invW;
        const float uHatY = uy * invW;
        const float vHatX = vx * invH;
        const float vHatY = vy * invH;

        const float feather = 5.0f + std::min(18.0f, xf.cz * 0.22f);
        const float totalHalfW = halfWProj + feather;
        const float totalHalfH = halfHProj + feather;

        const float baseCornerRad = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * xf.baseW);
        const float foreshorten = std::min(halfWProj / std::max(1.0f, xf.baseW * 0.5f),
                                           halfHProj / std::max(1.0f, xf.baseH * 0.5f));
        const float shadowCornerRad = baseCornerRad * std::clamp(foreshorten, 0.15f, 1.25f);
        const float sAlpha = std::clamp(0.36f - xf.cz * 0.0022f, 0.14f, 0.36f);

        const float q0x = scx - uHatX * totalHalfW - vHatX * totalHalfH;
        const float q0y = scy - uHatY * totalHalfW - vHatY * totalHalfH;
        const float q1x = scx + uHatX * totalHalfW - vHatX * totalHalfH;
        const float q1y = scy + uHatY * totalHalfW - vHatY * totalHalfH;
        const float q2x = scx + uHatX * totalHalfW + vHatX * totalHalfH;
        const float q2y = scy + uHatY * totalHalfW + vHatY * totalHalfH;
        const float q3x = scx - uHatX * totalHalfW + vHatX * totalHalfH;
        const float q3y = scy - uHatY * totalHalfW + vHatY * totalHalfH;

        r.drawOrientedDropShadowQuad(q0x, q0y, q1x, q1y, q2x, q2y, q3x, q3y,
                                     totalHalfW, totalHalfH, shadowCornerRad, feather, sAlpha);
    }

    if (pass == CardRenderPass::ShadowOnly) {
        return;
    }

    const auto outerUV = getCardPerimeterUV(effRu, effRv, 0.0f, 0.0f);

    // 2. Physical Cardstock Rim along the rounded boundary
    {
        std::array<Point2D, 16> frontPts;
        std::array<Point2D, 16> backPts;
        for (size_t i = 0; i < 16; ++i) {
            frontPts[i] = xf.projectUV(outerUV[i].x, outerUV[i].y, 1.0f);
            backPts[i]  = xf.projectUV(outerUV[i].x, outerUV[i].y, -1.0f);
        }

        for (size_t i = 0; i < 16; ++i) {
            size_t next = (i + 1) % 16;
            Point2D f0 = frontPts[i];
            Point2D f1 = frontPts[next];
            Point2D b1 = backPts[next];
            Point2D b0 = backPts[i];

            float area = (f1.x - f0.x) * (b1.y - f0.y) - (b1.x - f0.x) * (f1.y - f0.y);
            if (area > 0.0f) {
                float lightShade = 0.88f + (outerUV[i].x - 0.5f) * 0.08f;
                float rimR = 0.91f * lightShade;
                float rimG = 0.88f * lightShade;
                float rimB = 0.82f * lightShade;
                r.drawTriangle(f0.x, f0.y, f1.x, f1.y, b1.x, b1.y, rimR, rimG, rimB, 1.0f);
                r.drawTriangle(f0.x, f0.y, b1.x, b1.y, b0.x, b0.y, rimR, rimG, rimB, 1.0f);
            }
        }
    }

    const bool geomFront = xf.isFrontFacing();
    const bool front = c.faceUp && geomFront;
    const float zSign = geomFront ? 1.0f : -1.0f;

    // Smooth directional lighting from light source based on 3D surface normal
    const float cosY = std::cos(xf.yaw);
    const float sinY = std::sin(xf.yaw);
    const float cosP = std::cos(xf.pitch);
    const float sinP = std::sin(xf.pitch);
    const float nx = zSign * sinY;
    const float ny = -zSign * cosY * sinP;
    const float nz = zSign * cosY * cosP;

    const float toLightX = light.x - xf.cx;
    const float toLightY = light.y - xf.cy;
    const float toLightZ = light.z - xf.cz;
    const float invLen = 1.0f / std::max(1.0f, std::sqrt(toLightX * toLightX + toLightY * toLightY + toLightZ * toLightZ));
    const float lx = toLightX * invLen;
    const float ly = toLightY * invLen;
    const float lz = toLightZ * invLen;

    const float nDotL = std::clamp(nx * lx + ny * ly + nz * lz, -0.25f, 1.0f);
    const float cardLight = std::clamp(0.86f + 0.14f * nDotL, 0.72f, 1.04f);

    // 3. Card Surface with Texture Mapping
    const CardTexture* tex = front
        ? CardTextureCache::instance().getCardTexture(c.suit, c.rank)
        : CardTextureCache::instance().getCardBackTexture(c.id);

    Point2D p0 = xf.projectUV(0.0f, 0.0f, zSign);
    Point2D p1 = xf.projectUV(1.0f, 0.0f, zSign);
    Point2D p2 = xf.projectUV(1.0f, 1.0f, zSign);
    Point2D p3 = xf.projectUV(0.0f, 1.0f, zSign);

    float w0 = xf.getScaleUV(0.0f, 0.0f, zSign);
    float w1 = xf.getScaleUV(1.0f, 0.0f, zSign);
    float w2 = xf.getScaleUV(1.0f, 1.0f, zSign);
    float w3 = xf.getScaleUV(0.0f, 1.0f, zSign);

    std::array<Point2D, 16> pts;
    for (size_t i = 0; i < 16; ++i) {
        pts[i] = xf.projectUV(outerUV[i].x, outerUV[i].y, zSign);
    }

    float scratchDistort = 0.0f;
    const auto wearLvl = CardTextureCache::instance().cardWearLevel();
    if (wearLvl == CardWearLevel::Played) scratchDistort = 0.0018f;
    else if (wearLvl == CardWearLevel::Vintage) scratchDistort = 0.0035f;

    const float condParam = static_cast<float>(wearLvl) + 10.0f * static_cast<float>(CardTextureCache::instance().cardStock());

    if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().hasGpuAtlas()) {
        const auto& slot = front
            ? CardTextureCache::instance().getAtlasSlot(c.suit, c.rank)
            : CardTextureCache::instance().getBackAtlasSlot(c.id);

        std::array<Point2D, 16> atlasUVs;
        for (size_t i = 0; i < 16; ++i) {
            atlasUVs[i].x = slot.u0 + outerUV[i].x * (slot.u1 - slot.u0);
            atlasUVs[i].y = slot.v0 + outerUV[i].y * (slot.v1 - slot.v0);
        }
        r.drawTexturedPolygon(pts.data(), atlasUVs.data(), 16, cardLight, cardLight, cardLight, 1.0f, scratchDistort, condParam, outerUV.data());
    } else if (tex && (!tex->argb32.empty() || !tex->rgba.empty())) {
        int mipW = tex->width;
        int mipH = tex->height;
        const uint32_t* mipData = tex->getMipData(xf.baseW, mipW, mipH);
        if (mipData) {
            r.drawTexturedConvexPolygon(pts.data(), 16, mipData, mipW, mipH,
                                        p0, p1, p2, p3, w0, w1, w2, w3, cardLight);
        } else if (!tex->rgba.empty()) {
            r.drawTexturedConvexPolygon(pts.data(), 16, tex->rgba.data(), tex->width, tex->height,
                                        p0, p1, p2, p3, w0, w1, w2, w3, cardLight);
        }
    } else {
        // Fallback to solid surface
        draw3DRoundedSurface(r, xf, outerUV, zSign,
                             0.98f * cardLight, 0.96f * cardLight, 0.90f * cardLight,
                             0.92f * cardLight, 0.88f * cardLight, 0.79f * cardLight, 1.0f);
    }

    if (c.enshadedGlow || c.hasTag("selected")) {
        draw3DRoundedOutline(r, xf, outerUV, zSign, pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f);
    } else if (wearLvl == CardWearLevel::Pristine) {
        draw3DRoundedOutline(r, xf, outerUV, zSign, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f);
    }
}

struct CardPhysicalWarp {
    bool hasWarp{false};
    float bowY{0.0f};       // Vertical camber foreshortening (0.0 to 0.052)
    float bowX{0.0f};       // Horizontal camber foreshortening (0.0 to 0.025)
    float liftZ{0.0f};      // Center/edge elevation off table (0.0 to 5.8 px)
};

inline CardPhysicalWarp computeCardPhysicalWarp(uint8_t cardId, CardWearLevel wear) noexcept {
    CardPhysicalWarp warp{};
    if (wear == CardWearLevel::Pristine) {
        // Pristine: brand new out-of-the-box cards are dead flat
        return warp;
    }

    // Determine odds of this card having physical bend/camber based on Wear setting
    float prob = 0.0f;
    float maxBow = 0.0f;
    float maxLift = 0.0f;
    if (wear == CardWearLevel::Natural) {
        prob = 0.28f;      // ~28% of cards have subtle handling curve
        maxBow = 0.016f;   // subtle ~1-2px foreshortening
        maxLift = 1.8f;
    } else if (wear == CardWearLevel::Played) {
        prob = 0.65f;      // ~65% of cards have noticeable riffle bow/arch
        maxBow = 0.032f;   // ~3-4px foreshortening
        maxLift = 3.6f;
    } else { // Vintage
        prob = 0.88f;      // ~88% of vintage cards are warped/cupped
        maxBow = 0.050f;   // ~5-7px foreshortening
        maxLift = 5.8f;
    }

    // Deterministic hash check per card
    const float roll = (cardHashNoise(0x9E3779B9u, cardId, 801) + 1.0f) * 0.5f; // [0.0, 1.0]
    if (roll > prob) {
        // This card remains flat
        return warp;
    }

    warp.hasWarp = true;
    const float mag = 0.40f + 0.60f * std::abs(cardHashNoise(0x9E3779B9u, cardId, 802));
    warp.bowY = maxBow * mag;
    warp.liftZ = maxLift * mag;

    // A subset (~35% of warped cards) also exhibit slight lateral bow along X
    if (std::abs(cardHashNoise(0x9E3779B9u, cardId, 803)) > 0.65f) {
        warp.bowX = (maxBow * 0.50f) * mag;
    }

    return warp;
}

struct CardScreenRect {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};
    float radius{0.0f};
    float scaleX{1.0f};
    CardPhysicalWarp warp{};
};

inline CardScreenRect computeCardScreenRect(
    const Card& c,
    float baseX,
    float baseY,
    float baseW,
    float baseH,
    CardWearLevel wearLvl
) noexcept {
    CardScreenRect rect{};
    rect.warp = computeCardPhysicalWarp(c.id, wearLvl);

    // Physical card camber foreshortening:
    // Curved cards project a slightly shorter chord on screen without altering the game grid
    const float effBaseW = (rect.warp.bowX > 0.0f) ? (baseW * (1.0f - rect.warp.bowX)) : baseW;
    const float effBaseH = (rect.warp.bowY > 0.0f) ? (baseH * (1.0f - rect.warp.bowY)) : baseH;
    const float baseXOffset = (baseW - effBaseW) * 0.5f;
    const float baseYOffset = (baseH - effBaseH) * 0.5f;

    const float liftScale = 1.0f + c.elevation * 0.0035f;
    const float flipCos = std::cos(c.flipAngle);
    rect.scaleX = std::max(0.06f, std::abs(flipCos)) * liftScale;
    const float scaleY = liftScale;

    rect.w = effBaseW * rect.scaleX;
    rect.h = effBaseH * scaleY;
    rect.x = (baseX + baseXOffset) + (effBaseW - rect.w) * 0.5f;
    rect.y = (baseY + baseYOffset) + (effBaseH - rect.h) * 0.5f;

    const float effRu = CardTextureCache::instance().effectiveRu();
    rect.radius = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * rect.w);
    return rect;
}

// Hardwood 3.0 Scoped 2D Rotation for resting micro-tilts & flight banking
struct ScopedCardRotation {
    eatsbits::ui::BatchRenderer2D& renderer;
    bool active;
    ScopedCardRotation(eatsbits::ui::BatchRenderer2D& rIn, float angleRad, float cx, float cy)
        : renderer(rIn), active(std::abs(angleRad) > 0.0001f) {
        if (active) {
            renderer.setRotation(angleRad * (180.0f / 3.14159265358979323846f), cx, cy);
        }
    }
    ~ScopedCardRotation() {
        if (active) {
            renderer.resetRotation();
        }
    }
};

inline void drawInterCardContactShadow(
    eatsbits::ui::BatchRenderer2D& r,
    const Card& c,
    float baseX,
    float baseY,
    float baseW,
    float baseH,
    const eatsbits::ui::LightSource2D& light
) {
    if (c.flipAngle >= 0.005f) return;
    const CardScreenRect rect = computeCardScreenRect(c, baseX, baseY, baseW, baseH, CardTextureCache::instance().cardWearLevel());
    ScopedCardRotation contactRotGuard(r, c.angle, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);
    r.drawRectDropShadow(rect.x, rect.y, rect.w, rect.h, rect.radius, 1.2f, light, 0.38f);
}

void drawPhysicalDeckRim(
    eatsbits::ui::BatchRenderer2D& r,
    const Card& topCard,
    float baseW, float baseH,
    size_t cardCount,
    const eatsbits::ui::LightSource2D& light
) {
    if (cardCount < 2 || baseW <= 0.0f || baseH <= 0.0f) return;
    (void)light;

    const auto& cache = CardTextureCache::instance();
    const auto wearLvl = cache.cardWearLevel();
    const auto stock = cache.cardStock();
    const CardScreenRect rect = computeCardScreenRect(topCard, topCard.x, topCard.y, baseW, baseH, wearLvl);
    ScopedCardRotation rotGuard(r, topCard.angle, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);

    const float thickness = std::clamp(static_cast<float>(cardCount) * 0.28f, 1.2f, 6.0f);
    // Bevel offset toward bottom-right (matching light direction and slight top-down camera angle)
    const float rimY = thickness * 0.85f;
    const float rimX = thickness * 0.30f;

    // Match cardstock pulp tone to current Wear & Stock settings so the deck edge blends naturally
    float baseR = 0.86f, baseG = 0.84f, baseB = 0.80f;
    switch (wearLvl) {
        case CardWearLevel::Pristine: baseR = 0.86f; baseG = 0.84f; baseB = 0.80f; break;
        case CardWearLevel::Natural:  baseR = 0.81f; baseG = 0.77f; baseB = 0.70f; break;
        case CardWearLevel::Played:   baseR = 0.73f; baseG = 0.67f; baseB = 0.57f; break;
        case CardWearLevel::Vintage:  baseR = 0.61f; baseG = 0.53f; baseB = 0.42f; break;
        default: break;
    }
    if (stock == CardStock::AntiqueLaid || stock == CardStock::CottonRag) {
        baseR *= 0.95f;
        baseG *= 0.94f;
        baseB *= 0.92f;
    } else if (stock == CardStock::SmoothParchment) {
        baseR = std::min(1.0f, baseR * 1.02f);
        baseG *= 0.97f;
        baseB *= 0.89f;
    }

    // 1. Cut paper cardstock rim base
    r.drawRoundedRect(rect.x + rimX, rect.y + rimY, rect.w, rect.h, rect.radius, baseR, baseG, baseB, 1.0f, 4);

    // 2. Bottom creasing shadow line where the deck meets the table
    r.drawRoundedRectOutline(rect.x + rimX, rect.y + rimY, rect.w, rect.h, rect.radius,
                             baseR * 0.32f, baseG * 0.30f, baseB * 0.26f, 0.50f, 1.0f, 4);

    // 3. Subtle micro-striation representing stacked card layers
    if (thickness >= 2.5f) {
        const float midY = rect.y + rimY * 0.5f;
        const float midX = rect.x + rimX * 0.5f;
        r.drawRoundedRectOutline(midX, midY, rect.w, rect.h, rect.radius,
                                 baseR * 0.84f, baseG * 0.82f, baseB * 0.80f, 0.42f, 0.8f, 4);
    }
}

void drawProceduralCard(
    eatsbits::ui::BatchRenderer2D& r,
    const Card& c,
    float baseX,
    float baseY,
    float baseW,
    float baseH,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light,
    CardRenderPass pass = CardRenderPass::ShadowAndBody,
    float prevCardX = 99999.0f,
    float prevCardY = 99999.0f,
    float opacity = 1.0f
) {
    if (c.flipAngle >= 0.005f) {
        // True 3D In-Game Card Flip (Smoothly eases into target position & angle at both endpoints)
        Card3DTransform xf{};
        xf.baseW = baseW;
        xf.baseH = baseH;

        const float arc = std::sin(c.flipAngle);
        const float flutter = std::sin(c.flipAngle * 2.0f) * arc * 0.018f;
        const float rollVariance = cardHashNoise(0x7F4A23u, c.id, 501) * 0.035f;
        const float pitchVariance = cardHashNoise(0x7F4A23u, c.id, 502) * 0.03f;

        xf.yaw = c.flipAngle;
        xf.cz = arc * (baseW * 0.42f) + c.elevation;
        xf.pitch = arc * (0.18f + pitchVariance);
        xf.roll = c.angle + arc * rollVariance + flutter;
        xf.cx = baseX + baseW * 0.5f;
        xf.cy = baseY + baseH * 0.5f;

        render3DCard(r, c, xf, pal, light, pass);
        return;
    }

    const auto wearLvl = CardTextureCache::instance().cardWearLevel();
    const CardScreenRect rect = computeCardScreenRect(c, baseX, baseY, baseW, baseH, wearLvl);
    const float x = rect.x;
    const float y = rect.y;
    const float w = rect.w;
    const float h = rect.h;
    const float radius = rect.radius;
    const float scaleX = rect.scaleX;
    const CardPhysicalWarp& warp = rect.warp;
    ScopedCardRotation rotGuard(r, c.angle, x + w * 0.5f, y + h * 0.5f);

    // Dynamic drop shadow driven by physical light source & card camber depth:
    // Emit a single unified shadow at the card's effective elevation so Max-blend
    // never produces a two-tone step between contact and penumbra silhouettes.
    if (pass != CardRenderPass::BodyOnly) {
        (void)prevCardX;
        (void)prevCardY;
        const float shadowElevation = 2.5f + c.elevation + warp.liftZ * 0.65f;
        const float shadowOpacity = std::clamp(0.32f + (c.elevation + warp.liftZ * 0.4f) * 0.010f, 0.16f, 0.48f);
        r.drawRectDropShadow(x, y, w, h, radius, shadowElevation, light, shadowOpacity);
    }
    if (pass == CardRenderPass::ShadowOnly) {
        return;
    }

    const bool showBack = (!c.faceUp) || (c.flipAngle > kHalfPi);
    float scratchDistort = 0.0f;
    if (wearLvl == CardWearLevel::Played) scratchDistort = 0.0018f;
    else if (wearLvl == CardWearLevel::Vintage) scratchDistort = 0.0035f;

    const float condParam = static_cast<float>(wearLvl) + 10.0f * static_cast<float>(CardTextureCache::instance().cardStock());

    // Fast GPU Texture Atlas path
    if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().hasGpuAtlas()) {
        const auto& slot = showBack
            ? CardTextureCache::instance().getBackAtlasSlot(c.id)
            : CardTextureCache::instance().getAtlasSlot(c.suit, c.rank);
        r.drawTexturedRoundedRectGpu(x, y, w, h, radius, slot.u0, slot.v0, slot.u1, slot.v1, 1.0f, 1.0f, 1.0f, opacity, scratchDistort, condParam);

        // Optional RPG / modern solitaire card stat badges (HP, ATK, Shield, Durability)
    if (!showBack && !c.stats.empty()) {
        auto drawBadge = [&](float bx, float by, float bw, float bh,
                             float bgR, float bgG, float bgB, const char* txt) {
            r.drawRoundedRect(bx, by, bw, bh, 3.0f, bgR, bgG, bgB, 0.92f, 2);
            r.drawRoundedRectOutline(bx, by, bw, bh, 3.0f, 0.95f, 0.90f, 0.78f, 0.55f, 1.0f, 2);
            eatsbits::ui::glyph::drawVectorText(r, txt, bx + 4.0f, by + 3.0f, 9.5f,
                                                0.98f, 0.96f, 0.90f, 1.0f);
        };
        if (auto it = c.stats.find("hp"); it != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "HP %d", it->second);
            drawBadge(x + 4.0f, y + h - 18.0f, 38.0f, 14.0f, 0.62f, 0.14f, 0.16f, buf);
        }
        if (auto it = c.stats.find("attack"); it != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "ATK %d", it->second);
            drawBadge(x + w - 44.0f, y + h - 18.0f, 40.0f, 14.0f, 0.58f, 0.36f, 0.10f, buf);
        } else if (auto itD = c.stats.find("durability"); itD != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "DUR %d", itD->second);
            drawBadge(x + w - 44.0f, y + h - 18.0f, 40.0f, 14.0f, 0.16f, 0.38f, 0.56f, buf);
        }
    }

    if (c.enshadedGlow || c.hasTag("selected")) {
            r.drawRoundedRectOutline(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f,
                                     pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f, 4);
        } else if (wearLvl == CardWearLevel::Pristine) {
            r.drawRoundedRectOutline(x, y, w, h, radius, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f, 4);
        }
        return;
    }

    const CardTexture* tex = showBack
        ? CardTextureCache::instance().getCardBackTexture(c.id)
        : CardTextureCache::instance().getCardTexture(c.suit, c.rank);

    if (tex && !tex->argb32.empty()) {
        int mipW = tex->width;
        int mipH = tex->height;
        const uint32_t* mipData = tex->getMipData(w, mipW, mipH);
        if (mipData) {
            r.drawTexturedRoundedRect(x, y, w, h, radius, mipData, mipW, mipH, opacity);
        }

        // Optional RPG / modern solitaire card stat badges (HP, ATK, Shield, Durability)
    if (!showBack && !c.stats.empty()) {
        auto drawBadge = [&](float bx, float by, float bw, float bh,
                             float bgR, float bgG, float bgB, const char* txt) {
            r.drawRoundedRect(bx, by, bw, bh, 3.0f, bgR, bgG, bgB, 0.92f, 2);
            r.drawRoundedRectOutline(bx, by, bw, bh, 3.0f, 0.95f, 0.90f, 0.78f, 0.55f, 1.0f, 2);
            eatsbits::ui::glyph::drawVectorText(r, txt, bx + 4.0f, by + 3.0f, 9.5f,
                                                0.98f, 0.96f, 0.90f, 1.0f);
        };
        if (auto it = c.stats.find("hp"); it != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "HP %d", it->second);
            drawBadge(x + 4.0f, y + h - 18.0f, 38.0f, 14.0f, 0.62f, 0.14f, 0.16f, buf);
        }
        if (auto it = c.stats.find("attack"); it != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "ATK %d", it->second);
            drawBadge(x + w - 44.0f, y + h - 18.0f, 40.0f, 14.0f, 0.58f, 0.36f, 0.10f, buf);
        } else if (auto itD = c.stats.find("durability"); itD != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "DUR %d", itD->second);
            drawBadge(x + w - 44.0f, y + h - 18.0f, 40.0f, 14.0f, 0.16f, 0.38f, 0.56f, buf);
        }
    }

    if (c.enshadedGlow || c.hasTag("selected")) {
            r.drawRoundedRectOutline(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f,
                                     pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f, 4);
        } else if (wearLvl == CardWearLevel::Pristine) {
            r.drawRoundedRectOutline(x, y, w, h, radius, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f, 4);
        }
        return;
    }

    if (showBack) {
        // Zero-flush procedural card back (cream border + gradient inner panel + gold/cyan filigree frame + center sigil)
        r.drawRoundedRect(x, y, w, h, radius, 0.93f, 0.89f, 0.80f, 1.0f, 4);
        const float pad = std::max(3.5f, w * 0.065f);
        if (w > 2.0f * pad + 4.0f) {
            r.drawRoundedRectGradient(
                x + pad, y + pad, w - 2.0f * pad, h - 2.0f * pad, radius * 0.65f,
                pal.cardBackR0, pal.cardBackG0, pal.cardBackB0,
                pal.cardBackR1, pal.cardBackG1, pal.cardBackB1, 1.0f, 4
            );
            r.drawRoundedRectOutline(x + pad, y + pad, w - 2.0f * pad, h - 2.0f * pad,
                                     radius * 0.65f, pal.trimR, pal.trimG, pal.trimB, 0.78f, 1.2f, 4);
            const float pad2 = pad + std::max(3.0f, w * 0.045f);
            if (w > 2.0f * pad2 + 4.0f) {
                r.drawRoundedRectOutline(x + pad2, y + pad2, w - 2.0f * pad2, h - 2.0f * pad2,
                                         radius * 0.45f, pal.trimR, pal.trimG, pal.trimB, 0.42f, 1.0f, 4);
            }
            clockwork_ui::drawClockworkLogo(r, x + w * 0.5f, y + h * 0.5f, std::min(w * 0.28f, h * 0.20f), 0.0f);
        }
        r.drawRoundedRectOutline(x, y, w, h, radius, 0.15f, 0.12f, 0.09f, 0.85f, 1.2f, 4);
        return;
    }

    // 2. Warm Antique Parchment Cardstock Face
    r.drawRoundedRectGradient(x, y, w, h, radius,
                              0.98f, 0.96f, 0.90f,
                              0.92f, 0.88f, 0.79f, 1.0f, 4);

    const float inset = std::max(3.5f, w * 0.055f);
    if (w > 2.0f * inset + 6.0f) {
        r.drawRoundedRectOutline(x + inset, y + inset, w - 2.0f * inset, h - 2.0f * inset,
                                 radius * 0.6f, 0.78f, 0.72f, 0.62f, 0.45f, 1.0f, 4);
    }

    const bool red = isRedSuit(c.suit);
    const float sr = red ? 0.76f : 0.13f;
    const float sg = red ? 0.14f : 0.14f;
    const float sb = red ? 0.14f : 0.18f;

    if (scaleX > 0.35f) {
        const char* rStr = rankString(c.rank);
        const float fontH = baseW * 0.15f * scaleX;
        eatsbits::ui::glyph::drawVectorText(r, rStr, x + w * 0.09f, y + h * 0.055f, fontH, sr, sg, sb, 1.0f);
        drawSuitPip(r, c.suit, x + w * 0.15f, y + h * 0.22f, w * 0.072f, baseW * 0.072f, sr, sg, sb, 1.0f);

        eatsbits::ui::glyph::drawVectorText(r, rStr, x + w * 0.76f, y + h * 0.82f, fontH * 0.92f, sr, sg, sb, 0.90f);
        drawSuitPip(r, c.suit, x + w * 0.83f, y + h * 0.75f, w * 0.065f, baseW * 0.065f, sr, sg, sb, 0.90f);

        const int rk = static_cast<int>(c.rank);
        if (rk >= 11) {
            r.drawCircle(x + w * 0.5f, y + h * 0.52f, w * 0.27f, pal.trimR, pal.trimG, pal.trimB, 0.16f, 16);
            r.drawCircleOutline(x + w * 0.5f, y + h * 0.52f, w * 0.27f, pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.3f, 16);
            drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.54f, w * 0.18f, baseW * 0.18f, sr, sg, sb, 0.95f);
            float cyCrown = y + h * 0.33f;
            r.drawTriangle(x + w * 0.36f, cyCrown + 6.0f, x + w * 0.64f, cyCrown + 6.0f, x + w * 0.5f, cyCrown - 5.0f,
                           pal.trimR, pal.trimG, pal.trimB, 0.92f);
        } else if (c.rank == Rank::Ace) {
            r.drawCircle(x + w * 0.5f, y + h * 0.52f, w * 0.26f, pal.trimR, pal.trimG, pal.trimB, 0.14f, 16);
            drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.52f, w * 0.23f, baseW * 0.23f, sr, sg, sb, 0.96f);
        } else {
            const float prX = w * 0.088f;
            const float prY = baseW * 0.088f;
            if (rk == 2 || rk == 3) {
                drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.30f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.72f, prX, prY, sr, sg, sb, 0.90f);
                if (rk == 3) drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.51f, prX, prY, sr, sg, sb, 0.90f);
            } else {
                drawSuitPip(r, c.suit, x + w * 0.34f, y + h * 0.30f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.66f, y + h * 0.30f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.34f, y + h * 0.72f, prX, prY, sr, sg, sb, 0.90f);
                drawSuitPip(r, c.suit, x + w * 0.66f, y + h * 0.72f, prX, prY, sr, sg, sb, 0.90f);
                if (rk >= 6) {
                    drawSuitPip(r, c.suit, x + w * 0.34f, y + h * 0.51f, prX, prY, sr, sg, sb, 0.90f);
                    drawSuitPip(r, c.suit, x + w * 0.66f, y + h * 0.51f, prX, prY, sr, sg, sb, 0.90f);
                }
                if (rk == 5 || rk == 7 || rk == 9) {
                    drawSuitPip(r, c.suit, x + w * 0.5f, y + h * (rk == 7 ? 0.40f : 0.51f), prX, prY, sr, sg, sb, 0.90f);
                }
                if (rk >= 8) {
                    drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.38f, prX * 0.9f, prY * 0.9f, sr, sg, sb, 0.90f);
                    drawSuitPip(r, c.suit, x + w * 0.5f, y + h * 0.64f, prX * 0.9f, prY * 0.9f, sr, sg, sb, 0.90f);
                }
            }
        }
    }

    // Optional RPG / modern solitaire card stat badges (HP, ATK, Shield, Durability)
    if (!showBack && !c.stats.empty()) {
        auto drawBadge = [&](float bx, float by, float bw, float bh,
                             float bgR, float bgG, float bgB, const char* txt) {
            r.drawRoundedRect(bx, by, bw, bh, 3.0f, bgR, bgG, bgB, 0.92f, 2);
            r.drawRoundedRectOutline(bx, by, bw, bh, 3.0f, 0.95f, 0.90f, 0.78f, 0.55f, 1.0f, 2);
            eatsbits::ui::glyph::drawVectorText(r, txt, bx + 4.0f, by + 3.0f, 9.5f,
                                                0.98f, 0.96f, 0.90f, 1.0f);
        };
        if (auto it = c.stats.find("hp"); it != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "HP %d", it->second);
            drawBadge(x + 4.0f, y + h - 18.0f, 38.0f, 14.0f, 0.62f, 0.14f, 0.16f, buf);
        }
        if (auto it = c.stats.find("attack"); it != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "ATK %d", it->second);
            drawBadge(x + w - 44.0f, y + h - 18.0f, 40.0f, 14.0f, 0.58f, 0.36f, 0.10f, buf);
        } else if (auto itD = c.stats.find("durability"); itD != c.stats.end()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "DUR %d", itD->second);
            drawBadge(x + w - 44.0f, y + h - 18.0f, 40.0f, 14.0f, 0.16f, 0.38f, 0.56f, buf);
        }
    }

    if (c.enshadedGlow || c.hasTag("selected")) {
        r.drawRoundedRectOutline(x - 1.5f, y - 1.5f, w + 3.0f, h + 3.0f, radius + 1.5f,
                                 pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f, 4);
    } else {
        r.drawRoundedRectOutline(x, y, w, h, radius, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f, 4);
    }
}

void drawCarvedButton(
    eatsbits::ui::BatchRenderer2D& r,
    const HudButtonRect& b,
    bool hovered,
    bool highlighted,
    const ThemePalette& pal
) {
    if (SettingsManager::instance().settings().guiTheme == GuiThemeStyle::ClockworkEngine) {
        clockwork_ui::drawBrassButton(r, b.x, b.y, b.w, b.h, 4.0f, b.label, hovered, highlighted, 10.5f);
        if (b.iconId != SvgIconId::None) {
            constexpr float iconSize = 16.0f;
            const bool hasLabel = (b.label != nullptr && b.label[0] != '\0');
            float iconX = hasLabel ? (b.x + 6.0f) : (b.x + (b.w - iconSize) * 0.5f);
            float iconY = b.y + (b.h - iconSize) * 0.5f;
            const auto& cpal = clockwork_ui::palette();
            float iconR = (hovered || highlighted) ? cpal.furnaceCoreR : cpal.brassBrightR;
            float iconG = (hovered || highlighted) ? cpal.furnaceCoreG : cpal.brassBrightG;
            float iconB = (hovered || highlighted) ? cpal.furnaceCoreB : cpal.brassBrightB;
            SvgIconRenderer::instance().renderIcon(r, b.iconId, iconX, iconY, iconSize, iconSize, iconR, iconG, iconB, 1.0f);
        }
        if (b.id == BtnVariant) {
            // Flanking brass gears beside game variant, matching the clockwork mockup
            const auto& cpal = clockwork_ui::palette();
            clockwork_ui::drawGear(r, b.x - 7.0f, b.y + b.h * 0.5f, 9.0f, 7.0f, 4.5f, 8, 0.4f,
                                   cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.95f,
                                   cpal.brassBrightR, cpal.brassBrightG, cpal.brassBrightB, 1.0f, 4, 2.5f);
            clockwork_ui::drawGear(r, b.x + b.w + 7.0f, b.y + b.h * 0.5f, 9.0f, 7.0f, 4.5f, 8, -0.4f,
                                   cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.95f,
                                   cpal.brassBrightR, cpal.brassBrightG, cpal.brassBrightB, 1.0f, 4, 2.5f);
        }
        return;
    }

    const float bgBoost = hovered ? 0.08f : 0.0f;
    r.drawRoundedRectGradient(
        b.x, b.y, b.w, b.h, 5.0f,
        std::min(1.0f, pal.woodR0 + 0.06f + bgBoost),
        std::min(1.0f, pal.woodG0 + 0.05f + bgBoost),
        std::min(1.0f, pal.woodB0 + 0.04f + bgBoost),
        std::min(1.0f, pal.woodR1 + 0.02f + bgBoost),
        std::min(1.0f, pal.woodG1 + 0.02f + bgBoost),
        std::min(1.0f, pal.woodB1 + 0.02f + bgBoost),
        0.96f, 4
    );
    const float borderR = (hovered || highlighted) ? pal.accentR : pal.trimR;
    const float borderG = (hovered || highlighted) ? pal.accentG : pal.trimG;
    const float borderB = (hovered || highlighted) ? pal.accentB : pal.trimB;
    r.drawRoundedRectOutline(b.x, b.y, b.w, b.h, 5.0f, borderR, borderG, borderB,
                             (hovered || highlighted) ? 0.95f : 0.62f, (hovered || highlighted) ? 1.6f : 1.1f, 4);

    float textOffsetX = 10.0f;
    const bool hasLabel = (b.label != nullptr && b.label[0] != '\0');
    if (b.iconId != SvgIconId::None) {
        constexpr float iconSize = 16.0f;
        float iconX = hasLabel ? (b.x + 6.0f) : (b.x + (b.w - iconSize) * 0.5f);
        float iconY = b.y + (b.h - iconSize) * 0.5f;
        float iconR = (hovered || highlighted) ? pal.accentR : pal.trimR;
        float iconG = (hovered || highlighted) ? pal.accentG : pal.trimG;
        float iconB = (hovered || highlighted) ? pal.accentB : pal.trimB;
        SvgIconRenderer::instance().renderIcon(r, b.iconId, iconX, iconY, iconSize, iconSize, iconR, iconG, iconB, 1.0f);
        textOffsetX = 26.0f;
    }

    if (hasLabel) {
        float textW = eatsbits::ui::glyph::getVectorTextWidth(10.5f, b.label);
        float textX = (b.w <= 38.0f || b.id == BtnVariant)
            ? (b.x + (b.w - textW) * 0.5f)
            : (b.x + textOffsetX);
        float textY = b.y + (b.h - 11.0f) * 0.5f;

        eatsbits::ui::glyph::drawVectorText(
            r, b.label, textX, textY, 10.5f,
            highlighted ? pal.accentR : 0.94f,
            highlighted ? pal.accentG : 0.90f,
            highlighted ? pal.accentB : 0.82f,
            1.0f
        );
    }
}

void drawMenuOrbButton(
    eatsbits::ui::BatchRenderer2D& r,
    const HudButtonRect& b,
    bool hovered,
    bool menuOpen,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light
) {
    const float cx = b.x + b.w * 0.5f;
    const float cy = b.y + b.h * 0.5f;
    const float radius = b.w * 0.5f;

    if (SettingsManager::instance().settings().guiTheme == GuiThemeStyle::ClockworkEngine) {
        clockwork_ui::drawClockworkMenuOrb(r, cx, cy, radius, hovered, menuOpen, 0.0f);
        return;
    }

    const float alpha = (hovered || menuOpen) ? 0.95f : 0.42f;

    r.drawCircleDropShadow(cx, cy, radius, 4.0f, light, alpha * 0.45f);
    r.drawCircle(cx, cy, radius,
                 pal.woodR0 + 0.08f, pal.woodG0 + 0.06f, pal.woodB0 + 0.05f,
                 alpha, 24);

    const float rimR = hovered ? 1.0f : (menuOpen ? pal.accentR : pal.trimR);
    const float rimG = hovered ? 1.0f : (menuOpen ? pal.accentG : pal.trimG);
    const float rimB = hovered ? 1.0f : (menuOpen ? pal.accentB : pal.trimB);

    r.drawCircleOutline(cx, cy, radius, rimR, rimG, rimB, alpha, (hovered || menuOpen) ? 1.8f : 1.2f, 24);

    if (menuOpen) {
        // Draw crisp 'X' close indicator when menu is pinned/toggled on
        const float xSize = radius * 0.38f;
        r.drawLine(cx - xSize, cy - xSize, cx + xSize, cy + xSize, rimR, rimG, rimB, alpha, 2.2f);
        r.drawLine(cx - xSize, cy + xSize, cx + xSize, cy - xSize, rimR, rimG, rimB, alpha, 2.2f);
    } else {
        // SolEngine Clockwork 'S' insignia
        const float fontSize = radius * 0.75f;
        const float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSize, "S");
        // Subtle drop shadow
        eatsbits::ui::glyph::drawVectorText(
            r, "S", cx - txtW * 0.5f + 1.0f, cy - fontSize * 0.5f + 1.0f, fontSize,
            0.0f, 0.0f, 0.0f, alpha * 0.65f
        );
        // Foreground glyph
        eatsbits::ui::glyph::drawVectorText(
            r, "S", cx - txtW * 0.5f, cy - fontSize * 0.5f, fontSize,
            rimR, rimG, rimB, alpha
        );
    }
}

void drawStaticTableChrome(
    eatsbits::ui::BatchRenderer2D& r,
    const SolitaireBoard& board,
    float width,
    float height,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light
) {
    // 1. Physically Lit Procedural Playfield spanning 100% of the display
    ProceduralBackgroundSystem::instance().renderBackground(
        r, 0.0f, 0.0f, width, height, board.tableThemeSlug(), light, board.seed()
    );

    r.drawDustSpecks(0.0f, 0.0f, width, height, 36, board.seed(), pal.trimR, pal.trimG, pal.trimB, 0.12f);

    // Subtle table perimeter border
    r.drawRoundedRectOutline(0.0f, 0.0f, width, height, 0.0f, pal.trimR, pal.trimG, pal.trimB, 0.35f, 1.5f, 0);

    // 2. Recessed Empty Pile Wells & Foundation Runes
    const auto* profMark = SettingsManager::instance().getGameProfile(board.activeGameDef().id);
    const bool showMarkings = (profMark && profMark->showTableMarkings)
        ? *profMark->showTableMarkings
        : SettingsManager::instance().settings().showTableMarkings;
    if (showMarkings) {
        const float cw = board.cardWidth();
        const float ch = board.cardHeight();
        const float wellRadius = std::max(5.0f, cw * 0.08f);

        // Blend directly into the background texture as if dyed/painted on the table
        r.setBlendMode(eatsbits::ui::BlendMode::Multiply);

        const auto& piles = board.piles();
        for (size_t pIdx = 0; pIdx < piles.size(); ++pIdx) {
            const Pile& p = piles[pIdx];
            if (p.x < -500.0f) continue;
            ScopedCardRotation slotRotGuard(r, p.angle, p.x + cw * 0.5f, p.y + ch * 0.5f);
            r.drawRoundedRect(p.x, p.y, cw, ch, wellRadius, 0.0f, 0.0f, 0.0f, 0.25f, 4);
            r.drawRoundedRectOutline(p.x, p.y, cw, ch, wellRadius, pal.trimR, pal.trimG, pal.trimB, 0.40f, 1.2f, 4);

            if (p.type == PileType::Foundation) {
                Suit foundationSuit = static_cast<Suit>(p.index % 4);
                drawSuitPip(r, foundationSuit, p.x + cw * 0.5f, p.y + ch * 0.5f, cw * 0.18f, cw * 0.18f,
                            pal.trimR, pal.trimG, pal.trimB, 0.32f);
            } else if (p.type == PileType::Stock) {
                r.drawCircleOutline(p.x + cw * 0.5f, p.y + ch * 0.5f, cw * 0.20f,
                                    pal.trimR, pal.trimG, pal.trimB, 0.40f, 2.0f, 16);
            }
        }

        r.setBlendMode(eatsbits::ui::BlendMode::Normal);
    }
}

void drawTransportBar(
    eatsbits::ui::BatchRenderer2D& r,
    float width,
    float height,
    const AutoPlayController& controller,
    const std::vector<HudButtonRect>& buttons,
    int hoveredBtnId,
    const ThemePalette& pal,
    const eatsbits::ui::LightSource2D& light
) {
    (void)height;
    if (buttons.empty() || !controller.isActive()) return;

    const float barX = buttons.front().x - 12.0f;
    const float barY = buttons.front().y - 5.0f;
    const float barW = (buttons.back().x + buttons.back().w + 12.0f) - barX;
    const float barH = 42.0f;

    // Status Banner above transport bar
    const float bannerH = 26.0f;
    const float bannerW = std::max(barW, 380.0f);
    const float bannerX = (width - bannerW) * 0.5f;
    const float bannerY = barY - bannerH - 8.0f;

    // 1. Draw Banner
    r.drawRoundedRectGradient(
        bannerX, bannerY, bannerW, bannerH, 6.0f,
        pal.woodR0 * 0.4f + 0.04f, pal.woodG0 * 0.4f + 0.04f, pal.woodB0 * 0.4f + 0.04f,
        pal.woodR1 * 0.3f + 0.02f, pal.woodG1 * 0.3f + 0.02f, pal.woodB1 * 0.3f + 0.02f,
        0.94f, 4
    );
    r.drawRoundedRectOutline(bannerX, bannerY, bannerW, bannerH, 6.0f,
                             pal.accentR, pal.accentG, pal.accentB, 0.88f, 1.4f, 4);

    std::string status = controller.getStatusText();
    std::string moveDesc = controller.getCurrentMoveDescription();
    if (!moveDesc.empty() && controller.isPlaying()) {
        status += " [" + moveDesc + "]";
    }
    float textW = eatsbits::ui::glyph::getVectorTextWidth(11.0f, status.c_str());
    float textX = bannerX + (bannerW - textW) * 0.5f;
    float textY = bannerY + (bannerH - 11.0f) * 0.5f;
    eatsbits::ui::glyph::drawVectorText(r, status.c_str(), textX, textY, 11.0f,
                                        pal.accentR, pal.accentG, pal.accentB, 1.0f);

    // 2. Draw Transport Bar
    r.drawRectDropShadow(barX, barY, barW, barH, 8.0f, 10.0f, light, 0.5f);
    r.drawRoundedRectGradient(
        barX, barY, barW, barH, 8.0f,
        pal.woodR0 + 0.06f, pal.woodG0 + 0.05f, pal.woodB0 + 0.04f,
        pal.woodR1 + 0.03f, pal.woodG1 + 0.03f, pal.woodB1 + 0.02f,
        0.98f, 4
    );
    r.drawRoundedRectOutline(barX, barY, barW, barH, 8.0f,
                             pal.trimR, pal.trimG, pal.trimB, 0.90f, 1.5f, 4);

    // 3. Draw Buttons
    for (const auto& b : buttons) {
        bool hov = (hoveredBtnId == b.id);
        bool hi = (b.id == BtnPlayPause && controller.isPlaying());
        drawCarvedButton(r, b, hov, hi, pal);
    }
}

} // namespace

struct HelpSidebarRect {
    float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f};
    float closeX{0.0f}, closeY{0.0f}, closeW{24.0f}, closeH{24.0f};
    float showMeX{0.0f}, showMeY{0.0f}, showMeW{78.0f}, showMeH{22.0f};
    float fontIncX{0.0f}, fontIncY{0.0f}, fontIncW{24.0f}, fontIncH{22.0f};
    float fontDecX{0.0f}, fontDecY{0.0f}, fontDecW{24.0f}, fontDecH{22.0f};
    bool isBottomSheet{false};
};

HelpSidebarRect computeHelpSidebarRect(float width, float height) noexcept {
    HelpSidebarRect r{};
    // Desktop: right-hand sidebar. Mobile (both portrait & landscape) / narrow web: bottom sheet.
    r.isBottomSheet = (width < 780.0f || height < 500.0f);
    if (r.isBottomSheet) {
        r.h = std::clamp(height * 0.44f, 160.0f, 260.0f);
        r.w = std::max(220.0f, width - 24.0f);
        r.x = 12.0f;
        r.y = height - r.h - 12.0f;
    } else {
        r.w = std::min(350.0f, width * 0.34f);
        r.h = std::max(220.0f, height - 96.0f);
        r.x = width - r.w - 12.0f;
        r.y = 48.0f;
    }
    r.closeX = r.x + r.w - 30.0f;
    r.closeY = r.y + 8.0f;
    r.showMeX = r.closeX - 84.0f;
    r.showMeY = r.y + 9.0f;
    r.fontIncX = r.showMeX - 28.0f;
    r.fontIncY = r.y + 9.0f;
    r.fontDecX = r.fontIncX - 26.0f;
    r.fontDecY = r.y + 9.0f;
    return r;
}

std::string toLowerAscii(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string lookupGlossaryTerm(const std::string& term, const SolitaireBoard& board) {
    const std::string lower = toLowerAscii(term);
    if (lower == "build up") return "Building up in rank: A -> 2 -> 3 -> 4 -> 5 ...";
    if (lower == "build down") return "Building down in rank: K -> Q -> J -> 10 -> 9 ...";
    if (lower == "alternating color") return "Red on Black, or Black on Red (e.g. 7H on 8S).";
    if (lower == "suit") return "Cards must share the same suit (Spades, Hearts, Diamonds, or Clubs).";
    if (lower == "room") return "The 4 active encounter cards currently in front of you.";
    if (lower == "cap") return "Max monster power your equipped weapon can still block.";
    for (const auto& sd : board.docManifest().stats) {
        if (toLowerAscii(sd.key) == lower) return sd.title + ": " + sd.description;
    }
    for (const auto& td : board.docManifest().tags) {
        if (toLowerAscii(td.tag) == lower) return td.title + ": " + td.description;
    }
    for (const auto& zd : board.docManifest().zones) {
        if (toLowerAscii(zd.title) == lower) {
            const std::string& desc = !zd.description.empty() ? zd.description : (!zd.bullets.empty() ? zd.bullets.front() : "");
            return desc.empty() ? zd.title : (zd.title + ": " + desc);
        }
    }
    for (const auto& rd : board.docManifest().rules) {
        const std::string rTitle = toLowerAscii(rd.title);
        if ((rTitle == lower || rTitle.rfind(lower, 0) == 0) && !rd.bullets.empty()) {
            return rd.title + ": " + rd.bullets.front();
        }
    }
    return term + ": Rule element on the table.";
}

void drawHelpRichLine(
    eatsbits::ui::BatchRenderer2D& r,
    const std::string& lineStr,
    float x,
    float y,
    float fontSize,
    float baseR,
    float baseG,
    float baseB,
    float baseA,
    const ThemePalette& pal,
    float mouseX,
    float mouseY,
    float viewY,
    float viewH,
    std::string& outHoveredGlossaryTerm
) {
    float cx = x;
    size_t pos = 0;
    while (pos < lineStr.size()) {
        size_t open = lineStr.find("[[", pos);
        if (open == std::string::npos) {
            std::string tail = lineStr.substr(pos);
            eatsbits::ui::glyph::drawVectorText(r, tail.c_str(), cx, y, fontSize, baseR, baseG, baseB, baseA);
            break;
        }
        if (open > pos) {
            std::string pre = lineStr.substr(pos, open - pos);
            eatsbits::ui::glyph::drawVectorText(r, pre.c_str(), cx, y, fontSize, baseR, baseG, baseB, baseA);
            cx += eatsbits::ui::glyph::getVectorTextWidth(fontSize, pre.c_str());
        }
        size_t close = lineStr.find("]]", open + 2);
        if (close == std::string::npos) {
            std::string tail = lineStr.substr(open);
            eatsbits::ui::glyph::drawVectorText(r, tail.c_str(), cx, y, fontSize, baseR, baseG, baseB, baseA);
            break;
        }
        std::string term = lineStr.substr(open + 2, close - (open + 2));
        float tw = eatsbits::ui::glyph::getVectorTextWidth(fontSize, term.c_str());
        eatsbits::ui::glyph::drawVectorText(r, term.c_str(), cx, y, fontSize, pal.accentR, pal.accentG, pal.accentB, 1.0f);
        r.drawLine(cx, y + fontSize + 1.3f, cx + tw, y + fontSize + 1.3f, pal.accentR, pal.accentG, pal.accentB, 0.85f, 1.0f);
        if (mouseX >= cx && mouseX <= cx + tw && mouseY >= y - 2.0f && mouseY <= y + fontSize + 2.8f &&
            mouseY >= viewY && mouseY <= viewY + viewH) {
            outHoveredGlossaryTerm = term;
        }
        cx += tw;
        pos = close + 2;
    }
}

void drawMiniCardStack(eatsbits::ui::BatchRenderer2D& r, const std::vector<std::string>& cards, float x, float y) {
    const size_t count = std::min<size_t>(cards.size(), 3);
    for (size_t i = 0; i < count; ++i) {
        const std::string& code = cards[i];
        if (code.size() < 2) continue;
        char suitCh = static_cast<char>(std::toupper(static_cast<unsigned char>(code.back())));
        std::string rankStr = code.substr(0, code.size() - 1);
        const bool isRed = (suitCh == 'H' || suitCh == 'D');
        const char* suitSym = (suitCh == 'H') ? "H" : (suitCh == 'D') ? "D" : (suitCh == 'C') ? "C" : "S";
        const float cy = y + static_cast<float>(i) * 14.0f;
        r.drawRoundedRect(x, cy, 34.0f, 16.0f, 3.0f, 0.96f, 0.94f, 0.88f, 1.0f, 2);
        r.drawRoundedRectOutline(x, cy, 34.0f, 16.0f, 3.0f, 0.30f, 0.25f, 0.20f, 0.90f, 1.0f, 2);
        std::string label = rankStr + suitSym;
        eatsbits::ui::glyph::drawVectorText(r, label.c_str(), x + 4.0f, cy + 3.0f, 9.5f,
                                            isRed ? 0.78f : 0.14f, isRed ? 0.12f : 0.14f, isRed ? 0.14f : 0.16f, 1.0f);
    }
}

void drawPipMeter(eatsbits::ui::BatchRenderer2D& r, float x, float y, uint8_t rating, const ThemePalette& pal) {
    for (uint8_t i = 0; i < 5; ++i) {
        const bool filled = (i < rating);
        const float px = x + static_cast<float>(i) * 11.0f;
        r.drawRoundedRect(px, y, 9.0f, 9.0f, 2.0f,
                          filled ? pal.accentR : 0.25f,
                          filled ? pal.accentG : 0.23f,
                          filled ? pal.accentB : 0.21f,
                          filled ? 0.95f : 0.60f, 1);
        r.drawRoundedRectOutline(px, y, 9.0f, 9.0f, 2.0f, 0.10f, 0.08f, 0.06f, 0.85f, 1.0f, 1);
    }
}

std::vector<ZoneDoc> resolveHelpZones(const SolitaireBoard& board) {
    const auto& manifest = board.docManifest();
    if (!manifest.zones.empty()) {
        return manifest.zones;
    }
    // Auto-discover classic pile roles if no doc_zone() was declared
    std::vector<ZoneDoc> autoZones;
    bool seenStock = false, seenWaste = false, seenFound = false, seenTab = false, seenCell = false;
    for (size_t i = 0; i < board.piles().size(); ++i) {
        const auto& p = board.piles()[i];
        if (p.type == PileType::Stock && !seenStock) {
            seenStock = true;
            autoZones.push_back({static_cast<uint8_t>(i), "Stock", "Click to draw new cards.", {}, {"Click to draw cards from the Stock."}});
        } else if (p.type == PileType::Waste && !seenWaste) {
            seenWaste = true;
            autoZones.push_back({static_cast<uint8_t>(i), "Waste", "Drawn cards ready to play.", {}, {"Top face-up card is available to play."}});
        } else if (p.type == PileType::Foundation && !seenFound) {
            seenFound = true;
            autoZones.push_back({static_cast<uint8_t>(i), "Foundations", "Build suit sequences here to win.", {"AH", "2H", "3H"}, {"[[Build up]] in the same [[Suit]] from Ace to King."}});
        } else if (p.type == PileType::FreeCell && !seenCell) {
            seenCell = true;
            autoZones.push_back({static_cast<uint8_t>(i), "FreeCells", "Temporary single-card reserve slots.", {}, {"Store any single exposed card temporarily."}});
        } else if (p.type == PileType::Tableau && !seenTab) {
            seenTab = true;
            autoZones.push_back({static_cast<uint8_t>(i), "Tableau", "Main playfield columns.", {"8C", "7H", "6S"}, {"[[Build down]] in [[Alternating color]]."}});
        }
    }
    return autoZones;
}

void drawInfoCircleIcon(eatsbits::ui::BatchRenderer2D& r, float cx, float cy, float radius,
                        float cr, float cg, float cb, float alpha = 0.95f) {
    r.drawCircleOutline(cx, cy, radius, cr, cg, cb, alpha, 1.3f, 16);
    r.drawCircle(cx, cy - radius * 0.36f, std::max(1.0f, radius * 0.16f), cr, cg, cb, alpha, 10);
    r.drawLine(cx, cy - radius * 0.08f, cx, cy + radius * 0.45f, cr, cg, cb, alpha, 1.35f);
}

std::vector<std::string> wrapHelpText(const std::string& text, float maxWidthPx, float fontSize) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    std::istringstream iss(text);
    std::string word;
    std::string current;
    while (iss >> word) {
        std::string candidate = current.empty() ? word : (current + " " + word);
        if (!current.empty() && eatsbits::ui::glyph::getVectorTextWidth(fontSize, candidate.c_str()) > maxWidthPx) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

void openYouTubeVideo(const std::string& videoId) {
    if (videoId.empty() || videoId.size() > 32) return;
    for (unsigned char ch : videoId) {
        if (!std::isalnum(ch) && ch != '-' && ch != '_') return;
    }
    const std::string url = "https://www.youtube.com/watch?v=" + videoId;
#if defined(__EMSCRIPTEN__)
    EM_ASM({ window.open(UTF8ToString($0), '_blank', 'noopener'); }, url.c_str());
#elif defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\" &";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
    std::system(cmd.c_str());
#endif
}

struct HelpZoneHit {
    uint8_t zoneIndex{0};
    float x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f};
};

struct HelpSidebarLayout {
    HelpSidebarRect sb{};
    float viewY{0.0f};
    float viewH{0.0f};
    float contentH{0.0f};
    float videoX{0.0f}, videoY{0.0f}, videoW{0.0f}, videoH{0.0f};
    float analyzeX{0.0f}, analyzeY{0.0f}, analyzeW{0.0f}, analyzeH{24.0f};
    float hintX{0.0f}, hintY{0.0f}, hintW{0.0f}, hintH{26.0f};
    float solvableX{0.0f}, solvableY{0.0f}, solvableW{0.0f}, solvableH{26.0f};
    float gamesX{0.0f}, gamesY{0.0f}, gamesW{0.0f}, gamesH{26.0f};
    std::vector<HelpZoneHit> zoneHits;
};

HelpSidebarLayout buildHelpSidebarLayout(
    const SolitaireBoard& board,
    float width,
    float height,
    float helpScrollY,
    const HelpOddsInfo* odds,
    bool hasVideoThumb = false
) {
    HelpSidebarLayout out{};
    out.sb = computeHelpSidebarRect(width, height);
    out.viewY = out.sb.y + 40.0f;
    out.viewH = std::max(40.0f, out.sb.h - 46.0f);

    const float fs = std::clamp(SettingsManager::instance().settings().helpFontScale, 0.85f, 1.35f);
    const float bodySize = 11.0f * fs;
    const float titleSize = 11.5f * fs;
    const float lineH = 16.0f * fs;
    const float headerStep = 17.0f * fs;

    const auto& def = board.activeGameDef();
    const auto& manifest = board.docManifest();
    const float lx = out.sb.x + 12.0f;
    const float maxTextW = std::max(100.0f, out.sb.w - 28.0f);
    float cy = out.viewY + 4.0f - std::max(0.0f, helpScrollY);
    const float startCy = cy;

    // 0. Optional Centered YouTube Thumbnail Card (only shown when thumbnail is ready)
    if (hasVideoThumb && !manifest.videoId.empty()) {
        out.videoW = std::min(240.0f, maxTextW);
        out.videoH = std::round(out.videoW * (9.0f / 16.0f));
        out.videoX = out.sb.x + (out.sb.w - out.videoW) * 0.5f;
        out.videoY = cy;
        cy += out.videoH + 10.0f;
    }

    // 1. Optional Odds/Skill/Time section
    if (odds && odds->hasResults && !odds->running) {
        cy += 3.0f * (17.0f * fs); // 3 rows (Time, Odds, Skill)
    }
    out.analyzeX = lx;
    out.analyzeY = cy;
    out.analyzeW = std::min(210.0f, maxTextW);
    out.analyzeH = 24.0f;
    cy += 30.0f;

    // 2. Decks & Wrapped Object / Defeat
    cy += headerStep; // DECKS
    const std::string& goalText = !manifest.winGoal.empty() ? manifest.winGoal : def.description;
    if (!goalText.empty()) {
        auto objLines = wrapHelpText("OBJECT: " + goalText, maxTextW, bodySize);
        cy += static_cast<float>(objLines.size()) * lineH + 4.0f;
    }
    if (!manifest.loseGoal.empty()) {
        auto loseLines = wrapHelpText("DEFEAT: " + manifest.loseGoal, maxTextW, bodySize);
        cy += static_cast<float>(loseLines.size()) * lineH + 4.0f;
    }
    if (!goalText.empty() || !manifest.loseGoal.empty()) {
        cy += 2.0f;
    }

    // 3. Zones
    const auto zones = resolveHelpZones(board);
    for (const auto& zd : zones) {
        const float zRowH = std::max(20.0f, titleSize + 8.0f);
        out.zoneHits.push_back({zd.zoneIndex, lx - 4.0f, cy - 2.0f, out.sb.w - 20.0f, zRowH});
        cy += zRowH;

        float miniBottom = cy;
        float textW = maxTextW - 10.0f;
        if (!zd.previewCards.empty()) {
            textW = std::max(80.0f, maxTextW - 48.0f);
            miniBottom = cy + static_cast<float>(std::min<size_t>(zd.previewCards.size(), 3)) * 14.0f + 4.0f;
        }
        const auto& bullets = !zd.bullets.empty() ? zd.bullets : std::vector<std::string>{zd.description};
        for (const auto& bText : bullets) {
            auto bLines = wrapHelpText(bText, textW, bodySize);
            cy += static_cast<float>(std::max<size_t>(1, bLines.size())) * lineH;
        }
        cy = std::max(cy, miniBottom) + 6.0f;
    }

    // 4. Rules
    if (!manifest.rules.empty()) {
        cy += 4.0f + headerStep; // RULES: header
        for (const auto& rd : manifest.rules) {
            cy += titleSize + 6.0f; // Rule title
            for (const auto& bText : rd.bullets) {
                auto bLines = wrapHelpText(bText, maxTextW - 10.0f, bodySize);
                cy += static_cast<float>(std::max<size_t>(1, bLines.size())) * lineH;
            }
            cy += 4.0f;
        }
    }

    // 5. Stats & Card Tags
    if (!manifest.stats.empty()) {
        cy += 4.0f + headerStep; // STATS: header
        for (const auto& sd : manifest.stats) {
            auto lines = wrapHelpText(sd.title + " (" + sd.key + "): " + sd.description, maxTextW, bodySize);
            cy += static_cast<float>(std::max<size_t>(1, lines.size())) * lineH;
        }
    }
    if (!manifest.tags.empty()) {
        cy += 4.0f + headerStep; // CARD TYPES: header
        for (const auto& td : manifest.tags) {
            auto lines = wrapHelpText(td.title + ": " + td.description, maxTextW, bodySize);
            cy += static_cast<float>(std::max<size_t>(1, lines.size())) * lineH;
        }
    }

    // 5. Alternate Names
    const auto& alts = !manifest.alternateNames.empty() ? manifest.alternateNames : def.alternateNames;
    if (!alts.empty()) {
        cy += 6.0f + headerStep;
        std::string joined;
        for (size_t i = 0; i < alts.size(); ++i) {
            if (i > 0) joined += ", ";
            joined += alts[i];
        }
        auto aLines = wrapHelpText(joined, maxTextW, bodySize);
        cy += static_cast<float>(std::max<size_t>(1, aLines.size())) * lineH + 6.0f;
    }

    // 6. Action Buttons at bottom of Help Sidebar (Hint, New Solvable, Games Library)
    cy += 6.0f;
    const float btnGap = 8.0f;
    const float halfW = std::max(60.0f, (maxTextW - btnGap) * 0.5f);
    out.hintX = lx;
    out.hintY = cy;
    out.hintW = halfW;
    out.hintH = 26.0f;

    out.solvableX = lx + halfW + btnGap;
    out.solvableY = cy;
    out.solvableW = halfW;
    out.solvableH = 26.0f;
    cy += 32.0f;

    out.gamesX = lx;
    out.gamesY = cy;
    out.gamesW = maxTextW;
    out.gamesH = 26.0f;
    cy += 34.0f;

    out.contentH = cy - startCy;
    return out;
}

void drawHelpSidebar(
    eatsbits::ui::BatchRenderer2D& r,
    const SolitaireBoard& board,
    float width,
    float height,
    const ThemePalette& pal,
    int highlightedHelpZone,
    float helpScrollY,
    float mouseX,
    float mouseY,
    const HelpOddsInfo* odds,
    const HelpVideoThumb* videoThumb
) {
    const bool hasVideoThumb = (videoThumb && videoThumb->ready() && videoThumb->videoId == board.docManifest().videoId);
    const HelpSidebarLayout rawHl = buildHelpSidebarLayout(board, width, height, 0.0f, odds, hasVideoThumb);
    const float maxScrollY = std::max(0.0f, rawHl.contentH - rawHl.viewH);
    const float clampedScrollY = std::clamp(helpScrollY, 0.0f, maxScrollY);
    const HelpSidebarLayout hl = buildHelpSidebarLayout(board, width, height, clampedScrollY, odds, hasVideoThumb);
    const HelpSidebarRect& sb = hl.sb;
    r.drawRoundedRectGradient(sb.x, sb.y, sb.w, sb.h, 8.0f,
                              pal.woodR0 * 0.92f, pal.woodG0 * 0.92f, pal.woodB0 * 0.92f,
                              pal.woodR1 * 0.88f, pal.woodG1 * 0.88f, pal.woodB1 * 0.88f, 0.96f, 4);
    r.drawRoundedRectOutline(sb.x, sb.y, sb.w, sb.h, 8.0f, pal.trimR, pal.trimG, pal.trimB, 0.85f, 1.8f, 4);

    const auto& def = board.activeGameDef();
    const auto& manifest = board.docManifest();
    const float lx = sb.x + 12.0f;
    const float maxTextW = std::max(100.0f, sb.w - 28.0f);

    const float fs = std::clamp(SettingsManager::instance().settings().helpFontScale, 0.85f, 1.35f);
    const float bodySize = 11.0f * fs;
    const float headerSize = 12.0f * fs;
    const float titleSize = 11.5f * fs;
    const float btnFontSize = 10.5f * fs;
    const float lineH = 16.0f * fs;
    const float headerStep = 17.0f * fs;

    // Header (fixed)
    eatsbits::ui::glyph::drawVectorText(r, def.name.c_str(), lx, sb.y + 11.0f, 14.0f, pal.accentR, pal.accentG, pal.accentB, 1.0f);
    // Font size controls: A- / A+
    r.drawRoundedRect(sb.fontDecX, sb.fontDecY, sb.fontDecW, sb.fontDecH, 4.0f, 0.20f, 0.18f, 0.15f, 0.90f, 2);
    r.drawRoundedRectOutline(sb.fontDecX, sb.fontDecY, sb.fontDecW, sb.fontDecH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.0f, 2);
    eatsbits::ui::glyph::drawVectorText(r, "A-", sb.fontDecX + 5.0f, sb.fontDecY + 5.5f, 9.5f, 0.92f, 0.90f, 0.84f, 1.0f);
    r.drawRoundedRect(sb.fontIncX, sb.fontIncY, sb.fontIncW, sb.fontIncH, 4.0f, 0.20f, 0.18f, 0.15f, 0.90f, 2);
    r.drawRoundedRectOutline(sb.fontIncX, sb.fontIncY, sb.fontIncW, sb.fontIncH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.0f, 2);
    eatsbits::ui::glyph::drawVectorText(r, "A+", sb.fontIncX + 5.0f, sb.fontIncY + 5.5f, 9.5f, 0.92f, 0.90f, 0.84f, 1.0f);

    r.drawRoundedRect(sb.showMeX, sb.showMeY, sb.showMeW, sb.showMeH, 4.0f, 0.16f, 0.34f, 0.24f, 0.92f, 2);
    r.drawRoundedRectOutline(sb.showMeX, sb.showMeY, sb.showMeW, sb.showMeH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.70f, 1.0f, 2);
    vector_icon::drawShowMeIcon(
        r,
        sb.showMeX + 12.0f,
        sb.showMeY + sb.showMeH * 0.5f,
        5.5f,
        0.94f, 0.96f, 0.88f,
        1.0f, 1.0f, 0.96f,
        1.0f, false
    );
    eatsbits::ui::glyph::drawVectorText(r, "SHOW ME", sb.showMeX + 21.0f, sb.showMeY + 5.5f, 9.5f, 0.94f, 0.96f, 0.88f, 1.0f);
    r.drawRoundedRect(sb.closeX, sb.closeY, sb.closeW, sb.closeH, 4.0f, 0.28f, 0.14f, 0.14f, 0.90f, 2);
    eatsbits::ui::glyph::drawVectorText(r, "X", sb.closeX + 7.5f, sb.closeY + 5.5f, 11.0f, 0.96f, 0.88f, 0.84f, 1.0f);
    r.drawLine(sb.x + 8.0f, sb.y + 36.0f, sb.x + sb.w - 8.0f, sb.y + 36.0f, pal.trimR, pal.trimG, pal.trimB, 0.35f, 1.0f);

    std::string hoveredGlossaryTerm;

    {
        auto scissor = r.makeScissorScope(sb.x + 2.0f, hl.viewY, sb.w - 4.0f, hl.viewH);
        float cy = hl.viewY + 4.0f - clampedScrollY;

        // 0. Optional Centered YouTube Thumbnail Card (only shown when thumbnail is ready)
        if (hasVideoThumb) {
            const bool vHov = (mouseX >= hl.videoX && mouseX <= hl.videoX + hl.videoW &&
                               mouseY >= cy && mouseY <= cy + hl.videoH &&
                               mouseY >= hl.viewY && mouseY <= hl.viewY + hl.viewH);
            r.drawRgbaBitmap(hl.videoX, cy, hl.videoW, hl.videoH,
                             videoThumb->rgba.data(), videoThumb->width, videoThumb->height,
                             vHov ? 1.0f : 0.94f);
            r.drawRoundedRectOutline(hl.videoX, cy, hl.videoW, hl.videoH, 4.0f,
                                     vHov ? pal.accentR : pal.trimR, vHov ? pal.accentG : pal.trimG, vHov ? pal.accentB : pal.trimB,
                                     vHov ? 0.95f : 0.70f, vHov ? 1.6f : 1.1f, 3);
            // Centered YouTube play button overlay
            const float badgeW = 42.0f;
            const float badgeH = 28.0f;
            const float badgeX = hl.videoX + (hl.videoW - badgeW) * 0.5f;
            const float badgeY = cy + (hl.videoH - badgeH) * 0.5f;
            r.drawRoundedRect(badgeX, badgeY, badgeW, badgeH, 7.0f,
                              vHov ? 0.92f : 0.80f, 0.12f, 0.12f, vHov ? 0.96f : 0.86f, 2);
            const float px = badgeX + 16.0f;
            const float py = badgeY + 7.5f;
            r.drawTriangle(px, py, px, py + 13.0f, px + 12.0f, py + 6.5f, 0.98f, 0.98f, 0.98f, 1.0f);
            cy += hl.videoH + 10.0f;
        }

        // 1. On-Demand Solver Odds / Skill / Time
        if (odds && odds->hasResults && !odds->running) {
            auto drawStatRow = [&](const char* lbl, uint8_t rating, const std::string& valTxt) {
                eatsbits::ui::glyph::drawVectorText(r, lbl, lx, cy + 1.0f, bodySize, pal.trimR, pal.trimG, pal.trimB, 0.95f);
                drawPipMeter(r, lx + 64.0f * fs, cy + 1.0f, rating, pal);
                eatsbits::ui::glyph::drawVectorText(r, valTxt.c_str(), lx + 126.0f * fs, cy + 1.0f, bodySize, 0.92f, 0.90f, 0.84f, 0.95f);
                cy += 17.0f * fs;
            };
            drawStatRow("TIME:", odds->timeRating, odds->timeLabel);
            drawStatRow("ODDS:", odds->oddsRating, odds->oddsLabel);
            drawStatRow("SKILL:", odds->skillRating, odds->skillLabel);
        }
        if (odds && odds->running) {
            r.drawRoundedRect(hl.analyzeX, cy, hl.analyzeW, hl.analyzeH, 4.0f, 0.45f, 0.16f, 0.14f, 0.92f, 2);
            r.drawRoundedRectOutline(hl.analyzeX, cy, hl.analyzeW, hl.analyzeH, 4.0f, pal.accentR, pal.accentG, pal.accentB, 0.80f, 1.0f, 2);
            char prog[64];
            std::snprintf(prog, sizeof(prog), "CANCEL (%u/%u)", odds->completedSeeds, odds->totalSeeds);
            eatsbits::ui::glyph::drawVectorText(r, prog, hl.analyzeX + 8.0f, cy + 6.0f, btnFontSize, 0.98f, 0.90f, 0.84f, 1.0f);
        } else {
            r.drawRoundedRect(hl.analyzeX, cy, hl.analyzeW, hl.analyzeH, 4.0f, 0.18f, 0.24f, 0.30f, 0.92f, 2);
            r.drawRoundedRectOutline(hl.analyzeX, cy, hl.analyzeW, hl.analyzeH, 4.0f, pal.trimR, pal.trimG, pal.trimB, 0.65f, 1.0f, 2);
            const char* lbl = (odds && odds->hasResults) ? "RE-ANALYZE ODDS" : "ANALYZE ODDS";
            eatsbits::ui::glyph::drawVectorText(r, lbl, hl.analyzeX + 8.0f, cy + 6.0f, btnFontSize, 0.92f, 0.94f, 0.96f, 1.0f);
        }
        cy += 30.0f;

        // 2. Decks & Word-Wrapped Object
        char deckBuf[48];
        if (def.deckCards > 0) {
            std::snprintf(deckBuf, sizeof(deckBuf), "DECKS: %u (%u CARDS)",
                          static_cast<unsigned>(std::max<uint8_t>(1, def.decks)),
                          static_cast<unsigned>(def.deckCards));
        } else {
            std::snprintf(deckBuf, sizeof(deckBuf), "DECKS: %u", static_cast<unsigned>(std::max<uint8_t>(1, def.decks)));
        }
        eatsbits::ui::glyph::drawVectorText(r, deckBuf, lx, cy, headerSize, pal.trimR, pal.trimG, pal.trimB, 0.95f);
        cy += headerStep;

        const std::string& goalText = !manifest.winGoal.empty() ? manifest.winGoal : def.description;
        if (!goalText.empty()) {
            for (const auto& line : wrapHelpText("OBJECT: " + goalText, maxTextW, bodySize)) {
                eatsbits::ui::glyph::drawVectorText(r, line.c_str(), lx, cy, bodySize, 0.94f, 0.91f, 0.84f, 0.95f);
                cy += lineH;
            }
            cy += 4.0f;
        }
        if (!manifest.loseGoal.empty()) {
            for (const auto& line : wrapHelpText("DEFEAT: " + manifest.loseGoal, maxTextW, bodySize)) {
                eatsbits::ui::glyph::drawVectorText(r, line.c_str(), lx, cy, bodySize, 0.95f, 0.68f, 0.62f, 0.95f);
                cy += lineH;
            }
            cy += 4.0f;
        }
        if (!goalText.empty() || !manifest.loseGoal.empty()) {
            cy += 2.0f;
        }

        // 3. Zones with ⓘ outline icon, mini-card previews, and word-wrapped [[term]] bullets
        const auto zones = resolveHelpZones(board);
        for (size_t i = 0; i < zones.size(); ++i) {
            const auto& zd = zones[i];
            const float zRowH = std::max(20.0f, titleSize + 8.0f);
            const bool isHi = (highlightedHelpZone == static_cast<int>(zd.zoneIndex));
            if (isHi) {
                r.drawRoundedRect(lx - 4.0f, cy - 2.0f, sb.w - 20.0f, zRowH, 3.0f, pal.accentR, pal.accentG, pal.accentB, 0.24f, 2);
            }
            drawInfoCircleIcon(r, lx + 6.0f, cy + titleSize * 0.55f + 1.0f, 5.5f,
                               isHi ? 1.0f : pal.accentR, isHi ? 0.96f : pal.accentG, isHi ? 0.72f : pal.accentB);
            eatsbits::ui::glyph::drawVectorText(r, zd.title.c_str(), lx + 16.0f, cy + 1.5f, titleSize,
                                                isHi ? 1.0f : pal.accentR, isHi ? 0.96f : pal.accentG, isHi ? 0.72f : pal.accentB, 1.0f);
            cy += zRowH;

            float textX = lx + 10.0f;
            float textW = maxTextW - 10.0f;
            float miniBottom = cy;
            if (!zd.previewCards.empty()) {
                drawMiniCardStack(r, zd.previewCards, lx + 4.0f, cy);
                textX = lx + 44.0f;
                textW = std::max(80.0f, maxTextW - 48.0f);
                miniBottom = cy + static_cast<float>(std::min<size_t>(zd.previewCards.size(), 3)) * 14.0f + 4.0f;
            }

            const auto& bullets = !zd.bullets.empty() ? zd.bullets : std::vector<std::string>{zd.description};
            for (const auto& bText : bullets) {
                auto bLines = wrapHelpText(bText, textW, bodySize);
                if (bLines.empty()) bLines.push_back("");
                for (size_t li = 0; li < bLines.size(); ++li) {
                    if (li == 0) {
                        eatsbits::ui::glyph::drawVectorText(r, "-", textX - 7.0f, cy, bodySize, 0.85f, 0.82f, 0.74f, 0.9f);
                    }
                    drawHelpRichLine(r, bLines[li], textX, cy, bodySize, 0.90f, 0.87f, 0.80f, 0.95f,
                                     pal, mouseX, mouseY, hl.viewY, hl.viewH, hoveredGlossaryTerm);
                    cy += lineH;
                }
            }
            cy = std::max(cy, miniBottom) + 6.0f;
        }

        // 4. Rules (How to Play / Card Resolution)
        if (!manifest.rules.empty()) {
            cy += 4.0f;
            eatsbits::ui::glyph::drawVectorText(r, "RULES:", lx, cy, headerSize, pal.trimR, pal.trimG, pal.trimB, 0.95f);
            cy += headerStep;
            for (const auto& rd : manifest.rules) {
                eatsbits::ui::glyph::drawVectorText(r, rd.title.c_str(), lx, cy, titleSize, pal.accentR, pal.accentG, pal.accentB, 1.0f);
                cy += titleSize + 6.0f;
                for (const auto& bText : rd.bullets) {
                    auto bLines = wrapHelpText(bText, maxTextW - 10.0f, bodySize);
                    if (bLines.empty()) bLines.push_back("");
                    for (size_t li = 0; li < bLines.size(); ++li) {
                        if (li == 0) {
                            eatsbits::ui::glyph::drawVectorText(r, "-", lx + 3.0f, cy, bodySize, 0.85f, 0.82f, 0.74f, 0.9f);
                        }
                        drawHelpRichLine(r, bLines[li], lx + 10.0f, cy, bodySize, 0.90f, 0.87f, 0.80f, 0.95f,
                                         pal, mouseX, mouseY, hl.viewY, hl.viewH, hoveredGlossaryTerm);
                        cy += lineH;
                    }
                }
                cy += 4.0f;
            }
        }

        // 5. Stats & Card Tags
        if (!manifest.stats.empty()) {
            cy += 4.0f;
            eatsbits::ui::glyph::drawVectorText(r, "STATS:", lx, cy, headerSize, pal.trimR, pal.trimG, pal.trimB, 0.95f);
            cy += headerStep;
            for (const auto& sd : manifest.stats) {
                auto lines = wrapHelpText(sd.title + " (" + sd.key + "): " + sd.description, maxTextW, bodySize);
                if (lines.empty()) lines.push_back("");
                for (const auto& line : lines) {
                    drawHelpRichLine(r, line, lx, cy, bodySize, 0.65f, 0.88f, 0.95f, 0.92f,
                                     pal, mouseX, mouseY, hl.viewY, hl.viewH, hoveredGlossaryTerm);
                    cy += lineH;
                }
            }
        }
        if (!manifest.tags.empty()) {
            cy += 4.0f;
            eatsbits::ui::glyph::drawVectorText(r, "CARD TYPES:", lx, cy, headerSize, pal.trimR, pal.trimG, pal.trimB, 0.95f);
            cy += headerStep;
            for (const auto& td : manifest.tags) {
                auto lines = wrapHelpText(td.title + ": " + td.description, maxTextW, bodySize);
                if (lines.empty()) lines.push_back("");
                for (const auto& line : lines) {
                    drawHelpRichLine(r, line, lx, cy, bodySize, 0.95f, 0.78f, 0.62f, 0.92f,
                                     pal, mouseX, mouseY, hl.viewY, hl.viewH, hoveredGlossaryTerm);
                    cy += lineH;
                }
            }
        }

        // 5. Alternate Names
        const auto& alts = !manifest.alternateNames.empty() ? manifest.alternateNames : def.alternateNames;
        if (!alts.empty()) {
            cy += 6.0f;
            eatsbits::ui::glyph::drawVectorText(r, "ALTERNATE NAMES:", lx, cy, headerSize, pal.trimR, pal.trimG, pal.trimB, 0.95f);
            cy += headerStep;
            std::string joined;
            for (size_t i = 0; i < alts.size(); ++i) {
                if (i > 0) joined += ", ";
                joined += alts[i];
            }
            for (const auto& line : wrapHelpText(joined, maxTextW, bodySize)) {
                eatsbits::ui::glyph::drawVectorText(r, line.c_str(), lx, cy, bodySize, 0.88f, 0.85f, 0.78f, 0.90f);
                cy += lineH;
            }
            cy += 6.0f;
        }

        // 6. Action Buttons at bottom of Help Sidebar (Hint, New Solvable, Games Library)
        cy += 6.0f;
        auto drawSidebarBtn = [&](float bx, float by, float bw, float bh, const char* label,
                                  float bgR, float bgG, float bgB, float txtR, float txtG, float txtB,
                                  int iconKind) {
            bool hov = (mouseX >= bx && mouseX <= bx + bw && mouseY >= by && mouseY <= by + bh &&
                        mouseY >= hl.viewY && mouseY <= hl.viewY + hl.viewH);
            r.drawRoundedRect(bx, by, bw, bh, 4.0f, bgR + (hov ? 0.06f : 0.0f), bgG + (hov ? 0.05f : 0.0f), bgB + (hov ? 0.04f : 0.0f), 0.95f, 2);
            r.drawRoundedRectOutline(bx, by, bw, bh, 4.0f, hov ? pal.accentR : pal.trimR, hov ? pal.accentG : pal.trimG, hov ? pal.accentB : pal.trimB, 0.80f, 1.1f, 2);
            const float tw = eatsbits::ui::glyph::getVectorTextWidth(10.5f, label);
            const float iconRad = 6.2f;
            const float iconGap = (iconKind > 0) ? (iconRad * 2.0f + 5.0f) : 0.0f;
            const float contentX = bx + (bw - (tw + iconGap)) * 0.5f;
            if (iconKind > 0) {
                const float icx = contentX + iconRad;
                const float icy = by + bh * 0.5f;
                const float hiR = std::min(1.0f, txtR * 1.25f + 0.15f);
                const float hiG = std::min(1.0f, txtG * 1.25f + 0.15f);
                const float hiB = std::min(1.0f, txtB * 1.25f + 0.15f);
                if (iconKind == 1) {
                    vector_icon::drawSolvableDealIcon(r, icx, icy, iconRad, txtR, txtG, txtB, hiR, hiG, hiB, 1.0f, false);
                } else if (iconKind == 2) {
                    vector_icon::drawGameLibraryIcon(r, icx, icy, iconRad, txtR, txtG, txtB, hiR, hiG, hiB, 1.0f, false);
                } else if (iconKind == 3) {
                    vector_icon::drawHintIcon(r, icx, icy, iconRad, txtR, txtG, txtB, hiR, hiG, hiB, 1.0f, false);
                }
            }
            eatsbits::ui::glyph::drawVectorText(r, label, contentX + iconGap, by + (bh - 10.5f) * 0.5f, 10.5f, txtR, txtG, txtB, 1.0f);
        };

        drawSidebarBtn(hl.hintX, cy, hl.hintW, hl.hintH, "HINT [H]",
                       0.24f, 0.20f, 0.14f, pal.trimR, pal.trimG, pal.trimB, 3);
        drawSidebarBtn(hl.solvableX, cy, hl.solvableW, hl.solvableH, "NEW SOLVABLE",
                       0.16f, 0.28f, 0.22f, 0.92f, 0.96f, 0.88f, 1);
        cy += 32.0f;
        drawSidebarBtn(hl.gamesX, cy, hl.gamesW, hl.gamesH, "GAMES LIBRARY",
                       0.20f, 0.22f, 0.28f, pal.accentR, pal.accentG, pal.accentB, 2);
    }

    // Subtle vertical scrollbar indicator when content overflows
    if (maxScrollY > 1.0f) {
        const float trackX = sb.x + sb.w - 7.0f;
        const float trackY = hl.viewY + 4.0f;
        const float trackW = 4.0f;
        const float trackH = std::max(24.0f, hl.viewH - 8.0f);
        const float thumbH = std::max(24.0f, trackH * (hl.viewH / std::max(hl.viewH, hl.contentH)));
        const float scrollFrac = clampedScrollY / maxScrollY;
        const float thumbY = trackY + scrollFrac * (trackH - thumbH);
        r.drawRoundedRect(trackX, trackY, trackW, trackH, 2.0f, 0.05f, 0.04f, 0.03f, 0.45f, 2);
        r.drawRoundedRect(trackX, thumbY, trackW, thumbH, 2.0f, pal.accentR, pal.accentG, pal.accentB, 0.85f, 2);
    }

    // Floating Glossary Tooltip (outside scissor!)
    if (!hoveredGlossaryTerm.empty()) {
        const std::string tip = lookupGlossaryTerm(hoveredGlossaryTerm, board);
        const auto tipLines = wrapHelpText(tip, 260.0f, 9.5f);
        float maxLineW = 0.0f;
        for (const auto& tl : tipLines) {
            maxLineW = std::max(maxLineW, eatsbits::ui::glyph::getVectorTextWidth(9.5f, tl.c_str()));
        }
        const float tw = std::min(280.0f, maxLineW + 18.0f);
        const float th = 16.0f + static_cast<float>(std::max<size_t>(1, tipLines.size())) * 13.5f;
        const float tx = std::clamp(mouseX - tw * 0.5f, 8.0f, width - tw - 8.0f);
        const float ty = std::max(8.0f, mouseY - th - 10.0f);
        r.drawRoundedRectGradient(tx, ty, tw, th, 5.0f, 0.16f, 0.12f, 0.08f, 0.10f, 0.08f, 0.05f, 0.98f, 3);
        r.drawRoundedRectOutline(tx, ty, tw, th, 5.0f, pal.accentR, pal.accentG, pal.accentB, 0.95f, 1.5f, 3);
        float lineY = ty + 8.0f;
        for (const auto& tl : tipLines) {
            eatsbits::ui::glyph::drawVectorText(r, tl.c_str(), tx + 9.0f, lineY, 9.5f, 0.98f, 0.94f, 0.84f, 1.0f);
            lineY += 13.5f;
        }
    }
}

void computeZoneHighlightBounds(const SolitaireBoard& board, int zoneIdx,
                                float& outX, float& outY, float& outW, float& outH) noexcept {
    const auto& piles = board.piles();
    const float cw = board.cardWidth();
    const float ch = board.cardHeight();
    if (zoneIdx < 0 || static_cast<size_t>(zoneIdx) >= piles.size()) {
        outX = outY = outW = outH = 0.0f;
        return;
    }
    const Pile& target = piles[static_cast<size_t>(zoneIdx)];
    const auto& docZones = board.docManifest().zones;
    size_t docZonesOfSameType = 0;
    for (const auto& zd : docZones) {
        if (zd.zoneIndex < piles.size() && piles[zd.zoneIndex].type == target.type) {
            ++docZonesOfSameType;
        }
    }
    const bool groupByType = (docZones.empty() || docZonesOfSameType <= 1) &&
        (target.type == PileType::Foundation ||
         target.type == PileType::Tableau ||
         target.type == PileType::FreeCell ||
         target.type == PileType::Stock ||
         target.type == PileType::Waste);

    float minX = target.x;
    float minY = target.y;
    float maxX = target.x + cw;
    float maxY = target.y + ch;

    auto expandWithPile = [&](const Pile& p) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x + cw);
        maxY = std::max(maxY, p.y + ch);
        for (const auto& c : p.cards) {
            minX = std::min(minX, c.x);
            minY = std::min(minY, c.y);
            maxX = std::max(maxX, c.x + cw);
            maxY = std::max(maxY, c.y + ch);
        }
    };

    if (groupByType) {
        for (const auto& p : piles) {
            if (p.type == target.type) {
                expandWithPile(p);
            }
        }
    } else {
        expandWithPile(target);
    }

    outX = minX - 4.0f;
    outY = minY - 4.0f;
    outW = (maxX - minX) + 8.0f;
    outH = (maxY - minY) + 8.0f;
}

namespace {
void drawShadowedEmber(
    eatsbits::ui::BatchRenderer2D& r,
    float x,
    float y,
    float radius,
    float cr,
    float cg,
    float cb,
    float alpha
) {
    if (radius <= 0.1f || alpha <= 0.01f) return;
    // 1. Dark espresso/soot shadow well: carves high-contrast silhouette over white/parchment cards
    r.drawCircleRadialGradient(
        x, y, radius * 2.45f,
        0.04f, 0.02f, 0.01f, alpha * 0.72f,
        0.04f, 0.02f, 0.01f, 0.0f,
        0.0f, 0.0f, 12
    );
    // 2. 3-stop ember: incandescent cream core -> theme accent ring -> soft fade
    r.drawCircleRadial3StopGradient(
        x, y, radius * 1.35f,
        1.0f, 0.98f, 0.88f, alpha,
        cr, cg, cb, alpha * 0.96f,
        cr * 0.55f, cg * 0.30f, cb * 0.08f, 0.0f,
        0.0f, 0.0f, 0.48f, 10
    );
}
} // namespace

int MagicParticleSystem::update(float dt) noexcept {
    if (dt <= 0.0f) return 0;
    int newlyActivated = 0;
    for (size_t i = 0; i < ribbons_.size();) {
        MagicArcRibbon& rib = ribbons_[i];
        if (rib.delay > 0.0f) {
            rib.delay -= dt;
            if (rib.delay <= 0.0f) {
                rib.delay = 0.0f;
            }
            ++i;
            continue;
        }
        rib.age += dt;
        if (rib.age >= rib.travelDuration + rib.fadeDuration) {
            ribbons_[i] = ribbons_.back();
            ribbons_.pop_back();
            continue;
        }
        ++i;
    }
    for (size_t i = 0; i < particles_.size();) {
        MagicParticle& p = particles_[i];
        if (p.delay > 0.0f) {
            p.delay -= dt;
            if (p.delay <= 0.0f) {
                p.delay = 0.0f;
                ++newlyActivated;
            }
            ++i;
            continue;
        }
        p.life += dt;
        if (p.life >= p.maxLife) {
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vx *= 0.96f;
        p.vy *= 0.96f;
        ++i;
    }
    return newlyActivated;
}

void MagicParticleSystem::render(eatsbits::ui::BatchRenderer2D& r) const {
    // 1. Tapered comet ribbons (drawn underneath individual embers)
    for (const auto& rib : ribbons_) {
        if (rib.delay > 0.0f || rib.travelDuration <= 0.0f) continue;

        float headT = 1.0f;
        float tailT = 0.0f;
        float headAlpha = 1.0f;
        if (rib.age <= rib.travelDuration) {
            const float u = std::clamp(rib.age / rib.travelDuration, 0.0f, 1.0f);
            headT = 1.0f - (1.0f - u) * (1.0f - u);
            tailT = std::max(0.0f, headT - 0.55f);
            headAlpha = std::clamp(u / 0.12f, 0.0f, 1.0f);
        } else {
            const float f = std::clamp((rib.age - rib.travelDuration) / std::max(0.01f, rib.fadeDuration), 0.0f, 1.0f);
            headT = 1.0f;
            tailT = 0.45f + 0.52f * f;
            headAlpha = 1.0f - f;
        }

        if (headT - tailT <= 0.01f || headAlpha <= 0.01f) continue;

        constexpr int kRibbonPts = 20;
        std::array<Point2D, kRibbonPts> pts{};
        for (int i = 0; i < kRibbonPts; ++i) {
            const float s = static_cast<float>(i) / static_cast<float>(kRibbonPts - 1);
            const float t = tailT + (headT - tailT) * s;
            const float it = 1.0f - t;
            pts[static_cast<size_t>(i)] = Point2D{
                it * it * rib.x0 + 2.0f * it * t * rib.cx + t * t * rib.x1,
                it * it * rib.y0 + 2.0f * it * t * rib.cy + t * t * rib.y1
            };
        }

        // Dark under-stroke for universal contrast on white cards and light themes
        r.drawPathGradient(
            pts.data(), pts.size(), rib.width * 1.9f,
            0.04f, 0.02f, 0.01f, 0.0f,
            0.04f, 0.02f, 0.01f, headAlpha * 0.66f,
            false, true
        );
        // Glowing theme-colored -> incandescent comet core
        r.drawPathGradient(
            pts.data(), pts.size(), rib.width,
            rib.r, rib.g, rib.b, 0.0f,
            1.0f, 0.94f, 0.72f, headAlpha * 0.96f,
            false, true
        );

        // Bright comet head leading the arc
        const Point2D& headPt = pts.back();
        drawShadowedEmber(r, headPt.x, headPt.y, rib.width * 1.05f, rib.r, rib.g, rib.b, headAlpha);
    }

    // 2. Shadowed ember particles
    for (const auto& p : particles_) {
        if (p.delay > 0.0f || p.maxLife <= 0.0f) continue;
        float t = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        float alpha = (t < 0.18f) ? (t / 0.18f) : (1.0f - (t - 0.18f) / 0.82f);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        float rad = p.size * (1.15f - 0.50f * t);
        drawShadowedEmber(r, p.x, p.y, rad, p.r, p.g, p.b, alpha);
    }
}

void MagicParticleSystem::spawnZoneBurst(float x, float y, float w, float h, float r, float g, float b) {
    auto nextF = [&]() noexcept -> float {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xFFFFu) / 65535.0f;
    };
    constexpr size_t kMaxParticles = 180;
    const int count = std::clamp(static_cast<int>((w + h) * 0.16f), 56, 144);
    const float baseSize = std::clamp((w + h) * 0.015f, 2.4f, 4.8f);
    for (int i = 0; i < count; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(count);
        float px = x, py = y, nx = 0.0f, ny = 0.0f;
        if (u < 0.25f) {
            float k = u / 0.25f;
            px = x + k * w; py = y; ny = -1.0f;
        } else if (u < 0.50f) {
            float k = (u - 0.25f) / 0.25f;
            px = x + w; py = y + k * h; nx = 1.0f;
        } else if (u < 0.75f) {
            float k = (u - 0.50f) / 0.25f;
            px = x + (1.0f - k) * w; py = y + h; ny = 1.0f;
        } else {
            float k = (u - 0.75f) / 0.25f;
            px = x; py = y + (1.0f - k) * h; nx = -1.0f;
        }
        MagicParticle p{};
        p.x = px + (nextF() - 0.5f) * 4.0f;
        p.y = py + (nextF() - 0.5f) * 4.0f;
        p.vx = nx * (14.0f + nextF() * 20.0f) + (nextF() - 0.5f) * 10.0f;
        p.vy = ny * (14.0f + nextF() * 20.0f) + (nextF() - 0.5f) * 10.0f;
        p.delay = 0.001f + nextF() * 0.16f;
        p.maxLife = 0.45f + nextF() * 0.30f;
        p.size = baseSize * (0.75f + nextF() * 0.55f);
        p.r = r; p.g = g; p.b = b;
        if (particles_.size() >= kMaxParticles) {
            particles_.erase(particles_.begin());
        }
        particles_.push_back(p);
    }
}

void MagicParticleSystem::spawnArcTrail(float x0, float y0, float x1, float y1, float r, float g, float b,
                                        float cardWidth, float startDelay, bool withRibbon) {
    auto nextF = [&]() noexcept -> float {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xFFFFu) / 65535.0f;
    };
    const float dist = std::hypot(x1 - x0, y1 - y0);
    const float midX = (x0 + x1) * 0.5f;
    const float midY = std::min(y0, y1) - std::clamp(dist * 0.26f, 24.0f, 110.0f);

    if (withRibbon) {
        constexpr size_t kMaxRibbons = 8;
        MagicArcRibbon rib{};
        rib.x0 = x0; rib.y0 = y0;
        rib.cx = midX; rib.cy = midY;
        rib.x1 = x1; rib.y1 = y1;
        rib.delay = startDelay;
        rib.travelDuration = 0.38f;
        rib.fadeDuration = 0.34f;
        rib.width = std::clamp(cardWidth * 0.062f, 3.6f, 7.2f);
        rib.r = r; rib.g = g; rib.b = b;
        if (ribbons_.size() >= kMaxRibbons) {
            ribbons_.erase(ribbons_.begin());
        }
        ribbons_.push_back(rib);
    }

    constexpr size_t kMaxParticles = 180;
    constexpr int kSteps = 28;
    const float baseSize = std::clamp(cardWidth * 0.044f, 2.5f, 5.2f);
    for (int i = 0; i <= kSteps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSteps);
        float it = 1.0f - t;
        float bx = it * it * x0 + 2.0f * it * t * midX + t * t * x1;
        float by = it * it * y0 + 2.0f * it * t * midY + t * t * y1;
        MagicParticle p{};
        p.x = bx + (nextF() - 0.5f) * 6.0f;
        p.y = by + (nextF() - 0.5f) * 6.0f;
        p.vx = (nextF() - 0.5f) * 22.0f;
        p.vy = -8.0f - nextF() * 18.0f;
        p.delay = startDelay + 0.001f + t * 0.34f;
        p.maxLife = 0.52f + nextF() * 0.24f;
        p.size = baseSize * (0.70f + (1.0f - std::abs(t - 0.5f)) * 0.60f);
        p.r = r; p.g = g; p.b = b;
        if (particles_.size() >= kMaxParticles) {
            particles_.erase(particles_.begin());
        }
        particles_.push_back(p);
    }
}

namespace {

struct VictoryAirborneCard {
    Card card;
    Card3DTransform xf;
};

struct VictorySourceCard {
    Card card;
    float startX{0.0f};
    float startY{0.0f};
    uint8_t pileIndex{0};
    size_t cardIndexInPile{0};
};

inline float resolveVictoryAmp(float amp, float extent) noexcept {
    return (amp <= 2.0f) ? (amp * extent) : amp;
}

inline uint32_t victoryHash(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

inline float victoryHashFloat(uint32_t x) noexcept {
    return static_cast<float>(victoryHash(x) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

bool buildVictoryOverrideStage(VictoryAnimOverride mode, VictoryStageSpec& outSpec) noexcept {
    if (mode == VictoryAnimOverride::GameDefault) return false;
    outSpec = VictoryStageSpec{};
    switch (mode) {
        case VictoryAnimOverride::CascadeBounce:
            outSpec.type = VictoryAnimType::CascadeBounce;
            outSpec.source = VictoryCardSource::Foundation;
            break;
        case VictoryAnimOverride::FigureEight:
            outSpec.type = VictoryAnimType::FigureEight;
            outSpec.source = VictoryCardSource::AllPiles;
            break;
        case VictoryAnimOverride::SpiralVortex:
            outSpec.type = VictoryAnimType::SpiralVortex;
            outSpec.source = VictoryCardSource::AllPiles;
            break;
        case VictoryAnimOverride::OrbitRing:
            outSpec.type = VictoryAnimType::OrbitRing;
            outSpec.source = VictoryCardSource::AllPiles;
            break;
        case VictoryAnimOverride::Fireworks:
            outSpec.type = VictoryAnimType::Fireworks;
            outSpec.source = VictoryCardSource::SyntheticDeck;
            break;
        default:
            return false;
    }
    return true;
}

bool shouldHideVictoryPileCard(const SolitaireBoard& board, const Pile& p, size_t cIdx) noexcept {
    const float vTime = board.victorySeconds();
    VictoryStageSpec overrideSpec{};
    const auto* profVic = SettingsManager::instance().getGameProfile(board.activeGameDef().id);
    const VictoryAnimOverride vicOverride = (profVic && profVic->victoryAnimOverride)
        ? *profVic->victoryAnimOverride
        : SettingsManager::instance().settings().victoryAnimOverride;
    if (buildVictoryOverrideStage(vicOverride, overrideSpec)) {
        if (overrideSpec.source == VictoryCardSource::SyntheticDeck) return false;
        if (overrideSpec.source == VictoryCardSource::AllPiles) return vTime >= overrideSpec.delay;
        if (p.type != PileType::Foundation) return false;
        const float tLaunch = (12.0f - static_cast<float>(cIdx)) * 0.16f + static_cast<float>(p.index) * 0.06f;
        return vTime >= tLaunch;
    }

    const auto& stages = board.victoryChoreography().stages;
    if (stages.empty()) {
        if (p.type != PileType::Foundation) return false;
        const float tLaunch = (12.0f - static_cast<float>(cIdx)) * 0.16f + static_cast<float>(p.index) * 0.06f;
        return vTime >= tLaunch;
    }

    for (const auto& s : stages) {
        if (vTime < s.delay) continue;
        if (s.source == VictoryCardSource::AllPiles) {
            return true;
        }
        if (s.source == VictoryCardSource::Foundation && p.type == PileType::Foundation) {
            if (s.type == VictoryAnimType::CascadeBounce) {
                const float tLaunch = s.delay + (12.0f - static_cast<float>(cIdx)) * 0.16f + static_cast<float>(p.index) * 0.06f;
                if (vTime >= tLaunch) return true;
            } else {
                return true;
            }
        }
    }
    return false;
}

std::vector<VictorySourceCard> collectVictorySourceCards(
    const SolitaireBoard& board,
    VictoryCardSource source,
    float cw,
    float ch,
    float width,
    float height,
    int32_t countOverride) {
    std::vector<VictorySourceCard> cards;
    if (source != VictoryCardSource::SyntheticDeck) {
        for (const auto& p : board.piles()) {
            if (source == VictoryCardSource::Foundation && p.type != PileType::Foundation) continue;
            for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
                VictorySourceCard sc{};
                sc.card = p.cards[cIdx];
                sc.card.faceUp = true;
                sc.card.flipAngle = 0.0f;
                sc.startX = p.x + cw * 0.5f;
                sc.startY = p.y + ch * 0.5f;
                sc.pileIndex = p.index;
                sc.cardIndexInPile = cIdx;
                cards.push_back(sc);
            }
        }
    }

    if (cards.empty() || source == VictoryCardSource::SyntheticDeck) {
        cards.clear();
        const size_t total = (countOverride > 0) ? static_cast<size_t>(countOverride) : 52u;
        cards.reserve(total);
        for (size_t i = 0; i < total; ++i) {
            VictorySourceCard sc{};
            sc.card.suit = static_cast<Suit>((i / 13u) % 4u);
            sc.card.rank = static_cast<Rank>(1u + (i % 13u));
            sc.card.faceUp = true;
            sc.card.flipAngle = 0.0f;
            sc.startX = width * 0.5f;
            sc.startY = height + ch * 0.45f;
            sc.pileIndex = static_cast<uint8_t>((i / 13u) % 4u);
            sc.cardIndexInPile = i % 13u;
            cards.push_back(sc);
        }
    } else if (countOverride > 0 && static_cast<size_t>(countOverride) < cards.size()) {
        cards.resize(static_cast<size_t>(countOverride));
    }
    return cards;
}

void evaluateVictoryStage(
    const VictoryStageSpec& spec,
    const SolitaireBoard& board,
    float vTime,
    float width,
    float height,
    float cw,
    float ch,
    std::vector<VictoryAirborneCard>& outAirborne) {
    const float stageTime = vTime - spec.delay;
    if (stageTime < 0.0f) return;
    if (spec.duration > 0.0f && stageTime > spec.duration) return;

    const float floorY = height - ch * 0.75f;
    const float centerX = width * 0.5f;
    const float centerY = height * 0.48f;
    const float ax = resolveVictoryAmp(spec.ampX, width);
    const float ay = resolveVictoryAmp(spec.ampY, height);

    if (spec.type == VictoryAnimType::Fireworks) {
        const auto pool = collectVictorySourceCards(board, spec.source, cw, ch, width, height, 0);
        const int32_t rocketCount = (spec.count > 0) ? spec.count : 4;
        const float speed = std::max(0.2f, spec.speed);
        const float tBurst = 0.68f / speed;
        const float cycleDur = spec.loop ? (2.55f / speed) : 10000.0f;

        for (int32_t rIdx = 0; rIdx < rocketCount; ++rIdx) {
            const float tRocket = stageTime - static_cast<float>(rIdx) * spec.stagger;
            if (tRocket < 0.0f) continue;

            const uint32_t burstCycle = spec.loop ? static_cast<uint32_t>(tRocket / cycleDur) : 0u;
            const float tLocal = spec.loop ? std::fmod(tRocket, cycleDur) : tRocket;
            const uint32_t seedBase = static_cast<uint32_t>(rIdx) * 977u + burstCycle * 7919u + 131u;

            const float launchX = width * (0.18f + 0.64f * victoryHashFloat(seedBase + 1u));
            const float launchY = height + ch * 0.45f;
            const float apexX = width * (0.20f + 0.60f * victoryHashFloat(seedBase + 2u));
            const float apexY = height * (0.18f + 0.28f * victoryHashFloat(seedBase + 3u));

            if (tLocal < tBurst) {
                const float u = std::clamp(tLocal / tBurst, 0.0f, 1.0f);
                const float ease = 1.0f - (1.0f - u) * (1.0f - u);
                const float posX = launchX + (apexX - launchX) * ease;
                const float posY = launchY + (apexY - launchY) * ease;

                VictoryAirborneCard vac{};
                vac.card = pool[static_cast<size_t>(seedBase) % pool.size()].card;
                vac.xf.baseW = cw;
                vac.xf.baseH = ch;
                vac.xf.cx = posX;
                vac.xf.cy = posY;
                vac.xf.cz = 45.0f + u * 55.0f;
                vac.xf.yaw = tLocal * 8.0f * spec.spin;
                vac.xf.pitch = 0.35f;
                vac.xf.roll = std::atan2(apexY - launchY, apexX - launchX) + 1.5707963f;
                outAirborne.push_back(vac);
            } else {
                const float tExp = tLocal - tBurst;
                const float dragK = 1.45f;
                const float dragSpan = (1.0f - std::exp(-dragK * tExp)) / dragK;
                const float gravDrop = 0.5f * spec.gravity * tExp * tExp;

                for (int32_t b = 0; b < spec.burstCount; ++b) {
                    const uint32_t bSeed = seedBase + static_cast<uint32_t>(b + 1) * 313u;
                    const float theta = (6.2831853f * static_cast<float>(b)) / static_cast<float>(spec.burstCount)
                                      + (victoryHashFloat(bSeed + 1u) - 0.5f) * 0.35f;
                    const float speedMag = (240.0f + 220.0f * victoryHashFloat(bSeed + 2u)) * speed;
                    const float zDir = (victoryHashFloat(bSeed + 3u) - 0.35f) * 2.0f;

                    const float vx = std::cos(theta) * speedMag;
                    const float vy = std::sin(theta) * speedMag - 90.0f;
                    const float vz = zDir * 140.0f;

                    const float posX = apexX + vx * dragSpan;
                    const float posY = apexY + vy * dragSpan + gravDrop;
                    if (posY > height + ch * 1.4f) continue;

                    VictoryAirborneCard vac{};
                    vac.card = pool[(static_cast<size_t>(bSeed)) % pool.size()].card;
                    vac.xf.baseW = cw;
                    vac.xf.baseH = ch;
                    vac.xf.cx = posX;
                    vac.xf.cy = posY;
                    vac.xf.cz = std::max(5.0f, 80.0f + vz * dragSpan);
                    vac.xf.yaw = tExp * (3.2f + victoryHashFloat(bSeed + 4u) * 4.5f) * spec.spin;
                    vac.xf.pitch = std::sin(tExp * 4.0f + static_cast<float>(b)) * 0.55f;
                    vac.xf.roll = std::cos(tExp * 3.5f + static_cast<float>(b)) * 0.55f;
                    outAirborne.push_back(vac);
                }
            }
        }
        return;
    }

    const auto sourceCards = collectVictorySourceCards(board, spec.source, cw, ch, width, height, spec.count);
    const size_t totalCards = sourceCards.size();
    for (size_t i = 0; i < totalCards; ++i) {
        const auto& sc = sourceCards[i];
        const float tLaunch = (spec.type == VictoryAnimType::CascadeBounce && spec.source == VictoryCardSource::Foundation)
                            ? ((12.0f - static_cast<float>(sc.cardIndexInPile)) * 0.16f + static_cast<float>(sc.pileIndex) * 0.06f)
                            : (static_cast<float>(i) * spec.stagger);
        if (stageTime < tLaunch) continue;

        const float dt = stageTime - tLaunch;
        const uint32_t cardId = static_cast<uint32_t>(sc.pileIndex * 13 + sc.cardIndexInPile + i);

        VictoryAirborneCard vac{};
        vac.card = sc.card;

        Card3DTransform xf{};
        xf.baseW = cw;
        xf.baseH = ch;

        if (spec.type == VictoryAnimType::CascadeBounce) {
            const float vx = ((static_cast<float>(sc.pileIndex) - 1.5f) * 170.0f) + std::sin(static_cast<float>(cardId) * 2.3f) * 85.0f;
            const float rawX = sc.startX + vx * dt * spec.speed;

            const float minX = cw * 0.6f;
            const float maxX = width - cw * 0.6f;
            const float spanX = std::max(10.0f, maxX - minX);
            float normX = std::fmod(std::abs(rawX - minX), spanX * 2.0f);
            if (normX > spanX) normX = spanX * 2.0f - normX;
            const float posX = minX + normX;

            const float bouncePeriod = 1.45f / std::max(0.2f, spec.speed);
            const float cycle = std::fmod(dt, bouncePeriod) / bouncePeriod;
            const int bounceNum = static_cast<int>(dt / bouncePeriod);
            const float decay = std::max(0.25f, 1.0f - static_cast<float>(bounceNum) * 0.12f);
            const float apexH = (decay * 340.0f) + 60.0f;
            float posY = floorY - 4.0f * apexH * cycle * (1.0f - cycle);
            if (dt < 0.35f) {
                const float launchT = dt / 0.35f;
                posY = sc.startY + (posY - sc.startY) * launchT;
            }

            xf.cx = posX;
            xf.cy = posY;
            xf.cz = 45.0f + std::sin(dt * 3.6f + static_cast<float>(cardId)) * 35.0f + (1.0f - cycle) * 30.0f;
            xf.yaw = dt * (3.5f + static_cast<float>(cardId % 4) * 0.6f) * spec.spin;
            xf.pitch = 0.22f + std::sin(dt * 4.0f + static_cast<float>(cardId) * 0.6f) * 0.40f;
            xf.roll = std::cos(dt * 3.0f + static_cast<float>(cardId) * 0.8f) * 0.30f;
        } else if (spec.type == VictoryAnimType::FigureEight) {
            const float phase = dt * spec.speed * 1.45f - static_cast<float>(i) * 0.22f;
            const float targetX = centerX + ax * std::sin(phase);
            const float targetY = centerY + ay * std::sin(2.0f * phase);
            const float blend = std::clamp(dt / 0.45f, 0.0f, 1.0f);
            const float ease = blend * blend * (3.0f - 2.0f * blend);
            xf.cx = sc.startX + (targetX - sc.startX) * ease;
            xf.cy = sc.startY + (targetY - sc.startY) * ease;
            xf.cz = 55.0f + 42.0f * std::cos(phase);
            xf.yaw = dt * 2.2f * spec.spin + std::cos(phase) * 0.45f;
            xf.pitch = 0.25f * std::sin(2.0f * phase);
            xf.roll = 0.35f * std::cos(phase);
        } else if (spec.type == VictoryAnimType::SpiralVortex) {
            const float theta = dt * spec.speed * 2.2f + static_cast<float>(i) * 0.35f;
            const float radialOsc = 0.22f + 0.78f * (0.5f + 0.5f * std::sin(dt * spec.speed * 0.9f + static_cast<float>(i) * 0.16f));
            const float targetX = centerX + ax * radialOsc * std::cos(theta);
            const float targetY = centerY + ay * radialOsc * std::sin(theta);
            const float blend = std::clamp(dt / 0.45f, 0.0f, 1.0f);
            const float ease = blend * blend * (3.0f - 2.0f * blend);
            xf.cx = sc.startX + (targetX - sc.startX) * ease;
            xf.cy = sc.startY + (targetY - sc.startY) * ease;
            xf.cz = 40.0f + 75.0f * (1.0f - radialOsc);
            xf.yaw = theta * 0.7f * spec.spin;
            xf.pitch = 0.35f * std::sin(theta);
            xf.roll = theta + 1.5707963f;
        } else { // OrbitRing
            const float denom = static_cast<float>(std::max<size_t>(1u, totalCards));
            const float theta = dt * spec.speed * 1.5f + (6.2831853f * static_cast<float>(i)) / denom;
            const float targetX = centerX + ax * std::cos(theta);
            const float targetY = centerY + ay * 0.55f * std::sin(theta);
            const float blend = std::clamp(dt / 0.5f, 0.0f, 1.0f);
            const float ease = blend * blend * (3.0f - 2.0f * blend);
            xf.cx = sc.startX + (targetX - sc.startX) * ease;
            xf.cy = sc.startY + (targetY - sc.startY) * ease;
            xf.cz = 75.0f + 55.0f * std::sin(theta);
            xf.yaw = -theta * spec.spin;
            xf.pitch = 0.30f;
            xf.roll = 0.18f * std::cos(theta);
        }

        vac.xf = xf;
        outAirborne.push_back(vac);
    }
}

void updateShowcaseCamera(
    ShowcaseCameraState& cam,
    const SolitaireBoard& board,
    const AutoPlayController& autoPlay,
    int highlightedHelpZone,
    float width,
    float height,
    float dt,
    int directorFocusZone = -1,
    int directorMoveFrom = -1,
    int directorMoveCard = -1,
    int directorMoveTo = -1
) noexcept {
    if (width <= 1.0f || height <= 1.0f) return;

    if (cam.focusX <= 0.0f && cam.focusY <= 0.0f) {
        cam.focusX = width * 0.5f;
        cam.focusY = height * 0.5f;
        cam.targetFocusX = cam.focusX;
        cam.targetFocusY = cam.focusY;
    }
    if (cam.userFocusX <= 0.0f && cam.userFocusY <= 0.0f) {
        cam.userFocusX = width * 0.5f;
        cam.userFocusY = height * 0.5f;
    }

    const auto& piles = board.piles();
    const float cw = board.cardWidth();
    const float ch = board.cardHeight();

    const SolverMove* activeMove = autoPlay.isPlaying() ? autoPlay.peekNextMove() : nullptr;
    SolverMove dirSynthMove{};
    if (!activeMove && directorMoveFrom >= 0 && directorMoveTo >= 0) {
        dirSynthMove.fromPile = static_cast<uint8_t>(directorMoveFrom);
        dirSynthMove.cardIndex = static_cast<uint8_t>(std::max(0, directorMoveCard));
        dirSynthMove.toPile = static_cast<uint8_t>(directorMoveTo);
        activeMove = &dirSynthMove;
    }

    if (activeMove) {
        const SolverMove* mv = activeMove;
        cam.focusPileA = static_cast<int>(mv->fromPile);
        cam.focusCardA = static_cast<int>(mv->cardIndex);
        cam.focusPileB = static_cast<int>(mv->toPile);
        cam.targetSpotlightAlpha = 0.42f;

        if (mv->fromPile < piles.size() && mv->toPile < piles.size()) {
            const Pile& sp = piles[mv->fromPile];
            const Pile& dp = piles[mv->toPile];
            float sx0 = sp.x, sy0 = sp.y, sx1 = sp.x + cw, sy1 = sp.y + ch;
            if (!sp.cards.empty()) {
                const size_t idx = std::min<size_t>(mv->cardIndex, sp.cards.size() - 1);
                sx0 = sp.cards[idx].x;
                sy0 = sp.cards[idx].y;
                sx1 = sx0 + cw;
                sy1 = sp.cards.back().y + ch;
            }
            float dx0 = dp.x, dy0 = dp.y, dx1 = dp.x + cw, dy1 = dp.y + ch;
            if (!dp.cards.empty()) {
                dx0 = dp.cards.back().x;
                dy0 = dp.cards.back().y;
                dx1 = dx0 + cw;
                dy1 = dy0 + ch;
            }
            const float minX = std::min(sx0, dx0);
            const float minY = std::min(sy0, dy0);
            const float maxX = std::max(sx1, dx1);
            const float maxY = std::max(sy1, dy1);
            const float spanW = (maxX - minX) + cw * 1.4f;
            const float spanH = (maxY - minY) + ch * 1.2f;
            const float fitZoom = std::min(width / std::max(spanW, 1.0f), height / std::max(spanH, 1.0f));
            cam.targetZoom = std::clamp(fitZoom, 1.0f, 1.26f);

            const float halfW = (width * 0.5f) / cam.targetZoom;
            const float halfH = (height * 0.5f) / cam.targetZoom;
            const float cx = (minX + maxX) * 0.5f;
            const float cy = (minY + maxY) * 0.5f;
            cam.targetFocusX = (halfW < width * 0.5f) ? std::clamp(cx, halfW, width - halfW) : (width * 0.5f);
            cam.targetFocusY = (halfH < height * 0.5f) ? std::clamp(cy, halfH, height - halfH) : (height * 0.5f);
        }
    } else if (directorFocusZone >= 0 && static_cast<size_t>(directorFocusZone) < piles.size()) {
        float zx0 = 0.0f, zy0 = 0.0f, zw = 0.0f, zh = 0.0f;
        computeZoneHighlightBounds(board, directorFocusZone, zx0, zy0, zw, zh);
        if (zw > 0.0f && zh > 0.0f) {
            const float spanW = zw + cw * 1.5f;
            const float spanH = zh + ch * 1.2f;
            const float fitZoom = std::min(width / std::max(spanW, 1.0f), height / std::max(spanH, 1.0f));
            cam.targetZoom = std::clamp(fitZoom, 1.0f, 1.22f);
            const float halfW = (width * 0.5f) / cam.targetZoom;
            const float halfH = (height * 0.5f) / cam.targetZoom;
            const float cx = zx0 + zw * 0.5f;
            const float cy = zy0 + zh * 0.5f;
            cam.targetFocusX = (halfW < width * 0.5f) ? std::clamp(cx, halfW, width - halfW) : (width * 0.5f);
            cam.targetFocusY = (halfH < height * 0.5f) ? std::clamp(cy, halfH, height - halfH) : (height * 0.5f);
        } else {
            cam.targetZoom = 1.0f;
            cam.targetFocusX = width * 0.5f;
            cam.targetFocusY = height * 0.5f;
        }
        cam.targetSpotlightAlpha = 0.36f;
        cam.focusPileA = directorFocusZone;
        cam.focusCardA = 0;
        cam.focusPileB = -1;
    } else if (highlightedHelpZone >= 0 && static_cast<size_t>(highlightedHelpZone) < piles.size()) {
        cam.targetZoom = 1.0f;
        cam.targetFocusX = width * 0.5f;
        cam.targetFocusY = height * 0.5f;
        cam.targetSpotlightAlpha = 0.34f;
        cam.focusPileA = highlightedHelpZone;
        cam.focusCardA = 0;
        cam.focusPileB = -1;
    } else {
        cam.userZoom = std::clamp(cam.userZoom, 1.0f, 3.0f);
        if (cam.userZoom <= 1.002f) {
            cam.userZoom = 1.0f;
            cam.userFocusX = width * 0.5f;
            cam.userFocusY = height * 0.5f;
        } else {
            const float halfW = (width * 0.5f) / cam.userZoom;
            const float halfH = (height * 0.5f) / cam.userZoom;
            cam.userFocusX = (halfW < width * 0.5f) ? std::clamp(cam.userFocusX, halfW, width - halfW) : (width * 0.5f);
            cam.userFocusY = (halfH < height * 0.5f) ? std::clamp(cam.userFocusY, halfH, height - halfH) : (height * 0.5f);
        }
        cam.targetZoom = cam.userZoom;
        cam.targetFocusX = cam.userFocusX;
        cam.targetFocusY = cam.userFocusY;
        cam.targetSpotlightAlpha = 0.0f;
        cam.focusPileA = -1;
        cam.focusCardA = -1;
        cam.focusPileB = -1;
    }

    const float dampRate = (cam.userPanActive && !activeMove && directorFocusZone < 0 && highlightedHelpZone < 0)
        ? 24.0f
        : ((activeMove || directorFocusZone >= 0 || highlightedHelpZone >= 0) ? 6.5f : 12.0f);
    const float k = 1.0f - std::exp(-dampRate * std::clamp(dt, 0.0f, 0.05f));
    cam.zoom += (cam.targetZoom - cam.zoom) * k;
    cam.focusX += (cam.targetFocusX - cam.focusX) * k;
    cam.focusY += (cam.targetFocusY - cam.focusY) * k;
    cam.spotlightAlpha += (cam.targetSpotlightAlpha - cam.spotlightAlpha) * k;
}

struct DirectorRuntimeState {
    bool active{false};
    bool finished{false};
    std::vector<DirectorBeat> beats;
    size_t beatIdx{0};
    float beatTimer{0.0f};
    float totalElapsed{0.0f};
    int movesDoneInBeat{0};
    float nextMoveAt{0.0f};
    int highlightZone{-1};
    int moveFocusFrom{-1};
    int moveFocusCard{-1};
    int moveFocusTo{-1};
    float moveFocusTimer{0.0f};
};

bool loadDirectorTimeline(const std::string& path, std::vector<DirectorBeat>& outBeats) {
    outBeats.clear();
    if (path.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(f);
        return false;
    }
    std::string content(static_cast<size_t>(sz), '\0');
    if (std::fread(content.data(), 1, content.size(), f) != content.size()) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        // Format: BEAT|action|durationSec|targetZone|moveCount|wavPath|caption
        std::vector<std::string> fields;
        size_t fStart = 0;
        while (fStart <= line.size()) {
            size_t sep = line.find('|', fStart);
            if (sep == std::string::npos) {
                fields.push_back(line.substr(fStart));
                break;
            }
            fields.push_back(line.substr(fStart, sep - fStart));
            fStart = sep + 1;
        }
        if (fields.size() < 6 || fields[0] != "BEAT") continue;

        DirectorBeat b;
        if (fields[1] == "highlight_zone") b.action = DirectorAction::HighlightZone;
        else if (fields[1] == "play_moves") b.action = DirectorAction::PlayMoves;
        else if (fields[1] == "reset_camera") b.action = DirectorAction::ResetCamera;
        else if (fields[1] == "finish_game") b.action = DirectorAction::FinishGame;
        else if (fields[1] == "move") b.action = DirectorAction::ExplicitMove;
        else if (fields[1] == "draw_stock") b.action = DirectorAction::DrawStock;
        else if (fields[1] == "hint") b.action = DirectorAction::Hint;
        else b.action = DirectorAction::Idle;

        b.durationSec = std::max(0.2f, std::stof(fields[2]));
        b.targetZone = std::stoi(fields[3]);
        b.moveCount = std::max(0, std::stoi(fields[4]));
        b.wavPath = fields[5];
        if (fields.size() > 6) b.caption = fields[6];
        if (fields.size() > 7) b.fromPile = std::stoi(fields[7]);
        if (fields.size() > 8) b.cardIndex = std::stoi(fields[8]);
        if (fields.size() > 9) b.toPile = std::stoi(fields[9]);
        if (fields.size() > 10) b.ghostCards = fields[10];
        if (fields.size() > 11 && !fields[11].empty()) b.ghostDelaySec = std::max(0.0f, std::stof(fields[11]));
        if (fields.size() > 12 && !fields[12].empty()) b.ghostPile = std::stoi(fields[12]);
        outBeats.push_back(std::move(b));
    }
    return !outBeats.empty();
}

void evaluateActiveVictoryAirborne(
    const SolitaireBoard& board,
    float width,
    float height,
    std::vector<VictoryAirborneCard>& outAirborne) {
    const float cw = board.cardWidth();
    const float ch = board.cardHeight();
    const float vTime = board.victorySeconds();
    VictoryStageSpec overrideSpec{};
    const auto* profVic = SettingsManager::instance().getGameProfile(board.activeGameDef().id);
    const VictoryAnimOverride vicOverride = (profVic && profVic->victoryAnimOverride)
        ? *profVic->victoryAnimOverride
        : SettingsManager::instance().settings().victoryAnimOverride;
    if (buildVictoryOverrideStage(vicOverride, overrideSpec)) {
        evaluateVictoryStage(overrideSpec, board, vTime, width, height, cw, ch, outAirborne);
    } else {
        const auto& stages = board.victoryChoreography().stages;
        if (stages.empty()) {
            VictoryStageSpec defaultStage{};
            evaluateVictoryStage(defaultStage, board, vTime, width, height, cw, ch, outAirborne);
        } else {
            for (const auto& stage : stages) {
                evaluateVictoryStage(stage, board, vTime, width, height, cw, ch, outAirborne);
            }
        }
    }
    std::stable_sort(outAirborne.begin(), outAirborne.end(), [](const VictoryAirborneCard& a, const VictoryAirborneCard& b) noexcept {
        return a.xf.cz < b.xf.cz - 0.01f;
    });
}

} // namespace

void TableRenderer::renderBoard(
    eatsbits::ui::BatchRenderer2D& r,
    const SolitaireBoard& board,
    float width,
    float height,
    int draggedPile,
    int draggedCardIdx,
    float dragOffsetX,
    float dragOffsetY,
    int hoveredButtonId,
    bool /*menuOpen*/,
    MenuAnchor menuAnchor,
    bool menuFlowVertical,
    bool isDraggingMenu,
    MenuAnchor dragPreviewAnchor,
    bool dragPreviewFlowVertical,
    bool isGesturing,
    float gestureStartX,
    float gestureStartY,
    float gestureCurX,
    float gestureCurY,
    const eatsbits::ui::LightSource2D* dynamicLight,
    const class AutoPlayController* autoPlay,
    bool helpOpen,
    int highlightedHelpZone,
    float helpScrollY,
    float helpMouseX,
    float helpMouseY,
    const HelpOddsInfo* helpOdds,
    const MagicParticleSystem* magicParticles,
    const HintHighlight* hintHighlight,
    bool screensaverMode,
    const ShowcaseCameraState* showcaseCamera,
    bool hideHud,
    float dragPivotAngle,
    float dragGrabLocalX,
    float dragGrabLocalY,
    const float* dragChainAngles,
    size_t dragChainCount,
    const std::vector<GhostCardCue>* ghostCards,
    const HelpVideoThumb* helpVideoThumb,
    const VictoryGatherState* victoryGather
) {
    const ThemePalette pal = getPalette(board.tableTheme());
    // Overhead table illumination: dynamic swaying light or centered static default
    const eatsbits::ui::LightSource2D light = dynamicLight ? *dynamicLight : eatsbits::ui::LightSource2D{width * 0.5f, height * 0.28f, 550.0f, 1.0f};

    if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().isAtlasDirty()) {
        CardTextureCache::instance().buildGpuAtlas(r);
    }

    // Activate physical 3D scene illumination & natural light-source vignette for tabletop and cards
    r.setSceneLightingActive(true);

    const bool camZoomed = (showcaseCamera && showcaseCamera->zoom > 1.002f);
    if (camZoomed) {
        r.setCameraTransform(showcaseCamera->zoom, showcaseCamera->focusX, showcaseCamera->focusY, width * 0.5f, height * 0.5f);
    }

    // 1. Static Table Chrome (Recorded once into FBO cache and blitted via fast memcpy!)
    static TableThemeId s_cachedTheme = static_cast<TableThemeId>(-1);
    static GameVariant s_cachedVariant = GameVariant::KlondikeDraw1;
    static float s_cachedW = 0.0f;
    static float s_cachedH = 0.0f;
    static bool s_cachedMarkings = true;

    const auto* profMark = SettingsManager::instance().getGameProfile(board.activeGameDef().id);
    const bool showMarkings = (profMark && profMark->showTableMarkings)
        ? *profMark->showTableMarkings
        : SettingsManager::instance().settings().showTableMarkings;

    const bool themeOrSizeChanged =
        (s_cachedTheme != board.tableTheme() ||
         std::abs(s_cachedW - width) > 0.5f ||
         std::abs(s_cachedH - height) > 0.5f);
    if (themeOrSizeChanged ||
        s_cachedVariant != board.variant() ||
        s_cachedMarkings != showMarkings) {
        r.invalidateChromeCache();
        if (themeOrSizeChanged) {
            ProceduralBackgroundSystem::instance().invalidateCache();
        }
        s_cachedTheme = board.tableTheme();
        s_cachedVariant = board.variant();
        s_cachedMarkings = showMarkings;
        s_cachedW = width;
        s_cachedH = height;
    }

    if (!camZoomed && r.isChromeCacheValid()) {
        r.drawCachedChrome(1.0f);
    } else if (!camZoomed && r.beginChromeRecording()) {
        drawStaticTableChrome(r, board, width, height, pal, light);
        r.endChromeRecording();
    } else {
        drawStaticTableChrome(r, board, width, height, pal, light);
    }

    // 2. Bottom-Center Live Telemetry Watermark (Seed, Score, Moves, Timer)
    if (showMarkings && !hideHud) {
        int mins = static_cast<int>(board.elapsedSeconds()) / 60;
        int secs = static_cast<int>(board.elapsedSeconds()) % 60;
        char botTelemetry[256];
        if (width >= 720.0f) {
            std::snprintf(botTelemetry, sizeof(botTelemetry), "SEED #%u    SCORE: %d    MOVES: %u    %02d:%02d",
                          board.seed(), board.score(), board.moveCount(), mins, secs);
        } else {
            std::snprintf(botTelemetry, sizeof(botTelemetry), "SCORE: %d    MOVES: %u    %02d:%02d",
                          board.score(), board.moveCount(), mins, secs);
        }
        const auto& bb = board.blackboard();
        if (!bb.intStats.empty()) {
            size_t len = std::strlen(botTelemetry);
            int shown = 0;
            for (const auto& [k, v] : bb.intStats) {
                if (shown >= 4 || len + 32 >= sizeof(botTelemetry)) break;
                int wrote = std::snprintf(botTelemetry + len, sizeof(botTelemetry) - len, "    %s: %d", k.c_str(), v);
                if (wrote > 0) len += static_cast<size_t>(wrote);
                ++shown;
            }
        }
        const float teleLen = static_cast<float>(std::strlen(botTelemetry)) * 6.2f;
        const float teleX = std::max(12.0f + board.safeLeft(), (width - teleLen) * 0.5f);
        eatsbits::ui::glyph::drawVectorText(r, botTelemetry, teleX, height - 16.0f - board.safeBottom(), 11.5f,
                                            pal.trimR, pal.trimG, pal.trimB, 0.52f);
    }

    // 3. Resting Cards (culls buried invisible cards in Stock/Waste/Foundation!)
    const float cw = board.cardWidth();
    const float ch = board.cardHeight();
    const auto& piles = board.piles();

    auto isCardMoving = [](const Card& c) noexcept -> bool {
        const float dx = c.x - c.targetX;
        const float dy = c.y - c.targetY;
        const float da = c.angle - c.targetAngle;
        return (dx * dx + dy * dy > 0.25f) || (std::abs(da) > 0.003f) || (c.elevation > 0.15f) || (c.flipAngle > 0.005f);
    };

    struct AnimatingCardEntry {
        size_t pileIdx;
        size_t cardIdx;
        const Card* card;
    };
    thread_local std::vector<AnimatingCardEntry> animatingCards;
    animatingCards.clear();
    struct RestingPileRenderData {
        size_t pIdx{0};
        size_t stackCount{0};
        bool isTightlyStacked{false};
        std::vector<size_t> restingIndices;
    };
    thread_local std::vector<RestingPileRenderData> restingPiles;
    size_t restingPilesCount = 0;
    thread_local std::vector<size_t> drawOrder;
    drawOrder.resize(piles.size());
    for (size_t i = 0; i < piles.size(); ++i) drawOrder[i] = i;
    std::stable_sort(drawOrder.begin(), drawOrder.end(), [&piles](size_t a, size_t b) noexcept {
        return piles[a].zIndex < piles[b].zIndex;
    });

    for (size_t pIdx : drawOrder) {
        const Pile& p = piles[pIdx];
        if (p.cards.empty()) continue;

        size_t startIdx = 0;
        if (p.type == PileType::Stock) {
            // Render up to top 5 cards to show the stratified physical deck edge thickness
            startIdx = (p.cards.size() > 5) ? (p.cards.size() - 5) : 0;
        } else if (p.type == PileType::Foundation) {
            // Render up to top 4 cards to show stratified foundation stack depth
            startIdx = (p.cards.size() > 4) ? (p.cards.size() - 4) : 0;
        } else if (p.type == PileType::FreeCell) {
            startIdx = (p.cards.size() > 2) ? (p.cards.size() - 2) : 0;
        } else if (p.type == PileType::Waste) {
            // At most top 4 cards are visible during Draw-3 fan
            startIdx = (p.cards.size() > 4) ? (p.cards.size() - 4) : 0;
        }

        if (restingPilesCount >= restingPiles.size()) {
            restingPiles.emplace_back();
        }
        RestingPileRenderData& rp = restingPiles[restingPilesCount];
        rp.pIdx = pIdx;
        rp.isTightlyStacked = (p.type == PileType::Stock || p.type == PileType::Foundation || p.type == PileType::FreeCell);
        rp.restingIndices.clear();
        size_t totalRestingCount = 0;
        bool pileHasMovingBelow = false;
        for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
            if (static_cast<int>(pIdx) == draggedPile &&
                draggedCardIdx >= 0 &&
                static_cast<int>(cIdx) >= draggedCardIdx) {
                continue;
            }
            if (victoryGather && victoryGather->active) {
                continue; // All cards are gathering back to deal origin
            }
            if (board.isVictory() && shouldHideVictoryPileCard(board, p, cIdx)) {
                continue; // Card launched into 3D victory animation
            }
            const Card& c = p.cards[cIdx];
            if (isCardMoving(c) || pileHasMovingBelow) {
                pileHasMovingBelow = true;
                animatingCards.push_back(AnimatingCardEntry{pIdx, cIdx, &c});
            } else {
                ++totalRestingCount;
                if (cIdx >= startIdx) {
                    rp.restingIndices.push_back(cIdx);
                }
            }
        }
        rp.stackCount = totalRestingCount;
        if (!rp.restingIndices.empty()) {
            ++restingPilesCount;
        }
    }

    // Pass A: Render ALL resting pile table drop shadows in a single unified Max-blend shadow layer
    if (restingPilesCount > 0) {
        r.beginShadowLayer();
        for (size_t rpIdx = 0; rpIdx < restingPilesCount; ++rpIdx) {
            const auto& rp = restingPiles[rpIdx];
            const Pile& p = piles[rp.pIdx];
            for (size_t i = 0; i < rp.restingIndices.size(); ++i) {
                if (i > 0 && rp.isTightlyStacked) {
                    continue; // Tightly stacked deck/foundation piles emit unified table shadows
                }
                const Card& c = p.cards[rp.restingIndices[i]];
                if (rp.isTightlyStacked && rp.stackCount > 1) {
                    // Continuous multi-slice stack shadow:
                    // 1. Crisp, dark grounding contact shadow at base elevation
                    drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light, CardRenderPass::ShadowOnly);
                    // 2. Stepped intermediate + top penumbra slices forming a seamless 3D prism shadow
                    const float stackElevation = std::min(18.0f, static_cast<float>(rp.stackCount) * 0.38f);
                    const int slices = (stackElevation > 8.0f) ? 3 : 2;
                    for (int s = 1; s <= slices; ++s) {
                        const float frac = static_cast<float>(s) / static_cast<float>(slices);
                        Card sliceCard = c;
                        sliceCard.elevation = c.elevation + stackElevation * frac;
                        drawProceduralCard(r, sliceCard, sliceCard.x, sliceCard.y, cw, ch, pal, light, CardRenderPass::ShadowOnly);
                    }
                } else {
                    drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light, CardRenderPass::ShadowOnly);
                }
            }
        }
        r.endShadowLayer();
    }

    // Pass B: Render physical deck rims, inter-card contact shadows, and resting card bodies
    for (size_t rpIdx = 0; rpIdx < restingPilesCount; ++rpIdx) {
        const auto& rp = restingPiles[rpIdx];
        const Pile& p = piles[rp.pIdx];
        if (rp.isTightlyStacked && rp.stackCount >= 2 && !rp.restingIndices.empty()) {
            const Card& topCard = p.cards[rp.restingIndices.back()];
            drawPhysicalDeckRim(r, topCard, cw, ch, rp.stackCount, light);
        }

        for (size_t i = 0; i < rp.restingIndices.size(); ++i) {
            const Card& c = p.cards[rp.restingIndices[i]];
            if (i > 0 && (p.type == PileType::Tableau || p.type == PileType::Waste)) {
                // Inter-card contact shadow: card i rests physically on top of card i-1,
                // casting a tight, crisp contact shadow (gap ~0.3mm) across card i-1's face.
                drawInterCardContactShadow(r, c, c.x, c.y, cw, ch, light);
            }
            drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light, CardRenderPass::BodyOnly);
        }
    }

    // 3b. Showcase Spotlight Scrim & Focused Pile Lift
    if (showcaseCamera && showcaseCamera->spotlightAlpha > 0.01f) {
        r.drawRect(-width, -height, width * 3.0f, height * 3.0f, 0.0f, 0.0f, 0.0f, showcaseCamera->spotlightAlpha);

        const float effRu = CardTextureCache::instance().effectiveRu();
        const float cardRad = (effRu <= 0.001f) ? 2.0f : std::max(2.0f, effRu * cw);

        auto drawFocusPile = [&](int pIdx, int fromCardIdx) {
            if (pIdx < 0 || static_cast<size_t>(pIdx) >= piles.size()) return;
            const Pile& fp = piles[static_cast<size_t>(pIdx)];
            if (fp.cards.empty()) {
                ScopedCardRotation rotGuard(r, fp.angle, fp.x + cw * 0.5f, fp.y + ch * 0.5f);
                r.drawRoundedRectOutline(fp.x - 2.0f, fp.y - 2.0f, cw + 4.0f, ch + 4.0f, cardRad + 2.0f,
                                         pal.accentR, pal.accentG, pal.accentB, 0.85f, 2.2f, 4);
                return;
            }
            const bool tightlyStacked = (fp.type == PileType::Stock || fp.type == PileType::Foundation || fp.type == PileType::FreeCell);
            size_t startIdx = (tightlyStacked || fromCardIdx < 0)
                ? (fp.cards.size() - 1)
                : std::min<size_t>(static_cast<size_t>(fromCardIdx), fp.cards.size() - 1);

            const CardWearLevel wearLvl = CardTextureCache::instance().cardWearLevel();

            // Draw all card outlines underneath the card bodies so overlapping cards in a
            // highlighted run occlude interior borders, leaving only the outer stack perimeter.
            for (size_t ci = startIdx; ci < fp.cards.size(); ++ci) {
                const Card& fc = fp.cards[ci];
                if (isCardMoving(fc)) continue;
                const CardScreenRect rect = computeCardScreenRect(fc, fc.x, fc.y, cw, ch, wearLvl);
                ScopedCardRotation rotGuard(r, fc.angle, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);
                r.drawRoundedRectOutline(rect.x - 2.0f, rect.y - 2.0f, rect.w + 4.0f, rect.h + 4.0f,
                                         rect.radius + 2.0f, pal.accentR, pal.accentG, pal.accentB,
                                         0.82f, 2.6f, 4);
            }
            for (size_t ci = startIdx; ci < fp.cards.size(); ++ci) {
                const Card& fc = fp.cards[ci];
                if (isCardMoving(fc)) continue;
                drawProceduralCard(r, fc, fc.x, fc.y, cw, ch, pal, light, CardRenderPass::BodyOnly);
            }
        };

        drawFocusPile(showcaseCamera->focusPileA, showcaseCamera->focusCardA);
        if (showcaseCamera->focusPileB != showcaseCamera->focusPileA) {
            drawFocusPile(showcaseCamera->focusPileB, -1);
        }
    }

    // 4. Spring-Animated & 3D Flipping Cards (Always rendered above all resting cards)
    if (!animatingCards.empty()) {
        std::stable_sort(animatingCards.begin(), animatingCards.end(), [](const AnimatingCardEntry& a, const AnimatingCardEntry& b) noexcept {
            if (a.pileIdx == b.pileIdx) {
                return a.cardIdx < b.cardIdx;
            }
            float elevA = a.card->elevation + std::sin(a.card->flipAngle) * 25.0f;
            float elevB = b.card->elevation + std::sin(b.card->flipAngle) * 25.0f;
            return elevA < elevB - 0.01f;
        });
        // Render table shadows for animating cards underneath their bodies inside a unified Max-blend shadow layer
        r.beginShadowLayer();
        for (size_t i = 0; i < animatingCards.size(); ++i) {
            const auto& entry = animatingCards[i];
            drawProceduralCard(r, *entry.card, entry.card->x, entry.card->y, cw, ch, pal, light, CardRenderPass::ShadowOnly);
        }
        r.endShadowLayer();
        for (size_t i = 0; i < animatingCards.size(); ++i) {
            const auto& entry = animatingCards[i];
            if (i > 0 &&
                animatingCards[i - 1].pileIdx == entry.pileIdx &&
                std::abs(animatingCards[i - 1].card->elevation - entry.card->elevation) < 0.5f &&
                animatingCards[i - 1].card->flipAngle < 0.005f) {
                drawInterCardContactShadow(r, *entry.card, entry.card->x, entry.card->y, cw, ch, light);
            }
            drawProceduralCard(r, *entry.card, entry.card->x, entry.card->y, cw, ch, pal, light, CardRenderPass::BodyOnly);
        }
    }

    // 5. Dragged Card Sub-Stack (Elevated on top of all piles and animations; articulated pendulum chain)
    if (draggedPile >= 0 && draggedPile < static_cast<int>(piles.size()) && draggedCardIdx >= 0) {
        const Pile& p = piles[static_cast<size_t>(draggedPile)];
        const size_t firstDrag = static_cast<size_t>(draggedCardIdx);
        if (firstDrag < p.cards.size()) {
            const size_t count = p.cards.size() - firstDrag;
            const Card& lead = p.cards[firstDrag];
            const float pivotX = lead.x + dragOffsetX + dragGrabLocalX;
            const float pivotY = lead.y + dragOffsetY + dragGrabLocalY;

            thread_local std::vector<float> chainX;
            thread_local std::vector<float> chainY;
            thread_local std::vector<float> chainAng;
            chainX.resize(count);
            chainY.resize(count);
            chainAng.resize(count);

            for (size_t k = 0; k < count; ++k) {
                const float theta = (dragChainAngles && k < dragChainCount) ? dragChainAngles[k] : dragPivotAngle;
                chainAng[k] = theta;
                const Card& baseC = p.cards[firstDrag + k];
                if (k == 0) {
                    const float cx = baseC.x + dragOffsetX + cw * 0.5f;
                    const float cy = baseC.y + dragOffsetY + ch * 0.5f;
                    const float dx = cx - pivotX;
                    const float dy = cy - pivotY;
                    const float cosP = std::cos(theta);
                    const float sinP = std::sin(theta);
                    chainX[0] = (pivotX + cosP * dx - sinP * dy) - cw * 0.5f;
                    chainY[0] = (pivotY + sinP * dx + cosP * dy) - ch * 0.5f;
                } else {
                    const Card& prevBase = p.cards[firstDrag + k - 1];
                    const float offX = baseC.x - prevBase.x;
                    const float offY = baseC.y - prevBase.y;
                    const float parentTheta = chainAng[k - 1];
                    const float cosP = std::cos(parentTheta);
                    const float sinP = std::sin(parentTheta);
                    const float parentCx = chainX[k - 1] + cw * 0.5f;
                    const float parentCy = chainY[k - 1] + ch * 0.5f;
                    chainX[k] = (parentCx + cosP * offX - sinP * offY) - cw * 0.5f;
                    chainY[k] = (parentCy + sinP * offX + cosP * offY) - ch * 0.5f;
                }
            }

            r.beginShadowLayer();
            for (size_t k = 0; k < count; ++k) {
                Card c = p.cards[firstDrag + k];
                c.elevation = 14.0f;
                c.angle += chainAng[k];
                drawProceduralCard(r, c, chainX[k], chainY[k], cw, ch, pal, light, CardRenderPass::ShadowOnly);
            }
            r.endShadowLayer();
            for (size_t k = 0; k < count; ++k) {
                Card c = p.cards[firstDrag + k];
                c.elevation = 14.0f;
                c.angle += chainAng[k];
                if (k > 0) {
                    drawInterCardContactShadow(r, c, chainX[k], chainY[k], cw, ch, light);
                }
                drawProceduralCard(r, c, chainX[k], chainY[k], cw, ch, pal, light, CardRenderPass::BodyOnly);
            }
        }
    }

    // 6. Hardwood 3.0 3D Victory Cascade / Choreography & Gather-to-Origin Segue
    if (victoryGather && victoryGather->active) {
        std::vector<VictoryAirborneCard> gathering;
        gathering.reserve(victoryGather->cards.size());

        const float targetCx = victoryGather->targetX + victoryGather->targetW * 0.5f;
        const float targetCy = victoryGather->targetY + victoryGather->targetH * 0.5f;
        const float dur = std::max(0.05f, victoryGather->durationSec);
        const float kPiLocal = 3.14159265358979323846f;

        for (size_t i = 0; i < victoryGather->cards.size(); ++i) {
            const auto& gc = victoryGather->cards[i];
            const float localT = std::clamp((victoryGather->elapsedSec - gc.delaySec) / dur, 0.0f, 1.0f);
            // Smooth cubic ease-in-out
            const float ease = (localT < 0.5f)
                ? (4.0f * localT * localT * localT)
                : (1.0f - std::pow(-2.0f * localT + 2.0f, 3.0f) * 0.5f);
            const float arcZ = std::sin(localT * kPiLocal) * 42.0f;

            // Wrap startYaw into [-pi, pi] so cards take the shortest spin back to face-down
            float sYaw = std::fmod(gc.startYaw, 2.0f * kPiLocal);
            if (sYaw > kPiLocal) sYaw -= 2.0f * kPiLocal;
            if (sYaw < -kPiLocal) sYaw += 2.0f * kPiLocal;
            const float endYaw = gc.card.faceUp ? kPiLocal : 0.0f;
            float dYaw = endYaw - sYaw;
            if (dYaw > kPiLocal) dYaw -= 2.0f * kPiLocal;
            if (dYaw < -kPiLocal) dYaw += 2.0f * kPiLocal;

            float sRoll = std::fmod(gc.startRoll, 2.0f * kPiLocal);
            if (sRoll > kPiLocal) sRoll -= 2.0f * kPiLocal;
            if (sRoll < -kPiLocal) sRoll += 2.0f * kPiLocal;

            VictoryAirborneCard vac{};
            vac.card = gc.card;
            vac.xf.baseW = gc.startW + (victoryGather->targetW - gc.startW) * ease;
            vac.xf.baseH = gc.startH + (victoryGather->targetH - gc.startH) * ease;
            vac.xf.cx = gc.startX + (targetCx - gc.startX) * ease;
            vac.xf.cy = gc.startY + (targetCy - gc.startY) * ease;
            vac.xf.cz = gc.startZ + (6.0f - gc.startZ) * ease + arcZ;
            vac.xf.yaw = sYaw + dYaw * ease;
            vac.xf.pitch = gc.startPitch * (1.0f - ease);
            vac.xf.roll = sRoll * (1.0f - ease);
            gathering.push_back(vac);
        }

        std::stable_sort(gathering.begin(), gathering.end(), [](const VictoryAirborneCard& a, const VictoryAirborneCard& b) noexcept {
            return a.xf.cz < b.xf.cz - 0.01f;
        });

        r.beginShadowLayer();
        for (const auto& vac : gathering) {
            render3DCard(r, vac.card, vac.xf, pal, light, CardRenderPass::ShadowOnly);
        }
        r.endShadowLayer();
        for (const auto& vac : gathering) {
            render3DCard(r, vac.card, vac.xf, pal, light, CardRenderPass::BodyOnly);
        }
    } else if (board.isVictory()) {
        std::vector<VictoryAirborneCard> airborne;
        airborne.reserve(64);
        evaluateActiveVictoryAirborne(board, width, height, airborne);

        r.beginShadowLayer();
        for (const auto& vac : airborne) {
            render3DCard(r, vac.card, vac.xf, pal, light, CardRenderPass::ShadowOnly);
        }
        r.endShadowLayer();
        for (const auto& vac : airborne) {
            render3DCard(r, vac.card, vac.xf, pal, light, CardRenderPass::BodyOnly);
        }
    } else if (board.gameStatus() == GameStatus::Defeat) {
        const float bw = 460.0f;
        const float bh = 96.0f;
        const float bx = (width - bw) * 0.5f;
        const float by = (height - bh) * 0.5f;
        r.drawRectDropShadow(bx, by, bw, bh, 10.0f, 16.0f, light, 0.60f);
        r.drawRoundedRectGradient(bx, by, bw, bh, 10.0f,
                                  0.20f, 0.08f, 0.09f,
                                  0.11f, 0.04f, 0.05f, 0.98f, 4);
        r.drawRoundedRectOutline(bx, by, bw, bh, 10.0f, 0.86f, 0.24f, 0.26f, 0.95f, 2.2f, 4);
        eatsbits::ui::glyph::drawVectorText(r, "THE VEIL CLAIMS YOU  -  DEFEAT", bx + 62.0f, by + 24.0f, 18.0f,
                                            0.95f, 0.34f, 0.34f, 1.0f);
        eatsbits::ui::glyph::drawVectorText(r, "PRESS [N] OR CLICK NEW DEAL TO TRY AGAIN", bx + 68.0f, by + 58.0f, 12.5f,
                                            0.94f, 0.90f, 0.82f, 0.95f);
    }

    // 7. Dynamic Felt Gesture Laser Trail & Visual Hint Overlay
    if (isGesturing) {
        const float gdx = gestureCurX - gestureStartX;
        const float gdy = gestureCurY - gestureStartY;
        const float gdist = std::hypot(gdx, gdy);
        if (gdist > 15.0f) {
            r.drawLine(gestureStartX, gestureStartY, gestureCurX, gestureCurY,
                       pal.accentR, pal.accentG, pal.accentB, 0.75f, 3.0f);
            r.drawCircle(gestureCurX, gestureCurY, 5.0f,
                         pal.accentR, pal.accentG, pal.accentB, 0.90f, 16);

            const char* hintStr = nullptr;
            if (std::abs(gdx) > std::abs(gdy) * 1.25f) {
                hintStr = (gdx < 0.0f) ? "<-- UNDO [Z]" : "REDO [Y] -->";
            } else if (gdy < -30.0f && std::abs(gdy) > std::abs(gdx) * 1.25f) {
                hintStr = "^^^ AUTO-STEP [A]";
            } else if (gdy > 30.0f && std::abs(gdy) > std::abs(gdx) * 1.25f) {
                hintStr = "vvv NETHER SIGHT [H]";
            }

            if (hintStr) {
                const float textLen = static_cast<float>(std::strlen(hintStr)) * 6.4f;
                eatsbits::ui::glyph::drawVectorText(
                    r, hintStr, gestureCurX - textLen * 0.5f, gestureCurY - 22.0f, 12.0f,
                    pal.accentR, pal.accentG, pal.accentB, 0.95f
                );
            }
        }
    }

    // Disable scene lighting for hint highlights, HUD menu, anchor medallion, and modal dialogs so UI stays at 100% crisp contrast
    r.setSceneLightingActive(false);

    // 7b. Hint Source & Destination Dual-Contrast Card Rim Highlights
    if (hintHighlight && hintHighlight->active && hintHighlight->duration > 0.0f) {
        const float ht = std::clamp(hintHighlight->age / hintHighlight->duration, 0.0f, 1.0f);
        const float fadeIn = std::clamp(ht / 0.08f, 0.0f, 1.0f);
        const float fadeOut = (ht > 0.78f) ? std::clamp((1.0f - ht) / 0.22f, 0.0f, 1.0f) : 1.0f;
        const float pulse = 0.72f + 0.28f * std::sin(ht * 12.56637f); // 2 full pulses
        const float srcAlpha = fadeIn * fadeOut * pulse;
        const float dstFadeIn = std::clamp((ht - 0.14f) / 0.16f, 0.0f, 1.0f);
        const float dstAlpha = dstFadeIn * fadeOut * pulse;

        const float effRu = CardTextureCache::instance().effectiveRu();
        const float cardRad = (effRu <= 0.001f) ? 2.0f : std::max(2.0f, effRu * cw);
        const float darkW = std::max(4.6f, cw * 0.068f);
        const float accentW = std::max(2.3f, cw * 0.034f);

        auto drawHintRim = [&](float bx, float by, float bw, float bh, float alpha) {
            if (alpha <= 0.01f) return;
            r.drawRoundedRectOutline(bx - 2.0f, by - 2.0f, bw + 4.0f, bh + 4.0f, cardRad + 2.0f,
                                     0.04f, 0.02f, 0.01f, alpha * 0.84f, darkW, 4);
            r.drawRoundedRectOutline(bx - 1.5f, by - 1.5f, bw + 3.0f, bh + 3.0f, cardRad + 1.5f,
                                     pal.accentR, pal.accentG, pal.accentB, alpha * 0.98f, accentW, 4);
            r.drawRoundedRectOutline(bx - 0.5f, by - 0.5f, bw + 1.0f, bh + 1.0f, cardRad + 0.5f,
                                     1.0f, 0.96f, 0.80f, alpha * 0.65f, 1.1f, 4);
        };

        if (hintHighlight->fromPile < piles.size()) {
            const auto& sp = piles[hintHighlight->fromPile];
            float sx = sp.x, sy = sp.y, sw = cw, sh = ch;
            if (hintHighlight->cardIndex < sp.cards.size()) {
                sx = sp.cards[hintHighlight->cardIndex].x;
                sy = sp.cards[hintHighlight->cardIndex].y;
                sh = std::max(ch, (sp.cards.back().y + ch) - sy);
            }
            drawHintRim(sx, sy, sw, sh, srcAlpha);
        }
        if (hintHighlight->toPile < piles.size() && dstAlpha > 0.01f) {
            const auto& dp = piles[hintHighlight->toPile];
            float dx = dp.x, dy = dp.y;
            if (!dp.cards.empty()) {
                dx = dp.cards.back().x;
                dy = dp.cards.back().y;
            }
            drawHintRim(dx, dy, cw, ch, dstAlpha);
        }
    }

    static float sAnimTime = 0.0f;
    sAnimTime += 0.016f;

    if (highlightedHelpZone >= 0 && static_cast<size_t>(highlightedHelpZone) < piles.size()) {
        float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
        computeZoneHighlightBounds(board, highlightedHelpZone, zx, zy, zw, zh);
        if (zw > 0.0f && zh > 0.0f) {
            const float pulse = 0.78f + 0.22f * std::sin(sAnimTime * 4.5f);
            r.drawRoundedRect(zx, zy, zw, zh, 8.0f, pal.accentR, pal.accentG, pal.accentB, 0.08f * pulse, 4);
            r.drawRoundedRectOutline(zx - 1.5f, zy - 1.5f, zw + 3.0f, zh + 3.0f, 9.5f,
                                     0.04f, 0.02f, 0.01f, 0.70f * pulse, 3.6f, 4);
            r.drawRoundedRectOutline(zx, zy, zw, zh, 8.0f,
                                     pal.accentR, pal.accentG, pal.accentB, 0.92f * pulse, 2.0f, 4);
        }
    }
    if (ghostCards && !ghostCards->empty()) {
        const float effRu = CardTextureCache::instance().effectiveRu();
        const float cardRad = (effRu <= 0.001f) ? 2.0f : std::max(2.0f, effRu * cw);
        for (const auto& gc : *ghostCards) {
            if (gc.ageSec < gc.delaySec) continue;
            const float localAge = gc.ageSec - gc.delaySec;
            if (localAge >= gc.durationSec) continue;

            const float fadeIn = std::clamp(localAge / 0.24f, 0.0f, 1.0f);
            const float fadeOut = std::clamp((gc.durationSec - localAge) / 0.45f, 0.0f, 1.0f);
            const float alpha = gc.maxOpacity * fadeIn * fadeOut;
            if (alpha <= 0.01f) continue;

            const float moveT = std::clamp(localAge / 0.30f, 0.0f, 1.0f);
            const float ease = 1.0f - (1.0f - moveT) * (1.0f - moveT) * (1.0f - moveT);
            const float gx = gc.startX + (gc.targetX - gc.startX) * ease;
            const float gy = gc.startY + (gc.targetY - gc.startY) * ease;

            Card renderC = gc.card;
            renderC.faceUp = true;
            renderC.elevation = 6.0f + (1.0f - ease) * 14.0f;
            drawProceduralCard(r, renderC, gx, gy, cw, ch, pal, light, CardRenderPass::BodyOnly, 99999.0f, 99999.0f, alpha);
            r.drawRoundedRectOutline(gx - 1.5f, gy - 1.5f, cw + 3.0f, ch + 3.0f, cardRad + 1.5f,
                                     pal.accentR, pal.accentG, pal.accentB, alpha * 0.90f, 2.0f, 4);
        }
    }

    if (magicParticles) {
        magicParticles->render(r);
    }
    r.drawSceneAtmospherePass(width, height);
    if (camZoomed) {
        r.clearCameraTransform();
    }

    // 8. Always-on-Top Floating Clockwork Medallion Bar
    const auto layout = computeClockworkBarLayout(width, height, board, menuAnchor, menuFlowVertical);

    // 8a. Dedicated RPG Stat Panel (anchored in empty top-right board slot above Room 4)
    {
        const auto& bb = board.blackboard();
        auto itHp = bb.intStats.find("HP");
        if (itHp != bb.intStats.end() && piles.size() >= 7) {
            const int hp = std::clamp(itHp->second, 0, 99);
            const int maxHp = std::max(20, bb.getInt("MAX_HP", 20));
            const int wpn = bb.getInt("WEAPON", 0);
            const int cap = bb.getInt("CAP", 99);
            const bool hasPotionUsed = bb.intStats.count("POTION_USED") > 0;
            const bool potionUsed = bb.getInt("POTION_USED", 0) != 0;
            const bool hasFleeUsed = bb.intStats.count("FLEE_USED") > 0;
            const bool fleeUsed = bb.getInt("FLEE_USED", 0) != 0;

            const float effRu = CardTextureCache::instance().effectiveRu();
            const float cardRad = (effRu <= 0.001f) ? 2.0f : std::max(2.0f, effRu * cw);
            const float hudW = cw;
            const float hudH = ch;
            const float hudX = piles.back().x;
            const float hudY = piles[0].y;

            r.drawRectDropShadow(hudX, hudY, hudW, hudH, cardRad, 3.5f, light, 0.40f);
            r.drawRoundedRect(hudX, hudY, hudW, hudH, cardRad,
                              pal.woodR0 * 0.72f, pal.woodG0 * 0.72f, pal.woodB0 * 0.72f, 0.92f, 3);
            r.drawRoundedRectOutline(hudX, hudY, hudW, hudH, cardRad,
                                     pal.trimR, pal.trimG, pal.trimB, 0.75f, 1.2f, 3);

            const float padX = std::max(6.0f, hudW * 0.08f);
            const float padY = std::max(6.0f, hudH * 0.07f);
            const float innerW = hudW - padX * 2.0f;
            const float rowCount = 3.0f + (hasPotionUsed ? 1.0f : 0.0f) + (hasFleeUsed ? 1.0f : 0.0f);
            const float rowStep = (hudH - padY * 2.0f) / std::max(3.0f, rowCount);
            float ry = hudY + padY;
            const float fontSz = std::clamp(hudW * 0.095f, 7.0f, 11.0f);

            // Row 1: HP Bar
            const float hpBarH = std::clamp(rowStep * 0.76f, 14.0f, 20.0f);
            const float hpFrac = std::clamp(static_cast<float>(hp) / static_cast<float>(maxHp), 0.0f, 1.0f);
            r.drawRoundedRect(hudX + padX, ry, innerW, hpBarH, 4.0f, 0.08f, 0.04f, 0.04f, 0.92f, 3);
            if (hpFrac > 0.001f) {
                const float fillR = (hpFrac > 0.5f) ? 0.22f : (hpFrac > 0.25f ? 0.85f : 0.86f);
                const float fillG = (hpFrac > 0.5f) ? 0.74f : (hpFrac > 0.25f ? 0.58f : 0.18f);
                const float fillB = (hpFrac > 0.5f) ? 0.34f : (hpFrac > 0.25f ? 0.14f : 0.18f);
                r.drawRoundedRect(hudX + padX + 1.5f, ry + 1.5f,
                                  std::max(4.0f, (innerW - 3.0f) * hpFrac), hpBarH - 3.0f,
                                  3.0f, fillR, fillG, fillB, 0.95f, 3);
            }
            char hpBuf[32];
            std::snprintf(hpBuf, sizeof(hpBuf), "HP %d/%d", hp, maxHp);
            eatsbits::ui::glyph::drawVectorText(r, hpBuf, hudX + padX + 6.0f, ry + (hpBarH - fontSz) * 0.5f, fontSz,
                                                0.98f, 0.97f, 0.94f, 1.0f);
            ry += rowStep;

            // Row 2: Weapon Power
            char wpnBuf[32];
            if (wpn > 0) {
                std::snprintf(wpnBuf, sizeof(wpnBuf), "WPN: %d", wpn);
                eatsbits::ui::glyph::drawVectorText(r, wpnBuf, hudX + padX, ry + 2.0f, fontSz,
                                                    0.96f, 0.84f, 0.42f, 1.0f);
            } else {
                eatsbits::ui::glyph::drawVectorText(r, "WPN: NONE", hudX + padX, ry + 2.0f, fontSz,
                                                    0.78f, 0.76f, 0.72f, 0.88f);
            }
            ry += rowStep;

            // Row 3: Weapon Durability Cap
            char capBuf[32];
            if (wpn <= 0) {
                std::snprintf(capBuf, sizeof(capBuf), "CAP: --");
            } else if (cap >= 99) {
                std::snprintf(capBuf, sizeof(capBuf), "CAP: ANY");
            } else {
                std::snprintf(capBuf, sizeof(capBuf), "CAP: <=%d", cap);
            }
            eatsbits::ui::glyph::drawVectorText(r, capBuf, hudX + padX, ry + 2.0f, fontSz,
                                                (wpn > 0) ? 0.92f : 0.65f,
                                                (wpn > 0) ? 0.88f : 0.63f,
                                                (wpn > 0) ? 0.76f : 0.60f, 0.95f);
            ry += rowStep;

            // Row 4: Potion Limit
            if (hasPotionUsed) {
                const char* potTxt = potionUsed ? "POTION: USED" : "POTION: OK";
                eatsbits::ui::glyph::drawVectorText(r, potTxt, hudX + padX, ry + 2.0f, fontSz * 0.94f,
                                                    potionUsed ? 0.65f : 0.95f,
                                                    potionUsed ? 0.62f : 0.44f,
                                                    potionUsed ? 0.62f : 0.48f, 0.95f);
                ry += rowStep;
            }

            // Row 5: Flee Availability
            if (hasFleeUsed) {
                const char* fleeTxt = fleeUsed ? "FLEE: LOCKED" : "FLEE: OK";
                eatsbits::ui::glyph::drawVectorText(r, fleeTxt, hudX + padX, ry + 2.0f, fontSz * 0.94f,
                                                    fleeUsed ? 0.65f : 0.52f,
                                                    fleeUsed ? 0.62f : 0.86f,
                                                    fleeUsed ? 0.62f : 0.96f, 0.95f);
            }
        }
    }
    // Medallions & Transport Bar (hidden in Screensaver Mode or --hide-hud)
    if (!screensaverMode && !hideHud) {
    int hoveredIdx = -1;
    for (size_t i = 0; i < layout.buttons.size(); ++i) {
        const auto& b = layout.buttons[i];
        const bool hovered = (b.id == hoveredButtonId);
        if (hovered) hoveredIdx = static_cast<int>(i);

        const float cx = b.x + b.w * 0.5f;
        const float cy = b.y + b.h * 0.5f;
        const float radius = b.w * 0.5f;

        if (b.id == BtnMenuToggle) {
            // Master SolEngine Orb
            clockwork_ui::drawClockworkMenuOrb(r, cx, cy, radius, hovered, isDraggingMenu, sAnimTime);
        } else if (b.id == BtnGameTitlePill) {
            // Game Title Pill: displays active game name & opens Game Library on 1 tap
            const auto& cpal = clockwork_ui::palette();
            const auto& ts = SettingsManager::instance().settings();
            float fontSz = 11.0f;
            float pillPadX = 10.0f;
            if (ts.toolbarScale == ToolbarScale::Compact) {
                fontSz = 9.5f;
                pillPadX = 8.0f;
            } else if (ts.toolbarScale == ToolbarScale::Large) {
                fontSz = 12.5f;
                pillPadX = 12.0f;
            }

            const float pillRad = b.h * 0.5f;
            const float pillAlpha = hovered ? 1.0f : 0.88f;
            r.drawRectDropShadow(
                b.x, b.y, b.w, b.h, pillRad, 3.0f,
                eatsbits::ui::LightSource2D{cx - 120.0f, cy - 240.0f, 400.0f, 0.85f},
                pillAlpha * 0.45f
            );
            r.drawRoundedRectGradient(
                b.x, b.y, b.w, b.h, pillRad,
                cpal.chassisLightR + (hovered ? 0.08f : 0.02f),
                cpal.chassisLightG + (hovered ? 0.07f : 0.02f),
                cpal.chassisLightB + (hovered ? 0.06f : 0.01f),
                cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB,
                0.92f, 6
            );
            r.drawRoundedRectOutline(
                b.x, b.y, b.w, b.h, pillRad,
                hovered ? cpal.furnaceAmberR : cpal.brassMidR,
                hovered ? cpal.furnaceAmberG : cpal.brassMidG,
                hovered ? cpal.furnaceAmberB : cpal.brassMidB,
                hovered ? 0.98f : 0.80f, hovered ? 1.6f : 1.2f, 6
            );

            std::string gameTitle = board.activeGameDef().name;
            if (gameTitle.empty()) {
                if (const auto* def = GameRegistry::instance().findByVariant(board.variant())) {
                    gameTitle = def->name;
                }
            }
            if (gameTitle.empty()) gameTitle = "Solitaire";

            const float iconSize = fontSz + 2.5f;
            const float iconSpace = iconSize + 5.0f;
            const float chevronSpace = 14.0f;
            const float maxTextW = std::max(16.0f, b.w - pillPadX * 2.0f - iconSpace - chevronSpace);
            if (eatsbits::ui::glyph::getVectorTextWidth(fontSz, gameTitle.c_str()) > maxTextW) {
                while (gameTitle.size() > 1 &&
                       eatsbits::ui::glyph::getVectorTextWidth(fontSz, (gameTitle + "...").c_str()) > maxTextW) {
                    gameTitle.pop_back();
                }
                gameTitle += "...";
            }

            vector_icon::drawGameLibraryIcon(
                r,
                b.x + pillPadX + iconSize * 0.5f,
                b.y + b.h * 0.5f,
                iconSize * 0.50f,
                hovered ? cpal.furnaceAmberR : cpal.brassBrightR,
                hovered ? cpal.furnaceAmberG : cpal.brassBrightG,
                hovered ? cpal.furnaceAmberB : cpal.brassBrightB,
                cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB,
                1.0f, false
            );

            const float textY = b.y + (b.h - fontSz) * 0.5f - 0.5f;
            eatsbits::ui::glyph::drawVectorText(
                r, gameTitle.c_str(), b.x + pillPadX + iconSpace, textY, fontSz,
                hovered ? 1.0f : 0.94f,
                hovered ? 0.96f : 0.90f,
                hovered ? 0.86f : 0.80f,
                1.0f
            );

            // Crisp brass chevron 'v' on the right side
            const float chevX = b.x + b.w - pillPadX - 4.0f;
            const float chevY = b.y + b.h * 0.5f;
            const float chR = hovered ? cpal.furnaceAmberR : cpal.brassHighlightR;
            const float chG = hovered ? cpal.furnaceAmberG : cpal.brassHighlightG;
            const float chB = hovered ? cpal.furnaceAmberB : cpal.brassHighlightB;
            r.drawLine(chevX - 3.5f, chevY - 1.5f, chevX, chevY + 2.0f, chR, chG, chB, 0.95f, 1.5f);
            r.drawLine(chevX, chevY + 2.0f, chevX + 3.5f, chevY - 1.5f, chR, chG, chB, 0.95f, 1.5f);
        } else {
            // Circled Medallion Button with embossed glyph
            clockwork_ui::drawClockworkMedallion(
                r, cx, cy, radius, hovered, false,
                b.glyph, sAnimTime
            );
        }
    }

    // Tooltip for hovered medallion
    if (hoveredIdx >= 0 && !isDraggingMenu) {
        const auto& b = layout.buttons[hoveredIdx];
        int placement = 0; // 0=above, 1=below, 2=left, 3=right
        if (!layout.isVertical) {
            placement = (menuAnchor == MenuAnchor::BottomLeft || menuAnchor == MenuAnchor::BottomRight) ? 0 : 1;
        } else {
            placement = (menuAnchor == MenuAnchor::BottomLeft || menuAnchor == MenuAnchor::TopLeft) ? 3 : 2;
        }
        clockwork_ui::drawClockworkTooltip(r, b.x, b.y, b.w, b.h, b.label, placement);
    }

    // Ghost preview while dragging menu to re-anchor / reflow
    if (isDraggingMenu) {
        const auto ghostLayout = computeClockworkBarLayout(width, height, board, dragPreviewAnchor, dragPreviewFlowVertical);

        for (const auto& gb : ghostLayout.buttons) {
            if (gb.id == BtnGameTitlePill) {
                const float gRad = gb.h * 0.5f;
                r.drawRoundedRect(gb.x, gb.y, gb.w, gb.h, gRad, 0.94f, 0.80f, 0.36f, 0.18f, 6);
                r.drawRoundedRectOutline(gb.x, gb.y, gb.w, gb.h, gRad, 0.95f, 0.52f, 0.12f, 0.85f, 1.8f, 6);
            } else {
                const float gcx = gb.x + gb.w * 0.5f;
                const float gcy = gb.y + gb.h * 0.5f;
                r.drawCircle(gcx, gcy, gb.w * 0.5f, 0.94f, 0.80f, 0.36f, 0.18f, 20);
                r.drawCircleOutline(gcx, gcy, gb.w * 0.5f, 0.95f, 0.52f, 0.12f, 0.85f, 1.8f, 20);
            }
        }
    }

    // 9. Floating Auto-Play Transport Bar
    if (autoPlay && autoPlay->isActive()) {
        auto tBtns = buildTransportButtons(width, height, *autoPlay, board.safeBottom());
        drawTransportBar(r, width, height, *autoPlay, tBtns, hoveredButtonId, pal, light);
    }
    } else if (screensaverMode) {
        // Subtle Screensaver Mode watermark
        const char* ssLabel = "SCREENSAVER  |  TAP ANYWHERE TO EXIT";
        const float fontSz = 10.0f;
        const float txtW = eatsbits::ui::glyph::getVectorTextWidth(fontSz, ssLabel);
        const float pillW = txtW + 24.0f;
        const float pillH = 24.0f;
        const float pillX = (width - pillW) * 0.5f;
        const float pillY = height - pillH - 14.0f - board.safeBottom();
        r.drawRoundedRect(pillX, pillY, pillW, pillH, pillH * 0.5f, 0.05f, 0.04f, 0.03f, 0.45f, 6);
        r.drawRoundedRectOutline(pillX, pillY, pillW, pillH, pillH * 0.5f,
                                 pal.accentR, pal.accentG, pal.accentB, 0.35f, 1.0f, 6);
        eatsbits::ui::glyph::drawVectorText(r, ssLabel, pillX + 12.0f, pillY + 7.0f, fontSz,
                                            0.92f, 0.90f, 0.85f, 0.65f);
    }

    // 10. Responsive Help Sidebar
    if (helpOpen) {
        drawHelpSidebar(r, board, width, height, pal, highlightedHelpZone, helpScrollY, helpMouseX, helpMouseY, helpOdds, helpVideoThumb);
    }
}

bool TableRenderer::writePngFile(
    const std::string& filePath,
    uint32_t width,
    uint32_t height,
    const uint8_t* rgbaPixels
) {
    if (!rgbaPixels || width == 0 || height == 0) return false;

    auto crc32 = [](const uint8_t* data, size_t len) -> uint32_t {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i) {
            c ^= data[i];
            for (int k = 0; k < 8; ++k) {
                c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
            }
        }
        return ~c;
    };

    auto appendU32BE = [](std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
    };

    auto appendChunk = [&](std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data) {
        appendU32BE(png, static_cast<uint32_t>(data.size()));
        size_t typeOffset = png.size();
        png.push_back(static_cast<uint8_t>(type[0]));
        png.push_back(static_cast<uint8_t>(type[1]));
        png.push_back(static_cast<uint8_t>(type[2]));
        png.push_back(static_cast<uint8_t>(type[3]));
        png.insert(png.end(), data.begin(), data.end());
        uint32_t crc = crc32(png.data() + typeOffset, 4 + data.size());
        appendU32BE(png, crc);
    };

    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    appendU32BE(ihdr, width);
    appendU32BE(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    const size_t rowBytes = static_cast<size_t>(width) * 4 + 1;
    raw.reserve(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgbaPixels + static_cast<size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4);
    }

    std::vector<uint8_t> zdata;
    zdata.push_back(0x78);
    zdata.push_back(0x01);
    size_t pos = 0;
    uint32_t s1 = 1, s2 = 0;
    while (pos < raw.size()) {
        size_t blockLen = std::min<size_t>(65535, raw.size() - pos);
        bool finalBlock = (pos + blockLen == raw.size());
        zdata.push_back(finalBlock ? 0x01 : 0x00);
        zdata.push_back(static_cast<uint8_t>(blockLen & 0xFF));
        zdata.push_back(static_cast<uint8_t>((blockLen >> 8) & 0xFF));
        uint16_t nlen = static_cast<uint16_t>(~blockLen);
        zdata.push_back(static_cast<uint8_t>(nlen & 0xFF));
        zdata.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        for (size_t i = 0; i < blockLen; ++i) {
            uint8_t b = raw[pos + i];
            zdata.push_back(b);
            s1 = (s1 + b) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        pos += blockLen;
    }
    appendU32BE(zdata, (s2 << 16) | s1);
    appendChunk(png, "IDAT", zdata);
    appendChunk(png, "IEND", {});

    std::ofstream out(filePath, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

bool TableRenderer::renderToPng(
    SolitaireBoard& board,
    uint32_t width,
    uint32_t height,
    const std::string& outputPngPath,
    int aaMode
) {
    board.computeLayout(static_cast<float>(width), static_cast<float>(height));
    for (int i = 0; i < 90; ++i) {
        board.updateAnimations(1.0f / 60.0f);
    }

    eatsbits::ui::BatchRenderer2D renderer;
    if (!renderer.initialize(nullptr, width, height, eatsbits::ui::RenderBackendType::Filament)) {
        return false;
    }
    CardTextureCache::instance().initialize();
    renderer.setDirectPresent(false);
    renderer.setAntiAliasingMode(aaMode);
    renderer.beginFrame(static_cast<float>(width), static_cast<float>(height));
    renderBoard(renderer, board, static_cast<float>(width), static_cast<float>(height));
    renderer.endFrame();

    std::vector<uint32_t> fbPixels;
    uint32_t outW = width;
    uint32_t outH = height;
    renderer.snapshotFramebuffer(fbPixels, outW, outH);
    renderer.shutdown();

    if (fbPixels.empty()) return false;

    std::vector<uint8_t> rgba(fbPixels.size() * 4);
    for (size_t i = 0; i < fbPixels.size(); ++i) {
        uint32_t p = fbPixels[i];
        rgba[i * 4 + 0] = static_cast<uint8_t>((p >> 16) & 0xFF);
        rgba[i * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
        rgba[i * 4 + 2] = static_cast<uint8_t>(p & 0xFF);
        rgba[i * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xFF);
    }

    return writePngFile(outputPngPath, outW, outH, rgba.data());
}

namespace {

void drawPlayContextMenu(
    eatsbits::ui::BatchRenderer2D& r,
    float width,
    float height,
    float anchorX,
    float anchorY,
    float mouseX,
    float mouseY,
    bool isSolvableActive,
    int& hoveredOption
) {
    constexpr float menuW = 230.0f;
    constexpr float itemH = 30.0f;
    constexpr float headerH = 26.0f;
    constexpr float pad = 8.0f;
    constexpr int kCount = 6;
    constexpr float menuH = headerH + static_cast<float>(kCount) * itemH + pad * 2.0f;

    float menuX = anchorX;
    float menuY = (anchorY > height * 0.5f) ? (anchorY - menuH - 8.0f) : (anchorY + 44.0f);
    menuX = std::clamp(menuX, 8.0f, width - menuW - 8.0f);
    menuY = std::clamp(menuY, 8.0f, height - menuH - 8.0f);

    const auto& cpal = clockwork_ui::palette();

    // 1. Drop shadow
    r.drawRectDropShadow(menuX, menuY, menuW, menuH, 10.0f, 18.0f,
                         eatsbits::ui::LightSource2D{menuX + menuW * 0.5f, -200.0f, 1.0f}, 0.65f);

    // 2. Chassis background
    r.drawRoundedRectGradient(
        menuX, menuY, menuW, menuH, 8.0f,
        cpal.chassisLightR, cpal.chassisLightG, cpal.chassisLightB,
        cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB,
        0.96f, 6
    );
    r.drawRoundedRectOutline(
        menuX, menuY, menuW, menuH, 8.0f,
        cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.95f, 1.4f, 6
    );

    // 3. Header
    eatsbits::ui::glyph::drawVectorText(
        r, "NEW DEAL OPTIONS", menuX + 12.0f, menuY + 7.0f, 10.0f,
        cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.95f
    );
    r.drawLine(menuX + 10.0f, menuY + headerH, menuX + menuW - 10.0f, menuY + headerH,
               cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 0.7f, 1.0f);

    // 4. Options
    struct Option {
        const char* glyph;
        const char* label;
    };
    const Option options[kCount] = {
        { "▶", "New Random Deal" },
        { "*", "New Solvable Deal" },
        { "↺", "Restart Current Deal" },
        { isSolvableActive ? "[x]" : "[ ]", "Solvable Mode (Shift+N)" },
        { ">>", "Screensaver (Shift+A)" },
        { "G", "Game Library..." }
    };

    hoveredOption = -1;
    float curY = menuY + headerH + pad * 0.5f;
    for (int i = 0; i < kCount; ++i) {
        bool hovered = (mouseX >= menuX + pad && mouseX <= menuX + menuW - pad &&
                        mouseY >= curY && mouseY <= curY + itemH);
        if (hovered) hoveredOption = i;

        if (hovered) {
            r.drawRoundedRect(
                menuX + pad, curY, menuW - pad * 2.0f, itemH, 4.0f,
                cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.22f, 4
            );
            r.drawRoundedRectOutline(
                menuX + pad, curY, menuW - pad * 2.0f, itemH, 4.0f,
                cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.8f, 1.0f, 4
            );
        }

        eatsbits::ui::glyph::drawVectorText(
            r, options[i].glyph, menuX + pad + 8.0f, curY + 8.0f, 11.0f,
            hovered ? cpal.brassBrightR : cpal.brassHighlightR,
            hovered ? cpal.brassBrightG : cpal.brassHighlightG,
            hovered ? cpal.brassBrightB : cpal.brassHighlightB,
            1.0f
        );

        eatsbits::ui::glyph::drawVectorText(
            r, options[i].label, menuX + pad + 32.0f, curY + 8.0f, 11.0f,
            hovered ? 1.0f : 0.88f,
            hovered ? 0.96f : 0.84f,
            hovered ? 0.85f : 0.76f,
            1.0f
        );

        curY += itemH;
    }
}

void drawToolbarConfigMenu(
    eatsbits::ui::BatchRenderer2D& r,
    float width,
    float height,
    float anchorX,
    float anchorY,
    float mouseX,
    float mouseY,
    const AppSettings& s,
    int& hoveredScaleIdx,
    int& hoveredToggleIdx,
    bool& hoveredClose
) {
    constexpr float menuW = 240.0f;
    constexpr float headerH = 26.0f;
    constexpr float scaleRowH = 34.0f;
    constexpr float itemH = 26.0f;
    constexpr float pad = 8.0f;
    constexpr int kNumToggles = 9;
    constexpr float footerH = 32.0f;
    constexpr float menuH = headerH + scaleRowH + 14.0f + static_cast<float>(kNumToggles) * itemH + footerH + pad * 2.0f;

    float menuX = anchorX;
    float menuY = (anchorY > height * 0.5f) ? (anchorY - menuH - 8.0f) : (anchorY + 44.0f);
    menuX = std::clamp(menuX, 8.0f, width - menuW - 8.0f);
    menuY = std::clamp(menuY, 8.0f, height - menuH - 8.0f);

    const auto& cpal = clockwork_ui::palette();

    // 1. Drop shadow
    r.drawRectDropShadow(menuX, menuY, menuW, menuH, 10.0f, 18.0f,
                         eatsbits::ui::LightSource2D{menuX + menuW * 0.5f, -200.0f, 1.0f}, 0.65f);

    // 2. Chassis background
    r.drawRoundedRectGradient(
        menuX, menuY, menuW, menuH, 8.0f,
        cpal.chassisLightR, cpal.chassisLightG, cpal.chassisLightB,
        cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB,
        0.97f, 6
    );
    r.drawRoundedRectOutline(
        menuX, menuY, menuW, menuH, 8.0f,
        cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.95f, 1.4f, 6
    );

    // 3. Header
    eatsbits::ui::glyph::drawVectorText(
        r, "TOOLBAR CONFIGURATION", menuX + 12.0f, menuY + 7.0f, 10.0f,
        cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.95f
    );
    r.drawLine(menuX + 10.0f, menuY + headerH, menuX + menuW - 10.0f, menuY + headerH,
               cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 0.7f, 1.0f);

    // 4. Scale row
    float scaleY = menuY + headerH + 6.0f;
    const char* scaleNames[3] = { "Small", "Regular", "Large" };
    const float pillW = (menuW - pad * 2.0f - 8.0f) / 3.0f;
    const float pillH = 22.0f;

    hoveredScaleIdx = -1;
    for (int i = 0; i < 3; ++i) {
        float px = menuX + pad + static_cast<float>(i) * (pillW + 4.0f);
        bool isCurrent = (static_cast<int>(s.toolbarScale) == i);
        bool hovered = (mouseX >= px && mouseX <= px + pillW && mouseY >= scaleY && mouseY <= scaleY + pillH);
        if (hovered) hoveredScaleIdx = i;

        if (isCurrent) {
            r.drawRoundedRect(px, scaleY, pillW, pillH, 4.0f,
                              cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.40f, 4);
            r.drawRoundedRectOutline(px, scaleY, pillW, pillH, 4.0f,
                                     cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f, 1.2f, 4);
        } else if (hovered) {
            r.drawRoundedRect(px, scaleY, pillW, pillH, 4.0f,
                              cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.20f, 4);
            r.drawRoundedRectOutline(px, scaleY, pillW, pillH, 4.0f,
                                     cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.7f, 1.0f, 4);
        } else {
            r.drawRoundedRect(px, scaleY, pillW, pillH, 4.0f,
                              cpal.chassisDarkR, cpal.chassisDarkG, cpal.chassisDarkB, 0.60f, 4);
            r.drawRoundedRectOutline(px, scaleY, pillW, pillH, 4.0f,
                                     cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 0.5f, 1.0f, 4);
        }

        const float txtW = eatsbits::ui::glyph::getVectorTextWidth(9.0f, scaleNames[i]);
        eatsbits::ui::glyph::drawVectorText(
            r, scaleNames[i], px + (pillW - txtW) * 0.5f, scaleY + 5.5f, 9.0f,
            isCurrent ? cpal.brassHighlightR : 0.80f,
            isCurrent ? cpal.brassHighlightG : 0.78f,
            isCurrent ? cpal.brassHighlightB : 0.72f,
            1.0f
        );
    }

    // Divider
    float divY = scaleY + pillH + 8.0f;
    r.drawLine(menuX + 10.0f, divY, menuX + menuW - 10.0f, divY,
               cpal.brassDarkR, cpal.brassDarkG, cpal.brassDarkB, 0.5f, 1.0f);

    // Section label
    eatsbits::ui::glyph::drawVectorText(
        r, "VISIBLE ICONS", menuX + 12.0f, divY + 4.0f, 9.0f,
        cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 0.75f
    );

    // 5. Toggle checkboxes
    struct ToggleDef {
        uint32_t flag;
        const char* label;
        const char* glyph;
    };
    const ToggleDef toggles[kNumToggles] = {
        { ToolbarItem_Play,       "Play / Deal",       "▶" },
        { ToolbarItem_Undo,       "Undo",              "<" },
        { ToolbarItem_Redo,       "Redo",              ">" },
        { ToolbarItem_AutoPlay,   "Auto-Play",         ">>" },
        { ToolbarItem_Hint,       "Hint",              "?" },
        { ToolbarItem_Restart,    "Restart Deal",      "↺" },
        { ToolbarItem_Solvable,   "Solvable Deal",     "*" },
        { ToolbarItem_Seed,       "Deal Seed",         "#" },
        { ToolbarItem_GameSelect, "Select Game",       "G" }
    };

    hoveredToggleIdx = -1;
    float curY = divY + 18.0f;
    for (int i = 0; i < kNumToggles; ++i) {
        bool isEnabled = (s.toolbarItems & toggles[i].flag) != 0;
        bool hovered = (mouseX >= menuX + pad && mouseX <= menuX + menuW - pad &&
                        mouseY >= curY && mouseY <= curY + itemH);
        if (hovered) hoveredToggleIdx = i;

        if (hovered) {
            r.drawRoundedRect(
                menuX + pad, curY, menuW - pad * 2.0f, itemH, 4.0f,
                cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.15f, 4
            );
        }

        const float cbSize = 13.0f;
        const float cbX = menuX + pad + 6.0f;
        const float cbY = curY + (itemH - cbSize) * 0.5f;

        r.drawRoundedRect(cbX, cbY, cbSize, cbSize, 3.0f,
                          isEnabled ? cpal.furnaceAmberR : cpal.chassisDarkR,
                          isEnabled ? cpal.furnaceAmberG : cpal.chassisDarkG,
                          isEnabled ? cpal.furnaceAmberB : cpal.chassisDarkB,
                          0.85f, 4);
        r.drawRoundedRectOutline(cbX, cbY, cbSize, cbSize, 3.0f,
                                 cpal.brassMidR, cpal.brassMidG, cpal.brassMidB, 0.9f, 1.0f, 4);

        if (isEnabled) {
            eatsbits::ui::glyph::drawVectorText(
                r, "x", cbX + 2.5f, cbY + 1.5f, 9.0f,
                cpal.brassBrightR, cpal.brassBrightG, cpal.brassBrightB, 1.0f
            );
        }

        eatsbits::ui::glyph::drawVectorText(
            r, toggles[i].glyph, cbX + cbSize + 8.0f, curY + 6.0f, 10.0f,
            cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 0.9f
        );

        eatsbits::ui::glyph::drawVectorText(
            r, toggles[i].label, cbX + cbSize + 28.0f, curY + 6.5f, 10.0f,
            isEnabled ? 0.90f : 0.55f,
            isEnabled ? 0.88f : 0.52f,
            isEnabled ? 0.82f : 0.48f,
            1.0f
        );

        curY += itemH;
    }

    // 6. Close button in footer
    float closeBtnY = curY + 4.0f;
    float closeBtnW = menuW - pad * 2.0f;
    float closeBtnH = 22.0f;
    hoveredClose = (mouseX >= menuX + pad && mouseX <= menuX + pad + closeBtnW &&
                    mouseY >= closeBtnY && mouseY <= closeBtnY + closeBtnH);

    r.drawRoundedRect(
        menuX + pad, closeBtnY, closeBtnW, closeBtnH, 4.0f,
        hoveredClose ? cpal.furnaceAmberR : cpal.chassisDarkR,
        hoveredClose ? cpal.furnaceAmberG : cpal.chassisDarkG,
        hoveredClose ? cpal.furnaceAmberB : cpal.chassisDarkB,
        hoveredClose ? 0.35f : 0.60f, 4
    );
    r.drawRoundedRectOutline(
        menuX + pad, closeBtnY, closeBtnW, closeBtnH, 4.0f,
        hoveredClose ? cpal.brassHighlightR : cpal.brassMidR,
        hoveredClose ? cpal.brassHighlightG : cpal.brassMidG,
        hoveredClose ? cpal.brassHighlightB : cpal.brassMidB,
        0.85f, 1.0f, 4
    );
    const float doneTxtW = eatsbits::ui::glyph::getVectorTextWidth(10.0f, "Done");
    eatsbits::ui::glyph::drawVectorText(
        r, "Done", menuX + (menuW - doneTxtW) * 0.5f, closeBtnY + 5.5f, 10.0f,
        hoveredClose ? cpal.brassHighlightR : 0.85f,
        hoveredClose ? cpal.brassHighlightG : 0.82f,
        hoveredClose ? cpal.brassHighlightB : 0.78f,
        1.0f
    );
}

} // namespace

#if defined(__EMSCRIPTEN__)
static bool s_webResizePending = false;
static uint32_t s_webPendingCssW = 0;
static uint32_t s_webPendingCssH = 0;
static float s_webPendingDpr = 1.0f;
static bool s_webSafeAreaPending = false;
static float s_webPendingSafeTop = 0.0f;
static float s_webPendingSafeRight = 0.0f;
static float s_webPendingSafeBottom = 0.0f;
static float s_webPendingSafeLeft = 0.0f;
#endif

struct InteractiveContext {
    SolitaireBoard& board;
    AudioFoleyEngine& foley;
    eatsbits::ui::BatchRenderer2D renderer;
    GLFWwindow* window = nullptr;
    uint32_t width = 1280;
    uint32_t height = 720;
#if defined(__EMSCRIPTEN__)
    uint32_t fbWidth = 1280;
    uint32_t fbHeight = 720;
    float currentDpr = 1.0f;
#endif

    AutoPlayController autoPlayController;
    int draggedPile = -1;
    int draggedCardIdx = -1;
    double dragStartMouseX = 0.0;
    double dragStartMouseY = 0.0;
    float dragOffsetX = 0.0f;
    float dragOffsetY = 0.0f;
    float dragGrabLocalX = 0.0f;
    float dragGrabLocalY = 0.0f;
    float dragPivotAngle = 0.0f;
    float dragPivotVel = 0.0f;
    float prevDragOffsetX = 0.0f;
    float prevDragOffsetY = 0.0f;
    std::array<float, 52> dragChainAngles{};
    std::array<float, 52> dragChainVels{};
    size_t dragChainCount = 0;
    float dragSoundAccumulator = 0.0f;
    double lastDragMouseX = 0.0;
    double lastDragMouseY = 0.0;

    bool prevLeftDown = false;
    bool prevRightDown = false;
    bool prevMiddleDown = false;
    float midPanLastX = 0.0f;
    float midPanLastY = 0.0f;
    double lastMiddleClickTime = -10.0;
    bool feltPanning = false;
    float feltPanLastX = 0.0f;
    float feltPanLastY = 0.0f;
    double lastFeltTapTime = -10.0;
    float lastFeltTapX = 0.0f;
    float lastFeltTapY = 0.0f;
    float lastWarpedFmx = 0.0f;
    float lastWarpedFmy = 0.0f;
    bool pinchActive = false;
    double pinchSuppressUntil = -10.0;
    float pinchLastDist = 0.0f;
    float pinchLastMidX = 0.0f;
    float pinchLastMidY = 0.0f;

    void clampUserCamera() noexcept {
        const float vw = static_cast<float>(width);
        const float vh = static_cast<float>(height);
        if (vw <= 0.0f || vh <= 0.0f) return;
        showcaseCam.userZoom = std::clamp(showcaseCam.userZoom, 1.0f, 3.0f);
        if (showcaseCam.userZoom <= 1.002f) {
            showcaseCam.userZoom = 1.0f;
            showcaseCam.userFocusX = vw * 0.5f;
            showcaseCam.userFocusY = vh * 0.5f;
        } else {
            const float halfW = (vw * 0.5f) / showcaseCam.userZoom;
            const float halfH = (vh * 0.5f) / showcaseCam.userZoom;
            showcaseCam.userFocusX = (halfW < vw * 0.5f) ? std::clamp(showcaseCam.userFocusX, halfW, vw - halfW) : (vw * 0.5f);
            showcaseCam.userFocusY = (halfH < vh * 0.5f) ? std::clamp(showcaseCam.userFocusY, halfH, vh - halfH) : (vh * 0.5f);
        }
    }

    void resetUserCamera() noexcept {
        const float vw = static_cast<float>(width);
        const float vh = static_cast<float>(height);
        showcaseCam.userZoom = 1.0f;
        showcaseCam.userFocusX = vw * 0.5f;
        showcaseCam.userFocusY = vh * 0.5f;
        showcaseCam.userPanActive = false;
    }

    void zoomUserCameraAt(float factor, float screenX, float screenY) noexcept {
        const float vw = static_cast<float>(width);
        const float vh = static_cast<float>(height);
        if (vw <= 0.0f || vh <= 0.0f) return;
        if (showcaseCam.userFocusX <= 0.0f && showcaseCam.userFocusY <= 0.0f) {
            showcaseCam.userFocusX = vw * 0.5f;
            showcaseCam.userFocusY = vh * 0.5f;
        }
        const float oldZoom = std::clamp(showcaseCam.userZoom, 1.0f, 3.0f);
        const float newZoom = std::clamp(oldZoom * factor, 1.0f, 3.0f);
        if (std::abs(newZoom - oldZoom) <= 0.0001f) return;

        const float boardX = showcaseCam.userFocusX + (screenX - vw * 0.5f) / oldZoom;
        const float boardY = showcaseCam.userFocusY + (screenY - vh * 0.5f) / oldZoom;
        showcaseCam.userZoom = newZoom;
        if (newZoom <= 1.002f) {
            resetUserCamera();
        } else {
            showcaseCam.userFocusX = boardX - (screenX - vw * 0.5f) / newZoom;
            showcaseCam.userFocusY = boardY - (screenY - vh * 0.5f) / newZoom;
            clampUserCamera();
        }
    }

    void panUserCameraBy(float screenDx, float screenDy) noexcept {
        if (showcaseCam.userZoom <= 1.002f) return;
        showcaseCam.userFocusX -= screenDx / showcaseCam.userZoom;
        showcaseCam.userFocusY -= screenDy / showcaseCam.userZoom;
        clampUserCamera();
    }
    bool prevKeyN = false;
    bool prevKeyR = false;
    bool prevKeyZ = false;
    bool prevKeyY = false;
    bool prevKeyH = false;
    bool prevKeyA = false;
    bool prevKeyT = false;
    bool prevKeyD = false;
    bool prevKeyW = false;
    bool prevKeyS = false;
    bool prevKeyC = false;
    bool prevKeyV = false;
    bool prevKeyM = false;
    bool prevKeyEsc = false;
    bool prevKeyF1 = false;
    bool helpOpen = false;
    int highlightedHelpZone = -1;
    float helpScrollY = 0.0f;
    float helpMaxScrollY = 0.0f;
    bool helpPointerDown = false;
    bool helpDraggingScroll = false;
    bool helpDraggingScrollbar = false;
    float helpDragStartX = 0.0f;
    float helpDragStartY = 0.0f;
    float helpInitialScrollY = 0.0f;
    float prevHelpMouseX = -1.0f;
    float prevHelpMouseY = -1.0f;
    std::string lastOddsGameId;
    size_t lastTrailedMoveIdx = SIZE_MAX;
    int magicSoundAccum = 0;
    ShowcaseCameraState showcaseCam;
    DirectorRuntimeState directorState;
    bool directorHideHud = false;
    bool directorAutoExit = false;
    std::string recordMovesPath;
    std::vector<DirectorBeat> recordedBeats;
    size_t lastRecordedUndoDepth = 0;
    bool lastRecordedSolvable = true;
    std::string recordStatusBanner;
    float recordStatusTimer = 0.0f;
    std::vector<GhostCardCue> activeGhostCards;
    MagicParticleSystem magicParticles;
    HintHighlight hintHighlight{};
    uint32_t hintMoveCount = 0;
    HelpOddsInfo helpOdds{};
    std::atomic<bool> oddsCancel{false};
    std::mutex oddsMutex;
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    std::thread oddsThread;
#endif

    HelpVideoThumb helpVideoThumb{};
    std::string videoThumbRequestedId;
    std::mutex videoThumbMutex;
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    std::thread videoThumbThread;
#endif

    static std::string getVideoThumbCachePath(const std::string& videoId) {
        std::filesystem::path dir;
#if defined(_WIN32)
        if (const char* appdata = std::getenv("APPDATA")) {
            dir = std::filesystem::path(appdata) / "SolEngine" / "cache";
        }
#else
        if (const char* home = std::getenv("HOME")) {
            dir = std::filesystem::path(home) / ".config" / "solengine" / "cache";
        }
#endif
        if (dir.empty()) {
            dir = std::filesystem::path("cache");
        }
        return (dir / ("yt_" + videoId + ".jpg")).string();
    }

    static bool isValidYouTubeId(const std::string& videoId) {
        if (videoId.empty() || videoId.size() > 32) return false;
        for (unsigned char ch : videoId) {
            if (!std::isalnum(ch) && ch != '-' && ch != '_') return false;
        }
        return true;
    }

    static bool decodeThumbBytes(const uint8_t* bytes, size_t len, const std::string& videoId, HelpVideoThumb& out) {
        if (!bytes || len == 0) return false;
        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load_from_memory(bytes, static_cast<int>(len), &w, &h, &comp, 4);
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels) stbi_image_free(pixels);
            return false;
        }
        out.videoId = videoId;
        out.width = w;
        out.height = h;
        out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
        stbi_image_free(pixels);
        return true;
    }

    static bool fetchYouTubeThumbBytes(const std::string& videoId, std::vector<uint8_t>& outBytes) {
        outBytes.clear();
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        HINTERNET hSession = WinHttpOpen(L"SolEngine/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        WinHttpSetTimeouts(hSession, 3500, 3500, 3500, 4500);
        HINTERNET hConnect = WinHttpConnect(hSession, L"i.ytimg.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }
        std::wstring path = L"/vi/";
        for (char c : videoId) path.push_back(static_cast<wchar_t>(c));
        path += L"/mqdefault.jpg";
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        bool ok = false;
        if (hRequest &&
            WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD statusCode = 0;
            DWORD statusLen = sizeof(statusCode);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusLen, WINHTTP_NO_HEADER_INDEX) &&
                statusCode == 200) {
                constexpr size_t kMaxBytes = 512u * 1024u;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                    if (outBytes.size() + avail > kMaxBytes) {
                        outBytes.clear();
                        break;
                    }
                    const size_t prevSize = outBytes.size();
                    outBytes.resize(prevSize + avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, outBytes.data() + prevSize, avail, &read)) {
                        outBytes.clear();
                        break;
                    }
                    outBytes.resize(prevSize + read);
                }
                ok = !outBytes.empty();
            }
        }
        if (hRequest) WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ok;
#elif !defined(__EMSCRIPTEN__)
        const std::string cmd = "curl -fsSL --max-time 4 \"https://i.ytimg.com/vi/" + videoId + "/mqdefault.jpg\"";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return false;
        uint8_t buf[4096];
        size_t n = 0;
        constexpr size_t kMaxBytes = 512u * 1024u;
        while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
            if (outBytes.size() + n > kMaxBytes) {
                outBytes.clear();
                break;
            }
            outBytes.insert(outBytes.end(), buf, buf + n);
        }
        const int rc = pclose(pipe);
        return (rc == 0 && !outBytes.empty());
#else
        (void)videoId;
        return false;
#endif
    }

    void cancelVideoThumb() {
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        if (videoThumbThread.joinable()) {
            videoThumbThread.join();
        }
#endif
    }

    void ensureHelpVideoThumb(const std::string& videoId, const std::string& base64Fallback) {
        if (!isValidYouTubeId(videoId)) {
            if (!videoThumbRequestedId.empty()) {
                cancelVideoThumb();
                std::lock_guard<std::mutex> lk(videoThumbMutex);
                helpVideoThumb = {};
                videoThumbRequestedId.clear();
            }
            return;
        }
        if (videoThumbRequestedId == videoId) {
            return;
        }
        cancelVideoThumb();
        {
            std::lock_guard<std::mutex> lk(videoThumbMutex);
            helpVideoThumb = {};
            videoThumbRequestedId = videoId;
        }
        if (!base64Fallback.empty()) {
            std::vector<uint8_t> raw = eatsbits::ui::base64Decode(base64Fallback);
            HelpVideoThumb decoded;
            if (decodeThumbBytes(raw.data(), raw.size(), videoId, decoded)) {
                std::lock_guard<std::mutex> lk(videoThumbMutex);
                helpVideoThumb = std::move(decoded);
                return;
            }
        }
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        videoThumbThread = std::thread([this, videoId]() {
            const std::string cachePath = getVideoThumbCachePath(videoId);
            std::vector<uint8_t> jpgBytes;
            {
                std::ifstream ifs(cachePath, std::ios::binary | std::ios::ate);
                if (ifs.is_open()) {
                    const std::streamsize sz = ifs.tellg();
                    if (sz > 0 && sz <= 512 * 1024) {
                        jpgBytes.resize(static_cast<size_t>(sz));
                        ifs.seekg(0);
                        if (!ifs.read(reinterpret_cast<char*>(jpgBytes.data()), sz)) {
                            jpgBytes.clear();
                        }
                    }
                }
            }
            HelpVideoThumb decoded;
            if (!jpgBytes.empty() && decodeThumbBytes(jpgBytes.data(), jpgBytes.size(), videoId, decoded)) {
                std::lock_guard<std::mutex> lk(videoThumbMutex);
                if (videoThumbRequestedId == videoId) {
                    helpVideoThumb = std::move(decoded);
                }
                return;
            }
            if (fetchYouTubeThumbBytes(videoId, jpgBytes) &&
                decodeThumbBytes(jpgBytes.data(), jpgBytes.size(), videoId, decoded)) {
                try {
                    std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path());
                    std::ofstream ofs(cachePath, std::ios::binary);
                    if (ofs.is_open()) {
                        ofs.write(reinterpret_cast<const char*>(jpgBytes.data()),
                                  static_cast<std::streamsize>(jpgBytes.size()));
                    }
                } catch (...) {}
                std::lock_guard<std::mutex> lk(videoThumbMutex);
                if (videoThumbRequestedId == videoId) {
                    helpVideoThumb = std::move(decoded);
                }
            }
        });
#endif
    }

    bool hasHelpVideoThumb() {
        std::lock_guard<std::mutex> lk(videoThumbMutex);
        return helpVideoThumb.ready() && helpVideoThumb.videoId == board.docManifest().videoId;
    }

    void cancelOddsAnalysis() {
        oddsCancel.store(true);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        if (oddsThread.joinable()) {
            oddsThread.join();
        }
#endif
        std::lock_guard<std::mutex> lk(oddsMutex);
        helpOdds.running = false;
    }

    void startOddsAnalysis() {
        cancelOddsAnalysis();
        oddsCancel.store(false);
        {
            std::lock_guard<std::mutex> lk(oddsMutex);
            helpOdds = {};
            helpOdds.running = true;
            helpOdds.totalSeeds = 12;
        }
        const uint32_t baseSeed = board.seed();
        const SolitaireGameDef defCopy = board.activeGameDef();
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        oddsThread = std::thread([this, baseSeed, defCopy]() {
            uint32_t solved = 0;
            uint32_t timedOutCount = 0;
            std::vector<uint32_t> solvedStates;
            std::vector<uint32_t> solvedMoves;
            solvedStates.reserve(12);
            solvedMoves.reserve(12);
            SolitaireSolver solver;
            for (uint32_t i = 0; i < 12; ++i) {
                if (oddsCancel.load(std::memory_order_relaxed)) break;
                SolitaireBoard probe;
                probe.newDeal(baseSeed + 1 + i, defCopy);
                SolveResult res = solver.solve(probe, 20000, 220.0f, &oddsCancel);
                if (res.solved) {
                    ++solved;
                    solvedStates.push_back(res.statesExplored);
                    solvedMoves.push_back(static_cast<uint32_t>(res.moves.size()));
                } else if (res.timedOut) {
                    ++timedOutCount;
                }
                std::lock_guard<std::mutex> lk(oddsMutex);
                helpOdds.completedSeeds = i + 1;
            }
            std::lock_guard<std::mutex> lk(oddsMutex);
            const uint32_t done = std::max<uint32_t>(1, helpOdds.completedSeeds);
            const uint32_t pct = (solved * 100u) / done;
            helpOdds.oddsRating = static_cast<uint8_t>(std::clamp<uint32_t>(1 + pct / 20u, 1u, 5u));
            char oBuf[32];
            std::snprintf(oBuf, sizeof(oBuf), "%u%% (%u/%u)", pct, solved, done);
            helpOdds.oddsLabel = oBuf;

            auto medianOf = [](std::vector<uint32_t>& v) -> uint32_t {
                if (v.empty()) return 0;
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            const uint32_t medSolvedStates = medianOf(solvedStates);
            const uint32_t medSolvedMoves = medianOf(solvedMoves);
            const float solveRate = static_cast<float>(solved) / static_cast<float>(done);

            // Calibrate skill/difficulty primarily by empirical win rate, adjusted by
            // planning depth on solved deals (avoiding dead-end search-cap bias).
            int skill = 3;
            if (solveRate >= 0.90f) {
                skill = 1;
                if (medSolvedStates >= 400 || medSolvedMoves >= 80) skill = 3;
                else if (medSolvedStates >= 150 || medSolvedMoves >= 55) skill = 2;
            } else if (solveRate >= 0.70f) {
                skill = 2;
                if (medSolvedStates >= 1200 || medSolvedMoves >= 130) skill = 3;
            } else if (solveRate >= 0.35f) {
                skill = 3;
                if (medSolvedStates >= 2000) skill = 4;
            } else if (solveRate >= 0.15f) {
                skill = 4;
                if (medSolvedStates >= 3000) skill = 5;
            } else {
                skill = (timedOutCount > done / 2) ? 4 : 5;
            }
            helpOdds.skillRating = static_cast<uint8_t>(std::clamp(skill, 1, 5));
            static constexpr const char* kSkillLabels[5] = { "Easy", "Casual", "Moderate", "Hard", "Expert" };
            helpOdds.skillLabel = kSkillLabels[helpOdds.skillRating - 1];

            if (solved > 0) {
                if (medSolvedMoves < 45) { helpOdds.timeRating = 1; helpOdds.timeLabel = "Fast"; }
                else if (medSolvedMoves < 85) { helpOdds.timeRating = 2; helpOdds.timeLabel = "Quick"; }
                else if (medSolvedMoves < 140) { helpOdds.timeRating = 3; helpOdds.timeLabel = "Medium"; }
                else if (medSolvedMoves < 220) { helpOdds.timeRating = 4; helpOdds.timeLabel = "Long"; }
                else { helpOdds.timeRating = 5; helpOdds.timeLabel = "Marathon"; }
            } else {
                const uint32_t cards = (defCopy.deckCards > 0)
                    ? defCopy.deckCards
                    : static_cast<uint32_t>(std::max<uint8_t>(1, defCopy.decks)) * 52u;
                if (cards < 45) { helpOdds.timeRating = 2; helpOdds.timeLabel = "Quick"; }
                else if (cards <= 52) { helpOdds.timeRating = 3; helpOdds.timeLabel = "Medium"; }
                else { helpOdds.timeRating = 4; helpOdds.timeLabel = "Long"; }
            }
            helpOdds.hasResults = !oddsCancel.load(std::memory_order_relaxed);
            helpOdds.running = false;
        });
#endif
    }
    bool prevKeyF11 = false;
    bool prevKeyEnter = false;
    bool prevKey1 = false;
    bool prevKey2 = false;
    bool prevKey3 = false;
    bool prevKey4 = false;
    bool prevKey5 = false;
    bool prevKey6 = false;
    bool prevKey7 = false;
    bool prevKeyO = false;
    bool prevKeyBack = false;
    bool prevKeyTab = false;
    bool prevKeyJ = false;
    bool prevKeyL = false;
    bool prevKeyG = false;
    bool prevKeyUp = false;
    bool prevKeyDown = false;
    bool prevKeySpace = false;
    bool prevKeyP = false;
    bool prevKeyF = false;
    bool prevKeyLBracket = false;
    bool prevKeyRBracket = false;

    bool isFullscreen = false;
    bool hasSavedWindowState = false;
    int savedWindowX = 100;
    int savedWindowY = 100;
    int savedWindowW = 1280;
    int savedWindowH = 720;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    LONG savedWindowStyle = 0;
#endif

    bool menuOpen = false;
    MenuAnchor menuAnchor = MenuAnchor::BottomLeft;
    bool menuFlowVertical = false;

    bool isClickingSolEngineOrb = false;
    bool isDraggingMenu = false;
    float menuDragStartMouseX = 0.0f;
    float menuDragStartMouseY = 0.0f;
    MenuAnchor dragPreviewAnchor = MenuAnchor::BottomLeft;
    bool dragPreviewFlowVertical = false;

    bool isGesturing = false;
    float gestureStartX = 0.0f;
    float gestureStartY = 0.0f;
    float gestureCurX = 0.0f;
    float gestureCurY = 0.0f;

    // Toolbar hold and context menu state
    int pressedToolbarBtn = BtnNone;
    float pressStartTime = 0.0f;
    float pressStartX = 0.0f;
    float pressStartY = 0.0f;
    bool holdTriggered = false;

    bool playMenuOpen = false;
    float playMenuAnchorX = 0.0f;
    float playMenuAnchorY = 0.0f;

    bool toolbarConfigOpen = false;
    float toolbarConfigAnchorX = 0.0f;
    float toolbarConfigAnchorY = 0.0f;

    bool autoCascadeActive = false;
    float autoCascadeTimer = 0.0f;
    bool screensaverActive = false;
    VictoryGatherState victoryGather;
    bool pendingGatherDeal = false;
    uint32_t pendingGatherSeed = 0;
    bool pendingGatherSeedReady = false;
    bool pendingGatherSilent = false;
    double lastTime = 0.0;
    SettingsDialog settingsDialog;
    SeedDialog seedDialog;
    LoadingDialog loadingDialog;
    GameBrowserDialog gameBrowserDialog;
    DynamicLightController lightController;

    enum class AsyncOp {
        None,
        Conditioning,
        DeckLoad,
        TableTheme,
        FileDrop,
        SolvableDeal
    };

    struct AsyncManager {
        std::atomic<bool> isBusy{false};
        std::atomic<bool> cancelRequested{false};
        std::atomic<float> progress{0.0f};
        std::mutex mutex;
        std::string statusText{"Initializing..."};
        AsyncOp opType{AsyncOp::None};
        CardTextureCache::StagingDeck stagingDeck;
        bool stagingDeckReady{false};
        bool customBgReady{false};
        bool customAudioReady{false};
        EatsLoadResult pendingEatsResult;
        bool pendingEatsReady{false};
        TableThemeId targetTheme{TableThemeId::EmeraldLawn};
        std::string targetThemeSlug;
        bool themeReady{false};
        uint32_t solvableSeed{0};
        bool solvableSeedReady{false};
        SolitaireGameDef solvableDef;
        std::string dialogTitle{"PREPARING PROCEDURAL ASSETS"};
        std::string dialogInitialStatus{"Initializing..."};
        bool dialogCancellable{true};
        bool silent{false};
        double startTime{0.0};
        double debounceSeconds{0.12}; // 120ms debounce threshold to prevent micro-flashes on fast cache hits
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        std::thread workerThread;
#endif

        void joinWorker() {
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
            if (workerThread.joinable()) {
                workerThread.join();
            }
#endif
        }

        void stopWorker() {
            if (isBusy.load()) {
                cancelRequested.store(true);
            }
            joinWorker();
        }

        void resetState() {
            opType = AsyncOp::None;
            themeReady = false;
            targetThemeSlug.clear();
            stagingDeckReady = false;
            stagingDeck = CardTextureCache::StagingDeck{};
            customBgReady = false;
            customAudioReady = false;
            pendingEatsResult = EatsLoadResult{};
            pendingEatsReady = false;
            solvableSeedReady = false;
            silent = false;
            cancelRequested.store(false);
            isBusy.store(false);
            progress.store(0.0f);
        }

        ~AsyncManager() {
            stopWorker();
        }
    } asyncManager;

    void startAsyncTableTheme(const TableThemeDef& def) {
        asyncManager.stopWorker();

        auto& settings = SettingsManager::instance().settings();
        if (!def.options.empty()) {
            auto it = settings.tableThemeOptions.find(def.id);
            settings.tableThemeOption = (it != settings.tableThemeOptions.end()) ? it->second : def.options[0].id;
            settings.tableThemeOptions[def.id] = settings.tableThemeOption;
        } else {
            settings.tableThemeOption.clear();
        }
        board.setTableThemeOption(settings.tableThemeOption);
        ProceduralBackgroundSystem::instance().setThemeOption(settings.tableThemeOption);

        if (def.filePath.empty()) {
            ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
            board.setTableTheme(TableThemeId::PastelTan);
            board.setTableThemeSlug(def.id);
            lightController.applyThemeDefault(TableThemeId::PastelTan);
            settings.tableTheme = TableThemeId::PastelTan;
            settings.tableThemeSlug = def.id;
            SettingsManager::instance().save();
            ProceduralBackgroundSystem::instance().invalidateCache();
            asyncManager.resetState();
            foley.triggerSound(FoleySound::WoodClick, 0.85f);
            return;
        }

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.themeReady = false;
        asyncManager.opType = AsyncOp::TableTheme;
        asyncManager.targetTheme = (def.legacyId != TableThemeId::Count) ? def.legacyId : TableThemeId::PaintedWood;
        asyncManager.targetThemeSlug = def.id;
        asyncManager.dialogTitle = "SYNTHESIZING TABLE SURFACE";
        asyncManager.dialogInitialStatus = "Synthesizing tactile surface texture: " + def.name + "...";
        asyncManager.dialogCancellable = true;
        asyncManager.startTime = glfwGetTime();
        asyncManager.debounceSeconds = 0.05;
        asyncManager.isBusy.store(true);
        {
            std::lock_guard<std::mutex> lock(asyncManager.mutex);
            asyncManager.statusText = "Synthesizing procedural table surface: " + def.name + "...";
        }

        float curW = static_cast<float>(width);
        float curH = static_cast<float>(height);
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        bool ok = ProceduralBackgroundSystem::instance().prebakeThemeDef(
            def, curW, curH,
            eatsbits::ui::LightSource2D{curW * 0.5f, -200.0f, 1.0f},
            [this](float p, const std::string& msg) -> bool {
                asyncManager.progress.store(p);
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    asyncManager.statusText = msg;
                }
                return !asyncManager.cancelRequested.load();
            }
        );
        if (ok && !asyncManager.cancelRequested.load()) {
            asyncManager.themeReady = true;
        }
        asyncManager.isBusy.store(false);
#else
        asyncManager.workerThread = std::thread([this, def, curW, curH]() {
            bool ok = ProceduralBackgroundSystem::instance().prebakeThemeDef(
                def, curW, curH,
                eatsbits::ui::LightSource2D{curW * 0.5f, -200.0f, 1.0f},
                [this](float p, const std::string& msg) -> bool {
                    asyncManager.progress.store(p);
                    {
                        std::lock_guard<std::mutex> lock(asyncManager.mutex);
                        asyncManager.statusText = msg;
                    }
                    return !asyncManager.cancelRequested.load();
                }
            );
            if (ok && !asyncManager.cancelRequested.load()) {
                asyncManager.themeReady = true;
            }
            asyncManager.isBusy.store(false);
        });
#endif
    }

    void startAsyncTableTheme(TableThemeId theme) {
        size_t idx = ProceduralBackgroundSystem::instance().findThemeIndex("", theme);
        const auto* def = ProceduralBackgroundSystem::instance().getThemeDef(idx);
        if (def) {
            startAsyncTableTheme(*def);
        } else {
            TableThemeDef fallbackDef{"", SolitaireBoard::themeName(theme), "", theme, false};
            startAsyncTableTheme(fallbackDef);
        }
    }

    void startAsyncConditioning(CardWearLevel wear, CardStock stock, bool silent = false) {
        asyncManager.stopWorker();
        asyncManager.silent = silent;

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.stagingDeckReady = false;
        asyncManager.stagingDeck = CardTextureCache::StagingDeck{};
        asyncManager.stagingDeck.hasNewBase = false;
        asyncManager.stagingDeck.theme = CardTextureCache::instance().currentDeckTheme();
        asyncManager.stagingDeck.deckId = CardTextureCache::instance().currentDeckId();
        asyncManager.stagingDeck.cardW = CardTextureCache::instance().cardWidth();
        asyncManager.stagingDeck.cardH = CardTextureCache::instance().cardHeight();
        asyncManager.opType = AsyncOp::Conditioning;
        asyncManager.dialogTitle = "CONDITIONING PLAYING CARDS";
        asyncManager.dialogInitialStatus = "Synthesizing physical paper stock & wear...";
        asyncManager.dialogCancellable = true;
        asyncManager.startTime = glfwGetTime();
        asyncManager.debounceSeconds = 0.12;
        asyncManager.isBusy.store(true);
        {
            std::lock_guard<std::mutex> lock(asyncManager.mutex);
            asyncManager.statusText = "Initializing card paper simulation...";
        }

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        bool ok = CardTextureCache::instance().bakeStagedConditioning(
            asyncManager.stagingDeck, wear, stock,
            [this](float p, const std::string& msg) -> bool {
                asyncManager.progress.store(p);
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    asyncManager.statusText = msg;
                }
                return !asyncManager.cancelRequested.load();
            }
        );
        if (ok && !asyncManager.cancelRequested.load()) {
            asyncManager.stagingDeckReady = true;
        }
        asyncManager.isBusy.store(false);
#else
        asyncManager.workerThread = std::thread([this, wear, stock]() {
            bool ok = CardTextureCache::instance().bakeStagedConditioning(
                asyncManager.stagingDeck, wear, stock,
                [this](float p, const std::string& msg) -> bool {
                    asyncManager.progress.store(p);
                    {
                        std::lock_guard<std::mutex> lock(asyncManager.mutex);
                        asyncManager.statusText = msg;
                    }
                    return !asyncManager.cancelRequested.load();
                }
            );
            if (ok && !asyncManager.cancelRequested.load()) {
                asyncManager.stagingDeckReady = true;
            }
            asyncManager.isBusy.store(false);
        });
#endif
    }

    void startAsyncDeckTheme(DeckThemeId theme, CardWearLevel wear, CardStock stock, bool silent = false) {
        asyncManager.stopWorker();
        asyncManager.silent = silent;

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.stagingDeckReady = false;
        asyncManager.opType = AsyncOp::DeckLoad;
        asyncManager.dialogTitle = "LOADING DECK THEME";
        asyncManager.dialogInitialStatus = "Loading artwork and synthesizing textures...";
        asyncManager.dialogCancellable = true;
        asyncManager.startTime = glfwGetTime();
        asyncManager.debounceSeconds = 0.12;
        asyncManager.isBusy.store(true);
        {
            std::lock_guard<std::mutex> lock(asyncManager.mutex);
            asyncManager.statusText = "Loading card artwork master...";
        }

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        bool ok = CardTextureCache::instance().loadStagedDeckTheme(
            asyncManager.stagingDeck, theme, wear, stock,
            CardTextureCache::kDefaultCardW, CardTextureCache::kDefaultCardH,
            [this](float p, const std::string& msg) -> bool {
                asyncManager.progress.store(p);
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    asyncManager.statusText = msg;
                }
                return !asyncManager.cancelRequested.load();
            }
        );
        if (ok && !asyncManager.cancelRequested.load()) {
            asyncManager.stagingDeckReady = true;
        }
        asyncManager.isBusy.store(false);
#else
        asyncManager.workerThread = std::thread([this, theme, wear, stock]() {
            bool ok = CardTextureCache::instance().loadStagedDeckTheme(
                asyncManager.stagingDeck, theme, wear, stock,
                CardTextureCache::kDefaultCardW, CardTextureCache::kDefaultCardH,
                [this](float p, const std::string& msg) -> bool {
                    asyncManager.progress.store(p);
                    {
                        std::lock_guard<std::mutex> lock(asyncManager.mutex);
                        asyncManager.statusText = msg;
                    }
                    return !asyncManager.cancelRequested.load();
                }
            );
            if (ok && !asyncManager.cancelRequested.load()) {
                asyncManager.stagingDeckReady = true;
            }
            asyncManager.isBusy.store(false);
        });
#endif
    }

    void startAsyncDeckDef(const CardTextureCache::DeckDef& def, CardWearLevel wear, CardStock stock, bool silent = false) {
#if defined(__EMSCRIPTEN__)
        const bool isProceduralBuiltin = (def.legacyId == DeckThemeId::SolEngineClassic ||
                                          def.legacyId == DeckThemeId::SolPolyWhite ||
                                          def.legacyId == DeckThemeId::SolPolyDark ||
                                          def.legacyId == DeckThemeId::ScoundrelLowPoly ||
                                          def.legacyId == DeckThemeId::MemoryLowPoly ||
                                          def.legacyId == DeckThemeId::SolPolyClassic ||
                                          def.legacyId == DeckThemeId::SolPolyMobile ||
                                          def.legacyId == DeckThemeId::LowPolyWar);
        if (def.filePath.empty() && !isProceduralBuiltin) {
            std::string jsName = def.id;
            EM_ASM({
                if (window.loadDeckByName) {
                    window.loadDeckByName(UTF8ToString($0));
                }
            }, jsName.c_str());
            return;
        }
#endif
        asyncManager.stopWorker();
        asyncManager.silent = silent;

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.stagingDeckReady = false;
        asyncManager.opType = AsyncOp::DeckLoad;
        asyncManager.dialogTitle = "LOADING DECK THEME";
        asyncManager.dialogInitialStatus = "Loading " + def.name + "...";
        asyncManager.dialogCancellable = true;
        asyncManager.startTime = glfwGetTime();
        asyncManager.debounceSeconds = 0.12;
        asyncManager.isBusy.store(true);
        {
            std::lock_guard<std::mutex> lock(asyncManager.mutex);
            asyncManager.statusText = "Loading card artwork master...";
        }

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        bool ok = CardTextureCache::instance().loadStagedDeckByDef(
            asyncManager.stagingDeck, def, wear, stock,
            CardTextureCache::kDefaultCardW, CardTextureCache::kDefaultCardH,
            [this](float p, const std::string& msg) -> bool {
                asyncManager.progress.store(p);
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    asyncManager.statusText = msg;
                }
                return !asyncManager.cancelRequested.load();
            }
        );
        if (ok && !asyncManager.cancelRequested.load()) {
            asyncManager.stagingDeckReady = true;
        }
        asyncManager.isBusy.store(false);
#else
        asyncManager.workerThread = std::thread([this, def, wear, stock]() {
            bool ok = CardTextureCache::instance().loadStagedDeckByDef(
                asyncManager.stagingDeck, def, wear, stock,
                CardTextureCache::kDefaultCardW, CardTextureCache::kDefaultCardH,
                [this](float p, const std::string& msg) -> bool {
                    asyncManager.progress.store(p);
                    {
                        std::lock_guard<std::mutex> lock(asyncManager.mutex);
                        asyncManager.statusText = msg;
                    }
                    return !asyncManager.cancelRequested.load();
                }
            );
            if (ok && !asyncManager.cancelRequested.load()) {
                asyncManager.stagingDeckReady = true;
            }
            asyncManager.isBusy.store(false);
        });
#endif
    }

    bool runStagedEatsDrop(
        const std::string& filePath,
        CardWearLevel wear,
        CardStock stock,
        int cardW,
        int cardH,
        const CardTextureCache::ProgressCallback& progressCb
    ) {
        if (cardW < CardTextureCache::kDefaultCardW || cardH < CardTextureCache::kDefaultCardH) {
            cardW = CardTextureCache::kDefaultCardW;
            cardH = CardTextureCache::kDefaultCardH;
        }

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Try unified .eats v2 package loader first
        EatsLoadResult pkgRes;
        if (EatsPackageLoader::loadSource(content, filePath, pkgRes, &foley) && pkgRes.hasAnyAsset()) {
            if (!pkgRes.deckIds.empty()) {
                const auto& decks = CardTextureCache::instance().availableDecks();
                const std::string& targetDeckId = pkgRes.deckIds.front();
                for (const auto& d : decks) {
                    if (d.id == targetDeckId) {
                        if (!d.blobBase64.empty()) {
                            std::vector<uint8_t> deckBytes = eatsbits::ui::base64Decode(d.blobBase64);
                            if (!deckBytes.empty()) {
                                if (d.cornerRadius >= 0.0f) asyncManager.stagingDeck.cornerRadius = d.cornerRadius;
                                if (CardTextureCache::instance().loadStagedMemoryDeck(
                                        asyncManager.stagingDeck, deckBytes.data(), deckBytes.size(),
                                        d.format.empty() ? "evecz" : d.format, DeckThemeId::Custom,
                                        filePath, wear, stock, cardW, cardH, progressCb)) {
                                    asyncManager.stagingDeckReady = true;
                                }
                            }
                        } else if (CardTextureCache::instance().loadStagedFileDeck(
                                       asyncManager.stagingDeck, filePath, wear, stock, cardW, cardH, progressCb)) {
                            asyncManager.stagingDeckReady = true;
                        }
                        break;
                    }
                }
            }
            asyncManager.pendingEatsResult = std::move(pkgRes);
            asyncManager.pendingEatsReady = true;
            if (progressCb) progressCb(1.0f, "Loaded .eats package.");
            return true;
        }

        std::string scriptPart = content;
        std::string blobPart;
        size_t markerPos = content.find("__BLOB__");
        if (markerPos == std::string::npos) {
            markerPos = content.find("__DATA__");
        }
        if (markerPos != std::string::npos) {
            scriptPart = content.substr(0, markerPos);
            size_t nextLine = content.find('\n', markerPos);
            if (nextLine != std::string::npos) {
                blobPart = content.substr(nextLine + 1);
            }
        }

        // Check if this Eatscript file is a procedural audio soundpack
        if (scriptPart.find("audio.") != std::string::npos ||
            scriptPart.find("define_sound") != std::string::npos ||
            scriptPart.find("define_sequence") != std::string::npos) {
            if (progressCb) progressCb(0.25f, "Configuring procedural audio synthesis...");
            if (foley.loadFromEatsScript(scriptPart)) {
                asyncManager.customAudioReady = true;
                if (progressCb) progressCb(1.0f, "Custom audio soundpack activated.");
                return true;
            }
        }

        eatsbits::eatscript::Evaluator evaluator;
        bool bgConfigured = false;
        bool setDeckCalled = false;
        std::vector<uint8_t> capturedDeckBytes;
        std::string capturedFormat = "svgz";
        int capturedTargetW = cardW;
        int capturedTargetH = cardH;
        DeckThemeId capturedDeckTheme = DeckThemeId::Custom;
        float capturedCornerRadius = -1.0f;

        auto setThemeFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                              const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
            if (kwargs.find("corner_radius") != kwargs.end()) {
                capturedCornerRadius = kwargs.at("corner_radius").asFloat(24.0f);
            } else if (args.size() > 1 && args[1].isDict() && args[1].hasKey("corner_radius")) {
                capturedCornerRadius = args[1].get("corner_radius").asFloat(24.0f);
            } else if (!args.empty() && args[0].isDict() && args[0].hasKey("corner_radius")) {
                capturedCornerRadius = args[0].get("corner_radius").asFloat(24.0f);
            }
            return eatsbits::eatscript::Value(true);
        };

        auto setCornerRadiusFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                                     const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
            float r = 24.0f;
            if (!args.empty() && args[0].isNumber()) {
                r = args[0].asFloat(24.0f);
            } else if (kwargs.find("radius") != kwargs.end()) {
                r = kwargs.at("radius").asFloat(24.0f);
            }
            capturedCornerRadius = r;
            return eatsbits::eatscript::Value(true);
        };

        auto setBgFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                           const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
            if (progressCb && !progressCb(0.05f, "Decoding custom table background...")) {
                return eatsbits::eatscript::Value(false);
            }
            std::string rawStr;
            if (!args.empty() && args[0].isString()) {
                rawStr = args[0].asString();
            } else if (kwargs.find("blob") != kwargs.end()) {
                rawStr = kwargs.at("blob").asString();
            }

            std::vector<uint8_t> bgBytes;
            if (rawStr.rfind("data:", 0) == 0) {
                auto parsed = eatsbits::ui::parseDataUri(rawStr);
                if (parsed && !parsed->data.empty()) {
                    bgBytes = std::move(parsed->data);
                }
            }
            if (bgBytes.empty() && !rawStr.empty()) {
                bgBytes = eatsbits::ui::base64Decode(rawStr);
                if (bgBytes.empty()) {
                    bgBytes.assign(rawStr.begin(), rawStr.end());
                }
            }
            if (bgBytes.empty() && !blobPart.empty()) {
                bgBytes.assign(blobPart.begin(), blobPart.end());
            }

            const eatsbits::eatscript::Value* optsDict = nullptr;
            if (args.size() > 1 && args[1].isDict()) {
                optsDict = &args[1];
            }

            auto getOptFloat = [&](const std::string& key, float defVal) {
                if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asFloat(defVal);
                if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asFloat(defVal);
                return defVal;
            };

            float uvScale = getOptFloat("uv_scale", 2.0f);
            float normalStrength = getOptFloat("normal_strength", 1.35f);
            float specularIntensity = getOptFloat("specular_intensity", 0.65f);
            float ambientLevel = getOptFloat("ambient_level", 0.22f);
            float diffuseLevel = getOptFloat("diffuse_level", 0.88f);
            float tintR = getOptFloat("tint_r", 1.0f);
            float tintG = getOptFloat("tint_g", 1.0f);
            float tintB = getOptFloat("tint_b", 1.0f);

            if (optsDict && optsDict->hasKey("tint")) {
                auto tintVal = optsDict->get("tint");
                if (tintVal.isList() && tintVal.listVal.size() >= 3) {
                    tintR = tintVal.listVal[0].asFloat(tintR);
                    tintG = tintVal.listVal[1].asFloat(tintG);
                    tintB = tintVal.listVal[2].asFloat(tintB);
                }
            }

            if (!bgBytes.empty() &&
                ProceduralBackgroundSystem::instance().loadCustomThemeFromMemory(
                    bgBytes.data(), bgBytes.size(),
                    uvScale, normalStrength, specularIntensity,
                    ambientLevel, diffuseLevel, tintR, tintG, tintB)) {
                bgConfigured = true;
                return eatsbits::eatscript::Value(true);
            }
            return eatsbits::eatscript::Value(false);
        };

        auto setDeckFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                             const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
            std::string rawStr;
            if (!args.empty() && args[0].isString()) {
                rawStr = args[0].asString();
            } else if (kwargs.find("blob") != kwargs.end()) {
                rawStr = kwargs.at("blob").asString();
            }

            std::vector<uint8_t> deckBytes;
            std::string format = "svgz";

            if (rawStr.rfind("data:", 0) == 0) {
                auto parsed = eatsbits::ui::parseDataUri(rawStr);
                if (parsed && !parsed->data.empty()) {
                    deckBytes = std::move(parsed->data);
                    if (parsed->mimeType.find("svg") != std::string::npos) format = "svg";
                    else if (parsed->mimeType.find("png") != std::string::npos) format = "png";
                    else if (parsed->mimeType.find("jpeg") != std::string::npos || parsed->mimeType.find("jpg") != std::string::npos) format = "jpg";
                }
            }
            if (deckBytes.empty() && !rawStr.empty()) {
                deckBytes = eatsbits::ui::base64Decode(rawStr);
                if (deckBytes.empty()) {
                    deckBytes.assign(rawStr.begin(), rawStr.end());
                }
            }
            if (deckBytes.empty() && !blobPart.empty()) {
                deckBytes.assign(blobPart.begin(), blobPart.end());
            }

            const eatsbits::eatscript::Value* optsDict = nullptr;
            if (args.size() > 1 && args[1].isDict()) {
                optsDict = &args[1];
            }

            auto getOptString = [&](const std::string& key, const std::string& defVal) {
                if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asString();
                if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asString();
                return defVal;
            };
            auto getOptInt = [&](const std::string& key, int defVal) {
                if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asInt(defVal);
                if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asInt(defVal);
                return defVal;
            };

            format = getOptString("format", format);
            int targetW = getOptInt("card_w", cardW);
            int targetH = getOptInt("card_h", cardH);

            if (kwargs.find("corner_radius") != kwargs.end()) {
                capturedCornerRadius = kwargs.at("corner_radius").asFloat(24.0f);
            } else if (optsDict && optsDict->hasKey("corner_radius")) {
                capturedCornerRadius = optsDict->get("corner_radius").asFloat(24.0f);
            }

            std::string themeId = getOptString("theme_id", getOptString("theme", ""));
            DeckThemeId deckTheme = DeckThemeId::Custom;
            if (themeId == "baroque_cell_shaded" || themeId == "imperial_baroque" ||
                filePath.find("baroque_cell_shaded") != std::string::npos || filePath.find("imperial_baroque") != std::string::npos) {
                deckTheme = DeckThemeId::BaroqueCellShaded;
            } else if (themeId == "shader_stained_glass" || filePath.find("shader_stained_glass") != std::string::npos) {
                deckTheme = DeckThemeId::StainedGlass;
            } else if (themeId == "shader_holo_synthwave" || filePath.find("shader_holo_synthwave") != std::string::npos) {
                deckTheme = DeckThemeId::PolychromeCrt;
            } else if (themeId == "shader_watercolor_sketch" || filePath.find("shader_watercolor_sketch") != std::string::npos) {
                deckTheme = DeckThemeId::WatercolorSketch;
            } else if (themeId == "baroque_noir" || themeId == "baroque_relief" || themeId == "baroque_arches" || themeId == "baroque" ||
                filePath.find("baroque_noir") != std::string::npos || filePath.find("baroque") != std::string::npos) {
                deckTheme = DeckThemeId::BaroqueNoir;
            } else if (themeId == "baroque_light" || themeId == "baroque_alabaster" ||
                       filePath.find("baroque_light") != std::string::npos || filePath.find("baroque_alabaster") != std::string::npos) {
                deckTheme = DeckThemeId::BaroqueLight;
            } else if (themeId == "baroque_dark" || filePath.find("baroque_dark") != std::string::npos) {
                deckTheme = DeckThemeId::BaroqueDark;
            } else if (themeId == "solengine" || themeId == "solenshade") {
                deckTheme = DeckThemeId::SolEngineClassic;
            } else if (themeId == "classic") {
                deckTheme = DeckThemeId::ClassicSvg;
            } else if (themeId == "steampunk") {
                deckTheme = DeckThemeId::SteampunkAi;
            } else if (themeId == "anime") {
                deckTheme = DeckThemeId::Anime;
            } else if (themeId == "cyberpunk") {
                deckTheme = DeckThemeId::Cyberpunk;
            }

            if (!deckBytes.empty()) {
                setDeckCalled = true;
                capturedDeckBytes = std::move(deckBytes);
                capturedFormat = format;
                capturedTargetW = targetW;
                capturedTargetH = targetH;
                capturedDeckTheme = deckTheme;
                return eatsbits::eatscript::Value(true);
            }
            return eatsbits::eatscript::Value(false);
        };

        evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
        evaluator.registerMemberFunction("sol", "create_theme", setThemeFn);
        evaluator.registerMemberFunction("sol", "set_theme", setThemeFn);
        evaluator.registerMemberFunction("sol", "set_background", setBgFn);
        evaluator.registerMemberFunction("sol", "set_deck", setDeckFn);
        evaluator.registerMemberFunction("sol", "set_corner_radius", setCornerRadiusFn);

        evaluator.registerFunction("create_theme", setThemeFn);
        evaluator.registerFunction("set_theme", setThemeFn);
        evaluator.registerFunction("set_background", setBgFn);
        evaluator.registerFunction("set_deck", setDeckFn);
        evaluator.registerFunction("set_corner_radius", setCornerRadiusFn);

        evaluator.evaluateSource(scriptPart);

        bool deckConfigured = false;
        if (setDeckCalled && !capturedDeckBytes.empty()) {
            asyncManager.stagingDeck.cornerRadius = capturedCornerRadius;
            if (CardTextureCache::instance().loadStagedMemoryDeck(
                    asyncManager.stagingDeck,
                    capturedDeckBytes.data(),
                    capturedDeckBytes.size(),
                    capturedFormat,
                    capturedDeckTheme,
                    filePath,
                    wear,
                    stock,
                    capturedTargetW,
                    capturedTargetH,
                    progressCb)) {
                deckConfigured = true;
                asyncManager.stagingDeckReady = true;
            }
        } else {
            if (CardTextureCache::instance().loadStagedFileDeck(
                    asyncManager.stagingDeck, filePath, wear, stock, cardW, cardH, progressCb)) {
                if (capturedCornerRadius >= 0.0f && asyncManager.stagingDeck.cornerRadius < 0.0f) {
                    asyncManager.stagingDeck.cornerRadius = capturedCornerRadius;
                }
                deckConfigured = true;
                asyncManager.stagingDeckReady = true;
            } else if (!bgConfigured) {
                if (scriptPart.find("audio.") != std::string::npos ||
                    scriptPart.find("define_sound") != std::string::npos ||
                    scriptPart.find("define_sequence") != std::string::npos) {
                    if (foley.loadFromEatsScript(scriptPart)) {
                        foley.triggerSound(FoleySound::Shuffle, 0.85f);
                        return true;
                    }
                }
                if (progressCb && !progressCb(0.30f, "Synthesizing custom table material...")) {
                    return false;
                }
                if (ProceduralBackgroundSystem::instance().loadCustomEatsTheme(filePath)) {
                    bgConfigured = true;
                }
            }
        }

        asyncManager.customBgReady = bgConfigured;
        return (deckConfigured || bgConfigured) && !asyncManager.cancelRequested.load();
    }

    bool runStagedFileDrop(
        const std::string& filePath,
        CardWearLevel wear,
        CardStock stock,
        const CardTextureCache::ProgressCallback& progressCb
    ) {
        int cardW = CardTextureCache::kDefaultCardW;
        int cardH = CardTextureCache::kDefaultCardH;

        std::string ext;
        size_t dot = filePath.rfind('.');
        if (dot != std::string::npos) {
            ext = filePath.substr(dot);
            for (char& c : ext) c = static_cast<char>(std::tolower(c));
        }

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".svg") {
            bool ok = CardTextureCache::instance().loadStagedFileDeck(
                asyncManager.stagingDeck, filePath, wear, stock, cardW, cardH, progressCb
            );
            if (ok && !asyncManager.cancelRequested.load()) {
                asyncManager.stagingDeckReady = true;
                return true;
            }
            if (ext != ".svg" && ProceduralBackgroundSystem::instance().loadCustomEatsTheme(filePath)) {
                asyncManager.customBgReady = true;
                return true;
            }
            return false;
        }

        return runStagedEatsDrop(filePath, wear, stock, cardW, cardH, progressCb);
    }

    void startAsyncFileDrop(const std::string& filePath) {
        if (filePath.empty()) return;

        asyncManager.stopWorker();

        CardWearLevel wear = CardTextureCache::instance().cardWearLevel();
        CardStock stock = CardTextureCache::instance().cardStock();

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.stagingDeckReady = false;
        asyncManager.customBgReady = false;
        asyncManager.opType = AsyncOp::FileDrop;
        asyncManager.dialogTitle = "LOADING CUSTOM CONTENT";
        asyncManager.dialogInitialStatus = "Inspecting package & synthesizing assets...";
        asyncManager.dialogCancellable = true;
        asyncManager.startTime = glfwGetTime();
        asyncManager.debounceSeconds = 0.05; // Fast 50ms response for dropped files
        asyncManager.isBusy.store(true);
        {
            std::lock_guard<std::mutex> lock(asyncManager.mutex);
            asyncManager.statusText = "Inspecting dropped file package...";
        }

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        runStagedFileDrop(
            filePath, wear, stock,
            [this](float p, const std::string& msg) -> bool {
                asyncManager.progress.store(p);
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    asyncManager.statusText = msg;
                }
                return !asyncManager.cancelRequested.load();
            }
        );
        asyncManager.isBusy.store(false);
#else
        asyncManager.workerThread = std::thread([this, filePath, wear, stock]() {
            runStagedFileDrop(
                filePath, wear, stock,
                [this](float p, const std::string& msg) -> bool {
                    asyncManager.progress.store(p);
                    {
                        std::lock_guard<std::mutex> lock(asyncManager.mutex);
                        asyncManager.statusText = msg;
                    }
                    return !asyncManager.cancelRequested.load();
                }
            );
            asyncManager.isBusy.store(false);
        });
#endif
    }

    void startAsyncSolvableDeal(const SolitaireGameDef& def, uint32_t startSeed = 0, bool silent = false) {
        asyncManager.stopWorker();
        asyncManager.resetState();

        if (startSeed == 0) {
            std::random_device rd;
            auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            startSeed = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(nowVal)) & 0x7FFFFFFFu;
            if (startSeed == 0) startSeed = 2026u;
        }

        if (!def.supportsSolver) {
            if (victoryGather.active || pendingGatherDeal || (screensaverActive && board.isVictory())) {
                pendingGatherSeed = startSeed;
                pendingGatherSeedReady = true;
                return;
            }
            applyNewDealWithAnimation(startSeed, def, silent);
            return;
        }

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.solvableSeedReady = false;
        asyncManager.solvableSeed = startSeed;
        asyncManager.solvableDef = def;
        asyncManager.opType = AsyncOp::SolvableDeal;
        asyncManager.silent = silent;
        // Ultra-low-win-rate games (e.g. Accordion): pick instantly from curated verified demo_seeds (0ms wait)
        if (def.preferCuratedSolvable && !def.demoSeeds.empty()) {
            size_t idx = static_cast<size_t>(startSeed) % def.demoSeeds.size();
            if (def.demoSeeds.size() > 1 && def.demoSeeds[idx] == board.seed()) {
                idx = (idx + 1) % def.demoSeeds.size();
            }
            asyncManager.solvableSeed = def.demoSeeds[idx];
            asyncManager.solvableSeedReady = true;
            asyncManager.isBusy.store(false);
            return;
        }

        asyncManager.dialogTitle = "SEEKING SOLVABLE DEAL";
        asyncManager.dialogInitialStatus = "Searching for guaranteed winnable seed...";
        asyncManager.dialogCancellable = true;
        asyncManager.startTime = glfwGetTime();
        asyncManager.debounceSeconds = 0.04;
        asyncManager.isBusy.store(true);
        {
            std::lock_guard<std::mutex> lock(asyncManager.mutex);
            asyncManager.statusText = "Analyzing deal #" + std::to_string(startSeed) + " (1/80)...";
        }

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
        const uint32_t prevBoardSeed = board.seed();
        asyncManager.workerThread = std::thread([this, def, startSeed, prevBoardSeed]() {
            SolitaireSolver solver;
            uint32_t currentSeed = startSeed;
            const uint32_t maxAttempts = 150;
            uint32_t attempts = 0;
            bool found = false;

            while (attempts < maxAttempts && !asyncManager.cancelRequested.load()) {
                ++attempts;
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    asyncManager.statusText = "Testing deal #" + std::to_string(currentSeed) + " (" + std::to_string(attempts) + "/" + std::to_string(maxAttempts) + ")...";
                }
                asyncManager.progress.store(static_cast<float>(attempts) / static_cast<float>(maxAttempts));

                SolitaireBoard testBoard;
                testBoard.newDeal(currentSeed, def);

                SolveResult res = solver.solve(testBoard, 25000, 350.0f, &asyncManager.cancelRequested);
                if (res.solved) {
                    found = true;
                    asyncManager.solvableSeed = currentSeed;
                    asyncManager.solvableSeedReady = true;
                    break;
                }

                currentSeed = (currentSeed * 1664525u + 1013904223u) & 0x7FFFFFFFu;
                if (currentSeed == 0) currentSeed = 2026u;
            }

            if (!found && !asyncManager.cancelRequested.load()) {
                uint32_t fallbackSeed = startSeed;
                if (!def.demoSeeds.empty()) {
                    size_t idx = static_cast<size_t>(startSeed) % def.demoSeeds.size();
                    if (def.demoSeeds.size() > 1 && def.demoSeeds[idx] == prevBoardSeed) {
                        idx = (idx + 1) % def.demoSeeds.size();
                    }
                    fallbackSeed = def.demoSeeds[idx];
                } else if (def.demoSeed != 0) {
                    fallbackSeed = def.demoSeed;
                }
                asyncManager.solvableSeed = fallbackSeed;
                asyncManager.solvableSeedReady = true;
            }
            asyncManager.isBusy.store(false);
        });
#else
        uint32_t fallbackSeed = startSeed;
        if (!def.demoSeeds.empty()) {
            size_t idx = static_cast<size_t>(startSeed) % def.demoSeeds.size();
            if (def.demoSeeds.size() > 1 && def.demoSeeds[idx] == board.seed()) {
                idx = (idx + 1) % def.demoSeeds.size();
            }
            fallbackSeed = def.demoSeeds[idx];
        } else if (def.demoSeed != 0) {
            fallbackSeed = def.demoSeed;
        }
        asyncManager.solvableSeed = fallbackSeed;
        asyncManager.solvableSeedReady = true;
        asyncManager.isBusy.store(false);
#endif
    }

    void enterScreensaverMode() {
        menuOpen = false;
        playMenuOpen = false;
        toolbarConfigOpen = false;
        helpOpen = false;
        draggedPile = -1;
        draggedCardIdx = -1;
        isGesturing = false;
        pressedToolbarBtn = BtnNone;
        holdTriggered = false;
        autoCascadeActive = false;
        autoPlayController.stop();
        autoPlayController.setSpeed(AutoPlaySpeed::Normal);
        screensaverActive = true;
        if (!asyncManager.isBusy.load()) {
            startAsyncSolvableDeal(board.activeGameDef(), 0, true);
        }
        foley.triggerSound(FoleySound::Flip, 0.75f);
    }

    void applyNewDealWithAnimation(uint32_t seed, const SolitaireGameDef& def, bool silent) {
        autoCascadeActive = false;
        autoPlayController.stop();
        board.newDeal(seed, def);
        board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        board.triggerDealAnimation();
        if (!screensaverActive) {
            SettingsManager::instance().settings().lastSeed = seed;
            SettingsManager::instance().settings().variant = def.nativeVariant;
            SettingsManager::instance().save();
            applyEffectiveSettings(true);
            SessionManager::instance().clearSession();
        }
        if (loadingDialog.isOpen()) {
            loadingDialog.close();
            foley.triggerSound(FoleySound::Shuffle, 0.90f);
        } else if (!silent) {
            foley.triggerSound(FoleySound::Deal, 0.85f);
        }
        if (screensaverActive) {
            autoPlayController.setSpeed(AutoPlaySpeed::Normal);
            autoPlayController.start(board);
        }
    }

    void beginVictoryGather(bool silent = false) {
        if (victoryGather.active) return;

        float ox = 0.0f;
        float oy = 0.0f;
        board.dealOrigin(ox, oy);
        const float cw = board.cardWidth();
        const float ch = board.cardHeight();

        victoryGather.cards.clear();
        std::vector<VictoryAirborneCard> airborne;
        airborne.reserve(64);
        evaluateActiveVictoryAirborne(board, static_cast<float>(width), static_cast<float>(height), airborne);

        for (const auto& vac : airborne) {
            VictoryGatherCard gc{};
            gc.card = vac.card;
            gc.startX = vac.xf.cx;
            gc.startY = vac.xf.cy;
            gc.startZ = vac.xf.cz;
            gc.startYaw = vac.xf.yaw;
            gc.startPitch = vac.xf.pitch;
            gc.startRoll = vac.xf.roll;
            gc.startW = vac.xf.baseW;
            gc.startH = vac.xf.baseH;
            victoryGather.cards.push_back(gc);
        }

        for (const auto& p : board.piles()) {
            for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
                if (shouldHideVictoryPileCard(board, p, cIdx)) continue;
                const Card& c = p.cards[cIdx];
                VictoryGatherCard gc{};
                gc.card = c;
                gc.startX = c.x + cw * 0.5f;
                gc.startY = c.y + ch * 0.5f;
                gc.startZ = c.elevation;
                gc.startYaw = 0.0f;
                gc.startPitch = 0.0f;
                gc.startRoll = c.angle;
                gc.startW = cw;
                gc.startH = ch;
                victoryGather.cards.push_back(gc);
            }
        }

        if (victoryGather.cards.empty()) {
            return;
        }

        const float step = std::min(0.005f, 0.16f / static_cast<float>(victoryGather.cards.size()));
        for (size_t i = 0; i < victoryGather.cards.size(); ++i) {
            victoryGather.cards[i].delaySec = static_cast<float>(i) * step;
        }

        victoryGather.targetX = ox;
        victoryGather.targetY = oy;
        victoryGather.targetW = cw;
        victoryGather.targetH = ch;
        victoryGather.elapsedSec = 0.0f;
        victoryGather.durationSec = 0.58f;
        victoryGather.active = true;
        pendingGatherDeal = true;
        pendingGatherSilent = silent;
        if (!silent) {
            foley.triggerSound(FoleySound::Flip, 0.65f);
        }
    }

    void updateVictoryGather(float dt) {
        if (!victoryGather.active) return;
        victoryGather.elapsedSec += dt;
        const float maxDelay = victoryGather.cards.empty() ? 0.0f : victoryGather.cards.back().delaySec;
        if (victoryGather.elapsedSec >= (victoryGather.durationSec + maxDelay) && pendingGatherSeedReady) {
            victoryGather.active = false;
            victoryGather.cards.clear();
            pendingGatherDeal = false;
            pendingGatherSeedReady = false;
            applyNewDealWithAnimation(pendingGatherSeed, board.activeGameDef(), pendingGatherSilent);
            foley.triggerSound(FoleySound::Shuffle, 0.72f);
        }
    }

    void exitScreensaverMode(bool triggerSound = true) {
        if (!screensaverActive) return;
        screensaverActive = false;
        victoryGather.active = false;
        victoryGather.cards.clear();
        pendingGatherDeal = false;
        pendingGatherSeedReady = false;
        if (asyncManager.isBusy.load() && asyncManager.opType == AsyncOp::SolvableDeal) {
            asyncManager.cancelRequested.store(true);
        }
        autoPlayController.stop();
        if (triggerSound) {
            foley.triggerSound(FoleySound::WoodClick, 0.7f);
        }
    }

    InteractiveContext(SolitaireBoard& b, AudioFoleyEngine& f, uint32_t w, uint32_t h, bool skipSessionRestore = false)
        : board(b), foley(f), width(w), height(h) {
        SettingsManager::instance().load();
        SettingsManager::instance().loadPresets();
        // Defer texture loading and conditioning to async startup flow to keep cold boot instantaneous
        SettingsManager::instance().applyToGame(board, foley, false);
        lightController.setPreset(SettingsManager::instance().settings().lightingPreset);
        menuAnchor = SettingsManager::instance().settings().menuAnchor;
        menuFlowVertical = SettingsManager::instance().settings().menuFlowVertical;

        if (!skipSessionRestore && SessionManager::instance().hasSavedSession()) {
            SessionManager::instance().loadSession(board);
        }

        GameRegistry::instance().initialize();

        SettingsManager::instance().applyToGame(board, foley, false);
        effectiveSettings_ = SettingsManager::instance().resolveForGame(activeGameId());
        const AppSettings& startupSettings = effectiveSettings_;
        lightController.setPreset(startupSettings.lightingPreset);
        if (startupSettings.tableTint != TableTintPreset::Auto) {
            ProceduralBackgroundSystem::instance().setTableTint(startupSettings.tableTint);
        }
        if (!startupSettings.tableThemeSlug.empty()) {
            size_t idx = ProceduralBackgroundSystem::instance().findThemeIndex(startupSettings.tableThemeSlug, startupSettings.tableTheme);
            const auto* def = ProceduralBackgroundSystem::instance().getThemeDef(idx);
            if (def && !def->filePath.empty()) {
                ProceduralBackgroundSystem::instance().loadCustomEatsTheme(def->filePath);
                board.setTableThemeSlug(def->id);
            }
        }

        seedDialog.onDealSeed = [this](uint32_t s) {
            autoCascadeActive = false;
            autoPlayController.stop();
            if (!board.activeGameDef().id.empty()) {
                board.newDeal(s, board.activeGameDef());
            } else {
                board.newDeal(s, board.variant());
            }
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
            SessionManager::instance().clearSession();
            foley.triggerSound(FoleySound::Shuffle, 0.9f);
        };
        gameBrowserDialog.onGameSelected = [this](const SolitaireGameDef& def) {
            autoCascadeActive = false;
            autoPlayController.stop();
            if (SettingsManager::instance().settings().guaranteeSolvable) {
                startAsyncSolvableDeal(def);
            } else {
                uint32_t s = board.seed();
                if (SettingsManager::instance().settings().randomizeOnNewDeal) {
                    std::random_device rd;
                    auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                    s = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(nowVal)) & 0x7FFFFFFFu;
                    if (s == 0) s = 2026u;
                }
                board.newDeal(s, def);
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                SettingsManager::instance().settings().variant = def.nativeVariant;
                SettingsManager::instance().settings().lastSeed = s;
                if (!def.defaultPresetId.empty() &&
                    SettingsManager::instance().activePresetForGame(def.id).empty() &&
                    SettingsManager::instance().findPreset(def.defaultPresetId)) {
                    SettingsManager::instance().setGamePreset(def.id, def.defaultPresetId);
                }
                SettingsManager::instance().save();
                applyEffectiveSettings(true);
                SessionManager::instance().clearSession();
                foley.triggerSound(FoleySound::Deal, 0.85f);
            }
        };

        gameBrowserDialog.onSolvableSelected = [this](const SolitaireGameDef& def) {
            autoCascadeActive = false;
            autoPlayController.stop();
            startAsyncSolvableDeal(def);
        };

        settingsDialog.onOpenGameBrowser = [this]() {
            std::string curId = board.activeGameDef().id;
            if (curId.empty()) {
                const auto* def = GameRegistry::instance().findByVariant(board.variant());
                if (def) curId = def->id;
            }
            gameBrowserDialog.open(curId);
        };

        settingsDialog.onSelectFavoriteGame = [this](const std::string& gameId) {
            const auto* def = GameRegistry::instance().findById(gameId);
            if (def && gameBrowserDialog.onGameSelected) {
                gameBrowserDialog.onGameSelected(*def);
            }
        };

        settingsDialog.onSettingsChanged = [this]() {
            const auto& g = SettingsManager::instance().settings();
            bool variantChanged = (g.variant != board.variant());
            if (variantChanged) {
                board.newDeal(g.lastSeed, g.variant);
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                SessionManager::instance().clearSession();
                foley.triggerSound(FoleySound::Deal, 0.85f);
            }
            applyEffectiveSettings(true);
        };

        settingsDialog.onPreviewDeck = [this](const AppSettings& s) {
            syncDeckAndConditioning(s, true);
        };
    }

    [[nodiscard]] std::string activeGameId() const {
        if (!board.activeGameDef().id.empty()) return board.activeGameDef().id;
        if (const auto* def = GameRegistry::instance().findByVariant(board.variant())) return def->id;
        return "";
    }

    AppSettings effectiveSettings_{};

    void applyEffectiveSettings(bool applyTexturesAndThemes) {
        effectiveSettings_ = SettingsManager::instance().resolveForGame(activeGameId());
        const AppSettings& s = effectiveSettings_;
        menuAnchor = s.menuAnchor;
        menuFlowVertical = s.menuFlowVertical;
        renderer.setAntiAliasingMode(4);
        lightController.setPreset(s.lightingPreset);

        board.setCardDeviation(s.cardDeviation);
        board.setLayoutMode(s.layoutMode);
        board.setStockPosition(s.stockPosition);
        foley.setMasterVolume(s.masterVolume);
        foley.setMuted(s.muted);
        foley.setShufflePreference(s.shuffleStyle);
        foley.setSoundPackId(s.soundPackId);
        foley.setAmbienceVolume(s.ambienceVolume);
        foley.setAmbience(s.ambienceId);
        CardTextureCache::instance().setCornerStyle(s.cardCorners);

        auto& cache = CardTextureCache::instance();
        bool themeChanged = (s.tableTheme != board.tableTheme() || s.tableThemeSlug != board.tableThemeSlug());
        bool tintChanged = (s.tableTint != ProceduralBackgroundSystem::instance().tableTint());
        if (tintChanged) {
            ProceduralBackgroundSystem::instance().setTableTint(s.tableTint);
            ProceduralBackgroundSystem::instance().invalidateCache();
        }
        if (s.tableThemeOption != ProceduralBackgroundSystem::instance().themeOption()) {
            board.setTableThemeOption(s.tableThemeOption);
            ProceduralBackgroundSystem::instance().setThemeOption(s.tableThemeOption);
            ProceduralBackgroundSystem::instance().invalidateCache();
        }

        if (!applyTexturesAndThemes) return;

        auto applyThemeNow = [&]() {
            if (!themeChanged) return;
            ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
            size_t idx = ProceduralBackgroundSystem::instance().findThemeIndex(s.tableThemeSlug, s.tableTheme);
            const auto* def = ProceduralBackgroundSystem::instance().getThemeDef(idx);
            if (def && !def->filePath.empty()) {
                ProceduralBackgroundSystem::instance().loadCustomEatsTheme(def->filePath);
                board.setTableTheme(def->legacyId);
                board.setTableThemeSlug(def->id);
            } else {
                board.setTableTheme(s.tableTheme);
                board.setTableThemeSlug(s.tableThemeSlug);
            }
            ProceduralBackgroundSystem::instance().invalidateCache();
        };

        if (syncDeckAndConditioning(s, false, applyThemeNow)) {
            return;
        }
        if (themeChanged) {
            size_t idx = ProceduralBackgroundSystem::instance().findThemeIndex(s.tableThemeSlug, s.tableTheme);
            const auto* def = ProceduralBackgroundSystem::instance().getThemeDef(idx);
            if (def) {
                startAsyncTableTheme(*def);
            } else {
                startAsyncTableTheme(s.tableTheme);
            }
        }
    }

    bool syncDeckAndConditioning(const AppSettings& s, bool silent, const std::function<void()>& beforeLoad = {}) {
        auto& cache = CardTextureCache::instance();
        const bool isLowPoly = (s.deckTheme == DeckThemeId::SolPolyWhite ||
                                s.deckTheme == DeckThemeId::SolPolyDark ||
                                s.deckTheme == DeckThemeId::ScoundrelLowPoly ||
                                s.deckTheme == DeckThemeId::MemoryLowPoly ||
                                s.deckTheme == DeckThemeId::SolPolyClassic ||
                                s.deckTheme == DeckThemeId::SolPolyMobile ||
                                s.deckTheme == DeckThemeId::LowPolyWar);
        bool seedChanged = isLowPoly && (s.lowPolySeed != cache.lowPolySeed());
        if (seedChanged) {
            cache.setLowPolySeed(s.lowPolySeed);
        }
        bool deckChanged = seedChanged || ((!s.deckSlug.empty())
            ? (s.deckSlug != cache.currentDeckId())
            : (s.deckTheme != cache.currentDeckTheme()));
        bool wearChanged = (s.cardWear != cache.cardWearLevel());
        bool stockChanged = (s.cardStock != cache.cardStock());
        bool cardBackChanged = (s.cardBackId != cache.currentCardBackId());

        if (deckChanged) {
            if (beforeLoad) beforeLoad();
            const auto& decks = cache.availableDecks();
            size_t dIdx = cache.findDeckIndex(s.deckSlug, s.deckTheme);
            if (dIdx < decks.size()) {
                startAsyncDeckDef(decks[dIdx], s.cardWear, s.cardStock, silent);
            } else {
                startAsyncDeckTheme(s.deckTheme, s.cardWear, s.cardStock, silent);
            }
            return true;
        }
        if (wearChanged || stockChanged) {
            if (beforeLoad) beforeLoad();
            startAsyncConditioning(s.cardWear, s.cardStock, silent);
            return true;
        }
        if (cardBackChanged) {
            cache.setActiveCardBack(s.cardBackId);
        }
        return false;
    }

    void checkStartupSettingsLoad() {
        effectiveSettings_ = SettingsManager::instance().resolveForGame(activeGameId());
        const AppSettings& s = effectiveSettings_;
        auto& cache = CardTextureCache::instance();

        const bool isLowPoly = (s.deckTheme == DeckThemeId::SolPolyWhite ||
                                s.deckTheme == DeckThemeId::SolPolyDark ||
                                s.deckTheme == DeckThemeId::ScoundrelLowPoly ||
                                s.deckTheme == DeckThemeId::MemoryLowPoly ||
                                s.deckTheme == DeckThemeId::SolPolyClassic ||
                                s.deckTheme == DeckThemeId::SolPolyMobile ||
                                s.deckTheme == DeckThemeId::LowPolyWar);
        bool seedChanged = isLowPoly && (s.lowPolySeed != cache.lowPolySeed());
        if (seedChanged) {
            cache.setLowPolySeed(s.lowPolySeed);
        }
        bool deckChanged = seedChanged || ((!s.deckSlug.empty())
            ? (s.deckSlug != cache.currentDeckId())
            : (s.deckTheme != cache.currentDeckTheme()));
        bool wearChanged = (s.cardWear != cache.cardWearLevel());
        bool stockChanged = (s.cardStock != cache.cardStock());
        bool cardBackChanged = (s.cardBackId != cache.currentCardBackId());

        if (deckChanged) {
            const auto& decks = cache.availableDecks();
            size_t dIdx = cache.findDeckIndex(s.deckSlug, s.deckTheme);
            if (dIdx < decks.size()) {
                startAsyncDeckDef(decks[dIdx], s.cardWear, s.cardStock);
            } else {
                startAsyncDeckTheme(s.deckTheme, s.cardWear, s.cardStock);
            }
        } else if (wearChanged || stockChanged) {
            startAsyncConditioning(s.cardWear, s.cardStock);
        } else if (cardBackChanged) {
            cache.setActiveCardBack(s.cardBackId);
        }
    }

    void toggleFullscreen() noexcept {
        setFullscreen(!isFullscreen);
    }

    void setFullscreen(bool enable) noexcept {
        if (!window) return;
        if (isFullscreen == enable) return;
        isFullscreen = enable;

#if defined(__EMSCRIPTEN__)
        if (enable) {
            EM_ASM({
                var el = document.getElementById('canvas-container') || document.documentElement;
                if (el.requestFullscreen) {
                    el.requestFullscreen();
                } else if (el.webkitRequestFullscreen) {
                    el.webkitRequestFullscreen();
                }
            });
        } else {
            EM_ASM({
                if (document.exitFullscreen) {
                    document.exitFullscreen();
                } else if (document.webkitExitFullscreen) {
                    document.webkitExitFullscreen();
                }
            });
        }
#elif defined(_WIN32)
        HWND hwnd = glfwGetWin32Window(window);
        if (hwnd) {
            if (enable) {
                LONG style = GetWindowLong(hwnd, GWL_STYLE);
                if (!hasSavedWindowState) {
                    savedWindowStyle = style;
                    RECT rect{};
                    if (GetWindowRect(hwnd, &rect)) {
                        savedWindowX = rect.left;
                        savedWindowY = rect.top;
                        savedWindowW = rect.right - rect.left;
                        savedWindowH = rect.bottom - rect.top;
                    }
                    hasSavedWindowState = true;
                }

                int posX = 0;
                int posY = 0;
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);

                HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                if (hMonitor) {
                    MONITORINFO mi{};
                    mi.cbSize = sizeof(mi);
                    if (GetMonitorInfoW(hMonitor, &mi)) {
                        posX = mi.rcMonitor.left;
                        posY = mi.rcMonitor.top;
                        screenWidth = mi.rcMonitor.right - mi.rcMonitor.left;
                        screenHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
                    }
                }

                LONG newStyle = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE;
                SetWindowLong(hwnd, GWL_STYLE, newStyle);
                SetWindowPos(hwnd, HWND_TOP, posX, posY, screenWidth, screenHeight,
                             SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            } else {
                LONG restoreStyle = (savedWindowStyle != 0) ? savedWindowStyle : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                SetWindowLong(hwnd, GWL_STYLE, restoreStyle);
                SetWindowPos(hwnd, HWND_TOP, savedWindowX, savedWindowY, savedWindowW, savedWindowH,
                             SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
                ShowWindow(hwnd, SW_RESTORE);
            }

            int winW = 0, winH = 0;
            glfwGetWindowSize(window, &winW, &winH);
            if (winW > 0 && winH > 0) {
                width = static_cast<uint32_t>(winW);
                height = static_cast<uint32_t>(winH);
                renderer.resize(width, height);
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
            }
        }
#else
        if (enable) {
            glfwGetWindowPos(window, &savedWindowX, &savedWindowY);
            glfwGetWindowSize(window, &savedWindowW, &savedWindowH);
            hasSavedWindowState = true;
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            if (monitor) {
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                if (mode) {
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                }
            }
        } else {
            glfwSetWindowMonitor(window, nullptr, savedWindowX, savedWindowY, savedWindowW, savedWindowH, 0);
        }
        int winW = 0, winH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        if (winW > 0 && winH > 0) {
            width = static_cast<uint32_t>(winW);
            height = static_cast<uint32_t>(winH);
            renderer.resize(width, height);
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        }
#endif
        foley.triggerSound(FoleySound::WoodClick, 0.65f);
    }

    void triggerAction(int btnId) {
        if (btnId != BtnHint) {
            hintHighlight.active = false;
        }
        switch (btnId) {
            case BtnNewDeal:
                autoPlayController.stop();
                autoCascadeActive = false;
                if (board.isVictory() && !victoryGather.active) {
                    beginVictoryGather(false);
                    if (SettingsManager::instance().settings().guaranteeSolvable) {
                        pendingGatherSeedReady = false;
                        startAsyncSolvableDeal(board.activeGameDef(), 0, true);
                    } else {
                        uint32_t nextSeed = board.seed() + 1u;
                        if (SettingsManager::instance().settings().randomizeOnNewDeal) {
                            std::random_device rd;
                            auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                            nextSeed = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(nowVal)) & 0x7FFFFFFFu;
                            if (nextSeed == 0) nextSeed = 2026u;
                        }
                        pendingGatherSeed = nextSeed;
                        pendingGatherSeedReady = true;
                    }
                    break;
                }
                if (SettingsManager::instance().settings().guaranteeSolvable) {
                    startAsyncSolvableDeal(board.activeGameDef());
                    break;
                }
                {
                    uint32_t nextSeed = board.seed() + 1u;
                    if (SettingsManager::instance().settings().randomizeOnNewDeal) {
                        std::random_device rd;
                        auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                        nextSeed = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(nowVal)) & 0x7FFFFFFFu;
                        if (nextSeed == 0) nextSeed = 2026u;
                    }
                    applyNewDealWithAnimation(nextSeed, board.activeGameDef(), false);
                }
                break;
            case BtnSolvableDeal:
                autoPlayController.stop();
                autoCascadeActive = false;
                startAsyncSolvableDeal(board.activeGameDef());
                break;
            case BtnRestart:
                autoPlayController.stop();
                autoCascadeActive = false;
                board.restartDeal();
                SessionManager::instance().clearSession();
                foley.triggerSound(FoleySound::Deal, 0.85f);
                break;
            case BtnVariantPrev: {
                autoPlayController.stop();
                autoCascadeActive = false;
                const auto& favs = SettingsManager::instance().settings().favorites;
                if (!favs.empty()) {
                    std::string curId = board.activeGameDef().id;
                    if (curId.empty()) {
                        const auto* def = GameRegistry::instance().findByVariant(board.variant());
                        if (def) curId = def->id;
                    }
                    int curIdx = -1;
                    for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
                        if (favs[i] == curId) { curIdx = i; break; }
                    }
                    int prevIdx = (curIdx <= 0) ? (static_cast<int>(favs.size()) - 1) : (curIdx - 1);
                    const auto* targetDef = GameRegistry::instance().findById(favs[prevIdx]);
                    if (targetDef) {
                        board.newDeal(board.seed(), *targetDef);
                        board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                        SettingsManager::instance().settings().variant = targetDef->nativeVariant;
                        applyEffectiveSettings(true);
                        SessionManager::instance().clearSession();
                        foley.triggerSound(FoleySound::Deal, 0.85f);
                    }
                }
                break;
            }
            case BtnVariantNext: {
                autoCascadeActive = false;
                const auto& favs = SettingsManager::instance().settings().favorites;
                if (!favs.empty()) {
                    std::string curId = board.activeGameDef().id;
                    if (curId.empty()) {
                        const auto* def = GameRegistry::instance().findByVariant(board.variant());
                        if (def) curId = def->id;
                    }
                    int curIdx = -1;
                    for (int i = 0; i < static_cast<int>(favs.size()); ++i) {
                        if (favs[i] == curId) { curIdx = i; break; }
                    }
                    int nextIdx = (curIdx < 0 || curIdx + 1 >= static_cast<int>(favs.size())) ? 0 : (curIdx + 1);
                    const auto* targetDef = GameRegistry::instance().findById(favs[nextIdx]);
                    if (targetDef) {
                        board.newDeal(board.seed(), *targetDef);
                        board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                        SettingsManager::instance().settings().variant = targetDef->nativeVariant;
                        applyEffectiveSettings(true);
                        SessionManager::instance().clearSession();
                        foley.triggerSound(FoleySound::Deal, 0.85f);
                    }
                }
                break;
            }
            case BtnGameTitlePill:
            case BtnVariant: {
                std::string curId = board.activeGameDef().id;
                if (curId.empty()) {
                    const auto* def = GameRegistry::instance().findByVariant(board.variant());
                    if (def) curId = def->id;
                }
                gameBrowserDialog.open(curId);
                foley.triggerSound(FoleySound::WoodClick, 0.8f);
                break;
            }
            case BtnRollSeed: {
                autoCascadeActive = false;
                std::random_device rd;
                auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                uint32_t nextSeed = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(nowVal)) & 0x7FFFFFFFu;
                if (nextSeed == 0) nextSeed = 2026u;
                if (!board.activeGameDef().id.empty()) {
                    board.newDeal(nextSeed, board.activeGameDef());
                } else {
                    board.newDeal(nextSeed, board.variant());
                }
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                SettingsManager::instance().settings().lastSeed = nextSeed;
                SettingsManager::instance().save();
                SessionManager::instance().clearSession();
                foley.triggerSound(FoleySound::Deal, 0.9f);
                break;
            }
            case BtnTheme: {
                const auto& themes = ProceduralBackgroundSystem::instance().availableThemes();
                if (!themes.empty()) {
                    const auto& s = SettingsManager::instance().settings();
                    size_t curIdx = ProceduralBackgroundSystem::instance().findThemeIndex(s.tableThemeSlug, s.tableTheme);
                    size_t nextIdx = (curIdx + 1u) % themes.size();
                    startAsyncTableTheme(themes[nextIdx]);
                }
                foley.triggerSound(FoleySound::Flip, 0.8f);
                break;
            }
            case BtnDeck: {
                auto& cache = CardTextureCache::instance();
                const auto& decks = cache.availableDecks();
                if (!decks.empty()) {
                    size_t curIdx = cache.findDeckIndex(cache.currentDeckId(), cache.currentDeckTheme());
                    size_t nextIdx = (curIdx + 1u) % decks.size();
                    startAsyncDeckDef(decks[nextIdx], cache.cardWearLevel(), cache.cardStock());
                }
                foley.triggerSound(FoleySound::Flip, 0.7f);
                break;
            }
            case BtnUndo:
                autoCascadeActive = false;
                if (board.undo()) {
                    foley.triggerSound(FoleySound::Deal, 0.75f);
                }
                break;
            case BtnRedo:
                autoCascadeActive = false;
                if (board.redo()) {
                    foley.triggerSound(FoleySound::Deal, 0.75f);
                }
                break;
            case BtnHint: {
                HintMove hm = board.findBestHint();
                if (hm.valid) {
                    const auto& piles = board.piles();
                    if (hm.fromPile < piles.size() && hm.toPile < piles.size()) {
                        const auto& sp = piles[hm.fromPile];
                        const auto& dp = piles[hm.toPile];
                        float sx = sp.x + board.cardWidth() * 0.5f;
                        float sy = sp.y + board.cardHeight() * 0.5f;
                        if (hm.cardIndex < sp.cards.size()) {
                            sx = sp.cards[hm.cardIndex].x + board.cardWidth() * 0.5f;
                            sy = sp.cards[hm.cardIndex].y + board.cardHeight() * 0.5f;
                        }
                        float dx = dp.x + board.cardWidth() * 0.5f;
                        float dy = dp.y + board.cardHeight() * 0.5f;
                        if (!dp.cards.empty()) {
                            dx = dp.cards.back().x + board.cardWidth() * 0.5f;
                            dy = dp.cards.back().y + board.cardHeight() * 0.5f;
                        }
                        ThemePalette hpal = getPalette(board.tableTheme());
                        hintHighlight.active = true;
                        hintHighlight.fromPile = hm.fromPile;
                        hintHighlight.cardIndex = hm.cardIndex;
                        hintHighlight.toPile = hm.toPile;
                        hintHighlight.age = 0.0f;
                        hintHighlight.duration = 1.45f;
                        hintMoveCount = board.moveCount();
                        magicParticles.spawnArcTrail(sx, sy, dx, dy, hpal.accentR, hpal.accentG, hpal.accentB,
                                                     board.cardWidth(), 0.12f, true);
                    }
                } else {
                    foley.triggerSound(FoleySound::Flip, 0.5f);
                }
                break;
            }
            case BtnAutoPlay:
                if (autoPlayController.isActive()) {
                    autoPlayController.stop();
                } else {
                    autoPlayController.start(board);
                }
                foley.triggerSound(FoleySound::Deal, 0.8f);
                break;
            case BtnPlayRestart:
                autoPlayController.restart(board, &foley);
                break;
            case BtnStepBack:
                autoPlayController.stepBackward(board, &foley);
                break;
            case BtnPlayPause:
                autoPlayController.togglePlayPause();
                break;
            case BtnStepFwd:
                autoPlayController.stepForward(board, &foley);
                break;
            case BtnSpeedToggle:
                autoPlayController.cycleSpeed();
                foley.triggerSound(FoleySound::WoodClick, 0.7f);
                break;
            case BtnTakeOver:
                autoPlayController.stop();
                foley.triggerSound(FoleySound::Deal, 0.75f);
                break;
            case BtnMenuToggle:
                SettingsManager::instance().syncFromGame(board, foley);
                settingsDialog.open(activeGameId(), board.activeGameDef().name);
                foley.triggerSound(FoleySound::Flip, 0.8f);
                break;
            case BtnSeedDialog:
                seedDialog.open(board.seed(), SolitaireBoard::variantName(board.variant()));
                foley.triggerSound(FoleySound::Flip, 0.8f);
                break;
            case BtnMenuAnchor: {
                int nextAnchor = (static_cast<int>(menuAnchor) + 1) % 4;
                menuAnchor = static_cast<MenuAnchor>(nextAnchor);
                foley.triggerSound(FoleySound::Flip, 0.65f);
                break;
            }
            case BtnCardWear: {
                int nextWear = (static_cast<int>(CardTextureCache::instance().cardWearLevel()) + 1) % static_cast<int>(CardWearLevel::Count);
                startAsyncConditioning(static_cast<CardWearLevel>(nextWear), CardTextureCache::instance().cardStock());
                foley.triggerSound(FoleySound::Flip, 0.7f);
                break;
            }
            case BtnCardStock: {
                int nextStock = (static_cast<int>(CardTextureCache::instance().cardStock()) + 1) % static_cast<int>(CardStock::Count);
                startAsyncConditioning(CardTextureCache::instance().cardWearLevel(), static_cast<CardStock>(nextStock));
                foley.triggerSound(FoleySound::Flip, 0.7f);
                break;
            }
            case BtnCardCorners: {
                int nextStyle = (static_cast<int>(CardTextureCache::instance().cornerStyle()) + 1) % static_cast<int>(CardCornerStyle::Count);
                CardTextureCache::instance().setCornerStyle(static_cast<CardCornerStyle>(nextStyle));
                foley.triggerSound(FoleySound::Flip, 0.75f);
                break;
            }
            case BtnDeviation: {
                float curDev = board.cardDeviation();
                float nextDev = 0.0f;
                if (curDev < 0.10f) nextDev = 0.25f;
                else if (curDev < 0.35f) nextDev = 0.50f;
                else if (curDev < 0.65f) nextDev = 0.75f;
                else if (curDev < 0.90f) nextDev = 1.0f;
                else nextDev = 0.0f;

                board.setCardDeviation(nextDev);
                SettingsManager::instance().settings().cardDeviation = nextDev;
                SettingsManager::instance().save();
                foley.triggerSound(FoleySound::Flip, 0.75f);
                break;
            }
            case BtnSettings:
                SettingsManager::instance().syncFromGame(board, foley);
                settingsDialog.open(activeGameId(), board.activeGameDef().name);
                foley.triggerSound(FoleySound::Flip, 0.8f);
                break;
            case BtnHelp:
                helpOpen = !helpOpen;
                if (!helpOpen) {
                    highlightedHelpZone = -1;
                    helpScrollY = 0.0f;
                    helpPointerDown = false;
                    helpDraggingScroll = false;
                    helpDraggingScrollbar = false;
                }
                foley.triggerSound(FoleySound::Flip, 0.75f);
                break;
            default:
                break;
        }
    }

    void updateAndRender(float fixedDt = 0.0f) {
        glfwPollEvents();
        double now = 0.0;
        float dt = 0.0f;
        if (fixedDt > 0.0f) {
            static double sFixedClock = 0.0;
            sFixedClock += static_cast<double>(fixedDt);
            now = sFixedClock;
            dt = fixedDt;
        } else {
            now = glfwGetTime();
            dt = static_cast<float>(now - lastTime);
            lastTime = now;
        }

        auto screenPan = [&](float x) noexcept -> float {
            return (width > 0) ? std::clamp(((x / static_cast<float>(width)) * 2.0f - 1.0f) * 0.45f, -0.45f, 0.45f) : 0.0f;
        };

        if (asyncManager.opType != AsyncOp::None || asyncManager.isBusy.load() || loadingDialog.isOpen()) {
            double elapsed = now - asyncManager.startTime;
            if (asyncManager.isBusy.load() && !asyncManager.silent && elapsed >= asyncManager.debounceSeconds && !loadingDialog.isOpen()) {
                loadingDialog.open(
                    asyncManager.dialogTitle,
                    asyncManager.dialogInitialStatus,
                    asyncManager.dialogCancellable
                );
            }

            if (loadingDialog.isOpen()) {
                loadingDialog.setProgress(asyncManager.progress.load());
                {
                    std::lock_guard<std::mutex> lock(asyncManager.mutex);
                    loadingDialog.setStatus(asyncManager.statusText);
                }

                if (loadingDialog.isCancelRequested() && !asyncManager.cancelRequested.load()) {
                    asyncManager.cancelRequested.store(true);
                    loadingDialog.setStatus("Cancelling operation...");
                }
            }

            if (!asyncManager.isBusy.load()) {
                asyncManager.joinWorker();

                if (asyncManager.cancelRequested.load()) {
                    if (loadingDialog.isOpen()) {
                        loadingDialog.close();
                    }
                    foley.triggerSound(FoleySound::WoodClick, 0.6f);
                    if (asyncManager.opType != AsyncOp::SolvableDeal) {
                        // Cancelled! Revert settings to active baseline so user isn't stuck on next boot
                        SettingsManager::instance().settings().deckTheme = CardTextureCache::instance().currentDeckTheme();
                        SettingsManager::instance().settings().deckSlug = CardTextureCache::instance().currentDeckId();
                        SettingsManager::instance().settings().cardWear = CardTextureCache::instance().cardWearLevel();
                        SettingsManager::instance().settings().cardStock = CardTextureCache::instance().cardStock();
                        SettingsManager::instance().save();
                    }
                } else if (asyncManager.opType == AsyncOp::SolvableDeal && asyncManager.solvableSeedReady) {
                    if (victoryGather.active || pendingGatherDeal || (screensaverActive && board.isVictory())) {
                        pendingGatherSeed = asyncManager.solvableSeed;
                        pendingGatherSeedReady = true;
                    } else {
                        applyNewDealWithAnimation(asyncManager.solvableSeed, asyncManager.solvableDef, asyncManager.silent);
                    }
                } else if (asyncManager.opType == AsyncOp::TableTheme && asyncManager.themeReady) {
                    board.setTableTheme(asyncManager.targetTheme);
                    board.setTableThemeSlug(asyncManager.targetThemeSlug);
                    lightController.applyThemeDefault(asyncManager.targetTheme);
                    if (!SettingsManager::instance().hasGameProfile(activeGameId())) {
                        SettingsManager::instance().settings().tableTheme = asyncManager.targetTheme;
                        SettingsManager::instance().settings().tableThemeSlug = asyncManager.targetThemeSlug;
                        SettingsManager::instance().save();
                    }
                    if (loadingDialog.isOpen()) {
                        loadingDialog.close();
                        foley.triggerSound(FoleySound::Shuffle, 0.85f);
                    } else {
                        foley.triggerSound(FoleySound::Flip, 0.80f);
                    }
                } else if (asyncManager.stagingDeckReady || asyncManager.customBgReady || asyncManager.customAudioReady || asyncManager.pendingEatsReady) {
                    if (asyncManager.pendingEatsReady) {
                        const auto& res = asyncManager.pendingEatsResult;
                        if (!res.presetIds.empty()) {
                            if (const auto* preset = SettingsManager::instance().findPreset(res.presetIds.front())) {
                                SettingsManager::applyProfileToSettings(preset->profile, SettingsManager::instance().settings());
                                SettingsManager::instance().applyToGame(board, foley, false);
                            }
                        }
                        if (!res.themeIds.empty()) {
                            SettingsManager::instance().settings().tableThemeSlug = res.themeIds.front();
                            ProceduralBackgroundSystem::instance().invalidateCache();
                        }
                        if (!res.postFxIds.empty()) {
                            SettingsManager::instance().settings().postFxId = res.postFxIds.front();
                        }
                        if (!res.ambienceIds.empty()) {
                            SettingsManager::instance().settings().ambienceId = res.ambienceIds.front();
                            foley.setAmbience(res.ambienceIds.front());
                        }
                        if (!res.soundPackIds.empty()) {
                            SettingsManager::instance().settings().soundPackId = res.soundPackIds.front();
                            foley.setSoundPackId(res.soundPackIds.front());
                        }
                        if (!res.gameIds.empty()) {
                            if (const auto* gdef = GameRegistry::instance().findById(res.gameIds.front())) {
                                autoPlayController.stop();
                                uint32_t seed = gdef->demoSeed > 0 ? gdef->demoSeed : board.seed();
                                board.newDeal(seed, *gdef);
                                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                            }
                        }
                        SettingsManager::instance().save();
                    }
                    if (asyncManager.stagingDeckReady) {
                        CardTextureCache::instance().commitStagedDeck(std::move(asyncManager.stagingDeck));
                        CardTextureCache::instance().setActiveCardBack(effectiveSettings_.cardBackId);
                        if (renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
                            CardTextureCache::instance().buildGpuAtlas(renderer);
                        }
                        if (!settingsDialog.isOpen() && !SettingsManager::instance().hasGameProfile(activeGameId())) {
                            SettingsManager::instance().settings().deckTheme = CardTextureCache::instance().currentDeckTheme();
                            SettingsManager::instance().settings().deckSlug = CardTextureCache::instance().currentDeckId();
                            SettingsManager::instance().settings().cardWear = CardTextureCache::instance().cardWearLevel();
                            SettingsManager::instance().settings().cardStock = CardTextureCache::instance().cardStock();
                            SettingsManager::instance().save();
                        }
                    }
                    if (asyncManager.customBgReady) {
                        ProceduralBackgroundSystem::instance().invalidateCache();
                    }
                    if (loadingDialog.isOpen()) {
                        loadingDialog.close();
                    }
                    foley.triggerSound(FoleySound::Shuffle, 0.85f);
                } else {
                    if (loadingDialog.isOpen()) {
                        loadingDialog.close();
                    }
                    // Load failed; keep active baseline in settings
                    if (!SettingsManager::instance().hasGameProfile(activeGameId())) {
                        SettingsManager::instance().settings().deckTheme = CardTextureCache::instance().currentDeckTheme();
                        SettingsManager::instance().settings().cardWear = CardTextureCache::instance().cardWearLevel();
                        SettingsManager::instance().settings().cardStock = CardTextureCache::instance().cardStock();
                        SettingsManager::instance().save();
                    }
                }

                asyncManager.resetState();
            }
        }

#if defined(__EMSCRIPTEN__)
        bool needsBoardLayout = false;
        if (s_webSafeAreaPending) {
            s_webSafeAreaPending = false;
            if (std::abs(board.safeTop() - s_webPendingSafeTop) > 0.5f ||
                std::abs(board.safeRight() - s_webPendingSafeRight) > 0.5f ||
                std::abs(board.safeBottom() - s_webPendingSafeBottom) > 0.5f ||
                std::abs(board.safeLeft() - s_webPendingSafeLeft) > 0.5f) {
                board.setSafeAreaInsets(s_webPendingSafeTop, s_webPendingSafeRight,
                                        s_webPendingSafeBottom, s_webPendingSafeLeft);
                needsBoardLayout = true;
            }
        }
        if (s_webResizePending) {
            s_webResizePending = false;
            int targetW = std::max(320, static_cast<int>(s_webPendingCssW));
            int targetH = std::max(240, static_cast<int>(s_webPendingCssH));
            float dpr = std::clamp(s_webPendingDpr, 1.0f, 2.0f);

            int targetFbW = static_cast<int>(std::round(static_cast<float>(targetW) * dpr));
            int targetFbH = static_cast<int>(std::round(static_cast<float>(targetH) * dpr));

            if (static_cast<uint32_t>(targetW) != width ||
                static_cast<uint32_t>(targetH) != height ||
                static_cast<uint32_t>(targetFbW) != fbWidth ||
                static_cast<uint32_t>(targetFbH) != fbHeight ||
                std::abs(currentDpr - dpr) > 0.001f) {

                width = static_cast<uint32_t>(targetW);
                height = static_cast<uint32_t>(targetH);
                fbWidth = static_cast<uint32_t>(targetFbW);
                fbHeight = static_cast<uint32_t>(targetFbH);
                currentDpr = dpr;

                EM_ASM({
                    var canvas = document.getElementById('canvas');
                    if (canvas) {
                        canvas.width = $0;
                        canvas.height = $1;
                        canvas.style.width = '100%';
                        canvas.style.height = '100%';
                    }
                    if (typeof GLFW !== 'undefined' && GLFW.active) {
                        GLFW.active.width = $2;
                        GLFW.active.height = $3;
                        GLFW.active.framebufferWidth = $0;
                        GLFW.active.framebufferHeight = $1;
                    }
                }, fbWidth, fbHeight, width, height);

                renderer.setRenderScale(currentDpr, currentDpr);
                renderer.resize(fbWidth, fbHeight);
                needsBoardLayout = true;
            }
        }
        if (needsBoardLayout) {
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        }
#else
        if (fixedDt <= 0.0f) {
            int winW = 0, winH = 0;
            glfwGetWindowSize(window, &winW, &winH);
            if (winW > 0 && winH > 0 && (static_cast<uint32_t>(winW) != width || static_cast<uint32_t>(winH) != height)) {
                width = static_cast<uint32_t>(winW);
                height = static_cast<uint32_t>(winH);
                renderer.resize(width, height);
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
            }
        }
#endif

        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        const float rawFmx = static_cast<float>(mx);
        const float rawFmy = static_cast<float>(my);
        float fmx = rawFmx;
        float fmy = rawFmy;
        const auto& activeMouseCfg = settingsDialog.isOpen()
            ? settingsDialog.previewSettings()
            : effectiveSettings_;
        if (!(settingsDialog.isOpen() && settingsDialog.isPeekingPostFx())) {
            PostFxSystem::instance().warpMouseCoords(
                activeMouseCfg.postFxId, activeMouseCfg.postFxOption, activeMouseCfg.postFxIntensity,
                static_cast<float>(width), static_cast<float>(height), fmx, fmy);
        }
        const float uiFmx = activeMouseCfg.postFxAffectUi ? fmx : rawFmx;
        const float uiFmy = activeMouseCfg.postFxAffectUi ? fmy : rawFmy;
        lastWarpedFmx = fmx;
        lastWarpedFmy = fmy;

        int hoveredBtn = screensaverActive ? BtnNone : hitTestHudButtons(fmx, fmy, static_cast<float>(width), static_cast<float>(height), board, menuAnchor, menuFlowVertical);
        if (!screensaverActive && autoPlayController.isActive()) {
            int tBtn = hitTestTransportButtons(fmx, fmy, static_cast<float>(width), static_cast<float>(height), autoPlayController, board.safeBottom());
            if (tBtn != BtnNone) {
                hoveredBtn = tBtn;
            }
        }

        float boardFmx = fmx;
        float boardFmy = fmy;
        if (showcaseCam.zoom > 1.002f) {
            boardFmx = showcaseCam.focusX + (fmx - static_cast<float>(width) * 0.5f) / showcaseCam.zoom;
            boardFmy = showcaseCam.focusY + (fmy - static_cast<float>(height) * 0.5f) / showcaseCam.zoom;
        }

        const bool touchPinchBlocked = pinchActive || (now < pinchSuppressUntil);
        const bool leftDown = !touchPinchBlocked && (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
        const bool rightDown = !touchPinchBlocked && (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
        const bool middleDown = !touchPinchBlocked && (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);

        if (touchPinchBlocked) {
            draggedPile = -1;
            draggedCardIdx = -1;
            dragOffsetX = 0.0f;
            dragOffsetY = 0.0f;
            isGesturing = false;
            feltPanning = false;
        }

        if (screensaverActive) {
            if ((leftDown && !prevLeftDown) || (rightDown && !prevRightDown)) {
                exitScreensaverMode(true);
            }
        } else if (loadingDialog.isOpen() || asyncManager.isBusy.load()) {
            if (loadingDialog.isOpen()) {
                if (leftDown && !prevLeftDown) {
                    loadingDialog.handleMouseButton(0, 1, uiFmx, uiFmy);
                }
            }
        } else if (seedDialog.isOpen()) {
            if (leftDown && !prevLeftDown) {
                seedDialog.handleMouseButton(0, 1, uiFmx, uiFmy, board, foley, window);
            } else if (leftDown) {
                seedDialog.handleMouseMove(uiFmx, uiFmy);
            } else if (!leftDown && prevLeftDown) {
                seedDialog.handleMouseButton(0, 0, uiFmx, uiFmy, board, foley, window);
            }
        } else if (gameBrowserDialog.isOpen()) {
            if (leftDown && !prevLeftDown) {
                gameBrowserDialog.handleMouseButton(0, 1, uiFmx, uiFmy, board, foley);
            } else if (leftDown) {
                gameBrowserDialog.handleMouseMove(uiFmx, uiFmy);
            } else if (!leftDown && prevLeftDown) {
                gameBrowserDialog.handleMouseButton(0, 0, uiFmx, uiFmy, board, foley);
            }
        } else if (settingsDialog.isOpen()) {
            if (leftDown && !prevLeftDown) {
                settingsDialog.handleMouseButton(0, 1, uiFmx, uiFmy, board, foley);
            } else if (leftDown) {
                settingsDialog.handleMouseMove(uiFmx, uiFmy, board, foley);
            } else if (!leftDown && prevLeftDown) {
                settingsDialog.handleMouseButton(0, 0, uiFmx, uiFmy, board, foley);
            }
        } else if (playMenuOpen) {
            if (leftDown && !prevLeftDown) {
                constexpr float menuW = 230.0f;
                constexpr float itemH = 30.0f;
                constexpr float headerH = 26.0f;
                constexpr float pad = 8.0f;
                constexpr int kCount = 6;
                constexpr float menuH = headerH + static_cast<float>(kCount) * itemH + pad * 2.0f;
                float menuX = playMenuAnchorX;
                float menuY = (playMenuAnchorY > static_cast<float>(height) * 0.5f) ? (playMenuAnchorY - menuH - 8.0f) : (playMenuAnchorY + 44.0f);
                menuX = std::clamp(menuX, 8.0f, static_cast<float>(width) - menuW - 8.0f);
                menuY = std::clamp(menuY, 8.0f, static_cast<float>(height) - menuH - 8.0f);

                if (uiFmx >= menuX && uiFmx <= menuX + menuW && uiFmy >= menuY && uiFmy <= menuY + menuH) {
                    float curY = menuY + headerH + pad * 0.5f;
                    for (int i = 0; i < kCount; ++i) {
                        if (uiFmx >= menuX + pad && uiFmx <= menuX + menuW - pad && uiFmy >= curY && uiFmy <= curY + itemH) {
                            if (i == 0) { // New Random Deal
                                triggerAction(BtnNewDeal);
                            } else if (i == 1) { // New Solvable Deal
                                triggerAction(BtnSolvableDeal);
                            } else if (i == 2) { // Restart Current Deal
                                triggerAction(BtnRestart);
                            } else if (i == 3) { // Solvable Deals Mode Toggle
                                auto& s = SettingsManager::instance().settings();
                                s.guaranteeSolvable = !s.guaranteeSolvable;
                                SettingsManager::instance().save();
                                foley.triggerSound(FoleySound::WoodClick, 0.85f);
                            } else if (i == 4) { // Screensaver Mode
                                playMenuOpen = false;
                                enterScreensaverMode();
                                break;
                            } else if (i == 5) { // Game Library...
                                triggerAction(BtnVariant);
                            }
                            break;
                        }
                        curY += itemH;
                    }
                }
                playMenuOpen = false;
            }
        } else if (toolbarConfigOpen) {
            if (leftDown && !prevLeftDown) {
                constexpr float menuW = 240.0f;
                constexpr float headerH = 26.0f;
                constexpr float scaleRowH = 34.0f;
                constexpr float itemH = 26.0f;
                constexpr float pad = 8.0f;
                constexpr int kNumToggles = 9;
                constexpr float footerH = 32.0f;
                constexpr float menuH = headerH + scaleRowH + 14.0f + static_cast<float>(kNumToggles) * itemH + footerH + pad * 2.0f;
                float menuX = toolbarConfigAnchorX;
                float menuY = (toolbarConfigAnchorY > static_cast<float>(height) * 0.5f) ? (toolbarConfigAnchorY - menuH - 8.0f) : (toolbarConfigAnchorY + 44.0f);
                menuX = std::clamp(menuX, 8.0f, static_cast<float>(width) - menuW - 8.0f);
                menuY = std::clamp(menuY, 8.0f, static_cast<float>(height) - menuH - 8.0f);

                if (uiFmx >= menuX && uiFmx <= menuX + menuW && uiFmy >= menuY && uiFmy <= menuY + menuH) {
                    float scaleY = menuY + headerH + 6.0f;
                    const float pillW = (menuW - pad * 2.0f - 8.0f) / 3.0f;
                    const float pillH = 22.0f;
                    for (int i = 0; i < 3; ++i) {
                        float px = menuX + pad + static_cast<float>(i) * (pillW + 4.0f);
                        if (uiFmx >= px && uiFmx <= px + pillW && uiFmy >= scaleY && uiFmy <= scaleY + pillH) {
                            auto& s = SettingsManager::instance().settings();
                            s.toolbarScale = static_cast<ToolbarScale>(i);
                            SettingsManager::instance().save();
                            foley.triggerSound(FoleySound::WoodClick, 0.85f);
                        }
                    }

                    float divY = scaleY + pillH + 8.0f;
                    const uint32_t toggleFlags[kNumToggles] = {
                        ToolbarItem_Play, ToolbarItem_Undo, ToolbarItem_Redo, ToolbarItem_AutoPlay,
                        ToolbarItem_Hint, ToolbarItem_Restart, ToolbarItem_Solvable, ToolbarItem_Seed,
                        ToolbarItem_GameSelect
                    };
                    float curY = divY + 18.0f;
                    for (int i = 0; i < kNumToggles; ++i) {
                        if (uiFmx >= menuX + pad && uiFmx <= menuX + menuW - pad && uiFmy >= curY && uiFmy <= curY + itemH) {
                            auto& s = SettingsManager::instance().settings();
                            s.toolbarItems ^= toggleFlags[i];
                            SettingsManager::instance().save();
                            foley.triggerSound(FoleySound::WoodClick, 0.85f);
                        }
                        curY += itemH;
                    }

                    float closeBtnY = curY + 4.0f;
                    float closeBtnW = menuW - pad * 2.0f;
                    float closeBtnH = 22.0f;
                    if (uiFmx >= menuX + pad && uiFmx <= menuX + pad + closeBtnW && uiFmy >= closeBtnY && uiFmy <= closeBtnY + closeBtnH) {
                        toolbarConfigOpen = false;
                        foley.triggerSound(FoleySound::WoodClick, 0.85f);
                    }
                } else {
                    toolbarConfigOpen = false;
                }
            }
        } else if (helpOpen && !leftDown) {
            if (std::hypot(fmx - prevHelpMouseX, fmy - prevHelpMouseY) > 0.5f) {
                prevHelpMouseX = fmx;
                prevHelpMouseY = fmy;
                HelpOddsInfo oddsSnap;
                { std::lock_guard<std::mutex> lk(oddsMutex); oddsSnap = helpOdds; }
                const HelpSidebarLayout hl = buildHelpSidebarLayout(board, static_cast<float>(width), static_cast<float>(height), helpScrollY, &oddsSnap, hasHelpVideoThumb());
                int hoveredZ = -1;
                if (fmx >= hl.sb.x && fmx <= hl.sb.x + hl.sb.w && fmy >= hl.viewY && fmy <= hl.viewY + hl.viewH) {
                    for (const auto& zh : hl.zoneHits) {
                        if (fmx >= zh.x && fmx <= zh.x + zh.w && fmy >= zh.y && fmy <= zh.y + zh.h) {
                            hoveredZ = static_cast<int>(zh.zoneIndex);
                            break;
                        }
                    }
                }
                if (hoveredZ != highlightedHelpZone) {
                    highlightedHelpZone = hoveredZ;
                    if (hoveredZ >= 0 && static_cast<size_t>(hoveredZ) < board.piles().size()) {
                        float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
                        computeZoneHighlightBounds(board, hoveredZ, zx, zy, zw, zh);
                        ThemePalette hpal = getPalette(board.tableTheme());
                        magicParticles.spawnZoneBurst(zx, zy, zw, zh, hpal.accentR, hpal.accentG, hpal.accentB);
                    }
                }
            }
        }
        const bool modalOpen = loadingDialog.isOpen() || asyncManager.isBusy.load() ||
                               seedDialog.isOpen() || gameBrowserDialog.isOpen() ||
                               settingsDialog.isOpen();
        if (modalOpen) {
            hoveredBtn = -1;
        }
        if (!modalOpen && leftDown && !prevLeftDown && !playMenuOpen && !toolbarConfigOpen) {
            if (helpOpen && [&]() {
                HelpOddsInfo oddsSnap;
                { std::lock_guard<std::mutex> lk(oddsMutex); oddsSnap = helpOdds; }
                const HelpSidebarLayout hl = buildHelpSidebarLayout(board, static_cast<float>(width), static_cast<float>(height), helpScrollY, &oddsSnap, hasHelpVideoThumb());
                const HelpSidebarRect& sb = hl.sb;
                if (fmx < sb.x || fmx > sb.x + sb.w || fmy < sb.y || fmy > sb.y + sb.h) return false;
                if (fmx >= sb.closeX && fmx <= sb.closeX + sb.closeW && fmy >= sb.closeY && fmy <= sb.closeY + sb.closeH) {
                    helpOpen = false;
                    highlightedHelpZone = -1;
                    helpScrollY = 0.0f;
                    helpPointerDown = false;
                    helpDraggingScroll = false;
                    helpDraggingScrollbar = false;
                    foley.triggerSound(FoleySound::WoodClick, 0.75f);
                    return true;
                }
                if (fmx >= sb.showMeX && fmx <= sb.showMeX + sb.showMeW && fmy >= sb.showMeY && fmy <= sb.showMeY + sb.showMeH) {
                    autoPlayController.startDemo(board);
                    foley.triggerSound(FoleySound::Flip, 0.85f);
                    return true;
                }
                if (fmx >= sb.fontDecX && fmx <= sb.fontDecX + sb.fontDecW && fmy >= sb.fontDecY && fmy <= sb.fontDecY + sb.fontDecH) {
                    auto& s = SettingsManager::instance().settings();
                    s.helpFontScale = std::clamp(s.helpFontScale - 0.05f, 0.85f, 1.35f);
                    SettingsManager::instance().save();
                    foley.triggerSound(FoleySound::WoodClick, 0.75f);
                    return true;
                }
                if (fmx >= sb.fontIncX && fmx <= sb.fontIncX + sb.fontIncW && fmy >= sb.fontIncY && fmy <= sb.fontIncY + sb.fontIncH) {
                    auto& s = SettingsManager::instance().settings();
                    s.helpFontScale = std::clamp(s.helpFontScale + 0.05f, 0.85f, 1.35f);
                    SettingsManager::instance().save();
                    foley.triggerSound(FoleySound::WoodClick, 0.75f);
                    return true;
                }
                if (fmy >= hl.viewY && fmy <= hl.viewY + hl.viewH) {
                    helpMaxScrollY = std::max(0.0f, hl.contentH - hl.viewH);
                    if (helpMaxScrollY > 1.0f && fmx >= sb.x + sb.w - 22.0f) {
                        helpDraggingScrollbar = true;
                        const float trackY = hl.viewY + 4.0f;
                        const float trackH = std::max(24.0f, hl.viewH - 8.0f);
                        const float frac = std::clamp((fmy - trackY) / trackH, 0.0f, 1.0f);
                        helpScrollY = frac * helpMaxScrollY;
                        return true;
                    }
                    helpPointerDown = true;
                    helpDraggingScroll = false;
                    helpDragStartX = fmx;
                    helpDragStartY = fmy;
                    helpInitialScrollY = std::clamp(helpScrollY, 0.0f, helpMaxScrollY);
                    return true;
                }
                return true;
            }()) {
                // Click handled by Help Sidebar
            } else if (hoveredBtn == BtnMenuToggle) {
                isClickingSolEngineOrb = true;
                isDraggingMenu = false;
                menuDragStartMouseX = fmx;
                menuDragStartMouseY = fmy;
                dragPreviewAnchor = menuAnchor;
                dragPreviewFlowVertical = menuFlowVertical;
            } else if (hoveredBtn != BtnNone) {
                pressedToolbarBtn = hoveredBtn;
                pressStartTime = static_cast<float>(now);
                pressStartX = fmx;
                pressStartY = fmy;
                holdTriggered = false;
            } else if (!screensaverActive && board.isVictory()) {
                if (victoryGather.active && pendingGatherSeedReady) {
                    victoryGather.active = false;
                    victoryGather.cards.clear();
                    pendingGatherDeal = false;
                    pendingGatherSeedReady = false;
                    applyNewDealWithAnimation(pendingGatherSeed, board.activeGameDef(), false);
                } else if (!victoryGather.active) {
                    triggerAction(BtnNewDeal);
                }
            } else {
                int hitPile = -1;
                int hitCard = -1;
                if (board.hitTestCard(boardFmx, boardFmy, hitPile, hitCard)) {
                    if (autoPlayController.isActive()) {
                        autoPlayController.stop();
                    }
                    if (board.piles()[static_cast<size_t>(hitPile)].type == PileType::Stock) {
                        if (board.drawFromStock()) {
                            foley.triggerSound(FoleySound::Deal, 0.85f, screenPan(fmx));
                        }
                    } else if (hitCard >= 0) {
                        if (board.hasScriptCardClick() && board.hasScriptCanPick() &&
                            !board.canPickScriptCard(static_cast<uint8_t>(hitPile), static_cast<size_t>(hitCard))) {
                            if (board.smartMove(static_cast<uint8_t>(hitPile), static_cast<size_t>(hitCard))) {
                                foley.triggerSound(FoleySound::Snap, 0.75f, screenPan(fmx));
                            }
                        } else {
                            draggedPile = hitPile;
                            draggedCardIdx = hitCard;
                        dragStartMouseX = boardFmx;
                        dragStartMouseY = boardFmy;
                        lastDragMouseX = boardFmx;
                        lastDragMouseY = boardFmy;
                        dragSoundAccumulator = 0.0f;
                        dragOffsetX = 0.0f;
                        dragOffsetY = 0.0f;
                        prevDragOffsetX = 0.0f;
                        prevDragOffsetY = 0.0f;
                        dragPivotAngle = 0.0f;
                        dragPivotVel = 0.0f;
                        dragChainAngles.fill(0.0f);
                        dragChainVels.fill(0.0f);
                        dragChainCount = 0;
                        const auto& hitP = board.piles()[static_cast<size_t>(hitPile)];
                        if (hitCard >= 0 && static_cast<size_t>(hitCard) < hitP.cards.size()) {
                            dragGrabLocalX = boardFmx - hitP.cards[static_cast<size_t>(hitCard)].x;
                            dragGrabLocalY = boardFmy - hitP.cards[static_cast<size_t>(hitCard)].y;
                        } else {
                            dragGrabLocalX = board.cardWidth() * 0.5f;
                            dragGrabLocalY = board.cardHeight() * 0.5f;
                        }
                        foley.triggerSound(FoleySound::Snap, 0.75f, screenPan(fmx));
                        }
                    }
                } else {
                    if (autoPlayController.isActive()) {
                        autoPlayController.stop();
                    }
                    board.clearSelection();
                    // Clicked on empty table felt -> Start Gesture or Zoomed Felt Pan!
                    isGesturing = true;
                    gestureStartX = fmx;
                    gestureStartY = fmy;
                    gestureCurX = fmx;
                    gestureCurY = fmy;
                    feltPanning = (showcaseCam.userZoom > 1.002f);
                    feltPanLastX = fmx;
                    feltPanLastY = fmy;
                }
            }
        } else if (leftDown && isClickingSolEngineOrb) {
            const float dragDist = std::hypot(fmx - menuDragStartMouseX, fmy - menuDragStartMouseY);
            if (dragDist >= 6.0f) {
                isDraggingMenu = true;
            }
            if (isDraggingMenu) {
                const float halfW = static_cast<float>(width) * 0.5f;
                const float halfH = static_cast<float>(height) * 0.5f;
                const bool isLeft = (fmx < halfW);
                const bool isTop = (fmy < halfH);

                if (isLeft && isTop) dragPreviewAnchor = MenuAnchor::TopLeft;
                else if (!isLeft && isTop) dragPreviewAnchor = MenuAnchor::TopRight;
                else if (isLeft && !isTop) dragPreviewAnchor = MenuAnchor::BottomLeft;
                else dragPreviewAnchor = MenuAnchor::BottomRight;

                // Determine flow direction based on user's cursor position relative to the target corner:
                if (dragPreviewAnchor == MenuAnchor::BottomLeft) {
                    float distFromBottom = static_cast<float>(height) - fmy;
                    float distFromLeft = fmx;
                    dragPreviewFlowVertical = (distFromBottom > distFromLeft * 0.85f);
                } else if (dragPreviewAnchor == MenuAnchor::BottomRight) {
                    float distFromBottom = static_cast<float>(height) - fmy;
                    float distFromRight = static_cast<float>(width) - fmx;
                    dragPreviewFlowVertical = (distFromBottom > distFromRight * 0.85f);
                } else if (dragPreviewAnchor == MenuAnchor::TopLeft) {
                    float distFromTop = fmy;
                    float distFromLeft = fmx;
                    dragPreviewFlowVertical = (distFromTop > distFromLeft * 0.85f);
                } else { // TopRight
                    float distFromTop = fmy;
                    float distFromRight = static_cast<float>(width) - fmx;
                    dragPreviewFlowVertical = (distFromTop > distFromRight * 0.85f);
                }
            }
        } else if (leftDown && pressedToolbarBtn != BtnNone && !holdTriggered) {
            float dist = std::hypot(fmx - pressStartX, fmy - pressStartY);
            if (dist > 10.0f) {
                pressedToolbarBtn = BtnNone;
            } else if (static_cast<float>(now) - pressStartTime >= 0.35f) {
                if (pressedToolbarBtn == BtnNewDeal) {
                    holdTriggered = true;
                    playMenuOpen = true;
                    toolbarConfigOpen = false;
                    playMenuAnchorX = pressStartX;
                    playMenuAnchorY = pressStartY;
                    foley.triggerSound(FoleySound::WoodClick, 0.85f);
                } else if (pressedToolbarBtn == BtnToolbarResize) {
                    holdTriggered = true;
                    toolbarConfigOpen = true;
                    playMenuOpen = false;
                    toolbarConfigAnchorX = pressStartX;
                    toolbarConfigAnchorY = pressStartY;
                    foley.triggerSound(FoleySound::WoodClick, 0.85f);
                }
            }
        } else if (leftDown && draggedPile >= 0) {
            float dx = static_cast<float>(boardFmx - lastDragMouseX);
            float dy = static_cast<float>(boardFmy - lastDragMouseY);
            lastDragMouseX = boardFmx;
            lastDragMouseY = boardFmy;
            dragOffsetX = static_cast<float>(boardFmx - dragStartMouseX);
            dragOffsetY = static_cast<float>(boardFmy - dragStartMouseY);
            dragSoundAccumulator += std::hypot(dx, dy);
            if (dragSoundAccumulator > 72.0f) {
                float speed = std::clamp(std::hypot(dx, dy) * 0.08f + 0.65f, 0.6f, 1.4f);
                foley.triggerSound(FoleySound::Drag, 0.32f * speed, screenPan(fmx));
                dragSoundAccumulator = 0.0f;
            }
        } else if (leftDown && helpOpen && (helpPointerDown || helpDraggingScrollbar)) {
            HelpOddsInfo oddsSnap;
            { std::lock_guard<std::mutex> lk(oddsMutex); oddsSnap = helpOdds; }
            const HelpSidebarLayout hl = buildHelpSidebarLayout(board, static_cast<float>(width), static_cast<float>(height), helpScrollY, &oddsSnap, hasHelpVideoThumb());
            helpMaxScrollY = std::max(0.0f, hl.contentH - hl.viewH);
            if (helpDraggingScrollbar && helpMaxScrollY > 1.0f) {
                const float trackY = hl.viewY + 4.0f;
                const float trackH = std::max(24.0f, hl.viewH - 8.0f);
                const float frac = std::clamp((fmy - trackY) / trackH, 0.0f, 1.0f);
                helpScrollY = frac * helpMaxScrollY;
            } else if (helpPointerDown && helpMaxScrollY > 1.0f) {
                if (!helpDraggingScroll && std::abs(fmy - helpDragStartY) > 5.0f) {
                    helpDraggingScroll = true;
                }
                if (helpDraggingScroll) {
                    helpScrollY = std::clamp(helpInitialScrollY - (fmy - helpDragStartY), 0.0f, helpMaxScrollY);
                }
            }
        } else if (leftDown && isGesturing) {
            gestureCurX = fmx;
            gestureCurY = fmy;
            if (feltPanning) {
                panUserCameraBy(fmx - feltPanLastX, fmy - feltPanLastY);
                feltPanLastX = fmx;
                feltPanLastY = fmy;
                showcaseCam.userPanActive = true;
            }
        } else if (!modalOpen && !leftDown && prevLeftDown) {
            if (helpPointerDown || helpDraggingScrollbar) {
                if (helpOpen && !helpDraggingScroll && !helpDraggingScrollbar) {
                    HelpOddsInfo oddsSnap;
                    { std::lock_guard<std::mutex> lk(oddsMutex); oddsSnap = helpOdds; }
                    const bool hasThumb = hasHelpVideoThumb();
                    const HelpSidebarLayout hl = buildHelpSidebarLayout(board, static_cast<float>(width), static_cast<float>(height), helpScrollY, &oddsSnap, hasThumb);
                    if (fmy >= hl.viewY && fmy <= hl.viewY + hl.viewH) {
                        if (hasThumb &&
                            fmx >= hl.videoX && fmx <= hl.videoX + hl.videoW &&
                            fmy >= hl.videoY && fmy <= hl.videoY + hl.videoH) {
                            openYouTubeVideo(board.docManifest().videoId);
                            foley.triggerSound(FoleySound::WoodClick, 0.85f);
                        } else if (fmx >= hl.analyzeX && fmx <= hl.analyzeX + hl.analyzeW && fmy >= hl.analyzeY && fmy <= hl.analyzeY + hl.analyzeH) {
                            if (oddsSnap.running) cancelOddsAnalysis();
                            else startOddsAnalysis();
                            foley.triggerSound(FoleySound::WoodClick, 0.85f);
                        } else if (fmx >= hl.hintX && fmx <= hl.hintX + hl.hintW && fmy >= hl.hintY && fmy <= hl.hintY + hl.hintH) {
                            triggerAction(BtnHint);
                        } else if (fmx >= hl.solvableX && fmx <= hl.solvableX + hl.solvableW && fmy >= hl.solvableY && fmy <= hl.solvableY + hl.solvableH) {
                            triggerAction(BtnSolvableDeal);
                        } else if (fmx >= hl.gamesX && fmx <= hl.gamesX + hl.gamesW && fmy >= hl.gamesY && fmy <= hl.gamesY + hl.gamesH) {
                            triggerAction(BtnVariant);
                        } else {
                            for (const auto& zh : hl.zoneHits) {
                                if (fmx >= zh.x && fmx <= zh.x + zh.w && fmy >= zh.y && fmy <= zh.y + zh.h) {
                                    int zIdx = static_cast<int>(zh.zoneIndex);
                                    highlightedHelpZone = zIdx;
                                    if (zIdx >= 0 && static_cast<size_t>(zIdx) < board.piles().size()) {
                                        float zx = 0.0f, zy = 0.0f, zw = 0.0f, zb = 0.0f;
                                        computeZoneHighlightBounds(board, zIdx, zx, zy, zw, zb);
                                        ThemePalette hpal = getPalette(board.tableTheme());
                                        magicParticles.spawnZoneBurst(zx, zy, zw, zb, hpal.accentR, hpal.accentG, hpal.accentB);
                                    }
                                    foley.triggerSound(FoleySound::WoodClick, 0.75f);
                                    break;
                                }
                            }
                        }
                    }
                }
                helpPointerDown = false;
                helpDraggingScroll = false;
                helpDraggingScrollbar = false;
            } else if (pressedToolbarBtn != BtnNone) {
                if (!holdTriggered && hoveredBtn == pressedToolbarBtn) {
                    if (pressedToolbarBtn == BtnToolbarResize) {
                        auto& s = SettingsManager::instance().settings();
                        int nextScale = (static_cast<int>(s.toolbarScale) + 1) % 3;
                        s.toolbarScale = static_cast<ToolbarScale>(nextScale);
                        SettingsManager::instance().save();
                        foley.triggerSound(FoleySound::WoodClick, 0.9f);
                    } else {
                        triggerAction(pressedToolbarBtn);
                    }
                }
                pressedToolbarBtn = BtnNone;
                holdTriggered = false;
            } else if (isClickingSolEngineOrb) {
                if (isDraggingMenu) {
                    menuAnchor = dragPreviewAnchor;
                    menuFlowVertical = dragPreviewFlowVertical;
                    auto& s = SettingsManager::instance().settings();
                    s.menuAnchor = menuAnchor;
                    s.menuFlowVertical = menuFlowVertical;
                    SettingsManager::instance().save();
                    foley.triggerSound(FoleySound::WoodClick, 0.9f);
                } else {
                    triggerAction(BtnSettings);
                }
                isClickingSolEngineOrb = false;
                isDraggingMenu = false;
            } else if (draggedPile >= 0) {
                const float dragDist = std::hypot(dragOffsetX, dragOffsetY);
                bool moved = false;

                if (draggedPile < static_cast<int>(board.piles().size()) &&
                    draggedCardIdx >= 0 &&
                    static_cast<size_t>(draggedCardIdx) < board.piles()[static_cast<size_t>(draggedPile)].cards.size()) {

                    if (dragDist <= 6.0f) {
                        moved = board.smartMove(static_cast<uint8_t>(draggedPile), static_cast<size_t>(draggedCardIdx));
                    } else {
                        auto& srcPile = board.piles()[static_cast<size_t>(draggedPile)];
                        const Card& lead = srcPile.cards[static_cast<size_t>(draggedCardIdx)];
                        const float dropCx = lead.x + dragOffsetX + board.cardWidth() * 0.5f;
                        const float dropCy = lead.y + dragOffsetY + board.cardHeight() * 0.5f;
                        int bestTarget = board.findBestDropPile(static_cast<uint8_t>(draggedPile),
                                                                static_cast<size_t>(draggedCardIdx),
                                                                dropCx, dropCy);

                        for (size_t cIdx = static_cast<size_t>(draggedCardIdx); cIdx < srcPile.cards.size(); ++cIdx) {
                            srcPile.cards[cIdx].x += dragOffsetX;
                            srcPile.cards[cIdx].y += dragOffsetY;
                            srcPile.cards[cIdx].elevation = 14.0f;
                        }

                        if (bestTarget >= 0) {
                            moved = board.executeMove(static_cast<uint8_t>(draggedPile),
                                                      static_cast<size_t>(draggedCardIdx),
                                                      static_cast<uint8_t>(bestTarget));
                        }
                    }
                }

                float dropPan = screenPan(static_cast<float>(fmx));
                if (moved) {
                    foley.triggerSound(FoleySound::Place, 0.85f, dropPan);
                } else if (dragDist > 6.0f) {
                    foley.triggerSound(FoleySound::Place, 0.65f, dropPan);
                }
                draggedPile = -1;
                draggedCardIdx = -1;
                dragOffsetX = 0.0f;
                dragOffsetY = 0.0f;
            } else if (isGesturing) {
                isGesturing = false;
                const bool wasFeltPan = feltPanning;
                feltPanning = false;
                if (!middleDown && !pinchActive) {
                    showcaseCam.userPanActive = false;
                }
                const float gdx = gestureCurX - gestureStartX;
                const float gdy = gestureCurY - gestureStartY;
                const float gdist = std::hypot(gdx, gdy);

                if (gdist <= 10.0f) {
                    if (now - lastFeltTapTime <= 0.32 &&
                        std::hypot(gestureCurX - lastFeltTapX, gestureCurY - lastFeltTapY) <= 28.0f) {
                        if (showcaseCam.userZoom > 1.002f) {
                            resetUserCamera();
                        }
                        lastFeltTapTime = -10.0;
                    } else {
                        lastFeltTapTime = now;
                        lastFeltTapX = gestureCurX;
                        lastFeltTapY = gestureCurY;
                    }
                } else if (!wasFeltPan && gdist > 36.0f) {
                    if (std::abs(gdx) > std::abs(gdy) * 1.25f) {
                        if (gdx < 0.0f) {
                            // Swipe Left on Felt -> UNDO
                            triggerAction(BtnUndo);
                        } else {
                            // Swipe Right on Felt -> REDO
                            triggerAction(BtnRedo);
                        }
                    } else if (gdy < -30.0f && std::abs(gdy) > std::abs(gdx) * 1.25f) {
                        // Swipe Up on Felt -> AUTO-PLAY
                        triggerAction(BtnAutoPlay);
                    } else if (gdy > 30.0f && std::abs(gdy) > std::abs(gdx) * 1.25f) {
                        // Swipe Down on Felt -> NETHER SIGHT
                        triggerAction(BtnHint);
                    }
                }
            }
        }

        if (!modalOpen && rightDown && !prevRightDown) {
            if (hoveredBtn == BtnNewDeal) {
                playMenuOpen = true;
                toolbarConfigOpen = false;
                playMenuAnchorX = fmx;
                playMenuAnchorY = fmy;
                foley.triggerSound(FoleySound::WoodClick, 0.85f);
            } else if (hoveredBtn == BtnToolbarResize) {
                toolbarConfigOpen = true;
                playMenuOpen = false;
                toolbarConfigAnchorX = fmx;
                toolbarConfigAnchorY = fmy;
                foley.triggerSound(FoleySound::WoodClick, 0.85f);
            } else if (hoveredBtn < 0) {
                int hitPile = -1;
                int hitCard = -1;
                if (board.hitTestCard(boardFmx, boardFmy, hitPile, hitCard) && hitPile >= 0 && hitCard >= 0) {
                    if (board.smartMove(static_cast<uint8_t>(hitPile), static_cast<size_t>(hitCard))) {
                        foley.triggerSound(FoleySound::Place, 0.85f, screenPan(fmx));
                    }
                } else {
                    playMenuOpen = false;
                    toolbarConfigOpen = false;
                    settingsDialog.open();
                    foley.triggerSound(FoleySound::WoodClick, 0.75f);
                }
            }
        }

        if (!modalOpen && !screensaverActive && !playMenuOpen && !toolbarConfigOpen && !autoPlayController.isActive()) {
            if (middleDown && !prevMiddleDown) {
                midPanLastX = fmx;
                midPanLastY = fmy;
                if (now - lastMiddleClickTime <= 0.32) {
                    resetUserCamera();
                    lastMiddleClickTime = -10.0;
                } else {
                    lastMiddleClickTime = now;
                }
            } else if (middleDown) {
                if (showcaseCam.userZoom > 1.002f) {
                    panUserCameraBy(fmx - midPanLastX, fmy - midPanLastY);
                    showcaseCam.userPanActive = true;
                }
                midPanLastX = fmx;
                midPanLastY = fmy;
            } else if (!middleDown && prevMiddleDown) {
                if (!feltPanning && !pinchActive) {
                    showcaseCam.userPanActive = false;
                }
            }
        } else if (!middleDown && prevMiddleDown && !feltPanning && !pinchActive) {
            showcaseCam.userPanActive = false;
        }

        prevLeftDown = leftDown;
        prevRightDown = rightDown;
        prevMiddleDown = middleDown;

        auto edgeKey = [&](int key, bool& prev) -> bool {
            bool down = glfwGetKey(window, key) == GLFW_PRESS;
            bool trig = down && !prev;
            prev = down;
            return trig;
        };

        if (loadingDialog.isOpen() || asyncManager.isBusy.load()) {
            if (loadingDialog.isOpen()) {
                if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) loadingDialog.handleKey(GLFW_KEY_ESCAPE, 1, 0);
            }
        } else if (seedDialog.isOpen()) {
            int mods = 0;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) mods |= 2;
            if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) seedDialog.handleKey(GLFW_KEY_ESCAPE, 1, mods, board, foley, window);
            if (edgeKey(GLFW_KEY_ENTER, prevKeyEnter)) seedDialog.handleKey(GLFW_KEY_ENTER, 1, mods, board, foley, window);
            if (edgeKey(GLFW_KEY_BACKSPACE, prevKeyBack)) seedDialog.handleKey(GLFW_KEY_BACKSPACE, 1, mods, board, foley, window);
            if (edgeKey(GLFW_KEY_C, prevKeyC)) seedDialog.handleKey(GLFW_KEY_C, 1, mods, board, foley, window);
            if (edgeKey(GLFW_KEY_V, prevKeyV)) seedDialog.handleKey(GLFW_KEY_V, 1, mods, board, foley, window);
        } else if (gameBrowserDialog.isOpen()) {
            if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) gameBrowserDialog.handleKey(GLFW_KEY_ESCAPE, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_ENTER, prevKeyEnter)) gameBrowserDialog.handleKey(GLFW_KEY_ENTER, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_BACKSPACE, prevKeyBack)) gameBrowserDialog.handleKey(GLFW_KEY_BACKSPACE, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_UP, prevKeyUp)) gameBrowserDialog.handleKey(GLFW_KEY_UP, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_DOWN, prevKeyDown)) gameBrowserDialog.handleKey(GLFW_KEY_DOWN, 1, 0, board, foley);
        } else if (settingsDialog.isOpen()) {
            if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) settingsDialog.handleKey(GLFW_KEY_ESCAPE, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_ENTER, prevKeyEnter)) settingsDialog.handleKey(GLFW_KEY_ENTER, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_BACKSPACE, prevKeyBack)) settingsDialog.handleKey(GLFW_KEY_BACKSPACE, 1, 0, board, foley);
            if (edgeKey(GLFW_KEY_TAB, prevKeyTab)) settingsDialog.handleKey(GLFW_KEY_TAB, 1, 0, board, foley);
        } else if (screensaverActive) {
            if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) {
                exitScreensaverMode(true);
            }
            if (edgeKey(GLFW_KEY_A, prevKeyA)) {
                if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                    exitScreensaverMode(true);
                }
            }
            const bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
                               (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
            const bool keyEnter = (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) ||
                                  (glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS);
            const bool keyF11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
            const bool isFullscreenTrigger = (keyF11 && !prevKeyF11) || (isAlt && keyEnter && !prevKeyEnter);
            prevKeyF11 = keyF11;
            prevKeyEnter = keyEnter;
            if (isFullscreenTrigger) {
                toggleFullscreen();
            }
        } else {
            if (edgeKey(GLFW_KEY_N, prevKeyN)) {
                if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                    triggerAction(BtnSolvableDeal);
                } else {
                    triggerAction(BtnNewDeal);
                }
            }
            if (edgeKey(GLFW_KEY_R, prevKeyR)) triggerAction(BtnRestart);
            if (edgeKey(GLFW_KEY_Z, prevKeyZ)) triggerAction(BtnUndo);
            if (edgeKey(GLFW_KEY_Y, prevKeyY)) triggerAction(BtnRedo);
            if (edgeKey(GLFW_KEY_H, prevKeyH)) triggerAction(BtnHint);
            if (edgeKey(GLFW_KEY_A, prevKeyA)) {
                if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                    enterScreensaverMode();
                } else {
                    triggerAction(BtnAutoPlay);
                }
            }
            if (edgeKey(GLFW_KEY_T, prevKeyT)) triggerAction(BtnTheme);
            if (edgeKey(GLFW_KEY_D, prevKeyD)) triggerAction(BtnDeck);
            if (edgeKey(GLFW_KEY_W, prevKeyW)) triggerAction(BtnCardWear);
            if (edgeKey(GLFW_KEY_S, prevKeyS)) triggerAction(BtnCardStock);
            if (edgeKey(GLFW_KEY_C, prevKeyC)) triggerAction(BtnCardCorners);
            if (edgeKey(GLFW_KEY_M, prevKeyM)) triggerAction(BtnMenuToggle);
            if (edgeKey(GLFW_KEY_J, prevKeyJ)) triggerAction(BtnDeviation);
            if (edgeKey(GLFW_KEY_O, prevKeyO)) triggerAction(BtnSettings);
            if (edgeKey(GLFW_KEY_G, prevKeyG)) triggerAction(BtnVariant);
            if (edgeKey(GLFW_KEY_L, prevKeyL)) {
                lightController.cyclePreset();
                SettingsManager::instance().settings().lightingPreset = lightController.getPreset();
                SettingsManager::instance().save();
                foley.triggerSound(FoleySound::WoodClick, 0.65f);
                std::cout << "[SolEngine] Dynamic lighting switched to: " << lightController.getPresetName() << std::endl;
            }
            if (edgeKey(GLFW_KEY_F1, prevKeyF1)) {
                helpOpen = !helpOpen;
                if (!helpOpen) {
                    highlightedHelpZone = -1;
                    helpPointerDown = false;
                    helpDraggingScroll = false;
                    helpDraggingScrollbar = false;
                }
                foley.triggerSound(FoleySound::Flip, 0.75f);
            }
            if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) {
                if (helpOpen) {
                    helpOpen = false;
                    highlightedHelpZone = -1;
                    helpPointerDown = false;
                    helpDraggingScroll = false;
                    helpDraggingScrollbar = false;
                } else if (playMenuOpen) {
                    playMenuOpen = false;
                } else if (toolbarConfigOpen) {
                    toolbarConfigOpen = false;
                } else {
                    if (menuOpen) menuOpen = false;
                    if (autoPlayController.isActive()) autoPlayController.stop();
                }
            }
            if (edgeKey(GLFW_KEY_SPACE, prevKeySpace)) {
                if (autoPlayController.isActive()) {
                    autoPlayController.togglePlayPause();
                } else {
                    autoPlayController.start(board);
                }
            }
            if (edgeKey(GLFW_KEY_P, prevKeyP)) {
                if (autoPlayController.isActive()) {
                    autoPlayController.togglePlayPause();
                } else {
                    autoPlayController.start(board);
                }
            }
            if (edgeKey(GLFW_KEY_LEFT_BRACKET, prevKeyLBracket)) {
                autoPlayController.stepBackward(board, &foley);
            }
            if (edgeKey(GLFW_KEY_RIGHT_BRACKET, prevKeyRBracket)) {
                autoPlayController.stepForward(board, &foley);
            }
            if (edgeKey(GLFW_KEY_F, prevKeyF)) {
                autoPlayController.cycleSpeed();
            }

            // F11 or Alt+Enter: Fullscreen Toggle (matching Eatsbits standard)
            const bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
                               (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
            const bool keyEnter = (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) ||
                                  (glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS);
            const bool keyF11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;

            const bool isFullscreenTrigger = (keyF11 && !prevKeyF11) || (isAlt && keyEnter && !prevKeyEnter);
            prevKeyF11 = keyF11;
            prevKeyEnter = keyEnter;

            if (isFullscreenTrigger) {
                toggleFullscreen();
            }
            auto switchVariant = [&](GameVariant v) {
                autoCascadeActive = false;
                board.newDeal(board.seed(), v);
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                SettingsManager::instance().settings().variant = v;
                SessionManager::instance().clearSession();
                foley.triggerSound(FoleySound::Deal, 0.85f);
            };
            if (edgeKey(GLFW_KEY_1, prevKey1)) switchVariant(GameVariant::KlondikeDraw1);
            if (edgeKey(GLFW_KEY_2, prevKey2)) switchVariant(GameVariant::KlondikeDraw3);
            if (edgeKey(GLFW_KEY_3, prevKey3)) {
                if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
                    triggerAction(BtnSeedDialog);
                } else {
                    switchVariant(GameVariant::FreeCell);
                }
            }
            if (edgeKey(GLFW_KEY_4, prevKey4)) switchVariant(GameVariant::Spider1Suit);
            if (edgeKey(GLFW_KEY_5, prevKey5)) switchVariant(GameVariant::Spider2Suits);
            if (edgeKey(GLFW_KEY_6, prevKey6)) switchVariant(GameVariant::Spider4Suits);
            if (edgeKey(GLFW_KEY_7, prevKey7)) switchVariant(GameVariant::Golf);
        }

        autoPlayController.update(dt, board, &foley);
        updateVictoryGather(dt);
        if (screensaverActive) {
            if (victoryGather.active) {
                // Wait for gather-to-origin animation to finish
            } else if (board.isVictory()) {
                if (!pendingGatherSeedReady && asyncManager.opType == AsyncOp::None && !asyncManager.isBusy.load()) {
                    pendingGatherDeal = true;
                    startAsyncSolvableDeal(board.activeGameDef(), 0, true);
                }
                if (board.victorySeconds() >= 6.0f && pendingGatherSeedReady) {
                    beginVictoryGather(true);
                }
            } else if (asyncManager.opType == AsyncOp::None && !asyncManager.isBusy.load()) {
                const AutoPlayState apState = autoPlayController.state();
                if (apState == AutoPlayState::Completed ||
                    apState == AutoPlayState::Failed || apState == AutoPlayState::Inactive) {
                    autoPlayController.stop();
                    startAsyncSolvableDeal(board.activeGameDef(), 0, true);
                }
            }
        }
        board.updateAnimations(dt);
        {
            std::array<float, 32> dealPans{};
            const uint8_t dealSoundCount = board.consumePendingDealPans(dealPans.data(), static_cast<uint8_t>(dealPans.size()));
            for (uint8_t i = 0; i < dealSoundCount; ++i) {
                foley.triggerSound(FoleySound::Deal, 0.82f, dealPans[i]);
            }
        }
        const AppSettings& activeCfg = settingsDialog.isOpen()
            ? settingsDialog.previewSettings()
            : effectiveSettings_;
        if (lightController.getPreset() != activeCfg.lightingPreset) {
            lightController.setPreset(activeCfg.lightingPreset);
        }
        if (lightController.getAngle() != activeCfg.lightAngle) {
            lightController.setAngle(activeCfg.lightAngle);
        }
        if (std::abs(lightController.getWaver() - activeCfg.lightWaver) > 0.001f) {
            lightController.setWaver(activeCfg.lightWaver);
        }
        if (std::abs(lightController.getWaverSpeed() - activeCfg.lightWaverSpeed) > 0.001f) {
            lightController.setWaverSpeed(activeCfg.lightWaverSpeed);
        }
        if (std::abs(lightController.getFlickerLevel() - activeCfg.lightFlicker) > 0.001f) {
            lightController.setFlickerLevel(activeCfg.lightFlicker);
        }
        lightController.update(dt);
        const eatsbits::ui::LightSource2D dynamicLight = lightController.getEffectiveLight(static_cast<float>(width), static_cast<float>(height));
        const auto lCol = lightController.getLightColor();
        const auto aCol = lightController.getAmbientColor();

        if (width > 0 && height > 0) {
            bool postFxEnabled = false;
            if (!activeCfg.postFxId.empty() && activeCfg.postFxId != "none" &&
                !(settingsDialog.isOpen() && settingsDialog.isPeekingPostFx())) {
                const auto& available = PostFxSystem::instance().availableEffects();
                (void)available;
                const PostFxDef* pfx = PostFxSystem::instance().getEffectById(activeCfg.postFxId);
                if (pfx && !pfx->wgslSource.empty()) {
                    if (renderer.setPostFxShader(pfx->wgslSource)) {
                        float u[4]{};
                        float p1 = 0.0f, p2 = 0.0f;
                        PostFxSystem::instance().resolveParams(activeCfg.postFxId, activeCfg.postFxOption, u, p1, p2);
                        renderer.setPostFxParams(activeCfg.postFxIntensity, u[0], u[1], u[2], u[3], p1, p2);
                        postFxEnabled = true;
                    }
                }
            }
            renderer.setPostFxActive(postFxEnabled);

            settingsDialog.setLowCapabilityHardware(renderer.isLowCapabilityHardware());
            uint32_t maxRenderW = 0;
            uint32_t maxRenderH = 0;
            RenderResolutionMode effectiveRes = activeCfg.renderResolution;
            if (effectiveRes == RenderResolutionMode::Auto) {
                effectiveRes = renderer.isLowCapabilityHardware()
                    ? RenderResolutionMode::Res720p
                    : RenderResolutionMode::Native;
            }
            switch (effectiveRes) {
                case RenderResolutionMode::Res1080p: maxRenderW = 1920; maxRenderH = 1080; break;
                case RenderResolutionMode::Res720p:  maxRenderW = 1280; maxRenderH = 720;  break;
                case RenderResolutionMode::Res540p:  maxRenderW = 960;  maxRenderH = 540;  break;
                default: break;
            }
            renderer.setInternalMaxResolution(maxRenderW, maxRenderH);

            renderer.beginFrame(static_cast<float>(width), static_cast<float>(height));
            const eatsbits::ui::LightSource2D steadyLight = lightController.getSteadyLight(static_cast<float>(width), static_cast<float>(height));
            renderer.setGlobalLight(dynamicLight, lCol[0], lCol[1], lCol[2], aCol[0], aCol[1], aCol[2],
                                    activeCfg.sceneDarkness, activeCfg.lightStrength);
            renderer.setSceneAtmosphere(static_cast<uint8_t>(activeCfg.shadowGobo),
                                        activeCfg.goboSoftness,
                                        activeCfg.dustMotesIntensity,
                                        steadyLight.x, steadyLight.y, steadyLight.z,
                                        static_cast<uint8_t>(activeCfg.lightAngle),
                                        activeCfg.cloudOpacity);
            renderer.setShaderTime(static_cast<float>(now));
            if (board.activeGameDef().id != lastOddsGameId) {
                cancelOddsAnalysis();
                { std::lock_guard<std::mutex> lk(oddsMutex); helpOdds = {}; }
                highlightedHelpZone = -1;
                helpScrollY = 0.0f;
                lastOddsGameId = board.activeGameDef().id;
            }

            if (hintHighlight.active) {
                hintHighlight.age += dt;
                if (hintHighlight.age >= hintHighlight.duration ||
                    board.moveCount() != hintMoveCount ||
                    autoPlayController.isPlaying() ||
                    board.isVictory()) {
                    hintHighlight.active = false;
                }
            }

            int newlyActivated = magicParticles.update(dt);
            if (newlyActivated > 0) {
                if (magicSoundAccum == 0) {
                    foley.triggerSound(FoleySound::MagicTine, 0.38f);
                }
                magicSoundAccum += newlyActivated;
                while (magicSoundAccum >= 20) {
                    magicSoundAccum -= 20;
                    foley.triggerSound(FoleySound::MagicTine, 0.38f);
                }
            }
            if (!autoPlayController.isPlaying()) {
                lastTrailedMoveIdx = SIZE_MAX;
            } else if (autoPlayController.showMoveTrails() &&
                       autoPlayController.moveProgress() >= 0.30f &&
                       lastTrailedMoveIdx != autoPlayController.currentMoveIndex()) {
                if (const SolverMove* nextMv = autoPlayController.peekNextMove()) {
                    lastTrailedMoveIdx = autoPlayController.currentMoveIndex();
                    const auto& piles = board.piles();
                    if (nextMv->fromPile < piles.size() && nextMv->toPile < piles.size()) {
                        const auto& sp = piles[nextMv->fromPile];
                        const auto& dp = piles[nextMv->toPile];
                        float sx = sp.x + board.cardWidth() * 0.5f;
                        float sy = sp.y + board.cardHeight() * 0.5f;
                        if (nextMv->cardIndex < sp.cards.size()) {
                            sx = sp.cards[nextMv->cardIndex].x + board.cardWidth() * 0.5f;
                            sy = sp.cards[nextMv->cardIndex].y + board.cardHeight() * 0.5f;
                        }
                        float dx = dp.x + board.cardWidth() * 0.5f;
                        float dy = dp.y + board.cardHeight() * 0.5f;
                        if (!dp.cards.empty()) {
                            dx = dp.cards.back().x + board.cardWidth() * 0.5f;
                            dy = dp.cards.back().y + board.cardHeight() * 0.5f;
                        }
                        ThemePalette apal = getPalette(board.tableTheme());
                        magicParticles.spawnArcTrail(sx, sy, dx, dy, apal.accentR, apal.accentG, apal.accentB,
                                                     board.cardWidth(), 0.0f, true);
                    }
                }
            }

            if (draggedPile >= 0 && draggedPile < static_cast<int>(board.piles().size()) && draggedCardIdx >= 0) {
            const float safeDt = std::clamp(dt, 0.001f, 0.05f);
            const float vx = (dragOffsetX - prevDragOffsetX) / safeDt;
            const float vy = (dragOffsetY - prevDragOffsetY) / safeDt;
            prevDragOffsetX = dragOffsetX;
            prevDragOffsetY = dragOffsetY;

            const auto& dp = board.piles()[static_cast<size_t>(draggedPile)];
            const size_t firstIdx = static_cast<size_t>(draggedCardIdx);
            if (firstIdx < dp.cards.size()) {
                const size_t count = std::min<size_t>(dp.cards.size() - firstIdx, dragChainAngles.size());
                dragChainCount = count;
                const float cmX = board.cardWidth() * 0.5f;
                const float cmY = board.cardHeight() * 0.5f;
                const float rx = cmX - dragGrabLocalX;
                const float ry = cmY - dragGrabLocalY;
                const float torque0 = (-vx * ry + vy * rx) * 0.00018f;
                dragChainVels[0] += (torque0 - 150.0f * dragChainAngles[0] - 13.5f * dragChainVels[0]) * safeDt;
                dragChainAngles[0] = std::clamp(dragChainAngles[0] + dragChainVels[0] * safeDt, -0.30f, 0.30f);
                dragPivotAngle = dragChainAngles[0];

                for (size_t k = 1; k < count; ++k) {
                    const float offY = dp.cards[firstIdx + k].y - dp.cards[firstIdx + k - 1].y;
                    const float linkTorque = -vx * std::max(8.0f, offY) * 0.00014f;
                    const float couple = 105.0f * (dragChainAngles[k - 1] - dragChainAngles[k]);
                    const float restore = -45.0f * dragChainAngles[k];
                    dragChainVels[k] += (linkTorque + couple + restore - 12.0f * dragChainVels[k]) * safeDt;
                    dragChainAngles[k] = std::clamp(dragChainAngles[k] + dragChainVels[k] * safeDt, -0.36f, 0.36f);
                }
            }
        } else {
            dragPivotAngle = 0.0f;
            dragPivotVel = 0.0f;
            dragChainCount = 0;
        }

        for (size_t gi = 0; gi < activeGhostCards.size();) {
            activeGhostCards[gi].ageSec += dt;
            if (activeGhostCards[gi].ageSec >= activeGhostCards[gi].delaySec + activeGhostCards[gi].durationSec) {
                activeGhostCards.erase(activeGhostCards.begin() + static_cast<ptrdiff_t>(gi));
            } else {
                ++gi;
            }
        }

        if (!recordMovesPath.empty()) {
            const size_t curDepth = board.undoStack().size();
            if (curDepth > lastRecordedUndoDepth) {
                const auto& cmd = board.undoStack().back();
                DirectorBeat rb;
                if (cmd.fromPile == 0 && cmd.toPile == 1 && !board.piles().empty() && board.piles()[0].type == PileType::Stock) {
                    rb.action = DirectorAction::DrawStock;
                    rb.caption = "Draw from Stock";
                } else {
                    rb.action = DirectorAction::ExplicitMove;
                    rb.fromPile = static_cast<int>(cmd.fromPile);
                    rb.toPile = static_cast<int>(cmd.toPile);
                    const size_t fpSize = (cmd.fromPile < board.piles().size()) ? board.piles()[cmd.fromPile].cards.size() : 0;
                    rb.cardIndex = static_cast<int>(fpSize);
                    rb.caption = "Move pile " + std::to_string(rb.fromPile) + " -> " + std::to_string(rb.toPile);
                }
                recordedBeats.push_back(std::move(rb));
                lastRecordedUndoDepth = curDepth;

                SolitaireSolver checkSolver;
                auto checkRes = checkSolver.solve(board, 60000, 1000.0f);
                lastRecordedSolvable = checkRes.solved;
                recordStatusBanner = checkRes.solved
                    ? ("REC #" + std::to_string(recordedBeats.size()) + " | SOLVABLE (" + std::to_string(checkRes.moves.size()) + " left)")
                    : ("REC #" + std::to_string(recordedBeats.size()) + " | WARNING: UNSOLVABLE - PRESS [Z] TO UNDO!");
                recordStatusTimer = 4.0f;
            } else if (curDepth < lastRecordedUndoDepth) {
                while (recordedBeats.size() > curDepth && !recordedBeats.empty()) {
                    recordedBeats.pop_back();
                }
                lastRecordedUndoDepth = curDepth;
                recordStatusBanner = "REC #" + std::to_string(recordedBeats.size()) + " (Undid move)";
                recordStatusTimer = 2.5f;
            }
            if (recordStatusTimer > 0.0f) {
                recordStatusTimer = std::max(0.0f, recordStatusTimer - dt);
            }
        }

        if (directorState.active && !directorState.finished && !directorState.beats.empty() && !asyncManager.isBusy.load()) {
                auto startBeat = [&](size_t idx) {
                    directorState.beatIdx = idx;
                    directorState.beatTimer = 0.0f;
                    directorState.movesDoneInBeat = 0;
                    const DirectorBeat& b = directorState.beats[idx];
                    if (!b.wavPath.empty()) {
                        foley.playVoiceWav(b.wavPath, 1.0f);
                    }
                    if (b.action == DirectorAction::HighlightZone) {
                        directorState.highlightZone = b.targetZone;
                        if (autoPlayController.isPlaying()) autoPlayController.pause();
                        float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
                        computeZoneHighlightBounds(board, b.targetZone, zx, zy, zw, zh);
                        if (zw > 0.0f && zh > 0.0f) {
                            ThemePalette hpal = getPalette(board.tableTheme());
                            magicParticles.spawnZoneBurst(zx, zy, zw, zh, hpal.accentR, hpal.accentG, hpal.accentB);
                            foley.triggerSound(FoleySound::MagicTine, 0.35f);
                        }
                        if (!b.ghostCards.empty() && b.targetZone >= 0 && static_cast<size_t>(b.targetZone) < board.piles().size()) {
                            activeGhostCards.clear();
                            std::vector<std::string> tokens;
                            std::stringstream gss(b.ghostCards);
                            std::string tok;
                            while (std::getline(gss, tok, ',')) {
                                if (!tok.empty()) tokens.push_back(tok);
                            }
                            const auto& piles = board.piles();
                            const size_t basePileIdx = (b.ghostPile >= 0 && static_cast<size_t>(b.ghostPile) < piles.size())
                                ? static_cast<size_t>(b.ghostPile)
                                : static_cast<size_t>(b.targetZone);
                            const Pile& basePile = piles[basePileIdx];
                            const bool isFound = (basePile.type == PileType::Foundation);
                            const float fanStep = std::max(18.0f, board.cardHeight() * 0.24f);
                            float anchorX = basePile.x;
                            float anchorY = basePile.y;
                            if (!isFound && !basePile.cards.empty()) {
                                anchorX = basePile.cards.back().x;
                                anchorY = basePile.cards.back().y;
                            }
                            for (size_t gi = 0; gi < tokens.size(); ++gi) {
                                const std::string& code = tokens[gi];
                                if (code.size() < 2) continue;
                                const char sCh = static_cast<char>(std::toupper(static_cast<unsigned char>(code.back())));
                                const std::string rStr = code.substr(0, code.size() - 1);
                                Suit gs = Suit::Spades;
                                if (sCh == 'H') gs = Suit::Hearts;
                                else if (sCh == 'D') gs = Suit::Diamonds;
                                else if (sCh == 'C') gs = Suit::Clubs;
                                int rVal = 1;
                                if (rStr == "A") rVal = 1;
                                else if (rStr == "J") rVal = 11;
                                else if (rStr == "Q") rVal = 12;
                                else if (rStr == "K") rVal = 13;
                                else rVal = std::clamp(std::stoi(rStr), 1, 13);

                                // If Foundation, pick the matching suit Foundation pile if available
                                float destX = anchorX;
                                float destY = anchorY;
                                if (isFound) {
                                    size_t foundOrd = 0;
                                    for (const auto& fp : piles) {
                                        if (fp.type == PileType::Foundation) {
                                            if ((foundOrd % 4) == static_cast<size_t>(gs)) {
                                                destX = fp.x;
                                                destY = fp.y;
                                                break;
                                            }
                                            ++foundOrd;
                                        }
                                    }
                                } else {
                                    destY = anchorY + static_cast<float>(gi + 1) * fanStep;
                                }

                                GhostCardCue cue;
                                cue.card.suit = gs;
                                cue.card.rank = static_cast<Rank>(rVal);
                                cue.card.faceUp = true;
                                cue.startX = destX;
                                cue.startY = destY + board.cardHeight() * 0.45f;
                                cue.targetX = destX;
                                cue.targetY = destY;
                                cue.delaySec = b.ghostDelaySec + static_cast<float>(gi) * 0.55f;
                                cue.ageSec = 0.0f;
                                cue.durationSec = std::max(2.0f, b.durationSec - cue.delaySec - 0.15f);
                                cue.maxOpacity = 0.72f;
                                activeGhostCards.push_back(cue);
                            }
                        }
                    } else if (b.action == DirectorAction::PlayMoves) {
                        directorState.highlightZone = -1;
                        if (!autoPlayController.isActive()) {
                            autoPlayController.startDemo(board);
                            autoPlayController.pause();
                        }
                        const int mc = std::max(1, b.moveCount);
                        directorState.nextMoveAt = b.durationSec / static_cast<float>(mc + 1);
                    } else if (b.action == DirectorAction::ResetCamera) {
                        directorState.highlightZone = -1;
                        if (autoPlayController.isPlaying()) autoPlayController.pause();
                    } else if (b.action == DirectorAction::FinishGame) {
                        directorState.highlightZone = -1;
                        autoPlayController.start(board);
                        autoPlayController.setSpeed(AutoPlaySpeed::Hyper);
                        autoPlayController.play();
                    } else if (b.action == DirectorAction::ExplicitMove) {
                        directorState.highlightZone = -1;
                        if (autoPlayController.isPlaying()) autoPlayController.pause();
                        if (b.fromPile >= 0 && b.toPile >= 0) {
                            const auto& piles = board.piles();
                            const size_t fp = static_cast<size_t>(b.fromPile);
                            const size_t tp = static_cast<size_t>(b.toPile);
                            if (fp < piles.size() && tp < piles.size() && !piles[fp].cards.empty()) {
                                const size_t ci = (b.cardIndex >= 0 && static_cast<size_t>(b.cardIndex) < piles[fp].cards.size())
                                    ? static_cast<size_t>(b.cardIndex)
                                    : (piles[fp].cards.size() - 1);
                                directorState.moveFocusFrom = b.fromPile;
                                directorState.moveFocusCard = static_cast<int>(ci);
                                directorState.moveFocusTo = b.toPile;
                                directorState.moveFocusTimer = std::min(b.durationSec, 2.2f);
                                const float sx = piles[fp].cards[ci].x + board.cardWidth() * 0.5f;
                                const float sy = piles[fp].cards[ci].y + board.cardHeight() * 0.5f;
                                const float dx = (piles[tp].cards.empty() ? piles[tp].x : piles[tp].cards.back().x) + board.cardWidth() * 0.5f;
                                const float dy = (piles[tp].cards.empty() ? piles[tp].y : piles[tp].cards.back().y) + board.cardHeight() * 0.5f;
                                ThemePalette apal = getPalette(board.tableTheme());
                                magicParticles.spawnArcTrail(sx, sy, dx, dy, apal.accentR, apal.accentG, apal.accentB, board.cardWidth(), 0.0f, true);
                                if (board.executeMove(static_cast<uint8_t>(fp), ci, static_cast<uint8_t>(tp))) {
                                    foley.triggerSound(FoleySound::Place, 0.85f);
                                }
                            }
                        }
                    } else if (b.action == DirectorAction::DrawStock) {
                        directorState.highlightZone = -1;
                        if (autoPlayController.isPlaying()) autoPlayController.pause();
                        directorState.moveFocusFrom = 0;
                        directorState.moveFocusCard = -1;
                        directorState.moveFocusTo = 1;
                        directorState.moveFocusTimer = std::min(b.durationSec, 1.6f);
                        if (board.drawFromStock()) {
                            foley.triggerSound(FoleySound::Deal, 0.85f);
                        }
                    } else if (b.action == DirectorAction::Hint) {
                        directorState.highlightZone = -1;
                        HintMove hm = board.findBestHint();
                        if (hm.valid) {
                            hintHighlight.active = true;
                            hintHighlight.fromPile = hm.fromPile;
                            hintHighlight.cardIndex = hm.cardIndex;
                            hintHighlight.toPile = hm.toPile;
                            hintHighlight.age = 0.0f;
                            hintHighlight.duration = std::max(1.2f, std::min(b.durationSec, 2.5f));
                            hintMoveCount = board.moveCount();
                            directorState.moveFocusFrom = hm.fromPile;
                            directorState.moveFocusCard = hm.cardIndex;
                            directorState.moveFocusTo = hm.toPile;
                            directorState.moveFocusTimer = hintHighlight.duration;
                        }
                    } else {
                        directorState.highlightZone = -1;
                    }
                };

                directorState.totalElapsed += dt;
                if (directorState.moveFocusTimer > 0.0f) {
                    directorState.moveFocusTimer = std::max(0.0f, directorState.moveFocusTimer - dt);
                    if (directorState.moveFocusTimer <= 0.0f) {
                        directorState.moveFocusFrom = -1;
                        directorState.moveFocusCard = -1;
                        directorState.moveFocusTo = -1;
                    }
                }
                const DirectorBeat& cur = directorState.beats[directorState.beatIdx];
                if (cur.action != DirectorAction::FinishGame && autoPlayController.isPlaying()) {
                    autoPlayController.pause();
                }
                if (directorState.beatTimer < 0.0f) {
                    startBeat(directorState.beatIdx);
                } else {
                    directorState.beatTimer += dt;
                    if (cur.action == DirectorAction::PlayMoves && cur.moveCount > 0 && !autoPlayController.isSolving()) {
                        if (!autoPlayController.isActive()) {
                            autoPlayController.startDemo(board);
                            autoPlayController.pause();
                        } else if (directorState.movesDoneInBeat < cur.moveCount && directorState.beatTimer >= directorState.nextMoveAt) {
                            if (const SolverMove* mv = autoPlayController.peekCurrentOrNextMove()) {
                                directorState.moveFocusFrom = static_cast<int>(mv->fromPile);
                                directorState.moveFocusCard = static_cast<int>(mv->cardIndex);
                                directorState.moveFocusTo = static_cast<int>(mv->toPile);
                                directorState.moveFocusTimer = 1.65f;
                                const auto& piles = board.piles();
                                if (mv->fromPile < piles.size() && mv->toPile < piles.size()) {
                                    const auto& sp = piles[mv->fromPile];
                                    const auto& dp = piles[mv->toPile];
                                    float sx = sp.x + board.cardWidth() * 0.5f;
                                    float sy = sp.y + board.cardHeight() * 0.5f;
                                    if (mv->cardIndex < sp.cards.size()) {
                                        sx = sp.cards[mv->cardIndex].x + board.cardWidth() * 0.5f;
                                        sy = sp.cards[mv->cardIndex].y + board.cardHeight() * 0.5f;
                                    }
                                    float dx = dp.x + board.cardWidth() * 0.5f;
                                    float dy = dp.y + board.cardHeight() * 0.5f;
                                    if (!dp.cards.empty()) {
                                        dx = dp.cards.back().x + board.cardWidth() * 0.5f;
                                        dy = dp.cards.back().y + board.cardHeight() * 0.5f;
                                    }
                                    ThemePalette apal = getPalette(board.tableTheme());
                                    magicParticles.spawnArcTrail(sx, sy, dx, dy, apal.accentR, apal.accentG, apal.accentB,
                                                                 board.cardWidth(), 0.0f, true);
                                }
                            }
                            autoPlayController.stepForward(board, &foley);
                            ++directorState.movesDoneInBeat;
                            const float stepGap = cur.durationSec / static_cast<float>(cur.moveCount + 1);
                            directorState.nextMoveAt += stepGap;
                        }
                    }
                    const bool isFinishBeat = (cur.action == DirectorAction::FinishGame);
                    const bool finishReady = !isFinishBeat ||
                                             board.isVictory() ||
                                             autoPlayController.state() == AutoPlayState::Completed ||
                                             autoPlayController.state() == AutoPlayState::Failed;
                    if (directorState.beatTimer >= cur.durationSec && finishReady) {
                        if (directorState.beatIdx + 1 < directorState.beats.size()) {
                            startBeat(directorState.beatIdx + 1);
                        } else {
                            directorState.finished = true;
                            directorState.highlightZone = -1;
                            if (directorAutoExit) {
                                glfwSetWindowShouldClose(window, GLFW_TRUE);
                            }
                        }
                    }
                }
            }

            HelpOddsInfo oddsSnap;
            { std::lock_guard<std::mutex> lk(oddsMutex); oddsSnap = helpOdds; }
            HelpVideoThumb videoSnap;
            if (helpOpen) {
                ensureHelpVideoThumb(board.docManifest().videoId, board.docManifest().videoThumbBase64);
                std::lock_guard<std::mutex> lk(videoThumbMutex);
                videoSnap = helpVideoThumb;
            }
            const int activeHighlightZone = helpOpen
                ? highlightedHelpZone
                : (directorState.active ? directorState.highlightZone : -1);
            updateShowcaseCamera(showcaseCam, board, autoPlayController,
                                 activeHighlightZone,
                                 static_cast<float>(width), static_cast<float>(height), dt,
                                 directorState.active ? directorState.highlightZone : -1,
                                 directorState.active ? directorState.moveFocusFrom : -1,
                                 directorState.active ? directorState.moveFocusCard : -1,
                                 directorState.active ? directorState.moveFocusTo : -1);
            TableRenderer::renderBoard(renderer, board, static_cast<float>(width), static_cast<float>(height),
                                       draggedPile, draggedCardIdx, dragOffsetX, dragOffsetY, hoveredBtn,
                                       menuOpen, menuAnchor, menuFlowVertical, isDraggingMenu, dragPreviewAnchor, dragPreviewFlowVertical,
                                       isGesturing, gestureStartX, gestureStartY, gestureCurX, gestureCurY,
                                       &dynamicLight, &autoPlayController,
                                       helpOpen, activeHighlightZone, helpScrollY, fmx, fmy, &oddsSnap, &magicParticles, &hintHighlight,
                                       screensaverActive, &showcaseCam, directorHideHud,
                                       dragPivotAngle, dragGrabLocalX, dragGrabLocalY,
                                       dragChainCount > 0 ? dragChainAngles.data() : nullptr, dragChainCount,
                                       activeGhostCards.empty() ? nullptr : &activeGhostCards,
                                       helpOpen ? &videoSnap : nullptr,
                                       victoryGather.active ? &victoryGather : nullptr);
            if (!activeCfg.postFxAffectUi) {
                renderer.applyPostFxPass();
            }
            if (settingsDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                settingsDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), uiFmx, uiFmy, pal, static_cast<float>(now));
            }
            if (seedDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                seedDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), uiFmx, uiFmy, pal, static_cast<float>(now), window);
            }
            if (gameBrowserDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                gameBrowserDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), uiFmx, uiFmy, pal);
            }
            if (loadingDialog.isOpen() && !directorState.active) {
                ThemePalette pal = getPalette(board.tableTheme());
                loadingDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), uiFmx, uiFmy, pal, now);
            }

            // Opening Animated Title Card for Director Mode (2x scale, eased fly-in from left -> fly-out to right)
            if (directorState.active && !asyncManager.isBusy.load() && directorState.totalElapsed < 4.2f) {
                const float t = directorState.totalElapsed;
                const float fw = static_cast<float>(width);
                const float fh = static_cast<float>(height);
                const float cxBase = fw * 0.5f;
                const float cy = fh * 0.5f;
                const auto& cpal = clockwork_ui::palette();

                float scrimAlpha = 0.78f;
                float slideX = 0.0f;
                if (t < 0.65f) {
                    const float p = std::clamp(t / 0.65f, 0.0f, 1.0f);
                    const float easeOut = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
                    slideX = -fw * 0.75f * (1.0f - easeOut);
                    scrimAlpha = 0.78f * easeOut;
                } else if (t > 3.45f) {
                    const float p = std::clamp((t - 3.45f) / 0.75f, 0.0f, 1.0f);
                    const float easeIn = p * p * p;
                    slideX = fw * 0.75f * easeIn;
                    scrimAlpha = 0.78f * (1.0f - easeIn);
                }

                renderer.drawRect(0.0f, 0.0f, fw, fh, 0.04f, 0.03f, 0.025f, scrimAlpha);

                const float cx = cxBase + slideX;
                if (cx > -150.0f && cx < fw + 150.0f) {
                    clockwork_ui::drawClockworkMenuOrb(renderer, cx, cy - 84.0f, 92.0f, true, false, static_cast<float>(now));

                    const char* brandTxt = "SolEngine";
                    const float brandSz = 76.0f;
                    const float brandW = eatsbits::ui::glyph::getVectorTextWidth(brandSz, brandTxt);
                    eatsbits::ui::glyph::drawVectorText(renderer, brandTxt, cx - brandW * 0.5f + 3.0f, cy + 38.0f, brandSz,
                                                        0.0f, 0.0f, 0.0f, 0.70f);
                    eatsbits::ui::glyph::drawVectorText(renderer, brandTxt, cx - brandW * 0.5f, cy + 34.0f, brandSz,
                                                        cpal.brassHighlightR, cpal.brassHighlightG, cpal.brassHighlightB, 1.0f);

                    std::string gName = board.activeGameDef().name;
                    if (gName.empty()) gName = SolitaireBoard::variantName(board.variant());
                    const float subSz = 38.0f;
                    const float subW = eatsbits::ui::glyph::getVectorTextWidth(subSz, gName.c_str());
                    eatsbits::ui::glyph::drawVectorText(renderer, gName.c_str(), cx - subW * 0.5f + 2.0f, cy + 126.0f, subSz,
                                                        0.0f, 0.0f, 0.0f, 0.65f);
                    eatsbits::ui::glyph::drawVectorText(renderer, gName.c_str(), cx - subW * 0.5f, cy + 124.0f, subSz,
                                                        0.94f, 0.90f, 0.82f, 0.96f);
                }
            }

            // Draw hold progress ring on active button
            if (pressedToolbarBtn != BtnNone && !holdTriggered &&
                (pressedToolbarBtn == BtnNewDeal || pressedToolbarBtn == BtnToolbarResize)) {
                float progress = std::clamp((static_cast<float>(now) - pressStartTime) / 0.35f, 0.0f, 1.0f);
                if (progress > 0.05f) {
                    const auto& cpal = clockwork_ui::palette();
                    renderer.drawCircleOutline(pressStartX, pressStartY, 22.0f,
                                               cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.95f, 2.2f, 28);
                    renderer.drawCircle(pressStartX, pressStartY, 14.0f * progress,
                                        cpal.furnaceAmberR, cpal.furnaceAmberG, cpal.furnaceAmberB, 0.35f, 20);
                }
            }

            if (playMenuOpen) {
                int dummyHover = -1;
                drawPlayContextMenu(renderer, static_cast<float>(width), static_cast<float>(height),
                                    playMenuAnchorX, playMenuAnchorY, uiFmx, uiFmy,
                                    SettingsManager::instance().settings().guaranteeSolvable, dummyHover);
            }

            if (toolbarConfigOpen) {
                int dummyScale = -1;
                int dummyToggle = -1;
                bool dummyClose = false;
                drawToolbarConfigMenu(renderer, static_cast<float>(width), static_cast<float>(height),
                                      toolbarConfigAnchorX, toolbarConfigAnchorY, uiFmx, uiFmy,
                                      SettingsManager::instance().settings(),
                                      dummyScale, dummyToggle, dummyClose);
            }
            if (!recordMovesPath.empty() && !recordStatusBanner.empty() && recordStatusTimer > 0.0f) {
                const float fw = static_cast<float>(width);
                const float bw = eatsbits::ui::glyph::getVectorTextWidth(13.0f, recordStatusBanner.c_str()) + 28.0f;
                const float bx = (fw - bw) * 0.5f;
                renderer.drawRoundedRect(bx, 12.0f, bw, 28.0f, 6.0f,
                                         lastRecordedSolvable ? 0.06f : 0.35f,
                                         lastRecordedSolvable ? 0.14f : 0.06f,
                                         0.06f, 0.90f, 4);
                eatsbits::ui::glyph::drawVectorText(renderer, recordStatusBanner.c_str(), bx + 14.0f, 19.0f, 13.0f,
                                                    lastRecordedSolvable ? 0.55f : 1.0f,
                                                    lastRecordedSolvable ? 0.95f : 0.45f,
                                                    lastRecordedSolvable ? 0.60f : 0.45f, 1.0f);
            }

            renderer.endFrame();
            renderer.clearSceneCamera();
        }
    }
};

static std::unique_ptr<InteractiveContext> s_activeContext;

int TableRenderer::runInteractiveWindow(
    SolitaireBoard& board,
    AudioFoleyEngine& foley,
    uint32_t width,
    uint32_t height,
    [[maybe_unused]] int initialAaMode,
    bool startScreensaver,
    const DirectorOptions& directorOptions
) {
#if defined(__EMSCRIPTEN__)
    double initCssW = EM_ASM_DOUBLE({
        if (typeof document === 'undefined') return 1280;
        var container = document.getElementById('canvas-container');
        var cw = (container && container.clientWidth > 0) ? container.clientWidth : 0;
        var iw = (typeof window !== 'undefined' && window.innerWidth) ? window.innerWidth : 0;
        return Math.max(cw, iw) || 1280;
    });
    double initCssH = EM_ASM_DOUBLE({
        if (typeof document === 'undefined') return 720;
        var container = document.getElementById('canvas-container');
        var ch = (container && container.clientHeight > 0) ? container.clientHeight : 0;
        var ih = (typeof window !== 'undefined' && window.innerHeight) ? window.innerHeight : 0;
        return Math.max(ch, ih) || 720;
    });
    double initDpr = EM_ASM_DOUBLE({
        return (typeof window !== 'undefined' && window.devicePixelRatio) ? window.devicePixelRatio : 1.0;
    });
    initDpr = std::clamp(initDpr, 1.0, 2.0);
    if (initCssW > 0.0 && initCssH > 0.0) {
        width = static_cast<uint32_t>(std::round(initCssW));
        height = static_cast<uint32_t>(std::round(initCssH));
    }
    uint32_t initFbW = static_cast<uint32_t>(std::round(static_cast<float>(width) * static_cast<float>(initDpr)));
    uint32_t initFbH = static_cast<uint32_t>(std::round(static_cast<float>(height) * static_cast<float>(initDpr)));
#endif

    if (!glfwInit()) {
        std::cerr << "[SolEngine] Failed to initialize GLFW windowing.\n";
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    const bool isOfflineExport = !directorOptions.exportVideoDir.empty();
    if (isOfflineExport) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }
    const int winCreateW = isOfflineExport ? 640 : static_cast<int>(width);
    const int winCreateH = isOfflineExport ? 360 : static_cast<int>(height);
    GLFWwindow* window = glfwCreateWindow(winCreateW, winCreateH,
                                          "Solitaire Engine", nullptr, nullptr);
    if (!window) {
#if !defined(__EMSCRIPTEN__)
        const char* desc = nullptr;
        glfwGetError(&desc);
        std::cerr << "[SolEngine] Failed to create GLFW window: " << (desc ? desc : "unknown error") << "\n";
#else
        std::cerr << "[SolEngine] Failed to create GLFW window.\n";
#endif
        glfwTerminate();
        return 1;
    }

#if !defined(__EMSCRIPTEN__)
    // Set native desktop window and taskbar icon
    {
        GLFWimage glfwIcon;
        glfwIcon.width = solengine::clockwork_icon::kAppIconWidth;
        glfwIcon.height = solengine::clockwork_icon::kAppIconHeight;
        glfwIcon.pixels = const_cast<unsigned char*>(solengine::clockwork_icon::kAppIconRgba64);
        glfwSetWindowIcon(window, 1, &glfwIcon);
    }
#endif

#if defined(__EMSCRIPTEN__)
    EM_ASM({
        if (typeof document === 'undefined') return;
        var canvas = document.getElementById('canvas');
        if (canvas) {
            canvas.width = $0;
            canvas.height = $1;
            canvas.style.width = '100%';
            canvas.style.height = '100%';
        }
        if (typeof GLFW !== 'undefined' && GLFW.active) {
            GLFW.active.width = $2;
            GLFW.active.height = $3;
            GLFW.active.framebufferWidth = $0;
            GLFW.active.framebufferHeight = $1;
        }
    }, initFbW, initFbH, width, height);
#endif

    s_activeContext = std::make_unique<InteractiveContext>(board, foley, width, height, directorOptions.skipSessionRestore);
    InteractiveContext& ctx = *s_activeContext;
    ctx.window = window;
    ctx.directorHideHud = directorOptions.hideHud;
    ctx.directorAutoExit = directorOptions.autoExit;
    ctx.recordMovesPath = directorOptions.recordMovesPath;
    ctx.lastRecordedUndoDepth = board.undoStack().size();
    if (!directorOptions.timelinePath.empty()) {
        if (loadDirectorTimeline(directorOptions.timelinePath, ctx.directorState.beats)) {
            ctx.directorState.active = true;
            ctx.directorState.beatIdx = 0;
            ctx.directorState.beatTimer = -1.0f; // Triggers startBeat(0) on first frame
            std::cout << "[SolEngine Director] Loaded " << ctx.directorState.beats.size()
                      << " beat(s) from " << directorOptions.timelinePath << "\n";
        } else {
            std::cerr << "[SolEngine Director] Failed to load timeline: " << directorOptions.timelinePath << "\n";
        }
    }

    glfwSetCharCallback(window, [](GLFWwindow* /*win*/, unsigned int codepoint) {
        if (s_activeContext && s_activeContext->gameBrowserDialog.isOpen()) {
            s_activeContext->gameBrowserDialog.handleChar(codepoint);
        } else if (s_activeContext && s_activeContext->settingsDialog.isOpen()) {
            s_activeContext->settingsDialog.handleChar(codepoint);
        } else if (s_activeContext && s_activeContext->seedDialog.isOpen()) {
            s_activeContext->seedDialog.handleChar(codepoint);
        }
    });

    glfwSetScrollCallback(window, [](GLFWwindow* /*win*/, double /*xoffset*/, double yoffset) {
        if (!s_activeContext) return;
        if (s_activeContext->gameBrowserDialog.isOpen()) {
            s_activeContext->gameBrowserDialog.handleScroll(static_cast<float>(yoffset));
        } else if (s_activeContext->settingsDialog.isOpen()) {
            s_activeContext->settingsDialog.handleScroll(static_cast<float>(yoffset));
        } else if (s_activeContext->helpOpen) {
            HelpOddsInfo oddsSnap;
            { std::lock_guard<std::mutex> lk(s_activeContext->oddsMutex); oddsSnap = s_activeContext->helpOdds; }
            const HelpSidebarLayout hl = buildHelpSidebarLayout(
                s_activeContext->board,
                static_cast<float>(s_activeContext->width),
                static_cast<float>(s_activeContext->height),
                0.0f,
                &oddsSnap,
                s_activeContext->hasHelpVideoThumb()
            );
            s_activeContext->helpMaxScrollY = std::max(0.0f, hl.contentH - hl.viewH);
            s_activeContext->helpScrollY = std::clamp(
                s_activeContext->helpScrollY - static_cast<float>(yoffset) * 28.0f,
                0.0f,
                s_activeContext->helpMaxScrollY
            );
        } else if (!s_activeContext->screensaverActive &&
                   !s_activeContext->loadingDialog.isOpen() &&
                   !s_activeContext->seedDialog.isOpen() &&
                   !s_activeContext->playMenuOpen &&
                   !s_activeContext->toolbarConfigOpen &&
                   !s_activeContext->autoPlayController.isActive()) {
            const float factor = std::pow(1.12f, static_cast<float>(yoffset));
            s_activeContext->zoomUserCameraAt(factor, s_activeContext->lastWarpedFmx, s_activeContext->lastWarpedFmy);
        }
    });

#if defined(__EMSCRIPTEN__)
    auto handleTouchEvent = [](int eventType, const EmscriptenTouchEvent* touchEvent, void* /*userData*/) -> EM_BOOL {
        if (!s_activeContext || !touchEvent) return EM_FALSE;
        auto& ctx = *s_activeContext;
        if (ctx.screensaverActive || ctx.loadingDialog.isOpen() || ctx.seedDialog.isOpen() ||
            ctx.gameBrowserDialog.isOpen() || ctx.settingsDialog.isOpen() || ctx.helpOpen ||
            ctx.playMenuOpen || ctx.toolbarConfigOpen || ctx.autoPlayController.isActive()) {
            ctx.pinchActive = false;
            return EM_FALSE;
        }
        const double nowSec = glfwGetTime();
        if (touchEvent->numTouches >= 2 &&
            (eventType == EMSCRIPTEN_EVENT_TOUCHSTART || eventType == EMSCRIPTEN_EVENT_TOUCHMOVE)) {
            const float x0 = static_cast<float>(touchEvent->touches[0].targetX);
            const float y0 = static_cast<float>(touchEvent->touches[0].targetY);
            const float x1 = static_cast<float>(touchEvent->touches[1].targetX);
            const float y1 = static_cast<float>(touchEvent->touches[1].targetY);
            const float dist = std::max(8.0f, std::hypot(x1 - x0, y1 - y0));
            float midX = (x0 + x1) * 0.5f;
            float midY = (y0 + y1) * 0.5f;
            const auto& cfg = ctx.effectiveSettings_;
            PostFxSystem::instance().warpMouseCoords(
                cfg.postFxId, cfg.postFxOption, cfg.postFxIntensity,
                static_cast<float>(ctx.width), static_cast<float>(ctx.height), midX, midY);

            if (!ctx.pinchActive || eventType == EMSCRIPTEN_EVENT_TOUCHSTART) {
                ctx.pinchActive = true;
                ctx.pinchLastDist = dist;
                ctx.pinchLastMidX = midX;
                ctx.pinchLastMidY = midY;
                ctx.showcaseCam.userPanActive = true;
            } else {
                ctx.panUserCameraBy(midX - ctx.pinchLastMidX, midY - ctx.pinchLastMidY);
                if (ctx.pinchLastDist > 1.0f) {
                    ctx.zoomUserCameraAt(dist / ctx.pinchLastDist, midX, midY);
                }
                ctx.pinchLastDist = dist;
                ctx.pinchLastMidX = midX;
                ctx.pinchLastMidY = midY;
                ctx.showcaseCam.userPanActive = true;
            }
            ctx.pinchSuppressUntil = nowSec + 0.14;
            return EM_TRUE;
        }
        if (ctx.pinchActive && touchEvent->numTouches < 2) {
            ctx.pinchActive = false;
            ctx.showcaseCam.userPanActive = false;
            ctx.pinchSuppressUntil = nowSec + 0.14;
        }
        return EM_FALSE;
    };
    emscripten_set_touchstart_callback("#canvas", nullptr, EM_FALSE, handleTouchEvent);
    emscripten_set_touchmove_callback("#canvas", nullptr, EM_FALSE, handleTouchEvent);
    emscripten_set_touchend_callback("#canvas", nullptr, EM_FALSE, handleTouchEvent);
    emscripten_set_touchcancel_callback("#canvas", nullptr, EM_FALSE, handleTouchEvent);
#endif

    glfwSetDropCallback(window, [](GLFWwindow* /*win*/, int count, const char* paths[]) {
        for (int i = 0; i < count; ++i) {
            if (paths && paths[i]) {
                std::cout << "[SolEngine] Drag-and-drop file received: " << paths[i] << std::endl;
                loadFile(paths[i]);
            }
        }
    });
#if defined(__EMSCRIPTEN__)
    ctx.fbWidth = initFbW;
    ctx.fbHeight = initFbH;
    ctx.currentDpr = static_cast<float>(initDpr);
    ctx.renderer.setRenderScale(ctx.currentDpr, ctx.currentDpr);
#endif

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    HWND hwnd = glfwGetWin32Window(window);
    void* nativeHandle = hwnd ? static_cast<void*>(hwnd) : static_cast<void*>(window);
#else
    void* nativeHandle = static_cast<void*>(window);
#endif

#if defined(__EMSCRIPTEN__)
    if (!ctx.renderer.initialize(nativeHandle, initFbW, initFbH, eatsbits::ui::RenderBackendType::WebGPU)) {
#else
    if (!ctx.renderer.initialize(nativeHandle, width, height, eatsbits::ui::RenderBackendType::WebGPU)) {
#endif
        std::cerr << "[SolEngine] Failed to initialize 2D batch renderer.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        s_activeContext.reset();
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    ctx.renderer.setAntiAliasingMode(ctx.renderer.isLowCapabilityHardware() ? 0 : 4);
#else
    ctx.renderer.setAntiAliasingMode(4);
#endif
    CardTextureCache::instance().initialize();
    if (ctx.renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
        CardTextureCache::instance().buildGpuAtlas(ctx.renderer);
    }

#if defined(__EMSCRIPTEN__)
    double initSafeTop = EM_ASM_DOUBLE({
        if (typeof getComputedStyle === 'undefined' || typeof document === 'undefined') return 0.0;
        return parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--sat')) || 0.0;
    });
    double initSafeRight = EM_ASM_DOUBLE({
        if (typeof getComputedStyle === 'undefined' || typeof document === 'undefined') return 0.0;
        return parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--sar')) || 0.0;
    });
    double initSafeBottom = EM_ASM_DOUBLE({
        if (typeof getComputedStyle === 'undefined' || typeof document === 'undefined') return 0.0;
        return parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--sab')) || 0.0;
    });
    double initSafeLeft = EM_ASM_DOUBLE({
        if (typeof getComputedStyle === 'undefined' || typeof document === 'undefined') return 0.0;
        return parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--sal')) || 0.0;
    });
    board.setSafeAreaInsets(
        static_cast<float>(initSafeTop),
        static_cast<float>(initSafeRight),
        static_cast<float>(initSafeBottom),
        static_cast<float>(initSafeLeft)
    );
#endif
    board.computeLayout(static_cast<float>(width), static_cast<float>(height));
    ctx.lastTime = glfwGetTime();

    // Check user settings and asynchronously load/condition deck if different from baseline
    ctx.checkStartupSettingsLoad();
    if (startScreensaver) {
        ctx.enterScreensaverMode();
    } else if (!ctx.asyncManager.isBusy.load()) {
        foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
    }

#if defined(__EMSCRIPTEN__)
    std::cout << "[SolEngine Web] Registering ResizeObserver and Emscripten requestAnimationFrame main loop..." << std::endl;
    EM_ASM({
        if (!window._solResizeHooked) {
            window._solResizeHooked = true;
            var notifyResize = function() {
                var container = document.getElementById('canvas-container');
                var cw = (container && container.clientWidth > 0) ? container.clientWidth : 0;
                var ch = (container && container.clientHeight > 0) ? container.clientHeight : 0;
                var iw = window.innerWidth || 0;
                var ih = window.innerHeight || 0;
                var w = Math.max(cw, iw) || 1280;
                var h = Math.max(ch, ih) || 720;
                var dpr = window.devicePixelRatio || 1.0;
                if (typeof Module !== 'undefined') {
                    if (Module._solengine_on_web_safe_area && typeof getComputedStyle !== 'undefined') {
                        var cs = getComputedStyle(document.documentElement);
                        var sat = parseFloat(cs.getPropertyValue('--sat')) || 0.0;
                        var sar = parseFloat(cs.getPropertyValue('--sar')) || 0.0;
                        var sab = parseFloat(cs.getPropertyValue('--sab')) || 0.0;
                        var sal = parseFloat(cs.getPropertyValue('--sal')) || 0.0;
                        Module._solengine_on_web_safe_area(sat, sar, sab, sal);
                    }
                    if (Module._solengine_on_web_resize) {
                        Module._solengine_on_web_resize(Math.round(w), Math.round(h), dpr);
                    }
                }
            };
            if (typeof ResizeObserver !== 'undefined') {
                var c = document.getElementById('canvas-container');
                if (c) {
                    new ResizeObserver(notifyResize).observe(c);
                }
            }
            window.addEventListener('resize', notifyResize);
            window.addEventListener('orientationchange', function() {
                setTimeout(notifyResize, 120);
                setTimeout(notifyResize, 350);
            });
            if (window.visualViewport) {
                window.visualViewport.addEventListener('resize', notifyResize);
            }
            notifyResize();
        }
    });
    emscripten_set_main_loop_arg([](void* arg) {
        auto* c = static_cast<InteractiveContext*>(arg);
        if (c) {
            c->updateAndRender();
        }
    }, &ctx, 0, 0);
    return 0;
#else
    if (!directorOptions.exportVideoDir.empty()) {
        const uint32_t exportFps = std::clamp(directorOptions.exportFps, 15u, 120u);
        const float fixedDt = 1.0f / static_cast<float>(exportFps);
        const uint32_t samplesPerFrame = 48000u / exportFps;

        const std::string logPath = directorOptions.exportVideoDir + "/export.log";
        FILE* fLog = std::fopen(logPath.c_str(), "w");
        auto logMsg = [&](const char* msg) {
            std::fprintf(stderr, "%s\n", msg);
            std::fflush(stderr);
            if (fLog) {
                std::fprintf(fLog, "%s\n", msg);
                std::fflush(fLog);
            }
        };

        if (!ctx.renderer.enableOffscreenCapture(width, height)) {
            logMsg("[SolEngine Export] Failed to initialize offscreen capture.");
        } else {
            ctx.renderer.resize(width, height);
            ctx.board.computeLayout(static_cast<float>(width), static_cast<float>(height));

            logMsg("[SolEngine Export] Waiting for initial deck/texture load...");
            int warmupTicks = 0;
            while (ctx.asyncManager.isBusy.load() && !glfwWindowShouldClose(window) && warmupTicks < 1800) {
                ctx.updateAndRender(fixedDt);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ++warmupTicks;
            }
            logMsg("[SolEngine Export] Assets ready. Starting capture...");

            const std::string rawPath = directorOptions.exportVideoDir + "/frames.raw";
            const std::string videoOnlyPath = directorOptions.exportVideoDir + "/video_only.mp4";
            const std::string wavPath = directorOptions.exportVideoDir + "/audio.wav";

            // Render 1 warmup frame to detect native pixel format (BGRA vs RGBA)
            std::vector<uint8_t> rawFrame;
            bool isBgra = true;
            FILE* fPipe = nullptr;
            FILE* fRaw = nullptr;
            bool usingPipe = false;

            if (!directorOptions.ffmpegBin.empty()) {
                ctx.updateAndRender(fixedDt);
                ctx.renderer.readbackCapturedNative(rawFrame, isBgra);
                const char* pixFmt = isBgra ? "bgra" : "rgba";
#if defined(_WIN32)
                std::string ffCmd = "\"" + directorOptions.ffmpegBin + "\" -y -loglevel error "
                                    "-f rawvideo -pix_fmt " + pixFmt + " -s " +
                                    std::to_string(width) + "x" + std::to_string(height) +
                                    " -r " + std::to_string(exportFps) +
                                    " -i - -c:v libx264 -preset veryfast -crf 15 -x264-params rc-lookahead=10 -pix_fmt yuv420p \"" +
                                    videoOnlyPath + "\"";
                HANDLE hRead = nullptr, hWrite = nullptr;
                SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
                if (CreatePipe(&hRead, &hWrite, &sa, 0)) {
                    SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0);
                    STARTUPINFOA si{};
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESTDHANDLES;
                    si.hStdInput = hRead;
                    si.hStdOutput = INVALID_HANDLE_VALUE;
                    si.hStdError = INVALID_HANDLE_VALUE;
                    PROCESS_INFORMATION pi{};
                    std::vector<char> cmdBuf(ffCmd.begin(), ffCmd.end());
                    cmdBuf.push_back('\0');
                    if (CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(hRead);
                        CloseHandle(pi.hThread);
                        CloseHandle(pi.hProcess);
                        int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hWrite), _O_WRONLY | _O_BINARY);
                        if (fd != -1) {
                            fPipe = _fdopen(fd, "wb");
                        } else {
                            CloseHandle(hWrite);
                        }
                    } else {
                        CloseHandle(hRead);
                        CloseHandle(hWrite);
                    }
                }
#else
                std::string ffCmd = "\"" + directorOptions.ffmpegBin + "\" -y -loglevel warning "
                                    "-f rawvideo -pix_fmt " + pixFmt + " -s " +
                                    std::to_string(width) + "x" + std::to_string(height) +
                                    " -r " + std::to_string(exportFps) +
                                    " -i - -c:v libx264 -preset fast -crf 14 -pix_fmt yuv420p \"" +
                                    videoOnlyPath + "\"";
                fPipe = popen(ffCmd.c_str(), "w");
#endif
                usingPipe = (fPipe != nullptr);
                logMsg(usingPipe ? "[SolEngine Export] ffmpeg pipe connected." : "[SolEngine Export] ffmpeg pipe failed, falling back to frames.raw.");
            }
            if (!usingPipe) {
                fRaw = std::fopen(rawPath.c_str(), "wb");
            }
            FILE* fWav = std::fopen(wavPath.c_str(), "wb");
            if ((fPipe || fRaw) && fWav) {
                uint8_t wavHdr[44]{
                    'R','I','F','F', 0,0,0,0, 'W','A','V','E',
                    'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
                    0x80,0xBB,0x00,0x00, // 48000 Hz
                    0x00,0xEE,0x02,0x00, // 48000 * 4 bytes/sec
                    4,0, 16,0,
                    'd','a','t','a', 0,0,0,0
                };
                std::fwrite(wavHdr, 1, 44, fWav);

                std::vector<float> audioFloat(samplesPerFrame * 2u);
                std::vector<int16_t> audioPcm(samplesPerFrame * 2u);
                uint64_t totalAudioBytes = 0;
                uint64_t frameCount = 0;

                std::fprintf(stderr, "[SolEngine Export] Streaming %ux%u @ %ufps (%s)...\n",
                             width, height, exportFps, usingPipe ? "direct ffmpeg pipe" : "frames.raw");
                std::fflush(stderr);

                // Write the warmup frame 0 if we already rendered it for pixel-format detection
                if (usingPipe && !rawFrame.empty()) {
                    std::fwrite(rawFrame.data(), 1, rawFrame.size(), fPipe);
                    ctx.foley.mixSamples(audioFloat.data(), samplesPerFrame, 48000u);
                    for (size_t s = 0; s < audioFloat.size(); ++s) {
                        float v = std::clamp(audioFloat[s], -1.0f, 1.0f);
                        audioPcm[s] = static_cast<int16_t>(std::lrint(v * 32767.0f));
                    }
                    std::fwrite(audioPcm.data(), sizeof(int16_t), audioPcm.size(), fWav);
                    totalAudioBytes += audioPcm.size() * sizeof(int16_t);
                    ++frameCount;
                }

                while (!glfwWindowShouldClose(window) && !ctx.directorState.finished) {
                    ctx.updateAndRender(fixedDt);
                    if (ctx.renderer.readbackCapturedNative(rawFrame, isBgra)) {
                        std::fwrite(rawFrame.data(), 1, rawFrame.size(), usingPipe ? fPipe : fRaw);
                    }
                    ctx.foley.mixSamples(audioFloat.data(), samplesPerFrame, 48000u);
                    for (size_t s = 0; s < audioFloat.size(); ++s) {
                        float v = std::clamp(audioFloat[s], -1.0f, 1.0f);
                        audioPcm[s] = static_cast<int16_t>(std::lrint(v * 32767.0f));
                    }
                    std::fwrite(audioPcm.data(), sizeof(int16_t), audioPcm.size(), fWav);
                    totalAudioBytes += audioPcm.size() * sizeof(int16_t);
                    ++frameCount;
                    if ((frameCount % 15) == 0) {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "[SolEngine Export] Rendered %.2fs (%llu frames)",
                                      static_cast<double>(frameCount) / static_cast<double>(exportFps),
                                      static_cast<unsigned long long>(frameCount));
                        logMsg(buf);
                    }
                }

                // Finalize WAV header sizes
                auto writeLe32 = [](FILE* fp, long offset, uint32_t val) {
                    uint8_t b[4]{
                        static_cast<uint8_t>(val & 0xFF),
                        static_cast<uint8_t>((val >> 8) & 0xFF),
                        static_cast<uint8_t>((val >> 16) & 0xFF),
                        static_cast<uint8_t>((val >> 24) & 0xFF)
                    };
                    std::fseek(fp, offset, SEEK_SET);
                    std::fwrite(b, 1, 4, fp);
                };
                uint32_t dataSz = static_cast<uint32_t>(std::min<uint64_t>(totalAudioBytes, 0xFFFFFF00ULL));
                writeLe32(fWav, 4, dataSz + 36u);
                writeLe32(fWav, 40, dataSz);
                std::fprintf(stderr, "[SolEngine Export] Finished %llu frames.\n",
                             static_cast<unsigned long long>(frameCount));
                std::fflush(stderr);
            }
            if (fPipe) {
#if defined(_WIN32)
                std::fclose(fPipe);
#else
                pclose(fPipe);
#endif
            }
            if (fRaw) std::fclose(fRaw);
            if (fWav) std::fclose(fWav);
            if (fLog) std::fclose(fLog);
            ctx.renderer.disableOffscreenCapture();
        }
    } else {
        while (!glfwWindowShouldClose(window)) {
            ctx.updateAndRender();
        }
    }

    if (!ctx.recordMovesPath.empty() && !ctx.recordedBeats.empty()) {
        std::ofstream out(ctx.recordMovesPath);
        if (out.is_open()) {
            std::string gSlug = ctx.board.activeGameDef().id;
            if (gSlug.empty()) gSlug = "klondike_draw_1";
            out << "{\n"
                << "  \"game_slug\": \"" << gSlug << "\",\n"
                << "  \"game_name\": \"" << ctx.board.activeGameDef().name << "\",\n"
                << "  \"seed\": " << ctx.board.seed() << ",\n"
                << "  \"voice_id\": \"aSt3B90XH7ZfdtFvSTFg\",\n"
                << "  \"model_id\": \"eleven_multilingual_v2\",\n"
                << "  \"beats\": [\n";
            for (size_t i = 0; i < ctx.recordedBeats.size(); ++i) {
                const auto& rb = ctx.recordedBeats[i];
                out << "    {\n";
                if (rb.action == DirectorAction::DrawStock) {
                    out << "      \"action\": \"draw_stock\",\n";
                } else {
                    out << "      \"action\": \"move\",\n"
                        << "      \"from\": " << rb.fromPile << ",\n"
                        << "      \"card_index\": " << rb.cardIndex << ",\n"
                        << "      \"to\": " << rb.toPile << ",\n";
                }
                out << "      \"text\": \"TODO: " << rb.caption << "\",\n"
                    << "      \"pad_sec\": 0.4\n"
                    << "    },\n";
            }
            out << "    {\n"
                << "      \"action\": \"finish_game\",\n"
                << "      \"text\": \"From here, let's kick the clockwork into four-times speed to watch the rest of the deal play out!\",\n"
                << "      \"pad_sec\": 0.6\n"
                << "    },\n"
                << "    {\n"
                << "      \"action\": \"reset_camera\",\n"
                << "      \"text\": \"And there you have it! Now it's your turn at the table—enjoy a round of " << ctx.board.activeGameDef().name << "!\",\n"
                << "      \"pad_sec\": 3.0\n"
                << "    }\n"
                << "  ]\n}\n";
            std::cout << "[SolEngine Recorder] Saved " << ctx.recordedBeats.size()
                      << " guided move(s) to " << ctx.recordMovesPath << "\n";
        }
    }

    if (!directorOptions.skipSessionRestore) {
        if (!ctx.board.isVictory()) {
            SessionManager::instance().saveSession(ctx.board);
        } else {
            SessionManager::instance().clearSession();
        }
        SettingsManager::instance().syncFromGame(ctx.board, ctx.foley);
        SettingsManager::instance().save();
    }

    ctx.cancelOddsAnalysis();
    ctx.cancelVideoThumb();
    ctx.renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    s_activeContext.reset();
    return 0;
#endif
}

bool loadFile(const std::string& filePath) {
    if (filePath.empty() || !s_activeContext) return false;
    s_activeContext->startAsyncFileDrop(filePath);
    return true;
}

} // namespace solengine

extern "C" {

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
#endif
bool solengine_load_file_from_web(const char* virtualPath) {
    if (!virtualPath) return false;
    return solengine::loadFile(virtualPath);
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
void solengine_set_search_query(const char* text) {
    if (!text || !solengine::s_activeContext) return;
    solengine::s_activeContext->gameBrowserDialog.setSearchQuery(text);
}

EMSCRIPTEN_KEEPALIVE
bool solengine_load_game_from_web(const char* gameSlug, uint32_t seed) {
    if (!gameSlug || !*gameSlug || !solengine::s_activeContext) return false;
    auto& ctx = *solengine::s_activeContext;
    auto& reg = solengine::GameRegistry::instance();
    reg.scanDirectory("/games");
    const solengine::SolitaireGameDef* gdef = reg.findById(gameSlug);
    if (!gdef) gdef = reg.findByName(gameSlug);
    solengine::SolitaireGameDef loadedDef;
    if (!gdef) {
        std::string path = std::string("/games/") + gameSlug + ".eats";
        if (reg.loadGameFile(path, loadedDef)) {
            gdef = &loadedDef;
        }
    }
    if (!gdef) return false;
    uint32_t effSeed = (seed > 0) ? seed : (gdef->demoSeed > 0 ? gdef->demoSeed : ctx.board.seed());
    ctx.autoPlayController.stop();
    ctx.board.newDeal(effSeed, *gdef);
    ctx.board.computeLayout(static_cast<float>(ctx.width), static_cast<float>(ctx.height));
    return true;
}

EMSCRIPTEN_KEEPALIVE
void solengine_on_web_resize(int cssW, int cssH, double dpr) {
    if (cssW > 0 && cssH > 0) {
        solengine::s_webPendingCssW = static_cast<uint32_t>(cssW);
        solengine::s_webPendingCssH = static_cast<uint32_t>(cssH);
        solengine::s_webPendingDpr = (dpr > 0.0) ? static_cast<float>(dpr) : 1.0f;
        solengine::s_webResizePending = true;
    }
}

EMSCRIPTEN_KEEPALIVE
void solengine_on_web_safe_area(double top, double right, double bottom, double left) {
    solengine::s_webPendingSafeTop = std::max(0.0f, static_cast<float>(top));
    solengine::s_webPendingSafeRight = std::max(0.0f, static_cast<float>(right));
    solengine::s_webPendingSafeBottom = std::max(0.0f, static_cast<float>(bottom));
    solengine::s_webPendingSafeLeft = std::max(0.0f, static_cast<float>(left));
    solengine::s_webSafeAreaPending = true;
}
#else
void solengine_set_search_query(const char* text) {
    if (!text || !solengine::s_activeContext) return;
    solengine::s_activeContext->gameBrowserDialog.setSearchQuery(text);
}
#endif

} // extern "C"
