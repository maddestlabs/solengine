#include "solengine/table_renderer.hpp"
#include "solengine/auto_play_controller.hpp"
#include "solengine/dynamic_light.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/classic_deck_symbols.hpp"
#include "solengine/procedural_background.hpp"
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
#include "eatsbits/ui/glyph/vector_font.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <random>

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
    BtnToolbarResize = 29
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
    if (flags & ToolbarItem_Hint) {
        sDefs.push_back({ BtnHint,         "[H]int",                  "?" });
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
        sDefs.push_back({ BtnVariant,      "[G] Select Game",         "G" });
    }
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

    if (anchor == MenuAnchor::BottomLeft) {
        layout.barX = margin;
        layout.barY = height - margin - layout.barH;
    } else if (anchor == MenuAnchor::BottomRight) {
        layout.barX = width - margin - layout.barW;
        layout.barY = height - margin - layout.barH;
    } else if (anchor == MenuAnchor::TopLeft) {
        layout.barX = margin;
        layout.barY = margin;
    } else if (anchor == MenuAnchor::TopRight) {
        layout.barX = width - margin - layout.barW;
        layout.barY = margin;
    }

    // Ergonometric clearance: avoid overlapping bottom stock/waste piles if present
    if ((board.variant() == GameVariant::KlondikeDraw1 || board.variant() == GameVariant::KlondikeDraw3) && !board.piles().empty()) {
        if (board.piles()[0].y > height * 0.5f) {
            const bool stockOnRight = (board.piles()[0].x > width * 0.5f);
            if (!isVertical) {
                if (anchor == MenuAnchor::BottomRight && stockOnRight) {
                    layout.barY = std::max(6.0f, board.piles()[0].y - layout.barH - 8.0f);
                } else if (anchor == MenuAnchor::BottomLeft && !stockOnRight) {
                    layout.barY = std::max(6.0f, board.piles()[0].y - layout.barH - 8.0f);
                }
            }
        }
    }

    layout.barX = std::clamp(layout.barX, 4.0f, std::max(4.0f, width - layout.barW - 4.0f));
    layout.barY = std::clamp(layout.barY, 4.0f, std::max(4.0f, height - layout.barH - 4.0f));

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
    const AutoPlayController& controller
) {
    std::vector<HudButtonRect> btns;
    if (!controller.isActive()) {
        return btns;
    }

    constexpr float btnW = 38.0f;
    constexpr float btnH = 32.0f;
    constexpr float gap = 6.0f;
    const float takeOverW = 90.0f;
    const float totalW = 5.0f * btnW + takeOverW + 5.0f * gap + 24.0f;
    const float barX = (width - totalW) * 0.5f;
    const float barY = height - 52.0f;

    auto makeBtn = [](HudButtonId id, float x, float y, float w, float h, const char* label, SvgIconId iconId) -> HudButtonRect {
        return HudButtonRect{id, x, y, w, h, label, label, iconId};
    };

    float curX = barX + 12.0f;
    float curY = barY + 5.0f;

    // 1. Restart Deal
    btns.push_back(makeBtn(BtnPlayRestart, curX, curY, btnW, btnH, "|<", SvgIconId::Restart));
    curX += btnW + gap;

    // 2. Step Backward (Rewind)
    btns.push_back(makeBtn(BtnStepBack, curX, curY, btnW, btnH, "<<", SvgIconId::StepBackward));
    curX += btnW + gap;

    // 3. Play / Pause
    SvgIconId playPauseIcon = controller.isPlaying() ? SvgIconId::Pause : SvgIconId::Play;
    const char* playPauseLbl = controller.isPlaying() ? "||" : ">";
    btns.push_back(makeBtn(BtnPlayPause, curX, curY, btnW, btnH, playPauseLbl, playPauseIcon));
    curX += btnW + gap;

    // 4. Step Forward
    btns.push_back(makeBtn(BtnStepFwd, curX, curY, btnW, btnH, ">>", SvgIconId::StepForward));
    curX += btnW + gap;

    // 5. Speed Toggle (1x, 2x, 4x)
    btns.push_back(makeBtn(BtnSpeedToggle, curX, curY, btnW, btnH, controller.speedLabel(), SvgIconId::None));
    curX += btnW + gap;

    // 6. Take Over
    btns.push_back(makeBtn(BtnTakeOver, curX, curY, takeOverW, btnH, "Take Over", SvgIconId::Close));

    return btns;
}

int hitTestTransportButtons(
    float mx,
    float my,
    float width,
    float height,
    const AutoPlayController& controller
) {
    if (!controller.isActive()) return BtnNone;
    auto btns = buildTransportButtons(width, height, controller);
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
        const float q3y = scy + uHatY * totalHalfW + vHatY * totalHalfH;

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

    if (c.enshadedGlow) {
        draw3DRoundedOutline(r, xf, outerUV, zSign, pal.accentR, pal.accentG, pal.accentB, 0.98f, 2.6f);
    } else if (wearLvl == CardWearLevel::Pristine) {
        draw3DRoundedOutline(r, xf, outerUV, zSign, 0.22f, 0.18f, 0.14f, 0.85f, 1.2f);
    }
}

