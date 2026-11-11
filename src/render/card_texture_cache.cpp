#include "solengine/card_texture_cache.hpp"
#include "solengine/card_canvas.hpp"
#include "solengine/ui/clockwork_theme.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "stb_image.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996 4100 4127 4244)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace solengine {

CardTextureCache& CardTextureCache::instance() {
    static CardTextureCache s_instance;
    return s_instance;
}

static inline size_t svgCellIndexForCard(Suit s, Rank r) noexcept {
    size_t rIdx = (static_cast<size_t>(r) >= 1 && static_cast<size_t>(r) <= 13)
                  ? (static_cast<size_t>(r) - 1) : 0;
    switch (s) {
        case Suit::Spades:   return 0 + rIdx;
        case Suit::Hearts:   return 13 + rIdx;
        case Suit::Clubs:    return 26 + rIdx;
        case Suit::Diamonds: return 39 + rIdx;
    }
    return 0;
}

static void syncArgb32AndMips(std::array<CardTexture, CardTextureCache::kTotalTextures>& textures) {
    for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
        auto& tex = textures[i];
        if (tex.width <= 0 || tex.height <= 0 || tex.rgba.size() < static_cast<size_t>(tex.width * tex.height * 4)) continue;
        const int w = tex.width;
        const int h = tex.height;
        const size_t count = static_cast<size_t>(w * h);

        tex.argb32.resize(count);
        for (size_t p = 0; p < count; ++p) {
            uint32_t r = tex.rgba[p * 4 + 0];
            uint32_t g = tex.rgba[p * 4 + 1];
            uint32_t b = tex.rgba[p * 4 + 2];
            uint32_t a = tex.rgba[p * 4 + 3];
            tex.argb32[p] = (a << 24) | (r << 16) | (g << 8) | b;
        }

        // Generate Mip 1 (W/2 x H/2, 2x2 box-supersampled)
        if (w >= 100 && h >= 140) {
            const int w1 = w / 2;
            const int h1 = h / 2;
            tex.mip1.resize(static_cast<size_t>(w1 * h1));
            for (int y = 0; y < h1; ++y) {
                const int y2 = y * 2;
                const uint32_t* row0 = &tex.argb32[y2 * w];
                const uint32_t* row1 = &tex.argb32[(y2 + 1) * w];
                uint32_t* dst = &tex.mip1[y * w1];
                for (int x = 0; x < w1; ++x) {
                    const int x2 = x * 2;
                    uint32_t p00 = row0[x2],     p10 = row0[x2 + 1];
                    uint32_t p01 = row1[x2],     p11 = row1[x2 + 1];

                    uint32_t r = (((p00 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF) + 2) >> 2;
                    uint32_t g = (((p00 >> 8)  & 0xFF) + ((p10 >> 8)  & 0xFF) + ((p01 >> 8)  & 0xFF) + ((p11 >> 8)  & 0xFF) + 2) >> 2;
                    uint32_t b = ((p00 & 0xFF)         + (p10 & 0xFF)         + (p01 & 0xFF)         + (p11 & 0xFF)         + 2) >> 2;
                    uint32_t a = (((p00 >> 24) & 0xFF) + ((p10 >> 24) & 0xFF) + ((p01 >> 24) & 0xFF) + ((p11 >> 24) & 0xFF) + 2) >> 2;

                    dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }

            // Generate Mip 2 (W/4 x H/4, 4x4 box-supersampled)
            if (w1 >= 80 && h1 >= 100) {
                const int w2 = w1 / 2;
                const int h2 = h1 / 2;
                tex.mip2.resize(static_cast<size_t>(w2 * h2));
                for (int y = 0; y < h2; ++y) {
                    const int y2 = y * 2;
                    const uint32_t* row0 = &tex.mip1[y2 * w1];
                    const uint32_t* row1 = &tex.mip1[(y2 + 1) * w1];
                    uint32_t* dst = &tex.mip2[y * w2];
                    for (int x = 0; x < w2; ++x) {
                        const int x2 = x * 2;
                        uint32_t p00 = row0[x2],     p10 = row0[x2 + 1];
                        uint32_t p01 = row1[x2],     p11 = row1[x2 + 1];

                        uint32_t r = (((p00 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF) + 2) >> 2;
                        uint32_t g = (((p00 >> 8)  & 0xFF) + ((p10 >> 8)  & 0xFF) + ((p01 >> 8)  & 0xFF) + ((p11 >> 8)  & 0xFF) + 2) >> 2;
                        uint32_t b = ((p00 & 0xFF)         + (p10 & 0xFF)         + (p01 & 0xFF)         + (p11 & 0xFF)         + 2) >> 2;
                        uint32_t a = (((p00 >> 24) & 0xFF) + ((p10 >> 24) & 0xFF) + ((p01 >> 24) & 0xFF) + ((p11 >> 24) & 0xFF) + 2) >> 2;

                        dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
}

struct HandlingScratch {
    float x0{0.0f}, y0{0.0f};
    float len{0.0f};
    float cosA{1.0f}, sinA{0.0f};
    float curvature{0.0f};
    float sigma{1.2f};
    float strength{0.0f};
};

struct EdgeNotch {
    float sCenter{0.0f};
    float depth{0.0f};
    float width{0.0f};
    float raggedScale{0.3f};
};

static bool applyCardConditioning(
    const std::array<CardTexture, CardTextureCache::kTotalTextures>& srcTextures,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& dstTextures,
    CardWearLevel wearLevel,
    CardStock cardStock,
    const std::function<bool(float progress, const char* desc)>& onProgress = nullptr
) {
    // Fast-path: When both Stock: None and Wear: Pristine are active, skip paper & ink synthesis completely!
    if (cardStock == CardStock::None && wearLevel == CardWearLevel::Pristine) {
        for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
            if (onProgress && (i % 13 == 0 || i == CardTextureCache::kCardBackIndex)) {
                float frac = static_cast<float>(i) / static_cast<float>(CardTextureCache::kTotalTextures);
                if (!onProgress(frac, "Copying crisp unconditioned card artwork...")) {
                    return false;
                }
            }
            dstTextures[i].width = srcTextures[i].width;
            dstTextures[i].height = srcTextures[i].height;
            dstTextures[i].rgba = srcTextures[i].rgba;
        }
        return true;
    }

    float paperWarmth    = 0.40f;
    float stockStrength  = 0.080f;
    float inkSoftening   = 0.25f;
    float edgePatina     = 0.0f;
    float edgeScuff      = 0.0f;
    float deckleFray     = 0.0f;
    float foxingStrength = 0.0f;
    float sepiaShift     = 0.0f;
    float creaseStrength = 0.0f;

    switch (wearLevel) {
        case CardWearLevel::Pristine:
            // Pristine cards: crisp, brand-new deck with clean, distinctly tactile paper stock finish
            paperWarmth    = 0.40f;
            stockStrength  = 0.080f;
            inkSoftening   = 0.25f;
            edgePatina     = 0.0f;
            edgeScuff      = 0.0f;
            deckleFray     = 0.0f;
            foxingStrength = 0.0f;
            sepiaShift     = 0.0f;
            creaseStrength = 0.0f;
            break;
        case CardWearLevel::Natural:
            // Natural: mellowed casino stock with noticeable paper tooth, gentle warmth and ink softening
            paperWarmth    = 0.65f;
            stockStrength  = 0.120f;
            inkSoftening   = 0.45f;
            edgePatina     = 0.25f;
            edgeScuff      = 0.18f;
            deckleFray     = 0.20f;
            foxingStrength = 0.10f;
            sepiaShift     = 0.08f;
            creaseStrength = 0.0f;
            break;
        case CardWearLevel::Played:
            // Played: clearly handled deck, edge patina, scuffed margins exposing paper pulp, early foxing
            paperWarmth    = 0.88f;
            stockStrength  = 0.165f;
            inkSoftening   = 0.70f;
            edgePatina     = 0.65f;
            edgeScuff      = 0.55f;
            deckleFray     = 0.50f;
            foxingStrength = 0.40f;
            sepiaShift     = 0.35f;
            creaseStrength = 0.30f;
            break;
        case CardWearLevel::Vintage:
            // Vintage: authentically aged antique cards (matching reference photo): rich tea/sepia toning,
            // deckled frayed perimeter, transparent notched bite-outs, exposed fibrous pulp core, distinct dark foxing spots, handling creases
            paperWarmth    = 1.0f;
            stockStrength  = 0.220f;
            inkSoftening   = 0.92f;
            edgePatina     = 1.0f;
            edgeScuff      = 1.0f;
            deckleFray     = 1.0f;
            foxingStrength = 0.85f;
            sepiaShift     = 0.75f;
            creaseStrength = 0.75f;
            break;
        default:
            break;
    }

    if (cardStock == CardStock::None) {
        stockStrength = 0.0f;
    }

    for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
        if (onProgress) {
            float frac = static_cast<float>(i) / static_cast<float>(CardTextureCache::kTotalTextures);
            char desc[128];
            if (i == CardTextureCache::kCardBackIndex) {
                std::snprintf(desc, sizeof(desc), "Conditioning Card Back (53/53)...");
            } else {
                Suit s = static_cast<Suit>(i / 13);
                uint8_t r = static_cast<uint8_t>((i % 13) + 1);
                const char* rStr = (r == 1) ? "Ace" : (r == 11) ? "Jack" : (r == 12) ? "Queen" : (r == 13) ? "King" : "";
                const char* sStr = (s == Suit::Spades) ? "Spades" : (s == Suit::Hearts) ? "Hearts" : (s == Suit::Diamonds) ? "Diamonds" : "Clubs";
                if (rStr[0] != '\0') {
                    std::snprintf(desc, sizeof(desc), "Card %zu/53: %s of %s", i + 1, rStr, sStr);
                } else {
                    std::snprintf(desc, sizeof(desc), "Card %zu/53: %u of %s", i + 1, r, sStr);
                }
            }
            if (!onProgress(frac, desc)) {
                return false; // User cancelled!
            }
        }
        const auto& src = srcTextures[i];
        auto& dst = dstTextures[i];
        dst.width = src.width;
        dst.height = src.height;
        dst.rgba = src.rgba;
        if (dst.width <= 0 || dst.height <= 0 || dst.rgba.empty()) continue;

        const int w = dst.width;
        const int h = dst.height;
        const float invW = 1.0f / static_cast<float>(w);
        const float invH = 1.0f / static_cast<float>(h);
        const uint8_t cardId = (i == CardTextureCache::kCardBackIndex) ? 0xBCu : static_cast<uint8_t>(i);

        const float cardAgeVariance = cardHashNoise(0x7F4A23u, cardId, 601) * 0.15f;
        const float honorBias = (i < 52 && (i % 13 == 0 || i % 13 >= 10)) ? 0.08f : 0.0f;

        // 1. Procedural Hairline Scratches (with subtle blur, directional micro-relief, and distortion)
        int numScratches = 0;
        if (wearLevel == CardWearLevel::Natural) numScratches = 1;
        else if (wearLevel == CardWearLevel::Played) numScratches = 2;
        else if (wearLevel == CardWearLevel::Vintage) numScratches = 4;

        std::array<HandlingScratch, 4> scratches;
        for (int s = 0; s < numScratches; ++s) {
            uint32_t scSeed = (static_cast<uint32_t>(cardId) * 0x9E3779B9u) ^ (static_cast<uint32_t>(s) * 0x85EBCA6Bu);
            float rx = cardHashNoise(scSeed, cardId, 101);
            float ry = cardHashNoise(scSeed, cardId, 102);
            scratches[s].x0 = static_cast<float>(w) * (0.15f + 0.70f * rx);
            scratches[s].y0 = static_cast<float>(h) * (0.15f + 0.70f * ry);
            scratches[s].len = static_cast<float>(w) * (0.14f + 0.28f * cardHashNoise(scSeed, cardId, 103));
            float angle = (cardHashNoise(scSeed, cardId, 104) - 0.5f) * 2.6f;
            scratches[s].cosA = std::cos(angle);
            scratches[s].sinA = std::sin(angle);
            scratches[s].curvature = (cardHashNoise(scSeed, cardId, 105) - 0.5f) * 0.0025f;
            scratches[s].sigma = 1.15f + 0.35f * cardHashNoise(scSeed, cardId, 106);
            scratches[s].strength = (wearLevel == CardWearLevel::Vintage ? 0.85f : (wearLevel == CardWearLevel::Played ? 0.60f : 0.35f));
        }

        // 2. Precompute 1D Edge Recession: Notches, Bite-Outs, Deckled Fibers & Indented Regions
        const int perimLen = 2 * (w + h);
        std::vector<float> edgeRecession(static_cast<size_t>(perimLen), 0.0f);

        int numNotches = 0;
        float macroAmp = 0.0f;
        float deckleAmp = 0.0f;
        float bevelRadius = 3.2f;

        switch (wearLevel) {
            case CardWearLevel::Pristine:
                numNotches = 0;
                macroAmp = 0.0f;
                deckleAmp = 0.0f;
                bevelRadius = 2.4f;
                break;
            case CardWearLevel::Natural:
                numNotches = 2;
                macroAmp = 0.35f;
                deckleAmp = 0.55f;
                bevelRadius = 3.0f;
                break;
            case CardWearLevel::Played:
                numNotches = 5;
                macroAmp = 1.10f;
                deckleAmp = 1.50f;
                bevelRadius = 4.0f;
                break;
            case CardWearLevel::Vintage:
                numNotches = 9;
                macroAmp = 2.20f;
                deckleAmp = 3.20f;
                bevelRadius = 5.0f;
                break;
            default:
                break;
        }

        std::array<EdgeNotch, 12> notches;
        for (int k = 0; k < numNotches; ++k) {
            uint32_t nSeed = (static_cast<uint32_t>(cardId) * 0x9E3779B9u) ^ (static_cast<uint32_t>(k) * 0x6C62272Eu);
            float normPos = cardHashNoise(nSeed, cardId, 301);

            // In vintage decks, bias a couple of notches to corner areas (matching reference photo tears)
            if (wearLevel == CardWearLevel::Vintage && (k == 0 || k == 1)) {
                float cornerCenters[4] = {
                    4.0f,
                    static_cast<float>(w),
                    static_cast<float>(w + h),
                    static_cast<float>(2 * w + h)
                };
                int cornerIdx = (cardId + k) % 4;
                normPos = (cornerCenters[cornerIdx] + (cardHashNoise(nSeed, cardId, 305) - 0.5f) * 20.0f) / static_cast<float>(perimLen);
                if (normPos < 0.0f) normPos += 1.0f;
                if (normPos >= 1.0f) normPos -= 1.0f;
            }

            notches[k].sCenter = normPos * static_cast<float>(perimLen);
            float depthBase = (wearLevel == CardWearLevel::Vintage) ? 4.5f : 2.5f;
            if (wearLevel == CardWearLevel::Vintage && k < 2) depthBase = 7.8f;
            notches[k].depth = depthBase * (0.7f + 0.6f * cardHashNoise(nSeed, cardId, 302));
            notches[k].width = 10.0f + 16.0f * cardHashNoise(nSeed, cardId, 303);
            notches[k].raggedScale = 0.25f + 0.40f * cardHashNoise(nSeed, cardId, 304);
        }

        for (int s = 0; s < perimLen; ++s) {
            float uPerim = static_cast<float>(s) / static_cast<float>(perimLen);

            // Macro organic indentation curves (uneven wear from handling)
            float macro = 0.0f;
            if (macroAmp > 0.01f) {
                float wave1 = std::sin(uPerim * 6.2831853f * 2.0f + cardId * 0.73f);
                float wave2 = std::sin(uPerim * 6.2831853f * 5.0f + cardId * 1.41f);
                float wave3 = std::sin(uPerim * 6.2831853f * 11.0f + cardId * 2.19f);
                macro = (wave1 * 0.50f + wave2 * 0.35f + wave3 * 0.20f) * macroAmp;

                float gripBR = std::exp(-std::pow(uPerim - 0.42f, 2.0f) * 350.0f) * 1.6f;
                float gripBL = std::exp(-std::pow(uPerim - 0.64f, 2.0f) * 350.0f) * 1.8f;
                macro += (gripBR + gripBL) * macroAmp * 0.4f;
            }

            // Discrete torn notches and chipped bite-outs
            float notchRec = 0.0f;
            for (int k = 0; k < numNotches; ++k) {
                float ds = std::abs(static_cast<float>(s) - notches[k].sCenter);
                if (ds > static_cast<float>(perimLen) * 0.5f) {
                    ds = static_cast<float>(perimLen) - ds;
                }
                if (ds < notches[k].width) {
                    float t = ds / notches[k].width;
                    float shape = std::pow(std::max(0.0f, 1.0f - t * t), 1.8f);
                    uint32_t jSeed = static_cast<uint32_t>(s * 3) ^ (static_cast<uint32_t>(k) * 0x510E527Fu);
                    float jag = 1.0f + (cardHashNoise(jSeed, cardId, 307) - 0.5f) * notches[k].raggedScale;
                    notchRec += shape * notches[k].depth * std::max(0.2f, jag);
                }
            }

            // Multi-scale deckled jagged paper fibers
            float deckle = 0.0f;
            if (deckleAmp > 0.01f) {
                uint32_t dSeed1 = static_cast<uint32_t>(s * 2.7f);
                uint32_t dSeed2 = static_cast<uint32_t>(s * 7.1f);
                float n1 = cardHashNoise(dSeed1, cardId, 501);
                float n2 = cardHashNoise(dSeed2, cardId, 502);
                deckle = (n1 * 0.65f + n2 * 0.35f) * deckleAmp;
            }

            edgeRecession[s] = std::max(0.0f, macro + notchRec + deckle);
        }

        const float effRu = CardTextureCache::instance().effectiveRu();
        const float cornerR = (effRu <= 0.001f) ? 0.0f : std::max(1.0f, effRu * static_cast<float>(w));

        for (int y = 0; y < h; ++y) {
            const float ny = (static_cast<float>(y) + 0.5f) * invH;

            for (int x = 0; x < w; ++x) {
                const size_t p = static_cast<size_t>(y * w + x) * 4;

                // Evaluate handling scratch displacement & shading
                float scratchDispX = 0.0f;
                float scratchDispY = 0.0f;
                float scratchHighlight = 0.0f;
                float scratchShadow = 0.0f;

                for (int s = 0; s < numScratches; ++s) {
                    const auto& sc = scratches[s];
                    float dx = static_cast<float>(x) - sc.x0;
                    float dy = static_cast<float>(y) - sc.y0;
                    float tProj = dx * sc.cosA + dy * sc.sinA;
                    if (tProj >= -4.0f && tProj <= sc.len + 4.0f) {
                        float perp = -dx * sc.sinA + dy * sc.cosA - sc.curvature * tProj * tProj;
                        float dPerpAbs = std::abs(perp);
                        if (dPerpAbs < 3.2f * sc.sigma) {
                            float g = std::exp(-perp * perp / (2.0f * sc.sigma * sc.sigma));
                            float disp = std::sin(perp * 1.4f) * g * 2.0f * sc.strength;
                            scratchDispX += -sc.sinA * disp;
                            scratchDispY += sc.cosA * disp;
                            if (perp < 0.0f) {
                                scratchHighlight += -perp * g * 0.16f * sc.strength;
                            } else {
                                scratchShadow += perp * g * 0.22f * sc.strength;
                            }
                        }
                    }
                }

                int sampleX = x;
                int sampleY = y;
                if (std::abs(scratchDispX) > 0.05f || std::abs(scratchDispY) > 0.05f) {
                    sampleX = std::clamp(x + static_cast<int>(std::round(scratchDispX)), 0, w - 1);
                    sampleY = std::clamp(y + static_cast<int>(std::round(scratchDispY)), 0, h - 1);
                }
                const size_t sp = static_cast<size_t>(sampleY * w + sampleX) * 4;

                float r = static_cast<float>(src.rgba[sp + 0]);
                float g = static_cast<float>(src.rgba[sp + 1]);
                float b = static_cast<float>(src.rgba[sp + 2]);
                const float a = static_cast<float>(src.rgba[sp + 3]);
                if (a < 10.0f) {
                    dst.rgba[p + 0] = 0;
                    dst.rgba[p + 1] = 0;
                    dst.rgba[p + 2] = 0;
                    dst.rgba[p + 3] = 0;
                    continue;
                }

                const float nx = (static_cast<float>(x) + 0.5f) * invW;
                const float dxEdge = static_cast<float>(std::min(x, (w - 1) - x));
                const float dyEdge = static_cast<float>(std::min(y, (h - 1) - y));
                float minEdgeDist = std::min(dxEdge, dyEdge);

                if (cornerR > 0.5f) {
                    if (x < cornerR && y < cornerR) {
                        float qx = cornerR - static_cast<float>(x);
                        float qy = cornerR - static_cast<float>(y);
                        minEdgeDist = std::min(minEdgeDist, cornerR - std::sqrt(qx * qx + qy * qy));
                    } else if (x > (w - 1) - cornerR && y < cornerR) {
                        float qx = static_cast<float>(x) - ((w - 1) - cornerR);
                        float qy = cornerR - static_cast<float>(y);
                        minEdgeDist = std::min(minEdgeDist, cornerR - std::sqrt(qx * qx + qy * qy));
                    } else if (x < cornerR && y > (h - 1) - cornerR) {
                        float qx = cornerR - static_cast<float>(x);
                        float qy = static_cast<float>(y) - ((h - 1) - cornerR);
                        minEdgeDist = std::min(minEdgeDist, cornerR - std::sqrt(qx * qx + qy * qy));
                    } else if (x > (w - 1) - cornerR && y > (h - 1) - cornerR) {
                        float qx = static_cast<float>(x) - ((w - 1) - cornerR);
                        float qy = static_cast<float>(y) - ((h - 1) - cornerR);
                        minEdgeDist = std::min(minEdgeDist, cornerR - std::sqrt(qx * qx + qy * qy));
                    }
                }

                int perimS = 0;
                float dxIn = 0.0f, dyIn = 0.0f;
                float tx = 0.0f, ty = 0.0f;

                if (y <= (h - 1 - y) && x >= y && (w - 1 - x) >= y) {
                    perimS = std::clamp(x, 0, w - 1);
                    dxIn = 0.0f; dyIn = 1.0f;
                    tx = 1.0f; ty = 0.0f;
                } else if ((w - 1 - x) <= x && y >= (w - 1 - x) && (h - 1 - y) >= (w - 1 - x)) {
                    perimS = w + std::clamp(y, 0, h - 1);
                    dxIn = -1.0f; dyIn = 0.0f;
                    tx = 0.0f; ty = 1.0f;
                } else if ((h - 1 - y) <= y && x >= (h - 1 - y) && (w - 1 - x) >= (h - 1 - y)) {
                    perimS = w + h + std::clamp((w - 1) - x, 0, w - 1);
                    dxIn = 0.0f; dyIn = -1.0f;
                    tx = -1.0f; ty = 0.0f;
                } else {
                    perimS = 2 * w + h + std::clamp((h - 1) - y, 0, h - 1);
                    dxIn = 1.0f; dyIn = 0.0f;
                    tx = 0.0f; ty = -1.0f;
                }
                perimS = std::clamp(perimS, 0, perimLen - 1);

                const float recession = edgeRecession[perimS];
                const float effectiveDist = minEdgeDist - recession;

                // Transparent notched areas & missing edge parts
                if (effectiveDist < -0.8f) {
                    dst.rgba[p + 0] = 0;
                    dst.rgba[p + 1] = 0;
                    dst.rgba[p + 2] = 0;
                    dst.rgba[p + 3] = 0;
                    continue;
                }

                // 1. Physical Paper Stock Microstructure
                float stockMod = 1.0f;
                switch (cardStock) {
                    case CardStock::CasinoLinen: {
                        // Modern casino air-cushion linen finish: dual-frequency cross-hatch dimple embossing
                        const float fx = static_cast<float>(x);
                        const float fy = static_cast<float>(y);
                        const float kGrid = 1.1424f; // 2*pi / 5.5
                        const float wx = std::sin(fx * kGrid);
                        const float wy = std::cos(fy * kGrid);
                        const float relief = (std::cos(fx * kGrid) + std::sin(fy * kGrid)) * 0.45f;
                        const float linen = (wx * wy * 0.75f + relief * 0.55f);
                        stockMod = 1.0f + linen * stockStrength;
                        break;
                    }
                    case CardStock::AntiqueLaid: {
                        // Traditional laid paper from wire mould sieves (as in reference deck)
                        const float fx = static_cast<float>(x);
                        const float fy = static_cast<float>(y);
                        const float kLaid = 1.18f;
                        const float wireWarp = std::sin(fx * 0.035f) * 1.8f;
                        const float laid = std::sin((fy + wireWarp) * kLaid);
                        const float chainMod = std::fmod(fx + 21.0f, 42.0f) - 21.0f;
                        const float chainDist = std::fabs(chainMod);
                        const float chain = std::exp(-chainDist * chainDist * 0.18f) * 0.65f;
                        const float grain = (static_cast<float>((x * 37 ^ y * 131) & 0xFF) / 255.0f - 0.5f) * 0.45f;
                        const float macroPulp = std::sin(nx * 7.0f) * std::cos(ny * 5.0f) * 0.25f;
                        const float laidTexture = laid * 0.70f - chain + grain + macroPulp;
                        stockMod = 1.0f + laidTexture * stockStrength;
                        break;
                    }
                    case CardStock::CottonRag: {
                        // Heavy fibrous cotton rag pasteboard with multi-directional organic pulp tooth
                        const float macroCloud = (std::sin(nx * 9.0f) * std::cos(ny * 7.0f) + std::sin((nx + ny) * 11.0f) * 0.5f) * 0.40f;
                        const uint32_t fHash1 = (static_cast<uint32_t>(x / 3) * 0x85EBCA6Bu) ^ (static_cast<uint32_t>(y / 3) * 0xC2B2AE35u);
                        const uint32_t fHash2 = (static_cast<uint32_t>(x / 2) * 0x7FED1631u) ^ (static_cast<uint32_t>(y / 2) * 0x510E527Fu);
                        const float fiber1 = (static_cast<float>(fHash1 & 0xFFFFu) / 32767.5f) - 1.0f;
                        const float fiber2 = (static_cast<float>(fHash2 & 0xFFFFu) / 32767.5f) - 1.0f;
                        float fleck = 0.0f;
                        if ((fHash2 & 0x1FFFu) < 22) {
                            fleck = -0.55f;
                        }
                        const float rag = macroCloud + fiber1 * 0.45f + fiber2 * 0.35f + fleck;
                        stockMod = 1.0f + rag * stockStrength;
                        break;
                    }
                    case CardStock::SmoothParchment: {
                        const float vellumMottle = (std::sin(nx * 6.0f) * std::cos(ny * 5.0f) + std::cos((nx - ny) * 8.0f) * 0.5f) * 0.50f;
                        const uint32_t pHash = (static_cast<uint32_t>(x) * 0x9E3779B9u) ^ (static_cast<uint32_t>(y) * 0x85EBCA6Bu);
                        const float fineGrain = (static_cast<float>(pHash & 0xFFFu) / 2047.5f) - 1.0f;
                        stockMod = 1.0f + (vellumMottle * 0.65f + fineGrain * 0.35f) * stockStrength;
                        break;
                    }
                    default:
                        break;
                }

                // 2. Paper Stock Tone & Substrate Radiance
                const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
                const float whiteMask = std::clamp((lum - 160.0f) / 75.0f, 0.0f, 1.0f);
                if (whiteMask > 0.0f) {
                    float paperTargetR = 250.0f - sepiaShift * 26.0f;
                    float paperTargetG = 244.0f - sepiaShift * 38.0f;
                    float paperTargetB = 232.0f - sepiaShift * 62.0f;

                    if (cardStock == CardStock::AntiqueLaid) {
                        paperTargetR -= 3.0f;
                        paperTargetB -= 6.0f;
                    } else if (cardStock == CardStock::CottonRag) {
                        paperTargetR -= 4.0f;
                        paperTargetG -= 3.0f;
                        paperTargetB -= 9.0f;
                    } else if (cardStock == CardStock::SmoothParchment) {
                        paperTargetR += 2.0f;
                        paperTargetB -= 3.0f;
                    }

                    const float blend = whiteMask * paperWarmth;
                    r = r * (1.0f - blend) + paperTargetR * blend;
                    g = g * (1.0f - blend) + paperTargetG * blend;
                    b = b * (1.0f - blend) + paperTargetB * blend;

                    r = std::clamp(r * stockMod, 0.0f, 255.0f);
                    g = std::clamp(g * stockMod, 0.0f, 255.0f);
                    b = std::clamp(b * stockMod, 0.0f, 255.0f);
                } else {
                    const float inkMod = 1.0f + (stockMod - 1.0f) * 0.45f;
                    r = std::clamp(r * inkMod, 0.0f, 255.0f);
                    g = std::clamp(g * inkMod, 0.0f, 255.0f);
                    b = std::clamp(b * inkMod, 0.0f, 255.0f);
                }

                // 3. Printing Press Ink Softening & Black Lift
                if (r > 140.0f && r > (g + b) * 0.9f) {
                    const float redTargetR = 196.0f - sepiaShift * 26.0f;
                    const float redTargetG = 34.0f  + sepiaShift * 14.0f;
                    const float redTargetB = 34.0f  - sepiaShift * 10.0f;
                    const float inkBlend = inkSoftening * std::clamp((r - (g + b) * 0.9f) / 100.0f, 0.0f, 1.0f);
                    r = r * (1.0f - inkBlend) + redTargetR * inkBlend;
                    g = g * (1.0f - inkBlend) + redTargetG * inkBlend;
                    b = b * (1.0f - inkBlend) + redTargetB * inkBlend;
                } else if (lum < 55.0f) {
                    const float blackLiftR = 24.0f + sepiaShift * 24.0f;
                    const float blackLiftG = 24.0f + sepiaShift * 16.0f;
                    const float blackLiftB = 28.0f + sepiaShift * 8.0f;
                    const float blackBlend = inkSoftening * (1.0f - lum / 55.0f);
                    r = r * (1.0f - blackBlend) + blackLiftR * blackBlend;
                    g = g * (1.0f - blackBlend) + blackLiftG * blackBlend;
                    b = b * (1.0f - blackBlend) + blackLiftB * blackBlend;
                }

                // 4. Antique Foxing & Rust Oxidation Specks (Played & Vintage) - reduced realistic frequency
                if (foxingStrength > 0.01f) {
                    constexpr int kFoxCell = 68;
                    const int gx = x / kFoxCell;
                    const int gy = y / kFoxCell;
                    const uint32_t cellSeed = (static_cast<uint32_t>(gx) * 0x1F1F1F1Fu) ^
                                              (static_cast<uint32_t>(gy) * 0x5D5D5D5Du) ^
                                              (static_cast<uint32_t>(cardId) * 0x9E3779B9u);
                    uint32_t ch = cellSeed;
                    ch = (ch ^ 61u) ^ (ch >> 16);
                    ch += (ch << 3);
                    ch ^= (ch >> 4);
                    ch *= 0x27D4EB2Du;
                    ch ^= (ch >> 15);

                    if ((ch & 0xFFu) < static_cast<uint32_t>(foxingStrength * 16.0f)) {
                        const float spotX = static_cast<float>(gx * kFoxCell + 6) + static_cast<float>((ch >> 8) & 0x1F);
                        const float spotY = static_cast<float>(gy * kFoxCell + 6) + static_cast<float>((ch >> 16) & 0x1F);
                        const float dxSpot = static_cast<float>(x) - spotX;
                        const float dySpot = static_cast<float>(y) - spotY;
                        const float dSpotSq = dxSpot * dxSpot + dySpot * dySpot;
                        const float radius = 1.6f + static_cast<float>((ch >> 24) & 0x03) * 0.9f;
                        const float radiusSq = radius * radius;
                        if (dSpotSq < radiusSq * 4.5f) {
                            const float dNorm = std::sqrt(dSpotSq) / radius;
                            const float core = std::exp(-dNorm * dNorm * 2.2f) * (0.45f + 0.40f * foxingStrength);
                            const float halo = std::exp(-dNorm * 1.5f) * 0.25f * foxingStrength;
                            const float totalIntensity = std::clamp(core + halo, 0.0f, 0.85f);
                            r *= (1.0f - totalIntensity * 0.45f);
                            g *= (1.0f - totalIntensity * 0.65f);
                            b *= (1.0f - totalIntensity * 0.88f);
                        }
                    }
                }

                // 5. Perimeter Handling Patina & Finger Oils (Played & Vintage)
                if (edgePatina > 0.01f) {
                    const float cdx = (nx - 0.5f) * 2.0f;
                    const float cdy = (ny - 0.5f) * 2.0f;
                    const float rDist = std::sqrt(cdx * cdx + cdy * cdy) * 0.70710678f;

                    // Asymmetric thumb grip zones
                    const float thumbBR = std::exp(-((nx - 0.88f) * (nx - 0.88f) * 14.0f + (ny - 0.92f) * (ny - 0.92f) * 10.0f));
                    const float thumbBL = std::exp(-((nx - 0.12f) * (nx - 0.12f) * 14.0f + (ny - 0.92f) * (ny - 0.92f) * 10.0f));
                    const float thumbSide = std::exp(-((nx - 0.96f) * (nx - 0.96f) * 20.0f + (ny - 0.50f) * (ny - 0.50f) * 8.0f)) * 0.6f;
                    const float thumbPatina = (thumbBR + thumbBL * 0.75f + thumbSide) * 0.18f;

                    const float edgeVignette = std::pow(std::clamp(rDist, 0.0f, 1.0f), 2.2f) * (0.16f + cardAgeVariance * 0.10f);
                    float vignette = (edgeVignette + thumbPatina) * edgePatina;

                    r *= (1.0f - vignette * 0.55f);
                    g *= (1.0f - vignette * 0.75f);
                    b *= (1.0f - vignette * 1.15f);
                }

                // 6. Exposed Paper Pulp Core & Subtle 3D Edge Shading (wrapping around notches!)
                const float pulpWidth = 1.5f + edgeScuff * 6.5f;
                if (edgeScuff > 0.01f && effectiveDist > 0.0f && effectiveDist <= pulpWidth) {
                    const float pulpR = 240.0f - sepiaShift * 16.0f;
                    const float pulpG = 232.0f - sepiaShift * 26.0f;
                    const float pulpB = 210.0f - sepiaShift * 44.0f;

                    float pulpIntensity = std::clamp((pulpWidth - effectiveDist) / pulpWidth, 0.0f, 1.0f) * edgeScuff * 0.92f;
                    uint32_t tSeed = static_cast<uint32_t>(perimS * 3) ^ (static_cast<uint32_t>(cardId) * 0x31u);
                    float pulpTooth = 1.0f + (cardHashNoise(tSeed, cardId, 601) - 0.5f) * 0.12f;
                    r = (r * (1.0f - pulpIntensity) + pulpR * pulpIntensity) * pulpTooth;
                    g = (g * (1.0f - pulpIntensity) + pulpG * pulpIntensity) * pulpTooth;
                    b = (b * (1.0f - pulpIntensity) + pulpB * pulpIntensity) * pulpTooth;
                }

                if (effectiveDist <= bevelRadius) {
                    int sPrev = (perimS == 0) ? (perimLen - 1) : (perimS - 1);
                    int sNext = (perimS + 1 >= perimLen) ? 0 : (perimS + 1);
                    float dRec_ds = (edgeRecession[sNext] - edgeRecession[sPrev]) * 0.5f;
                    float grad_s = -dRec_ds;

                    float gx = dxIn + tx * grad_s;
                    float gy = dyIn + ty * grad_s;
                    float gLen = std::sqrt(gx * gx + gy * gy);
                    if (gLen > 0.001f) {
                        float nx2d = gx / gLen;
                        float ny2d = gy / gLen;

                        float uB = std::clamp(effectiveDist / bevelRadius, 0.0f, 1.0f);
                        float nz = std::sin(uB * 1.5707963f);
                        float nRim = std::cos(uB * 1.5707963f);
                        float bnx = -nx2d * nRim;
                        float bny = -ny2d * nRim;

                        constexpr float kLx = -0.473f;
                        constexpr float kLy = -0.473f;
                        constexpr float kLz = 0.743f;

                        float nDotL = bnx * kLx + bny * kLy + nz * kLz;
                        float relShade = (nDotL - kLz) * 0.65f;

                        float hDotN = bnx * -0.32f + bny * -0.32f + nz * 0.89f;
                        float spec = (hDotN > 0.82f) ? std::pow(hDotN, 14.0f) * 0.35f : 0.0f;

                        float ao = std::clamp(effectiveDist / 1.5f, 0.68f, 1.0f);
                        float lightMod = std::clamp(1.0f + relShade, 0.48f, 1.45f) * ao;

                        r = std::clamp(r * lightMod + spec * 255.0f, 0.0f, 255.0f);
                        g = std::clamp(g * lightMod + spec * 250.0f, 0.0f, 255.0f);
                        b = std::clamp(b * lightMod + spec * 235.0f, 0.0f, 255.0f);
                    }
                }

                // 7. Organic Handling Crease (Vintage & Played)
                if (creaseStrength > 0.05f) {
                    const float creaseAngle = (static_cast<float>(cardId % 7) - 3.0f) * 0.15f;
                    const float creaseY = static_cast<float>(h) * (0.35f + 0.05f * static_cast<float>(cardId % 5));
                    const float distToCrease = std::fabs(static_cast<float>(y) - creaseY - (static_cast<float>(x) - static_cast<float>(w) * 0.5f) * creaseAngle);
                    if (distToCrease < 3.5f) {
                        const float foldSign = (static_cast<float>(y) > creaseY) ? 1.0f : -1.0f;
                        const float foldEffect = std::exp(-distToCrease * distToCrease / 3.0f) * foldSign * 0.08f * creaseStrength;
                        r = std::clamp(r * (1.0f + foldEffect), 0.0f, 255.0f);
                        g = std::clamp(g * (1.0f + foldEffect), 0.0f, 255.0f);
                        b = std::clamp(b * (1.0f + foldEffect), 0.0f, 255.0f);
                    }
                }

                // 8. Hairline Scratch Micro-Relief (sunward highlight & groove shadow)
                if (scratchShadow > 0.001f || scratchHighlight > 0.001f) {
                    r = std::clamp(r * (1.0f - scratchShadow) + scratchHighlight * 255.0f, 0.0f, 255.0f);
                    g = std::clamp(g * (1.0f - scratchShadow) + scratchHighlight * 242.0f, 0.0f, 255.0f);
                    b = std::clamp(b * (1.0f - scratchShadow) + scratchHighlight * 215.0f, 0.0f, 255.0f);
                }

                float finalA = a;
                if (effectiveDist <= 0.4f) {
                    finalA *= std::clamp((effectiveDist + 0.8f) / 1.2f, 0.0f, 1.0f);
                }

                dst.rgba[p + 0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
                dst.rgba[p + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
                dst.rgba[p + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
                dst.rgba[p + 3] = static_cast<uint8_t>(std::clamp(finalA, 0.0f, 255.0f));
            }
        }
    }
    return true;
}

static void bakeThemeCardBack(CardTexture& back, DeckThemeId theme, int cardW, int cardH) {
    back.width = cardW;
    back.height = cardH;
    back.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

    const float pad = std::max(6.0f, static_cast<float>(cardW) * 0.065f);
    const float padInner = pad + std::max(4.0f, static_cast<float>(cardW) * 0.045f);

    for (int y = 0; y < cardH; ++y) {
        float ny = static_cast<float>(y) / static_cast<float>(cardH);
        for (int x = 0; x < cardW; ++x) {
            size_t p = static_cast<size_t>(y * cardW + x) * 4;
            uint8_t r = 240, g = 232, b = 214, a = 255;

            if (theme == DeckThemeId::SolEnshade) {
                // Crisp minimal card back in solid charcoal with clean geometric inner border
                r = 24; g = 24; b = 27;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    if (x == static_cast<int>(pad) || x == static_cast<int>(cardW - pad - 1) ||
                        y == static_cast<int>(pad) || y == static_cast<int>(cardH - pad - 1)) {
                        r = 63; g = 63; b = 70;
                    }
                }
            } else if (theme == DeckThemeId::SteampunkAi) {
                // Warm antique ivory border with Victorian brass & copper filigree
                r = 248; g = 242; b = 230;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(58.0f + 16.0f * t);
                    g = static_cast<uint8_t>(28.0f + 10.0f * t);
                    b = static_cast<uint8_t>(24.0f + 10.0f * t);

                    // Polished brass outer border
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 200; g = 150; b = 62;
                    }

                    // Inner copper line
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 184; g = 115; b = 51;
                        }
                    }
                }
            } else if (theme == DeckThemeId::Anime) {
                // Crisp anime cardstock with midnight navy / luminous sapphire & gold celestial frame
                r = 248; g = 250; b = 255;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(18.0f + 12.0f * t);
                    g = static_cast<uint8_t>(24.0f + 16.0f * t);
                    b = static_cast<uint8_t>(44.0f + 26.0f * t);

                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 255; g = 183; b = 3;
                    }

                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 58; g = 134; b = 255;
                        }

                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.18f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.18f);
                        if (dx + dy <= 1.0f || (dx <= 0.2f && dy <= 0.7f) || (dy <= 0.2f && dx <= 0.7f)) {
                            r = 255; g = 183; b = 3;
                        }
                    }
                }
            } else if (theme == DeckThemeId::Cyberpunk) {
                // Dark obsidian cardstock with neon cyan brackets and hot magenta circuit trace
                r = 12; g = 16; b = 23;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(10.0f + 6.0f * t);
                    g = static_cast<uint8_t>(14.0f + 8.0f * t);
                    b = static_cast<uint8_t>(25.0f + 16.0f * t);

                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 0; g = 240; b = 255;
                    }

                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 255; g = 0; b = 127;
                        }

                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f && dx + dy >= 0.82f) {
                            r = 0; g = 240; b = 255;
                        } else if (dx + dy <= 0.45f) {
                            r = 255; g = 230; b = 0;
                        }
                    }
                }
            } else if (theme == DeckThemeId::BaroqueNoir || theme == DeckThemeId::Custom) {
                // Dark Baroque Noir: deep charcoal/obsidian with sculpted antique silver border and central medallion
                r = 24; g = 26; b = 30; // Dark slate border
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(18.0f + 10.0f * t);
                    g = static_cast<uint8_t>(20.0f + 10.0f * t);
                    b = static_cast<uint8_t>(24.0f + 12.0f * t);

                    // Chiseled antique silver outer border
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 192; g = 196; b = 204;
                    }

                    // Inner silver relief filigree line
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.2f || dInnerY <= 1.2f) {
                            r = 140; g = 145; b = 155;
                        }

                        // Center Baroque Diamond Medallion
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f && dx + dy >= 0.80f) {
                            r = 192; g = 196; b = 204; // Bright antique silver rim
                        } else if (dx + dy < 0.80f && dx + dy >= 0.35f) {
                            r = 45; g = 48; b = 56;  // Deep relief dark field
                        } else if (dx + dy < 0.35f) {
                            r = 210; g = 215; b = 224; // Central silver flourish core
                        }
                    }
                }
            } else if (theme == DeckThemeId::BaroqueLight) {
                // Luminous Baroque Light: warm ivory/alabaster cardstock with sculpted dark pewter border & blooming ruby medallion
                r = 246; g = 244; b = 238;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(242.0f + 6.0f * t);
                    g = static_cast<uint8_t>(239.0f + 6.0f * t);
                    b = static_cast<uint8_t>(232.0f + 6.0f * t);

                    // Chiseled dark pewter outer relief border
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 2.0f || dEdgeY <= 2.0f) {
                        r = 45; g = 48; b = 55;
                    }

                    // Inner dark acanthus relief frame line
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.2f || dInnerY <= 1.2f) {
                            r = 75; g = 80; b = 90;
                        }

                        // Center Blooming Ruby Diamond Medallion
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.18f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.18f);
                        if (dx + dy <= 1.0f) {
                            // Outer sculpted dark silver relief casing
                            r = 45; g = 48; b = 55;
                            // Inner glowing ruby core
                            if (dx + dy <= 0.55f) {
                                float glowT = 1.0f - ((dx + dy) / 0.55f);
                                r = static_cast<uint8_t>(220.0f + 35.0f * glowT);
                                g = static_cast<uint8_t>(20.0f + 80.0f * glowT);
                                b = static_cast<uint8_t>(35.0f + 85.0f * glowT);
                            }
                        }
                    }
                }
            } else if (theme == DeckThemeId::BaroqueDark || theme == DeckThemeId::BaroqueCellShaded) {
                // Baroque Dark: archaic dark slate/charcoal obsidian with sculpted antique silver border and radiant ruby bloom medallion
                r = 20; g = 22; b = 26; // Dark slate border
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(16.0f + 8.0f * t);
                    g = static_cast<uint8_t>(18.0f + 8.0f * t);
                    b = static_cast<uint8_t>(22.0f + 10.0f * t);

                    // Chiseled antique silver outer border
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.8f || dEdgeY <= 1.8f) {
                        r = 192; g = 196; b = 204;
                    }

                    // Inner silver relief filigree line
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.2f || dInnerY <= 1.2f) {
                            r = 140; g = 145; b = 155;
                        }

                        // Center Radiant Blooming Ruby Diamond Medallion
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.18f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.18f);
                        if (dx + dy <= 1.0f) {
                            // Sculpted antique silver rim
                            r = 192; g = 196; b = 204;
                            // Intensely prominent glowing ruby bloom core
                            if (dx + dy <= 0.65f) {
                                float glowT = 1.0f - ((dx + dy) / 0.65f);
                                r = static_cast<uint8_t>(220.0f + 35.0f * glowT);
                                g = static_cast<uint8_t>(15.0f + 60.0f * glowT);
                                b = static_cast<uint8_t>(35.0f + 60.0f * glowT);
                            }
                        }
                    }
                }
            } else {
                // Classic baize felt panel
                r = 240; g = 232; b = 214;
                if (x >= pad && x < cardW - pad && y >= pad && y < cardH - pad) {
                    float t = ny;
                    r = static_cast<uint8_t>(22.0f + (36.0f - 22.0f) * t);
                    g = static_cast<uint8_t>(44.0f + (68.0f - 44.0f) * t);
                    b = static_cast<uint8_t>(62.0f + (88.0f - 62.0f) * t);

                    // Gold filigree line
                    float dEdgeX = std::min(x - pad, (cardW - pad) - x);
                    float dEdgeY = std::min(y - pad, (cardH - pad) - y);
                    if (dEdgeX <= 1.5f || dEdgeY <= 1.5f) {
                        r = 218; g = 178; b = 88;
                    }

                    // Inner second gold border
                    if (x >= padInner && x < cardW - padInner && y >= padInner && y < cardH - padInner) {
                        float dInnerX = std::min(x - padInner, (cardW - padInner) - x);
                        float dInnerY = std::min(y - padInner, (cardH - padInner) - y);
                        if (dInnerX <= 1.0f || dInnerY <= 1.0f) {
                            r = 180; g = 145; b = 70;
                        }

                        // Center Diamond Sigil
                        float cx = static_cast<float>(cardW) * 0.5f;
                        float cy = static_cast<float>(cardH) * 0.5f;
                        float dx = std::abs(static_cast<float>(x) - cx) / (static_cast<float>(cardW) * 0.16f);
                        float dy = std::abs(static_cast<float>(y) - cy) / (static_cast<float>(cardH) * 0.16f);
                        if (dx + dy <= 1.0f) {
                            r = 224; g = 185; b = 92;
                        }
                    }
                }
            }

            back.rgba[p + 0] = r;
            back.rgba[p + 1] = g;
            back.rgba[p + 2] = b;
            back.rgba[p + 3] = a;
        }
    }

    // For Steampunk deck theme, stamp SolEngine's signature Clockwork Logo medallion centered on the card back
    if (theme == DeckThemeId::SteampunkAi) {
        const float logoR = std::min(static_cast<float>(cardW) * 0.34f, static_cast<float>(cardH) * 0.24f);
        clockwork_ui::bakeClockworkLogoRgba(
            back.rgba.data(), cardW, cardH, cardW * 4,
            static_cast<float>(cardW) * 0.5f,
            static_cast<float>(cardH) * 0.5f,
            logoR, 1.0f
        );
    }
}

