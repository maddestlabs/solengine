#include "solengine/card_back.hpp"
#include "solengine/card_canvas.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

#include "stb_image.h"
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace solengine {

namespace {

void bilinearResample(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH) {
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    for (int y = 0; y < dstH; ++y) {
        float srcY = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, srcH - 1);
        int y1 = std::clamp(y0 + 1, 0, srcH - 1);
        float fy = srcY - std::floor(srcY);

        for (int x = 0; x < dstW; ++x) {
            float srcX = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, srcW - 1);
            int x1 = std::clamp(x0 + 1, 0, srcW - 1);
            float fx = srcX - std::floor(srcX);

            const uint8_t* p00 = &src[(y0 * srcW + x0) * 4];
            const uint8_t* p10 = &src[(y0 * srcW + x1) * 4];
            const uint8_t* p01 = &src[(y1 * srcW + x0) * 4];
            const uint8_t* p11 = &src[(y1 * srcW + x1) * 4];

            uint8_t* out = &dst[(y * dstW + x) * 4];
            for (int c = 0; c < 4; ++c) {
                float top = p00[c] * (1.0f - fx) + p10[c] * fx;
                float bot = p01[c] * (1.0f - fx) + p11[c] * fx;
                out[c] = static_cast<uint8_t>(std::clamp(top * (1.0f - fy) + bot * fy + 0.5f, 0.0f, 255.0f));
            }
        }
    }
}

std::string cleanNameFromId(const std::string& id) {
    if (id.empty()) return "Card Back";
    std::string name = id;
    // Replace underscores and hyphens with spaces
    for (char& c : name) {
        if (c == '_' || c == '-') c = ' ';
    }
    // Capitalize words
    bool newWord = true;
    for (char& c : name) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            newWord = true;
        } else if (newWord) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            newWord = false;
        }
    }
    return name;
}

bool clipLine(float& x0, float& y0, float& x1, float& y1,
              float minX, float minY, float maxX, float maxY) {
    constexpr int INSIDE = 0; // 0000
    constexpr int LEFT   = 1; // 0001
    constexpr int RIGHT  = 2; // 0010
    constexpr int BOTTOM = 4; // 0100
    constexpr int TOP    = 8; // 1000

    auto computeOutCode = [minX, minY, maxX, maxY](float x, float y) -> int {
        int code = INSIDE;
        if (x < minX)      code |= LEFT;
        else if (x > maxX) code |= RIGHT;
        if (y < minY)      code |= TOP;
        else if (y > maxY) code |= BOTTOM;
        return code;
    };

    int outcode0 = computeOutCode(x0, y0);
    int outcode1 = computeOutCode(x1, y1);
    bool accept = false;

    while (true) {
        if (!(outcode0 | outcode1)) {
            accept = true;
            break;
        } else if (outcode0 & outcode1) {
            break;
        } else {
            float x = 0.0f, y = 0.0f;
            int outcodeOut = outcode0 ? outcode0 : outcode1;

            if (outcodeOut & TOP) {
                x = x0 + (x1 - x0) * (minY - y0) / (y1 - y0);
                y = minY;
            } else if (outcodeOut & BOTTOM) {
                x = x0 + (x1 - x0) * (maxY - y0) / (y1 - y0);
                y = maxY;
            } else if (outcodeOut & RIGHT) {
                y = y0 + (y1 - y0) * (maxX - x0) / (x1 - x0);
                x = maxX;
            } else if (outcodeOut & LEFT) {
                y = y0 + (y1 - y0) * (minX - x0) / (x1 - x0);
                x = minX;
            }

            if (outcodeOut == outcode0) {
                x0 = x;
                y0 = y;
                outcode0 = computeOutCode(x0, y0);
            } else {
                x1 = x;
                y1 = y;
                outcode1 = computeOutCode(x1, y1);
            }
        }
    }
    return accept;
}