void drawPhysicalDeckRim(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h, float radius,
    size_t cardCount,
    const eatsbits::ui::LightSource2D& light
) {
    if (cardCount < 2 || w <= 0.0f || h <= 0.0f) return;
    (void)light;
    const float thickness = std::clamp(static_cast<float>(cardCount) * 0.28f, 1.2f, 6.0f);
    // Bevel offset toward bottom-right (matching light direction and slight top-down camera angle)
    const float rimY = thickness * 0.85f;
    const float rimX = thickness * 0.30f;

    // 1. Cut paper cardstock rim base (warm ivory / core paper)
    r.drawRoundedRect(x + rimX, y + rimY, w, h, radius, 0.91f, 0.88f, 0.82f, 1.0f, 4);

    // 2. Bottom creasing shadow line where the deck meets the table
    r.drawRoundedRectOutline(x + rimX, y + rimY, w, h, radius, 0.28f, 0.25f, 0.20f, 0.45f, 1.0f, 4);

    // 3. Subtle micro-striation representing stacked card layers
    if (thickness >= 2.5f) {
        const float midY = y + rimY * 0.5f;
        const float midX = x + rimX * 0.5f;
        r.drawRoundedRectOutline(midX, midY, w, h, radius, 0.78f, 0.74f, 0.68f, 0.40f, 0.8f, 4);
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
    float prevCardY = 99999.0f
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
    const CardPhysicalWarp warp = computeCardPhysicalWarp(c.id, wearLvl);

    // Physical card camber foreshortening:
    // Curved cards project a slightly shorter chord on screen without altering the game grid
    const float effBaseW = (warp.bowX > 0.0f) ? (baseW * (1.0f - warp.bowX)) : baseW;
    const float effBaseH = (warp.bowY > 0.0f) ? (baseH * (1.0f - warp.bowY)) : baseH;
    const float baseXOffset = (baseW - effBaseW) * 0.5f;
    const float baseYOffset = (baseH - effBaseH) * 0.5f;

    const float liftScale = 1.0f + c.elevation * 0.0035f;
    const float flipCos = std::cos(c.flipAngle);
    const float scaleX = std::max(0.06f, std::abs(flipCos)) * liftScale;
    const float scaleY = liftScale;

    const float w = effBaseW * scaleX;
    const float h = effBaseH * scaleY;
    const float x = (baseX + baseXOffset) + (effBaseW - w) * 0.5f;
    const float y = (baseY + baseYOffset) + (effBaseH - h) * 0.5f;

    const float effRu = CardTextureCache::instance().effectiveRu();
    const float radius = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * w);

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
    ScopedCardRotation rotGuard(r, c.angle, x + w * 0.5f, y + h * 0.5f);

    // Dynamic drop shadow driven by physical light source & card camber depth:
    // When a card arches upward, the elevated center casts a broader, softer penumbra
    // while the contact ends remain grounded to the table.
    if (pass != CardRenderPass::BodyOnly) {
        const float shadowElevation = 2.5f + c.elevation + warp.liftZ;
        const float shadowOpacity = std::clamp(0.32f + (c.elevation + warp.liftZ * 0.4f) * 0.010f, 0.16f, 0.48f);
        const float prevScaledX = (std::abs(prevCardX) < 9000.0f) ? (prevCardX + (baseW - w) * 0.5f) : 99999.0f;
        const float prevScaledY = (std::abs(prevCardY) < 9000.0f) ? (prevCardY + (baseH - h) * 0.5f) : 99999.0f;

        if (warp.liftZ > 1.2f && c.elevation < 1.0f) {
            // Dual shadow for arched cards:
            // 1. Base contact shadow where card ends touch the table
            r.drawRectDropShadow(x, y, w, h, radius, 2.0f, light, 0.38f, prevScaledX, prevScaledY);
            // 2. Elevated soft penumbra where the center arches off the table
            r.drawRectDropShadow(x, y, w, h, radius, shadowElevation, light, shadowOpacity * 0.75f, prevScaledX, prevScaledY);
        } else {
            r.drawRectDropShadow(x, y, w, h, radius, shadowElevation, light, shadowOpacity, prevScaledX, prevScaledY);
        }
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
        r.drawTexturedRoundedRectGpu(x, y, w, h, radius, slot.u0, slot.v0, slot.u1, slot.v1, 1.0f, 1.0f, 1.0f, 1.0f, scratchDistort, condParam);

        if (c.enshadedGlow) {
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
            r.drawTexturedRoundedRect(x, y, w, h, radius, mipData, mipW, mipH, 1.0f);
        }

        if (c.enshadedGlow) {
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

    if (c.enshadedGlow) {
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
    const bool showMarkings = SettingsManager::instance().settings().showTableMarkings;
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
    const class AutoPlayController* autoPlay
) {
    const ThemePalette pal = getPalette(board.tableTheme());
    // Overhead table illumination: dynamic swaying light or centered static default
    const eatsbits::ui::LightSource2D light = dynamicLight ? *dynamicLight : eatsbits::ui::LightSource2D{width * 0.5f, height * 0.28f, 550.0f, 1.0f};

    if (r.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU && CardTextureCache::instance().isAtlasDirty()) {
        CardTextureCache::instance().buildGpuAtlas(r);
    }

    // Activate physical 3D scene illumination & natural light-source vignette for tabletop and cards
    r.setSceneLightingActive(true);

    // 1. Static Table Chrome (Recorded once into FBO cache and blitted via fast memcpy!)
    static TableThemeId s_cachedTheme = static_cast<TableThemeId>(-1);
    static GameVariant s_cachedVariant = GameVariant::KlondikeDraw1;
    static float s_cachedW = 0.0f;
    static float s_cachedH = 0.0f;
    static bool s_cachedMarkings = true;

    const bool showMarkings = SettingsManager::instance().settings().showTableMarkings;

    if (s_cachedTheme != board.tableTheme() ||
        s_cachedVariant != board.variant() ||
        s_cachedMarkings != showMarkings ||
        std::abs(s_cachedW - width) > 0.5f ||
        std::abs(s_cachedH - height) > 0.5f) {
        r.invalidateChromeCache();
        ProceduralBackgroundSystem::instance().invalidateCache();
        s_cachedTheme = board.tableTheme();
        s_cachedVariant = board.variant();
        s_cachedMarkings = showMarkings;
        s_cachedW = width;
        s_cachedH = height;
    }

    if (r.isChromeCacheValid()) {
        r.drawCachedChrome(1.0f);
    } else if (r.beginChromeRecording()) {
        drawStaticTableChrome(r, board, width, height, pal, light);
        r.endChromeRecording();
    } else {
        drawStaticTableChrome(r, board, width, height, pal, light);
    }

    // 2. Bottom-Center Live Telemetry Watermark (Seed, Score, Moves, Timer)
    if (showMarkings) {
        int mins = static_cast<int>(board.elapsedSeconds()) / 60;
        int secs = static_cast<int>(board.elapsedSeconds()) % 60;
        char botTelemetry[128];
        if (width >= 720.0f) {
            std::snprintf(botTelemetry, sizeof(botTelemetry), "SEED #%u    SCORE: %d    MOVES: %u    %02d:%02d",
                          board.seed(), board.score(), board.moveCount(), mins, secs);
        } else {
            std::snprintf(botTelemetry, sizeof(botTelemetry), "SCORE: %d    MOVES: %u    %02d:%02d",
                          board.score(), board.moveCount(), mins, secs);
        }
        const float teleLen = static_cast<float>(std::strlen(botTelemetry)) * 6.2f;
        const float teleX = std::max(12.0f, (width - teleLen) * 0.5f);
        eatsbits::ui::glyph::drawVectorText(r, botTelemetry, teleX, height - 16.0f, 11.5f,
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
    std::vector<AnimatingCardEntry> animatingCards;

    for (size_t pIdx = 0; pIdx < piles.size(); ++pIdx) {
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

        std::vector<size_t> restingIndices;
        bool pileHasMovingBelow = false;
        for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
            if (static_cast<int>(pIdx) == draggedPile &&
                draggedCardIdx >= 0 &&
                static_cast<int>(cIdx) >= draggedCardIdx) {
                continue;
            }
            if (board.isVictory() && p.type == PileType::Foundation) {
                uint8_t fIdx = p.index;
                float tLaunch = (12.0f - static_cast<float>(cIdx)) * 0.16f + fIdx * 0.06f;
                if (board.victorySeconds() >= tLaunch) {
                    continue; // Card launched into 3D victory cascade
                }
            }
            const Card& c = p.cards[cIdx];
            if (isCardMoving(c) || pileHasMovingBelow) {
                pileHasMovingBelow = true;
                animatingCards.push_back(AnimatingCardEntry{pIdx, cIdx, &c});
            } else if (cIdx >= startIdx) {
                restingIndices.push_back(cIdx);
            }
        }

        // Render table drop shadows under the entire resting pile first (using non-accumulating
        // transmittance compensation against the preceding card in the pile), then card bodies bottom-to-top
        const bool isTightlyStacked = (p.type == PileType::Stock || p.type == PileType::Foundation || p.type == PileType::FreeCell);
        const size_t stackCount = restingIndices.size();

        for (size_t i = 0; i < restingIndices.size(); ++i) {
            if (i > 0 && isTightlyStacked) {
                continue; // Tightly stacked deck/foundation piles emit unified table shadows
            }
            const Card& c = p.cards[restingIndices[i]];
            const float prevX = (i > 0) ? p.cards[restingIndices[i - 1]].x : 99999.0f;
            const float prevY = (i > 0) ? p.cards[restingIndices[i - 1]].y : 99999.0f;

            if (isTightlyStacked && stackCount > 1) {
                // Dual-component stack shadow:
                // 1. Crisp, dark grounding contact shadow at base elevation
                drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light, CardRenderPass::ShadowOnly, prevX, prevY);
                // 2. Elevated soft penumbra shadow matching full physical deck height
                const float stackElevation = std::min(18.0f, static_cast<float>(stackCount) * 0.38f);
                Card tallCard = c;
                tallCard.elevation = c.elevation + stackElevation;
                drawProceduralCard(r, tallCard, tallCard.x, tallCard.y, cw, ch, pal, light, CardRenderPass::ShadowOnly, prevX, prevY);
            } else {
                drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light, CardRenderPass::ShadowOnly, prevX, prevY);
            }
        }

        // Draw physical deck edge rim for multi-card stacks before rendering top card body
        if (isTightlyStacked && stackCount >= 2 && !restingIndices.empty()) {
            const Card& topCard = p.cards[restingIndices.back()];
            const float effRu = CardTextureCache::instance().effectiveRu();
            const float cardRadius = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * cw);
            drawPhysicalDeckRim(r, topCard.x, topCard.y, cw, ch, cardRadius, stackCount, light);
        }

        for (size_t i = 0; i < restingIndices.size(); ++i) {
            const Card& c = p.cards[restingIndices[i]];
            if (i > 0 && (p.type == PileType::Tableau || p.type == PileType::Waste)) {
                // Inter-card contact shadow: card i rests physically on top of card i-1,
                // casting a tight, crisp contact shadow (gap ~0.3mm) across card i-1's face.
                const float effRu = CardTextureCache::instance().effectiveRu();
                const float cardRadius = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * cw);
                r.drawRectDropShadow(c.x, c.y, cw, ch, cardRadius, 1.2f, light, 0.38f);
            }
            drawProceduralCard(r, c, c.x, c.y, cw, ch, pal, light, CardRenderPass::BodyOnly);
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
        // Render table shadows for animating cards underneath their bodies (grouped per pile without alpha accumulation)
        for (size_t i = 0; i < animatingCards.size(); ++i) {
            const auto& entry = animatingCards[i];
            const bool samePile = (i > 0 &&
                                   animatingCards[i - 1].pileIdx == entry.pileIdx &&
                                   animatingCards[i - 1].card->flipAngle < 0.005f &&
                                   entry.card->flipAngle < 0.005f &&
                                   std::abs(animatingCards[i - 1].card->elevation - entry.card->elevation) < 0.5f);
            const float prevX = samePile ? animatingCards[i - 1].card->x : 99999.0f;
            const float prevY = samePile ? animatingCards[i - 1].card->y : 99999.0f;
            drawProceduralCard(r, *entry.card, entry.card->x, entry.card->y, cw, ch, pal, light, CardRenderPass::ShadowOnly, prevX, prevY);
        }
        for (size_t i = 0; i < animatingCards.size(); ++i) {
            const auto& entry = animatingCards[i];
            drawProceduralCard(r, *entry.card, entry.card->x, entry.card->y, cw, ch, pal, light, CardRenderPass::BodyOnly);
        }
    }

    // 5. Dragged Card Sub-Stack (Elevated on top of all piles and animations; unified non-accumulating shadow underneath stack)
    if (draggedPile >= 0 && draggedPile < static_cast<int>(piles.size()) && draggedCardIdx >= 0) {
        const Pile& p = piles[static_cast<size_t>(draggedPile)];
        const size_t firstDrag = static_cast<size_t>(draggedCardIdx);
        for (size_t cIdx = firstDrag; cIdx < p.cards.size(); ++cIdx) {
            Card c = p.cards[cIdx];
            c.elevation = 14.0f;
            c.angle += std::clamp(dragOffsetX * 0.0006f, -0.045f, 0.045f);
            const float prevX = (cIdx > firstDrag) ? (p.cards[cIdx - 1].x + dragOffsetX) : 99999.0f;
            const float prevY = (cIdx > firstDrag) ? (p.cards[cIdx - 1].y + dragOffsetY) : 99999.0f;
            drawProceduralCard(r, c, c.x + dragOffsetX, c.y + dragOffsetY, cw, ch, pal, light, CardRenderPass::ShadowOnly, prevX, prevY);
        }
        for (size_t cIdx = firstDrag; cIdx < p.cards.size(); ++cIdx) {
            Card c = p.cards[cIdx];
            c.elevation = 14.0f;
            c.angle += std::clamp(dragOffsetX * 0.0006f, -0.045f, 0.045f);
            if (cIdx > firstDrag) {
                const float effRu = CardTextureCache::instance().effectiveRu();
                const float cardRadius = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * cw);
                r.drawRectDropShadow(c.x + dragOffsetX, c.y + dragOffsetY, cw, ch, cardRadius, 1.2f, light, 0.38f);
            }
            drawProceduralCard(r, c, c.x + dragOffsetX, c.y + dragOffsetY, cw, ch, pal, light, CardRenderPass::BodyOnly);
        }
    }

    // 6. Hardwood 3.0 3D Victory Cascade & Banner Overlay
    if (board.isVictory()) {
        struct VictoryAirborneCard {
            Card card;
            Card3DTransform xf;
        };

        std::vector<VictoryAirborneCard> airborne;
        airborne.reserve(52);

        const float floorY = height - ch * 0.75f;
        const float vTime = board.victorySeconds();

        for (const auto& p : board.piles()) {
            if (p.type != PileType::Foundation) continue;
            uint8_t fIdx = p.index;

            for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
                float tLaunch = (12.0f - static_cast<float>(cIdx)) * 0.16f + fIdx * 0.06f;
                if (vTime < tLaunch) continue;

                float dt = vTime - tLaunch;
                const Card& c = p.cards[cIdx];
                uint32_t cardId = static_cast<uint32_t>(fIdx * 13 + cIdx);

                VictoryAirborneCard vac{};
                vac.card = c;
                vac.card.faceUp = true;
                vac.card.flipAngle = 0.0f;

                // Horizontal motion with edge bouncing
                float startX = p.x + cw * 0.5f;
                float startY = p.y + ch * 0.5f;
                float vx = ((static_cast<float>(fIdx) - 1.5f) * 170.0f) + std::sin(static_cast<float>(cardId) * 2.3f) * 85.0f;
                float rawX = startX + vx * dt;

                const float minX = cw * 0.6f;
                const float maxX = width - cw * 0.6f;
                const float spanX = std::max(10.0f, maxX - minX);
                float normX = std::fmod(std::abs(rawX - minX), spanX * 2.0f);
                if (normX > spanX) normX = spanX * 2.0f - normX;
                float posX = minX + normX;

                // Vertical bouncing physics
                const float bouncePeriod = 1.45f;
                float cycle = std::fmod(dt, bouncePeriod) / bouncePeriod;
                int bounceNum = static_cast<int>(dt / bouncePeriod);
                float decay = std::max(0.25f, 1.0f - static_cast<float>(bounceNum) * 0.12f);
                float apexH = (decay * 340.0f) + 60.0f;
                float posY = floorY - 4.0f * apexH * cycle * (1.0f - cycle);
                if (dt < 0.35f) {
                    float launchT = dt / 0.35f;
                    posY = startY + (posY - startY) * launchT;
                }

                // 3D elevation and tumbling rotations
                float cz = 45.0f + std::sin(dt * 3.6f + static_cast<float>(cardId)) * 35.0f + (1.0f - cycle) * 30.0f;

                Card3DTransform xf{};
                xf.baseW = cw;
                xf.baseH = ch;
                xf.cx = posX;
                xf.cy = posY;
                xf.cz = cz;
                xf.yaw = dt * (3.5f + static_cast<float>(cardId % 4) * 0.6f);
                xf.pitch = 0.22f + std::sin(dt * 4.0f + static_cast<float>(cardId) * 0.6f) * 0.40f;
                xf.roll = std::cos(dt * 3.0f + static_cast<float>(cardId) * 0.8f) * 0.30f;

                vac.xf = xf;
                airborne.push_back(vac);
            }
        }

        // Depth-sort airborne cards from back to front (lowest cz to highest cz)
        std::stable_sort(airborne.begin(), airborne.end(), [](const VictoryAirborneCard& a, const VictoryAirborneCard& b) noexcept {
            return a.xf.cz < b.xf.cz - 0.01f;
        });

        for (const auto& vac : airborne) {
            render3DCard(r, vac.card, vac.xf, pal, light, CardRenderPass::ShadowOnly);
        }
        for (const auto& vac : airborne) {
            render3DCard(r, vac.card, vac.xf, pal, light, CardRenderPass::BodyOnly);
        }

        const float bw = 460.0f;
        const float bh = 96.0f;
        const float bx = (width - bw) * 0.5f;
        const float by = (height - bh) * 0.5f;
        r.drawRectDropShadow(bx, by, bw, bh, 10.0f, 16.0f, light, 0.60f);
        r.drawRoundedRectGradient(bx, by, bw, bh, 10.0f,
                                  pal.woodR0, pal.woodG0, pal.woodB0,
                                  pal.woodR1, pal.woodG1, pal.woodB1, 0.98f, 4);
        r.drawRoundedRectOutline(bx, by, bw, bh, 10.0f, pal.accentR, pal.accentG, pal.accentB, 0.95f, 2.2f, 4);
        eatsbits::ui::glyph::drawVectorText(r, "THE VEIL IS PARTED  -  VICTORY!", bx + 48.0f, by + 24.0f, 18.0f,
                                            pal.accentR, pal.accentG, pal.accentB, 1.0f);
        eatsbits::ui::glyph::drawVectorText(r, "PRESS [N] OR CLICK NEW DEAL FOR HANDS ANEW", bx + 64.0f, by + 58.0f, 12.5f,
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

    // Disable scene lighting for HUD menu, anchor medallion, and modal dialogs so UI stays at 100% crisp contrast
    r.setSceneLightingActive(false);

    // 8. Always-on-Top Floating Clockwork Medallion Bar
    const auto layout = computeClockworkBarLayout(width, height, board, menuAnchor, menuFlowVertical);
    static float sAnimTime = 0.0f;
    sAnimTime += 0.016f;

    // Medallions
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
            const float gcx = gb.x + gb.w * 0.5f;
            const float gcy = gb.y + gb.h * 0.5f;
            r.drawCircle(gcx, gcy, gb.w * 0.5f, 0.94f, 0.80f, 0.36f, 0.18f, 20);
            r.drawCircleOutline(gcx, gcy, gb.w * 0.5f, 0.95f, 0.52f, 0.12f, 0.85f, 1.8f, 20);
        }
    }

    // 9. Floating Auto-Play Transport Bar
    if (autoPlay && autoPlay->isActive()) {
        auto tBtns = buildTransportButtons(width, height, *autoPlay);
        drawTransportBar(r, width, height, *autoPlay, tBtns, hoveredButtonId, pal, light);
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
    constexpr int kCount = 5;
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
    float dragSoundAccumulator = 0.0f;
    double lastDragMouseX = 0.0;
    double lastDragMouseY = 0.0;

    bool prevLeftDown = false;
    bool prevRightDown = false;
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
        TableThemeId targetTheme{TableThemeId::EmeraldLawn};
        std::string targetThemeSlug;
        bool themeReady{false};
        uint32_t solvableSeed{0};
        bool solvableSeedReady{false};
        SolitaireGameDef solvableDef;
        std::string dialogTitle{"PREPARING PROCEDURAL ASSETS"};
        std::string dialogInitialStatus{"Initializing..."};
        bool dialogCancellable{true};
        double startTime{0.0};
        double debounceSeconds{0.12}; // 120ms debounce threshold to prevent micro-flashes on fast cache hits
#if !defined(__EMSCRIPTEN__)
        std::thread workerThread;
#endif

        void joinWorker() {
#if !defined(__EMSCRIPTEN__)
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
            solvableSeedReady = false;
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

        if (def.filePath.empty()) {
            ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
            board.setTableTheme(TableThemeId::PastelTan);
            board.setTableThemeSlug(def.id);
            lightController.applyThemeDefault(TableThemeId::PastelTan);
            SettingsManager::instance().settings().tableTheme = TableThemeId::PastelTan;
            SettingsManager::instance().settings().tableThemeSlug = def.id;
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
#if defined(__EMSCRIPTEN__)
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

    void startAsyncConditioning(CardWearLevel wear, CardStock stock) {
        asyncManager.stopWorker();

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.stagingDeckReady = false;
        asyncManager.stagingDeck = CardTextureCache::StagingDeck{};
        asyncManager.stagingDeck.hasNewBase = false;
        asyncManager.stagingDeck.theme = CardTextureCache::instance().currentDeckTheme();
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

#if defined(__EMSCRIPTEN__)
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

    void startAsyncDeckTheme(DeckThemeId theme, CardWearLevel wear, CardStock stock) {
        asyncManager.stopWorker();

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

#if defined(__EMSCRIPTEN__)
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
            } else if (themeId == "solenshade") {
                deckTheme = DeckThemeId::SolEnshade;
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

#if defined(__EMSCRIPTEN__)
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

    void startAsyncSolvableDeal(const SolitaireGameDef& def, uint32_t startSeed = 0) {
        asyncManager.stopWorker();
        asyncManager.resetState();

        if (startSeed == 0) {
            std::random_device rd;
            auto nowVal = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            startSeed = (static_cast<uint32_t>(rd()) ^ static_cast<uint32_t>(nowVal)) & 0x7FFFFFFFu;
            if (startSeed == 0) startSeed = 2026u;
        }

        asyncManager.cancelRequested.store(false);
        asyncManager.progress.store(0.0f);
        asyncManager.solvableSeedReady = false;
        asyncManager.solvableSeed = startSeed;
        asyncManager.solvableDef = def;
        asyncManager.opType = AsyncOp::SolvableDeal;
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

#if !defined(__EMSCRIPTEN__)
        asyncManager.workerThread = std::thread([this, def, startSeed]() {
            SolitaireSolver solver;
            uint32_t currentSeed = startSeed;
            const uint32_t maxAttempts = 80;
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

                SolveResult res = solver.solve(testBoard, 16000, 350.0f, &asyncManager.cancelRequested);
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
                asyncManager.solvableSeed = startSeed;
                asyncManager.solvableSeedReady = true;
            }
            asyncManager.isBusy.store(false);
        });
#else
        asyncManager.solvableSeed = startSeed;
        asyncManager.solvableSeedReady = true;
        asyncManager.isBusy.store(false);
#endif
    }

    InteractiveContext(SolitaireBoard& b, AudioFoleyEngine& f, uint32_t w, uint32_t h)
        : board(b), foley(f), width(w), height(h) {
        SettingsManager::instance().load();
        // Defer texture loading and conditioning to async startup flow to keep cold boot instantaneous
        SettingsManager::instance().applyToGame(board, foley, false);
        lightController.setPreset(SettingsManager::instance().settings().lightingPreset);
        menuAnchor = SettingsManager::instance().settings().menuAnchor;
        menuFlowVertical = SettingsManager::instance().settings().menuFlowVertical;

        if (SessionManager::instance().hasSavedSession()) {
            SessionManager::instance().loadSession(board);
        }

        GameRegistry::instance().initialize();

        const auto& startupSettings = SettingsManager::instance().settings();
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
            board.newDeal(s, board.variant());
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
                SettingsManager::instance().save();
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
            const auto& s = SettingsManager::instance().settings();
            menuAnchor = s.menuAnchor;
            menuFlowVertical = s.menuFlowVertical;
            renderer.setAntiAliasingMode(4);
            lightController.setPreset(s.lightingPreset);

            bool variantChanged = (s.variant != board.variant());
            if (variantChanged) {
                board.newDeal(s.lastSeed, s.variant);
                board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                SessionManager::instance().clearSession();
                foley.triggerSound(FoleySound::Deal, 0.85f);
            }

            board.setCardDeviation(s.cardDeviation);
            board.setLayoutMode(s.layoutMode);
            board.setStockPosition(s.stockPosition);
            foley.setMasterVolume(s.masterVolume);
            foley.setMuted(s.muted);
            foley.setShufflePreference(s.shuffleStyle);
            CardTextureCache::instance().setCornerStyle(s.cardCorners);

            auto& cache = CardTextureCache::instance();
            bool themeChanged = (s.tableTheme != board.tableTheme() || s.tableThemeSlug != board.tableThemeSlug());
            bool tintChanged = (s.tableTint != ProceduralBackgroundSystem::instance().tableTint());
            if (tintChanged) {
                ProceduralBackgroundSystem::instance().setTableTint(s.tableTint);
                ProceduralBackgroundSystem::instance().invalidateCache();
            }
            bool deckChanged = (s.deckTheme != cache.currentDeckTheme());
            bool wearChanged = (s.cardWear != cache.cardWearLevel());
            bool stockChanged = (s.cardStock != cache.cardStock());
            bool cardBackChanged = (s.cardBackId != cache.currentCardBackId());

            if (deckChanged) {
                if (themeChanged) {
                    ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
                    board.setTableTheme(s.tableTheme);
                    board.setTableThemeSlug(s.tableThemeSlug);
                    lightController.applyThemeDefault(s.tableTheme);
                    ProceduralBackgroundSystem::instance().invalidateCache();
                }
                startAsyncDeckTheme(s.deckTheme, s.cardWear, s.cardStock);
            } else if (wearChanged || stockChanged) {
                if (themeChanged) {
                    ProceduralBackgroundSystem::instance().clearCustomEatsTheme();
                    board.setTableTheme(s.tableTheme);
                    board.setTableThemeSlug(s.tableThemeSlug);
                    lightController.applyThemeDefault(s.tableTheme);
                    ProceduralBackgroundSystem::instance().invalidateCache();
                }
                startAsyncConditioning(s.cardWear, s.cardStock);
            } else {
                if (cardBackChanged) {
                    cache.setActiveCardBack(s.cardBackId);
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
        };
    }

    void checkStartupSettingsLoad() {
        const auto& s = SettingsManager::instance().settings();
        auto& cache = CardTextureCache::instance();

        bool deckChanged = (s.deckTheme != cache.currentDeckTheme());
        bool wearChanged = (s.cardWear != cache.cardWearLevel());
        bool stockChanged = (s.cardStock != cache.cardStock());
        bool cardBackChanged = (s.cardBackId != cache.currentCardBackId());

        if (deckChanged) {
            startAsyncDeckTheme(s.deckTheme, s.cardWear, s.cardStock);
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
        switch (btnId) {
            case BtnNewDeal:
                autoPlayController.stop();
                autoCascadeActive = false;
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
                    board.newDeal(nextSeed, board.activeGameDef());
                    SettingsManager::instance().settings().lastSeed = nextSeed;
                    SettingsManager::instance().save();
                    SessionManager::instance().clearSession();
                }
                foley.triggerSound(FoleySound::Deal, 0.9f);
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
                        SessionManager::instance().clearSession();
                        foley.triggerSound(FoleySound::Deal, 0.85f);
                    }
                }
                break;
            }
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
                board.newDeal(nextSeed, board.variant());
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
                uint8_t nextDeck = (static_cast<uint8_t>(CardTextureCache::instance().currentDeckTheme()) + 1u) %
                                   static_cast<uint8_t>(DeckThemeId::Count);
                if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::Custom) {
                    std::error_code ec;
                    if (!std::filesystem::exists("decks/custom.eats", ec) &&
                        !std::filesystem::exists("decks/custom.png", ec)) {
                        nextDeck = static_cast<uint8_t>(DeckThemeId::SolEnshade);
                    }
                }
#if defined(__EMSCRIPTEN__)
                if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::BaroqueNoir) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/baroque_arches.eats", ec) ||
                                  std::filesystem::exists("/decks/baroque_arches.eats", ec) ||
                                  std::filesystem::exists("decks/baroque_noir.eats", ec) ||
                                  std::filesystem::exists("/decks/baroque_noir.eats", ec) ||
                                  std::filesystem::exists("/user/baroque_arches.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadBaroqueDeck) {
                                window.loadBaroqueDeck();
                            } else if (window.loadDeckByName) {
                                window.loadDeckByName('baroque_arches');
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::BaroqueLight) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/baroque_light.eats", ec) ||
                                  std::filesystem::exists("/decks/baroque_light.eats", ec) ||
                                  std::filesystem::exists("decks/baroque_alabaster.eats", ec) ||
                                  std::filesystem::exists("/decks/baroque_alabaster.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadDeckByName) {
                                window.loadDeckByName('baroque_light');
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::BaroqueDark) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/baroque_dark.eats", ec) ||
                                  std::filesystem::exists("/decks/baroque_dark.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadDeckByName) {
                                window.loadDeckByName('baroque_dark');
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::BaroqueCellShaded) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/baroque_cell_shaded.eats", ec) ||
                                  std::filesystem::exists("/decks/baroque_cell_shaded.eats", ec) ||
                                  std::filesystem::exists("decks/imperial_baroque.eats", ec) ||
                                  std::filesystem::exists("/decks/imperial_baroque.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadDeckByName) {
                                window.loadDeckByName('baroque_cell_shaded');
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::SteampunkAi) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/steampunk.eats", ec) ||
                                  std::filesystem::exists("/decks/steampunk.eats", ec) ||
                                  std::filesystem::exists("/user/steampunk.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadSteampunkDeck) {
                                window.loadSteampunkDeck();
                            } else if (window.loadDeckByName) {
                                window.loadDeckByName('steampunk');
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::Anime) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/anime.eats", ec) ||
                                  std::filesystem::exists("/decks/anime.eats", ec) ||
                                  std::filesystem::exists("/user/anime.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadAnimeDeck) {
                                window.loadAnimeDeck();
                            } else if (window.loadDeckByName) {
                                window.loadDeckByName('anime');
                            }
                        });
                        break;
                    }
                } else if (static_cast<DeckThemeId>(nextDeck) == DeckThemeId::Cyberpunk) {
                    std::error_code ec;
                    bool exists = std::filesystem::exists("decks/cyberpunk.eats", ec) ||
                                  std::filesystem::exists("/decks/cyberpunk.eats", ec) ||
                                  std::filesystem::exists("/user/cyberpunk.eats", ec);
                    if (!exists) {
                        EM_ASM({
                            if (window.loadCyberpunkDeck) {
                                window.loadCyberpunkDeck();
                            } else if (window.loadDeckByName) {
                                window.loadDeckByName('cyberpunk');
                            }
                        });
                        break;
                    }
                }