const char* CardTextureCache::deckThemeName(DeckThemeId theme) noexcept {
    switch (theme) {
        case DeckThemeId::SolEnshade:  return "SOLENSHADE";
        case DeckThemeId::ClassicSvg:  return "CLASSIC";
        case DeckThemeId::SteampunkAi: return "STEAMPUNK";
        case DeckThemeId::Anime:       return "ANIME";
        case DeckThemeId::Cyberpunk:   return "CYBERPUNK";
        case DeckThemeId::BaroqueNoir:       return "BAROQUE NOIR";
        case DeckThemeId::BaroqueLight:      return "BAROQUE LIGHT";
        case DeckThemeId::BaroqueDark:       return "BAROQUE DARK";
        case DeckThemeId::BaroqueCellShaded: return "BAROQUE CELL SHADED";
        case DeckThemeId::StainedGlass:      return "STAINED GLASS";
        case DeckThemeId::PolychromeCrt:     return "POLYCHROME CRT";
        case DeckThemeId::WatercolorSketch:  return "DA VINCI WATERCOLOR";
        case DeckThemeId::Custom:            return "CUSTOM";
        case DeckThemeId::Count:       return "DEFAULT";
    }
    return "UNKNOWN";
}

const char* CardTextureCache::cardWearLevelName(CardWearLevel level) noexcept {
    switch (level) {
        case CardWearLevel::Pristine: return "PRISTINE";
        case CardWearLevel::Natural:  return "NATURAL";
        case CardWearLevel::Played:   return "PLAYED";
        case CardWearLevel::Vintage:  return "VINTAGE";
        case CardWearLevel::Count:    return "DEFAULT";
    }
    return "UNKNOWN";
}

