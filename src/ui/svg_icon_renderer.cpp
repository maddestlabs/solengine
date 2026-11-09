#include "solengine/ui/svg_icon_renderer.hpp"
#include "nanosvg.h"
#include "nanosvgrast.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace solengine {

namespace vector_icon {

void drawStar(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float outerRadius,
    float fillR, float fillG, float fillB, float fillA,
    float outlineR, float outlineG, float outlineB, float outlineA,
    float strokeWidth
) {
    if (outerRadius <= 0.0f || (fillA <= 0.0f && outlineA <= 0.0f)) return;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kInnerRatio = 0.38196601125f; // Golden ratio: (3 - sqrt(5)) / 2
    const float innerRadius = outerRadius * kInnerRatio;

    float vx[10];
    float vy[10];
    constexpr float startAngle = -kPi * 0.5f;
    constexpr float stepAngle = kPi * 0.2f;

    for (int i = 0; i < 10; ++i) {
        float angle = startAngle + static_cast<float>(i) * stepAngle;
        float rad = (i % 2 == 0) ? outerRadius : innerRadius;
        vx[i] = cx + rad * std::cos(angle);
        vy[i] = cy + rad * std::sin(angle);
    }

    if (fillA > 0.0f) {
        for (int i = 0; i < 10; ++i) {
            int next = (i + 1) % 10;
            r.drawTriangle(cx, cy, vx[i], vy[i], vx[next], vy[next], fillR, fillG, fillB, fillA);
        }
    }

    if (outlineA > 0.0f && strokeWidth > 0.0f) {
        for (int i = 0; i < 10; ++i) {
            int next = (i + 1) % 10;
            r.drawLine(vx[i], vy[i], vx[next], vy[next], outlineR, outlineG, outlineB, outlineA, strokeWidth);
        }
    }
}

void drawChevron(
    eatsbits::ui::BatchRenderer2D& r,
    float cx, float cy, float size,
    int direction,
    float red, float green, float blue, float alpha,
    float strokeWidth
) {
    if (size <= 0.0f || alpha <= 0.0f) return;
    float half = size * 0.5f;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    if (direction == 0) { // Left <
        x0 = cx + half * 0.5f; y0 = cy - half;
        x1 = cx - half * 0.5f; y1 = cy;
        x2 = cx + half * 0.5f; y2 = cy + half;
    } else if (direction == 1) { // Right >
        x0 = cx - half * 0.5f; y0 = cy - half;
        x1 = cx + half * 0.5f; y1 = cy;
        x2 = cx - half * 0.5f; y2 = cy + half;
    } else if (direction == 2) { // Up ^
        x0 = cx - half; y0 = cy + half * 0.5f;
        x1 = cx;        y1 = cy - half * 0.5f;
        x2 = cx + half; y2 = cy + half * 0.5f;
    } else { // Down v
        x0 = cx - half; y0 = cy - half * 0.5f;
        x1 = cx;        y1 = cy + half * 0.5f;
        x2 = cx + half; y2 = cy - half * 0.5f;
    }
    r.drawLine(x0, y0, x1, y1, red, green, blue, alpha, strokeWidth);
    r.drawLine(x1, y1, x2, y2, red, green, blue, alpha, strokeWidth);
}

} // namespace vector_icon