void drawClippedLine(CardCanvas& ctx, float x0, float y0, float x1, float y1,
                     float minX, float minY, float maxX, float maxY,
                     uint32_t color, float lineWidth = 1.0f) {
    if (clipLine(x0, y0, x1, y1, minX, minY, maxX, maxY)) {
        ctx.drawLine(x0, y0, x1, y1, color, lineWidth);
    }
}

} // namespace

CardBackRegistry& CardBackRegistry::instance() {
    static CardBackRegistry sInstance;
    return sInstance;
}

bool CardBackRegistry::bakeCanvasBack(
    CardTexture& outTex,
    int cardW,
    int cardH,
    const std::function<void(CardCanvas& canvas, int w, int h)>& drawFn
) {
    if (cardW <= 0 || cardH <= 0 || !drawFn) return false;
    CardCanvas canvas(cardW, cardH);
    drawFn(canvas, cardW, cardH);
    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba = std::move(canvas.rgba());
    return true;
}

bool CardBackRegistry::loadBitmapBack(CardTexture& outTex, const std::string& filePath, int cardW, int cardH) {
    if (cardW <= 0 || cardH <= 0) return false;
    int srcW = 0, srcH = 0, channels = 0;
    stbi_uc* data = stbi_load(filePath.c_str(), &srcW, &srcH, &channels, 4);
    if (!data) return false;

    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

    if (srcW == cardW && srcH == cardH) {
        std::memcpy(outTex.rgba.data(), data, outTex.rgba.size());
    } else {
        bilinearResample(data, srcW, srcH, outTex.rgba.data(), cardW, cardH);
    }
    stbi_image_free(data);
    return true;
}

bool CardBackRegistry::loadMemoryBack(CardTexture& outTex, const uint8_t* data, size_t size, const std::string& format, int cardW, int cardH) {
    if (!data || size == 0 || cardW <= 0 || cardH <= 0) return false;
    if (format == "svg") {
        std::string svgStr(reinterpret_cast<const char*>(data), size);
        return loadSvgBack(outTex, svgStr, cardW, cardH);
    }

    int srcW = 0, srcH = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(size), &srcW, &srcH, &channels, 4);
    if (!decoded) return false;

    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

    if (srcW == cardW && srcH == cardH) {
        std::memcpy(outTex.rgba.data(), decoded, outTex.rgba.size());
    } else {
        bilinearResample(decoded, srcW, srcH, outTex.rgba.data(), cardW, cardH);
    }
    stbi_image_free(decoded);
    return true;
}

bool CardBackRegistry::loadSvgBack(CardTexture& outTex, const std::string& svgSource, int cardW, int cardH) {
    if (svgSource.empty() || cardW <= 0 || cardH <= 0) return false;
    std::vector<char> xmlCopy(svgSource.begin(), svgSource.end());
    xmlCopy.push_back('\0');

    NSVGimage* image = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!image) return false;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return false;
    }

    float scaleX = (image->width > 0.0f) ? (static_cast<float>(cardW) / image->width) : 1.0f;
    float scaleY = (image->height > 0.0f) ? (static_cast<float>(cardH) / image->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba.assign(static_cast<size_t>(cardW * cardH * 4), 255);

    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, outTex.rgba.data(), cardW, cardH, cardW * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return true;
}

