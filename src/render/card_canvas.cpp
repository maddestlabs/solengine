#include "solengine/card_canvas.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/classic_deck_symbols.hpp"
#include "eatstextures/field_ops.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/eatscript/vm.hpp"
#include "eatsbits/ui/monospace_font_8x16.hpp"
#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace solengine {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string formatColorHex(uint32_t c) {
    uint8_t a = static_cast<uint8_t>((c >> 24) & 0xFF);
    uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(c & 0xFF);
    char buf[32];
    if (a == 255) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    } else {
        std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3f)", r, g, b, static_cast<float>(a) / 255.0f);
    }
    return std::string(buf);
}

} // namespace

CardCanvas::CardCanvas(int width, int height)
    : width_(std::max(16, width)), height_(std::max(16, height)) {
    const size_t totalPixels = static_cast<size_t>(width_ * height_);
    rgba_.assign(totalPixels * 4, 0);
    reliefMask_.assign(totalPixels, 0.0f);
    reliefLobeMask_.assign(totalPixels, 0.0f);
    reliefDetail_.assign(totalPixels, 0.0f);
    reliefElevation_.assign(totalPixels, 0.0f);
    reliefBulge_.assign(totalPixels, 0.0f);
    reliefMaterial_.assign(totalPixels, 0);
}

CardCanvas::~CardCanvas() = default;

void CardCanvas::clear(uint32_t color) {
    uint8_t a = static_cast<uint8_t>((color >> 24) & 0xFF);
    uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(color & 0xFF);

    const size_t totalPixels = static_cast<size_t>(width_ * height_);
    for (size_t i = 0; i < totalPixels; ++i) {
        rgba_[i * 4 + 0] = r;
        rgba_[i * 4 + 1] = g;
        rgba_[i * 4 + 2] = b;
        rgba_[i * 4 + 3] = a;
    }
    std::fill(reliefMask_.begin(), reliefMask_.end(), 0.0f);
    std::fill(reliefLobeMask_.begin(), reliefLobeMask_.end(), 0.0f);
    std::fill(reliefDetail_.begin(), reliefDetail_.end(), 0.0f);
    std::fill(reliefElevation_.begin(), reliefElevation_.end(), 0.0f);
    std::fill(reliefBulge_.begin(), reliefBulge_.end(), 0.0f);
    std::fill(reliefMaterial_.begin(), reliefMaterial_.end(), static_cast<uint8_t>(0));
    currentElevation_ = 0.75f;
    currentBulge_ = 1.0f;
    currentMaterial_ = 0;
}

void CardCanvas::setElevation(float zBase, float bulge) noexcept {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, zBase, bulge]() {
            setElevation(zBase, bulge);
        });
    }
    currentElevation_ = std::clamp(zBase, 0.0f, 2.5f);
    currentBulge_ = std::clamp(bulge, 0.0f, 3.0f);
}

void CardCanvas::setMaterial(const std::string& matName) noexcept {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, matName]() {
            setMaterial(matName);
        });
    }
    if (matName == "gem_cabochon" || matName == "cabochon_gem" || matName == "ruby_cabochon") {
        currentMaterial_ = 1;
    } else if (matName == "gem_faceted" || matName == "faceted_gem" || matName == "diamond_gem") {
        currentMaterial_ = 2;
    } else if (matName == "gold" || matName == "brass" || matName == "bronze") {
        currentMaterial_ = 3;
    } else if (matName == "onyx" || matName == "obsidian" || matName == "cloisonne_onyx") {
        currentMaterial_ = 4;
    } else {
        currentMaterial_ = 0;
    }
}

void CardCanvas::push() {
    stateStack_.push_back(state_);
}

void CardCanvas::pop() {
    if (!stateStack_.empty()) {
        state_ = stateStack_.back();
        stateStack_.pop_back();
    }
}

void CardCanvas::translate(float dx, float dy) {
    state_.tx += state_.a * dx + state_.c * dy;
    state_.ty += state_.b * dx + state_.d * dy;
}

void CardCanvas::rotate(float angleRad) {
    float cosA = std::cos(angleRad);
    float sinA = std::sin(angleRad);

    float a = state_.a * cosA + state_.c * sinA;
    float c = state_.a * (-sinA) + state_.c * cosA;
    float b = state_.b * cosA + state_.d * sinA;
    float d = state_.b * (-sinA) + state_.d * cosA;

    state_.a = a;
    state_.c = c;
    state_.b = b;
    state_.d = d;
}

void CardCanvas::rotateDegrees(float angleDeg) {
    rotate(angleDeg * (kPi / 180.0f));
}

void CardCanvas::scale(float sx, float sy) {
    state_.a *= sx;
    state_.b *= sx;
    state_.c *= sy;
    state_.d *= sy;
}

void CardCanvas::rotate180() {
    scale(-1.0f, -1.0f);
}

void CardCanvas::flipX() {
    scale(-1.0f, 1.0f);
}

void CardCanvas::flipY() {
    scale(1.0f, -1.0f);
}

void CardCanvas::setOpacity(float alpha) {
    state_.opacity = std::clamp(alpha, 0.0f, 1.0f);
}

void CardCanvas::setTint(uint32_t color) {
    state_.tint = color;
}

void CardCanvas::compositeBuffer(const uint8_t* srcRgba, int srcW, int srcH, float dstX, float dstY, float dstW, float dstH) {
    if (!srcRgba || srcW <= 0 || srcH <= 0 || dstW <= 0.0f || dstH <= 0.0f) return;

    const float globalAlpha = state_.opacity;
    bool hasTint = ((state_.tint >> 24) & 0xFF) > 0;
    float tintR = static_cast<float>((state_.tint >> 16) & 0xFF);
    float tintG = static_cast<float>((state_.tint >> 8) & 0xFF);
    float tintB = static_cast<float>(state_.tint & 0xFF);
    float tintA = static_cast<float>((state_.tint >> 24) & 0xFF) / 255.0f;

    const size_t totalCanvasPixels = static_cast<size_t>(width_ * height_);
    if (reliefMask_.size() < totalCanvasPixels) {
        reliefMask_.assign(totalCanvasPixels, 0.0f);
        reliefLobeMask_.assign(totalCanvasPixels, 0.0f);
        reliefDetail_.assign(totalCanvasPixels, 0.0f);
        reliefElevation_.assign(totalCanvasPixels, 0.0f);
        reliefBulge_.assign(totalCanvasPixels, 0.0f);
        reliefMaterial_.assign(totalCanvasPixels, 0);
    }

    // Measure internal luminance span of the primitive so dark shapes with internal
    // filigree or multi-tone baroque ornaments record accurate 3D creases and ridges.
    float minLum = 1.0f;
    float maxLum = 0.0f;
    float sumLum = 0.0f;
    size_t opaqueCount = 0;
    if (reliefRole_ == 1) {
        const size_t srcTotal = static_cast<size_t>(srcW * srcH);
        const size_t step = (srcTotal > 16384) ? 2 : 1;
        for (size_t i = 0; i < srcTotal; i += step) {
            uint8_t a = srcRgba[i * 4 + 3];
            if (a > 160) {
                float l = (0.299f * srcRgba[i * 4 + 0] + 0.587f * srcRgba[i * 4 + 1] + 0.114f * srcRgba[i * 4 + 2]) / 255.0f;
                minLum = std::min(minLum, l);
                maxLum = std::max(maxLum, l);
                sumLum += l;
                ++opaqueCount;
            }
        }
    }
    const float lumSpan = (opaqueCount > 0) ? (maxLum - minLum) : 0.0f;
    const float meanLum = (opaqueCount > 0) ? (sumLum / static_cast<float>(opaqueCount)) : 0.5f;

    auto updateReliefPixel = [&](size_t dstIdx, float sr, float sg, float sb, float sa) {
        if (reliefRole_ == 0) {
            return;
        }
        if (reliefRole_ == 2) {
            // Recessed medallion well: indent elevation to a sunken basin floor (z = 0.16)
            // so the surrounding high-elevation cartouche casts a deep shadow into the basin
            // and the center Spade sits visibly lower than the surrounding gold scrollwork.
            reliefMask_[dstIdx] = reliefMask_[dstIdx] * (1.0f - sa * 0.88f) + 0.16f * sa;
            reliefLobeMask_[dstIdx] *= (1.0f - sa * 0.94f);
            reliefDetail_[dstIdx] = reliefDetail_[dstIdx] * (1.0f - sa) - 0.24f * sa;
            reliefElevation_[dstIdx] = reliefElevation_[dstIdx] * (1.0f - sa) + 0.16f * sa;
            reliefBulge_[dstIdx] = reliefBulge_[dstIdx] * (1.0f - sa) + 0.18f * sa;
            return;
        }
        float prevMask = reliefMask_[dstIdx];
        float prevElev = reliefElevation_[dstIdx];
        reliefMask_[dstIdx] = std::max(prevMask, sa);

        float zTier = currentElevation_;
        float bulgeTier = currentBulge_;

        // If a higher-elevation layer overlaps an existing foreground layer, create a clean
        // Z-step boundary at the anti-aliased transition so the upper scroll domes independently.
        bool isOverlapStep = (prevMask > 0.25f) && (std::abs(zTier - prevElev) > 0.14f);
        float stackBoost = (prevMask > 0.20f) ? (prevMask * 0.14f) : 0.0f;
        float internalDetail = 0.15f;
        float lobeVal = sa;
        if (lumSpan > 0.015f) {
            float pixLum = (0.299f * sr + 0.587f * sg + 0.114f * sb) / 255.0f;
            float relTone = std::clamp((pixLum - minLum) / lumSpan, 0.0f, 1.0f);
            if (meanLum < 0.26f) {
                // Predominantly dark primitive (e.g. black onyx spade with carved/gold filigree):
                // base basin sits recessed while outer rim and internal scrolls dome individually.
                internalDetail = -0.20f + std::pow(relTone, 0.70f) * 1.15f;
                lobeVal = std::clamp((relTone - 0.15f) / 0.30f, 0.0f, 1.0f) * sa;
            } else {
                // Multi-tone ornament: dark undercut creases (relTone < 0.25) drop lobeVal to 0
                // so every individual leaf lobe and scroll inflates into a distinct 3D cushion.
                internalDetail = (relTone - 0.36f) * 1.05f;
                lobeVal = std::clamp((relTone - 0.22f) / 0.28f, 0.0f, 1.0f) * sa;
            }
        }
        if (isOverlapStep && sa < 0.82f) {
            lobeVal = 0.0f;
        }
        reliefLobeMask_[dstIdx] = reliefLobeMask_[dstIdx] * (1.0f - sa * 0.88f) + lobeVal * 0.88f;
        reliefDetail_[dstIdx] = reliefDetail_[dstIdx] * (1.0f - sa * 0.80f) + (stackBoost + internalDetail) * (0.65f + 0.35f * bulgeTier) * sa;
        reliefElevation_[dstIdx] = reliefElevation_[dstIdx] * (1.0f - sa) + zTier * sa;
        reliefBulge_[dstIdx] = reliefBulge_[dstIdx] * (1.0f - sa) + bulgeTier * sa;
        if (sa > 0.35f) {
            reliefMaterial_[dstIdx] = currentMaterial_;
        }
    };

    // Fast direct 1:1 composite for full-size fragments
    if (srcW == width_ && srcH == height_ && dstX == 0.0f && dstY == 0.0f &&
        dstW == static_cast<float>(width_) && dstH == static_cast<float>(height_) &&
        globalAlpha >= 0.999f && !hasTint) {
        const uint8_t* s = srcRgba;
        uint8_t* d = rgba_.data();
        for (size_t i = 0; i < totalCanvasPixels; ++i, s += 4, d += 4) {
            uint8_t sa = s[3];
            if (sa == 0) continue;
            float a = static_cast<float>(sa) / 255.0f;
            updateReliefPixel(i, static_cast<float>(s[0]), static_cast<float>(s[1]), static_cast<float>(s[2]), a);
            if (sa == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            } else {
                float invA = 1.0f - a;
                d[0] = static_cast<uint8_t>(s[0] * a + d[0] * invA);
                d[1] = static_cast<uint8_t>(s[1] * a + d[1] * invA);
                d[2] = static_cast<uint8_t>(s[2] * a + d[2] * invA);
                d[3] = static_cast<uint8_t>(std::clamp(static_cast<float>(d[3]) + static_cast<float>(sa) * invA, 0.0f, 255.0f));
            }
        }
        return;
    }

    int minX = std::max(0, static_cast<int>(std::floor(dstX)));
    int minY = std::max(0, static_cast<int>(std::floor(dstY)));
    int maxX = std::min(width_, static_cast<int>(std::ceil(dstX + dstW)));
    int maxY = std::min(height_, static_cast<int>(std::ceil(dstY + dstH)));

    float invDstW = 1.0f / dstW;
    float invDstH = 1.0f / dstH;

    for (int y = minY; y < maxY; ++y) {
        float srcYF = (static_cast<float>(y) - dstY) * invDstH * static_cast<float>(srcH);
        int srcY = std::clamp(static_cast<int>(srcYF), 0, srcH - 1);

        size_t rowBase = static_cast<size_t>(y * width_);
        uint8_t* dstRow = &rgba_[(rowBase + minX) * 4];
        for (int x = minX; x < maxX; ++x) {
            float srcXF = (static_cast<float>(x) - dstX) * invDstW * static_cast<float>(srcW);
            int srcX = std::clamp(static_cast<int>(srcXF), 0, srcW - 1);

            const uint8_t* srcPixel = &srcRgba[static_cast<size_t>(srcY * srcW + srcX) * 4];
            float sa = (static_cast<float>(srcPixel[3]) / 255.0f) * globalAlpha;

            if (sa > 0.001f) {
                float sr = static_cast<float>(srcPixel[0]);
                float sg = static_cast<float>(srcPixel[1]);
                float sb = static_cast<float>(srcPixel[2]);

                if (hasTint) {
                    sr = sr * (1.0f - tintA) + tintR * tintA;
                    sg = sg * (1.0f - tintA) + tintG * tintA;
                    sb = sb * (1.0f - tintA) + tintB * tintA;
                }

                updateReliefPixel(rowBase + x, sr, sg, sb, sa);

                float da = static_cast<float>(dstRow[3]) / 255.0f;
                float outA = sa + da * (1.0f - sa);

                if (outA > 0.0001f) {
                    float outR = (sr * sa + static_cast<float>(dstRow[0]) * da * (1.0f - sa)) / outA;
                    float outG = (sg * sa + static_cast<float>(dstRow[1]) * da * (1.0f - sa)) / outA;
                    float outB = (sb * sa + static_cast<float>(dstRow[2]) * da * (1.0f - sa)) / outA;

                    dstRow[0] = static_cast<uint8_t>(std::clamp(outR, 0.0f, 255.0f));
                    dstRow[1] = static_cast<uint8_t>(std::clamp(outG, 0.0f, 255.0f));
                    dstRow[2] = static_cast<uint8_t>(std::clamp(outB, 0.0f, 255.0f));
                    dstRow[3] = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
                }
            }
            dstRow += 4;
        }
    }
}

void CardCanvas::rasterizeSvgFragment(const std::string& svgXml, float x, float y, float w, float h) {
    if (svgXml.empty()) return;

    std::vector<char> xmlCopy(svgXml.begin(), svgXml.end());
    xmlCopy.push_back('\0');

    NSVGimage* img = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!img) return;

    int renderW = (w > 0.0f) ? static_cast<int>(std::ceil(w)) : static_cast<int>(img->width);
    int renderH = (h > 0.0f) ? static_cast<int>(std::ceil(h)) : static_cast<int>(img->height);
    renderW = std::clamp(renderW, 1, 4096);
    renderH = std::clamp(renderH, 1, 4096);

    thread_local static NSVGrasterizer* sRast = nullptr;
    if (!sRast) {
        sRast = nsvgCreateRasterizer();
    }
    if (!sRast) {
        nsvgDelete(img);
        return;
    }

    float scaleX = (img->width > 0.0f) ? (static_cast<float>(renderW) / img->width) : 1.0f;
    float scaleY = (img->height > 0.0f) ? (static_cast<float>(renderH) / img->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    thread_local static std::vector<uint8_t> sTmpBuffer;
    size_t needed = static_cast<size_t>(renderW * renderH * 4);
    if (sTmpBuffer.size() < needed) {
        sTmpBuffer.resize(needed);
    }
    std::fill(sTmpBuffer.begin(), sTmpBuffer.begin() + needed, 0);

    nsvgRasterize(sRast, img, 0.0f, 0.0f, scale, sTmpBuffer.data(), renderW, renderH, renderW * 4);
    nsvgDelete(img);

    compositeBuffer(sTmpBuffer.data(), renderW, renderH, x, y, static_cast<float>(renderW), static_cast<float>(renderH));
}

void CardCanvas::fillRect(float x, float y, float w, float h, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, color]() { fillRect(x, y, w, h, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='0' y='0' width='" << w << "' height='" << h << "' fill='" << hex << "'/>"
       << "</svg>";
    float dstW = w * std::abs(state_.a);
    float dstH = h * std::abs(state_.d);
    int prevRole = reliefRole_;
    if (dstW >= static_cast<float>(width_) * 0.88f && dstH >= static_cast<float>(height_) * 0.88f) {
        reliefRole_ = 0;
    }
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, dstW, dstH);
    reliefRole_ = prevRole;
}

void CardCanvas::strokeRect(float x, float y, float w, float h, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, color, lineWidth]() { strokeRect(x, y, w, h, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='" << (lineWidth * 0.5f) << "' y='" << (lineWidth * 0.5f) << "' width='" << (w - lineWidth) << "' height='" << (h - lineWidth)
       << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillRoundedRect(float x, float y, float w, float h, float radius, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, radius, color]() { fillRoundedRect(x, y, w, h, radius, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='0' y='0' width='" << w << "' height='" << h << "' rx='" << radius << "' ry='" << radius << "' fill='" << hex << "'/>"
       << "</svg>";
    float dstW = w * std::abs(state_.a);
    float dstH = h * std::abs(state_.d);
    int prevRole = reliefRole_;
    if (dstW >= static_cast<float>(width_) * 0.88f && dstH >= static_cast<float>(height_) * 0.88f) {
        reliefRole_ = 0;
    }
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, dstW, dstH);
    reliefRole_ = prevRole;
}

void CardCanvas::strokeRoundedRect(float x, float y, float w, float h, float radius, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, w, h, radius, color, lineWidth]() { strokeRoundedRect(x, y, w, h, radius, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << w << "' height='" << h << "' viewBox='0 0 " << w << " " << h << "'>"
       << "<rect x='" << (lineWidth * 0.5f) << "' y='" << (lineWidth * 0.5f) << "' width='" << (w - lineWidth) << "' height='" << (h - lineWidth)
       << "' rx='" << radius << "' ry='" << radius << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::fillCircle(float cx, float cy, float radius, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, radius, color]() { fillCircle(cx, cy, radius, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    float d = radius * 2.0f;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << d << "' height='" << d << "' viewBox='0 0 " << d << " " << d << "'>"
       << "<circle cx='" << radius << "' cy='" << radius << "' r='" << radius << "' fill='" << hex << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * (cx - radius), state_.ty + state_.d * (cy - radius), d * std::abs(state_.a), d * std::abs(state_.d));
}

void CardCanvas::strokeCircle(float cx, float cy, float radius, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, radius, color, lineWidth]() { strokeCircle(cx, cy, radius, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    float d = radius * 2.0f;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << d << "' height='" << d << "' viewBox='0 0 " << d << " " << d << "'>"
       << "<circle cx='" << radius << "' cy='" << radius << "' r='" << (radius - lineWidth * 0.5f) << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), state_.tx + state_.a * (cx - radius), state_.ty + state_.d * (cy - radius), d * std::abs(state_.a), d * std::abs(state_.d));
}

void CardCanvas::fillOval(float cx, float cy, float rx, float ry, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, rx, ry, color]() { fillOval(cx, cy, rx, ry, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<ellipse cx='" << cx << "' cy='" << cy << "' rx='" << rx << "' ry='" << ry << "' fill='" << hex << "'/>";
    ss << "</g></svg>";
    int prevRole = reliefRole_;
    if (rx >= static_cast<float>(width_) * 0.14f && ry >= static_cast<float>(height_) * 0.12f) {
        reliefRole_ = 2;
    }
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    reliefRole_ = prevRole;
}

void CardCanvas::strokeOval(float cx, float cy, float rx, float ry, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, rx, ry, color, lineWidth]() { strokeOval(cx, cy, rx, ry, color, lineWidth); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<ellipse cx='" << cx << "' cy='" << cy << "' rx='" << rx << "' ry='" << ry
       << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "'/>";
    ss << "</g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawLine(float x0, float y0, float x1, float y1, uint32_t color, float width) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x0, y0, x1, y1, color, width]() { drawLine(x0, y0, x1, y1, color, width); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_ << "' viewBox='0 0 " << width_ << " " << height_ << "'>"
       << "<line x1='" << (state_.tx + state_.a * x0) << "' y1='" << (state_.ty + state_.d * y0)
       << "' x2='" << (state_.tx + state_.a * x1) << "' y2='" << (state_.ty + state_.d * y1)
       << "' stroke='" << hex << "' stroke-width='" << (width * std::abs(state_.a)) << "' stroke-linecap='round'/>"
       << "</svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawSvg(const std::string& svgXml, float x, float y, float w, float h) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, svgXml, x, y, w, h]() { drawSvg(svgXml, x, y, w, h); });
    }
    rasterizeSvgFragment(svgXml, state_.tx + state_.a * x, state_.ty + state_.d * y, w * std::abs(state_.a), h * std::abs(state_.d));
}

void CardCanvas::drawSvgPath(const std::string& pathD, float cx, float cy, float scale, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, pathD, cx, cy, scale, color]() { drawSvgPath(pathD, cx, cy, scale, color); });
    }
    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_ << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ") scale(" << scale << ")'>";
    ss << "<path d='" << pathD << "' fill='" << hex << "'/>";
    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawText(const std::string& text, float x, float y, float size, uint32_t color, const std::string& align) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, text, x, y, size, color, align]() { drawText(text, x, y, size, color, align); });
    }
    if (text.empty() || size <= 0.0f) return;
    if (color == 0) color = 0xFF1A1A1A;

    const float charH = 16.0f;
    const float charAdvance = 8.5f;
    const float scale = size / charH;
    const float totalW = static_cast<float>(text.size()) * charAdvance * scale;

    float startX = x;
    if (align == "center") {
        startX = x - totalW * 0.5f;
    } else if (align == "right") {
        startX = x - totalW;
    }

    std::string hex = formatColorHex(color);
    std::ostringstream pathD;

    for (size_t ci = 0; ci < text.size(); ++ci) {
        uint8_t ch = static_cast<uint8_t>(text[ci]);
        const uint8_t* glyph = eatsbits::ui::getGlyph8x16Bitmap(ch);
        float gx = startX + static_cast<float>(ci) * charAdvance * scale;

        for (int row = 0; row < 16; ++row) {
            uint8_t rowBits = glyph[row];
            if (rowBits == 0) continue;
            for (int col = 0; col < 8; ++col) {
                if (rowBits & (1 << (7 - col))) {
                    float px = gx + static_cast<float>(col) * scale;
                    float py = y + static_cast<float>(row) * scale;
                    pathD << "M " << px << " " << py
                          << " h " << scale << " v " << scale << " h " << -scale << " Z ";
                }
            }
        }
    }

    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<path d='" << pathD.str() << "' fill='" << hex << "'/>";
    ss << "</g></svg>";

    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawRank(const std::string& rank, float x, float y, float size, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, rank, x, y, size, color]() { drawRank(rank, x, y, size, color); });
    }
    if (rank.empty()) return;
    auto glyph = getClassicRankGlyph(rank);
    if (glyph.path.empty()) return;

    if (color == 0) color = 0xFF1A1A1A;

    float aspect = glyph.width / glyph.height;
    float drawH = size;
    float drawW = size * aspect;

    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << x << " " << y << ") scale(" << (drawW / glyph.width) << " " << (drawH / glyph.height)
       << ") translate(" << (-glyph.minX - glyph.width * 0.5f) << " " << (-glyph.minY - glyph.height * 0.5f) << ")'>";
    ss << "<path d='" << glyph.path << "' fill='" << hex << "'/>";
    ss << "</g></g></svg>";

    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawSuit(Suit suit, float cx, float cy, float size, uint32_t color) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, suit, cx, cy, size, color]() { drawSuit(suit, cx, cy, size, color); });
    }
    std::string_view path;
    switch (suit) {
        case Suit::Spades:   path = kClassicSuitPathSpades; break;
        case Suit::Hearts:   path = kClassicSuitPathHearts; break;
        case Suit::Clubs:    path = kClassicSuitPathClubs; break;
        case Suit::Diamonds: path = kClassicSuitPathDiamonds; break;
    }
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }
    drawSvgPath(std::string(path), cx, cy, size * 0.5f, color);
}

void CardCanvas::beginSymmetry(bool mirrorX, bool mirrorY) {
    symmetryActive_ = true;
    symmetryMirrorX_ = mirrorX;
    symmetryMirrorY_ = mirrorY;
    radialSymmetryNFold_ = 0;
    symmetryQueue_.clear();
}

void CardCanvas::endSymmetry() {
    if (!symmetryActive_) return;
    symmetryActive_ = false;

    auto ops = std::move(symmetryQueue_);
    symmetryQueue_.clear();

    for (const auto& op : ops) op();

    if (symmetryMirrorX_) {
        push();
        translate(static_cast<float>(width_), 0.0f);
        flipX();
        for (const auto& op : ops) op();
        pop();
    }
    if (symmetryMirrorY_) {
        push();
        translate(0.0f, static_cast<float>(height_));
        flipY();
        for (const auto& op : ops) op();
        pop();
    }
    if (symmetryMirrorX_ && symmetryMirrorY_) {
        push();
        translate(static_cast<float>(width_), static_cast<float>(height_));
        flipX();
        flipY();
        for (const auto& op : ops) op();
        pop();
    }
}

void CardCanvas::beginRadialSymmetry(int nFold, float cx, float cy, bool mirrorSectors) {
    symmetryActive_ = true;
    radialSymmetryNFold_ = std::clamp(nFold, 2, 64);
    radialSymmetryCx_ = (cx < 0.0f) ? (static_cast<float>(width_) * 0.5f) : cx;
    radialSymmetryCy_ = (cy < 0.0f) ? (static_cast<float>(height_) * 0.5f) : cy;
    radialSymmetryMirror_ = mirrorSectors;
    symmetryQueue_.clear();
}

void CardCanvas::endRadialSymmetry() {
    if (!symmetryActive_) return;
    symmetryActive_ = false;

    auto ops = std::move(symmetryQueue_);
    symmetryQueue_.clear();

    const int n = std::max(1, radialSymmetryNFold_);
    const float stepAngle = (2.0f * kPi) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        push();
        translate(radialSymmetryCx_, radialSymmetryCy_);
        rotate(static_cast<float>(i) * stepAngle);
        translate(-radialSymmetryCx_, -radialSymmetryCy_);
        for (const auto& op : ops) op();
        pop();

        if (radialSymmetryMirror_) {
            push();
            translate(radialSymmetryCx_, radialSymmetryCy_);
            rotate(static_cast<float>(i) * stepAngle);
            flipX();
            translate(-radialSymmetryCx_, -radialSymmetryCy_);
            for (const auto& op : ops) op();
            pop();
        }
    }
    radialSymmetryNFold_ = 0;
}