const char* SvgIconRenderer::getStockSvgXml(SvgIconId iconId) noexcept {
    switch (iconId) {
        case SvgIconId::NewDeal:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <rect x="3" y="5" width="11" height="15" rx="2" />
                <path d="M7 3h9a2 2 0 0 1 2 2v13" />
                <path d="M19 7l1.5-1.5M19 2l1.5 1.5M21.5 5.5l1.5 1.5M23 3l-1.5 1.5" stroke-width="1.2" />
                <path d="M8.5 11l.01 0M8.5 14l.01 0" stroke-width="2.5" />
            </svg>)";

        case SvgIconId::Restart:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M3 12a9 9 0 1 0 2.64-6.36L3 8" />
                <path d="M3 3v5h5" />
            </svg>)";

        case SvgIconId::Variant:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <rect x="2" y="7" width="9" height="13" rx="1.5" />
                <rect x="7" y="5" width="9" height="13" rx="1.5" />
                <rect x="12" y="3" width="9" height="13" rx="1.5" />
            </svg>)";

        case SvgIconId::Theme:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <path d="M12 2C6.48 2 2 6.48 2 12c0 2.8 1.15 5.33 3 7.14V20a2 2 0 0 0 2 2h2a2 2 0 0 0 2-2v-1.5c0-.83.67-1.5 1.5-1.5h1.5a5 5 0 0 0 5-5c0-5.52-3.13-10-7-10z" />
                <circle cx="7.5" cy="10.5" r="1.5" fill="white" />
                <circle cx="11.5" cy="7.5" r="1.5" fill="white" />
                <circle cx="16.5" cy="10.5" r="1.5" fill="white" />
            </svg>)";

        case SvgIconId::Deck:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <rect x="4" y="2" width="16" height="20" rx="2.5" />
                <path d="M12 6c-2 2.5-4 4-4 6 0 1.66 1.34 3 3 3 .7 0 1.34-.24 1.85-.64l-.85 2.64h2l-.85-2.64c.51.4 1.15.64 1.85.64 1.66 0 3-1.34 3-3 0-2-2-3.5-4-6z" fill="white" />
            </svg>)";

        case SvgIconId::CardStock:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
                <polyline points="14 2 14 8 20 8" />
                <line x1="8" y1="13" x2="16" y2="13" />
                <line x1="8" y1="17" x2="14" y2="17" />
            </svg>)";

        case SvgIconId::CardCorners:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="3" y="3" width="18" height="18" rx="5" />
                <path d="M8 3A5 5 0 0 0 3 8" stroke-width="3" stroke="#f6c050" />
                <circle cx="8" cy="8" r="1.5" fill="white" />
            </svg>)";

        case SvgIconId::CardWear:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="9" />
                <path d="M12 7l1.5 3.5 3.5.5-2.5 2.5.5 3.5-3-1.5-3 1.5.5-3.5-2.5-2.5 3.5-.5z" fill="white" />
            </svg>)";

        case SvgIconId::Undo:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M9 14L4 9l5-5" />
                <path d="M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5v1a5.5 5.5 0 0 1-5.5 5.5H11" />
            </svg>)";

        case SvgIconId::Redo:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M15 14l5-5-5-5" />
                <path d="M20 9H9.5A5.5 5.5 0 0 0 4 14.5v1A5.5 5.5 0 0 0 9.5 21H13" />
            </svg>)";

        case SvgIconId::Hint:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z" />
                <circle cx="12" cy="12" r="3.5" fill="white" />
                <path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41" stroke-width="1.5" />
            </svg>)";

        case SvgIconId::AutoFinish:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" fill="white" />
            </svg>)";

        case SvgIconId::MenuAnchor:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <rect x="3" y="3" width="18" height="18" rx="2" />
                <circle cx="7" cy="17" r="2.5" fill="white" />
                <path d="M12 3v18M3 12h18" stroke-dasharray="2 2" stroke-width="1.2" />
            </svg>)";

        case SvgIconId::Settings:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="3" />
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
            </svg>)";

        case SvgIconId::Dice:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <rect x="3" y="3" width="18" height="18" rx="3.5" />
                <circle cx="8" cy="8" r="1.5" fill="white" />
                <circle cx="16" cy="8" r="1.5" fill="white" />
                <circle cx="12" cy="12" r="1.5" fill="white" />
                <circle cx="8" cy="16" r="1.5" fill="white" />
                <circle cx="16" cy="16" r="1.5" fill="white" />
            </svg>)";

        case SvgIconId::VolumeHigh:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" fill="white" />
                <path d="M15.54 8.46a5 5 0 0 1 0 7.07" />
                <path d="M19.07 4.93a10 10 0 0 1 0 14.14" />
            </svg>)";

        case SvgIconId::VolumeMute:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5" fill="white" />
                <line x1="23" y1="9" x2="17" y2="15" />
                <line x1="17" y1="9" x2="23" y2="15" />
            </svg>)";

        case SvgIconId::Close:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
                <line x1="18" y1="6" x2="6" y2="18" />
                <line x1="6" y1="6" x2="18" y2="18" />
            </svg>)";

        case SvgIconId::Check:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
                <polyline points="20 6 9 17 4 12" />
            </svg>)";

        case SvgIconId::Copy:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <rect x="9" y="9" width="13" height="13" rx="2" />
                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1" />
            </svg>)";

        case SvgIconId::ChevronLeft:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round">
                <polyline points="15 18 9 12 15 6" />
            </svg>)";

        case SvgIconId::ChevronRight:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round">
                <polyline points="9 18 15 12 9 6" />
            </svg>)";

        case SvgIconId::Star:
            return R"(<svg viewBox="0 0 24 24" fill="white" stroke="white" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />
            </svg>)";

        case SvgIconId::StarOutline:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2.0" stroke-linecap="round" stroke-linejoin="round">
                <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />
            </svg>)";

        case SvgIconId::Play:
            return R"(<svg viewBox="0 0 24 24" fill="white">
                <polygon points="6 4 20 12 6 20 6 4" />
            </svg>)";

        case SvgIconId::Pause:
            return R"(<svg viewBox="0 0 24 24" fill="white">
                <rect x="6" y="4" width="4" height="16" rx="1" />
                <rect x="14" y="4" width="4" height="16" rx="1" />
            </svg>)";

        case SvgIconId::FastForward:
            return R"(<svg viewBox="0 0 24 24" fill="white">
                <polygon points="4 5 13 12 4 19 4 5" />
                <polygon points="12 5 21 12 12 19 12 5" />
            </svg>)";

        case SvgIconId::StepForward:
            return R"(<svg viewBox="0 0 24 24" fill="white">
                <polygon points="5 4 15 12 5 20 5 4" />
                <rect x="16" y="4" width="3" height="16" rx="1" />
            </svg>)";

        case SvgIconId::StepBackward:
            return R"(<svg viewBox="0 0 24 24" fill="white">
                <polygon points="19 20 9 12 19 4 19 20" />
                <rect x="5" y="4" width="3" height="16" rx="1" />
            </svg>)";

        case SvgIconId::AutoPlay:
            return R"(<svg viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="10" />
                <polygon points="10 8 16 12 10 16 10 8" fill="white" stroke="none" />
            </svg>)";

        default:
            return "";
    }
}