void CardBackRegistry::initialize(int cardW, int cardH) {
    if (initialized_ && currentCardW_ == cardW && currentCardH_ == cardH) return;
    initialized_ = true;
    currentCardW_ = cardW;
    currentCardH_ = cardH;
    universalBacks_.clear();

    // 1. SolEnshade Minimalist
    {
        CardBackDescriptor d;
        d.id = "solenshade";
        d.displayName = "SolEnshade Charcoal";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFF18181B);
                ctx.strokeRoundedRect(0.5f, 0.5f, fw - 1.0f, fh - 1.0f, 6.0f, 0xFF27272A, 1.0f);
                ctx.strokeRoundedRect(12.0f, 12.0f, fw - 24.0f, fh - 24.0f, 4.0f, 0xFF3F3F46, 1.2f);
                // Center diamond geometry
                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                const float rw = fw * 0.18f;
                const float rh = fh * 0.18f;
                ctx.drawLine(cx, cy - rh, cx + rw, cy, 0xFF71717A, 1.5f);
                ctx.drawLine(cx + rw, cy, cx, cy + rh, 0xFF71717A, 1.5f);
                ctx.drawLine(cx, cy + rh, cx - rw, cy, 0xFF71717A, 1.5f);
                ctx.drawLine(cx - rw, cy, cx, cy - rh, 0xFF71717A, 1.5f);
                ctx.fillCircle(cx, cy, 3.5f, 0xFFA1A1AA);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 2. Classic Sapphire Tartan
    {
        CardBackDescriptor d;
        d.id = "classic_sapphire";
        d.displayName = "Classic Sapphire";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                // Ivory card border
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFFFBF8F2);
                ctx.strokeRoundedRect(0.5f, 0.5f, fw - 1.0f, fh - 1.0f, 6.0f, 0xFFD8D0C4, 1.0f);

                // Deep sapphire inner panel
                const float pad = fw * 0.065f;
                ctx.fillRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFF14274E);
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFF1F6F9, 1.5f);

                // Ornate casino cross-hatch lattice
                const float innerPad = pad + 6.0f;
                const float minX = innerPad;
                const float maxX = fw - innerPad;
                const float minY = innerPad;
                const float maxY = fh - innerPad;
                const float step = 14.0f;
                for (float offset = -fh; offset < fw + fh; offset += step) {
                    drawClippedLine(ctx, minX, offset, maxX, offset + (maxX - minX), minX, minY, maxX, maxY, 0x22FFFFFF, 1.0f);
                    drawClippedLine(ctx, maxX, offset, minX, offset + (maxX - minX), minX, minY, maxX, maxY, 0x22FFFFFF, 1.0f);
                }

                // Central Cartouche & Ace of Spades rosette
                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                ctx.fillOval(cx, cy, fw * 0.22f, fh * 0.18f, 0xFF14274E);
                ctx.strokeOval(cx, cy, fw * 0.22f, fh * 0.18f, 0xFF9BA4B4, 1.8f);
                ctx.strokeOval(cx, cy, fw * 0.18f, fh * 0.15f, 0xFFF1F6F9, 1.2f);
                ctx.drawSuit(Suit::Spades, cx, cy - 4.0f, fw * 0.12f, 0xFFF1F6F9);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 3. Classic Crimson Baize
    {
        CardBackDescriptor d;
        d.id = "classic_crimson";
        d.displayName = "Classic Crimson";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                // Ivory card border
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFFFBF8F2);
                ctx.strokeRoundedRect(0.5f, 0.5f, fw - 1.0f, fh - 1.0f, 6.0f, 0xFFD8D0C4, 1.0f);

                // Regal crimson baize
                const float pad = fw * 0.065f;
                ctx.fillRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFF7A0C16);
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFE2C481, 1.6f);

                // Diamond lattice pattern
                const float innerPad = pad + 4.0f;
                const float minX = innerPad;
                const float maxX = fw - innerPad;
                const float minY = innerPad;
                const float maxY = fh - innerPad;
                const float step = 16.0f;
                for (float offset = -fh; offset < fw + fh; offset += step) {
                    drawClippedLine(ctx, minX, offset, maxX, offset + (maxX - minX), minX, minY, maxX, maxY, 0x24FFDE94, 1.0f);
                    drawClippedLine(ctx, maxX, offset, minX, offset + (maxX - minX), minX, minY, maxX, maxY, 0x24FFDE94, 1.0f);
                }

                // Center Gold Starburst Cartouche
                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                ctx.fillOval(cx, cy, fw * 0.22f, fh * 0.18f, 0xFF7A0C16);
                ctx.strokeOval(cx, cy, fw * 0.22f, fh * 0.18f, 0xFFE2C481, 2.0f);
                ctx.strokeOval(cx, cy, fw * 0.18f, fh * 0.15f, 0xFFFFF1C5, 1.0f);
                ctx.drawSuit(Suit::Diamonds, cx, cy, fw * 0.14f, 0xFFE2C481);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 4. Clockwork Brass (Victorian Steampunk)
    {
        CardBackDescriptor d;
        d.id = "clockwork_brass";
        d.displayName = "Clockwork Brass";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFFF8F2E6);
                const float pad = fw * 0.065f;
                ctx.fillRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFF3A1C18);
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFC8963E, 2.2f);

                const float innerPad = pad + fw * 0.045f;
                ctx.strokeRoundedRect(innerPad, innerPad, fw - innerPad * 2.0f, fh - innerPad * 2.0f, 2.0f, 0xFFB87333, 1.4f);

                // Clockwork gear medallion
                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                const float rMed = fw * 0.26f;
                ctx.fillCircle(cx, cy, rMed, 0xFF2A1210);
                ctx.strokeCircle(cx, cy, rMed, 0xFFC8963E, 2.4f);
                ctx.strokeCircle(cx, cy, rMed * 0.75f, 0xFFB87333, 1.6f);

                // Gear teeth
                constexpr int kTeeth = 16;
                for (int t = 0; t < kTeeth; ++t) {
                    float ang = static_cast<float>(t) * (6.2831853f / static_cast<float>(kTeeth));
                    float x0 = cx + std::cos(ang) * (rMed - 4.0f);
                    float y0 = cy + std::sin(ang) * (rMed - 4.0f);
                    float x1 = cx + std::cos(ang) * (rMed + 7.0f);
                    float y1 = cy + std::sin(ang) * (rMed + 7.0f);
                    ctx.drawLine(x0, y0, x1, y1, 0xFFC8963E, 3.2f);
                }
                ctx.fillCircle(cx, cy, 6.0f, 0xFFC8963E);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 5. Celestial Midnight
    {
        CardBackDescriptor d;
        d.id = "celestial_midnight";
        d.displayName = "Celestial Midnight";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFFF8FAFF);
                const float pad = fw * 0.065f;
                ctx.fillRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFF0D1B2A);
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFFFB703, 1.8f);

                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                // Astral rings
                ctx.strokeCircle(cx, cy, fw * 0.28f, 0x553A86FF, 1.4f);
                ctx.strokeCircle(cx, cy, fw * 0.20f, 0xFFFFB703, 1.8f);

                // Radiating 8-point starburst
                constexpr int kPoints = 8;
                for (int i = 0; i < kPoints; ++i) {
                    float ang = static_cast<float>(i) * (6.2831853f / static_cast<float>(kPoints));
                    float rLen = (i % 2 == 0) ? (fw * 0.25f) : (fw * 0.14f);
                    float x0 = cx; float y0 = cy;
                    float x1 = cx + std::cos(ang) * rLen;
                    float y1 = cy + std::sin(ang) * rLen;
                    ctx.drawLine(x0, y0, x1, y1, (i % 2 == 0) ? 0xFFFFB703 : 0xFFFFFFFF, 1.8f);
                }
                ctx.fillCircle(cx, cy, 4.5f, 0xFFFFFFFF);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 6. Obsidian Baroque
    {
        CardBackDescriptor d;
        d.id = "obsidian_baroque";
        d.displayName = "Obsidian Baroque";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFF14161B);
                const float pad = fw * 0.065f;
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFC5A869, 2.2f);

                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                // Baroque arches and acanthus cartouche
                ctx.drawBaroqueArch(cx, cy, fw * 0.35f, fh * 0.38f, "ogee_spires", 0xFFC5A869, 0xFFFFF3D1, 2.4f);
                ctx.drawGemPip(Suit::Diamonds, cx, cy, fw * 0.24f, 0xFFC91428, 0xFFC5A869, "faceted_gem");
                ctx.repeatBeads(pad + 10.0f, 2.2f, 8.0f, 0xFFC5A869);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 7. Emerald Filigree
    {
        CardBackDescriptor d;
        d.id = "emerald_filigree";
        d.displayName = "Emerald Filigree";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFFF8F5EE);
                const float pad = fw * 0.065f;
                ctx.fillRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFF0D3B2E);
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFE2C481, 1.8f);

                // Wire lattice
                const float innerPad = pad + 4.0f;
                const float minX = innerPad;
                const float maxX = fw - innerPad;
                const float minY = innerPad;
                const float maxY = fh - innerPad;
                const float step = 15.0f;
                for (float offset = -fh; offset < fw + fh; offset += step) {
                    drawClippedLine(ctx, minX, offset, maxX, offset + (maxX - minX), minX, minY, maxX, maxY, 0x22E2C481, 1.0f);
                    drawClippedLine(ctx, maxX, offset, minX, offset + (maxX - minX), minX, minY, maxX, maxY, 0x22E2C481, 1.0f);
                }

                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                ctx.fillCircle(cx, cy, fw * 0.22f, 0xFF0D3B2E);
                ctx.strokeCircle(cx, cy, fw * 0.22f, 0xFFE2C481, 2.0f);
                ctx.strokeCircle(cx, cy, fw * 0.17f, 0xFFE2C481, 1.0f);
                ctx.drawSuit(Suit::Clubs, cx, cy, fw * 0.12f, 0xFFE2C481);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 8. Cyberpunk Neon
    {
        CardBackDescriptor d;
        d.id = "cyber_neon";
        d.displayName = "Cyberpunk Neon";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                ctx.fillRoundedRect(0, 0, fw, fh, 6.0f, 0xFF0A0E17);
                const float pad = fw * 0.065f;
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFF00F0FF, 1.8f);

                const float innerPad = pad + fw * 0.045f;
                ctx.strokeRoundedRect(innerPad, innerPad, fw - innerPad * 2.0f, fh - innerPad * 2.0f, 2.0f, 0xFFFF007F, 1.2f);

                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;
                const float rw = fw * 0.18f;
                const float rh = fh * 0.18f;
                ctx.drawLine(cx, cy - rh, cx + rw, cy, 0xFF00F0FF, 2.0f);
                ctx.drawLine(cx + rw, cy, cx, cy + rh, 0xFF00F0FF, 2.0f);
                ctx.drawLine(cx, cy + rh, cx - rw, cy, 0xFF00F0FF, 2.0f);
                ctx.drawLine(cx - rw, cy, cx, cy - rh, 0xFF00F0FF, 2.0f);
                ctx.fillCircle(cx, cy, 4.0f, 0xFFFFE600);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }
}