const char* CardTextureCache::cardStockName(CardStock stock) noexcept {
    switch (stock) {
        case CardStock::None:            return "NONE";
        case CardStock::CasinoLinen:     return "LINEN";
        case CardStock::AntiqueLaid:     return "LAID RAG";
        case CardStock::CottonRag:       return "COTTON";
        case CardStock::SmoothParchment: return "PARCHMENT";
        case CardStock::Count:           return "DEFAULT";
    }
    return "UNKNOWN";
}

const char* CardTextureCache::cornerStyleName(CardCornerStyle style) noexcept {
    switch (style) {
        case CardCornerStyle::Auto:    return "CORNERS: AUTO [C]";
        case CardCornerStyle::Sharp:   return "CORNERS: SHARP [C]";
        case CardCornerStyle::Vintage: return "CORNERS: VINTAGE [C]";
        case CardCornerStyle::Round:   return "CORNERS: ROUND [C]";
        case CardCornerStyle::Count:   return "CORNERS [C]";
    }
    return "CORNERS [C]";
}

void CardTextureCache::applyConditioningAndSync(CardWearLevel wear, CardStock stock) {
    applyCardConditioning(baseTextures_, textures_, wear, stock);
    if (currentTheme_ == DeckThemeId::SteampunkAi) {
        bakeThemeCardBack(textures_[kCardBackIndex], currentTheme_, cardW_, cardH_);
    }
    syncArgb32AndMips(textures_);
    if (hasGpuAtlas_) {
        atlasDirty_ = true;
    }
}