SvgIconRenderer& SvgIconRenderer::instance() {
    static SvgIconRenderer sInstance;
    return sInstance;
}

SvgIconRenderer::SvgIconRenderer() {
    rasterizer_ = nsvgCreateRasterizer();
}

SvgIconRenderer::~SvgIconRenderer() {
    if (rasterizer_) {
        nsvgDeleteRasterizer(static_cast<NSVGrasterizer*>(rasterizer_));
        rasterizer_ = nullptr;
    }
}

const SvgIconRenderer::CachedIconTexture* SvgIconRenderer::getOrRasterize(SvgIconId id, int targetW, int targetH) {
    if (id == SvgIconId::None || targetW <= 0 || targetH <= 0) return nullptr;

    uint64_t key = (static_cast<uint64_t>(id) << 32) | (static_cast<uint64_t>(targetW & 0xFFFF) << 16) | static_cast<uint64_t>(targetH & 0xFFFF);
    auto it = stockCache_.find(key);
    if (it != stockCache_.end()) {
        return &it->second;
    }

    const char* xml = getStockSvgXml(id);
    if (!xml || !*xml) return nullptr;

    std::vector<char> xmlCopy(xml, xml + std::strlen(xml) + 1);
    NSVGimage* img = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!img) return nullptr;

    float scaleX = (img->width > 0.0f) ? (static_cast<float>(targetW) / img->width) : 1.0f;
    float scaleY = (img->height > 0.0f) ? (static_cast<float>(targetH) / img->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    std::vector<uint8_t> tmpRgba(static_cast<size_t>(targetW * targetH * 4), 0);
    nsvgRasterize(static_cast<NSVGrasterizer*>(rasterizer_), img, 0.0f, 0.0f, scale, tmpRgba.data(), targetW, targetH, targetW * 4);
    nsvgDelete(img);

    CachedIconTexture cached;
    cached.width = targetW;
    cached.height = targetH;
    cached.alphaMask.resize(static_cast<size_t>(targetW * targetH));
    for (size_t i = 0; i < cached.alphaMask.size(); ++i) {
        cached.alphaMask[i] = tmpRgba[i * 4 + 3];
    }

    auto inserted = stockCache_.emplace(key, std::move(cached));
    return &inserted.first->second;
}