void CardCanvas::drawVolute(float cx, float cy, float radius, float turns, float angleStart,
                            uint32_t color, float width, const std::string& profile) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, radius, turns, angleStart, color, width, profile]() {
            drawVolute(cx, cy, radius, turns, angleStart, color, width, profile);
        });
    }

    if (radius <= 1.0f || turns <= 0.0f) return;

    const int steps = std::clamp(static_cast<int>(turns * 48.0f), 24, 256);
    const float totalAngle = turns * 2.0f * kPi;
    const float rMin = std::max(1.0f, radius * 0.12f);
    const float growthRate = std::log(radius / rMin);

    std::vector<std::pair<float, float>> leftEdge;
    std::vector<std::pair<float, float>> rightEdge;
    leftEdge.reserve(steps + 1);
    rightEdge.reserve(steps + 1);

    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float theta = angleStart + t * totalAngle;
        float r = rMin * std::exp(growthRate * t);

        float w = width * (0.2f + 0.8f * std::pow(t, 0.7f));

        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        float px = cx + r * cosT;
        float py = cy + r * sinT;

        float dr = r * (growthRate / totalAngle);
        float tx = dr * cosT - r * sinT;
        float ty = dr * sinT + r * cosT;
        float tlen = std::hypot(tx, ty);
        if (tlen > 1e-4f) {
            tx /= tlen;
            ty /= tlen;
        } else {
            tx = -sinT;
            ty = cosT;
        }

        float nx = -ty;
        float ny = tx;

        leftEdge.push_back({px + nx * (w * 0.5f), py + ny * (w * 0.5f)});
        rightEdge.push_back({px - nx * (w * 0.5f), py - ny * (w * 0.5f)});
    }

    std::ostringstream path;
    path << "M " << leftEdge[0].first << " " << leftEdge[0].second;
    for (size_t i = 1; i < leftEdge.size(); ++i) {
        path << " L " << leftEdge[i].first << " " << leftEdge[i].second;
    }
    for (int i = static_cast<int>(rightEdge.size()) - 1; i >= 0; --i) {
        path << " L " << rightEdge[i].first << " " << rightEdge[i].second;
    }
    path << " Z";

    std::string hex = formatColorHex(color);
    std::ostringstream svg;
    svg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    svg << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    svg << "<path d='" << path.str() << "' fill='" << hex << "' stroke='" << hex << "' stroke-width='0.5'/>";

    if (profile == "fluted_ridge") {
        uint8_t a = (color >> 24) & 0xFF;
        uint8_t r = std::min(255, static_cast<int>(((color >> 16) & 0xFF) * 1.35f));
        uint8_t g = std::min(255, static_cast<int>(((color >> 8) & 0xFF) * 1.35f));
        uint8_t b = std::min(255, static_cast<int>((color & 0xFF) * 1.35f));
        uint32_t spineCol = (a << 24) | (r << 16) | (g << 8) | b;
        std::string spineHex = formatColorHex(spineCol);

        svg << "<path d='";
        for (size_t i = 0; i < leftEdge.size(); ++i) {
            float mx = (leftEdge[i].first + rightEdge[i].first) * 0.5f;
            float my = (leftEdge[i].second + rightEdge[i].second) * 0.5f;
            if (i == 0) svg << "M " << mx << " " << my;
            else svg << " L " << mx << " " << my;
        }
        svg << "' fill='none' stroke='" << spineHex << "' stroke-width='" << std::max(0.7f, width * 0.28f) << "' stroke-linecap='round'/>";
    }

    svg << "</g></svg>";
    rasterizeSvgFragment(svg.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawAcanthus(float x1, float y1, float x2, float y2, int lobes,
                              const std::string& curlDir, uint32_t color,
                              uint32_t strokeColor, float strokeWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x1, y1, x2, y2, lobes, curlDir, color, strokeColor, strokeWidth]() {
            drawAcanthus(x1, y1, x2, y2, lobes, curlDir, color, strokeColor, strokeWidth);
        });
    }

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::hypot(dx, dy);
    if (len < 4.0f) return;

    float ux = dx / len;
    float uy = dy / len;
    float nx = -uy;
    float ny = ux;
    if (curlDir == "right") {
        nx = -nx;
        ny = -ny;
    }

    lobes = std::clamp(lobes, 2, 9);
    float bow = len * 0.18f;

    const int spineSteps = lobes * 6;
    std::vector<std::pair<float, float>> spine(spineSteps + 1);
    for (int i = 0; i <= spineSteps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(spineSteps);
        float arch = std::sin(t * kPi) * bow;
        spine[i] = {x1 + ux * (len * t) + nx * arch,
                    y1 + uy * (len * t) + ny * arch};
    }

    std::ostringstream path;
    path << "M " << spine[0].first << " " << spine[0].second;

    for (int l = 0; l < lobes; ++l) {
        float t0 = static_cast<float>(l) / static_cast<float>(lobes);
        float t1 = static_cast<float>(l + 1) / static_cast<float>(lobes);
        float tMid = (t0 + t1) * 0.5f;

        float lobeLen = len * (0.16f + 0.14f * std::sin(tMid * kPi));
        int idx0 = std::clamp(static_cast<int>(t0 * spineSteps), 0, spineSteps);
        int idx1 = std::clamp(static_cast<int>(t1 * spineSteps), 0, spineSteps);

        float baseX = spine[idx0].first;
        float baseY = spine[idx0].second;
        float nextX = spine[idx1].first;
        float nextY = spine[idx1].second;

        // Classical curved acanthus lobe with smooth swelling & hooked tip
        float tipX = baseX + ux * (len / static_cast<float>(lobes) * 0.9f) + nx * lobeLen;
        float tipY = baseY + uy * (len / static_cast<float>(lobes) * 0.9f) + ny * lobeLen;

        float cp1X = baseX + nx * (lobeLen * 0.85f) - ux * (lobeLen * 0.15f);
        float cp1Y = baseY + ny * (lobeLen * 0.85f) - uy * (lobeLen * 0.15f);
        path << " Q " << cp1X << " " << cp1Y << " " << tipX << " " << tipY;

        float cp2X = tipX - nx * (lobeLen * 0.4f) + ux * (lobeLen * 0.25f);
        float cp2Y = tipY - ny * (lobeLen * 0.4f) + uy * (lobeLen * 0.25f);
        path << " Q " << cp2X << " " << cp2Y << " " << nextX << " " << nextY;
    }

    path << " L " << spine.back().first << " " << spine.back().second;
    for (int i = spineSteps - 1; i >= 0; --i) {
        float t = static_cast<float>(i) / static_cast<float>(spineSteps);
        float innerOffset = len * 0.045f * std::sin(t * kPi);
        path << " L " << (spine[i].first - nx * innerOffset) << " " << (spine[i].second - ny * innerOffset);
    }
    path << " Z";

    std::string fillHex = formatColorHex(color);
    std::string strokeHex = strokeColor ? formatColorHex(strokeColor) : fillHex;

    std::ostringstream svg;
    svg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    svg << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    svg << "<path d='" << path.str() << "' fill='" << fillHex << "' stroke='" << strokeHex << "' stroke-width='" << strokeWidth << "' stroke-linejoin='round'/>";

    svg << "<path d='";
    for (size_t i = 0; i < spine.size(); ++i) {
        if (i == 0) svg << "M " << spine[i].first << " " << spine[i].second;
        else svg << " L " << spine[i].first << " " << spine[i].second;
    }
    svg << "' fill='none' stroke='" << strokeHex << "' stroke-width='" << std::max(0.8f, strokeWidth * 0.8f) << "' stroke-linecap='round'/>";

    svg << "</g></svg>";
    rasterizeSvgFragment(svg.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::repeatBeads(float inset, float radius, float spacing, uint32_t color) {
    if (radius <= 0.0f || spacing <= 1.0f) return;

    float x0 = inset;
    float x1 = static_cast<float>(width_) - inset;
    float y0 = inset;
    float y1 = static_cast<float>(height_) - inset;
    if (x1 <= x0 || y1 <= y0) return;

    float bw = x1 - x0;
    float bh = y1 - y0;
    int nx = std::max(2, static_cast<int>(std::round(bw / spacing)));
    int ny = std::max(2, static_cast<int>(std::round(bh / spacing)));
    float dx = bw / static_cast<float>(nx);
    float dy = bh / static_cast<float>(ny);

    uint8_t wa = static_cast<uint8_t>(((color >> 24) & 0xFF) * 0.45f);
    uint32_t wireCol = (wa << 24) | (color & 0x00FFFFFF);
    strokeRect(x0, y0, bw, bh, wireCol, 0.8f);

    std::string hex = formatColorHex(color);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";

    float glintR = radius * 0.35f;
    float glintOff = radius * 0.3f;

    auto addBead = [&](float bx, float by) {
        ss << "<circle cx='" << bx << "' cy='" << by << "' r='" << radius << "' fill='" << hex << "'/>";
        ss << "<circle cx='" << (bx - glintOff) << "' cy='" << (by - glintOff) << "' r='" << glintR << "' fill='#FFFFFF' opacity='0.55'/>";
    };

    for (int i = 0; i <= nx; ++i) {
        float bx = x0 + static_cast<float>(i) * dx;
        addBead(bx, y0);
        addBead(bx, y1);
    }
    for (int j = 1; j < ny; ++j) {
        float by = y0 + static_cast<float>(j) * dy;
        addBead(x0, by);
        addBead(x1, by);
    }

    ss << "</g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::strokeCartouche(float cx, float cy, float rx, float ry,
                                 const std::string& style, uint32_t color, float lineWidth) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, cx, cy, rx, ry, style, color, lineWidth]() {
            strokeCartouche(cx, cy, rx, ry, style, color, lineWidth);
        });
    }

    if (rx <= 2.0f || ry <= 2.0f) return;

    std::string hex = formatColorHex(color);
    std::ostringstream path;

    if (style == "baroque_oval") {
        float tx = cx, ty = cy - ry;
        float bx = cx, by = cy + ry;
        float rxR = cx + rx, ryR = cy;
        float rxL = cx - rx, ryL = cy;
        float sX = rx * 0.72f;
        float sY = ry * 0.70f;

        path << "M " << tx << " " << ty
             << " C " << (cx + sX * 0.5f) << " " << ty << " " << (cx + sX) << " " << (cy - sY * 0.85f) << " " << rxR << " " << ryR
             << " C " << (cx + sX) << " " << (cy + sY * 0.85f) << " " << (cx + sX * 0.5f) << " " << by << " " << bx << " " << by
             << " C " << (cx - sX * 0.5f) << " " << by << " " << (cx - sX) << " " << (cy + sY * 0.85f) << " " << rxL << " " << ryL
             << " C " << (cx - sX) << " " << (cy - sY * 0.85f) << " " << (cx - sX * 0.5f) << " " << ty << " " << tx << " " << ty << " Z";
    } else {
        path << "M " << (cx - rx) << " " << (cy - ry + ry * 0.2f)
             << " Q " << cx << " " << (cy - ry * 1.15f) << " " << (cx + rx) << " " << (cy - ry + ry * 0.2f)
             << " Q " << (cx + rx * 1.15f) << " " << cy << " " << (cx + rx) << " " << (cy + ry - ry * 0.2f)
             << " Q " << cx << " " << (cy + ry * 1.15f) << " " << (cx - rx) << " " << (cy + ry - ry * 0.2f)
             << " Q " << (cx - rx * 1.15f) << " " << cy << " " << (cx - rx) << " " << (cy - ry + ry * 0.2f)
             << " Z";
    }

    std::ostringstream svg;
    svg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    svg << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    svg << "<path d='" << path.str() << "' fill='none' stroke='" << hex << "' stroke-width='" << lineWidth << "' stroke-linejoin='round'/>";
    svg << "</g></svg>";
    rasterizeSvgFragment(svg.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

void CardCanvas::drawOrnateSuit(Suit suit, float cx, float cy, float size, uint32_t bodyColor, uint32_t filigreeColor) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, suit, cx, cy, size, bodyColor, filigreeColor]() {
            drawOrnateSuit(suit, cx, cy, size, bodyColor, filigreeColor);
        });
    }

    if (bodyColor == 0) {
        bodyColor = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }
    if (filigreeColor == 0) {
        filigreeColor = 0xFFD4AF37;
    }

    drawSuit(suit, cx, cy, size, bodyColor);

    std::string filigreeHex = formatColorHex(filigreeColor);
    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ") scale(" << (size * 0.5f) << ")'>";

    if (suit == Suit::Spades) {
        ss << "<path d='M 0 0.35 L 0 -0.55' stroke='" << filigreeHex << "' stroke-width='0.05' stroke-linecap='round'/>";
        ss << "<path d='M -0.12 -0.15 C -0.35 -0.3 -0.45 0.1 -0.18 0.25 C 0.0 0.35 -0.05 0.15 -0.12 0.05' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<path d='M 0.12 -0.15 C 0.35 -0.3 0.45 0.1 0.18 0.25 C 0.0 0.35 0.05 0.15 0.12 0.05' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<circle cx='0' cy='-0.12' r='0.06' fill='" << filigreeHex << "'/>";
    } else if (suit == Suit::Hearts) {
        ss << "<path d='M 0 0.45 C -0.2 0.2 -0.4 0.0 -0.25 -0.25 C -0.15 -0.38 -0.02 -0.28 -0.08 -0.15' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<path d='M 0 0.45 C 0.2 0.2 0.4 0.0 0.25 -0.25 C 0.15 -0.38 0.02 -0.28 0.08 -0.15' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<circle cx='0' cy='0.1' r='0.055' fill='" << filigreeHex << "'/>";
    } else if (suit == Suit::Diamonds) {
        ss << "<path d='M 0 -0.45 L 0.25 0 L 0 0.45 L -0.25 0 Z' fill='none' stroke='" << filigreeHex << "' stroke-width='0.045'/>";
        ss << "<circle cx='0' cy='0' r='0.08' fill='" << filigreeHex << "'/>";
        ss << "<path d='M 0 -0.25 L 0.12 0 L 0 0.25 L -0.12 0 Z' fill='none' stroke='" << filigreeHex << "' opacity='0.75'/>";
    } else if (suit == Suit::Clubs) {
        ss << "<circle cx='0' cy='-0.3' r='0.12' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04'/>";
        ss << "<circle cx='-0.3' cy='0.15' r='0.11' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04'/>";
        ss << "<circle cx='0.3' cy='0.15' r='0.11' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04'/>";
        ss << "<path d='M -0.12 0.55 L 0 0.25 L 0.12 0.55' fill='none' stroke='" << filigreeHex << "' stroke-width='0.04' stroke-linecap='round'/>";
        ss << "<circle cx='0' cy='0.05' r='0.06' fill='" << filigreeHex << "'/>";
    }

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
}

namespace {

void computeBaroquePalette(uint32_t primaryCol, uint32_t secondCol,
                           std::string& baseHex, std::string& deepHex, std::string& ridgeHex) {
    uint8_t a = static_cast<uint8_t>((primaryCol >> 24) & 0xFF);
    if (a == 0) a = 255;
    int r1 = static_cast<int>((primaryCol >> 16) & 0xFF);
    int g1 = static_cast<int>((primaryCol >> 8) & 0xFF);
    int b1 = static_cast<int>(primaryCol & 0xFF);
    float lum1 = (0.299f * r1 + 0.587f * g1 + 0.114f * b1) / 255.0f;

    int r2 = static_cast<int>((secondCol >> 16) & 0xFF);
    int g2 = static_cast<int>((secondCol >> 8) & 0xFF);
    int b2 = static_cast<int>(secondCol & 0xFF);
    float lum2 = (0.299f * r2 + 0.587f * g2 + 0.114f * b2) / 255.0f;

    uint32_t deepCol = 0;
    uint32_t ridgeCol = 0;

    auto makeDarker = [&](int r, int g, int b) -> uint32_t {
        int dr = std::clamp(static_cast<int>(r * 0.56f), 0, 255);
        int dg = std::clamp(static_cast<int>(g * 0.54f), 0, 255);
        int db = std::clamp(static_cast<int>(b * 0.50f), 0, 255);
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(dr) << 16) |
               (static_cast<uint32_t>(dg) << 8) | static_cast<uint32_t>(db);
    };

    auto makeBrighter = [&](int r, int g, int b) -> uint32_t {
        int br = std::clamp(static_cast<int>(r * 1.28f + 30.0f), 0, 255);
        int bg = std::clamp(static_cast<int>(g * 1.26f + 26.0f), 0, 255);
        int bb = std::clamp(static_cast<int>(b * 1.20f + 20.0f), 0, 255);
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(br) << 16) |
               (static_cast<uint32_t>(bg) << 8) | static_cast<uint32_t>(bb);
    };

    if (secondCol != 0 && lum2 < lum1 - 0.03f) {
        deepCol = (static_cast<uint32_t>(a) << 24) | (secondCol & 0x00FFFFFF);
        ridgeCol = makeBrighter(r1, g1, b1);
    } else if (secondCol != 0 && lum2 > lum1 + 0.03f) {
        deepCol = makeDarker(r1, g1, b1);
        ridgeCol = (static_cast<uint32_t>(a) << 24) | (secondCol & 0x00FFFFFF);
    } else {
        deepCol = makeDarker(r1, g1, b1);
        ridgeCol = makeBrighter(r1, g1, b1);
    }

    baseHex = formatColorHex((static_cast<uint32_t>(a) << 24) | (primaryCol & 0x00FFFFFF));
    deepHex = formatColorHex(deepCol);
    ridgeHex = formatColorHex(ridgeCol);
}

void separableBoxBlur(const std::vector<float>& src, std::vector<float>& dst,
                      std::vector<float>& tmp, int w, int h, int radius) {
    if (radius <= 0) {
        std::copy(src.begin(), src.begin() + static_cast<size_t>(w * h), dst.begin());
        return;
    }
    const float invWin = 1.0f / static_cast<float>(2 * radius + 1);

    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        const float* rowIn = &src[static_cast<size_t>(y * w)];
        float* rowOut = &tmp[static_cast<size_t>(y * w)];
        float sum = rowIn[0] * static_cast<float>(radius + 1);
        for (int rx = 1; rx <= radius; ++rx) {
            sum += rowIn[std::min(w - 1, rx)];
        }
        for (int x = 0; x < w; ++x) {
            rowOut[x] = sum * invWin;
            int removeIdx = std::max(0, x - radius);
            int addIdx = std::min(w - 1, x + radius + 1);
            sum += rowIn[addIdx] - rowIn[removeIdx];
        }
    }

    // Vertical pass
    for (int x = 0; x < w; ++x) {
        float sum = tmp[x] * static_cast<float>(radius + 1);
        for (int ry = 1; ry <= radius; ++ry) {
            sum += tmp[static_cast<size_t>(std::min(h - 1, ry) * w + x)];
        }
        for (int y = 0; y < h; ++y) {
            dst[static_cast<size_t>(y * w + x)] = sum * invWin;
            int removeIdx = std::max(0, y - radius);
            int addIdx = std::min(h - 1, y + radius + 1);
            sum += tmp[static_cast<size_t>(addIdx * w + x)] - tmp[static_cast<size_t>(removeIdx * w + x)];
        }
    }
}

void separableTentBlur(const std::vector<float>& src, std::vector<float>& dst,
                       std::vector<float>& tmp, std::vector<float>& ping, int w, int h, int radius) {
    int r1 = std::max(1, (radius + 1) / 2);
    int r2 = std::max(1, radius - r1 + 1);
    separableBoxBlur(src, ping, tmp, w, h, r1);
    separableBoxBlur(ping, dst, tmp, w, h, r2);
}

void chamferDistance2Pass(const std::vector<float>& mask, std::vector<float>& dist, int w, int h, float maxCap) {
    const size_t total = static_cast<size_t>(w * h);
    for (size_t i = 0; i < total; ++i) {
        dist[i] = (mask[i] > 0.35f) ? maxCap * mask[i] : 0.0f;
    }
    // Forward pass (top-left to bottom-right)
    for (int y = 0; y < h; ++y) {
        size_t row = static_cast<size_t>(y * w);
        size_t rowUp = static_cast<size_t>(std::max(0, y - 1) * w);
        for (int x = 0; x < w; ++x) {
            float d = dist[row + x];
            if (d <= 0.0f) continue;
            if (x > 0) d = std::min(d, dist[row + x - 1] + 1.0f);
            if (y > 0) {
                d = std::min(d, dist[rowUp + x] + 1.0f);
                if (x > 0) d = std::min(d, dist[rowUp + x - 1] + 1.414f);
                if (x + 1 < w) d = std::min(d, dist[rowUp + x + 1] + 1.414f);
            }
            dist[row + x] = d;
        }
    }
    // Backward pass (bottom-right to top-left)
    for (int y = h - 1; y >= 0; --y) {
        size_t row = static_cast<size_t>(y * w);
        size_t rowDn = static_cast<size_t>(std::min(h - 1, y + 1) * w);
        for (int x = w - 1; x >= 0; --x) {
            float d = dist[row + x];
            if (d <= 0.0f) continue;
            if (x + 1 < w) d = std::min(d, dist[row + x + 1] + 1.0f);
            if (y + 1 < h) {
                d = std::min(d, dist[rowDn + x] + 1.0f);
                if (x + 1 < w) d = std::min(d, dist[rowDn + x + 1] + 1.414f);
                if (x > 0) d = std::min(d, dist[rowDn + x - 1] + 1.414f);
            }
            dist[row + x] = d;
        }
    }
}

} // namespace

void CardCanvas::drawBaroqueCorner(float x, float y, float size, uint32_t goldColor, uint32_t accentColor) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, x, y, size, goldColor, accentColor]() {
            drawBaroqueCorner(x, y, size, goldColor, accentColor);
        });
    }

    if (size <= 4.0f) return;
    if (goldColor == 0) goldColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string goldHex, deepHex, accHex;
    computeBaroquePalette(goldColor, accentColor, goldHex, deepHex, accHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    float scale = size / 100.0f;

    auto makeHeader = [&]() {
        std::ostringstream s;
        s << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
          << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
        s << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
          << " " << state_.tx << " " << state_.ty << ")'>";
        s << "<g transform='translate(" << x << " " << y << ") scale(" << scale << ")'>";
        return s.str();
    };
    const std::string header = makeHeader();

    // PASS 1 (Mid-Elevation Base Foliage, z = 0.62, bulge = 0.85):
    // Calyx base sheath, cascading border fronds, and diagonal inner acanthus plumes
    currentElevation_ = 0.62f;
    currentBulge_ = 0.85f;
    {
        std::ostringstream ss;
        ss << header;
        // 1. Broad Sculpted Acanthus Calyx Base Sheath
        ss << "<path d='M 4 108 C 2 48 16 14 54 6 C 86 -1 110 14 106 40 C 103 62 84 74 62 70 C 44 67 38 50 46 38 "
           << "C 32 46 26 70 22 108 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='2.0' stroke-linejoin='round'/>";

        // 2. Cascading Vertical Acanthus Plumes
        ss << "<path d='M 10 26 C -4 38 -4 58 10 70 C 20 64 24 48 18 34 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.6'/>";
        ss << "<path d='M 8 33 Q 2 50 11 64' fill='none' stroke='" << accHex << "' stroke-width='2.2' stroke-linecap='round'/>";

        ss << "<path d='M 10 54 C -5 68 -4 92 12 104 C 22 98 25 80 19 64 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.6'/>";
        ss << "<path d='M 8 61 Q 2 80 12 97' fill='none' stroke='" << accHex << "' stroke-width='2.2' stroke-linecap='round'/>";

        ss << "<path d='M 9 84 C -4 102 -2 126 14 138 C 23 130 25 112 18 96 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.6'/>";
        ss << "<path d='M 8 92 Q 3 112 13 130' fill='none' stroke='" << accHex << "' stroke-width='2.0' stroke-linecap='round'/>";

        // 3. Cascading Horizontal Acanthus Plumes
        ss << "<path d='M 28 12 C 40 -4 64 -4 76 12 C 68 21 52 23 36 18 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.6'/>";
        ss << "<path d='M 34 10 Q 52 2 70 10' fill='none' stroke='" << accHex << "' stroke-width='2.2' stroke-linecap='round'/>";

        ss << "<path d='M 64 11 C 80 -4 104 -4 114 12 C 106 21 90 23 72 18 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.6'/>";
        ss << "<path d='M 70 10 Q 90 2 108 10' fill='none' stroke='" << accHex << "' stroke-width='2.2' stroke-linecap='round'/>";

        // 4. Diagonal Inner Acanthus Fronds Sweeping Toward Medallion
        ss << "<path d='M 56 60 C 68 82 88 98 112 104 C 106 86 92 70 74 58 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.6'/>";
        ss << "<path d='M 62 63 Q 84 84 105 98' fill='none' stroke='" << accHex << "' stroke-width='2.2' stroke-linecap='round'/>";

        ss << "<path d='M 30 50 C 40 72 56 90 78 102 C 70 86 56 70 42 52 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
        ss << "<path d='M 35 55 Q 54 78 71 95' fill='none' stroke='" << accHex << "' stroke-width='1.9' stroke-linecap='round'/>";
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    // PASS 2 (High-Relief Bulging Primary C-Scroll & Terminal Volute Bosses, z = 1.28, bulge = 1.52):
    // Bulges dramatically toward the camera and casts directional shadows onto the lower foliage!
    currentElevation_ = 1.28f;
    currentBulge_ = 1.52f;
    {
        std::ostringstream ss;
        ss << header;
        // Primary Fluted C-Scroll Volute Spine
        ss << "<path d='M 12 98 C 12 52 24 20 56 12 C 82 4 98 18 98 38 C 98 58 80 70 62 66 C 46 63 40 48 48 38 C 52 32 62 34 61 42' "
           << "fill='none' stroke='" << deepHex << "' stroke-width='8.2' stroke-linecap='round' stroke-linejoin='round'/>";
        ss << "<path d='M 12 98 C 12 52 24 20 56 12 C 82 4 98 18 98 38 C 98 58 80 70 62 66 C 46 63 40 48 48 38 C 52 32 62 34 61 42' "
           << "fill='none' stroke='" << goldHex << "' stroke-width='5.8' stroke-linecap='round' stroke-linejoin='round'/>";
        ss << "<path d='M 12 96 C 12 52 24 20 56 12 C 82 4 98 18 98 38 C 98 58 80 70 62 66 C 46 63 40 48 48 38' "
           << "fill='none' stroke='" << accHex << "' stroke-width='2.6' stroke-linecap='round'/>";

        // Bulbous Scroll-End Volute Knobs & Center Rosette Boss
        ss << "<circle cx='60' cy='44' r='7.2' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.7'/>";
        ss << "<circle cx='59' cy='43' r='3.6' fill='" << accHex << "'/>";
        ss << "<circle cx='12' cy='98' r='5.2' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
        ss << "<circle cx='11.5' cy='97' r='2.4' fill='" << accHex << "'/>";
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
}

void CardCanvas::drawBaroqueCrest(float cx, float cy, float scale, uint32_t goldColor, uint32_t accentColor, bool flipY) {
    if (goldColor == 0) goldColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string goldHex, deepHex, accHex;
    computeBaroquePalette(goldColor, accentColor, goldHex, deepHex, accHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    float ySign = flipY ? -1.0f : 1.0f;

    std::ostringstream hdr;
    hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    hdr << "<g transform='translate(" << cx << " " << cy << ") scale(" << scale << " " << (scale * ySign) << ")'>";
    const std::string header = hdr.str();

    // PASS 1 (Mid-Relief Palmette Fan & Spear Finial, z = 0.66, bulge = 0.88)
    currentElevation_ = 0.66f;
    currentBulge_ = 0.88f;
    {
        std::ostringstream ss;
        ss << header;
        // 1. Broad Acanthus Crown Pediment Fan
        ss << "<path d='M 0 -4 C -16 -24 -38 -20 -46 -4 C -36 6 -18 6 0 0 C 18 6 36 6 46 -4 C 38 -20 16 -24 0 -4 Z' "
           << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
        ss << "<path d='M -6 -6 Q -24 -16 -40 -4' fill='none' stroke='" << accHex << "' stroke-width='1.8' stroke-linecap='round'/>";
        ss << "<path d='M 6 -6 Q 24 -16 40 -4' fill='none' stroke='" << accHex << "' stroke-width='1.8' stroke-linecap='round'/>";

        // 2. Outward Flaring Secondary Palmette Lobes
        ss << "<path d='M 0 -10 C -12 -28 -28 -22 -24 -6 C -16 -2 -6 -6 0 -8 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
        ss << "<path d='M 0 -10 C 12 -28 28 -22 24 -6 C 16 -2 6 -6 0 -8 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
        ss << "<path d='M -3 -12 Q -14 -20 -20 -8' fill='none' stroke='" << accHex << "' stroke-width='1.6' stroke-linecap='round'/>";
        ss << "<path d='M 3 -12 Q 14 -20 20 -8' fill='none' stroke='" << accHex << "' stroke-width='1.6' stroke-linecap='round'/>";

        // 3. Center Fleur-de-lis / Palmette Spear Finial
        ss << "<path d='M 0 -32 C -9 -20 -8 -6 0 4 C 8 -6 9 -20 0 -32 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
        ss << "<path d='M 0 -28 L 0 0' stroke='" << accHex << "' stroke-width='2.0' stroke-linecap='round'/>";
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    // PASS 2 (High-Relief Bulging Crown Volutes & Bulbous Knobs, z = 1.28, bulge = 1.50)
    currentElevation_ = 1.28f;
    currentBulge_ = 1.50f;
    {
        std::ostringstream ss;
        ss << header;
        // 4. Dual Outward-Curling Fluted Volute Scrolls
        ss << "<path d='M 0 -3 C -16 -14 -38 -16 -48 -2 C -54 10 -46 26 -32 26 C -20 26 -14 14 -22 6 C -26 2 -34 5 -32 11' "
           << "fill='none' stroke='" << deepHex << "' stroke-width='6.4' stroke-linecap='round'/>";
        ss << "<path d='M 0 -3 C 16 -14 38 -16 48 -2 C 54 10 46 26 32 26 C 20 26 14 14 22 6 C 26 2 34 5 32 11' "
           << "fill='none' stroke='" << deepHex << "' stroke-width='6.4' stroke-linecap='round'/>";

        ss << "<path d='M 0 -3 C -16 -14 -38 -16 -48 -2 C -54 10 -46 26 -32 26 C -20 26 -14 14 -22 6 C -26 2 -34 5 -32 11' "
           << "fill='none' stroke='" << goldHex << "' stroke-width='4.5' stroke-linecap='round'/>";
        ss << "<path d='M 0 -3 C 16 -14 38 -16 48 -2 C 54 10 46 26 32 26 C 20 26 14 14 22 6 C 26 2 34 5 32 11' "
           << "fill='none' stroke='" << goldHex << "' stroke-width='4.5' stroke-linecap='round'/>";

        ss << "<path d='M -2 -4 C -16 -14 -38 -16 -48 -2 C -52 8 -46 22 -34 22' fill='none' stroke='" << accHex << "' stroke-width='2.0' stroke-linecap='round'/>";
        ss << "<path d='M 2 -4 C 16 -14 38 -16 48 -2 C 52 8 46 22 34 22' fill='none' stroke='" << accHex << "' stroke-width='2.0' stroke-linecap='round'/>";

        // 5. High-Bulge Bosses & Drop Pearl Pendants
        ss << "<circle cx='0' cy='5' r='5.4' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
        ss << "<circle cx='-0.5' cy='4.5' r='2.6' fill='" << accHex << "'/>";
        ss << "<circle cx='-32' cy='13' r='4.2' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
        ss << "<circle cx='-32.5' cy='12.5' r='2.0' fill='" << accHex << "'/>";
        ss << "<circle cx='32' cy='13' r='4.2' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
        ss << "<circle cx='31.5' cy='12.5' r='2.0' fill='" << accHex << "'/>";
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
}

void CardCanvas::drawBaroqueCartouche(float cx, float cy, float rx, float ry, uint32_t goldColor, uint32_t accentColor, float lineWidth) {
    if (rx <= 4.0f || ry <= 4.0f) return;
    if (goldColor == 0) goldColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string goldHex, deepHex, accHex;
    computeBaroquePalette(goldColor, accentColor, goldHex, deepHex, accHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;

    std::ostringstream hdr;
    hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    hdr << "<g transform='translate(" << cx << " " << cy << ")'>";
    const std::string header = hdr.str();

    // PASS 1 (Mid-Elevation Acanthus Garland & Inner Molded Frame, z = 0.68, bulge = 0.90)
    currentElevation_ = 0.68f;
    currentBulge_ = 0.90f;
    {
        std::ostringstream ss;
        ss << header;

        auto drawQuadrantGarland = [&](float sxSign, float sySign) {
            ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";

            // Outer Secondary Acanthus Frond Tier
            ss << "<path d='M " << (rx * 0.32f) << " " << (-ry * 0.92f)
               << " C " << (rx * 0.56f) << " " << (-ry * 1.18f) << " " << (rx * 0.94f) << " " << (-ry * 1.00f) << " " << (rx * 0.80f) << " " << (-ry * 0.68f)
               << " C " << (rx * 0.64f) << " " << (-ry * 0.72f) << " " << (rx * 0.46f) << " " << (-ry * 0.82f) << " " << (rx * 0.32f) << " " << (-ry * 0.92f) << " Z' "
               << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
            ss << "<path d='M " << (rx * 0.38f) << " " << (-ry * 0.92f)
               << " Q " << (rx * 0.68f) << " " << (-ry * 1.02f) << " " << (rx * 0.78f) << " " << (-ry * 0.72f) << "' "
               << "fill='none' stroke='" << accHex << "' stroke-width='2.0' stroke-linecap='round'/>";

            ss << "<path d='M " << (rx * 0.66f) << " " << (-ry * 0.72f)
               << " C " << (rx * 1.04f) << " " << (-ry * 0.78f) << " " << (rx * 1.26f) << " " << (-ry * 0.48f) << " " << (rx * 1.04f) << " " << (-ry * 0.24f)
               << " C " << (rx * 0.88f) << " " << (-ry * 0.36f) << " " << (rx * 0.76f) << " " << (-ry * 0.54f) << " " << (rx * 0.66f) << " " << (-ry * 0.72f) << " Z' "
               << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
            ss << "<path d='M " << (rx * 0.72f) << " " << (-ry * 0.68f)
               << " Q " << (rx * 1.08f) << " " << (-ry * 0.58f) << " " << (rx * 1.02f) << " " << (-ry * 0.28f) << "' "
               << "fill='none' stroke='" << accHex << "' stroke-width='2.0' stroke-linecap='round'/>";

            // Primary Inner Acanthus Lobes Hugging the Cartouche Arch
            float ax1 = rx * 0.18f, ay1 = -ry * 0.96f;
            float ax2 = rx * 0.62f, ay2 = -ry * 0.74f;
            float ax3 = rx * 0.92f, ay3 = -ry * 0.34f;

            ss << "<path d='M " << ax1 << " " << ay1
               << " C " << (rx * 0.38f) << " " << (-ry * 1.10f) << " " << (rx * 0.70f) << " " << (-ry * 0.94f) << " " << ax2 << " " << ay2
               << " C " << (rx * 0.46f) << " " << (-ry * 0.76f) << " " << (rx * 0.30f) << " " << (-ry * 0.86f) << " " << ax1 << " " << ay1 << " Z' "
               << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
            ss << "<path d='M " << (ax1 + 4.0f) << " " << (ay1 - 2.0f)
               << " Q " << (rx * 0.48f) << " " << (-ry * 0.96f) << " " << (ax2 - 2.0f) << " " << (ay2 - 1.0f) << "' "
               << "fill='none' stroke='" << accHex << "' stroke-width='2.1' stroke-linecap='round'/>";

            ss << "<path d='M " << (rx * 0.50f) << " " << (-ry * 0.80f)
               << " C " << (rx * 0.82f) << " " << (-ry * 0.82f) << " " << (rx * 1.10f) << " " << (-ry * 0.56f) << " " << ax3 << " " << ay3
               << " C " << (rx * 0.78f) << " " << (-ry * 0.44f) << " " << (rx * 0.64f) << " " << (-ry * 0.64f) << " " << (rx * 0.50f) << " " << (-ry * 0.80f) << " Z' "
               << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
            ss << "<path d='M " << (rx * 0.56f) << " " << (-ry * 0.75f)
               << " Q " << (rx * 0.90f) << " " << (-ry * 0.64f) << " " << ax3 << " " << (ay3 - 2.0f) << "' "
               << "fill='none' stroke='" << accHex << "' stroke-width='2.1' stroke-linecap='round'/>";

            // Lower-waist acanthus plume approaching lateral clasp
            ss << "<path d='M " << (rx * 0.82f) << " " << (-ry * 0.46f)
               << " C " << (rx * 1.14f) << " " << (-ry * 0.38f) << " " << (rx * 1.20f) << " " << (-ry * 0.14f) << " " << (rx * 0.98f) << " " << (-ry * 0.06f)
               << " C " << (rx * 0.86f) << " " << (-ry * 0.16f) << " " << (rx * 0.80f) << " " << (-ry * 0.32f) << " " << (rx * 0.82f) << " " << (-ry * 0.46f) << " Z' "
               << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
            ss << "<path d='M " << (rx * 0.88f) << " " << (-ry * 0.39f)
               << " Q " << (rx * 1.08f) << " " << (-ry * 0.24f) << " " << (rx * 0.97f) << " " << (-ry * 0.10f) << "' "
               << "fill='none' stroke='" << accHex << "' stroke-width='1.9' stroke-linecap='round'/>";
            ss << "</g>";
        };
        drawQuadrantGarland(1.0f, 1.0f);
        drawQuadrantGarland(-1.0f, 1.0f);
        drawQuadrantGarland(1.0f, -1.0f);
        drawQuadrantGarland(-1.0f, -1.0f);

        // Inner parallel molded frame with highlight & 28 beaded pearls
        float inRx = rx * 0.86f;
        float inRy = ry * 0.86f;
        float inSx = inRx * 0.72f;
        float inSy = inRy * 0.70f;
        std::ostringstream inArchPath;
        inArchPath << "M 0 " << -inRy
                   << " C " << (inSx * 0.5f) << " " << -inRy << " " << inSx << " " << (-inSy * 0.85f) << " " << inRx << " 0"
                   << " C " << inSx << " " << (inSy * 0.85f) << " " << (inSx * 0.5f) << " " << inRy << " 0 " << inRy
                   << " C " << (-inSx * 0.5f) << " " << inRy << " " << -inSx << " " << (inSy * 0.85f) << " " << -inRx << " 0"
                   << " C " << -inSx << " " << (-inSy * 0.85f) << " " << (-inSx * 0.5f) << " " << -inRy << " 0 " << -inRy << " Z";
        ss << "<path d='" << inArchPath.str() << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 1.3f) << "' stroke-linejoin='round'/>";
        ss << "<path d='" << inArchPath.str() << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.55f) << "' stroke-linejoin='round'/>";

        constexpr int kPearlCount = 28;
        for (int p = 0; p < kPearlCount; ++p) {
            float angle = static_cast<float>(p) / static_cast<float>(kPearlCount) * 2.0f * kPi;
            float px = std::cos(angle) * (rx * 0.92f);
            float py = std::sin(angle) * (ry * 0.92f);
            ss << "<circle cx='" << px << "' cy='" << py << "' r='2.3' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.7'/>";
            ss << "<circle cx='" << (px - 0.5f) << "' cy='" << (py - 0.5f) << "' r='0.95' fill='" << accHex << "'/>";
        }
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    // PASS 2 (High-Relief Bulging Outer Arch Torus, Lateral Wing C-Scrolls & Boss Knobs, z = 1.32, bulge = 1.55):
    // Bulges forward like the heavy sculpted gold C-scrolls in the reference image and casts deep shadows inward & outward!
    currentElevation_ = 1.32f;
    currentBulge_ = 1.55f;
    {
        std::ostringstream ss;
        ss << header;

        float tx = 0.0f, ty = -ry;
        float bx = 0.0f, by = ry;
        float rxR = rx, ryR = 0.0f;
        float rxL = -rx, ryL = 0.0f;
        float sX = rx * 0.72f;
        float sY = ry * 0.70f;

        std::ostringstream archPath;
        archPath << "M " << tx << " " << ty
                 << " C " << (sX * 0.5f) << " " << ty << " " << sX << " " << (-sY * 0.85f) << " " << rxR << " " << ryR
                 << " C " << sX << " " << (sY * 0.85f) << " " << (sX * 0.5f) << " " << by << " " << bx << " " << by
                 << " C " << (-sX * 0.5f) << " " << by << " " << -sX << " " << (sY * 0.85f) << " " << rxL << " " << ryL
                 << " C " << -sX << " " << (-sY * 0.85f) << " " << (-sX * 0.5f) << " " << ty << " " << tx << " " << ty << " Z";

        ss << "<path d='" << archPath.str() << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 2.6f) << "' stroke-linejoin='round'/>";
        ss << "<path d='" << archPath.str() << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 1.70f) << "' stroke-linejoin='round'/>";
        ss << "<path d='" << archPath.str() << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.72f) << "' stroke-linejoin='round'/>";

        // Heavy Bulging Lateral C-Scroll Volutes & Bulbous Knobs flanking all 4 quadrants of the cartouche
        auto drawHighBulgeWingScrolls = [&](float sxSign, float sySign) {
            ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";
            // Sweeping outer C-scroll arch with bulbous scroll-eye knob
            std::ostringstream cScroll;
            cScroll << "M " << (rx * 0.52f) << " " << (-ry * 0.78f)
                    << " C " << (rx * 0.90f) << " " << (-ry * 0.84f) << " " << (rx * 1.22f) << " " << (-ry * 0.52f) << " " << (rx * 1.14f) << " " << (-ry * 0.20f)
                    << " C " << (rx * 1.08f) << " " << (-ry * 0.04f) << " " << (rx * 0.90f) << " " << (-ry * 0.06f) << " " << (rx * 0.92f) << " " << (-ry * 0.18f);
            ss << "<path d='" << cScroll.str() << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 2.3f) << "' stroke-linecap='round'/>";
            ss << "<path d='" << cScroll.str() << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 1.55f) << "' stroke-linecap='round'/>";
            ss << "<path d='" << cScroll.str() << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.65f) << "' stroke-linecap='round'/>";

            // Bulbous terminal scroll-head bosses
            float kx1 = rx * 0.54f, ky1 = -ry * 0.77f;
            float kx2 = rx * 0.94f, ky2 = -ry * 0.17f;
            ss << "<circle cx='" << kx1 << "' cy='" << ky1 << "' r='" << (lineWidth * 1.55f) << "' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
            ss << "<circle cx='" << (kx1 - 0.6f) << "' cy='" << (ky1 - 0.6f) << "' r='" << (lineWidth * 0.72f) << "' fill='" << accHex << "'/>";
            ss << "<circle cx='" << kx2 << "' cy='" << ky2 << "' r='" << (lineWidth * 1.65f) << "' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
            ss << "<circle cx='" << (kx2 - 0.6f) << "' cy='" << (ky2 - 0.6f) << "' r='" << (lineWidth * 0.78f) << "' fill='" << accHex << "'/>";
            ss << "</g>";
        };
        drawHighBulgeWingScrolls(1.0f, 1.0f);
        drawHighBulgeWingScrolls(-1.0f, 1.0f);
        drawHighBulgeWingScrolls(1.0f, -1.0f);
        drawHighBulgeWingScrolls(-1.0f, -1.0f);

        // Lateral Waist Shell / Rosette Clasps at left and right
        auto drawClasp = [&](float xOff, float dir) {
            ss << "<g transform='translate(" << xOff << " 0) scale(" << dir << " 1)'>";
            ss << "<path d='M -6 -26 C 14 -18 22 -8 22 0 C 22 8 14 18 -6 26 C 2 12 2 -12 -6 -26 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
            ss << "<path d='M -2 -16 Q 14 0 -2 16' fill='none' stroke='" << accHex << "' stroke-width='1.9' stroke-linecap='round'/>";
            ss << "<circle cx='7' cy='0' r='5.4' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
            ss << "<circle cx='6.5' cy='-0.5' r='2.5' fill='" << accHex << "'/>";
            ss << "</g>";
        };
        drawClasp(rx, 1.0f);
        drawClasp(-rx, -1.0f);

        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;

    // 5. Top and bottom Crown Pediments
    drawBaroqueCrest(cx, cy - ry, std::min(rx, ry) * 0.46f, goldColor, accentColor, false);
    drawBaroqueCrest(cx, cy + ry, std::min(rx, ry) * 0.46f, goldColor, accentColor, true);
}