void CardTextureCache::applyConditioningAndSync(CardWearLevel wear) {
    applyConditioningAndSync(wear, cardStock_);
}

void CardTextureCache::setCardStock(CardStock stock) {
    if (cardStock_ == stock && isLoaded_) return;
    cardStock_ = stock;
    if (isLoaded_) {
        applyConditioningAndSync(wearLevel_, cardStock_);
    }
}

void CardTextureCache::setCardWearLevel(CardWearLevel level) {
    if (wearLevel_ == level && isLoaded_) return;
    wearLevel_ = level;
    if (isLoaded_) {
        applyConditioningAndSync(wearLevel_, cardStock_);
    }
}

bool CardTextureCache::initialize(int cardW, int cardH) {
    if (isLoaded_ && cardW_ == cardW && cardH_ == cardH) {
        return true;
    }
    return loadDeckTheme(currentTheme_, cardW, cardH);
}

static void saveBinaryCache(const std::string& binPath, int cardW, int cardH,
                            const std::array<CardTexture, CardTextureCache::kTotalTextures>& textures);

static bool tryLoadBinaryCache(const std::string& binPath, const std::string& srcPath,
                               int cardW, int cardH,
                               std::array<CardTexture, CardTextureCache::kTotalTextures>& textures) {
    bool cacheFresh = false;
    std::error_code ec;
    if (std::filesystem::exists(binPath, ec) && std::filesystem::exists(srcPath, ec)) {
        auto binTime = std::filesystem::last_write_time(binPath, ec);
        auto srcTime = std::filesystem::last_write_time(srcPath, ec);
        if (!ec && binTime >= srcTime) {
            cacheFresh = true;
        } else {
            std::cout << "[SolEngine] Source deck is newer than binary cache (" << binPath << "), reloading...\n";
        }
    } else if (std::filesystem::exists(binPath, ec)) {
        cacheFresh = true;
    }

    if (!cacheFresh) return false;

    FILE* fp = fopen(binPath.c_str(), "rb");
    if (!fp) return false;

    uint32_t header[4] = {0};
    if (fread(header, sizeof(uint32_t), 4, fp) == 4) {
        const uint32_t kMagicRaw  = 0x4344434B; // 'CDCK' (legacy uncompressed ~38.4 MB)
        const uint32_t kMagicJpg  = 0x43444A50; // 'CDJP' (4:4:4 JPEG per-card compressed ~3-5 MB)
        const uint32_t kMagicZlib = 0x43445A4C; // 'CDZL' (lossless Deflate per-card compressed)

        if (header[1] == static_cast<uint32_t>(cardW) &&
            header[2] == static_cast<uint32_t>(cardH) &&
            header[3] == static_cast<uint32_t>(CardTextureCache::kTotalTextures)) {

            if (header[0] == kMagicJpg) {
                bool readOk = true;
                const size_t byteCount = static_cast<size_t>(cardW * cardH * 4);
                std::vector<uint8_t> compBuf;
                for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
                    uint32_t chunkLen = 0;
                    if (fread(&chunkLen, sizeof(uint32_t), 1, fp) != 1 || chunkLen == 0 || chunkLen > 16 * 1024 * 1024) {
                        readOk = false;
                        break;
                    }
                    compBuf.resize(chunkLen);
                    if (fread(compBuf.data(), 1, chunkLen, fp) != chunkLen) {
                        readOk = false;
                        break;
                    }
                    int decW = 0, decH = 0, comp = 0;
                    stbi_uc* decoded = stbi_load_from_memory(compBuf.data(), static_cast<int>(chunkLen), &decW, &decH, &comp, 4);
                    if (!decoded || decW != cardW || decH != cardH) {
                        if (decoded) stbi_image_free(decoded);
                        readOk = false;
                        break;
                    }
                    textures[i].width = cardW;
                    textures[i].height = cardH;
                    textures[i].rgba.assign(decoded, decoded + byteCount);
                    stbi_image_free(decoded);
                }
                fclose(fp);
                if (readOk) {
                    std::cout << "[SolEngine] Loaded compressed JPG deck cache from " << binPath << " (" << cardW << "x" << cardH << ")\n";
                    return true;
                }
                return false;
            } else if (header[0] == kMagicZlib) {
                bool readOk = true;
                const size_t byteCount = static_cast<size_t>(cardW * cardH * 4);
                std::vector<uint8_t> compBuf;
                for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
                    uint32_t chunkLen = 0;
                    if (fread(&chunkLen, sizeof(uint32_t), 1, fp) != 1 || chunkLen == 0 || chunkLen > 32 * 1024 * 1024) {
                        readOk = false;
                        break;
                    }
                    compBuf.resize(chunkLen);
                    if (fread(compBuf.data(), 1, chunkLen, fp) != chunkLen) {
                        readOk = false;
                        break;
                    }
                    int outLen = 0;
                    char* decomp = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(compBuf.data()), static_cast<int>(chunkLen), &outLen);
                    if (!decomp || static_cast<size_t>(outLen) != byteCount) {
                        if (decomp) free(decomp);
                        readOk = false;
                        break;
                    }
                    textures[i].width = cardW;
                    textures[i].height = cardH;
                    textures[i].rgba.assign(reinterpret_cast<const uint8_t*>(decomp), reinterpret_cast<const uint8_t*>(decomp) + byteCount);
                    free(decomp);
                }
                fclose(fp);
                if (readOk) {
                    std::cout << "[SolEngine] Loaded compressed Zlib deck cache from " << binPath << " (" << cardW << "x" << cardH << ")\n";
                    return true;
                }
                return false;
            } else if (header[0] == kMagicRaw) {
                bool readOk = true;
                const size_t byteCount = static_cast<size_t>(cardW * cardH * 4);
                for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
                    textures[i].width = cardW;
                    textures[i].height = cardH;
                    textures[i].rgba.resize(byteCount);
                    if (fread(textures[i].rgba.data(), 1, byteCount, fp) != byteCount) {
                        readOk = false;
                        break;
                    }
                }
                fclose(fp);
                if (readOk) {
                    std::cout << "[SolEngine] Loaded legacy raw deck binary cache from " << binPath << " (" << cardW << "x" << cardH << "); upgrading to compact JPG cache...\n";
                    saveBinaryCache(binPath, cardW, cardH, textures);
                    return true;
                }
                return false;
            }
        }
    }
    fclose(fp);
    return false;
}