void CardBackRegistry::registerUniversalBack(const CardBackDescriptor& desc) {
    for (auto& u : universalBacks_) {
        if (u.id == desc.id) {
            u = desc;
            return;
        }
    }
    universalBacks_.push_back(desc);
}

void CardBackRegistry::registerDeckBack(const CardBackDescriptor& desc) {
    for (auto& d : deckBacks_) {
        if (d.id == desc.id && d.deckOrigin == desc.deckOrigin) {
            d = desc;
            return;
        }
    }
    deckBacks_.push_back(desc);
}

void CardBackRegistry::clearDeckBacks(const std::string& deckOrigin) {
    deckBacks_.erase(
        std::remove_if(deckBacks_.begin(), deckBacks_.end(), [&](const CardBackDescriptor& d) {
            return d.deckOrigin == deckOrigin;
        }),
        deckBacks_.end()
    );
}

void CardBackRegistry::clearAllDeckBacks() {
    deckBacks_.clear();
}

std::vector<CardBackDescriptor> CardBackRegistry::getAvailableBacks(const std::string& deckOrigin) const {
    std::vector<CardBackDescriptor> result;

    // 1. Auto selection (dynamically resolves to deck's primary card back)
    const CardBackDescriptor* defBack = getDefaultBackForDeck(deckOrigin);
    std::string defName = defBack ? defBack->displayName : "Default";

    CardBackDescriptor autoDesc;
    autoDesc.id = "auto";
    autoDesc.displayName = "Auto (" + defName + ")";
    autoDesc.deckOrigin = deckOrigin;
    if (defBack) {
        autoDesc.baseTexture = defBack->baseTexture;
    }
    result.push_back(autoDesc);

    // 2. Deck-specific card backs (from the active deck)
    if (!deckOrigin.empty()) {
        for (const auto& d : deckBacks_) {
            if (d.deckOrigin == deckOrigin) {
                result.push_back(d);
            }
        }
    }

    // 3. Universal card backs (available across all decks)
    for (const auto& u : universalBacks_) {
        result.push_back(u);
    }

    return result;
}