void CardCanvas::drawBaroqueSpade(float cx, float cy, float size, uint32_t bodyColor, uint32_t filigreeColor, uint32_t accentColor) {
    if (size <= 4.0f) return;
    if (bodyColor == 0) bodyColor = 0xFF14151A;
    if (filigreeColor == 0) filigreeColor = 0xFFD4AF37;
    if (accentColor == 0) accentColor = 0xFFFFF8DC;

    std::string bodyHex = formatColorHex(bodyColor);
    std::string filigreeHex, deepHex, accHex;
    computeBaroquePalette(filigreeColor, accentColor, filigreeHex, deepHex, accHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    float scale = size / 200.0f;

    std::ostringstream hdr;
    hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    hdr << "<g transform='translate(" << cx << " " << cy << ") scale(" << scale << ")'>";
    const std::string header = hdr.str();

    // PASS 1 (Low-Elevation Spade Bevel Rim & Sunken Inner Sanctum Basin, z = 0.28, bulge = 0.58):
    // Sits visibly lower than the surrounding gold cartouche (z = 1.32) just like in the reference image!
    currentElevation_ = 0.28f;
    currentBulge_ = 0.58f;
    {
        std::ostringstream ss;
        ss << header;
        std::string spadePath =
            "M 0 -100 "
            "C 22 -62 65 -36 78 4 "
            "C 86 30 76 56 50 62 "
            "C 34 66 22 56 16 42 "
            "C 14 58 18 80 32 94 "
            "L -32 94 "
            "C -18 80 -14 58 -16 42 "
            "C -22 56 -34 66 -50 62 "
            "C -76 56 -86 30 -78 4 "
            "C -65 -36 -22 -62 0 -100 Z";

        ss << "<path d='" << spadePath << "' fill='" << bodyHex << "' stroke='" << deepHex << "' stroke-width='7.5' stroke-linejoin='round'/>";
        ss << "<path d='" << spadePath << "' fill='none' stroke='" << filigreeHex << "' stroke-width='4.8' stroke-linejoin='round'/>";
        ss << "<path d='" << spadePath << "' fill='none' stroke='" << accHex << "' stroke-width='1.8' stroke-linejoin='round'/>";

        std::string innerSpadePath =
            "M 0 -87 "
            "C 18 -53 54 -30 66 4 "
            "C 72 24 64 46 43 52 "
            "C 29 56 19 46 13 34 "
            "C 10 51 14 72 24 85 "
            "L -24 85 "
            "C -14 72 -10 51 -13 34 "
            "C -19 46 -29 56 -43 52 "
            "C -64 46 -72 24 -66 4 "
            "C -54 -30 -18 -53 0 -87 Z";
        ss << "<path d='" << innerSpadePath << "' fill='" << deepHex << "' stroke='" << deepHex << "' stroke-width='3.2' stroke-linejoin='round'/>";
        ss << "<path d='" << innerSpadePath << "' fill='none' stroke='" << filigreeHex << "' stroke-width='1.8' stroke-linejoin='round'/>";
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    // PASS 2 (Carved Inner Filigree Scrollwork & Tree-of-Life Spine, z = 0.42, bulge = 0.74):
    // Raised relative to the sunken spade sanctum (0.28), yet still nestled below the gold cartouche (1.32)!
    currentElevation_ = 0.42f;
    currentBulge_ = 0.74f;
    {
        std::ostringstream ss;
        ss << header;

        // Central Tree of Life / Tiered Baroque Palmette Spine
        ss << "<path d='M 0 -80 L 0 42' stroke='" << deepHex << "' stroke-width='5.8' stroke-linecap='round'/>";
        ss << "<path d='M 0 -80 L 0 42' stroke='" << filigreeHex << "' stroke-width='3.6' stroke-linecap='round'/>";
        ss << "<path d='M 0 -80 L 0 42' stroke='" << accHex << "' stroke-width='1.5' stroke-linecap='round'/>";

        ss << "<path d='M 0 -82 C -10 -66 -9 -52 0 -44 C 9 -52 10 -66 0 -82 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
        ss << "<path d='M 0 -78 L 0 -48' stroke='" << accHex << "' stroke-width='1.6' stroke-linecap='round'/>";
        ss << "<path d='M 0 -50 C -13 -59 -24 -50 -19 -40 C -13 -38 -5 -42 0 -44 C 5 -42 13 -38 19 -40 C 24 -50 13 -59 0 -50 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
        ss << "<path d='M 0 -20 C -15 -31 -28 -22 -23 -10 C -15 -8 -6 -12 0 -14 C 6 -12 15 -8 23 -10 C 28 -22 15 -31 0 -20 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";

        auto drawLobeScrollwork = [&](float xDir) {
            ss << "<g transform='scale(" << xDir << " 1)'>";

            ss << "<path d='M 4 -55 C 19 -57 36 -43 38 -26 C 31 -22 22 -30 18 -39 C 14 -45 8 -49 4 -55 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
            ss << "<path d='M 6 -52 Q 25 -47 34 -28' fill='none' stroke='" << accHex << "' stroke-width='1.5' stroke-linecap='round'/>";

            ss << "<path d='M 4 -35 C 29 -47 60 -25 60 7 C 60 35 35 47 16 33 C 26 35 43 25 45 5 C 47 -15 27 -31 4 -35 Z' "
               << "fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.3' stroke-linejoin='round'/>";

            ss << "<path d='M 24 -39 C 39 -43 54 -31 56 -16 C 47 -16 37 -25 24 -39 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.1'/>";
            ss << "<path d='M 28 -35 Q 43 -33 52 -18' fill='none' stroke='" << accHex << "' stroke-width='1.4' stroke-linecap='round'/>";

            ss << "<path d='M 51 -16 C 63 -4 65 16 55 30 C 48 19 48 3 51 -16 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.1'/>";
            ss << "<path d='M 53 -10 Q 59 8 53 24' fill='none' stroke='" << accHex << "' stroke-width='1.4' stroke-linecap='round'/>";

            ss << "<path d='M 2 -33 C 27 -45 57 -23 53 9 C 49 35 24 41 12 25 C 6 15 14 2 26 6 C 34 10 34 22 26 24 C 20 26 16 20 20 15' "
               << "fill='none' stroke='" << deepHex << "' stroke-width='5.2' stroke-linecap='round'/>";
            ss << "<path d='M 2 -33 C 27 -45 57 -23 53 9 C 49 35 24 41 12 25 C 6 15 14 2 26 6 C 34 10 34 22 26 24 C 20 26 16 20 20 15' "
               << "fill='none' stroke='" << filigreeHex << "' stroke-width='3.5' stroke-linecap='round'/>";
            ss << "<path d='M 4 -33 C 27 -44 56 -23 53 9 C 49 33 24 39 13 25 C 8 16 15 4 25 7' "
               << "fill='none' stroke='" << accHex << "' stroke-width='1.6' stroke-linecap='round'/>";
            ss << "<circle cx='23' cy='16' r='3.5' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.0'/>";
            ss << "<circle cx='23' cy='16' r='1.7' fill='" << accHex << "'/>";

            ss << "<path d='M 14 31 C 22 45 38 49 48 41 C 54 35 50 25 42 27 C 36 29 38 37 44 36' "
               << "fill='none' stroke='" << filigreeHex << "' stroke-width='2.6' stroke-linecap='round'/>";
            ss << "<path d='M 16 33 C 22 43 36 47 46 40' fill='none' stroke='" << accHex << "' stroke-width='1.2' stroke-linecap='round'/>";

            ss << "<path d='M 4 38 C 8 56 12 72 22 83 L 8 83 C 6 68 4 52 4 38 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.1'/>";
            ss << "<path d='M 6 44 Q 10 66 18 81' fill='none' stroke='" << accHex << "' stroke-width='1.3' stroke-linecap='round'/>";

            ss << "</g>";
        };
        drawLobeScrollwork(1.0f);
        drawLobeScrollwork(-1.0f);

        ss << "<circle cx='0' cy='-33' r='4.8' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
        ss << "<circle cx='0' cy='-33' r='2.2' fill='" << accHex << "'/>";
        ss << "<circle cx='0' cy='-3' r='5.2' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.2'/>";
        ss << "<circle cx='0' cy='-3' r='2.4' fill='" << accHex << "'/>";
        ss << "<path d='M 0 12 C -6 26 -8 54 -10 81 L 10 81 C 8 54 6 26 0 12 Z' fill='" << filigreeHex << "' stroke='" << deepHex << "' stroke-width='1.1'/>";
        ss << "<path d='M 0 18 L 0 77' stroke='" << accHex << "' stroke-width='1.6' stroke-linecap='round'/>";
        ss << "<line x1='-24' y1='85' x2='24' y2='85' stroke='" << accHex << "' stroke-width='1.7' stroke-linecap='round'/>";

        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
}

void CardCanvas::drawBaroqueUnderFoliage(float cx, float cy, float rx, float ry, uint32_t foliageColor, uint32_t highlightColor) {
    if (rx <= 4.0f || ry <= 4.0f) return;
    if (foliageColor == 0) foliageColor = 0xFF232730;
    if (highlightColor == 0) highlightColor = 0xFF3D4452;

    std::string baseHex, deepHex, ridgeHex;
    computeBaroquePalette(foliageColor, highlightColor, baseHex, deepHex, ridgeHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    // Low-Relief Tier (z = 0.24, bulge = 0.52): sits deep behind the gold scrolls in the 4 quadrants
    currentElevation_ = 0.24f;
    currentBulge_ = 0.52f;

    std::ostringstream ss;
    ss << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
       << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    ss << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
       << " " << state_.tx << " " << state_.ty << ")'>";
    ss << "<g transform='translate(" << cx << " " << cy << ")'>";

    auto drawQuadrantPlumes = [&](float sxSign, float sySign) {
        ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";

        // 1. Diagonal fanning charcoal acanthus frond sweeping from behind the cartouche shoulder toward the corner
        ss << "<path d='M " << (rx * 0.28f) << " " << (-ry * 0.62f)
           << " C " << (rx * 0.52f) << " " << (-ry * 1.12f) << " " << (rx * 0.96f) << " " << (-ry * 1.28f) << " " << (rx * 1.14f) << " " << (-ry * 1.02f)
           << " C " << (rx * 0.96f) << " " << (-ry * 0.88f) << " " << (rx * 0.64f) << " " << (-ry * 0.72f) << " " << (rx * 0.28f) << " " << (-ry * 0.62f) << " Z' "
           << "fill='" << baseHex << "' stroke='" << deepHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
        ss << "<path d='M " << (rx * 0.36f) << " " << (-ry * 0.68f)
           << " Q " << (rx * 0.76f) << " " << (-ry * 1.02f) << " " << (rx * 1.06f) << " " << (-ry * 1.00f) << "' "
           << "fill='none' stroke='" << ridgeHex << "' stroke-width='1.8' stroke-linecap='round'/>";

        // 2. Secondary serrated acanthus lobe fanning steeper toward the upper crown pocket
        ss << "<path d='M " << (rx * 0.16f) << " " << (-ry * 0.74f)
           << " C " << (rx * 0.28f) << " " << (-ry * 1.24f) << " " << (rx * 0.68f) << " " << (-ry * 1.42f) << " " << (rx * 0.82f) << " " << (-ry * 1.16f)
           << " C " << (rx * 0.62f) << " " << (-ry * 1.02f) << " " << (rx * 0.38f) << " " << (-ry * 0.86f) << " " << (rx * 0.16f) << " " << (-ry * 0.74f) << " Z' "
           << "fill='" << baseHex << "' stroke='" << deepHex << "' stroke-width='1.4' stroke-linejoin='round'/>";
        ss << "<path d='M " << (rx * 0.22f) << " " << (-ry * 0.82f)
           << " Q " << (rx * 0.48f) << " " << (-ry * 1.20f) << " " << (rx * 0.76f) << " " << (-ry * 1.15f) << "' "
           << "fill='none' stroke='" << ridgeHex << "' stroke-width='1.6' stroke-linecap='round'/>";

        // 3. Lower lateral acanthus frond fanning toward the outer border
        ss << "<path d='M " << (rx * 0.48f) << " " << (-ry * 0.46f)
           << " C " << (rx * 0.86f) << " " << (-ry * 0.82f) << " " << (rx * 1.26f) << " " << (-ry * 0.86f) << " " << (rx * 1.32f) << " " << (-ry * 0.58f)
           << " C " << (rx * 1.08f) << " " << (-ry * 0.50f) << " " << (rx * 0.78f) << " " << (-ry * 0.46f) << " " << (rx * 0.48f) << " " << (-ry * 0.46f) << " Z' "
           << "fill='" << baseHex << "' stroke='" << deepHex << "' stroke-width='1.4' stroke-linejoin='round'/>";
        ss << "<path d='M " << (rx * 0.56f) << " " << (-ry * 0.50f)
           << " Q " << (rx * 0.98f) << " " << (-ry * 0.72f) << " " << (rx * 1.24f) << " " << (-ry * 0.59f) << "' "
           << "fill='none' stroke='" << ridgeHex << "' stroke-width='1.6' stroke-linecap='round'/>";

        ss << "</g>";
    };
    drawQuadrantPlumes(1.0f, 1.0f);
    drawQuadrantPlumes(-1.0f, 1.0f);
    drawQuadrantPlumes(1.0f, -1.0f);
    drawQuadrantPlumes(-1.0f, -1.0f);

    ss << "</g></g></svg>";
    rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
}

void CardCanvas::drawAcanthusScroll(float cx, float cy, float radius, float angleStart, float angleEnd, uint32_t color, uint32_t /*strokeColor*/) {
    if (radius <= 2.0f) return;
    if (color == 0) color = 0xFFD4AF37;
    drawVolute(cx, cy, radius, std::abs(angleEnd - angleStart) / (2.0f * kPi), angleStart, color, 3.2f, "fluted_ridge");
}

void CardCanvas::drawBaroqueArch(float cx, float cy, float rx, float ry, const std::string& style,
                                 uint32_t goldColor, uint32_t accentColor, float lineWidth) {
    if (rx <= 8.0f || ry <= 8.0f) return;
    if (goldColor == 0) goldColor = 0xFFC5A869;
    if (accentColor == 0) accentColor = 0xFFFFF3D1;

    std::string goldHex, deepHex, accHex;
    computeBaroquePalette(goldColor, accentColor, goldHex, deepHex, accHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    const uint8_t prevMat = currentMaterial_;
    currentMaterial_ = 3; // Gold/bronze

    std::ostringstream hdr;
    hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    hdr << "<g transform='translate(" << cx << " " << cy << ")'>";
    const std::string header = hdr.str();

    if (style == "ogee_spires") {
        // Pointed Gothic-Baroque Ogee / Lozenge Arch (Hearts A-10 & Diamonds A-10)
        // PASS 1 (Mid-Relief Spire Pinnacles & Inner Molded Lozenge Track, z = 0.72, bulge = 0.92)
        currentElevation_ = 0.72f;
        currentBulge_ = 0.92f;
        {
            std::ostringstream ss;
            ss << header;
            auto drawQuadrantSpires = [&](float sxSign, float sySign) {
                ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";
                // Vertical Gothic-Baroque Spire Pinnacles rising from the shoulder & waist
                ss << "<path d='M " << (rx * 0.44f) << " " << (-ry * 0.62f)
                   << " L " << (rx * 0.50f) << " " << (-ry * 1.04f)
                   << " L " << (rx * 0.56f) << " " << (-ry * 0.56f) << " Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
                ss << "<line x1='" << (rx * 0.50f) << "' y1='" << (-ry * 0.98f) << "' x2='" << (rx * 0.50f) << "' y2='" << (-ry * 0.62f) << "' stroke='" << accHex << "' stroke-width='1.5'/>";
                ss << "<circle cx='" << (rx * 0.50f) << "' cy='" << (-ry * 1.05f) << "' r='2.4' fill='" << accHex << "' stroke='" << deepHex << "' stroke-width='0.9'/>";

                // Outer shoulder spire
                ss << "<path d='M " << (rx * 0.76f) << " " << (-ry * 0.36f)
                   << " L " << (rx * 0.84f) << " " << (-ry * 0.78f)
                   << " L " << (rx * 0.90f) << " " << (-ry * 0.28f) << " Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.3'/>";
                ss << "<line x1='" << (rx * 0.84f) << "' y1='" << (-ry * 0.72f) << "' x2='" << (rx * 0.83f) << "' y2='" << (-ry * 0.36f) << "' stroke='" << accHex << "' stroke-width='1.3'/>";
                ss << "<circle cx='" << (rx * 0.84f) << "' cy='" << (-ry * 0.79f) << "' r='2.1' fill='" << accHex << "' stroke='" << deepHex << "' stroke-width='0.8'/>";

                // Acanthus leaf sheath hugging the ogee curve
                ss << "<path d='M " << (rx * 0.14f) << " " << (-ry * 0.88f)
                   << " C " << (rx * 0.34f) << " " << (-ry * 0.96f) << " " << (rx * 0.64f) << " " << (-ry * 0.74f) << " " << (rx * 0.72f) << " " << (-ry * 0.48f)
                   << " C " << (rx * 0.52f) << " " << (-ry * 0.56f) << " " << (rx * 0.30f) << " " << (-ry * 0.72f) << " " << (rx * 0.14f) << " " << (-ry * 0.88f) << " Z' "
                   << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
                ss << "<path d='M " << (rx * 0.20f) << " " << (-ry * 0.84f) << " Q " << (rx * 0.46f) << " " << (-ry * 0.78f) << " " << (rx * 0.66f) << " " << (-ry * 0.52f) << "' fill='none' stroke='" << accHex << "' stroke-width='1.8'/>";
                ss << "</g>";
            };
            drawQuadrantSpires(1.0f, 1.0f);
            drawQuadrantSpires(-1.0f, 1.0f);
            drawQuadrantSpires(1.0f, -1.0f);
            drawQuadrantSpires(-1.0f, -1.0f);

            // Inner pointed ogee lozenge molding
            float irx = rx * 0.84f, iry = ry * 0.86f;
            std::ostringstream ogeeIn;
            ogeeIn << "M 0 " << -iry
                   << " C " << (irx * 0.28f) << " " << (-iry * 0.68f) << " " << (irx * 0.68f) << " " << (-iry * 0.32f) << " " << irx << " 0"
                   << " C " << (irx * 0.68f) << " " << (iry * 0.32f) << " " << (irx * 0.28f) << " " << (iry * 0.68f) << " 0 " << iry
                   << " C " << (-irx * 0.28f) << " " << (iry * 0.68f) << " " << (-irx * 0.68f) << " " << (iry * 0.32f) << " " << -irx << " 0"
                   << " C " << (-irx * 0.68f) << " " << (-iry * 0.32f) << " " << (-irx * 0.28f) << " " << (-iry * 0.68f) << " 0 " << -iry << " Z";
            ss << "<path d='" << ogeeIn.str() << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 2.2f) << "' stroke-linejoin='round'/>";
            ss << "<path d='" << ogeeIn.str() << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 1.4f) << "' stroke-linejoin='round'/>";
            ss << "<path d='" << ogeeIn.str() << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.55f) << "' stroke-linejoin='round'/>";
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }

        // PASS 2 (High-Relief Bulging C-Scroll Wings & Apex Crown Finials, z = 1.35, bulge = 1.58)
        currentElevation_ = 1.35f;
        currentBulge_ = 1.58f;
        {
            std::ostringstream ss;
            ss << header;
            auto drawOgeeWings = [&](float sxSign, float sySign) {
                ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";
                // Upper sweeping S-volute from apex to shoulder
                std::ostringstream s1;
                s1 << "M 0 " << (-ry * 0.94f)
                   << " C " << (rx * 0.28f) << " " << (-ry * 0.92f) << " " << (rx * 0.68f) << " " << (-ry * 0.68f) << " " << (rx * 0.72f) << " " << (-ry * 0.42f)
                   << " C " << (rx * 0.74f) << " " << (-ry * 0.26f) << " " << (rx * 0.54f) << " " << (-ry * 0.24f) << " " << (rx * 0.56f) << " " << (-ry * 0.36f);
                ss << "<path d='" << s1.str() << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 2.8f) << "' stroke-linecap='round'/>";
                ss << "<path d='" << s1.str() << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 1.9f) << "' stroke-linecap='round'/>";
                ss << "<path d='" << s1.str() << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.80f) << "' stroke-linecap='round'/>";

                // Massive waist C-scroll wing flaring outward
                std::ostringstream s2;
                s2 << "M " << (rx * 0.62f) << " " << (-ry * 0.44f)
                   << " C " << (rx * 0.98f) << " " << (-ry * 0.48f) << " " << (rx * 1.16f) << " " << (-ry * 0.18f) << " " << (rx * 0.96f) << " " << (-ry * 0.03f)
                   << " C " << (rx * 0.82f) << " " << (ry * 0.04f) << " " << (rx * 0.72f) << " " << (-ry * 0.08f) << " " << (rx * 0.80f) << " " << (-ry * 0.16f);
                ss << "<path d='" << s2.str() << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 3.0f) << "' stroke-linecap='round'/>";
                ss << "<path d='" << s2.str() << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 2.05f) << "' stroke-linecap='round'/>";
                ss << "<path d='" << s2.str() << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.85f) << "' stroke-linecap='round'/>";

                // Scroll-eye terminal bosses
                ss << "<circle cx='" << (rx * 0.57f) << "' cy='" << (-ry * 0.35f) << "' r='" << (lineWidth * 1.65f) << "' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.3'/>";
                ss << "<circle cx='" << (rx * 0.56f) << "' cy='" << (-ry * 0.36f) << "' r='" << (lineWidth * 0.75f) << "' fill='" << accHex << "'/>";
                ss << "<circle cx='" << (rx * 0.81f) << "' cy='" << (-ry * 0.15f) << "' r='" << (lineWidth * 1.85f) << "' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
                ss << "<circle cx='" << (rx * 0.80f) << "' cy='" << (-ry * 0.16f) << "' r='" << (lineWidth * 0.85f) << "' fill='" << accHex << "'/>";
                ss << "</g>";
            };
            drawOgeeWings(1.0f, 1.0f);
            drawOgeeWings(-1.0f, 1.0f);
            drawOgeeWings(1.0f, -1.0f);
            drawOgeeWings(-1.0f, -1.0f);

            // Central Top & Bottom Spire Finials
            for (float yDir : {-1.0f, 1.0f}) {
                ss << "<g transform='scale(1 " << yDir << ")'>";
                ss << "<path d='M 0 " << (-ry * 1.14f) << " C -9 " << (-ry * 1.00f) << " -8 " << (-ry * 0.88f) << " 0 " << (-ry * 0.80f)
                   << " C 8 " << (-ry * 0.88f) << " 9 " << (-ry * 1.00f) << " 0 " << (-ry * 1.14f) << " Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
                ss << "<line x1='0' y1='" << (-ry * 1.10f) << "' x2='0' y2='" << (-ry * 0.83f) << "' stroke='" << accHex << "' stroke-width='1.8'/>";
                ss << "<circle cx='0' cy='" << (-ry * 0.92f) << "' r='4.8' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.3'/>";
                ss << "<circle cx='-0.5' cy='" << (-ry * 0.92f - 0.5f) << "' r='2.2' fill='" << accHex << "'/>";
                ss << "</g>";
            }
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
    } else if (style == "imperial_spade_ace") {
        // Frameless Monumental Rocaille Cartouche for Ace of Spades (A♠)
        drawBaroqueUnderFoliage(cx, cy, rx * 0.95f, ry * 0.92f, 0xFF2B2620, goldColor);
        drawBaroqueCartouche(cx, cy, rx * 0.92f, ry * 0.86f, goldColor, accentColor, lineWidth * 1.15f);
        drawBaroqueSpade(cx, cy, std::min(rx, ry) * 1.38f, 0xFF221F1B, goldColor, accentColor);
    } else {
        // Default: "oval_clasp" (Spades 2-K, Clubs A-K, Red Courts & Jokers)
        // Concentric double-molded elliptical tracks bound by 4 cardinal acanthus clasps
        // PASS 1 (Inner & Outer Molded Oval Tracks + Acanthus Garland Sheaths, z = 0.76, bulge = 0.95)
        currentElevation_ = 0.76f;
        currentBulge_ = 0.95f;
        {
            std::ostringstream ss;
            ss << header;

            float outRx = rx, outRy = ry;
            float inRx = rx * 0.85f, inRy = ry * 0.86f;
            float midRx = rx * 0.925f, midRy = ry * 0.93f;

            // Recessed dark groove between inner and outer oval tracks
            ss << "<ellipse cx='0' cy='0' rx='" << midRx << "' ry='" << midRy
               << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (rx * 0.16f) << "'/>";

            // Outer molded oval track
            ss << "<ellipse cx='0' cy='0' rx='" << outRx << "' ry='" << outRy
               << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 2.5f) << "'/>";
            ss << "<ellipse cx='0' cy='0' rx='" << outRx << "' ry='" << outRy
               << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 1.65f) << "'/>";
            ss << "<ellipse cx='0' cy='0' rx='" << outRx << "' ry='" << outRy
               << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.65f) << "'/>";

            // Inner molded oval track
            ss << "<ellipse cx='0' cy='0' rx='" << inRx << "' ry='" << inRy
               << "' fill='none' stroke='" << deepHex << "' stroke-width='" << (lineWidth * 2.3f) << "'/>";
            ss << "<ellipse cx='0' cy='0' rx='" << inRx << "' ry='" << inRy
               << "' fill='none' stroke='" << goldHex << "' stroke-width='" << (lineWidth * 1.50f) << "'/>";
            ss << "<ellipse cx='0' cy='0' rx='" << inRx << "' ry='" << inRy
               << "' fill='none' stroke='" << accHex << "' stroke-width='" << (lineWidth * 0.60f) << "'/>";

            // 32 Beaded pearls seated inside the mid-channel
            constexpr int kPearls = 32;
            for (int p = 0; p < kPearls; ++p) {
                float a = static_cast<float>(p) / static_cast<float>(kPearls) * 2.0f * kPi;
                float px = std::cos(a) * midRx;
                float py = std::sin(a) * midRy;
                ss << "<circle cx='" << px << "' cy='" << py << "' r='2.1' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='0.7'/>";
                ss << "<circle cx='" << (px - 0.5f) << "' cy='" << (py - 0.5f) << "' r='0.9' fill='" << accHex << "'/>";
            }

            // 4-Quadrant Acanthus Sheaths wrapping the oval shoulders
            auto drawShoulderLeaves = [&](float sxSign, float sySign) {
                ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";
                ss << "<path d='M " << (rx * 0.24f) << " " << (-ry * 0.96f)
                   << " C " << (rx * 0.52f) << " " << (-ry * 1.06f) << " " << (rx * 0.84f) << " " << (-ry * 0.84f) << " " << (rx * 0.92f) << " " << (-ry * 0.52f)
                   << " C " << (rx * 0.78f) << " " << (-ry * 0.62f) << " " << (rx * 0.52f) << " " << (-ry * 0.84f) << " " << (rx * 0.24f) << " " << (-ry * 0.96f) << " Z' "
                   << "fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
                ss << "<path d='M " << (rx * 0.30f) << " " << (-ry * 0.94f)
                   << " Q " << (rx * 0.64f) << " " << (-ry * 0.90f) << " " << (rx * 0.86f) << " " << (-ry * 0.58f) << "' fill='none' stroke='" << accHex << "' stroke-width='1.8'/>";
                ss << "</g>";
            };
            drawShoulderLeaves(1.0f, 1.0f);
            drawShoulderLeaves(-1.0f, 1.0f);
            drawShoulderLeaves(1.0f, -1.0f);
            drawShoulderLeaves(-1.0f, -1.0f);

            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }

        // PASS 2 (High-Relief 4 Cardinal Acanthus Clasps & C-Scroll Volutes, z = 1.32, bulge = 1.55)
        currentElevation_ = 1.32f;
        currentBulge_ = 1.55f;
        {
            std::ostringstream ss;
            ss << header;
            // North & South Crown Clasps
            for (float ySign : {-1.0f, 1.0f}) {
                ss << "<g transform='scale(1 " << ySign << ")'>";
                // Central clasp shell binding inner & outer oval tracks
                ss << "<path d='M 0 " << (-ry * 1.10f) << " C -18 " << (-ry * 1.04f) << " -24 " << (-ry * 0.86f) << " 0 " << (-ry * 0.78f)
                   << " C 24 " << (-ry * 0.86f) << " 18 " << (-ry * 1.04f) << " 0 " << (-ry * 1.10f) << " Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
                // Paired outward C-scroll volutes
                for (float xSign : {-1.0f, 1.0f}) {
                    ss << "<g transform='scale(" << xSign << " 1)'>";
                    ss << "<path d='M 0 " << (-ry * 0.84f) << " C 14 " << (-ry * 1.04f) << " 38 " << (-ry * 1.04f) << " 44 " << (-ry * 0.90f)
                       << " C 48 " << (-ry * 0.80f) << " 36 " << (-ry * 0.74f) << " 30 " << (-ry * 0.82f) << "' fill='none' stroke='" << deepHex << "' stroke-width='5.2' stroke-linecap='round'/>";
                    ss << "<path d='M 0 " << (-ry * 0.84f) << " C 14 " << (-ry * 1.04f) << " 38 " << (-ry * 1.04f) << " 44 " << (-ry * 0.90f)
                       << " C 48 " << (-ry * 0.80f) << " 36 " << (-ry * 0.74f) << " 30 " << (-ry * 0.82f) << "' fill='none' stroke='" << goldHex << "' stroke-width='3.5' stroke-linecap='round'/>";
                    ss << "<path d='M 2 " << (-ry * 0.86f) << " C 14 " << (-ry * 1.02f) << " 36 " << (-ry * 1.02f) << " 42 " << (-ry * 0.90f) << "' fill='none' stroke='" << accHex << "' stroke-width='1.5' stroke-linecap='round'/>";
                    ss << "<circle cx='32' cy='" << (-ry * 0.82f) << "' r='3.6' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.1'/>";
                    ss << "<circle cx='31.5' cy='" << (-ry * 0.82f - 0.5f) << "' r='1.6' fill='" << accHex << "'/>";
                    ss << "</g>";
                }
                ss << "<circle cx='0' cy='" << (-ry * 0.93f) << "' r='5.2' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.4'/>";
                ss << "<circle cx='-0.6' cy='" << (-ry * 0.93f - 0.6f) << "' r='2.4' fill='" << accHex << "'/>";
                ss << "</g>";
            }

            // East & West Lateral Clasps
            for (float xSign : {-1.0f, 1.0f}) {
                ss << "<g transform='scale(" << xSign << " 1)'>";
                ss << "<path d='M " << (rx * 0.78f) << " 0 C " << (rx * 0.86f) << " -22 " << (rx * 1.04f) << " -18 " << (rx * 1.10f) << " 0"
                   << " C " << (rx * 1.04f) << " 18 " << (rx * 0.86f) << " 22 " << (rx * 0.78f) << " 0 Z' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.5'/>";
                for (float ySign : {-1.0f, 1.0f}) {
                    ss << "<g transform='scale(1 " << ySign << ")'>";
                    ss << "<path d='M " << (rx * 0.84f) << " 0 C " << (rx * 1.04f) << " -12 " << (rx * 1.08f) << " -32 " << (rx * 0.94f) << " -38"
                       << " C " << (rx * 0.84f) << " -42 " << (rx * 0.78f) << " -32 " << (rx * 0.86f) << " -26' fill='none' stroke='" << deepHex << "' stroke-width='4.8' stroke-linecap='round'/>";
                    ss << "<path d='M " << (rx * 0.84f) << " 0 C " << (rx * 1.04f) << " -12 " << (rx * 1.08f) << " -32 " << (rx * 0.94f) << " -38"
                       << " C " << (rx * 0.84f) << " -42 " << (rx * 0.78f) << " -32 " << (rx * 0.86f) << " -26' fill='none' stroke='" << goldHex << "' stroke-width='3.2' stroke-linecap='round'/>";
                    ss << "<circle cx='" << (rx * 0.86f) << "' cy='-27' r='3.4' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.0'/>";
                    ss << "<circle cx='" << (rx * 0.86f - 0.5f) << "' cy='-27.5' r='1.5' fill='" << accHex << "'/>";
                    ss << "</g>";
                }
                ss << "<circle cx='" << (rx * 0.93f) << "' cy='0' r='5.0' fill='" << goldHex << "' stroke='" << deepHex << "' stroke-width='1.3'/>";
                ss << "<circle cx='" << (rx * 0.93f - 0.5f) << "' cy='-0.5' r='2.3' fill='" << accHex << "'/>";
                ss << "</g>";
            }
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
    currentMaterial_ = prevMat;
}