static void saveBinaryCache(const std::string& binPath, int cardW, int cardH,
                            const std::array<CardTexture, CardTextureCache::kTotalTextures>& textures) {
#if defined(__EMSCRIPTEN__)
    (void)binPath;
    (void)cardW;
    (void)cardH;
    (void)textures;
    return;
#else
    FILE* outFp = fopen(binPath.c_str(), "wb");
    if (!outFp) return;

    // 'CDJP': Compact 4:4:4 JPEG-95 per-card binary container (~3-5 MB instead of 38.5 MB)
    const uint32_t kMagicJpg = 0x43444A50; // 'CDJP'
    uint32_t header[4] = {
        kMagicJpg,
        static_cast<uint32_t>(cardW),
        static_cast<uint32_t>(cardH),
        static_cast<uint32_t>(CardTextureCache::kTotalTextures)
    };
    fwrite(header, sizeof(uint32_t), 4, outFp);

    std::vector<uint8_t> jpgBuf;
    jpgBuf.reserve(256 * 1024);

    auto writeCallback = [](void* context, void* data, int size) {
        if (size <= 0 || !data || !context) return;
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        const auto* bytes = static_cast<const uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    };

    for (size_t i = 0; i < CardTextureCache::kTotalTextures; ++i) {
        jpgBuf.clear();
        stbi_write_jpg_to_func(writeCallback, &jpgBuf, cardW, cardH, 4, textures[i].rgba.data(), 95);
        uint32_t chunkLen = static_cast<uint32_t>(jpgBuf.size());
        fwrite(&chunkLen, sizeof(uint32_t), 1, outFp);
        if (chunkLen > 0) {
            fwrite(jpgBuf.data(), 1, chunkLen, outFp);
        }
    }
    fclose(outFp);
    std::cout << "[SolEngine] Saved compact JPG-compressed deck cache to " << binPath << "\n";
#endif
}

static bool rasterizeSvgToTextures(NSVGimage* image, int cardW, int cardH,
                                   std::array<CardTexture, CardTextureCache::kTotalTextures>& textures,
                                   DeckThemeId currentTheme,
                                   const std::string& binPath) {
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return false;
    }

    constexpr float kCellW = 750.0f;
    const float scale = static_cast<float>(cardW) / kCellW;

    const int cols = 9;
    const int rows = 6;
    const int totalW = cols * cardW;
    const int totalH = rows * cardH;

    std::cout << "[SolEngine] Rasterizing full SVG deck sheet (" << totalW << "x" << totalH << ") ...\n";

    std::vector<uint8_t> sheetRgba(static_cast<size_t>(totalW * totalH * 4), 0);
    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, sheetRgba.data(), totalW, totalH, totalW * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    // Extract and composite each card face onto background color
    const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
    for (Suit s : suits) {
        for (uint8_t r = 1; r <= 13; ++r) {
            Rank rank = static_cast<Rank>(r);
            size_t cell = svgCellIndexForCard(s, rank);
            size_t destIdx = CardTextureCache::cardIndex(s, rank);

            int col = static_cast<int>(cell % 9);
            int row = static_cast<int>(cell / 9);
            int cellX = col * cardW;
            int cellY = row * cardH;

            CardTexture& tex = textures[destIdx];
            tex.width = cardW;
            tex.height = cardH;
            tex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

            int sampleX = std::clamp(cellX + cardW / 6, 0, totalW - 1);
            int sampleY = std::clamp(cellY + cardH / 6, 0, totalH - 1);
            const uint8_t* marginPixel = &sheetRgba[static_cast<size_t>(sampleY * totalW + sampleX) * 4];
            const uint8_t bgR = (marginPixel[3] > 0) ? marginPixel[0] : 255;
            const uint8_t bgG = (marginPixel[3] > 0) ? marginPixel[1] : 255;
            const uint8_t bgB = (marginPixel[3] > 0) ? marginPixel[2] : 255;

            for (int y = 0; y < cardH; ++y) {
                const uint8_t* src = &sheetRgba[static_cast<size_t>((cellY + y) * totalW + cellX) * 4];
                uint8_t* dst = &tex.rgba[static_cast<size_t>(y * cardW) * 4];
                for (int x = 0; x < cardW; ++x) {
                    uint8_t sa = src[3];
                    if (sa == 255) {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 255;
                    } else if (sa == 0) {
                        dst[0] = bgR;
                        dst[1] = bgG;
                        dst[2] = bgB;
                        dst[3] = 255;
                    } else {
                        uint32_t invA = 255 - sa;
                        dst[0] = static_cast<uint8_t>((src[0] * sa + bgR * invA) / 255);
                        dst[1] = static_cast<uint8_t>((src[1] * sa + bgG * invA) / 255);
                        dst[2] = static_cast<uint8_t>((src[2] * sa + bgB * invA) / 255);
                        dst[3] = 255;
                    }
                    src += 4;
                    dst += 4;
                }
            }
        }
    }

    bakeThemeCardBack(textures[CardTextureCache::kCardBackIndex], currentTheme, cardW, cardH);
    syncArgb32AndMips(textures);

    if (!binPath.empty()) {
        saveBinaryCache(binPath, cardW, cardH, textures);
    }
    return true;
}

static bool processDecodedSheet(const uint8_t* sheetData, int sheetW, int sheetH,
                                int cardW, int cardH,
                                std::array<CardTexture, CardTextureCache::kTotalTextures>& textures,
                                DeckThemeId currentTheme,
                                const std::string& binPath) {
    constexpr int cols = 9;
    constexpr int rows = 6;
    const int cellSrcW = sheetW / cols;
    const int cellSrcH = sheetH / rows;

    const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
    for (Suit s : suits) {
        for (uint8_t r = 1; r <= 13; ++r) {
            Rank rank = static_cast<Rank>(r);
            size_t cell = svgCellIndexForCard(s, rank);
            size_t destIdx = CardTextureCache::cardIndex(s, rank);

            int col = static_cast<int>(cell % cols);
            int row = static_cast<int>(cell / cols);
            int srcStartX = col * cellSrcW;
            int srcStartY = row * cellSrcH;

            CardTexture& tex = textures[destIdx];
            tex.width = cardW;
            tex.height = cardH;
            tex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

            if (cellSrcW == cardW && cellSrcH == cardH) {
                for (int y = 0; y < cardH; ++y) {
                    const uint8_t* src = &sheetData[static_cast<size_t>((srcStartY + y) * sheetW + srcStartX) * 4];
                    uint8_t* dst = &tex.rgba[static_cast<size_t>(y * cardW) * 4];
                    std::memcpy(dst, src, static_cast<size_t>(cardW * 4));
                }
            } else {
                for (int y = 0; y < cardH; ++y) {
                    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(cardH) * static_cast<float>(cellSrcH) - 0.5f;
                    int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, cellSrcH - 1);
                    int y1 = std::clamp(y0 + 1, 0, cellSrcH - 1);
                    float fy = v - std::floor(v);

                    uint8_t* dst = &tex.rgba[static_cast<size_t>(y * cardW) * 4];
                    for (int x = 0; x < cardW; ++x) {
                        float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(cardW) * static_cast<float>(cellSrcW) - 0.5f;
                        int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, cellSrcW - 1);
                        int x1 = std::clamp(x0 + 1, 0, cellSrcW - 1);
                        float fx = u - std::floor(u);

                        const uint8_t* p00 = &sheetData[static_cast<size_t>((srcStartY + y0) * sheetW + srcStartX + x0) * 4];
                        const uint8_t* p10 = &sheetData[static_cast<size_t>((srcStartY + y0) * sheetW + srcStartX + x1) * 4];
                        const uint8_t* p01 = &sheetData[static_cast<size_t>((srcStartY + y1) * sheetW + srcStartX + x0) * 4];
                        const uint8_t* p11 = &sheetData[static_cast<size_t>((srcStartY + y1) * sheetW + srcStartX + x1) * 4];

                        for (int c = 0; c < 4; ++c) {
                            float top = p00[c] * (1.0f - fx) + p10[c] * fx;
                            float bot = p01[c] * (1.0f - fx) + p11[c] * fx;
                            dst[x * 4 + c] = static_cast<uint8_t>(std::clamp(std::round(top * (1.0f - fy) + bot * fy), 0.0f, 255.0f));
                        }
                    }
                }
            }
        }
    }

    bakeThemeCardBack(textures[CardTextureCache::kCardBackIndex], currentTheme, cardW, cardH);
    syncArgb32AndMips(textures);

    if (!binPath.empty()) {
        saveBinaryCache(binPath, cardW, cardH, textures);
    }
    return true;
}

static void bakeBaseProceduralDeck(
    int cardW,
    int cardH,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
    const CardTextureCache::ProgressCallback& onProgress = nullptr
) {
    const std::string rankNames[] = {
        "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    const char* suitDisplayNames[4] = { "Spades", "Hearts", "Diamonds", "Clubs" };

    for (size_t sIdx = 0; sIdx < 4; ++sIdx) {
        Suit s = static_cast<Suit>(sIdx);
        for (size_t rIdx = 1; rIdx <= 13; ++rIdx) {
            size_t cardNum = sIdx * 13 + rIdx;
            if (onProgress) {
                float frac = static_cast<float>(cardNum - 1) / static_cast<float>(CardTextureCache::kTotalTextures);
                std::string msg = "Synthesizing Card " + std::to_string(cardNum) + "/53 (" +
                                  rankNames[rIdx - 1] + " of " + suitDisplayNames[sIdx] + ")...";
                if (!onProgress(frac, msg)) return;
            }
            size_t idx = CardTextureCache::cardIndex(s, static_cast<Rank>(rIdx));
            CardCanvas canvas(cardW, cardH);
            canvas.drawCardFace(rankNames[rIdx - 1], s, static_cast<float>(cardW), static_cast<float>(cardH));
            outBase[idx].width = cardW;
            outBase[idx].height = cardH;
            outBase[idx].rgba = std::move(canvas.rgba());
        }
    }

    if (onProgress) {
        onProgress(52.0f / 53.0f, "Synthesizing Card 53/53 (Card Back)...");
    }
    bakeThemeCardBack(outBase[CardTextureCache::kCardBackIndex], DeckThemeId::SolEnshade, cardW, cardH);
}

static bool loadBaseFromMemory(
    const uint8_t* data,
    size_t size,
    const std::string& format,
    int cardW,
    int cardH,
    DeckThemeId theme,
    const std::string& cacheKey,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
    const CardTextureCache::ProgressCallback& onProgress = nullptr
) {
    if (!data || size == 0) return false;

    std::string binPath;
    if (!cacheKey.empty()) {
        binPath = cacheKey;
        size_t dotPos = binPath.rfind('.');
        if (dotPos != std::string::npos) {
            binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
        } else {
            binPath += ".bin";
        }
        if (tryLoadBinaryCache(binPath, cacheKey, cardW, cardH, outBase)) {
            if (onProgress && !onProgress(1.0f, "Loaded cached deck artwork.")) return false;
            return true;
        }
    }

    if (format == "svgz" || format == "svg_zlib" || format == "svg_deflate") {
        if (onProgress && !onProgress(0.15f, "Decompressing vector deck payload...")) return false;
        int outLen = 0;
        char* decomp = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(data),
                                               static_cast<int>(size), &outLen);
        if (!decomp) {
            decomp = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(data),
                                                      static_cast<int>(size), &outLen);
        }
        if (!decomp || outLen <= 0) {
            std::cerr << "[SolEngine] Failed to decompress SVGZ payload\n";
            return false;
        }

        std::vector<char> svgXml(decomp, decomp + outLen);
        svgXml.push_back('\0');
        free(decomp);

        if (onProgress && !onProgress(0.35f, "Parsing vector SVG geometry...")) return false;
        NSVGimage* image = nsvgParse(svgXml.data(), "px", 96.0f);
        if (!image) {
            std::cerr << "[SolEngine] Failed to parse decompressed SVG deck\n";
            return false;
        }

        if (onProgress && !onProgress(0.60f, "Rasterizing high-DPI vector card sheet...")) {
            nsvgDelete(image);
            return false;
        }
        bool ok = rasterizeSvgToTextures(image, cardW, cardH, outBase, theme, binPath);
        if (ok && onProgress && !onProgress(1.0f, "Vector card sheet rasterized.")) return false;
        return ok;
    } else if (format == "svg") {
        if (onProgress && !onProgress(0.25f, "Parsing vector SVG geometry...")) return false;
        std::vector<char> svgXml(data, data + size);
        svgXml.push_back('\0');

        NSVGimage* image = nsvgParse(svgXml.data(), "px", 96.0f);
        if (!image) {
            std::cerr << "[SolEngine] Failed to parse SVG deck\n";
            return false;
        }

        if (onProgress && !onProgress(0.60f, "Rasterizing high-DPI vector card sheet...")) {
            nsvgDelete(image);
            return false;
        }
        bool ok = rasterizeSvgToTextures(image, cardW, cardH, outBase, theme, binPath);
        if (ok && onProgress && !onProgress(1.0f, "Vector card sheet rasterized.")) return false;
        return ok;
    } else if (format == "png" || format == "bitmap" || format == "jpg" || format == "jpeg") {
        if (onProgress && !onProgress(0.25f, "Decoding master card sheet bitmap...")) return false;
        int sheetW = 0, sheetH = 0, channels = 0;
        stbi_uc* sheetData = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data),
                                                   static_cast<int>(size),
                                                   &sheetW, &sheetH, &channels, 4);
        if (!sheetData) {
            std::cerr << "[SolEngine] Failed to decode image deck (" << format << "): " << stbi_failure_reason() << "\n";
            return false;
        }

        if (onProgress && !onProgress(0.65f, "Extracting 52 card faces & card back...")) {
            stbi_image_free(sheetData);
            return false;
        }
        bool ok = processDecodedSheet(sheetData, sheetW, sheetH, cardW, cardH, outBase, theme, binPath);
        stbi_image_free(sheetData);
        if (ok && onProgress && !onProgress(1.0f, "Bitmap card sheet extracted.")) return false;
        return ok;
    }

    std::cerr << "[SolEngine] Unknown deck format: " << format << "\n";
    return false;
}