const CardBackDescriptor* CardBackRegistry::findBack(const std::string& id, const std::string& deckOrigin) const {
    if (id == "auto" || id.empty()) {
        return getDefaultBackForDeck(deckOrigin);
    }

    // 1. Check deck-specific backs
    if (!deckOrigin.empty()) {
        for (const auto& d : deckBacks_) {
            if (d.id == id && d.deckOrigin == deckOrigin) {
                return &d;
            }
        }
    }

    // 2. Check universal backs
    for (const auto& u : universalBacks_) {
        if (u.id == id) {
            return &u;
        }
    }

    // Fallback: deck default or first universal back
    const auto* def = getDefaultBackForDeck(deckOrigin);
    if (def) return def;
    return universalBacks_.empty() ? nullptr : &universalBacks_.front();
}

const CardBackDescriptor* CardBackRegistry::getDefaultBackForDeck(const std::string& deckOrigin) const {
    // Check if deck has registered card backs
    if (!deckOrigin.empty()) {
        for (const auto& d : deckBacks_) {
            if (d.deckOrigin == deckOrigin) {
                return &d;
            }
        }
    }

    // Map known deck themes to their signature card back
    if (deckOrigin == "steampunk") {
        for (const auto& u : universalBacks_) if (u.id == "clockwork_brass") return &u;
    } else if (deckOrigin == "anime") {
        for (const auto& u : universalBacks_) if (u.id == "celestial_midnight") return &u;
    } else if (deckOrigin == "cyberpunk") {
        for (const auto& u : universalBacks_) if (u.id == "cyber_neon") return &u;
    } else if (deckOrigin.find("baroque") != std::string::npos) {
        for (const auto& u : universalBacks_) if (u.id == "obsidian_baroque") return &u;
    } else if (deckOrigin == "classic") {
        for (const auto& u : universalBacks_) if (u.id == "classic_sapphire") return &u;
    }

    // Default to SolEnshade minimal or first universal
    for (const auto& u : universalBacks_) {
        if (u.id == "solenshade") return &u;
    }
    return universalBacks_.empty() ? nullptr : &universalBacks_.front();
}