#endif
                startAsyncDeckTheme(static_cast<DeckThemeId>(nextDeck),
                                    CardTextureCache::instance().cardWearLevel(),
                                    CardTextureCache::instance().cardStock());
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
            case BtnHint:
                if (board.findBestHint().valid) {
                    foley.triggerSound(FoleySound::SpectreWhisper, 0.85f);
                } else {
                    foley.triggerSound(FoleySound::Flip, 0.5f);
                }
                break;
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
                settingsDialog.open();
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
                settingsDialog.open();
                foley.triggerSound(FoleySound::Flip, 0.8f);
                break;
            default:
                break;
        }
    }

    void updateAndRender() {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        auto screenPan = [&](float x) noexcept -> float {
            return (width > 0) ? std::clamp(((x / static_cast<float>(width)) * 2.0f - 1.0f) * 0.30f, -0.30f, 0.30f) : 0.0f;
        };

        if (asyncManager.opType != AsyncOp::None || asyncManager.isBusy.load() || loadingDialog.isOpen()) {
            double elapsed = now - asyncManager.startTime;
            if (asyncManager.isBusy.load() && elapsed >= asyncManager.debounceSeconds && !loadingDialog.isOpen()) {
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
                        SettingsManager::instance().settings().cardWear = CardTextureCache::instance().cardWearLevel();
                        SettingsManager::instance().settings().cardStock = CardTextureCache::instance().cardStock();
                        SettingsManager::instance().save();
                    }
                } else if (asyncManager.opType == AsyncOp::SolvableDeal && asyncManager.solvableSeedReady) {
                    autoCascadeActive = false;
                    autoPlayController.stop();
                    board.newDeal(asyncManager.solvableSeed, asyncManager.solvableDef);
                    board.computeLayout(static_cast<float>(width), static_cast<float>(height));
                    SettingsManager::instance().settings().lastSeed = asyncManager.solvableSeed;
                    SettingsManager::instance().settings().variant = asyncManager.solvableDef.nativeVariant;
                    SettingsManager::instance().save();
                    SessionManager::instance().clearSession();
                    if (loadingDialog.isOpen()) {
                        loadingDialog.close();
                        foley.triggerSound(FoleySound::Shuffle, 0.90f);
                    } else {
                        foley.triggerSound(FoleySound::Deal, 0.85f);
                    }
                } else if (asyncManager.opType == AsyncOp::TableTheme && asyncManager.themeReady) {
                    board.setTableTheme(asyncManager.targetTheme);
                    board.setTableThemeSlug(asyncManager.targetThemeSlug);
                    lightController.applyThemeDefault(asyncManager.targetTheme);
                    SettingsManager::instance().settings().tableTheme = asyncManager.targetTheme;
                    SettingsManager::instance().settings().tableThemeSlug = asyncManager.targetThemeSlug;
                    SettingsManager::instance().save();
                    if (loadingDialog.isOpen()) {
                        loadingDialog.close();
                        foley.triggerSound(FoleySound::Shuffle, 0.85f);
                    } else {
                        foley.triggerSound(FoleySound::Flip, 0.80f);
                    }
                } else if (asyncManager.stagingDeckReady || asyncManager.customBgReady || asyncManager.customAudioReady) {
                    if (asyncManager.stagingDeckReady) {
                        CardTextureCache::instance().commitStagedDeck(std::move(asyncManager.stagingDeck));
                        CardTextureCache::instance().setActiveCardBack(SettingsManager::instance().settings().cardBackId);
                        if (renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
                            CardTextureCache::instance().buildGpuAtlas(renderer);
                        }
                        SettingsManager::instance().settings().deckTheme = CardTextureCache::instance().currentDeckTheme();
                        SettingsManager::instance().settings().cardWear = CardTextureCache::instance().cardWearLevel();
                        SettingsManager::instance().settings().cardStock = CardTextureCache::instance().cardStock();
                        SettingsManager::instance().save();
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
                    SettingsManager::instance().settings().deckTheme = CardTextureCache::instance().currentDeckTheme();
                    SettingsManager::instance().settings().cardWear = CardTextureCache::instance().cardWearLevel();
                    SettingsManager::instance().settings().cardStock = CardTextureCache::instance().cardStock();
                    SettingsManager::instance().save();
                }

                asyncManager.resetState();
            }
        }