static bool loadBaseFromBitmap(
    const std::string& bitmapFilePath,
    int cardW,
    int cardH,
    DeckThemeId& inOutTheme,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
    const CardTextureCache::ProgressCallback& onProgress = nullptr
) {
    if (bitmapFilePath.find("baroque_light") != std::string::npos || bitmapFilePath.find("baroque_alabaster") != std::string::npos) {
        inOutTheme = DeckThemeId::BaroqueLight;
    } else if (bitmapFilePath.find("baroque_dark") != std::string::npos) {
        inOutTheme = DeckThemeId::BaroqueDark;
    } else if (bitmapFilePath.find("baroque_noir") != std::string::npos || bitmapFilePath.find("baroque") != std::string::npos) {
        inOutTheme = DeckThemeId::BaroqueNoir;
    }

    std::string binPath = bitmapFilePath;
    size_t dotPos = binPath.rfind('.');
    if (dotPos != std::string::npos) {
        binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, bitmapFilePath, cardW, cardH, outBase)) {
        if (onProgress && !onProgress(1.0f, "Loaded cached deck artwork.")) return false;
        return true;
    }

    std::error_code ec;
    if (!std::filesystem::exists(bitmapFilePath, ec)) {
        return false;
    }

    if (onProgress && !onProgress(0.25f, "Decoding bitmap card sheet...")) return false;
    std::cout << "[SolEngine] Loading bitmap deck sheet: " << bitmapFilePath << " ...\n";
    int sheetW = 0, sheetH = 0, channels = 0;
    stbi_uc* sheetData = stbi_load(bitmapFilePath.c_str(), &sheetW, &sheetH, &channels, 4);
    if (!sheetData) {
        std::cerr << "[SolEngine] Failed to load bitmap deck: " << bitmapFilePath << " (" << stbi_failure_reason() << ")\n";
        return false;
    }

    if (onProgress && !onProgress(0.65f, "Extracting 52 card faces & card back...")) {
        stbi_image_free(sheetData);
        return false;
    }
    bool ok = processDecodedSheet(sheetData, sheetW, sheetH, cardW, cardH, outBase, inOutTheme, binPath);
    stbi_image_free(sheetData);
    if (ok && onProgress && !onProgress(1.0f, "Bitmap card sheet extracted.")) return false;
    return ok;
}

static bool loadBaseFromSvg(
    const std::string& svgFilePath,
    int cardW,
    int cardH,
    DeckThemeId theme,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
    const CardTextureCache::ProgressCallback& onProgress = nullptr
) {
    std::string binPath = svgFilePath;
    if (binPath.size() > 4 && binPath.substr(binPath.size() - 4) == ".svg") {
        binPath.replace(binPath.size() - 4, 4, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, svgFilePath, cardW, cardH, outBase)) {
        if (onProgress && !onProgress(1.0f, "Loaded cached deck artwork.")) return false;
        return true;
    }

    std::error_code ec;
    if (!std::filesystem::exists(svgFilePath, ec)) {
        return false;
    }

    if (onProgress && !onProgress(0.25f, "Parsing vector SVG deck sheet...")) return false;
    std::cout << "[SolEngine] Parsing SVG deck: " << svgFilePath << " ...\n";
    NSVGimage* image = nsvgParseFromFile(svgFilePath.c_str(), "px", 96.0f);
    if (!image) {
        std::cerr << "[SolEngine] Could not parse SVG deck: " << svgFilePath << "\n";
        return false;
    }

    if (onProgress && !onProgress(0.60f, "Rasterizing high-DPI vector card sheet...")) {
        nsvgDelete(image);
        return false;
    }
    bool ok = rasterizeSvgToTextures(image, cardW, cardH, outBase, theme, binPath);
    if (ok && onProgress && !onProgress(1.0f, "Vector card sheet rasterized.")) return false;
    return ok;
}

static bool loadBaseFromEats(
    const std::string& eatsFilePath,
    int& inOutCardW,
    int& inOutCardH,
    DeckThemeId& outTheme,
    float& outCornerRadius,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
    const CardTextureCache::ProgressCallback& onProgress = nullptr
) {
    int cardW = inOutCardW;
    int cardH = inOutCardH;

    if (eatsFilePath.find("solenshade") != std::string::npos) {
        outTheme = DeckThemeId::SolEnshade;
    } else if (eatsFilePath.find("cyberpunk") != std::string::npos) {
        outTheme = DeckThemeId::Cyberpunk;
    } else if (eatsFilePath.find("anime") != std::string::npos) {
        outTheme = DeckThemeId::Anime;
    } else if (eatsFilePath.find("steampunk") != std::string::npos) {
        outTheme = DeckThemeId::SteampunkAi;
    } else if (eatsFilePath.find("classic") != std::string::npos) {
        outTheme = DeckThemeId::ClassicSvg;
    } else if (eatsFilePath.find("baroque_light") != std::string::npos || eatsFilePath.find("baroque_alabaster") != std::string::npos) {
        outTheme = DeckThemeId::BaroqueLight;
    } else if (eatsFilePath.find("baroque_dark") != std::string::npos) {
        outTheme = DeckThemeId::BaroqueDark;
    } else if (eatsFilePath.find("baroque_cell_shaded") != std::string::npos || eatsFilePath.find("imperial_baroque") != std::string::npos) {
        outTheme = DeckThemeId::BaroqueCellShaded;
    } else if (eatsFilePath.find("shader_stained_glass") != std::string::npos) {
        outTheme = DeckThemeId::StainedGlass;
    } else if (eatsFilePath.find("shader_holo_synthwave") != std::string::npos) {
        outTheme = DeckThemeId::PolychromeCrt;
    } else if (eatsFilePath.find("shader_watercolor_sketch") != std::string::npos) {
        outTheme = DeckThemeId::WatercolorSketch;
    } else if (eatsFilePath.find("baroque_noir") != std::string::npos || eatsFilePath.find("baroque") != std::string::npos) {
        outTheme = DeckThemeId::BaroqueNoir;
    } else {
        outTheme = DeckThemeId::Custom;
    }

    std::string binPath = eatsFilePath;
    size_t dotPos = binPath.rfind('.');
    if (dotPos != std::string::npos) {
        binPath.replace(dotPos, binPath.size() - dotPos, ".bin");
    } else {
        binPath += ".bin";
    }

    if (tryLoadBinaryCache(binPath, eatsFilePath, cardW, cardH, outBase)) {
        if (onProgress && !onProgress(1.0f, "Loaded cached deck artwork.")) return false;
        return true;
    }

    std::ifstream file(eatsFilePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (onProgress && !onProgress(0.04f, "Evaluating Eatscript deck recipe...")) {
        return false;
    }
    std::cout << "[SolEngine] Parsing Eatscript deck recipe: " << eatsFilePath << " ...\n";

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

    eatsbits::eatscript::Evaluator evaluator;
    bool apiCalled = false;
    std::vector<uint8_t> apiBlob;
    std::string apiFormat = "svgz";
    int apiCardW = cardW;
    int apiCardH = cardH;

    auto setDeckFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                         const std::map<std::string, eatsbits::eatscript::Value>& kwargs) -> eatsbits::eatscript::Value {
        apiCalled = true;
        std::string rawStr;
        if (!args.empty() && args[0].isString()) {
            rawStr = args[0].asString();
        } else if (kwargs.find("blob") != kwargs.end()) {
            rawStr = kwargs.at("blob").asString();
        }

        if (rawStr.rfind("data:", 0) == 0) {
            auto parsed = eatsbits::ui::parseDataUri(rawStr);
            if (parsed && !parsed->data.empty()) {
                apiBlob = std::move(parsed->data);
                if (parsed->mimeType.find("svg") != std::string::npos) apiFormat = "svg";
                else if (parsed->mimeType.find("png") != std::string::npos) apiFormat = "png";
                else if (parsed->mimeType.find("jpeg") != std::string::npos || parsed->mimeType.find("jpg") != std::string::npos) apiFormat = "jpg";
            }
        }
        if (apiBlob.empty() && !rawStr.empty()) {
            apiBlob = eatsbits::ui::base64Decode(rawStr);
            if (apiBlob.empty()) {
                apiBlob.assign(rawStr.begin(), rawStr.end());
            }
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
        auto getOptFloat = [&](const std::string& key, float defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asFloat(defVal);
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asFloat(defVal);
            return defVal;
        };

        apiFormat = getOptString("format", apiFormat);
        apiCardW = getOptInt("card_w", apiCardW);
        apiCardH = getOptInt("card_h", apiCardH);

        float cr = getOptFloat("corner_radius", -1.0f);
        if (cr >= 0.0f) {
            outCornerRadius = cr;
        }

        std::string tId = getOptString("theme_id", getOptString("theme", ""));
        if (!tId.empty()) {
            if (tId == "solenshade") outTheme = DeckThemeId::SolEnshade;
            else if (tId == "classic") outTheme = DeckThemeId::ClassicSvg;
            else if (tId == "steampunk") outTheme = DeckThemeId::SteampunkAi;
            else if (tId == "anime") outTheme = DeckThemeId::Anime;
            else if (tId == "cyberpunk") outTheme = DeckThemeId::Cyberpunk;
            else if (tId == "baroque_light" || tId == "baroque_alabaster") outTheme = DeckThemeId::BaroqueLight;
            else if (tId == "baroque_dark") outTheme = DeckThemeId::BaroqueDark;
            else if (tId == "baroque_cell_shaded" || tId == "imperial_baroque") outTheme = DeckThemeId::BaroqueCellShaded;
            else if (tId == "shader_stained_glass") outTheme = DeckThemeId::StainedGlass;
            else if (tId == "shader_holo_synthwave") outTheme = DeckThemeId::PolychromeCrt;
            else if (tId == "shader_watercolor_sketch") outTheme = DeckThemeId::WatercolorSketch;
            else if (tId == "baroque_noir" || tId == "baroque_relief" || tId == "baroque_arches" || tId == "baroque") outTheme = DeckThemeId::BaroqueNoir;
        }

        return eatsbits::eatscript::Value(true);
    };

    auto themeFn = [&](const auto& args, const auto& kwargs) {
        const auto* optsDict = (!args.empty() && args[0].isDict()) ? &args[0] : nullptr;
        auto getOptString = [&](const std::string& key, const std::string& defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asString();
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asString();
            return defVal;
        };
        auto getOptFloat = [&](const std::string& key, float defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asFloat(defVal);
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asFloat(defVal);
            return defVal;
        };
        std::string themeId = getOptString("id", getOptString("theme_id", ""));
        if (!themeId.empty()) {
            if (themeId == "solenshade") outTheme = DeckThemeId::SolEnshade;
            else if (themeId == "classic") outTheme = DeckThemeId::ClassicSvg;
            else if (themeId == "steampunk") outTheme = DeckThemeId::SteampunkAi;
            else if (themeId == "anime") outTheme = DeckThemeId::Anime;
            else if (themeId == "cyberpunk") outTheme = DeckThemeId::Cyberpunk;
            else if (themeId == "baroque_light" || themeId == "baroque_alabaster") outTheme = DeckThemeId::BaroqueLight;
            else if (themeId == "baroque_dark") outTheme = DeckThemeId::BaroqueDark;
            else if (themeId == "baroque_cell_shaded" || themeId == "imperial_baroque") outTheme = DeckThemeId::BaroqueCellShaded;
            else if (themeId == "shader_stained_glass") outTheme = DeckThemeId::StainedGlass;
            else if (themeId == "shader_holo_synthwave") outTheme = DeckThemeId::PolychromeCrt;
            else if (themeId == "shader_watercolor_sketch") outTheme = DeckThemeId::WatercolorSketch;
            else if (themeId == "baroque_noir" || themeId == "baroque_relief" || themeId == "baroque_arches" || themeId == "baroque") outTheme = DeckThemeId::BaroqueNoir;
        }
        float cr = getOptFloat("corner_radius", -1.0f);
        if (cr >= 0.0f) {
            outCornerRadius = cr;
        }
        return eatsbits::eatscript::Value(true);
    };

    auto setCornerRadiusFn = [&](const auto& args, const auto& kwargs) {
        float r = eatsbits::eatscript::resolveArg(args, kwargs, 0, "radius", eatsbits::eatscript::Value(24.0)).asFloat(24.0f);
        outCornerRadius = r;
        return eatsbits::eatscript::Value(true);
    };

    evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("sol", "create_theme", themeFn);
    evaluator.registerMemberFunction("sol", "set_theme", themeFn);
    evaluator.registerMemberFunction("sol", "set_deck", setDeckFn);
    evaluator.registerMemberFunction("sol", "set_corner_radius", setCornerRadiusFn);
    evaluator.registerFunction("create_theme", themeFn);
    evaluator.registerFunction("set_theme", themeFn);
    evaluator.registerFunction("set_deck", setDeckFn);
    evaluator.registerFunction("set_corner_radius", setCornerRadiusFn);

    CardCanvas::bindToEvaluator(evaluator, nullptr);
    evaluator.evaluateSource(scriptPart);

    if (!evaluator.getLastError().empty()) {
        std::cerr << "[SolEngine] Eatscript deck parse/eval error in " << eatsFilePath << ":\n"
                  << "              " << evaluator.getLastError() << "\n";
    }

    if (evaluator.hasFunction("on_draw_card_face") || evaluator.hasFunction("on_draw_card_back")) {
        std::cout << "[SolEngine] Generating procedural card deck via Eatscript drawing API...\n";

        const Suit suits[4] = { Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs };
        const char* suitNames[4] = { "spades", "hearts", "diamonds", "clubs" };
        const char* suitDisplayNames[4] = { "Spades", "Hearts", "Diamonds", "Clubs" };
        const char* rankNames[13] = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };

        CardCanvas canvas(cardW, cardH);
        CardCanvas::bindToEvaluator(evaluator, &canvas);

        for (int sIdx = 0; sIdx < 4; ++sIdx) {
            Suit s = suits[sIdx];
            for (int rIdx = 1; rIdx <= 13; ++rIdx) {
                int cardNum = sIdx * 13 + rIdx;
                if (onProgress) {
                    float frac = 0.05f + 0.88f * (static_cast<float>(cardNum - 1) / static_cast<float>(CardTextureCache::kTotalTextures));
                    std::string msg = "Synthesizing Card " + std::to_string(cardNum) + "/53 (" +
                                      std::string(rankNames[rIdx - 1]) + " of " + std::string(suitDisplayNames[sIdx]) + ")...";
                    if (!onProgress(frac, msg)) {
                        return false;
                    }
                }

                Rank r = static_cast<Rank>(rIdx);
                size_t destIdx = CardTextureCache::cardIndex(s, r);
                CardTexture& tex = outBase[destIdx];
                tex.width = cardW;
                tex.height = cardH;

                canvas.clear(0x00000000);
                if (evaluator.hasFunction("on_draw_card_face")) {
                    evaluator.callFunction("on_draw_card_face", {
                        eatsbits::eatscript::Value("ctx"),
                        eatsbits::eatscript::Value(rankNames[rIdx - 1]),
                        eatsbits::eatscript::Value(suitNames[sIdx]),
                        eatsbits::eatscript::Value(static_cast<double>(cardW)),
                        eatsbits::eatscript::Value(static_cast<double>(cardH))
                    });
                    if (!evaluator.getLastError().empty()) {
                        std::cerr << "[SolEngine] on_draw_card_face error (" << rankNames[rIdx - 1] << " of " << suitNames[sIdx] << "): "
                                  << evaluator.getLastError() << "\n";
                    }
                } else {
                    canvas.drawCardFace(rankNames[rIdx - 1], s, static_cast<float>(cardW), static_cast<float>(cardH));
                }
                tex.rgba = canvas.rgba();
            }
        }

        if (onProgress && !onProgress(0.93f, "Synthesizing Card 53/53 (Card Back)...")) {
            return false;
        }

        // Card Back (Index: kTotalTextures - 1)
        size_t backIdx = CardTextureCache::kTotalTextures - 1;
        CardTexture& backTex = outBase[backIdx];
        backTex.width = cardW;
        backTex.height = cardH;

        canvas.clear(0x00000000);
        if (evaluator.hasFunction("on_draw_card_back")) {
            evaluator.callFunction("on_draw_card_back", {
                eatsbits::eatscript::Value("ctx"),
                eatsbits::eatscript::Value(static_cast<double>(cardW)),
                eatsbits::eatscript::Value(static_cast<double>(cardH))
            });
            backTex.rgba = canvas.rgba();
        } else {
            canvas.fillRoundedRect(0, 0, static_cast<float>(cardW), static_cast<float>(cardH), 8.0f, 0xFF1B365D);
            canvas.strokeRoundedRect(cardW * 0.05f, cardH * 0.04f, cardW * 0.90f, cardH * 0.92f, 6.0f, 0xFFD4AF37, 1.5f);
            backTex.rgba = canvas.rgba();
        }

        if (onProgress && !onProgress(0.97f, "Saving compressed deck cache...")) {
            return false;
        }
        if (!binPath.empty()) {
            saveBinaryCache(binPath, cardW, cardH, outBase);
        }
        if (onProgress && !onProgress(1.0f, "Procedural deck synthesis complete.")) {
            return false;
        }
        inOutCardW = cardW;
        inOutCardH = cardH;
        return true;
    }

    std::vector<uint8_t> rawBytes;
    std::string format = "svgz";

    if (apiCalled && !apiBlob.empty()) {
        rawBytes = std::move(apiBlob);
        format = apiFormat;
        cardW = apiCardW;
        cardH = apiCardH;
    } else {
        if (evaluator.hasVariable("format")) {
            format = evaluator.getVariable("format").asString();
        } else if (evaluator.hasVariable("deck_format")) {
            format = evaluator.getVariable("deck_format").asString();
        }

        if (evaluator.hasVariable("card_w")) {
            cardW = static_cast<int>(evaluator.getVariable("card_w").asNumber());
        }
        if (evaluator.hasVariable("card_h")) {
            cardH = static_cast<int>(evaluator.getVariable("card_h").asNumber());
        }

        if (!blobPart.empty()) {
            rawBytes.assign(blobPart.begin(), blobPart.end());
        } else {
            std::string blobStr;
            if (evaluator.hasVariable("deck_blob")) {
                blobStr = evaluator.getVariable("deck_blob").asString();
            } else if (evaluator.hasVariable("blob")) {
                blobStr = evaluator.getVariable("blob").asString();
            }
            if (!blobStr.empty()) {
                rawBytes = eatsbits::ui::base64Decode(blobStr);
                if (rawBytes.empty() && format == "svg") {
                    rawBytes.assign(blobStr.begin(), blobStr.end());
                }
            }
        }
    }

    if (rawBytes.empty()) {
        std::cerr << "[SolEngine] No deck payload found in Eatscript deck: " << eatsFilePath << "\n";
        return false;
    }

    inOutCardW = cardW;
    inOutCardH = cardH;
    return loadBaseFromMemory(rawBytes.data(), rawBytes.size(), format, cardW, cardH, outTheme, eatsFilePath, outBase, onProgress);
}