const SvgIconRenderer::CachedIconTexture* SvgIconRenderer::getOrRasterizeXml(const std::string& xml, int targetW, int targetH) {
    if (xml.empty() || targetW <= 0 || targetH <= 0) return nullptr;

    std::string key = xml + "_" + std::to_string(targetW) + "x" + std::to_string(targetH);
    auto it = xmlCache_.find(key);
    if (it != xmlCache_.end()) {
        return &it->second;
    }

    std::vector<char> xmlCopy(xml.begin(), xml.end());
    xmlCopy.push_back('\0');
    NSVGimage* img = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!img) return nullptr;

    float scaleX = (img->width > 0.0f) ? (static_cast<float>(targetW) / img->width) : 1.0f;
    float scaleY = (img->height > 0.0f) ? (static_cast<float>(targetH) / img->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    std::vector<uint8_t> tmpRgba(static_cast<size_t>(targetW * targetH * 4), 0);
    nsvgRasterize(static_cast<NSVGrasterizer*>(rasterizer_), img, 0.0f, 0.0f, scale, tmpRgba.data(), targetW, targetH, targetW * 4);
    nsvgDelete(img);

    CachedIconTexture cached;
    cached.width = targetW;
    cached.height = targetH;
    cached.alphaMask.resize(static_cast<size_t>(targetW * targetH));
    for (size_t i = 0; i < cached.alphaMask.size(); ++i) {
        cached.alphaMask[i] = tmpRgba[i * 4 + 3];
    }

    auto inserted = xmlCache_.emplace(std::move(key), std::move(cached));
    return &inserted.first->second;
}