#if defined(__EMSCRIPTEN__)
        double cssW = EM_ASM_DOUBLE({
            var container = document.getElementById('canvas-container');
            return (container && container.clientWidth > 0) ? container.clientWidth : (window.innerWidth || 1280);
        });
        double cssH = EM_ASM_DOUBLE({
            var container = document.getElementById('canvas-container');
            return (container && container.clientHeight > 0) ? container.clientHeight : (window.innerHeight || 720);
        });
        double dpr = EM_ASM_DOUBLE({
            return window.devicePixelRatio || 1.0;
        });
        if (dpr <= 0.0) dpr = 1.0;

        int targetW = static_cast<int>(std::round(cssW));
        int targetH = static_cast<int>(std::round(cssH));
        if (targetW < 320) targetW = 320;
        if (targetH < 240) targetH = 240;

        int targetFbW = static_cast<int>(std::round(static_cast<float>(targetW) * static_cast<float>(dpr)));
        int targetFbH = static_cast<int>(std::round(static_cast<float>(targetH) * static_cast<float>(dpr)));

        if (static_cast<uint32_t>(targetW) != width ||
            static_cast<uint32_t>(targetH) != height ||
            static_cast<uint32_t>(targetFbW) != fbWidth ||
            static_cast<uint32_t>(targetFbH) != fbHeight ||
            std::abs(currentDpr - static_cast<float>(dpr)) > 0.001f) {

            width = static_cast<uint32_t>(targetW);
            height = static_cast<uint32_t>(targetH);
            fbWidth = static_cast<uint32_t>(targetFbW);
            fbHeight = static_cast<uint32_t>(targetFbH);
            currentDpr = static_cast<float>(dpr);

            EM_ASM({
                var canvas = document.getElementById('canvas');
                if (canvas) {
                    canvas.width = $0;
                    canvas.height = $1;
                    canvas.style.width = $2 + 'px';
                    canvas.style.height = $3 + 'px';
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
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        }
#else
        int winW = 0, winH = 0;
        glfwGetWindowSize(window, &winW, &winH);
        if (winW > 0 && winH > 0 && (static_cast<uint32_t>(winW) != width || static_cast<uint32_t>(winH) != height)) {
            width = static_cast<uint32_t>(winW);
            height = static_cast<uint32_t>(winH);
            renderer.resize(width, height);
            board.computeLayout(static_cast<float>(width), static_cast<float>(height));
        }
#endif

        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        const float fmx = static_cast<float>(mx);
        const float fmy = static_cast<float>(my);

        int hoveredBtn = hitTestHudButtons(fmx, fmy, static_cast<float>(width), static_cast<float>(height), board, menuAnchor, menuFlowVertical);
        if (autoPlayController.isActive()) {
            int tBtn = hitTestTransportButtons(fmx, fmy, static_cast<float>(width), static_cast<float>(height), autoPlayController);
            if (tBtn != BtnNone) {
                hoveredBtn = tBtn;
            }
        }

        const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if (loadingDialog.isOpen() || asyncManager.isBusy.load()) {
            if (loadingDialog.isOpen()) {
                if (leftDown && !prevLeftDown) {
                    loadingDialog.handleMouseButton(0, 1, fmx, fmy);
                }
            }
        } else if (seedDialog.isOpen()) {
            if (leftDown && !prevLeftDown) {
                seedDialog.handleMouseButton(0, 1, fmx, fmy, board, foley, window);
            } else if (leftDown) {
                seedDialog.handleMouseMove(fmx, fmy);
            } else if (!leftDown && prevLeftDown) {
                seedDialog.handleMouseButton(0, 0, fmx, fmy, board, foley, window);
            }
        } else if (gameBrowserDialog.isOpen()) {
            if (leftDown && !prevLeftDown) {
                gameBrowserDialog.handleMouseButton(0, 1, fmx, fmy, board, foley);
            } else if (leftDown) {
                gameBrowserDialog.handleMouseMove(fmx, fmy);
            } else if (!leftDown && prevLeftDown) {
                gameBrowserDialog.handleMouseButton(0, 0, fmx, fmy, board, foley);
            }
        } else if (settingsDialog.isOpen()) {
            if (leftDown && !prevLeftDown) {
                settingsDialog.handleMouseButton(0, 1, fmx, fmy, board, foley);
            } else if (leftDown) {
                settingsDialog.handleMouseMove(fmx, fmy, board, foley);
            } else if (!leftDown && prevLeftDown) {
                settingsDialog.handleMouseButton(0, 0, fmx, fmy, board, foley);
            }
        } else if (playMenuOpen) {
            if (leftDown && !prevLeftDown) {
                constexpr float menuW = 230.0f;
                constexpr float itemH = 30.0f;
                constexpr float headerH = 26.0f;
                constexpr float pad = 8.0f;
                constexpr int kCount = 5;
                constexpr float menuH = headerH + static_cast<float>(kCount) * itemH + pad * 2.0f;
                float menuX = playMenuAnchorX;
                float menuY = (playMenuAnchorY > static_cast<float>(height) * 0.5f) ? (playMenuAnchorY - menuH - 8.0f) : (playMenuAnchorY + 44.0f);
                menuX = std::clamp(menuX, 8.0f, static_cast<float>(width) - menuW - 8.0f);
                menuY = std::clamp(menuY, 8.0f, static_cast<float>(height) - menuH - 8.0f);

                if (fmx >= menuX && fmx <= menuX + menuW && fmy >= menuY && fmy <= menuY + menuH) {
                    float curY = menuY + headerH + pad * 0.5f;
                    for (int i = 0; i < kCount; ++i) {
                        if (fmx >= menuX + pad && fmx <= menuX + menuW - pad && fmy >= curY && fmy <= curY + itemH) {
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
                            } else if (i == 4) { // Game Library...
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

                if (fmx >= menuX && fmx <= menuX + menuW && fmy >= menuY && fmy <= menuY + menuH) {
                    float scaleY = menuY + headerH + 6.0f;
                    const float pillW = (menuW - pad * 2.0f - 8.0f) / 3.0f;
                    const float pillH = 22.0f;
                    for (int i = 0; i < 3; ++i) {
                        float px = menuX + pad + static_cast<float>(i) * (pillW + 4.0f);
                        if (fmx >= px && fmx <= px + pillW && fmy >= scaleY && fmy <= scaleY + pillH) {
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
                        if (fmx >= menuX + pad && fmx <= menuX + menuW - pad && fmy >= curY && fmy <= curY + itemH) {
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
                    if (fmx >= menuX + pad && fmx <= menuX + pad + closeBtnW && fmy >= closeBtnY && fmy <= closeBtnY + closeBtnH) {
                        toolbarConfigOpen = false;
                        foley.triggerSound(FoleySound::WoodClick, 0.85f);
                    }
                } else {
                    toolbarConfigOpen = false;
                }
            }
        } else if (leftDown && !prevLeftDown) {
            if (hoveredBtn == BtnMenuToggle) {
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
            } else {
                int hitPile = -1;
                int hitCard = -1;
                if (board.hitTestCard(fmx, fmy, hitPile, hitCard)) {
                    if (autoPlayController.isActive()) {
                        autoPlayController.stop();
                    }
                    if (board.piles()[static_cast<size_t>(hitPile)].type == PileType::Stock) {
                        if (board.drawFromStock()) {
                            foley.triggerSound(FoleySound::Deal, 0.85f, screenPan(fmx));
                        }
                    } else if (hitCard >= 0) {
                        draggedPile = hitPile;
                        draggedCardIdx = hitCard;
                        dragStartMouseX = fmx;
                        dragStartMouseY = fmy;
                        lastDragMouseX = fmx;
                        lastDragMouseY = fmy;
                        dragSoundAccumulator = 0.0f;
                        dragOffsetX = 0.0f;
                        dragOffsetY = 0.0f;
                        foley.triggerSound(FoleySound::Snap, 0.75f, screenPan(fmx));
                    }
                } else {
                    if (autoPlayController.isActive()) {
                        autoPlayController.stop();
                    }
                    board.clearSelection();
                    // Clicked on empty table felt -> Start Gesture!
                    isGesturing = true;
                    gestureStartX = fmx;
                    gestureStartY = fmy;
                    gestureCurX = fmx;
                    gestureCurY = fmy;
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
            float dx = static_cast<float>(fmx - lastDragMouseX);
            float dy = static_cast<float>(fmy - lastDragMouseY);
            lastDragMouseX = fmx;
            lastDragMouseY = fmy;
            dragOffsetX = static_cast<float>(fmx - dragStartMouseX);
            dragOffsetY = static_cast<float>(fmy - dragStartMouseY);
            dragSoundAccumulator += std::hypot(dx, dy);
            if (dragSoundAccumulator > 72.0f) {
                float speed = std::clamp(std::hypot(dx, dy) * 0.08f + 0.65f, 0.6f, 1.4f);
                foley.triggerSound(FoleySound::Slide, 0.32f * speed, screenPan(fmx));
                dragSoundAccumulator = 0.0f;
            }
        } else if (leftDown && isGesturing) {
            gestureCurX = fmx;
            gestureCurY = fmy;
        } else if (!leftDown && prevLeftDown) {
            if (pressedToolbarBtn != BtnNone) {
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
                const float gdx = gestureCurX - gestureStartX;
                const float gdy = gestureCurY - gestureStartY;
                const float gdist = std::hypot(gdx, gdy);

                if (gdist > 36.0f) {
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

        if (!loadingDialog.isOpen() && !asyncManager.isBusy.load() && rightDown && !prevRightDown) {
            int hitPile = -1;
            int hitCard = -1;
            if (board.hitTestCard(fmx, fmy, hitPile, hitCard) && hitPile >= 0 && hitCard >= 0) {
                if (board.smartMove(static_cast<uint8_t>(hitPile), static_cast<size_t>(hitCard))) {
                    foley.triggerSound(FoleySound::Place, 0.85f, screenPan(fmx));
                }
            }
        }

        prevLeftDown = leftDown;
        prevRightDown = rightDown;

        if (rightDown && !prevRightDown) {
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
            }
        }

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
            if (edgeKey(GLFW_KEY_A, prevKeyA)) triggerAction(BtnAutoPlay);
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
            if (edgeKey(GLFW_KEY_ESCAPE, prevKeyEsc)) {
                if (playMenuOpen) {
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
        board.updateAnimations(dt);
        const AppSettings& activeCfg = settingsDialog.isOpen()
            ? settingsDialog.previewSettings()
            : SettingsManager::instance().settings();
        if (lightController.getPreset() != activeCfg.lightingPreset) {
            lightController.setPreset(activeCfg.lightingPreset);
        }
        lightController.update(dt);
        const eatsbits::ui::LightSource2D dynamicLight = lightController.getEffectiveLight(static_cast<float>(width), static_cast<float>(height));
        const auto lCol = lightController.getLightColor();
        const auto aCol = lightController.getAmbientColor();

        if (width > 0 && height > 0) {
            renderer.beginFrame(static_cast<float>(width), static_cast<float>(height));
            renderer.setGlobalLight(dynamicLight, lCol[0], lCol[1], lCol[2], aCol[0], aCol[1], aCol[2],
                                    activeCfg.sceneDarkness, activeCfg.lightStrength);
            renderer.setShaderTime(static_cast<float>(now));
            TableRenderer::renderBoard(renderer, board, static_cast<float>(width), static_cast<float>(height),
                                       draggedPile, draggedCardIdx, dragOffsetX, dragOffsetY, hoveredBtn,
                                       menuOpen, menuAnchor, menuFlowVertical, isDraggingMenu, dragPreviewAnchor, dragPreviewFlowVertical,
                                       isGesturing, gestureStartX, gestureStartY, gestureCurX, gestureCurY,
                                       &dynamicLight, &autoPlayController);
            if (settingsDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                settingsDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), fmx, fmy, pal, static_cast<float>(now));
            }
            if (seedDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                seedDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), fmx, fmy, pal, static_cast<float>(now), window);
            }
            if (gameBrowserDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                gameBrowserDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), fmx, fmy, pal);
            }
            if (loadingDialog.isOpen()) {
                ThemePalette pal = getPalette(board.tableTheme());
                loadingDialog.render(renderer, static_cast<float>(width), static_cast<float>(height), fmx, fmy, pal, now);
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
                                    playMenuAnchorX, playMenuAnchorY, fmx, fmy,
                                    SettingsManager::instance().settings().guaranteeSolvable, dummyHover);
            }

            if (toolbarConfigOpen) {
                int dummyScale = -1;
                int dummyToggle = -1;
                bool dummyClose = false;
                drawToolbarConfigMenu(renderer, static_cast<float>(width), static_cast<float>(height),
                                      toolbarConfigAnchorX, toolbarConfigAnchorY, fmx, fmy,
                                      SettingsManager::instance().settings(),
                                      dummyScale, dummyToggle, dummyClose);
            }
            renderer.endFrame();
        }
    }
};

static std::unique_ptr<InteractiveContext> s_activeContext;

int TableRenderer::runInteractiveWindow(
    SolitaireBoard& board,
    AudioFoleyEngine& foley,
    uint32_t width,
    uint32_t height,
    [[maybe_unused]] int initialAaMode
) {
#if defined(__EMSCRIPTEN__)
    double initCssW = EM_ASM_DOUBLE({
        var container = document.getElementById('canvas-container');
        return (container && container.clientWidth > 0) ? container.clientWidth : (window.innerWidth || 1280);
    });
    double initCssH = EM_ASM_DOUBLE({
        var container = document.getElementById('canvas-container');
        return (container && container.clientHeight > 0) ? container.clientHeight : (window.innerHeight || 720);
    });
    double initDpr = EM_ASM_DOUBLE({
        return window.devicePixelRatio || 1.0;
    });
    if (initDpr <= 0.0) initDpr = 1.0;
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
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height),
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
        var canvas = document.getElementById('canvas');
        if (canvas) {
            canvas.width = $0;
            canvas.height = $1;
            canvas.style.width = $2 + 'px';
            canvas.style.height = $3 + 'px';
        }
        if (typeof GLFW !== 'undefined' && GLFW.active) {
            GLFW.active.width = $2;
            GLFW.active.height = $3;
            GLFW.active.framebufferWidth = $0;
            GLFW.active.framebufferHeight = $1;
        }
    }, initFbW, initFbH, width, height);
#endif

    s_activeContext = std::make_unique<InteractiveContext>(board, foley, width, height);
    InteractiveContext& ctx = *s_activeContext;
    ctx.window = window;

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
        }
    });

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

    ctx.renderer.setAntiAliasingMode(4);
    CardTextureCache::instance().initialize();
    if (ctx.renderer.getBackendType() == eatsbits::ui::RenderBackendType::WebGPU) {
        CardTextureCache::instance().buildGpuAtlas(ctx.renderer);
    }

    board.computeLayout(static_cast<float>(width), static_cast<float>(height));
    ctx.lastTime = glfwGetTime();

    // Check user settings and asynchronously load/condition deck if different from baseline
    ctx.checkStartupSettingsLoad();
    if (!ctx.asyncManager.isBusy.load()) {
        foley.triggerSound(FoleySound::DeckShuffle, 0.85f);
    }

#if defined(__EMSCRIPTEN__)
    std::cout << "[SolEngine Web] Registering Emscripten requestAnimationFrame main loop..." << std::endl;
    emscripten_set_main_loop_arg([](void* arg) {
        auto* c = static_cast<InteractiveContext*>(arg);
        if (c) {
            c->updateAndRender();
        }
    }, &ctx, 0, 1);
    return 0;
#else
    while (!glfwWindowShouldClose(window)) {
        ctx.updateAndRender();
    }

    if (!ctx.board.isVictory()) {
        SessionManager::instance().saveSession(ctx.board);
    } else {
        SessionManager::instance().clearSession();
    }
    SettingsManager::instance().syncFromGame(ctx.board, ctx.foley);
    SettingsManager::instance().save();

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
#endif
void solengine_set_search_query(const char* text) {
    if (!text || !solengine::s_activeContext) return;
    solengine::s_activeContext->gameBrowserDialog.setSearchQuery(text);
}

} // extern "C"