bool CardTextureCache::loadBaseDeckTheme(
    DeckThemeId theme,
    int cardW,
    int cardH,
    std::array<CardTexture, kTotalTextures>& outBase,
    const ProgressCallback& onProgress
) {
    if (theme == currentTheme_ && isLoaded_ &&
        &outBase != &baseTextures_ &&
        baseTextures_[0].width == cardW &&
        baseTextures_[0].height == cardH &&
        !baseTextures_[0].rgba.empty()) {
        outBase = baseTextures_;
        if (onProgress && !onProgress(1.0f, "Loaded active deck master.")) return false;
        return true;
    }

    DeckThemeId loadedTheme = theme;
    float cornerRadius = -1.0f;
    int targetW = cardW;
    int targetH = cardH;

    auto tryEats = [&](const std::string& path) -> bool {
        targetW = cardW;
        targetH = cardH;
        return loadBaseFromEats(path, targetW, targetH, loadedTheme, cornerRadius, outBase, onProgress);
    };
    auto tryBitmap = [&](const std::string& path) -> bool {
        return loadBaseFromBitmap(path, cardW, cardH, loadedTheme, outBase, onProgress);
    };
    auto trySvg = [&](const std::string& path) -> bool {
        return loadBaseFromSvg(path, cardW, cardH, theme, outBase, onProgress);
    };

    if (theme == DeckThemeId::SolEnshade) {
        const std::string candidateEats[] = {
            "decks/solenshade.eats",
            "/decks/solenshade.eats",
            "/user/solenshade.eats",
            "../decks/solenshade.eats",
            "../../decks/solenshade.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
    } else if (theme == DeckThemeId::ClassicSvg) {
        const std::string candidateEats[] = {
            "decks/classic.eats",
            "/decks/classic.eats",
            "/user/classic.eats",
            "../decks/classic.eats",
            "../../decks/classic.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidatePaths[] = {
            "decks/classic_svg.svg",
            "../decks/classic_svg.svg",
            "../../decks/classic_svg.svg"
        };
        for (const auto& path : candidatePaths) {
            if (trySvg(path)) return true;
        }
    } else if (theme == DeckThemeId::SteampunkAi) {
        bool loaded = false;
        const std::string candidateEats[] = {
            "decks/steampunk.eats",
            "/decks/steampunk.eats",
            "/user/steampunk.eats",
            "../decks/steampunk.eats",
            "../../decks/steampunk.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) { loaded = true; break; }
        }
        if (!loaded) {
            const std::string candidateBitmaps[] = {
                "decks/steampunk.jpg",
                "decks/steampunk.png",
                "../decks/steampunk.jpg",
                "../decks/steampunk.png",
                "../../decks/steampunk.jpg",
                "../../decks/steampunk.png"
            };
            for (const auto& path : candidateBitmaps) {
                if (tryBitmap(path)) { loaded = true; break; }
            }
        }
        if (!loaded) {
            const std::string candidateSvgs[] = {
                "decks/steampunk_svg.svg",
                "../decks/steampunk_svg.svg",
                "../../decks/steampunk_svg.svg"
            };
            for (const auto& path : candidateSvgs) {
                if (trySvg(path)) { loaded = true; break; }
            }
        }
        if (loaded) {
            bakeThemeCardBack(outBase[CardTextureCache::kCardBackIndex], DeckThemeId::SteampunkAi, cardW, cardH);
            return true;
        }
    } else if (theme == DeckThemeId::BaroqueNoir) {
        const std::string candidateEats[] = {
            "decks/baroque_arches.eats",
            "decks/baroque_noir.eats",
            "decks/baroque_relief.eats",
            "/decks/baroque_arches.eats",
            "/decks/baroque_noir.eats",
            "/decks/baroque_relief.eats",
            "/user/baroque_arches.eats",
            "/user/baroque_noir.eats",
            "../decks/baroque_arches.eats",
            "../decks/baroque_noir.eats",
            "../../decks/baroque_arches.eats",
            "../../decks/baroque_noir.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_relief_KRITA_proc.jpg",
            "decks/baroque_noir.jpg",
            "decks/baroque_relief.jpg",
            "decks/baroque_noir.png",
            "../decks/baroque_relief_KRITA_proc.jpg",
            "../decks/baroque_noir.jpg",
            "../../decks/baroque_noir.jpg"
        };
        for (const auto& path : candidateBitmaps) {
            if (tryBitmap(path)) return true;
        }
    } else if (theme == DeckThemeId::BaroqueLight) {
        const std::string candidateEats[] = {
            "decks/baroque_light.eats",
            "decks/baroque_alabaster.eats",
            "/decks/baroque_light.eats",
            "/decks/baroque_alabaster.eats",
            "/user/baroque_light.eats",
            "../decks/baroque_light.eats",
            "../../decks/baroque_light.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_light.jpg",
            "decks/baroque_alabaster.jpg",
            "decks/baroque_light.png",
            "decks/baroque_alabaster.png",
            "../decks/baroque_light.jpg",
            "../../decks/baroque_light.jpg"
        };
        for (const auto& path : candidateBitmaps) {
            if (tryBitmap(path)) return true;
        }
    } else if (theme == DeckThemeId::BaroqueDark) {
        const std::string candidateEats[] = {
            "decks/baroque_dark.eats",
            "/decks/baroque_dark.eats",
            "/user/baroque_dark.eats",
            "../decks/baroque_dark.eats",
            "../../decks/baroque_dark.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_dark.jpg",
            "decks/baroque_dark.png",
            "../decks/baroque_dark.jpg",
            "../../decks/baroque_dark.jpg"
        };
        for (const auto& path : candidateBitmaps) {
            if (tryBitmap(path)) return true;
        }
    } else if (theme == DeckThemeId::BaroqueCellShaded) {
        const std::string candidateEats[] = {
            "decks/baroque_cell_shaded.eats",
            "decks/imperial_baroque.eats",
            "/decks/baroque_cell_shaded.eats",
            "/decks/imperial_baroque.eats",
            "/user/baroque_cell_shaded.eats",
            "../decks/baroque_cell_shaded.eats",
            "../decks/imperial_baroque.eats",
            "../../decks/baroque_cell_shaded.eats",
            "../../decks/imperial_baroque.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
    } else if (theme == DeckThemeId::StainedGlass) {
        for (const auto& path : {"decks/shader_stained_glass.eats", "/decks/shader_stained_glass.eats", "../decks/shader_stained_glass.eats", "../../decks/shader_stained_glass.eats"}) {
            if (tryEats(path)) return true;
        }
    } else if (theme == DeckThemeId::PolychromeCrt) {
        for (const auto& path : {"decks/shader_holo_synthwave.eats", "/decks/shader_holo_synthwave.eats", "../decks/shader_holo_synthwave.eats", "../../decks/shader_holo_synthwave.eats"}) {
            if (tryEats(path)) return true;
        }
    } else if (theme == DeckThemeId::WatercolorSketch) {
        for (const auto& path : {"decks/shader_watercolor_sketch.eats", "/decks/shader_watercolor_sketch.eats", "../decks/shader_watercolor_sketch.eats", "../../decks/shader_watercolor_sketch.eats"}) {
            if (tryEats(path)) return true;
        }
    } else if (theme == DeckThemeId::Custom) {
        const std::string candidateEats[] = {
            "decks/custom.eats",
            "/decks/custom.eats",
            "/user/custom.eats",
            "../decks/custom.eats",
            "../../decks/custom.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/custom.jpg",
            "decks/custom.png",
            "../decks/custom.jpg",
            "../../decks/custom.jpg"
        };
        for (const auto& path : candidateBitmaps) {
            if (tryBitmap(path)) return true;
        }
    } else if (theme == DeckThemeId::Anime) {
        const std::string candidateEats[] = {
            "decks/anime.eats",
            "/decks/anime.eats",
            "/user/anime.eats",
            "../decks/anime.eats",
            "../../decks/anime.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/anime.jpg",
            "decks/anime.png",
            "../decks/anime.jpg",
            "../../decks/anime.jpg"
        };
        for (const auto& path : candidateBitmaps) {
            if (tryBitmap(path)) return true;
        }
        const std::string candidateSvgs[] = {
            "decks/anime_svg.svg",
            "../decks/anime_svg.svg",
            "../../decks/anime_svg.svg"
        };
        for (const auto& path : candidateSvgs) {
            if (trySvg(path)) return true;
        }
    } else if (theme == DeckThemeId::Cyberpunk) {
        const std::string candidateEats[] = {
            "decks/cyberpunk.eats",
            "/decks/cyberpunk.eats",
            "/user/cyberpunk.eats",
            "../decks/cyberpunk.eats",
            "../../decks/cyberpunk.eats"
        };
        for (const auto& path : candidateEats) {
            if (tryEats(path)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/cyberpunk.jpg",
            "decks/cyberpunk.png",
            "../decks/cyberpunk.jpg",
            "../../decks/cyberpunk.jpg"
        };
        for (const auto& path : candidateBitmaps) {
            if (tryBitmap(path)) return true;
        }
        const std::string candidateSvgs[] = {
            "decks/cyberpunk_svg.svg",
            "../decks/cyberpunk_svg.svg",
            "../../decks/cyberpunk_svg.svg"
        };
        for (const auto& path : candidateSvgs) {
            if (trySvg(path)) return true;
        }
    }

    if (theme != DeckThemeId::SolEnshade) {
        std::cout << "[SolEngine] Themed deck not found, falling back to SolEnshade.\n";
        return loadBaseDeckTheme(DeckThemeId::SolEnshade, cardW, cardH, outBase, onProgress);
    }

    std::cout << "[SolEngine] Falling back to built-in procedural high-res card textures.\n";
    bakeBaseProceduralDeck(cardW, cardH, outBase, onProgress);
    return true;
}

bool CardTextureCache::loadDeckTheme(DeckThemeId theme, int cardW, int cardH) {
    currentTheme_ = theme;
    cardW_ = cardW;
    cardH_ = cardH;

    if (!loadBaseDeckTheme(theme, cardW, cardH, baseTextures_, nullptr)) {
        return false;
    }
    applyConditioningAndSync(wearLevel_, cardStock_);
    isLoaded_ = true;
    return true;
}

bool CardTextureCache::bakeStagedConditioning(
    StagingDeck& staging,
    CardWearLevel wear,
    CardStock stock,
    const ProgressCallback& onProgress
) {
    if (!staging.hasNewBase) {
        staging.theme = currentTheme_;
        staging.cardW = cardW_;
        staging.cardH = cardH_;
    }
    staging.wearLevel = wear;
    staging.cardStock = stock;

    const auto& srcBase = staging.hasNewBase ? staging.baseTextures : baseTextures_;
    if (srcBase[0].width <= 0 || srcBase[0].rgba.empty()) {
        return false;
    }

    bool ok = applyCardConditioning(
        srcBase,
        staging.textures,
        wear,
        stock,
        [&onProgress](float frac, const char* desc) -> bool {
            if (onProgress) {
                return onProgress(frac * 0.90f, std::string(desc));
            }
            return true;
        }
    );
    if (!ok) return false;

    if (staging.theme == DeckThemeId::SteampunkAi) {
        bakeThemeCardBack(staging.textures[kCardBackIndex], staging.theme, staging.cardW, staging.cardH);
    }

    if (onProgress && !onProgress(0.92f, "Pre-packing ARGB32 buffers & mipmaps...")) {
        return false;
    }
    syncArgb32AndMips(staging.textures);

    if (onProgress && !onProgress(1.0f, "Conditioning complete.")) {
        return false;
    }
    return true;
}

bool CardTextureCache::loadStagedDeckTheme(
    StagingDeck& staging,
    DeckThemeId theme,
    CardWearLevel wear,
    CardStock stock,
    int cardW,
    int cardH,
    const ProgressCallback& onProgress
) {
    if (onProgress && !onProgress(0.02f, "Loading card artwork master...")) {
        return false;
    }

    staging.theme = theme;
    staging.wearLevel = wear;
    staging.cardStock = stock;
    staging.cardW = cardW;
    staging.cardH = cardH;
    staging.hasNewBase = false;
    staging.cornerRadius = -1.0f;
    staging.hasNewBase = false;

    auto baseProgress = [&onProgress](float p, const std::string& msg) -> bool {
        if (!onProgress) return true;
        return onProgress(0.02f + p * 0.70f, msg);
    };

    if (!loadBaseDeckTheme(theme, cardW, cardH, staging.baseTextures, baseProgress)) {
        return false;
    }
    staging.hasNewBase = true;

    auto condProgress = [&onProgress](float p, const std::string& msg) -> bool {
        if (!onProgress) return true;
        return onProgress(0.72f + p * 0.28f, msg);
    };

    return bakeStagedConditioning(staging, wear, stock, condProgress);
}

bool CardTextureCache::loadStagedFileDeck(
    StagingDeck& staging,
    const std::string& filePath,
    CardWearLevel wear,
    CardStock stock,
    int cardW,
    int cardH,
    const ProgressCallback& onProgress
) {
    if (onProgress && !onProgress(0.02f, "Loading custom deck file...")) {
        return false;
    }

    staging.theme = DeckThemeId::Custom;
    staging.wearLevel = wear;
    staging.cardStock = stock;
    staging.cardW = cardW;
    staging.cardH = cardH;
    staging.cornerRadius = -1.0f;
    staging.hasNewBase = false;

    std::string ext;
    size_t dot = filePath.rfind('.');
    if (dot != std::string::npos) {
        ext = filePath.substr(dot);
        for (char& c : ext) c = static_cast<char>(std::tolower(c));
    }

    auto baseProgress = [&onProgress](float p, const std::string& msg) -> bool {
        if (!onProgress) return true;
        return onProgress(0.02f + p * 0.70f, msg);
    };

    bool loaded = false;
    if (ext == ".eats") {
        loaded = loadBaseFromEats(filePath, staging.cardW, staging.cardH, staging.theme, staging.cornerRadius, staging.baseTextures, baseProgress);
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        loaded = loadBaseFromBitmap(filePath, staging.cardW, staging.cardH, staging.theme, staging.baseTextures, baseProgress);
    } else if (ext == ".svg") {
        loaded = loadBaseFromSvg(filePath, staging.cardW, staging.cardH, staging.theme, staging.baseTextures, baseProgress);
    } else {
        loaded = loadBaseFromEats(filePath, staging.cardW, staging.cardH, staging.theme, staging.cornerRadius, staging.baseTextures, baseProgress);
    }

    if (!loaded) {
        return false;
    }
    staging.hasNewBase = true;

    auto condProgress = [&onProgress](float p, const std::string& msg) -> bool {
        if (!onProgress) return true;
        return onProgress(0.72f + p * 0.28f, msg);
    };

    return bakeStagedConditioning(staging, wear, stock, condProgress);
}

bool CardTextureCache::loadStagedMemoryDeck(
    StagingDeck& staging,
    const uint8_t* data,
    size_t size,
    const std::string& format,
    DeckThemeId theme,
    const std::string& cacheKey,
    CardWearLevel wear,
    CardStock stock,
    int cardW,
    int cardH,
    const ProgressCallback& onProgress
) {
    if (onProgress && !onProgress(0.02f, "Decoding embedded deck payload...")) {
        return false;
    }

    staging.theme = theme;
    staging.wearLevel = wear;
    staging.cardStock = stock;
    staging.cardW = cardW;
    staging.cardH = cardH;
    staging.hasNewBase = false;

    auto baseProgress = [&onProgress](float p, const std::string& msg) -> bool {
        if (!onProgress) return true;
        return onProgress(0.02f + p * 0.70f, msg);
    };

    if (!loadBaseFromMemory(data, size, format, cardW, cardH, theme, cacheKey, staging.baseTextures, baseProgress)) {
        return false;
    }
    staging.hasNewBase = true;

    auto condProgress = [&onProgress](float p, const std::string& msg) -> bool {
        if (!onProgress) return true;
        return onProgress(0.72f + p * 0.28f, msg);
    };

    return bakeStagedConditioning(staging, wear, stock, condProgress);
}

void CardTextureCache::commitStagedDeck(StagingDeck&& staging) {
    if (staging.textures[0].width <= 0 || staging.textures[0].height <= 0 || staging.textures[0].rgba.empty()) {
        std::cerr << "[SolEngine] Error: Refusing to commit empty/invalid staged deck!\n";
        return;
    }
    if (staging.hasNewBase) {
        baseTextures_ = std::move(staging.baseTextures);
    }
    textures_ = std::move(staging.textures);
    currentTheme_ = staging.theme;
    wearLevel_ = staging.wearLevel;
    cardStock_ = staging.cardStock;
    cardW_ = staging.cardW;
    cardH_ = staging.cardH;
    if (staging.cornerRadius >= 0.0f) {
        setDeckCornerRadius(staging.cornerRadius);
    }
    if (currentTheme_ == DeckThemeId::SteampunkAi) {
        bakeThemeCardBack(textures_[kCardBackIndex], currentTheme_, cardW_, cardH_);
        syncArgb32AndMips(textures_);
    }
    isLoaded_ = true;
    atlasDirty_ = true;
}

bool CardTextureCache::loadDeckFromMemory(
    const uint8_t* data,
    size_t size,
    const std::string& format,
    int cardW,
    int cardH,
    DeckThemeId theme,
    const std::string& cacheKey
) {
    if (!loadBaseFromMemory(data, size, format, cardW, cardH, theme, cacheKey, baseTextures_, nullptr)) {
        return false;
    }
    cardW_ = cardW;
    cardH_ = cardH;
    currentTheme_ = theme;
    applyConditioningAndSync(wearLevel_, cardStock_);
    isLoaded_ = true;
    return true;
}

bool CardTextureCache::loadEatsDeck(const std::string& eatsFilePath, int cardW, int cardH) {
    DeckThemeId theme = currentTheme_;
    float cornerRadius = -1.0f;
    int targetW = cardW;
    int targetH = cardH;
    if (!loadBaseFromEats(eatsFilePath, targetW, targetH, theme, cornerRadius, baseTextures_, nullptr)) {
        return false;
    }
    currentTheme_ = theme;
    cardW_ = targetW;
    cardH_ = targetH;
    if (cornerRadius >= 0.0f) {
        setDeckCornerRadius(cornerRadius);
    }
    applyConditioningAndSync(wearLevel_, cardStock_);
    isLoaded_ = true;
    return true;
}

bool CardTextureCache::loadBitmapDeck(const std::string& bitmapFilePath, int cardW, int cardH) {
    DeckThemeId theme = currentTheme_;
    if (!loadBaseFromBitmap(bitmapFilePath, cardW, cardH, theme, baseTextures_, nullptr)) {
        return false;
    }
    currentTheme_ = theme;
    cardW_ = cardW;
    cardH_ = cardH;
    applyConditioningAndSync(wearLevel_, cardStock_);
    isLoaded_ = true;
    return true;
}

bool CardTextureCache::loadSvgDeck(const std::string& svgFilePath, int cardW, int cardH) {
    if (!loadBaseFromSvg(svgFilePath, cardW, cardH, currentTheme_, baseTextures_, nullptr)) {
        return false;
    }
    cardW_ = cardW;
    cardH_ = cardH;
    applyConditioningAndSync(wearLevel_, cardStock_);
    isLoaded_ = true;
    return true;
}

void CardTextureCache::bakeProceduralDeck(int cardW, int cardH) {
    cardW_ = cardW;
    cardH_ = cardH;
    bakeBaseProceduralDeck(cardW, cardH, baseTextures_, nullptr);
    applyConditioningAndSync(wearLevel_, cardStock_);
    isLoaded_ = true;
}

const CardTexture* CardTextureCache::getCardTexture(Suit s, Rank r) const noexcept {
    size_t idx = cardIndex(s, r);
    return (idx < textures_.size() && isLoaded_) ? &textures_[idx] : nullptr;
}

const CardTexture* CardTextureCache::getCardBackTexture() const noexcept {
    return isLoaded_ ? &textures_[kCardBackIndex] : nullptr;
}

const CardTexture* CardTextureCache::getTextureByIndex(size_t index) const noexcept {
    return (index < textures_.size() && isLoaded_) ? &textures_[index] : nullptr;
}

bool CardTextureCache::buildGpuAtlas(eatsbits::ui::BatchRenderer2D& renderer) {
    if (!isLoaded_) return false;

    // 4096 x 4096 texture atlas for the GPU, holding uncompressed native full-resolution cards
    constexpr int kAtlasDim = 4096;
    std::vector<uint32_t> atlasRgba(static_cast<size_t>(kAtlasDim * kAtlasDim), 0);

    // Check if full-resolution fits in the 4096 atlas; otherwise fall back to mip1
    bool preferNative = true;
    if (textures_[0].width > 0 && textures_[0].height > 0) {
        int testPitchX = textures_[0].width + 4;
        int testPitchY = textures_[0].height + 4;
        int testCols = std::max(1, (kAtlasDim - 4) / testPitchX);
        int maxRow = static_cast<int>(kTotalTextures - 1) / testCols;
        if (2 + maxRow * testPitchY + textures_[0].height > kAtlasDim) {
            preferNative = false;
        }
    }

    int reportedW = 0;
    int reportedH = 0;

    for (size_t i = 0; i < kTotalTextures; ++i) {
        const CardTexture& tex = textures_[i];
        int srcW = 0;
        int srcH = 0;
        const uint32_t* srcData = nullptr;

        // Prefer native full-resolution ARGB32 (e.g. 360x504) for razor-sharp cards
        if (preferNative && tex.argb32.size() >= static_cast<size_t>(tex.width * tex.height)) {
            srcW = tex.width;
            srcH = tex.height;
            srcData = tex.argb32.data();
        } else if (tex.mip1.size() >= static_cast<size_t>((tex.width / 2) * (tex.height / 2))) {
            srcW = tex.width / 2;
            srcH = tex.height / 2;
            srcData = tex.mip1.data();
        } else if (tex.argb32.size() >= static_cast<size_t>(tex.width * tex.height)) {
            srcW = tex.width;
            srcH = tex.height;
            srcData = tex.argb32.data();
        } else {
            continue;
        }

        reportedW = srcW;
        reportedH = srcH;

        int slotPitchX = srcW + 4;
        int slotPitchY = srcH + 4;
        int cols = std::max(1, (kAtlasDim - 4) / slotPitchX);
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;

        int slotX = 2 + col * slotPitchX;
        int slotY = 2 + row * slotPitchY;

        if (slotX + srcW > kAtlasDim || slotY + srcH > kAtlasDim) {
            std::cerr << "[SolEngine] Card texture atlas overflow at card " << i << "!\n";
            break;
        }

        // Copy source ARGB32 into atlas RGBA8
        for (int y = 0; y < srcH; ++y) {
            uint32_t* dstRow = &atlasRgba[static_cast<size_t>(slotY + y) * kAtlasDim + slotX];
            const uint32_t* srcRow = &srcData[static_cast<size_t>(y) * srcW];
            for (int x = 0; x < srcW; ++x) {
                uint32_t c = srcRow[x];
                uint32_t r = (c >> 16) & 0xFF;
                uint32_t g = (c >> 8) & 0xFF;
                uint32_t b = c & 0xFF;
                uint32_t a = (c >> 24) & 0xFF;
                dstRow[x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
        }

        atlasSlots_[i].u0 = (static_cast<float>(slotX) + 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].v0 = (static_cast<float>(slotY) + 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].u1 = (static_cast<float>(slotX + srcW) - 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].v1 = (static_cast<float>(slotY + srcH) - 0.5f) / static_cast<float>(kAtlasDim);
        atlasSlots_[i].width = srcW;
        atlasSlots_[i].height = srcH;
    }

    renderer.updateRgbaTexture(0, 0, kAtlasDim, kAtlasDim,
                               reinterpret_cast<const uint8_t*>(atlasRgba.data()),
                               kAtlasDim, kAtlasDim);
    hasGpuAtlas_ = true;
    atlasDirty_ = false;
    std::cout << "[SolEngine] Card texture GPU atlas built successfully (4096x4096 RGBA8, 53 cards, slot size "
              << reportedW << "x" << reportedH << ").\n";
    return true;
}

const CardAtlasSlot& CardTextureCache::getAtlasSlot(Suit s, Rank r) const noexcept {
    size_t idx = cardIndex(s, r);
    return atlasSlots_[idx];
}

const CardAtlasSlot& CardTextureCache::getBackAtlasSlot() const noexcept {
    return atlasSlots_[kCardBackIndex];
}

const CardAtlasSlot& CardTextureCache::getAtlasSlotByIndex(size_t index) const noexcept {
    return atlasSlots_[std::min(index, kTotalTextures - 1)];
}

} // namespace solengine