void CardCanvas::drawGemPip(Suit suit, float cx, float cy, float size, uint32_t gemColor,
                            uint32_t goldColor, const std::string& style) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, suit, cx, cy, size, gemColor, goldColor, style]() {
            drawGemPip(suit, cx, cy, size, gemColor, goldColor, style);
        });
    }
    if (size <= 2.0f) return;
    if (goldColor == 0) goldColor = 0xFFC5A869;
    if (gemColor == 0) {
        gemColor = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFA80C18 : 0xFF101216;
    }

    std::string goldHex, deepGoldHex, accGoldHex;
    computeBaroquePalette(goldColor, 0xFFFFF3D1, goldHex, deepGoldHex, accGoldHex);
    std::string gemHex = formatColorHex(gemColor);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    const uint8_t prevMat = currentMaterial_;

    std::string effStyle = style;
    if (effStyle == "auto" || effStyle.empty()) {
        switch (suit) {
            case Suit::Hearts:   effStyle = "cabochon_gem"; break;
            case Suit::Diamonds: effStyle = "faceted_gem"; break;
            case Suit::Clubs:    effStyle = "cloisonne_onyx"; break;
            case Suit::Spades:   effStyle = "chiseled_bronze"; break;
        }
    }

    float r = size * 0.5f;
    std::ostringstream hdr;
    hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    hdr << "<g transform='translate(" << cx << " " << cy << ") scale(" << r << ")'>";
    const std::string header = hdr.str();

    if (effStyle == "cabochon_gem") {
        // 1. Raised Gold Collet Bezel (z = 0.88, bulge = 1.18, mat = gold)
        currentElevation_ = 0.88f;
        currentBulge_ = 1.18f;
        currentMaterial_ = 3;
        {
            std::ostringstream ss;
            ss << header;
            std::string_view p = (suit == Suit::Hearts) ? kClassicSuitPathHearts : kClassicSuitPathSpades;
            ss << "<path d='" << p << "' fill='" << deepGoldHex << "' stroke='" << deepGoldHex << "' stroke-width='0.28' stroke-linejoin='round'/>";
            ss << "<path d='" << p << "' fill='none' stroke='" << goldHex << "' stroke-width='0.18' stroke-linejoin='round'/>";
            ss << "<path d='" << p << "' fill='none' stroke='" << accGoldHex << "' stroke-width='0.07' stroke-linejoin='round'/>";
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
        // 2. Inner Translucent Cabochon Ruby Dome (z = 0.98, bulge = 1.72, mat = gem_cabochon)
        currentElevation_ = 0.98f;
        currentBulge_ = 1.72f;
        currentMaterial_ = 1;
        {
            std::ostringstream ss;
            ss << header;
            std::string_view p = (suit == Suit::Hearts) ? kClassicSuitPathHearts : kClassicSuitPathSpades;
            ss << "<g transform='scale(0.84)'>";
            ss << "<path d='" << p << "' fill='" << gemHex << "' stroke='#180204' stroke-width='0.08' stroke-linejoin='round'/>";
            ss << "</g></g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
    } else if (effStyle == "faceted_gem") {
        // 1. Raised Gold Lozenge Collet Bezel (z = 0.88, bulge = 1.15, mat = gold)
        currentElevation_ = 0.88f;
        currentBulge_ = 1.15f;
        currentMaterial_ = 3;
        {
            std::ostringstream ss;
            ss << header;
            ss << "<path d='M 0 -1.04 L 0.78 0 L 0 1.04 L -0.78 0 Z' fill='" << deepGoldHex << "' stroke='" << deepGoldHex << "' stroke-width='0.18' stroke-linejoin='round'/>";
            ss << "<path d='M 0 -1.02 L 0.76 0 L 0 1.02 L -0.76 0 Z' fill='none' stroke='" << goldHex << "' stroke-width='0.14' stroke-linejoin='round'/>";
            ss << "<path d='M 0 -1.00 L 0.74 0 L 0 1.00 L -0.74 0 Z' fill='none' stroke='" << accGoldHex << "' stroke-width='0.05' stroke-linejoin='round'/>";
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
        // 2. 4-Facet Pyramid-Cut Ruby Crystal (z = 1.00, bulge = 1.50, mat = gem_faceted)
        // We draw the 4 planar triangular facets with distinct luminance/detail steps + crisp facet ridge lines
        // so the normal extractor & wGem shader produce crisp planar facet reflections!
        currentElevation_ = 1.00f;
        currentBulge_ = 1.50f;
        currentMaterial_ = 2;
        {
            uint8_t gr = static_cast<uint8_t>((gemColor >> 16) & 0xFF);
            uint8_t gg = static_cast<uint8_t>((gemColor >> 8) & 0xFF);
            uint8_t gb = static_cast<uint8_t>(gemColor & 0xFF);
            auto toneHex = [&](float mul, int addR = 0, int addG = 0) {
                uint32_t c = (0xFFu << 24) |
                    (static_cast<uint32_t>(std::clamp(static_cast<int>(gr * mul) + addR, 0, 255)) << 16) |
                    (static_cast<uint32_t>(std::clamp(static_cast<int>(gg * mul) + addG, 0, 255)) << 8) |
                    static_cast<uint32_t>(std::clamp(static_cast<int>(gb * mul), 0, 255));
                return formatColorHex(c);
            };
            std::string fTL = toneHex(1.32f, 38, 12); // Top-left key-lit facet
            std::string fTR = toneHex(0.88f, 8, 0);   // Top-right grazing facet
            std::string fBL = toneHex(0.95f, 14, 0);  // Bottom-left grazing facet
            std::string fBR = toneHex(1.18f, 28, 4);  // Bottom-right internal refraction facet
            std::string ridgeCol = toneHex(1.45f, 55, 24);

            std::ostringstream ss;
            ss << header;
            ss << "<g transform='scale(0.84)'>";
            // 4 Triangular Pyramid Facets meeting at (0,0)
            ss << "<polygon points='0,-1 0,0 -0.74,0' fill='" << fTL << "'/>";
            ss << "<polygon points='0,-1 0.74,0 0,0' fill='" << fTR << "'/>";
            ss << "<polygon points='-0.74,0 0,0 0,1' fill='" << fBL << "'/>";
            ss << "<polygon points='0,0 0.74,0 0,1' fill='" << fBR << "'/>";
            // Inner table & facet ridge highlights
            ss << "<line x1='0' y1='-0.96' x2='0' y2='0.96' stroke='" << ridgeCol << "' stroke-width='0.045'/>";
            ss << "<line x1='-0.70' y1='0' x2='0.70' y2='0' stroke='" << ridgeCol << "' stroke-width='0.045'/>";
            ss << "<polygon points='0,-0.36 0.27,0 0,0.36 -0.27,0' fill='none' stroke='" << ridgeCol << "' stroke-width='0.035'/>";
            ss << "</g></g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
    } else if (effStyle == "cloisonne_onyx") {
        // 1. Raised Gold Cloisonné Trefoil Bezel Frame (z = 0.90, bulge = 1.22, mat = gold)
        currentElevation_ = 0.90f;
        currentBulge_ = 1.22f;
        currentMaterial_ = 3;
        {
            std::ostringstream ss;
            ss << header;
            ss << "<path d='" << kClassicSuitPathClubs << "' fill='" << deepGoldHex << "' stroke='" << deepGoldHex << "' stroke-width='0.28' stroke-linejoin='round'/>";
            ss << "<path d='" << kClassicSuitPathClubs << "' fill='" << goldHex << "' stroke='" << goldHex << "' stroke-width='0.18' stroke-linejoin='round'/>";
            ss << "<path d='" << kClassicSuitPathClubs << "' fill='none' stroke='" << accGoldHex << "' stroke-width='0.07' stroke-linejoin='round'/>";
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
        // 2. Recessed Matte-Satin Black Onyx Basin (z = 0.36, bulge = 0.32, mat = onyx)
        currentElevation_ = 0.36f;
        currentBulge_ = 0.32f;
        currentMaterial_ = 4;
        {
            std::ostringstream ss;
            ss << header;
            ss << "<g transform='scale(0.84)'>";
            ss << "<path d='" << kClassicSuitPathClubs << "' fill='#0d0e12' stroke='#050608' stroke-width='0.08' stroke-linejoin='round'/>";
            if (size >= 75.0f) {
                // Ace of Clubs internal gold filigree inlay inside the onyx trefoil
                ss << "<circle cx='0' cy='-0.46' r='0.22' fill='none' stroke='" << goldHex << "' stroke-width='0.055'/>";
                ss << "<circle cx='-0.44' cy='0.22' r='0.22' fill='none' stroke='" << goldHex << "' stroke-width='0.055'/>";
                ss << "<circle cx='0.44' cy='0.22' r='0.22' fill='none' stroke='" << goldHex << "' stroke-width='0.055'/>";
                ss << "<path d='M 0 -0.62 L 0 0.72 M -0.56 0.22 L 0.56 0.22' stroke='" << accGoldHex << "' stroke-width='0.045' stroke-linecap='round'/>";
                ss << "<circle cx='0' cy='0.05' r='0.11' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='0.03'/>";
            }
            ss << "</g></g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }
    } else {
        // "chiseled_bronze": Sculpted Bronze-Gold Spade Shield with Internal Palmette Relief
        if (size >= 75.0f) {
            drawBaroqueSpade(cx, cy, size, 0xFF221F1B, goldColor, 0xFFFFF3D1);
        } else {
            currentElevation_ = 0.86f;
            currentBulge_ = 1.15f;
            currentMaterial_ = 3;
            {
                std::ostringstream ss;
                ss << header;
                ss << "<path d='" << kClassicSuitPathSpades << "' fill='#26221c' stroke='" << deepGoldHex << "' stroke-width='0.22' stroke-linejoin='round'/>";
                ss << "<path d='" << kClassicSuitPathSpades << "' fill='none' stroke='" << goldHex << "' stroke-width='0.14' stroke-linejoin='round'/>";
                ss << "<path d='" << kClassicSuitPathSpades << "' fill='none' stroke='" << accGoldHex << "' stroke-width='0.05' stroke-linejoin='round'/>";
                // Internal embossed palmette fan & paired C-volutes
                ss << "<path d='M 0 -0.72 L 0 0.55' stroke='" << goldHex << "' stroke-width='0.09' stroke-linecap='round'/>";
                ss << "<path d='M 0 0.22 C -0.28 -0.18 -0.54 0.06 -0.44 0.34 C -0.34 0.52 -0.12 0.44 -0.16 0.28' fill='none' stroke='" << goldHex << "' stroke-width='0.085' stroke-linecap='round'/>";
                ss << "<path d='M 0 0.22 C 0.28 -0.18 0.54 0.06 0.44 0.34 C 0.34 0.52 0.12 0.44 0.16 0.28' fill='none' stroke='" << goldHex << "' stroke-width='0.085' stroke-linecap='round'/>";
                ss << "<path d='M 0 -0.22 C -0.18 -0.42 -0.34 -0.22 -0.24 -0.06' fill='none' stroke='" << accGoldHex << "' stroke-width='0.065' stroke-linecap='round'/>";
                ss << "<path d='M 0 -0.22 C 0.18 -0.42 0.34 -0.22 0.24 -0.06' fill='none' stroke='" << accGoldHex << "' stroke-width='0.065' stroke-linecap='round'/>";
                ss << "<circle cx='0' cy='-0.05' r='0.10' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='0.03'/>";
                ss << "</g></g></svg>";
                rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
            }
        }
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
    currentMaterial_ = prevMat;
}

void CardCanvas::drawPipTrellis(int rank, Suit suit, float cardW, float cardH,
                                uint32_t goldColor, uint32_t silverColor) {
    if (rank < 2 || rank > 10) return;
    if (goldColor == 0) goldColor = 0xFFC5A869;
    if (silverColor == 0) silverColor = 0xFF6E737B;

    std::string goldHex, deepGoldHex, accGoldHex;
    computeBaroquePalette(goldColor, 0xFFFFF3D1, goldHex, deepGoldHex, accGoldHex);
    std::string silvHex, deepSilvHex, accSilvHex;
    computeBaroquePalette(silverColor, 0xFFD8DDE6, silvHex, deepSilvHex, accSilvHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    const uint8_t prevMat = currentMaterial_;

    float cx = cardW * 0.5f;
    float cy = cardH * 0.5f;
    float rx = cardW * 0.31f;
    float ry = cardH * 0.33f;

    std::ostringstream hdr;
    hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
        << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
    hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
        << " " << state_.tx << " " << state_.ty << ")'>";
    hdr << "<g transform='translate(" << cx << " " << cy << ")'>";
    const std::string header = hdr.str();

    // PASS 1 (Tier 1A: Fine Silver-Pewter Hairline Arabesques in Recessed Pockets, z = 0.34, bulge = 0.55)
    if (rank >= 4) {
        currentElevation_ = 0.34f;
        currentBulge_ = 0.55f;
        currentMaterial_ = 0;
        std::ostringstream ss;
        ss << header;
        auto drawSilverWeb = [&](float sxSign, float sySign) {
            ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";
            // Delicate coiling vine arabesques bridging center to quadrant pips
            ss << "<path d='M 0 0 C " << (rx * 0.25f) << " " << (-ry * 0.08f) << " " << (rx * 0.48f) << " " << (-ry * 0.28f) << " " << (rx * 0.36f) << " " << (-ry * 0.48f)
               << " C " << (rx * 0.26f) << " " << (-ry * 0.62f) << " " << (rx * 0.08f) << " " << (-ry * 0.54f) << " " << (rx * 0.16f) << " " << (-ry * 0.42f) << "' "
               << "fill='none' stroke='" << deepSilvHex << "' stroke-width='2.6' stroke-linecap='round'/>";
            ss << "<path d='M 0 0 C " << (rx * 0.25f) << " " << (-ry * 0.08f) << " " << (rx * 0.48f) << " " << (-ry * 0.28f) << " " << (rx * 0.36f) << " " << (-ry * 0.48f)
               << " C " << (rx * 0.26f) << " " << (-ry * 0.62f) << " " << (rx * 0.08f) << " " << (-ry * 0.54f) << " " << (rx * 0.16f) << " " << (-ry * 0.42f) << "' "
               << "fill='none' stroke='" << silvHex << "' stroke-width='1.5' stroke-linecap='round'/>";

            // Secondary lateral scroll tendril
            ss << "<path d='M " << (rx * 0.18f) << " " << (-ry * 0.18f)
               << " C " << (rx * 0.56f) << " " << (-ry * 0.12f) << " " << (rx * 0.78f) << " " << (-ry * 0.32f) << " " << (rx * 0.64f) << " " << (-ry * 0.54f) << "' "
               << "fill='none' stroke='" << silvHex << "' stroke-width='1.3' stroke-linecap='round'/>";
            ss << "</g>";
        };
        drawSilverWeb(1.0f, 1.0f);
        drawSilverWeb(-1.0f, 1.0f);
        drawSilverWeb(1.0f, -1.0f);
        drawSilverWeb(-1.0f, -1.0f);
        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    // PASS 2 (Tier 1B: Sculpted Antique-Bronze Structural S-Scroll Trellis Bridges, z = 0.52, bulge = 0.82)
    currentElevation_ = 0.52f;
    currentBulge_ = 0.82f;
    currentMaterial_ = 3;
    {
        std::ostringstream ss;
        ss << header;

        auto drawQuadBridge = [&](float sxSign, float sySign) {
            ss << "<g transform='scale(" << sxSign << " " << sySign << ")'>";
            if (rank == 2) {
                // Rank 2: Open negative-space diamond window in center with inward-curling cusp volutes
                ss << "<path d='M 0 " << (-ry * 0.58f)
                   << " C " << (rx * 0.38f) << " " << (-ry * 0.52f) << " " << (rx * 0.66f) << " " << (-ry * 0.24f) << " " << (rx * 0.74f) << " 0' "
                   << "fill='none' stroke='" << deepGoldHex << "' stroke-width='4.2' stroke-linecap='round'/>";
                ss << "<path d='M 0 " << (-ry * 0.58f)
                   << " C " << (rx * 0.38f) << " " << (-ry * 0.52f) << " " << (rx * 0.66f) << " " << (-ry * 0.24f) << " " << (rx * 0.74f) << " 0' "
                   << "fill='none' stroke='" << goldHex << "' stroke-width='2.6' stroke-linecap='round'/>";
                // Cusp volute curling into the open window
                ss << "<path d='M " << (rx * 0.42f) << " " << (-ry * 0.34f)
                   << " C " << (rx * 0.22f) << " " << (-ry * 0.28f) << " " << (rx * 0.18f) << " " << (-ry * 0.12f) << " " << (rx * 0.30f) << " " << (-ry * 0.14f) << "' "
                   << "fill='none' stroke='" << goldHex << "' stroke-width='2.2' stroke-linecap='round'/>";
                if (suit == Suit::Clubs) {
                    // Horizontal ornate bridge across the waist on 2 of Clubs
                    ss << "<path d='M 0 0 L " << (rx * 0.72f) << " 0' stroke='" << goldHex << "' stroke-width='3.0'/>";
                    ss << "<path d='M " << (rx * 0.18f) << " 0 C " << (rx * 0.28f) << " -14 " << (rx * 0.46f) << " -14 " << (rx * 0.52f) << " 0' fill='none' stroke='" << accGoldHex << "' stroke-width='1.8'/>";
                }
            } else if (rank == 3) {
                // Rank 3: Vertical column of 3 pips flanked by symmetrical C-scroll brackets
                ss << "<path d='M 0 " << (-ry * 0.65f) << " L 0 0' stroke='" << goldHex << "' stroke-width='3.2'/>";
                ss << "<path d='M " << (rx * 0.18f) << " " << (-ry * 0.56f)
                   << " C " << (rx * 0.68f) << " " << (-ry * 0.52f) << " " << (rx * 0.76f) << " " << (-ry * 0.14f) << " " << (rx * 0.22f) << " " << (-ry * 0.06f) << "' "
                   << "fill='none' stroke='" << deepGoldHex << "' stroke-width='4.4' stroke-linecap='round'/>";
                ss << "<path d='M " << (rx * 0.18f) << " " << (-ry * 0.56f)
                   << " C " << (rx * 0.68f) << " " << (-ry * 0.52f) << " " << (rx * 0.76f) << " " << (-ry * 0.14f) << " " << (rx * 0.22f) << " " << (-ry * 0.06f) << "' "
                   << "fill='none' stroke='" << goldHex << "' stroke-width='2.8' stroke-linecap='round'/>";
                ss << "<path d='M " << (rx * 0.20f) << " " << (-ry * 0.54f)
                   << " C " << (rx * 0.64f) << " " << (-ry * 0.50f) << " " << (rx * 0.72f) << " " << (-ry * 0.16f) << " " << (rx * 0.24f) << " " << (-ry * 0.08f) << "' "
                   << "fill='none' stroke='" << accGoldHex << "' stroke-width='1.1' stroke-linecap='round'/>";
            } else {
                // Ranks 4-10: Interlocking S-scroll filigree cradles connecting quadrant pips to center & arch wall
                ss << "<path d='M 0 " << (-ry * 0.72f)
                   << " C " << (rx * 0.18f) << " " << (-ry * 0.72f) << " " << (rx * 0.36f) << " " << (-ry * 0.62f) << " " << (rx * 0.42f) << " " << (-ry * 0.48f)
                   << " C " << (rx * 0.48f) << " " << (-ry * 0.32f) << " " << (rx * 0.22f) << " " << (-ry * 0.16f) << " 0 0' "
                   << "fill='none' stroke='" << deepGoldHex << "' stroke-width='4.0' stroke-linecap='round'/>";
                ss << "<path d='M 0 " << (-ry * 0.72f)
                   << " C " << (rx * 0.18f) << " " << (-ry * 0.72f) << " " << (rx * 0.36f) << " " << (-ry * 0.62f) << " " << (rx * 0.42f) << " " << (-ry * 0.48f)
                   << " C " << (rx * 0.48f) << " " << (-ry * 0.32f) << " " << (rx * 0.22f) << " " << (-ry * 0.16f) << " 0 0' "
                   << "fill='none' stroke='" << goldHex << "' stroke-width='2.5' stroke-linecap='round'/>";
                ss << "<path d='M 0 " << (-ry * 0.72f)
                   << " C " << (rx * 0.18f) << " " << (-ry * 0.72f) << " " << (rx * 0.36f) << " " << (-ry * 0.62f) << " " << (rx * 0.42f) << " " << (-ry * 0.48f) << "' "
                   << "fill='none' stroke='" << accGoldHex << "' stroke-width='1.0' stroke-linecap='round'/>";

                // Outer arch anchor scroll
                ss << "<path d='M " << (rx * 0.42f) << " " << (-ry * 0.48f)
                   << " C " << (rx * 0.66f) << " " << (-ry * 0.46f) << " " << (rx * 0.78f) << " " << (-ry * 0.24f) << " " << (rx * 0.76f) << " 0' "
                   << "fill='none' stroke='" << deepGoldHex << "' stroke-width='3.8' stroke-linecap='round'/>";
                ss << "<path d='M " << (rx * 0.42f) << " " << (-ry * 0.48f)
                   << " C " << (rx * 0.66f) << " " << (-ry * 0.46f) << " " << (rx * 0.78f) << " " << (-ry * 0.24f) << " " << (rx * 0.76f) << " 0' "
                   << "fill='none' stroke='" << goldHex << "' stroke-width='2.3' stroke-linecap='round'/>";
            }
            ss << "</g>";
        };
        drawQuadBridge(1.0f, 1.0f);
        drawQuadBridge(-1.0f, 1.0f);
        drawQuadBridge(1.0f, -1.0f);
        drawQuadBridge(-1.0f, -1.0f);

        // Central rosette clasp on even ranks (4, 6, 8, 10) where no central pip sits at (0,0)
        if (rank % 2 == 0 && rank >= 4) {
            ss << "<circle cx='0' cy='0' r='6.5' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.4'/>";
            ss << "<circle cx='-0.6' cy='-0.6' r='2.8' fill='" << accGoldHex << "'/>";
        }

        ss << "</g></g></svg>";
        rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    }

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
    currentMaterial_ = prevMat;
}

void CardCanvas::drawBaroqueCourtBust(const std::string& rank, Suit suit, float cx, float cy, float rx, float ry,
                                      uint32_t goldColor, uint32_t silverColor, uint32_t gemColor) {
    if (rx <= 8.0f || ry <= 8.0f) return;
    if (goldColor == 0) goldColor = 0xFFC5A869;
    if (silverColor == 0) silverColor = 0xFFB8BDC4;
    if (gemColor == 0) gemColor = 0xFFA80C18;

    std::string goldHex, deepGoldHex, accGoldHex;
    computeBaroquePalette(goldColor, 0xFFFFF3D1, goldHex, deepGoldHex, accGoldHex);
    std::string silvHex, deepSilvHex, accSilvHex;
    computeBaroquePalette(silverColor, 0xFFF2F5FA, silvHex, deepSilvHex, accSilvHex);

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    const uint8_t prevMat = currentMaterial_;

    // 1. Suit-Specific Recessed Sanctum Backdrop inside the Oval Medallion (z = 0.18, bulge = 0.22)
    uint32_t sanctumCol = (suit == Suit::Hearts) ? 0xFF4A080E : 0xFF14161B;
    fillOval(cx, cy, rx * 0.84f, ry * 0.85f, sanctumCol);

    float sc = std::min(rx, ry) / 100.0f;

    // Helper to render one half-bust (upright when yDir = 1.0, inverted when yDir = -1.0)
    auto renderHalfBust = [&](float yDir) {
        std::ostringstream hdr;
        hdr << "<svg xmlns='http://www.w3.org/2000/svg' width='" << width_ << "' height='" << height_
            << "' viewBox='0 0 " << width_ << " " << height_ << "'>";
        hdr << "<g transform='matrix(" << state_.a << " " << state_.b << " " << state_.c << " " << state_.d
            << " " << state_.tx << " " << state_.ty << ")'>";
        hdr << "<g transform='translate(" << cx << " " << cy << ") scale(" << sc << " " << (sc * yDir) << ")'>";
        const std::string header = hdr.str();

        // PASS A (Background Sunburst Halo on Hearts / Cape & Pleated Ruff Collar, z = 0.46, bulge = 0.78)
        currentElevation_ = 0.46f;
        currentBulge_ = 0.78f;
        currentMaterial_ = 3;
        {
            std::ostringstream ss;
            ss << header;
            if (suit == Suit::Hearts) {
                // Radiant Golden Sunburst Rays behind the head (especially iconic on King/Queen of Hearts)
                for (int r = -6; r <= 6; ++r) {
                    float a = static_cast<float>(r) * 0.16f - kPi * 0.5f;
                    float x1 = std::cos(a) * 24.0f, y1 = -52.0f + std::sin(a) * 24.0f;
                    float x2 = std::cos(a) * 66.0f, y2 = -52.0f + std::sin(a) * 62.0f;
                    float xL = std::cos(a - 0.06f) * 36.0f, yL = -52.0f + std::sin(a - 0.06f) * 36.0f;
                    float xR = std::cos(a + 0.06f) * 36.0f, yR = -52.0f + std::sin(a + 0.06f) * 36.0f;
                    ss << "<polygon points='" << x1 << "," << y1 << " " << xL << "," << yL << " " << x2 << "," << y2 << " " << xR << "," << yR
                       << "' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='0.9'/>";
                }
            }
            if (rank == "Q" || rank == "q") {
                // High Renaissance Pleated Medici Fan Collar behind Queen's shoulders
                for (int p = -5; p <= 5; ++p) {
                    float a = static_cast<float>(p) * 0.19f - kPi * 0.5f;
                    float px = std::cos(a) * 44.0f;
                    float py = -44.0f + std::sin(a) * 36.0f;
                    ss << "<path d='M 0 -42 L " << (px - 6.0f) << " " << py << " Q " << px << " " << (py - 8.0f) << " " << (px + 6.0f) << " " << py << " Z' "
                       << "fill='" << silvHex << "' stroke='" << deepSilvHex << "' stroke-width='1.2'/>";
                }
            }
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }

        // PASS B (Sculpted Repoussé Armor Torso, Pauldrons & Brocade Mantle, z = 0.78, bulge = 1.15)
        currentElevation_ = 0.78f;
        currentBulge_ = 1.15f;
        currentMaterial_ = 0;
        {
            std::ostringstream ss;
            ss << header;
            // Broad torso breastplate / royal bodice tapering to waist divider (y = 0)
            ss << "<path d='M -48 0 C -52 -24 -40 -42 -22 -46 L 22 -46 C 40 -42 52 -24 48 0 Z' "
               << "fill='" << silvHex << "' stroke='" << deepSilvHex << "' stroke-width='2.0' stroke-linejoin='round'/>";
            // Articulated Shoulder Pauldrons (Left & Right)
            for (float xSign : {-1.0f, 1.0f}) {
                ss << "<g transform='scale(" << xSign << " 1)'>";
                ss << "<path d='M 20 -44 C 42 -46 58 -32 54 -12 C 44 -8 30 -16 22 -28 Z' "
                   << "fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.6'/>";
                ss << "<path d='M 24 -38 C 38 -38 48 -28 46 -16' fill='none' stroke='" << accGoldHex << "' stroke-width='1.8' stroke-linecap='round'/>";
                // Engraved breastplate volutes
                ss << "<path d='M 6 -10 C 20 -12 30 -24 24 -34 C 18 -38 10 -30 14 -24' fill='none' stroke='" << goldHex << "' stroke-width='2.2' stroke-linecap='round'/>";
                ss << "</g>";
            }
            // Central vertical armor/bodice ridge & waist belt
            ss << "<path d='M 0 -44 L 0 -2' stroke='" << goldHex << "' stroke-width='3.2' stroke-linecap='round'/>";
            ss << "<path d='M -50 0 L 50 0' stroke='" << goldHex << "' stroke-width='3.6' stroke-linecap='round'/>";
            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }

        // PASS C (High-Relief Sculpted Face, Hair/Beard Volutes, Crown & Weapons, z = 1.12, bulge = 1.45)
        currentElevation_ = 1.12f;
        currentBulge_ = 1.45f;
        currentMaterial_ = 0;
        {
            std::ostringstream ss;
            ss << header;
            // Neck column
            ss << "<rect x='-9' y='-52' width='18' height='14' rx='5' fill='" << silvHex << "' stroke='" << deepSilvHex << "' stroke-width='1.4'/>";

            // Flowing Hair Curls (Left & Right)
            for (float xSign : {-1.0f, 1.0f}) {
                ss << "<g transform='scale(" << xSign << " 1)'>";
                ss << "<path d='M 12 -74 C 24 -72 28 -58 22 -48 C 16 -42 10 -48 14 -54' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.5'/>";
                ss << "<path d='M 14 -64 C 26 -60 28 -46 20 -38' fill='none' stroke='" << accGoldHex << "' stroke-width='1.6' stroke-linecap='round'/>";
                ss << "</g>";
            }

            // Sculpted Renaissance Cranium & Facial Mask
            ss << "<path d='M -15 -72 C -16 -56 -11 -44 0 -43 C 11 -44 16 -56 15 -72 C 14 -84 -14 -84 -15 -72 Z' "
               << "fill='" << accSilvHex << "' stroke='" << deepSilvHex << "' stroke-width='1.6'/>";
            // Brow ridge, nose bridge & cheekbones (chiseled relief detail)
            ss << "<path d='M -10 -67 Q -5 -70 0 -67 Q 5 -70 10 -67' fill='none' stroke='" << deepSilvHex << "' stroke-width='1.6' stroke-linecap='round'/>";
            ss << "<ellipse cx='-5.5' cy='-64' rx='2.4' ry='1.4' fill='" << deepSilvHex << "'/>";
            ss << "<ellipse cx='5.5' cy='-64' rx='2.4' ry='1.4' fill='" << deepSilvHex << "'/>";
            ss << "<path d='M 0 -67 L -1.8 -56 L 0 -54.5 L 1.8 -56 Z' fill='" << accSilvHex << "' stroke='" << deepSilvHex << "' stroke-width='0.9'/>";

            if (rank == "K" || rank == "k") {
                // Bearded Imperial Sovereign: Lush symmetrical mustache & flowing volute beard
                ss << "<path d='M 0 -52 C -8 -54 -16 -48 -14 -38 C -10 -30 -4 -28 0 -26 C 4 -28 10 -30 14 -38 C 16 -48 8 -54 0 -52 Z' "
                   << "fill='" << silvHex << "' stroke='" << deepSilvHex << "' stroke-width='1.5'/>";
                ss << "<path d='M 0 -53 C -6 -55 -12 -52 -15 -48 M 0 -53 C 6 -55 12 -52 15 -48' fill='none' stroke='" << accSilvHex << "' stroke-width='1.8' stroke-linecap='round'/>";
                ss << "<path d='M -5 -46 C -10 -40 -8 -32 -2 -30 M 5 -46 C 10 -40 8 -32 2 -30' fill='none' stroke='" << accSilvHex << "' stroke-width='1.4' stroke-linecap='round'/>";
            } else {
                // Sculpted classical lips & chin
                ss << "<path d='M -4.5 -51 Q 0 -49.5 4.5 -51' fill='none' stroke='" << deepSilvHex << "' stroke-width='1.3' stroke-linecap='round'/>";
            }

            // Royal Crown / Tiara / Knight Plume Crest (z = 1.24)
            if (rank == "K" || rank == "k") {
                // Heavy Imperial Arched Crown
                ss << "<path d='M -18 -75 L -22 -94 L -10 -84 L 0 -98 L 10 -84 L 22 -94 L 18 -75 Z' "
                   << "fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.6' stroke-linejoin='round'/>";
                ss << "<path d='M -18 -75 Q 0 -79 18 -75' fill='none' stroke='" << accGoldHex << "' stroke-width='2.4' stroke-linecap='round'/>";
            } else if (rank == "Q" || rank == "q") {
                // Scalloped Renaissance Tiara
                ss << "<path d='M -16 -75 C -14 -88 -6 -84 0 -95 C 6 -84 14 -88 16 -75 Z' "
                   << "fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.5' stroke-linejoin='round'/>";
            } else {
                // Cavalier / Knight Visor Fillet & Plume
                ss << "<path d='M -16 -74 Q 0 -82 16 -74 L 14 -79 Q 0 -88 -14 -79 Z' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.4'/>";
            }

            // Suit-Specific Weapons & Royal Props
            if (rank == "J" || rank == "j") {
                // Halberd / Lance / Sword rising beside the right shoulder
                ss << "<line x1='38' y1='0' x2='48' y2='-92' stroke='" << goldHex << "' stroke-width='3.0' stroke-linecap='round'/>";
                ss << "<path d='M 48 -98 L 56 -78 L 46 -74 L 40 -80 Z' fill='" << accSilvHex << "' stroke='" << deepSilvHex << "' stroke-width='1.3'/>";
            } else if (rank == "K" || rank == "k") {
                // Royal Scepter
                ss << "<line x1='-38' y1='0' x2='-46' y2='-82' stroke='" << goldHex << "' stroke-width='3.2' stroke-linecap='round'/>";
                ss << "<circle cx='-46' cy='-86' r='5.5' fill='" << goldHex << "' stroke='" << deepGoldHex << "' stroke-width='1.3'/>";
            }

            ss << "</g></g></svg>";
            rasterizeSvgFragment(ss.str(), 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
        }

        // PASS D (Chest & Crown Gem Inlays using drawGemPip so they glow in 3D!)
        drawGemPip(suit, cx, cy - yDir * (22.0f * sc), 18.0f * sc, gemColor, goldColor);
        if (suit == Suit::Diamonds) {
            // Floating faceted ruby diamonds flanking the character bust (matching J♦, Q♦, K♦ in the reference sheet!)
            drawGemPip(Suit::Diamonds, cx - 54.0f * sc, cy - yDir * (56.0f * sc), 16.0f * sc, gemColor, goldColor, "faceted_gem");
            drawGemPip(Suit::Diamonds, cx + 54.0f * sc, cy - yDir * (56.0f * sc), 16.0f * sc, gemColor, goldColor, "faceted_gem");
        }
    };

    renderHalfBust(1.0f);
    renderHalfBust(-1.0f);

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
    currentMaterial_ = prevMat;
}

void CardCanvas::drawSculptedSvg(const std::string& svgXml, float x, float y, float w, float h,
                                 float zMin, float zMax, float bulge) {
    if (symmetryActive_) {
        symmetryQueue_.push_back([this, svgXml, x, y, w, h, zMin, zMax, bulge]() {
            drawSculptedSvg(svgXml, x, y, w, h, zMin, zMax, bulge);
        });
    }
    if (svgXml.empty()) return;

    std::vector<char> xmlCopy(svgXml.begin(), svgXml.end());
    xmlCopy.push_back('\0');

    NSVGimage* img = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!img) return;

    int renderW = (w > 0.0f) ? static_cast<int>(std::ceil(w)) : static_cast<int>(img->width);
    int renderH = (h > 0.0f) ? static_cast<int>(std::ceil(h)) : static_cast<int>(img->height);
    renderW = std::clamp(renderW, 1, 4096);
    renderH = std::clamp(renderH, 1, 4096);

    thread_local static NSVGrasterizer* sRast = nullptr;
    if (!sRast) {
        sRast = nsvgCreateRasterizer();
    }
    if (!sRast) {
        nsvgDelete(img);
        return;
    }

    int shapeCount = 0;
    for (NSVGshape* s = img->shapes; s != nullptr; s = s->next) {
        ++shapeCount;
    }
    if (shapeCount == 0) {
        nsvgDelete(img);
        return;
    }

    float scaleX = (img->width > 0.0f) ? (static_cast<float>(renderW) / img->width) : 1.0f;
    float scaleY = (img->height > 0.0f) ? (static_cast<float>(renderH) / img->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    thread_local static std::vector<uint8_t> sLayerBuf;
    size_t needed = static_cast<size_t>(renderW * renderH * 4);
    if (sLayerBuf.size() < needed) {
        sLayerBuf.resize(needed);
    }

    const float prevElev = currentElevation_;
    const float prevBulge = currentBulge_;
    currentBulge_ = bulge;

    NSVGshape* allShapes = img->shapes;
    int idx = 0;
    for (NSVGshape* s = allShapes; s != nullptr; ) {
        NSVGshape* nextShape = s->next;
        s->next = nullptr;
        img->shapes = s;

        float t = (shapeCount > 1) ? (static_cast<float>(idx) / static_cast<float>(shapeCount - 1)) : 0.5f;
        currentElevation_ = zMin + t * (zMax - zMin);

        std::fill(sLayerBuf.begin(), sLayerBuf.begin() + needed, 0);
        nsvgRasterize(sRast, img, 0.0f, 0.0f, scale, sLayerBuf.data(), renderW, renderH, renderW * 4);
        compositeBuffer(sLayerBuf.data(), renderW, renderH, state_.tx + state_.a * x, state_.ty + state_.d * y,
                        static_cast<float>(renderW), static_cast<float>(renderH));

        s->next = nextShape;
        s = nextShape;
        ++idx;
    }
    img->shapes = allShapes;
    nsvgDelete(img);

    currentElevation_ = prevElev;
    currentBulge_ = prevBulge;
}

void CardCanvas::applyFinish(const std::string& style, float lightAzimuth, float lightElevation,
                             float aoIntensity, float reliefDepth, uint32_t patinaColor,
                             uint32_t highlightColor, float paperGrain, float inkBleed) {
    if (rgba_.empty() || width_ <= 0 || height_ <= 0) return;

    if (style == "flat" || style == "none" || style.empty()) {
        return;
    }

    if (style == "hand_sketched") {
        const size_t totalPixels = static_cast<size_t>(width_ * height_);
        for (size_t i = 0; i < totalPixels; ++i) {
            uint8_t a = rgba_[i * 4 + 3];
            if (a == 0) continue;

            int px = static_cast<int>(i % width_);
            int py = static_cast<int>(i / width_);

            uint32_t h = static_cast<uint32_t>(px * 374761393 + py * 668265263);
            h = (h ^ (h >> 13)) * 1274126177;
            float n = static_cast<float>(h & 0xFFFF) / 65535.0f;

            float grainMod = 1.0f + (n - 0.5f) * paperGrain;
            rgba_[i * 4 + 0] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba_[i * 4 + 0]) * grainMod, 0.0f, 255.0f));
            rgba_[i * 4 + 1] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba_[i * 4 + 1]) * grainMod, 0.0f, 255.0f));
            rgba_[i * 4 + 2] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba_[i * 4 + 2]) * grainMod, 0.0f, 255.0f));

            if (inkBleed > 0.0f && a > 10 && a < 245) {
                float bleedOffset = (n - 0.5f) * inkBleed * 40.0f;
                rgba_[i * 4 + 3] = static_cast<uint8_t>(std::clamp(static_cast<float>(a) + bleedOffset, 0.0f, 255.0f));
            }
        }
        return;
    }

    if (style == "relief") {
        const int w = width_;
        const int h = height_;
        const size_t totalPixels = static_cast<size_t>(w * h);
        const float scaleFactor = std::clamp(static_cast<float>(std::min(w, h)) / 360.0f, 0.5f, 4.0f);

        if (reliefMask_.size() < totalPixels) {
            reliefMask_.assign(totalPixels, 0.0f);
            reliefLobeMask_.assign(totalPixels, 0.0f);
            reliefDetail_.assign(totalPixels, 0.0f);
            reliefElevation_.assign(totalPixels, 0.0f);
            reliefBulge_.assign(totalPixels, 0.0f);
            reliefMaterial_.assign(totalPixels, 0);
        }

        // Check whether foreground primitives were recorded in reliefMask_.
        // If not (e.g. raw buffer composite), synthesize mask from color distance to card plate.
        bool hasRecordedMask = false;
        for (size_t i = 0; i < totalPixels; i += 16) {
            if (reliefMask_[i] > 0.05f) {
                hasRecordedMask = true;
                break;
            }
        }
        if (!hasRecordedMask) {
            size_t sampleIdx = static_cast<size_t>((h / 8) * w + (w / 2));
            float bgR = static_cast<float>(rgba_[sampleIdx * 4 + 0]);
            float bgG = static_cast<float>(rgba_[sampleIdx * 4 + 1]);
            float bgB = static_cast<float>(rgba_[sampleIdx * 4 + 2]);
            for (size_t i = 0; i < totalPixels; ++i) {
                float r = static_cast<float>(rgba_[i * 4 + 0]);
                float g = static_cast<float>(rgba_[i * 4 + 1]);
                float b = static_cast<float>(rgba_[i * 4 + 2]);
                float a = static_cast<float>(rgba_[i * 4 + 3]) / 255.0f;
                float diff = (std::abs(r - bgR) + std::abs(g - bgG) + std::abs(b - bgB)) / (3.0f * 255.0f);
                float m = std::clamp(diff * 4.5f, 0.0f, 1.0f) * a;
                reliefMask_[i] = m;
                float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
                reliefLobeMask_[i] = (lum > 0.18f) ? m : 0.0f;
                reliefDetail_[i] = (lum - 0.35f) * 0.6f * m;
                reliefElevation_[i] = m * 0.75f;
                reliefBulge_[i] = m * 1.0f;
            }
        }

        thread_local static std::vector<float> lobeSeed;
        thread_local static std::vector<float> distLobe;
        thread_local static std::vector<float> distMacro;
        thread_local static std::vector<float> blur1;
        thread_local static std::vector<float> blur2;
        thread_local static std::vector<float> blur3;
        thread_local static std::vector<float> elevBlur;
        thread_local static std::vector<float> detailSmooth;
        thread_local static std::vector<float> heightRaw;
        thread_local static std::vector<float> height;
        thread_local static std::vector<float> tmpBuf;
        thread_local static std::vector<float> pingBuf;

        if (blur1.size() < totalPixels) {
            lobeSeed.resize(totalPixels);
            distLobe.resize(totalPixels);
            distMacro.resize(totalPixels);
            blur1.resize(totalPixels);
            blur2.resize(totalPixels);
            blur3.resize(totalPixels);
            elevBlur.resize(totalPixels);
            detailSmooth.resize(totalPixels);
            heightRaw.resize(totalPixels);
            height.resize(totalPixels);
            tmpBuf.resize(totalPixels);
            pingBuf.resize(totalPixels);
        }

        // Build lobeSeed from reliefLobeMask_ AND intra-layer Z-elevation cliffs:
        // Wherever a higher-elevation scroll overhangs a lower-elevation layer (z - zNbr > 0.12),
        // zero out lobeSeed along the cliff so the upper scroll domes into a full 3D cushion right at the overlap!
        for (int y = 0; y < h; ++y) {
            size_t row = static_cast<size_t>(y * w);
            size_t rowUp = static_cast<size_t>(std::max(0, y - 1) * w);
            size_t rowDn = static_cast<size_t>(std::min(h - 1, y + 1) * w);
            for (int x = 0; x < w; ++x) {
                size_t idx = row + x;
                float seed = reliefLobeMask_[idx];
                float zCur = reliefElevation_[idx];
                int xm1 = std::max(0, x - 1);
                int xp1 = std::min(w - 1, x + 1);
                if (zCur - reliefElevation_[row + xm1] > 0.12f ||
                    zCur - reliefElevation_[row + xp1] > 0.12f ||
                    zCur - reliefElevation_[rowUp + x] > 0.12f ||
                    zCur - reliefElevation_[rowDn + x] > 0.12f) {
                    seed = 0.0f;
                }
                lobeSeed[idx] = seed;
            }
        }

        // 1. Eikonal Distance-Transform Domes + Multi-Tier Elevation & Bulge Pyramid
        const float maxLobeDist = 8.5f * scaleFactor;
        const float maxMacroDist = 20.0f * scaleFactor;
        chamferDistance2Pass(lobeSeed, distLobe, w, h, maxLobeDist);
        chamferDistance2Pass(reliefMask_, distMacro, w, h, maxMacroDist);

        const int r1 = std::max(1, static_cast<int>(std::round(2.0f * scaleFactor)));
        const int r2 = std::max(2, static_cast<int>(std::round(6.0f * scaleFactor)));
        const int r3 = std::max(4, static_cast<int>(std::round(15.0f * scaleFactor)));

        separableTentBlur(lobeSeed, blur1, tmpBuf, pingBuf, w, h, r1);
        separableTentBlur(reliefMask_, blur2, tmpBuf, pingBuf, w, h, r2);
        separableTentBlur(reliefMask_, blur3, tmpBuf, pingBuf, w, h, r3);
        separableTentBlur(reliefElevation_, elevBlur, tmpBuf, pingBuf, w, h, r2);
        separableBoxBlur(reliefDetail_, detailSmooth, tmpBuf, w, h, std::max(1, static_cast<int>(std::round(1.0f * scaleFactor))));

        for (size_t i = 0; i < totalPixels; ++i) {
            float m = reliefMask_[i];
            float zElev = reliefElevation_[i];
            float bulge = reliefBulge_[i];
            float dLobe = std::clamp(distLobe[i] / maxLobeDist, 0.0f, 1.0f);
            float dMacro = std::clamp(distMacro[i] / maxMacroDist, 0.0f, 1.0f);

            // True circular / parabolic dome profile: sqrt(d * (2 - d)) rises steeply at edges and rounds over the crest
            float domeLobe = std::sqrt(std::max(0.0f, dLobe * (2.0f - dLobe)));
            float domeMacro = std::sqrt(std::max(0.0f, dMacro * (2.0f - dMacro)));

            float b1 = blur1[i];
            float b2 = blur2[i];

            float skirtBevel = 0.12f * b1;
            // Multi-tier base elevation (zElev) + bulge-scaled spherical dome inflation
            float tierBase = m * (0.42f * zElev + 0.12f * elevBlur[i]);
            float cushionVolume = m * bulge * (0.58f * domeLobe + 0.24f * domeMacro + 0.16f * b2);
            float carvedDetail = 0.50f * detailSmooth[i] + 0.26f * reliefDetail_[i];

            heightRaw[i] = skirtBevel + tierBase + cushionVolume + carvedDetail;
        }

        // Smooth the combined heightfield with a 2px tent filter for smooth C1-continuous normals
        separableTentBlur(heightRaw, height, tmpBuf, pingBuf, w, h, std::max(1, static_cast<int>(std::round(1.6f * scaleFactor))));
        // Also compute a 3px-blurred height reference in heightRaw for local curvature/ridge detection
        separableBoxBlur(height, heightRaw, tmpBuf, w, h, std::max(2, static_cast<int>(std::round(3.0f * scaleFactor))));

        // 2. Lighting Setup (Screen-space Y points down, so +sin(az) in math is -Y in screen space)
        const float azRad = lightAzimuth * (kPi / 180.0f);
        const float elRad = std::clamp(lightElevation, 12.0f, 82.0f) * (kPi / 180.0f);
        const float lx = std::cos(elRad) * std::cos(azRad);
        const float ly = -std::cos(elRad) * std::sin(azRad);
        const float lz = std::sin(elRad);

        float hx = lx, hy = ly, hz = lz + 1.0f;
        float hLen = std::sqrt(hx * hx + hy * hy + hz * hz);
        hx /= hLen; hy /= hLen; hz /= hLen;

        // Secondary rim / counter-fill light from opposite azimuth for studio 3D edge definition
        float lx2 = -lx * 0.85f;
        float ly2 = -ly * 0.85f;
        float lz2 = 0.52f;
        float l2Len = std::sqrt(lx2 * lx2 + ly2 * ly2 + lz2 * lz2);
        lx2 /= l2Len; ly2 /= l2Len; lz2 /= l2Len;

        float hx2 = lx2, hy2 = ly2, hz2 = lz2 + 1.0f;
        float h2Len = std::sqrt(hx2 * hx2 + hy2 * hy2 + hz2 * hz2);
        hx2 /= h2Len; hy2 /= h2Len; hz2 /= h2Len;

        const float rayHorizLen = std::hypot(lx, ly);
        const float rayDirX = (rayHorizLen > 1e-4f) ? (lx / rayHorizLen) : -0.707f;
        const float rayDirY = (rayHorizLen > 1e-4f) ? (ly / rayHorizLen) : -0.707f;
        const float rayTanEl = std::tan(elRad) * 0.055f / scaleFactor;

        const bool hasCustomPatina = (patinaColor != 0);
        const float patR = hasCustomPatina ? static_cast<float>((patinaColor >> 16) & 0xFF) / 255.0f : 0.04f;
        const float patG = hasCustomPatina ? static_cast<float>((patinaColor >> 8) & 0xFF) / 255.0f : 0.045f;
        const float patB = hasCustomPatina ? static_cast<float>(patinaColor & 0xFF) / 255.0f : 0.06f;

        const bool hasCustomHl = (highlightColor != 0);
        const float hlR = hasCustomHl ? static_cast<float>((highlightColor >> 16) & 0xFF) / 255.0f : 1.0f;
        const float hlG = hasCustomHl ? static_cast<float>((highlightColor >> 8) & 0xFF) / 255.0f : 0.96f;
        const float hlB = hasCustomHl ? static_cast<float>(highlightColor & 0xFF) / 255.0f : 0.86f;

        const float normalStrength = (reliefDepth * 2.85f) / scaleFactor;
        const float aoStrength = aoIntensity / 1.35f;

        // 3. Per-Pixel 3D Relief & Material-Aware PBR Shading
        for (int y = 0; y < h; ++y) {
            int ym1 = std::max(0, y - 1);
            int yp1 = std::min(h - 1, y + 1);
            int ym2 = std::max(0, y - 2);
            int yp2 = std::min(h - 1, y + 2);

            size_t rowIdx = static_cast<size_t>(y * w);
            size_t rowM1 = static_cast<size_t>(ym1 * w);
            size_t rowP1 = static_cast<size_t>(yp1 * w);
            size_t rowM2 = static_cast<size_t>(ym2 * w);
            size_t rowP2 = static_cast<size_t>(yp2 * w);

            for (int x = 0; x < w; ++x) {
                size_t idx = rowIdx + x;
                uint8_t curA = rgba_[idx * 4 + 3];
                if (curA <= 2) continue;

                int xm1 = std::max(0, x - 1);
                int xp1 = std::min(w - 1, x + 1);
                int xm2 = std::max(0, x - 2);
                int xp2 = std::min(w - 1, x + 2);

                float hCenter = height[idx];

                // Dual-scale gradient for crisp filigree details + smooth broad domes
                float gx1 = (height[rowIdx + xp1] - height[rowIdx + xm1]) * 0.5f;
                float gy1 = (height[rowP1 + x] - height[rowM1 + x]) * 0.5f;
                float gx2 = (height[rowIdx + xp2] - height[rowIdx + xm2]) * 0.25f;
                float gy2 = (height[rowP2 + x] - height[rowM2 + x]) * 0.25f;

                float dzdx = (gx1 * 0.65f + gx2 * 0.35f) * normalStrength;
                float dzdy = (gy1 * 0.65f + gy2 * 0.35f) * normalStrength;

                float nx = -dzdx;
                float ny = -dzdy;
                float nz = 1.0f;
                float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLen; ny *= invLen; nz *= invLen;

                // Local convexity / ridge curvature (+ on sculpted crests, - in carved creases)
                float localMeanH = heightRaw[idx];
                float curvature = (hCenter - localMeanH) * 6.0f;
                float ridge = std::clamp(curvature, -1.0f, 1.0f);
                float crevice = std::max(0.0f, -curvature);

                // Multi-scale Ambient Occlusion (tight crevices + elevation step overhang + base-plate contact halo)
                float m = reliefMask_[idx];
                float mLobe = lobeSeed[idx];
                float zElev = reliefElevation_[idx];
                float bulge = reliefBulge_[idx];
                float undercutCrease = m * (1.0f - mLobe) * 0.62f;
                float stepOverhangOcc = std::max(0.0f, elevBlur[idx] - zElev) * 1.45f;
                float pocketOcc = std::max(0.0f, blur2[idx] * 0.68f - hCenter) * 1.15f;
                float baseContact = (1.0f - m * 0.88f) * (0.48f * blur1[idx] + 0.36f * blur2[idx] + 0.16f * blur3[idx]);
                float aoOcc = (crevice * 0.78f + undercutCrease + stepOverhangOcc + pocketOcc + baseContact * 0.85f) * aoStrength;
                float aoVal = std::clamp(1.0f - aoOcc, 0.10f, 1.0f);

                // Ray-marched directional horizon shadow along the key light vector
                float shadowAccum = 0.0f;
                constexpr float kRaySteps[6] = {1.5f, 3.2f, 5.5f, 8.5f, 12.0f, 16.5f};
                for (float stepBase : kRaySteps) {
                    float d = stepBase * scaleFactor;
                    int sx = std::clamp(static_cast<int>(std::round(static_cast<float>(x) + rayDirX * d)), 0, w - 1);
                    int sy = std::clamp(static_cast<int>(std::round(static_cast<float>(y) + rayDirY * d)), 0, h - 1);
                    float hSample = height[static_cast<size_t>(sy * w + sx)];
                    float blocker = (hSample - hCenter) - d * rayTanEl;
                    if (blocker > 0.0f) {
                        shadowAccum = std::max(shadowAccum, std::clamp(blocker * 3.2f, 0.0f, 1.0f));
                    }
                }
                float castShadow = shadowAccum * std::clamp(aoStrength * 0.85f, 0.0f, 1.0f);

                // Base unlit color & material classification
                float r0 = static_cast<float>(rgba_[idx * 4 + 0]) / 255.0f;
                float g0 = static_cast<float>(rgba_[idx * 4 + 1]) / 255.0f;
                float b0 = static_cast<float>(rgba_[idx * 4 + 2]) / 255.0f;
                float lum0 = 0.299f * r0 + 0.587f * g0 + 0.114f * b0;
                uint8_t matTag = reliefMaterial_[idx];

                // 1) Gold / Warm Metallicity weight (preserves warm gold tone in highlights & bronze shadows)
                float warmChroma = (r0 - b0) + 0.55f * (g0 - b0);
                float wGold = std::clamp((warmChroma - 0.06f) / 0.28f, 0.0f, 1.0f) *
                              std::clamp((lum0 - 0.10f) / 0.22f, 0.0f, 1.0f) *
                              std::clamp(1.4f - std::abs(r0 - g0 * 1.26f) * 2.2f, 0.0f, 1.0f);
                if (matTag == 3) wGold = std::max(wGold, 0.92f);

                // 2) Dark Carved Onyx / Polished Obsidian weight (lets dark foregrounds gleam in 3D)
                float wDark = std::clamp((0.36f - lum0) / 0.26f, 0.0f, 1.0f) *
                              std::clamp(m * 1.4f + blur1[idx] * 0.4f, 0.0f, 1.0f);
                if (matTag == 4) wDark = std::max(wDark, 0.92f);

                // 3) Translucent Cabochon / Faceted Gemstone weight (Material Branch D: wGem)
                float redChroma = r0 - std::max(g0, b0);
                float wGem = (matTag == 1 || matTag == 2)
                    ? std::clamp(m * 1.25f, 0.0f, 1.0f)
                    : (std::clamp((redChroma - 0.28f) / 0.22f, 0.0f, 1.0f) *
                       std::clamp((bulge - 1.38f) / 0.25f, 0.0f, 1.0f) * m);
                if (wGem > 0.05f) {
                    wGold *= (1.0f - wGem);
                    wDark *= (1.0f - wGem);
                }

                // Lighting dot products
                float nDotL1 = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                float nDotL2 = std::max(0.0f, nx * lx2 + ny * ly2 + nz * lz2);
                float nDotH1 = std::max(0.0f, nx * hx + ny * hy + nz * hz);
                float nDotH2 = std::max(0.0f, nx * hx2 + ny * hy2 + nz * hz2);
                float fresnel = std::clamp(1.0f - nz, 0.0f, 1.0f);
                float slopeMag = std::hypot(nx, ny);

                // Camera-proximity exposure factor: high-elevation / high-bulge C-scrolls catch brighter
                // studio light, while low-elevation under-foliage and sunken basins sit deeper in space.
                float elevProximity = (m > 0.05f) ? std::clamp(0.84f + 0.22f * zElev, 0.72f, 1.24f) : 1.0f;

                float directLight = nDotL1 * (1.0f - castShadow * 0.84f);
                float relSlopeLight = (directLight - lz * 0.74f) * 1.45f + nDotL2 * 0.24f + std::max(0.0f, ridge) * 0.22f;
                float diffMod = std::clamp(0.88f + relSlopeLight, 0.12f, 1.62f) * elevProximity * aoVal * (1.0f - castShadow * 0.48f);

                // Apply diffuse lighting with warm bronze shadow roll-off for gold-ish colors
                float rDiff = r0 * diffMod;
                float gDiff = g0 * diffMod;
                float bDiff = b0 * diffMod;

                if (wGold > 0.01f) {
                    float clMod = std::max(0.05f, diffMod);
                    float goldR = r0 * std::pow(clMod, 0.80f);
                    float goldG = g0 * std::pow(clMod, 1.04f);
                    float goldB = b0 * std::pow(clMod, 1.52f);
                    rDiff = rDiff * (1.0f - wGold) + goldR * wGold;
                    gDiff = gDiff * (1.0f - wGold) + goldG * wGold;
                    bDiff = bDiff * (1.0f - wGold) + goldB * wGold;
                }

                // Multi-lobe Specular & Grazing Rim Reflections
                // Gate specular by slope/curvature so flat dark basins stay deep jet-black while curved slopes gleam!
                float bulgeSpecBoost = std::clamp(0.75f + 0.30f * bulge, 0.60f, 1.35f);
                float slopeGate = std::clamp(slopeMag * 2.3f + std::max(0.0f, ridge) * 0.7f, 0.0f, 1.0f);
                float shadowVis = (1.0f - castShadow * 0.88f) * (0.25f + 0.75f * aoVal) * bulgeSpecBoost;
                float specBroad = std::pow(nDotH1, 7.0f) * slopeGate * (0.35f + 0.65f * fresnel) * shadowVis;
                float specMid   = std::pow(nDotH1, 20.0f) * slopeGate * shadowVis;
                float specSharp = std::pow(nDotH1, 52.0f) * (0.25f + 0.75f * slopeGate) * shadowVis;
                float specRim   = std::pow(nDotH2, 5.0f) * fresnel * aoVal * 0.65f;
                float ridgeGlint = std::max(0.0f, ridge) * std::pow(nDotH1, 4.0f) * shadowVis * 0.45f;

                float rOut = rDiff;
                float gOut = gDiff;
                float bOut = bDiff;

                // A) Dark Carved Onyx / Burnished Pewter Specular (Additive so black shines with 3D depth)
                if (wDark > 0.01f) {
                    float onyxSheen = wDark * (specBroad * 0.36f + specMid * 0.48f + specRim * 0.32f + ridgeGlint * 0.65f);
                    float onyxGlint = wDark * (specSharp * 0.62f);
                    // Warm silver-pewter studio reflection on slopes + crisp highlight on peak ridges
                    rOut += onyxSheen * 0.84f + onyxGlint * hlR;
                    gOut += onyxSheen * 0.80f + onyxGlint * hlG;
                    bOut += onyxSheen * 0.76f + onyxGlint * hlB;
                }

                // B) Metallic Gold Specular (Self-tinted warm 24k gold sheen + champagne peak glint)
                if (wGold > 0.01f) {
                    float maxC = std::max({r0, g0, b0, 0.01f});
                    float metalTintR = std::min(1.0f, (r0 / maxC) * 0.5f + 0.50f);
                    float metalTintG = std::min(1.0f, (g0 / maxC) * 0.5f + 0.42f);
                    float metalTintB = std::min(1.0f, (b0 / maxC) * 0.4f + 0.16f);

                    float goldSheen = wGold * (specBroad * 0.28f + specMid * 0.46f + specRim * 0.24f + ridgeGlint * 0.55f);
                    float goldSparkle = wGold * (specSharp * 0.52f);

                    rOut += goldSheen * metalTintR + goldSparkle * hlR;
                    gOut += goldSheen * metalTintG + goldSparkle * hlG;
                    bOut += goldSheen * metalTintB + goldSparkle * hlB;
                }

                // D) Translucent Cabochon & Faceted Gemstones (Beer-Lambert Rim + Subsurface Caustic Belly Glow + Fresnel Window)
                if (wGem > 0.01f) {
                    float dLobeNorm = std::clamp(distLobe[idx] / maxLobeDist, 0.0f, 1.0f);
                    // 1. Beer-Lambert optical edge absorption near the collet bezel (dLobeNorm -> 0)
                    float edgeTrans = std::pow( std::clamp(dLobeNorm * 2.2f, 0.06f, 1.0f), 0.65f );
                    // 2. Internal subsurface caustic belly glow on the slope opposite the key light (-N_xy . L_xy > 0)
                    float oppLight = std::clamp(-(nx * lx + ny * ly) * 1.45f, 0.0f, 1.0f);
                    float causticBell = std::sin(std::clamp(dLobeNorm * 1.25f, 0.0f, 1.0f) * kPi);
                    float causticGlow = std::pow(oppLight, 1.25f) * causticBell * (1.0f - castShadow * 0.45f);
                    // 3. Faceted crystal planar sheet refraction (when matTag == 2)
                    float facetGlow = (matTag == 2)
                        ? (std::clamp(oppLight * 0.85f + nDotL2 * 0.65f, 0.0f, 1.0f) * 0.55f + std::max(0.0f, ridge) * 0.45f)
                        : 0.0f;
                    // 4. Razor-sharp dielectric crystal window reflection on upper-left crown
                    float gemWindowSpec = (std::pow(nDotH1, 76.0f) * 0.92f + std::pow(nDotH1, 18.0f) * 0.28f * slopeGate) *
                                          (1.0f - castShadow * 0.75f);
                    float gemRimBounce = std::pow(nDotH2, 6.0f) * fresnel * 0.48f;

                    float gemR = r0 * (0.28f + 0.78f * edgeTrans * aoVal) + (causticGlow * 0.95f + facetGlow * 0.70f + gemRimBounce * 0.45f) * std::min(1.0f, r0 * 1.55f + 0.25f);
                    float gemG = g0 * (0.18f + 0.65f * edgeTrans * aoVal) + (causticGlow * 0.24f + facetGlow * 0.18f) * std::min(1.0f, r0 * 1.2f) + (causticGlow + facetGlow) * g0 * 0.85f;
                    float gemB = b0 * (0.18f + 0.65f * edgeTrans * aoVal) + (causticGlow * 0.20f + facetGlow * 0.16f) * std::min(1.0f, b0 * 1.3f + 0.08f);

                    gemR += gemWindowSpec * 1.0f;
                    gemG += gemWindowSpec * 0.97f;
                    gemB += gemWindowSpec * 0.94f;

                    rOut = rOut * (1.0f - wGem) + gemR * wGem;
                    gOut = gOut * (1.0f - wGem) + gemG * wGem;
                    bOut = bOut * (1.0f - wGem) + gemB * wGem;
                }

                // C) Standard Dielectric Specular (e.g. Painted Enamel, Ivory, Silver/Pewter)
                float wOther = std::max(0.0f, 1.0f - wDark - wGold - wGem) * std::clamp(m * 1.2f, 0.0f, 1.0f);
                if (wOther > 0.01f) {
                    float coatSpec = wOther * (specBroad * 0.14f + specMid * 0.32f + specSharp * 0.45f + specRim * 0.16f + ridgeGlint * 0.35f);
                    rOut += coatSpec * hlR;
                    gOut += coatSpec * hlG;
                    bOut += coatSpec * hlB;
                }

                // Deep crevice patina tinting in recessed pockets
                if (aoVal < 0.68f && hasCustomPatina) {
                    float pw = ((0.68f - aoVal) / 0.68f) * 0.45f;
                    rOut = rOut * (1.0f - pw) + patR * pw;
                    gOut = gOut * (1.0f - pw) + patG * pw;
                    bOut = bOut * (1.0f - pw) + patB * pw;
                }

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp(rOut * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp(gOut * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp(bOut * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }
}

void CardCanvas::applyFilter(const std::string& effect, float intensity, float param1, float param2, uint32_t tintColor) {
    if (effect.empty() || effect == "none") return;
    const int w = width_;
    const int h = height_;
    if (w <= 2 || h <= 2 || rgba_.size() < static_cast<size_t>(w * h * 4)) return;

    const float mixW = std::clamp(intensity, 0.0f, 2.0f);
    if (mixW <= 1e-4f) return;

    const size_t totalPixels = static_cast<size_t>(w * h);
    const float scaleFactor = static_cast<float>(w) / 360.0f;

    thread_local static std::vector<uint8_t> srcCopy;
    thread_local static std::vector<float> chR;
    thread_local static std::vector<float> chG;
    thread_local static std::vector<float> chB;
    thread_local static std::vector<float> blurR;
    thread_local static std::vector<float> blurG;
    thread_local static std::vector<float> blurB;
    thread_local static std::vector<float> tmp1;
    thread_local static std::vector<float> tmp2;

    srcCopy = rgba_;
    if (chR.size() < totalPixels) {
        chR.resize(totalPixels);
        chG.resize(totalPixels);
        chB.resize(totalPixels);
        blurR.resize(totalPixels);
        blurG.resize(totalPixels);
        blurB.resize(totalPixels);
        tmp1.resize(totalPixels);
        tmp2.resize(totalPixels);
    }

    auto lumAt = [&](int x, int y) -> float {
        int cx = std::clamp(x, 0, w - 1);
        int cy = std::clamp(y, 0, h - 1);
        size_t i = static_cast<size_t>(cy * w + cx) * 4;
        return (0.299f * srcCopy[i + 0] + 0.587f * srcCopy[i + 1] + 0.114f * srcCopy[i + 2]) / 255.0f;
    };

    auto sobelAt = [&](int x, int y) -> float {
        float tl = lumAt(x - 1, y - 1), tc = lumAt(x, y - 1), tr = lumAt(x + 1, y - 1);
        float ml = lumAt(x - 1, y),                           mr = lumAt(x + 1, y);
        float bl = lumAt(x - 1, y + 1), bc = lumAt(x, y + 1), br = lumAt(x + 1, y + 1);
        float gx = (tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl);
        float gy = (bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr);
        float edge = std::hypot(gx, gy);
        if (reliefElevation_.size() == totalPixels) {
            int xm1 = std::max(0, x - 1), xp1 = std::min(w - 1, x + 1);
            int ym1 = std::max(0, y - 1), yp1 = std::min(h - 1, y + 1);
            float ezx = reliefElevation_[y * w + xp1] - reliefElevation_[y * w + xm1];
            float ezy = reliefElevation_[yp1 * w + x] - reliefElevation_[ym1 * w + x];
            edge = std::max(edge, std::hypot(ezx, ezy) * 1.35f);
        }
        return edge;
    };

    // 1. TOON / CEL SHADER
    if (effect == "toon" || effect == "cel" || effect == "cell_shaded") {
        const float levels = (param1 > 0.5f) ? std::clamp(param1, 2.0f, 16.0f) : 4.0f;
        const float edgeStrength = (param2 >= 0.0f) ? param2 : 1.0f;
        const uint32_t inkCol = (tintColor != 0) ? tintColor : 0xFF0E1015;
        const float inkR = static_cast<float>((inkCol >> 16) & 0xFF) / 255.0f;
        const float inkG = static_cast<float>((inkCol >> 8) & 0xFF) / 255.0f;
        const float inkB = static_cast<float>(inkCol & 0xFF) / 255.0f;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                float r = srcCopy[idx * 4 + 0] / 255.0f;
                float g = srcCopy[idx * 4 + 1] / 255.0f;
                float b = srcCopy[idx * 4 + 2] / 255.0f;
                float lum = std::max(1e-4f, 0.299f * r + 0.587f * g + 0.114f * b);

                // Quantize luminance into discrete cel bands with crisp specular rim step
                float qLum = std::floor(lum * levels + 0.35f) / levels;
                if (lum > 0.82f) qLum = 1.05f;
                float ratio = std::clamp(qLum / lum, 0.15f, 1.85f);

                float tr = std::clamp(r * ratio, 0.0f, 1.0f);
                float tg = std::clamp(g * ratio, 0.0f, 1.0f);
                float tb = std::clamp(b * ratio, 0.0f, 1.0f);

                // Boost cel chroma slightly
                float qL = 0.299f * tr + 0.587f * tg + 0.114f * tb;
                tr = std::clamp(qL + (tr - qL) * 1.18f, 0.0f, 1.0f);
                tg = std::clamp(qL + (tg - qL) * 1.18f, 0.0f, 1.0f);
                tb = std::clamp(qL + (tb - qL) * 1.18f, 0.0f, 1.0f);

                // Sobel comic/cel ink outline
                float edge = sobelAt(x, y);
                float inkW = std::clamp((edge - 0.18f) * 3.8f * edgeStrength, 0.0f, 1.0f);
                tr = tr * (1.0f - inkW) + inkR * inkW;
                tg = tg * (1.0f - inkW) + inkG * inkW;
                tb = tb * (1.0f - inkW) + inkB * inkW;

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((r * (1.0f - mixW) + tr * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((g * (1.0f - mixW) + tg * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((b * (1.0f - mixW) + tb * mixW) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }

    // 2. TILT-SHIFT (Per-Card Miniature Selective Focus / DoF)
    if (effect == "tilt_shift") {
        const float focusY = (param1 >= 0.0f && param1 <= 1.0f) ? param1 : 0.50f;
        const float focusHalfW = (param2 > 0.0f && param2 <= 1.0f) ? param2 : 0.22f;
        const int blurRad = std::max(2, static_cast<int>(std::round(5.5f * scaleFactor * std::clamp(mixW, 0.5f, 2.0f))));

        for (size_t i = 0; i < totalPixels; ++i) {
            chR[i] = srcCopy[i * 4 + 0] / 255.0f;
            chG[i] = srcCopy[i * 4 + 1] / 255.0f;
            chB[i] = srcCopy[i * 4 + 2] / 255.0f;
        }
        separableTentBlur(chR, blurR, tmp1, tmp2, w, h, blurRad);
        separableTentBlur(chG, blurG, tmp1, tmp2, w, h, blurRad);
        separableTentBlur(chB, blurB, tmp1, tmp2, w, h, blurRad);

        for (int y = 0; y < h; ++y) {
            float ny = static_cast<float>(y) / static_cast<float>(h - 1);
            float distFocus = std::max(0.0f, std::abs(ny - focusY) - focusHalfW * 0.5f);
            float dofT = std::clamp(distFocus / std::max(0.08f, 0.35f - focusHalfW * 0.3f), 0.0f, 1.0f);
            dofT = dofT * dofT * (3.0f - 2.0f * dofT); // Smoothstep CoC transition
            float effT = std::clamp(dofT * mixW, 0.0f, 1.0f);

            for (int x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(y * w + x);
                if (srcCopy[i * 4 + 3] <= 2) continue;

                float r = chR[i] * (1.0f - effT) + blurR[i] * effT;
                float g = chG[i] * (1.0f - effT) + blurG[i] * effT;
                float b = chB[i] * (1.0f - effT) + blurB[i] * effT;

                // Subtle miniature-diorama saturation & contrast boost
                float lum = 0.299f * r + 0.587f * g + 0.114f * b;
                float sat = 1.0f + 0.24f * std::min(1.0f, mixW);
                r = std::clamp(lum + (r - lum) * sat, 0.0f, 1.0f);
                g = std::clamp(lum + (g - lum) * sat, 0.0f, 1.0f);
                b = std::clamp(lum + (b - lum) * sat, 0.0f, 1.0f);

                rgba_[i * 4 + 0] = static_cast<uint8_t>(r * 255.0f);
                rgba_[i * 4 + 1] = static_cast<uint8_t>(g * 255.0f);
                rgba_[i * 4 + 2] = static_cast<uint8_t>(b * 255.0f);
            }
        }
        return;
    }

    // 3. DITHER (Ordered 4x4 Bayer Matrix Dithering)
    if (effect == "dither" || effect == "bayer") {
        constexpr float kBayer4x4[4][4] = {
            {  0.0f / 16.0f - 0.5f,  8.0f / 16.0f - 0.5f,  2.0f / 16.0f - 0.5f, 10.0f / 16.0f - 0.5f },
            { 12.0f / 16.0f - 0.5f,  4.0f / 16.0f - 0.5f, 14.0f / 16.0f - 0.5f,  6.0f / 16.0f - 0.5f },
            {  3.0f / 16.0f - 0.5f, 11.0f / 16.0f - 0.5f,  1.0f / 16.0f - 0.5f,  9.0f / 16.0f - 0.5f },
            { 15.0f / 16.0f - 0.5f,  7.0f / 16.0f - 0.5f, 13.0f / 16.0f - 0.5f,  5.0f / 16.0f - 0.5f }
        };
        const float levels = (param1 >= 2.0f) ? std::clamp(param1, 2.0f, 32.0f) : 4.0f;
        const int pxScale = (param2 >= 1.0f) ? std::clamp(static_cast<int>(std::round(param2)), 1, 8) : 1;
        const float spread = 1.0f / levels;

        for (int y = 0; y < h; ++y) {
            int sy = (y / pxScale) * pxScale;
            int by = (y / pxScale) & 3;
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                int sx = (x / pxScale) * pxScale;
                int bx = (x / pxScale) & 3;
                size_t sIdx = static_cast<size_t>(sy * w + sx);

                float bayer = kBayer4x4[by][bx] * spread;
                float r0 = srcCopy[sIdx * 4 + 0] / 255.0f;
                float g0 = srcCopy[sIdx * 4 + 1] / 255.0f;
                float b0 = srcCopy[sIdx * 4 + 2] / 255.0f;

                float dr = std::floor(std::clamp(r0 + bayer, 0.0f, 0.999f) * levels) / (levels - 1.0f);
                float dg = std::floor(std::clamp(g0 + bayer, 0.0f, 0.999f) * levels) / (levels - 1.0f);
                float db = std::floor(std::clamp(b0 + bayer, 0.0f, 0.999f) * levels) / (levels - 1.0f);

                float origR = srcCopy[idx * 4 + 0] / 255.0f;
                float origG = srcCopy[idx * 4 + 1] / 255.0f;
                float origB = srcCopy[idx * 4 + 2] / 255.0f;

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((origR * (1.0f - mixW) + dr * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((origG * (1.0f - mixW) + dg * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((origB * (1.0f - mixW) + db * mixW) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }

    // 4. CRT (Per-Card Trinitron CRT Scanlines, Barrel Curvature & RGB Phosphor Mask)
    if (effect == "crt") {
        const float curvature = (param1 >= 0.0f) ? param1 : 0.10f;
        const float scanStrength = (param2 >= 0.0f) ? param2 : 0.42f;

        for (int y = 0; y < h; ++y) {
            float ny = (static_cast<float>(y) / static_cast<float>(h - 1)) * 2.0f - 1.0f;
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                float nx = (static_cast<float>(x) / static_cast<float>(w - 1)) * 2.0f - 1.0f;
                float r2 = nx * nx + ny * ny;
                float barrel = 1.0f + curvature * r2;
                float u = (nx * barrel + 1.0f) * 0.5f * static_cast<float>(w - 1);
                float v = (ny * barrel + 1.0f) * 0.5f * static_cast<float>(h - 1);

                float cr = 0.02f, cg = 0.02f, cb = 0.025f;
                if (u >= 0.0f && u <= static_cast<float>(w - 1) && v >= 0.0f && v <= static_cast<float>(h - 1)) {
                    int sx = static_cast<int>(std::round(u));
                    int sy = static_cast<int>(std::round(v));
                    int sxR = std::clamp(sx - 1, 0, w - 1);
                    int sxB = std::clamp(sx + 1, 0, w - 1);

                    // Subtle RGB phosphor triad subpixel separation + scanline wave
                    cr = (srcCopy[static_cast<size_t>(sy * w + sxR) * 4 + 0] * 0.45f +
                          srcCopy[static_cast<size_t>(sy * w + sx)  * 4 + 0] * 0.55f) / 255.0f;
                    cg =  srcCopy[static_cast<size_t>(sy * w + sx)  * 4 + 1] / 255.0f;
                    cb = (srcCopy[static_cast<size_t>(sy * w + sxB) * 4 + 2] * 0.45f +
                          srcCopy[static_cast<size_t>(sy * w + sx)  * 4 + 2] * 0.55f) / 255.0f;

                    float scanWave = 0.5f + 0.5f * std::sin(v * kPi);
                    float scanMod = 1.0f - scanStrength * (1.0f - scanWave);

                    // Aperture grille vertical triad tint
                    int triad = x % 3;
                    float mR = (triad == 0) ? 1.12f : 0.94f;
                    float mG = (triad == 1) ? 1.12f : 0.94f;
                    float mB = (triad == 2) ? 1.12f : 0.94f;

                    // CRT corner tube vignette
                    float tubeVig = std::clamp(1.18f - 0.32f * r2, 0.25f, 1.0f);
                    cr = std::clamp(cr * scanMod * mR * tubeVig * 1.12f, 0.0f, 1.0f);
                    cg = std::clamp(cg * scanMod * mG * tubeVig * 1.12f, 0.0f, 1.0f);
                    cb = std::clamp(cb * scanMod * mB * tubeVig * 1.12f, 0.0f, 1.0f);
                }

                float origR = srcCopy[idx * 4 + 0] / 255.0f;
                float origG = srcCopy[idx * 4 + 1] / 255.0f;
                float origB = srcCopy[idx * 4 + 2] / 255.0f;
                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((origR * (1.0f - mixW) + cr * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((origG * (1.0f - mixW) + cg * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((origB * (1.0f - mixW) + cb * mixW) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }

    // 5. PENCIL SKETCH (Graphite Contours & Diagonal Cross-Hatching on Sketchbook Paper)
    if (effect == "pencil_sketch" || effect == "sketch") {
        const float hatchFreq = (param1 > 0.1f) ? param1 : 1.0f;
        const float colorBleed = (param2 >= 0.0f) ? std::clamp(param2, 0.0f, 1.0f) : 0.18f;
        const uint32_t paperCol = (tintColor != 0) ? tintColor : 0xFFF4EFE6;
        const float pR = static_cast<float>((paperCol >> 16) & 0xFF) / 255.0f;
        const float pG = static_cast<float>((paperCol >> 8) & 0xFF) / 255.0f;
        const float pB = static_cast<float>(paperCol & 0xFF) / 255.0f;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                float r = srcCopy[idx * 4 + 0] / 255.0f;
                float g = srcCopy[idx * 4 + 1] / 255.0f;
                float b = srcCopy[idx * 4 + 2] / 255.0f;
                float lum = 0.299f * r + 0.587f * g + 0.114f * b;

                float edge = sobelAt(x, y);
                float darkness = 1.0f - lum;

                // Two-way diagonal graphite cross-hatching
                float freq = (1.35f * hatchFreq) / scaleFactor;
                float h1 = 0.5f + 0.5f * std::sin((static_cast<float>(x) + static_cast<float>(y)) * freq);
                float h2 = 0.5f + 0.5f * std::sin((static_cast<float>(x) - static_cast<float>(y)) * freq * 1.15f);

                float stroke = std::clamp(edge * 1.65f, 0.0f, 1.0f);
                if (darkness > 0.32f) {
                    stroke = std::max(stroke, (1.0f - h1) * std::clamp((darkness - 0.30f) * 1.8f, 0.0f, 0.78f));
                }
                if (darkness > 0.58f) {
                    stroke = std::max(stroke, (1.0f - h2) * std::clamp((darkness - 0.55f) * 2.1f, 0.0f, 0.88f));
                }

                // Subtle tooth noise
                uint32_t hsh = static_cast<uint32_t>(x * 374761393u + y * 668265263u);
                hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
                float grain = (static_cast<float>(hsh & 0xFF) / 255.0f - 0.5f) * 0.06f;

                float graphite = std::clamp(1.0f - stroke * 0.86f + grain, 0.10f, 1.0f);
                float sr = pR * graphite * (1.0f - colorBleed) + r * graphite * colorBleed;
                float sg = pG * graphite * (1.0f - colorBleed) + g * graphite * colorBleed;
                float sb = pB * graphite * (1.0f - colorBleed) + b * graphite * colorBleed;

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((r * (1.0f - mixW) + sr * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((g * (1.0f - mixW) + sg * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((b * (1.0f - mixW) + sb * mixW) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }

    // 6. VHS GLITCH (Chromatic Aberration, Scanline Tracking Jitter & Tape Grain)
    if (effect == "vhs_glitch" || effect == "vhs" || effect == "glitch") {
        const float chromaShift = ((param1 >= 0.0f) ? param1 : 3.5f) * scaleFactor * mixW;
        const float jitterAmp = ((param2 >= 0.0f) ? param2 : 1.0f) * scaleFactor * mixW;

        for (int y = 0; y < h; ++y) {
            // Deterministic scanline tracking tear & horizontal wobble
            uint32_t rowHash = static_cast<uint32_t>((y / 2) * 2654435761u);
            rowHash ^= (rowHash >> 15);
            float rand01 = static_cast<float>(rowHash & 0xFFFF) / 65535.0f;
            float lineJitter = (rand01 - 0.5f) * 1.8f * jitterAmp;
            if (rand01 > 0.94f) {
                lineJitter += (rand01 - 0.94f) * 140.0f * jitterAmp; // Tracking tear bar
            }

            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                int xBase = std::clamp(static_cast<int>(std::round(static_cast<float>(x) + lineJitter)), 0, w - 1);
                int xR = std::clamp(static_cast<int>(std::round(static_cast<float>(xBase) - chromaShift)), 0, w - 1);
                int xB = std::clamp(static_cast<int>(std::round(static_cast<float>(xBase) + chromaShift)), 0, w - 1);

                float vr = srcCopy[static_cast<size_t>(y * w + xR) * 4 + 0] / 255.0f;
                float vg = srcCopy[static_cast<size_t>(y * w + xBase) * 4 + 1] / 255.0f;
                float vb = srcCopy[static_cast<size_t>(y * w + xB) * 4 + 2] / 255.0f;

                // Tape static & interlace line
                uint32_t pxHash = static_cast<uint32_t>(x * 1597334677u + y * 3812015801u);
                float noise = (static_cast<float>((pxHash >> 16) & 0xFF) / 255.0f - 0.5f) * 0.07f * mixW;
                float interlace = (y & 1) ? (1.0f - 0.08f * mixW) : 1.0f;

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((vr * interlace + noise) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((vg * interlace + noise) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((vb * interlace + noise) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }

    // 7. VIGNETTE (Optical Edge Falloff)
    if (effect == "vignette") {
        const float radius = (param1 > 0.0f) ? param1 : 0.72f;
        const float softness = (param2 > 0.01f) ? param2 : 0.48f;
        const uint32_t vigCol = (tintColor != 0) ? tintColor : 0xFF06070A;
        const float vR = static_cast<float>((vigCol >> 16) & 0xFF) / 255.0f;
        const float vG = static_cast<float>((vigCol >> 8) & 0xFF) / 255.0f;
        const float vB = static_cast<float>(vigCol & 0xFF) / 255.0f;

        for (int y = 0; y < h; ++y) {
            float ny = (static_cast<float>(y) / static_cast<float>(h - 1)) * 2.0f - 1.0f;
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                float nx = (static_cast<float>(x) / static_cast<float>(w - 1)) * 2.0f - 1.0f;
                float d = std::hypot(nx, ny);
                float vig = std::clamp((d - radius) / softness, 0.0f, 1.0f);
                vig = vig * vig * (3.0f - 2.0f * vig) * std::clamp(mixW, 0.0f, 1.0f);

                float r = srcCopy[idx * 4 + 0] / 255.0f;
                float g = srcCopy[idx * 4 + 1] / 255.0f;
                float b = srcCopy[idx * 4 + 2] / 255.0f;

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((r * (1.0f - vig) + vR * vig) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((g * (1.0f - vig) + vG * vig) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((b * (1.0f - vig) + vB * vig) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }

    // 8. BLOOM (Specular & Gemstone Halation Glow)
    if (effect == "bloom" || effect == "glow") {
        const float threshold = (param1 >= 0.0f && param1 <= 1.0f) ? param1 : 0.58f;
        const int radius = std::max(2, static_cast<int>(std::round(((param2 > 0.5f) ? param2 : 10.0f) * scaleFactor)));

        for (size_t i = 0; i < totalPixels; ++i) {
            float r = srcCopy[i * 4 + 0] / 255.0f;
            float g = srcCopy[i * 4 + 1] / 255.0f;
            float b = srcCopy[i * 4 + 2] / 255.0f;
            float maxC = std::max({r, g, b});
            float br = std::max(0.0f, (maxC - threshold) / std::max(0.05f, 1.0f - threshold));
            chR[i] = r * br;
            chG[i] = g * br;
            chB[i] = b * br;
        }
        separableTentBlur(chR, blurR, tmp1, tmp2, w, h, radius);
        separableTentBlur(chG, blurG, tmp1, tmp2, w, h, radius);
        separableTentBlur(chB, blurB, tmp1, tmp2, w, h, radius);

        for (size_t i = 0; i < totalPixels; ++i) {
            if (srcCopy[i * 4 + 3] <= 2) continue;
            float r = srcCopy[i * 4 + 0] / 255.0f + blurR[i] * mixW * 1.15f;
            float g = srcCopy[i * 4 + 1] / 255.0f + blurG[i] * mixW * 1.15f;
            float b = srcCopy[i * 4 + 2] / 255.0f + blurB[i] * mixW * 1.15f;
            rgba_[i * 4 + 0] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            rgba_[i * 4 + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
            rgba_[i * 4 + 2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        }
        return;
    }

    // 9. WATERCOLOR (4-Quadrant Kuwahara Brush Pooling + Wet-Edge Pigment Darkening)
    if (effect == "watercolor" || effect == "kuwahara") {
        const int kRad = std::clamp(static_cast<int>(std::round(((param1 >= 1.0f) ? param1 : 3.0f) * scaleFactor)), 1, 6);
        const float wetEdge = (param2 >= 0.0f) ? param2 : 0.65f;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                size_t idx = static_cast<size_t>(y * w + x);
                if (srcCopy[idx * 4 + 3] <= 2) continue;

                // Evaluate 4 overlapping Kuwahara quadrants around (x, y)
                float bestVar = 1e9f;
                float bestR = srcCopy[idx * 4 + 0] / 255.0f;
                float bestG = srcCopy[idx * 4 + 1] / 255.0f;
                float bestB = srcCopy[idx * 4 + 2] / 255.0f;

                const int qx0[4] = {-kRad, 0, -kRad, 0};
                const int qx1[4] = {0, kRad, 0, kRad};
                const int qy0[4] = {-kRad, -kRad, 0, 0};
                const int qy1[4] = {0, 0, kRad, kRad};

                for (int q = 0; q < 4; ++q) {
                    float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, sumL = 0.0f, sumL2 = 0.0f;
                    int count = 0;
                    for (int dy = qy0[q]; dy <= qy1[q]; ++dy) {
                        int sy = std::clamp(y + dy, 0, h - 1);
                        for (int dx = qx0[q]; dx <= qx1[q]; ++dx) {
                            int sx = std::clamp(x + dx, 0, w - 1);
                            size_t sIdx = static_cast<size_t>(sy * w + sx) * 4;
                            float sr = srcCopy[sIdx + 0] / 255.0f;
                            float sg = srcCopy[sIdx + 1] / 255.0f;
                            float sb = srcCopy[sIdx + 2] / 255.0f;
                            float sl = 0.299f * sr + 0.587f * sg + 0.114f * sb;
                            sumR += sr; sumG += sg; sumB += sb;
                            sumL += sl; sumL2 += sl * sl;
                            ++count;
                        }
                    }
                    float invN = 1.0f / static_cast<float>(count);
                    float meanL = sumL * invN;
                    float varL = sumL2 * invN - meanL * meanL;
                    if (varL < bestVar) {
                        bestVar = varL;
                        bestR = sumR * invN;
                        bestG = sumG * invN;
                        bestB = sumB * invN;
                    }
                }

                // Wet pigment pooling darkening along color boundaries + cold-press paper granulation
                float edge = sobelAt(x, y);
                float pool = std::clamp(1.0f - edge * wetEdge * 0.55f, 0.45f, 1.0f);
                float paper = 1.0f + 0.035f * std::sin(static_cast<float>(x) * 0.7f) * std::cos(static_cast<float>(y) * 0.7f);

                float wr = std::clamp(bestR * pool * paper, 0.0f, 1.0f);
                float wg = std::clamp(bestG * pool * paper, 0.0f, 1.0f);
                float wb = std::clamp(bestB * pool * paper, 0.0f, 1.0f);

                float origR = srcCopy[idx * 4 + 0] / 255.0f;
                float origG = srcCopy[idx * 4 + 1] / 255.0f;
                float origB = srcCopy[idx * 4 + 2] / 255.0f;

                rgba_[idx * 4 + 0] = static_cast<uint8_t>(std::clamp((origR * (1.0f - mixW) + wr * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 1] = static_cast<uint8_t>(std::clamp((origG * (1.0f - mixW) + wg * mixW) * 255.0f, 0.0f, 255.0f));
                rgba_[idx * 4 + 2] = static_cast<uint8_t>(std::clamp((origB * (1.0f - mixW) + wb * mixW) * 255.0f, 0.0f, 255.0f));
            }
        }
        return;
    }
}

void CardCanvas::applyShader(eatsbits::eatscript::FunctionDef* funcDef, const std::string& inlineSource,
                             int rank, Suit suit, float intensity,
                             float u0, float u1, float u2, float u3) {
    const int w = width_;
    const int h = height_;
    if (w <= 2 || h <= 2 || rgba_.size() < static_cast<size_t>(w * h * 4)) return;
    if (!funcDef && inlineSource.empty()) return;

    const float mixW = std::clamp(intensity, 0.0f, 1.0f);
    if (mixW <= 1e-4f) return;

    eatsbits::eatscript::VM vm;
    bool compiled = false;
    if (funcDef) {
        compiled = vm.compileShaderFunction(funcDef);
    } else if (!inlineSource.empty()) {
        compiled = vm.compileShaderSource(inlineSource);
    }
    if (!compiled) return;

    const size_t totalPixels = static_cast<size_t>(w * h);
    std::vector<uint8_t> srcCopy = rgba_;
    const uint8_t* srcData = srcCopy.data();

    const double dSuit = static_cast<double>(static_cast<int>(suit));
    const double dRank = static_cast<double>(rank);
    const double dIsRed = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 1.0 : 0.0;
    const double aspect = static_cast<double>(h) / static_cast<double>(w);
    const float cr = (cornerRadius_ <= 0.0f) ? 0.0f : std::max(1.0f, cornerRadius_ * (static_cast<float>(w) / 360.0f));
    const float halfW = static_cast<float>(w) * 0.5f;
    const float halfH = static_cast<float>(h) * 0.5f;

    const bool hasElev = (reliefElevation_.size() == totalPixels);
    const bool hasMask = (reliefMask_.size() == totalPixels);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y) {
        const double v = static_cast<double>(y) / static_cast<double>(h - 1);
        const double ny = (v * 2.0 - 1.0) * aspect;
        const float dyCard = std::abs((static_cast<float>(y) + 0.5f) - halfH) - (halfH - cr);

        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y * w + x);
            uint8_t origA = srcData[idx * 4 + 3];

            float cardCov = 1.0f;
            if (cr > 0.0f) {
                // Compute rounded card silhouette coverage so pure shader-drawn cards clip cleanly at corners
                float dxCard = std::abs((static_cast<float>(x) + 0.5f) - halfW) - (halfW - cr);
                float sdfCard = std::hypot(std::max(dxCard, 0.0f), std::max(dyCard, 0.0f)) +
                                std::min(std::max(dxCard, dyCard), 0.0f) - cr;
                cardCov = std::clamp(0.5f - sdfCard, 0.0f, 1.0f);
                if (origA <= 2 && cardCov <= 0.01f) continue;
            }

            const double u = static_cast<double>(x) / static_cast<double>(w - 1);
            const double nx = u * 2.0 - 1.0;

            eatsbits::eatscript::ShaderPixelContext pCtx;
            pCtx.u       = u;
            pCtx.v       = v;
            pCtx.nx      = nx;
            pCtx.ny      = ny;
            pCtx.px      = static_cast<double>(x);
            pCtx.py      = static_cast<double>(y);
            pCtx.inR     = static_cast<double>(srcData[idx * 4 + 0]) / 255.0;
            pCtx.inG     = static_cast<double>(srcData[idx * 4 + 1]) / 255.0;
            pCtx.inB     = static_cast<double>(srcData[idx * 4 + 2]) / 255.0;
            pCtx.inA     = (origA > 0) ? (static_cast<double>(origA) / 255.0) : static_cast<double>(cardCov);
            pCtx.height  = hasElev ? static_cast<double>(reliefElevation_[idx]) : 0.0;
            pCtx.mask    = hasMask ? static_cast<double>(reliefMask_[idx]) : 0.0;
            pCtx.rank    = dRank;
            pCtx.suit    = dSuit;
            pCtx.isRed   = dIsRed;
            pCtx.u0      = static_cast<double>(u0);
            pCtx.u1      = static_cast<double>(u1);
            pCtx.u2      = static_cast<double>(u2);
            pCtx.u3      = static_cast<double>(u3);
            pCtx.srcRgba = srcData;
            pCtx.width   = w;
            pCtx.heightPx = h;

            double outR = pCtx.inR, outG = pCtx.inG, outB = pCtx.inB, outA = pCtx.inA;
            vm.executePixelShader(pCtx, outR, outG, outB, outA);

            float fR = std::clamp(static_cast<float>(pCtx.inR * (1.0 - mixW) + outR * mixW), 0.0f, 1.0f);
            float fG = std::clamp(static_cast<float>(pCtx.inG * (1.0 - mixW) + outG * mixW), 0.0f, 1.0f);
            float fB = std::clamp(static_cast<float>(pCtx.inB * (1.0 - mixW) + outB * mixW), 0.0f, 1.0f);
            float fA = (origA > 2)
                ? static_cast<float>(origA) / 255.0f
                : std::clamp(static_cast<float>(outA) * cardCov, 0.0f, 1.0f);

            rgba_[idx * 4 + 0] = static_cast<uint8_t>(fR * 255.0f);
            rgba_[idx * 4 + 1] = static_cast<uint8_t>(fG * 255.0f);
            rgba_[idx * 4 + 2] = static_cast<uint8_t>(fB * 255.0f);
            rgba_[idx * 4 + 3] = static_cast<uint8_t>(fA * 255.0f);
        }
    }
}

void CardCanvas::drawCornerIndices(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color, float fontSize) {
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    float pipSize = fontSize * 0.85f;
    float marginX = cardW * 0.09f;
    float marginY = cardH * 0.05f;

    // Top-left index with 3px vertical padding between rank and suit pip
    float rankY = marginY + fontSize * 0.5f;
    float suitY = rankY + (fontSize * 0.5f) + (pipSize * 0.5f) + 3.0f;
    drawRank(rank, marginX, rankY, fontSize, color);
    drawSuit(suit, marginX, suitY, pipSize, color);

    // Bottom-right inverted index (180 degree rotation)
    push();
    translate(cardW, cardH);
    rotate180();
    drawRank(rank, marginX, rankY, fontSize, color);
    drawSuit(suit, marginX, suitY, pipSize, color);
    pop();
}

void CardCanvas::drawPipLayout(int rank, Suit suit, float cardW, float cardH, uint32_t color, const std::string& variant) {
    if (rank < 1 || rank > 10) return;
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFB81424 : 0xFF1A1A1A;
    }

    const bool useGemPips = (variant == "gem" || variant == "baroque" || variant == "baroque_gem" || variant == "relief");
    const auto& layout = getStandardPipLayout(rank);
    float basePipSize = cardW * (useGemPips ? 0.16f : 0.17f);

    for (const auto& item : layout) {
        float px = item.x * cardW;
        float py = item.y * cardH;
        float pSize = basePipSize * item.scale;

        if (useGemPips) {
            // Map classic outer pip columns (0.32..0.68) slightly inward so they sit comfortably inside the baroque arch
            px = cardW * 0.5f + (item.x - 0.5f) * cardW * 0.80f;
            py = cardH * 0.5f + (item.y - 0.5f) * cardH * 0.84f;
        }

        if (item.flipY) {
            push();
            translate(px, py);
            rotate180();
            if (useGemPips) {
                drawGemPip(suit, 0.0f, 0.0f, pSize, color, 0xFFC5A869);
            } else {
                drawSuit(suit, 0.0f, 0.0f, pSize, color);
            }
            pop();
        } else {
            if (useGemPips) {
                drawGemPip(suit, px, py, pSize, color, 0xFFC5A869);
            } else {
                drawSuit(suit, px, py, pSize, color);
            }
        }
    }
}

void CardCanvas::drawCourt(const std::string& rank, Suit suit, float x, float y, float w, float h, float /*opacity*/, uint32_t tint) {
    push();
    translate(x, y);

    float cx = w * 0.5f;
    uint32_t gold = (tint != 0) ? tint : 0xFFD4AF37;
    uint32_t suitColor = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFB81424 : 0xFF14151A;

    // 1. Ornate Arch Frame
    strokeRoundedRect(4.0f, 4.0f, w - 8.0f, h - 8.0f, 6.0f, gold, 1.8f);
    strokeRoundedRect(8.0f, 8.0f, w - 16.0f, h - 16.0f, 4.0f, gold, 0.8f);

    // 2. Royal Crest Crown
    float crownW = w * 0.44f;
    float crownH = crownW * 0.55f;
    float crownY = h * 0.14f;

    std::string goldHex = formatColorHex(gold);
    std::ostringstream crownSvg;
    crownSvg << "<svg xmlns='http://www.w3.org/2000/svg' width='" << crownW << "' height='" << crownH << "' viewBox='0 0 100 55'>"
             << "<path d='M 10 50 L 5 15 L 30 32 L 50 8 L 70 32 L 95 15 L 90 50 Z' fill='" << goldHex << "' stroke='#8C7220' stroke-width='2'/>"
             << "<circle cx='5' cy='15' r='4' fill='#FFF8DC'/>"
             << "<circle cx='50' cy='8' r='5' fill='#FFF8DC'/>"
             << "<circle cx='95' cy='15' r='4' fill='#FFF8DC'/>"
             << "<circle cx='30' cy='32' r='3.5' fill='#FFF8DC'/>"
             << "<circle cx='70' cy='32' r='3.5' fill='#FFF8DC'/>"
             << "</svg>";
    rasterizeSvgFragment(crownSvg.str(), state_.tx + state_.a * (cx - crownW * 0.5f), state_.ty + state_.d * crownY, crownW * std::abs(state_.a), crownH * std::abs(state_.d));

    // 3. Central Ornate Pip
    float pipSize = std::min(w, h) * 0.38f;
    drawOrnateSuit(suit, cx, h * 0.52f, pipSize, suitColor, gold);

    // 4. Bold Royal Rank Letter below crown
    drawRank(rank, cx, h * 0.86f, std::min(w, h) * 0.28f, gold);

    pop();
}

void CardCanvas::drawCardFace(const std::string& rank, Suit suit, float cardW, float cardH, uint32_t color, const std::string& layout) {
    float r = cornerRadius_;
    fillRoundedRect(0, 0, cardW, cardH, r, 0xFFFFFFFF);
    strokeRoundedRect(0, 0, cardW, cardH, r, 0xFFE0DACF, 1.0f);

    int rInt = parseRank(rank);
    if (color == 0) {
        color = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC0000 : 0xFF1A1A1A;
    }

    drawCornerIndices(rank, suit, cardW, cardH, color, cardW * 0.095f);

    if (rInt >= 11 && rInt <= 13) {
        float marginX = cardW * 0.16f;
        float marginY = cardH * 0.12f;
        float innerW = cardW - marginX * 2.0f;
        float innerH = cardH - marginY * 2.0f;
        drawCourt(rank, suit, marginX, marginY, innerW, innerH);
        strokeRect(marginX, marginY, innerW, innerH, color, 1.2f);
    } else {
        drawPipLayout(rInt, suit, cardW, cardH, color, layout);
    }
}

void CardCanvas::drawPixelSprite(
    const std::vector<std::string>& spriteLines,
    float cx, float cy,
    float pixelSize,
    uint32_t color,
    char onChar,
    bool flipY,
    bool flipX
) {
    if (spriteLines.empty() || pixelSize <= 0.0f) return;
    int h = static_cast<int>(spriteLines.size());
    int w = 0;
    for (const auto& line : spriteLines) {
        w = std::max(w, static_cast<int>(line.size()));
    }
    if (w <= 0 || h <= 0) return;

    float totalW = static_cast<float>(w) * pixelSize;
    float totalH = static_cast<float>(h) * pixelSize;
    float startX = cx - totalW * 0.5f;
    float startY = cy - totalH * 0.5f;

    uint8_t cr = static_cast<uint8_t>((color >> 16) & 0xFF);
    uint8_t cg = static_cast<uint8_t>((color >> 8) & 0xFF);
    uint8_t cb = static_cast<uint8_t>(color & 0xFF);
    uint8_t ca = static_cast<uint8_t>((color >> 24) & 0xFF);
    if (ca == 0 && (cr != 0 || cg != 0 || cb != 0)) ca = 255;

    for (int y = 0; y < h; ++y) {
        int srcY = flipY ? (h - 1 - y) : y;
        const auto& line = spriteLines[srcY];
        int lineLen = static_cast<int>(line.size());

        for (int x = 0; x < w; ++x) {
            int srcX = flipX ? (w - 1 - x) : x;
            if (srcX < lineLen && line[srcX] == onChar) {
                int px0 = static_cast<int>(std::round(startX + static_cast<float>(x) * pixelSize));
                int py0 = static_cast<int>(std::round(startY + static_cast<float>(y) * pixelSize));
                int px1 = static_cast<int>(std::round(startX + static_cast<float>(x + 1) * pixelSize));
                int py1 = static_cast<int>(std::round(startY + static_cast<float>(y + 1) * pixelSize));

                px0 = std::clamp(px0, 0, width_);
                px1 = std::clamp(px1, 0, width_);
                py0 = std::clamp(py0, 0, height_);
                py1 = std::clamp(py1, 0, height_);

                for (int py = py0; py < py1; ++py) {
                    uint8_t* row = &rgba_[static_cast<size_t>(py * width_ + px0) * 4];
                    for (int px = px0; px < px1; ++px) {
                        row[0] = cr;
                        row[1] = cg;
                        row[2] = cb;
                        row[3] = ca;
                        row += 4;
                    }
                }
            }
        }
    }
}

uint32_t CardCanvas::parseColor(const std::string& colorStr) {
    if (colorStr.empty()) return 0xFFFFFFFF;
    if (colorStr[0] == '#') {
        std::string hex = colorStr.substr(1);
        if (hex.size() == 3) {
            uint32_t r = std::stoul(hex.substr(0, 1) + hex.substr(0, 1), nullptr, 16);
            uint32_t g = std::stoul(hex.substr(1, 1) + hex.substr(1, 1), nullptr, 16);
            uint32_t b = std::stoul(hex.substr(2, 1) + hex.substr(2, 1), nullptr, 16);
            return 0xFF000000 | (r << 16) | (g << 8) | b;
        } else if (hex.size() == 6) {
            uint32_t rgb = std::stoul(hex, nullptr, 16);
            return 0xFF000000 | rgb;
        } else if (hex.size() == 8) {
            return std::stoul(hex, nullptr, 16);
        }
    }
    if (colorStr == "red") return 0xFFCC0000;
    if (colorStr == "black") return 0xFF1A1A1A;
    if (colorStr == "white") return 0xFFFFFFFF;
    if (colorStr == "gold") return 0xFFD4AF37;
    return 0xFFFFFFFF;
}

Suit CardCanvas::parseSuit(const std::string& suitStr) {
    if (suitStr == "hearts" || suitStr == "Hearts") return Suit::Hearts;
    if (suitStr == "diamonds" || suitStr == "Diamonds") return Suit::Diamonds;
    if (suitStr == "clubs" || suitStr == "Clubs") return Suit::Clubs;
    return Suit::Spades;
}

int CardCanvas::parseRank(const std::string& rankStr) {
    if (rankStr == "A" || rankStr == "a" || rankStr == "1") return 1;
    if (rankStr == "J" || rankStr == "j" || rankStr == "11") return 11;
    if (rankStr == "Q" || rankStr == "q" || rankStr == "12") return 12;
    if (rankStr == "K" || rankStr == "k" || rankStr == "13") return 13;
    try {
        return std::stoi(rankStr);
    } catch (...) {
        return 1;
    }
}

void CardCanvas::bindToEvaluator(eatsbits::eatscript::Evaluator& eval, CardCanvas* canvas) {
    // 0. General canvas properties & clear
    eval.registerMemberFunction("ctx", "clear", [canvas](const auto& args, const auto&) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        uint32_t col = 0;
        if (!args.empty()) {
            col = args[0].isString() ? CardCanvas::parseColor(args[0].asString()) : static_cast<uint32_t>(args[0].asInt());
        }
        canvas->clear(col);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "set_corner_radius", [canvas](const auto& args, const auto&) {
        if (!canvas || args.empty()) return eatsbits::eatscript::Value(false);
        canvas->setCornerRadius(args[0].asFloat(0.0f));
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "width", [canvas](const auto&, const auto&) {
        return eatsbits::eatscript::Value(canvas ? static_cast<double>(canvas->width()) : 0.0);
    });

    eval.registerMemberFunction("ctx", "height", [canvas](const auto&, const auto&) {
        return eatsbits::eatscript::Value(canvas ? static_cast<double>(canvas->height()) : 0.0);
    });

    // 1. ctx.fill_rect(x, y, w, h, color)
    eval.registerMemberFunction("ctx", "fill_rect", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        canvas->fillRect(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 2. ctx.stroke_rect(x, y, w, h, color, width)
    eval.registerMemberFunction("ctx", "stroke_rect", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        float lw = (args.size() > 5) ? args[5].asFloat(1.0f) : 1.0f;
        canvas->strokeRect(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 3. ctx.fill_rounded_rect(x, y, w, h, radius, color)
    eval.registerMemberFunction("ctx", "fill_rounded_rect", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 0, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 1, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        float defR = canvas ? canvas->cornerRadius() : 8.0f;
        float rad = eatsbits::eatscript::resolveArg(args, kwargs, 4, "radius", eatsbits::eatscript::Value(defR)).asFloat(defR);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        canvas->fillRoundedRect(x, y, w, h, rad, col);
        return eatsbits::eatscript::Value(true);
    });

    // 4. ctx.stroke_rounded_rect(x, y, w, h, radius, color, width)
    eval.registerMemberFunction("ctx", "stroke_rounded_rect", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 0, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 1, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        float defR = canvas ? canvas->cornerRadius() : 8.0f;
        float rad = eatsbits::eatscript::resolveArg(args, kwargs, 4, "radius", eatsbits::eatscript::Value(defR)).asFloat(defR);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 6, "line_width", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        canvas->strokeRoundedRect(x, y, w, h, rad, col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 5. ctx.fill_circle(cx, cy, radius, color)
    eval.registerMemberFunction("ctx", "fill_circle", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        uint32_t col = args[3].isString() ? CardCanvas::parseColor(args[3].asString()) : static_cast<uint32_t>(args[3].asInt());
        canvas->fillCircle(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 6. ctx.stroke_circle(cx, cy, radius, color, width)
    eval.registerMemberFunction("ctx", "stroke_circle", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        uint32_t col = args[3].isString() ? CardCanvas::parseColor(args[3].asString()) : static_cast<uint32_t>(args[3].asInt());
        float lw = (args.size() > 4) ? args[4].asFloat(1.0f) : 1.0f;
        canvas->strokeCircle(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 7. ctx.draw_line(x0, y0, x1, y1, color, width)
    eval.registerMemberFunction("ctx", "draw_line", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        float lw = (args.size() > 5) ? args[5].asFloat(1.0f) : 1.0f;
        canvas->drawLine(args[0].asFloat(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 8. ctx.draw_svg(svg_xml, x, y, w, h)
    eval.registerMemberFunction("ctx", "draw_svg", [canvas](const auto& args, const auto&) {
        if (!canvas || args.empty()) return eatsbits::eatscript::Value(false);
        float x = (args.size() > 1) ? args[1].asFloat(0.0f) : 0.0f;
        float y = (args.size() > 2) ? args[2].asFloat(0.0f) : 0.0f;
        float w = (args.size() > 3) ? args[3].asFloat(-1.0f) : -1.0f;
        float h = (args.size() > 4) ? args[4].asFloat(-1.0f) : -1.0f;
        canvas->drawSvg(args[0].asString(), x, y, w, h);
        return eatsbits::eatscript::Value(true);
    });

    // 9. ctx.draw_svg_path(path_d, cx, cy, scale, color)
    eval.registerMemberFunction("ctx", "draw_svg_path", [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 5) return eatsbits::eatscript::Value(false);
        uint32_t col = args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt());
        canvas->drawSvgPath(args[0].asString(), args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    });

    // 10. Transforms
    eval.registerMemberFunction("ctx", "push", [canvas](const auto&, const auto&) {
        if (canvas) canvas->push();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "pop", [canvas](const auto&, const auto&) {
        if (canvas) canvas->pop();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "translate", [canvas](const auto& args, const auto&) {
        if (canvas && args.size() >= 2) canvas->translate(args[0].asFloat(), args[1].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "rotate", [canvas](const auto& args, const auto&) {
        if (canvas && !args.empty()) canvas->rotateDegrees(args[0].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "scale", [canvas](const auto& args, const auto&) {
        if (canvas && args.size() >= 2) canvas->scale(args[0].asFloat(), args[1].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "rotate_180", [canvas](const auto&, const auto&) {
        if (canvas) canvas->rotate180();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "flip_x", [canvas](const auto&, const auto&) {
        if (canvas) canvas->flipX();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "flip_y", [canvas](const auto&, const auto&) {
        if (canvas) canvas->flipY();
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "set_opacity", [canvas](const auto& args, const auto&) {
        if (canvas && !args.empty()) canvas->setOpacity(args[0].asFloat());
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "set_tint", [canvas](const auto& args, const auto&) {
        if (canvas && !args.empty()) {
            uint32_t col = args[0].isString() ? CardCanvas::parseColor(args[0].asString()) : static_cast<uint32_t>(args[0].asInt());
            canvas->setTint(col);
        }
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "set_corner_radius", [canvas](const auto& args, const auto& kwargs) {
        if (canvas) {
            float r = eatsbits::eatscript::resolveArg(args, kwargs, 0, "radius", eatsbits::eatscript::Value(24.0)).asFloat(24.0f);
            canvas->setCornerRadius(r);
            CardTextureCache::instance().setDeckCornerRadius(r);
        }
        return eatsbits::eatscript::Value(true);
    });

    // 11. High-Level Classic Deck Functions
    auto drawSuitFn = [canvas](const auto& args, const auto&) {
        if (!canvas || args.size() < 4) return eatsbits::eatscript::Value(false);
        Suit s = CardCanvas::parseSuit(args[0].asString());
        uint32_t col = (args.size() > 4) ? (args[4].isString() ? CardCanvas::parseColor(args[4].asString()) : static_cast<uint32_t>(args[4].asInt())) : 0;
        canvas->drawSuit(s, args[1].asFloat(), args[2].asFloat(), args[3].asFloat(), col);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCornerIndicesFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("A"));
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));
        float fSize = eatsbits::eatscript::resolveArg(args, kwargs, 5, "font_size", eatsbits::eatscript::Value(28.0)).asFloat(28.0f);
        canvas->drawCornerIndices(rankVal.asString(), CardCanvas::parseSuit(suitVal.asString()), w, h, col, fSize);
        return eatsbits::eatscript::Value(true);
    };

    auto drawPipLayoutFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("1"));
        int r = rankVal.isString() ? CardCanvas::parseRank(rankVal.asString()) : rankVal.asInt();
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(suitVal.asString());
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));
        std::string variant = eatsbits::eatscript::resolveArg(args, kwargs, 5, "variant", eatsbits::eatscript::Value("classic")).asString();
        canvas->drawPipLayout(r, s, w, h, col, variant);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCourtFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("K"));
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(suitVal.asString());
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 2, "x", eatsbits::eatscript::Value(0.0)).asFloat();
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 3, "y", eatsbits::eatscript::Value(0.0)).asFloat();
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 4, "w", eatsbits::eatscript::Value(200.0)).asFloat(200.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 5, "h", eatsbits::eatscript::Value(300.0)).asFloat(300.0f);
        float op = eatsbits::eatscript::resolveArg(args, kwargs, 6, "opacity", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        auto tintVal = eatsbits::eatscript::resolveArg(args, kwargs, 7, "tint", eatsbits::eatscript::Value());
        uint32_t tint = tintVal.isString() ? CardCanvas::parseColor(tintVal.asString()) : static_cast<uint32_t>(tintVal.asInt(0));
        canvas->drawCourt(rankVal.asString(), s, x, y, w, h, op, tint);
        return eatsbits::eatscript::Value(true);
    };

    auto drawCardFaceFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("A"));
        auto suitVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(suitVal.asString());
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));
        std::string layout = eatsbits::eatscript::resolveArg(args, kwargs, 5, "layout", eatsbits::eatscript::Value("classic")).asString();
        canvas->drawCardFace(rankVal.asString(), s, w, h, col, layout);
        return eatsbits::eatscript::Value(true);
    };

    auto drawRankFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rankVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("A"));
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 1, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 2, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float size = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(24.0)).asFloat(24.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        canvas->drawRank(rankVal.asString(), x, y, size, col);
        return eatsbits::eatscript::Value(true);
    };

    auto drawTextFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto textVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "text", eatsbits::eatscript::Value(""));
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 1, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 2, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float size = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(16.0)).asFloat(16.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        std::string align = eatsbits::eatscript::resolveArg(args, kwargs, 5, "align", eatsbits::eatscript::Value("left")).asString();
        canvas->drawText(textVal.asString(), x, y, size, col, align);
        return eatsbits::eatscript::Value(true);
    };

    auto drawPixelSpriteFn = [canvas](const std::vector<eatsbits::eatscript::Value>& args,
                                      const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        if (!canvas || args.empty()) return eatsbits::eatscript::Value(false);
        std::vector<std::string> lines;
        const auto& spriteVal = args[0];
        if (spriteVal.isList()) {
            for (const auto& item : spriteVal.listVal) {
                lines.push_back(item.asString());
            }
        } else if (spriteVal.isString()) {
            std::stringstream ss(spriteVal.asString());
            std::string line;
            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cx", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cy", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float pSize = eatsbits::eatscript::resolveArg(args, kwargs, 3, "pixel_size", eatsbits::eatscript::Value(4.0)).asFloat(4.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#000000"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFF000000));
        auto onVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "on_char", eatsbits::eatscript::Value("#"));
        std::string onCharStr = onVal.asString();
        char onChar = onCharStr.empty() ? '#' : onCharStr[0];
        bool flipY = eatsbits::eatscript::resolveArg(args, kwargs, 6, "flip_y", eatsbits::eatscript::Value(false)).asBoolean(false);
        bool flipX = eatsbits::eatscript::resolveArg(args, kwargs, 7, "flip_x", eatsbits::eatscript::Value(false)).asBoolean(false);

        canvas->drawPixelSprite(lines, cx, cy, pSize, col, onChar, flipY, flipX);
        return eatsbits::eatscript::Value(true);
    };

    eval.setVariable("classic_deck", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    eval.registerMemberFunction("classic_deck", "draw_suit", drawSuitFn);
    eval.registerMemberFunction("classic_deck", "draw_corner_indices", drawCornerIndicesFn);
    eval.registerMemberFunction("classic_deck", "draw_pip_layout", drawPipLayoutFn);
    eval.registerMemberFunction("classic_deck", "draw_court", drawCourtFn);
    eval.registerMemberFunction("classic_deck", "draw_card_face", drawCardFaceFn);
    eval.registerMemberFunction("classic_deck", "draw_rank", drawRankFn);
    eval.registerMemberFunction("classic_deck", "draw_text", drawTextFn);
    eval.registerMemberFunction("classic_deck", "draw_pixel_sprite", drawPixelSpriteFn);

    eval.registerMemberFunction("ctx", "draw_suit", drawSuitFn);
    eval.registerMemberFunction("ctx", "draw_corner_indices", drawCornerIndicesFn);
    eval.registerMemberFunction("ctx", "draw_pip_layout", drawPipLayoutFn);
    eval.registerMemberFunction("ctx", "draw_court", drawCourtFn);
    eval.registerMemberFunction("ctx", "draw_card_face", drawCardFaceFn);
    eval.registerMemberFunction("ctx", "draw_rank", drawRankFn);
    eval.registerMemberFunction("ctx", "draw_text", drawTextFn);
    eval.registerMemberFunction("ctx", "draw_pixel_sprite", drawPixelSpriteFn);

    // 12. Ovals
    eval.registerMemberFunction("ctx", "fill_oval", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        canvas->fillOval(cx, cy, rx, ry, col);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "stroke_oval", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value("#ffffff"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFFFFFFF));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 5, "line_width", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        canvas->strokeOval(cx, cy, rx, ry, col, lw);
        return eatsbits::eatscript::Value(true);
    });

    // 13. Symmetry Scope
    eval.registerMemberFunction("ctx", "begin_symmetry", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        bool mx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "mirror_x", eatsbits::eatscript::Value(true)).asBoolean(true);
        bool my = eatsbits::eatscript::resolveArg(args, kwargs, 1, "mirror_y", eatsbits::eatscript::Value(true)).asBoolean(true);
        canvas->beginSymmetry(mx, my);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "end_symmetry", [canvas](const auto&, const auto&) {
        if (canvas) canvas->endSymmetry();
        return eatsbits::eatscript::Value(true);
    });

    // 14. Baroque Architectural Primitives
    eval.registerMemberFunction("ctx", "draw_volute", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float r = eatsbits::eatscript::resolveArg(args, kwargs, 2, "radius", eatsbits::eatscript::Value(30.0)).asFloat(30.0f);
        float turns = eatsbits::eatscript::resolveArg(args, kwargs, 3, "turns", eatsbits::eatscript::Value(1.25)).asFloat(1.25f);
        float aStart = eatsbits::eatscript::resolveArg(args, kwargs, 4, "angle_start", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFD4AF37));
        float width = eatsbits::eatscript::resolveArg(args, kwargs, 6, "width", eatsbits::eatscript::Value(3.5)).asFloat(3.5f);
        std::string prof = eatsbits::eatscript::resolveArg(args, kwargs, 7, "profile", eatsbits::eatscript::Value("fluted_ridge")).asString();

        canvas->drawVolute(cx, cy, r, turns, aStart, col, width, prof);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_acanthus", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x1 = 0.0f, y1 = 0.0f, x2 = 100.0f, y2 = 100.0f;
        if (kwargs.find("from") != kwargs.end()) {
            auto fVal = kwargs.at("from");
            if (fVal.isList() && fVal.listVal.size() >= 2) {
                x1 = fVal.listVal[0].asFloat();
                y1 = fVal.listVal[1].asFloat();
            }
        }
        if (kwargs.find("to") != kwargs.end()) {
            auto tVal = kwargs.at("to");
            if (tVal.isList() && tVal.listVal.size() >= 2) {
                x2 = tVal.listVal[0].asFloat();
                y2 = tVal.listVal[1].asFloat();
            }
        }
        int lobes = eatsbits::eatscript::resolveArg(args, kwargs, 4, "lobes", eatsbits::eatscript::Value(4)).asInt(4);
        std::string curlDir = eatsbits::eatscript::resolveArg(args, kwargs, 5, "curl_dir", eatsbits::eatscript::Value("left")).asString();
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 6, "color", eatsbits::eatscript::Value("#EDE6D6"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFEDE6D6));
        auto sColVal = eatsbits::eatscript::resolveArg(args, kwargs, 7, "stroke", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t sCol = sColVal.isString() ? CardCanvas::parseColor(sColVal.asString()) : static_cast<uint32_t>(sColVal.asInt(0xFFD4AF37));
        float sWidth = eatsbits::eatscript::resolveArg(args, kwargs, 8, "stroke_width", eatsbits::eatscript::Value(1.2)).asFloat(1.2f);

        canvas->drawAcanthus(x1, y1, x2, y2, lobes, curlDir, col, sCol, sWidth);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "repeat_beads", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float inset = eatsbits::eatscript::resolveArg(args, kwargs, 0, "inset", eatsbits::eatscript::Value(12.0)).asFloat(12.0f);
        float radius = eatsbits::eatscript::resolveArg(args, kwargs, 1, "radius", eatsbits::eatscript::Value(3.5)).asFloat(3.5f);
        float spacing = eatsbits::eatscript::resolveArg(args, kwargs, 2, "spacing", eatsbits::eatscript::Value(10.0)).asFloat(10.0f);
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFD4AF37));

        canvas->repeatBeads(inset, radius, spacing, col);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "stroke_cartouche", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(100.0)).asFloat(100.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(140.0)).asFloat(140.0f);
        std::string style = eatsbits::eatscript::resolveArg(args, kwargs, 4, "style", eatsbits::eatscript::Value("baroque_oval")).asString();
        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0xFFD4AF37));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 6, "line_width", eatsbits::eatscript::Value(2.5)).asFloat(2.5f);

        canvas->strokeCartouche(cx, cy, rx, ry, style, col, lw);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_ornate_suit", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto sVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(sVal.asString());
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(100.0)).asFloat(100.0f);
        auto bodyVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "body_color", eatsbits::eatscript::Value());
        uint32_t bodyCol = bodyVal.isString() ? CardCanvas::parseColor(bodyVal.asString()) : static_cast<uint32_t>(bodyVal.asInt(0));
        auto filigreeVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "filigree_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t filigreeCol = filigreeVal.isString() ? CardCanvas::parseColor(filigreeVal.asString()) : static_cast<uint32_t>(filigreeVal.asInt(0xFFD4AF37));

        canvas->drawOrnateSuit(s, cx, cy, sz, bodyCol, filigreeCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_corner", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 0, "x", eatsbits::eatscript::Value(0.0)).asFloat();
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 1, "y", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 2, "size", eatsbits::eatscript::Value(80.0)).asFloat(80.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "gold_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));

        canvas->drawBaroqueCorner(x, y, sz, gCol, aCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_cartouche", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(110.0)).asFloat(110.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(140.0)).asFloat(140.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "gold_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 6, "line_width", eatsbits::eatscript::Value(2.8)).asFloat(2.8f);

        canvas->drawBaroqueCartouche(cx, cy, rx, ry, gCol, aCol, lw);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_crest", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 2, "scale", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "gold_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));
        bool flipY = eatsbits::eatscript::resolveArg(args, kwargs, 5, "flip_y", eatsbits::eatscript::Value(false)).asBoolean(false);

        canvas->drawBaroqueCrest(cx, cy, sz, gCol, aCol, flipY);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_spade", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 2, "size", eatsbits::eatscript::Value(160.0)).asFloat(160.0f);
        auto bVal = eatsbits::eatscript::resolveArg(args, kwargs, 3, "body_color", eatsbits::eatscript::Value("#14151A"));
        uint32_t bCol = bVal.isString() ? CardCanvas::parseColor(bVal.asString()) : static_cast<uint32_t>(bVal.asInt(0xFF14151A));
        auto fVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "filigree_color", eatsbits::eatscript::Value("#D4AF37"));
        uint32_t fCol = fVal.isString() ? CardCanvas::parseColor(fVal.asString()) : static_cast<uint32_t>(fVal.asInt(0xFFD4AF37));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "accent_color", eatsbits::eatscript::Value("#FFF8DC"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF8DC));

        canvas->drawBaroqueSpade(cx, cy, sz, bCol, fCol, aCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_under_foliage", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(0.0)).asFloat();
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(0.0)).asFloat();
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(110.0)).asFloat(110.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(140.0)).asFloat(140.0f);
        auto fVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "foliage_color", eatsbits::eatscript::Value("#232730"));
        uint32_t fCol = fVal.isString() ? CardCanvas::parseColor(fVal.asString()) : static_cast<uint32_t>(fVal.asInt(0xFF232730));
        auto hVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "highlight_color", eatsbits::eatscript::Value("#3D4452"));
        uint32_t hCol = hVal.isString() ? CardCanvas::parseColor(hVal.asString()) : static_cast<uint32_t>(hVal.asInt(0xFF3D4452));

        canvas->drawBaroqueUnderFoliage(cx, cy, rx, ry, fCol, hCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "set_elevation", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float zBase = eatsbits::eatscript::resolveArg(args, kwargs, 0, "z_base", eatsbits::eatscript::Value(0.75)).asFloat(0.75f);
        float bulge = eatsbits::eatscript::resolveArg(args, kwargs, 1, "bulge", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        canvas->setElevation(zBase, bulge);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "reset_elevation", [canvas](const auto&, const auto&) {
        if (canvas) canvas->resetElevation();
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "set_material", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        std::string mat = eatsbits::eatscript::resolveArg(args, kwargs, 0, "material", eatsbits::eatscript::Value("auto")).asString();
        canvas->setMaterial(mat);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "reset_material", [canvas](const auto&, const auto&) {
        if (canvas) canvas->resetMaterial();
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "begin_radial_symmetry", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        int nFold = eatsbits::eatscript::resolveArg(args, kwargs, 0, "n_fold", eatsbits::eatscript::Value(8)).asInt(8);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cx", eatsbits::eatscript::Value(-1.0)).asFloat(-1.0f);
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cy", eatsbits::eatscript::Value(-1.0)).asFloat(-1.0f);
        bool mir = eatsbits::eatscript::resolveArg(args, kwargs, 3, "mirror_sectors", eatsbits::eatscript::Value(false)).asBoolean(false);
        canvas->beginRadialSymmetry(nFold, cx, cy, mir);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "end_radial_symmetry", [canvas](const auto&, const auto&) {
        if (canvas) canvas->endRadialSymmetry();
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_arch", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 0, "cx", eatsbits::eatscript::Value(180.0)).asFloat(180.0f);
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cy", eatsbits::eatscript::Value(252.0)).asFloat(252.0f);
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "rx", eatsbits::eatscript::Value(136.0)).asFloat(136.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ry", eatsbits::eatscript::Value(195.0)).asFloat(195.0f);
        std::string style = eatsbits::eatscript::resolveArg(args, kwargs, 4, "style", eatsbits::eatscript::Value("oval_clasp")).asString();
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "gold_color", eatsbits::eatscript::Value("#C5A869"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFC5A869));
        auto aVal = eatsbits::eatscript::resolveArg(args, kwargs, 6, "accent_color", eatsbits::eatscript::Value("#FFF3D1"));
        uint32_t aCol = aVal.isString() ? CardCanvas::parseColor(aVal.asString()) : static_cast<uint32_t>(aVal.asInt(0xFFFFF3D1));
        float lw = eatsbits::eatscript::resolveArg(args, kwargs, 7, "line_width", eatsbits::eatscript::Value(2.6)).asFloat(2.6f);
        canvas->drawBaroqueArch(cx, cy, rx, ry, style, gCol, aCol, lw);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_gem_pip", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto sVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "suit", eatsbits::eatscript::Value("hearts"));
        Suit s = CardCanvas::parseSuit(sVal.asString());
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 1, "cx", eatsbits::eatscript::Value(180.0)).asFloat(180.0f);
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cy", eatsbits::eatscript::Value(252.0)).asFloat(252.0f);
        float sz = eatsbits::eatscript::resolveArg(args, kwargs, 3, "size", eatsbits::eatscript::Value(60.0)).asFloat(60.0f);
        auto gemVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "gem_color", eatsbits::eatscript::Value());
        uint32_t gemCol = gemVal.isString() ? CardCanvas::parseColor(gemVal.asString()) : static_cast<uint32_t>(gemVal.asInt(0));
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "gold_color", eatsbits::eatscript::Value("#C5A869"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFC5A869));
        std::string style = eatsbits::eatscript::resolveArg(args, kwargs, 6, "style", eatsbits::eatscript::Value("auto")).asString();
        canvas->drawGemPip(s, cx, cy, sz, gemCol, gCol, style);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_pip_trellis", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto rVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("2"));
        int r = rVal.isString() ? CardCanvas::parseRank(rVal.asString()) : rVal.asInt(2);
        auto sVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(sVal.asString());
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 2, "card_w", eatsbits::eatscript::Value(360.0)).asFloat(360.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 3, "card_h", eatsbits::eatscript::Value(504.0)).asFloat(504.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "gold_color", eatsbits::eatscript::Value("#C5A869"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFC5A869));
        auto silVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "silver_color", eatsbits::eatscript::Value("#6E737B"));
        uint32_t silCol = silVal.isString() ? CardCanvas::parseColor(silVal.asString()) : static_cast<uint32_t>(silVal.asInt(0xFF6E737B));
        canvas->drawPipTrellis(r, s, w, h, gCol, silCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_baroque_court_bust", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        std::string rStr = eatsbits::eatscript::resolveArg(args, kwargs, 0, "rank", eatsbits::eatscript::Value("K")).asString();
        auto sVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "suit", eatsbits::eatscript::Value("spades"));
        Suit s = CardCanvas::parseSuit(sVal.asString());
        float cx = eatsbits::eatscript::resolveArg(args, kwargs, 2, "cx", eatsbits::eatscript::Value(180.0)).asFloat(180.0f);
        float cy = eatsbits::eatscript::resolveArg(args, kwargs, 3, "cy", eatsbits::eatscript::Value(252.0)).asFloat(252.0f);
        float rx = eatsbits::eatscript::resolveArg(args, kwargs, 4, "rx", eatsbits::eatscript::Value(128.0)).asFloat(128.0f);
        float ry = eatsbits::eatscript::resolveArg(args, kwargs, 5, "ry", eatsbits::eatscript::Value(186.0)).asFloat(186.0f);
        auto gVal = eatsbits::eatscript::resolveArg(args, kwargs, 6, "gold_color", eatsbits::eatscript::Value("#C5A869"));
        uint32_t gCol = gVal.isString() ? CardCanvas::parseColor(gVal.asString()) : static_cast<uint32_t>(gVal.asInt(0xFFC5A869));
        auto silVal = eatsbits::eatscript::resolveArg(args, kwargs, 7, "silver_color", eatsbits::eatscript::Value("#B8BDC4"));
        uint32_t silCol = silVal.isString() ? CardCanvas::parseColor(silVal.asString()) : static_cast<uint32_t>(silVal.asInt(0xFFB8BDC4));
        auto gemVal = eatsbits::eatscript::resolveArg(args, kwargs, 8, "gem_color", eatsbits::eatscript::Value("#A80C18"));
        uint32_t gemCol = gemVal.isString() ? CardCanvas::parseColor(gemVal.asString()) : static_cast<uint32_t>(gemVal.asInt(0xFFA80C18));
        canvas->drawBaroqueCourtBust(rStr, s, cx, cy, rx, ry, gCol, silCol, gemCol);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "draw_sculpted_svg", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas || args.empty()) return eatsbits::eatscript::Value(false);
        std::string xml = eatsbits::eatscript::resolveArg(args, kwargs, 0, "svg_xml", eatsbits::eatscript::Value("")).asString();
        float x = eatsbits::eatscript::resolveArg(args, kwargs, 1, "x", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float y = eatsbits::eatscript::resolveArg(args, kwargs, 2, "y", eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float w = eatsbits::eatscript::resolveArg(args, kwargs, 3, "w", eatsbits::eatscript::Value(-1.0)).asFloat(-1.0f);
        float h = eatsbits::eatscript::resolveArg(args, kwargs, 4, "h", eatsbits::eatscript::Value(-1.0)).asFloat(-1.0f);
        float zMin = eatsbits::eatscript::resolveArg(args, kwargs, 5, "z_min", eatsbits::eatscript::Value(0.35)).asFloat(0.35f);
        float zMax = eatsbits::eatscript::resolveArg(args, kwargs, 6, "z_max", eatsbits::eatscript::Value(1.25)).asFloat(1.25f);
        float bulge = eatsbits::eatscript::resolveArg(args, kwargs, 7, "bulge", eatsbits::eatscript::Value(1.15)).asFloat(1.15f);
        canvas->drawSculptedSvg(xml, x, y, w, h, zMin, zMax, bulge);
        return eatsbits::eatscript::Value(true);
    });

    eval.registerMemberFunction("ctx", "apply_finish", [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        std::string style = eatsbits::eatscript::resolveArg(args, kwargs, 0, "style", eatsbits::eatscript::Value("relief")).asString();
        float az = eatsbits::eatscript::resolveArg(args, kwargs, 1, "light_azimuth", eatsbits::eatscript::Value(135.0)).asFloat(135.0f);
        float el = eatsbits::eatscript::resolveArg(args, kwargs, 2, "light_elevation", eatsbits::eatscript::Value(45.0)).asFloat(45.0f);
        float ao = eatsbits::eatscript::resolveArg(args, kwargs, 3, "ao_intensity", eatsbits::eatscript::Value(1.6)).asFloat(1.6f);
        float depth = eatsbits::eatscript::resolveArg(args, kwargs, 4, "relief_depth", eatsbits::eatscript::Value(4.0)).asFloat(4.0f);
        auto patVal = eatsbits::eatscript::resolveArg(args, kwargs, 5, "patina_color", eatsbits::eatscript::Value());
        uint32_t patCol = patVal.isString() ? CardCanvas::parseColor(patVal.asString()) : static_cast<uint32_t>(patVal.asInt(0));
        auto hlVal = eatsbits::eatscript::resolveArg(args, kwargs, 6, "highlight_color", eatsbits::eatscript::Value());
        uint32_t hlCol = hlVal.isString() ? CardCanvas::parseColor(hlVal.asString()) : static_cast<uint32_t>(hlVal.asInt(0));
        float grain = eatsbits::eatscript::resolveArg(args, kwargs, 7, "paper_grain", eatsbits::eatscript::Value(0.15)).asFloat(0.15f);
        float bleed = eatsbits::eatscript::resolveArg(args, kwargs, 8, "ink_bleed", eatsbits::eatscript::Value(0.20)).asFloat(0.20f);

        canvas->applyFinish(style, az, el, ao, depth, patCol, hlCol, grain, bleed);
        return eatsbits::eatscript::Value(true);
    });

    auto applyFilterFn = [canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        std::string effect = eatsbits::eatscript::resolveArg(args, kwargs, 0, "effect", eatsbits::eatscript::Value("toon")).asString();
        float intensity = eatsbits::eatscript::resolveArg(args, kwargs, 1, "intensity", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);

        // Resolve effect-specific alias keywords onto param1 and param2
        float p1 = eatsbits::eatscript::resolveArg(args, kwargs, 2, "param1", eatsbits::eatscript::Value(-1.0)).asFloat(-1.0f);
        if (p1 < 0.0f) {
            for (const char* k : {"levels", "focus_y", "curvature", "hatch_density", "aberration", "threshold", "radius"}) {
                if (kwargs.find(k) != kwargs.end()) {
                    p1 = kwargs.at(k).asFloat(-1.0f);
                    break;
                }
            }
        }

        float p2 = eatsbits::eatscript::resolveArg(args, kwargs, 3, "param2", eatsbits::eatscript::Value(-1.0)).asFloat(-1.0f);
        if (p2 < 0.0f) {
            for (const char* k : {"outline", "edge_strength", "focus_width", "scale", "scanlines", "color_mix", "jitter", "softness", "blur_radius", "edge_darkening"}) {
                if (kwargs.find(k) != kwargs.end()) {
                    p2 = kwargs.at(k).asFloat(-1.0f);
                    break;
                }
            }
        }

        auto colVal = eatsbits::eatscript::resolveArg(args, kwargs, 4, "color", eatsbits::eatscript::Value());
        if (!colVal.isString() && colVal.asInt(0) == 0 && kwargs.find("tint") != kwargs.end()) {
            colVal = kwargs.at("tint");
        }
        uint32_t col = colVal.isString() ? CardCanvas::parseColor(colVal.asString()) : static_cast<uint32_t>(colVal.asInt(0));

        canvas->applyFilter(effect, intensity, p1, p2, col);
        return eatsbits::eatscript::Value(true);
    };

    eval.registerMemberFunction("ctx", "apply_filter", applyFilterFn);
    eval.registerMemberFunction("ctx", "apply_post_process", applyFilterFn);

    auto applyShaderFn = [&eval, canvas](const auto& args, const auto& kwargs) {
        if (!canvas) return eatsbits::eatscript::Value(false);
        auto fnVal = eatsbits::eatscript::resolveArg(args, kwargs, 0, "shader",
                     eatsbits::eatscript::resolveArg(args, kwargs, 0, "func", eatsbits::eatscript::Value("")));

        eatsbits::eatscript::FunctionDef* funcDef = nullptr;
        std::string inlineCode;
        if (fnVal.isFunction() && fnVal.funcDef) {
            funcDef = fnVal.funcDef.get();
        } else if (fnVal.isString()) {
            const std::string& s = fnVal.asString();
            if (eval.hasVariable(s)) {
                auto v = eval.getVariable(s);
                if (v.isFunction() && v.funcDef) {
                    funcDef = v.funcDef.get();
                }
            }
            if (!funcDef && s.find("def ") != std::string::npos) {
                inlineCode = s;
            }
        }

        auto rVal = eatsbits::eatscript::resolveArg(args, kwargs, 1, "rank", eatsbits::eatscript::Value(1));
        int r = rVal.isString() ? CardCanvas::parseRank(rVal.asString()) : rVal.asInt(1);
        auto sVal = eatsbits::eatscript::resolveArg(args, kwargs, 2, "suit", eatsbits::eatscript::Value("spades"));
        Suit suit = sVal.isString() ? CardCanvas::parseSuit(sVal.asString()) : static_cast<Suit>( std::clamp(sVal.asInt(0), 0, 3) );
        float intensity = eatsbits::eatscript::resolveArg(args, kwargs, 3, "intensity", eatsbits::eatscript::Value(1.0)).asFloat(1.0f);
        float u0 = eatsbits::eatscript::resolveArg(args, kwargs, 4, "u0", eval.hasVariable("tint_r") ? eval.getVariable("tint_r") : eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float u1 = eatsbits::eatscript::resolveArg(args, kwargs, 5, "u1", eval.hasVariable("tint_g") ? eval.getVariable("tint_g") : eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float u2 = eatsbits::eatscript::resolveArg(args, kwargs, 6, "u2", eval.hasVariable("tint_b") ? eval.getVariable("tint_b") : eatsbits::eatscript::Value(0.0)).asFloat(0.0f);
        float u3 = eatsbits::eatscript::resolveArg(args, kwargs, 7, "u3", eval.hasVariable("tint_active") ? eval.getVariable("tint_active") : eatsbits::eatscript::Value(0.0)).asFloat(0.0f);

        canvas->applyShader(funcDef, inlineCode, r, suit, intensity, u0, u1, u2, u3);
        return eatsbits::eatscript::Value(true);
    };

    eval.registerMemberFunction("ctx", "apply_shader", applyShaderFn);
    eval.registerMemberFunction("ctx", "draw_shader", applyShaderFn);
}

} // namespace solengine