void CardBackRegistry::scanCompanionBacks(const std::string& decksDir, int cardW, int cardH) {
    std::error_code ec;
    if (!std::filesystem::exists(decksDir, ec) || !std::filesystem::is_directory(decksDir, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(decksDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".svg") {
            continue;
        }

        // Look for pattern: <theme>_back_<name>.<ext>
        size_t backPos = filename.find("_back_");
        if (backPos != std::string::npos) {
            std::string theme = filename.substr(0, backPos);
            size_t dotPos = filename.rfind('.');
            std::string backName = filename.substr(backPos + 6, dotPos - (backPos + 6));

            CardBackDescriptor desc;
            desc.id = theme + "_" + backName;
            desc.displayName = cleanNameFromId(backName);
            desc.deckOrigin = theme;
            desc.type = (ext == ".svg") ? CardBackType::Svg : CardBackType::Bitmap;

            bool ok = false;
            if (ext == ".svg") {
                ok = loadSvgBack(desc.baseTexture, entry.path().string(), cardW, cardH);
            } else {
                ok = loadBitmapBack(desc.baseTexture, entry.path().string(), cardW, cardH);
            }
            if (ok) {
                registerDeckBack(desc);
                std::cout << "[SolEngine] Registered companion card back: " << desc.displayName
                          << " (" << desc.id << ") for deck " << theme << "\n";
            }
        }
    }
}

} // namespace solengine