void SvgIconRenderer::renderIcon(
    eatsbits::ui::BatchRenderer2D& r,
    SvgIconId iconId,
    float x,
    float y,
    float w,
    float h,
    float red,
    float green,
    float blue,
    float alpha
) {
    if (iconId == SvgIconId::None || w <= 0.0f || h <= 0.0f || alpha <= 0.0f) return;

    // Fast-path hardware-accelerated procedural 2D vector primitives
    if (iconId == SvgIconId::Star) {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float rad = std::min(w, h) * 0.48f;
        vector_icon::drawStar(r, cx, cy, rad, red, green, blue, alpha, red, green, blue, alpha, 1.2f);
        return;
    }
    if (iconId == SvgIconId::StarOutline) {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float rad = std::min(w, h) * 0.48f;
        vector_icon::drawStar(r, cx, cy, rad, 0.0f, 0.0f, 0.0f, 0.0f, red, green, blue, alpha, 1.5f);
        return;
    }
    if (iconId == SvgIconId::ChevronLeft) {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        vector_icon::drawChevron(r, cx, cy, std::min(w, h) * 0.65f, 0, red, green, blue, alpha, 2.0f);
        return;
    }
    if (iconId == SvgIconId::ChevronRight) {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        vector_icon::drawChevron(r, cx, cy, std::min(w, h) * 0.65f, 1, red, green, blue, alpha, 2.0f);
        return;
    }

    int pixelW = std::clamp(static_cast<int>(std::round(w * 1.5f)), 16, 256);
    int pixelH = std::clamp(static_cast<int>(std::round(h * 1.5f)), 16, 256);

    const auto* cached = getOrRasterize(iconId, pixelW, pixelH);
    if (!cached || cached->alphaMask.empty()) return;

    size_t numPixels = static_cast<size_t>(pixelW * pixelH);
    if (blitBuffer_.size() < numPixels) {
        blitBuffer_.resize(numPixels);
    }

    const uint32_t rByte = static_cast<uint32_t>(std::clamp(red * 255.0f, 0.0f, 255.0f));
    const uint32_t gByte = static_cast<uint32_t>(std::clamp(green * 255.0f, 0.0f, 255.0f));
    const uint32_t bByte = static_cast<uint32_t>(std::clamp(blue * 255.0f, 0.0f, 255.0f));
    const float alphaScale = std::clamp(alpha, 0.0f, 1.0f);

    for (size_t i = 0; i < numPixels; ++i) {
        uint8_t a = cached->alphaMask[i];
        if (a == 0) {
            blitBuffer_[i] = 0x00000000u;
        } else {
            uint32_t effA = static_cast<uint32_t>(std::round(static_cast<float>(a) * alphaScale));
            blitBuffer_[i] = (effA << 24) | (rByte << 16) | (gByte << 8) | bByte;
        }
    }

    r.drawTexturedRoundedRect(x, y, w, h, 0.0f, blitBuffer_.data(), pixelW, pixelH, 1.0f);
}

void SvgIconRenderer::renderSvgXml(
    eatsbits::ui::BatchRenderer2D& r,
    const std::string& svgXml,
    float x,
    float y,
    float w,
    float h,
    float red,
    float green,
    float blue,
    float alpha
) {
    if (svgXml.empty() || w <= 0.0f || h <= 0.0f || alpha <= 0.0f) return;

    int pixelW = std::clamp(static_cast<int>(std::round(w * 1.5f)), 16, 512);
    int pixelH = std::clamp(static_cast<int>(std::round(h * 1.5f)), 16, 512);

    const auto* cached = getOrRasterizeXml(svgXml, pixelW, pixelH);
    if (!cached || cached->alphaMask.empty()) return;

    size_t numPixels = static_cast<size_t>(pixelW * pixelH);
    if (blitBuffer_.size() < numPixels) {
        blitBuffer_.resize(numPixels);
    }

    const uint32_t rByte = static_cast<uint32_t>(std::clamp(red * 255.0f, 0.0f, 255.0f));
    const uint32_t gByte = static_cast<uint32_t>(std::clamp(green * 255.0f, 0.0f, 255.0f));
    const uint32_t bByte = static_cast<uint32_t>(std::clamp(blue * 255.0f, 0.0f, 255.0f));
    const float alphaScale = std::clamp(alpha, 0.0f, 1.0f);

    for (size_t i = 0; i < numPixels; ++i) {
        uint8_t a = cached->alphaMask[i];
        if (a == 0) {
            blitBuffer_[i] = 0x00000000u;
        } else {
            uint32_t effA = static_cast<uint32_t>(std::round(static_cast<float>(a) * alphaScale));
            blitBuffer_[i] = (effA << 24) | (rByte << 16) | (gByte << 8) | bByte;
        }
    }

    r.drawTexturedRoundedRect(x, y, w, h, 0.0f, blitBuffer_.data(), pixelW, pixelH, 1.0f);
}

} // namespace solengine

