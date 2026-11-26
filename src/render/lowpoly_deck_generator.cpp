#include "solengine/lowpoly_deck_generator.hpp"
#include "solengine/classic_deck_symbols.hpp"
#include "solengine/deterministic_rng.hpp"
#include "solengine/vector_binary.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

namespace solengine {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x{0.0f}, y{0.0f}, z{0.0f};
};

using Ramp4 = std::array<uint32_t, 4>;

struct Tri3 {
    Vec3 v0, v1, v2;
    Ramp4 ramp;
};

Vec3 vSub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 vCross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float vDot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 vNorm(Vec3 a) {
    const float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-6f) return {0.0f, 0.0f, 1.0f};
    return {a.x / len, a.y / len, a.z / len};
}

Vec3 rotX(Vec3 p, float deg) {
    const float r = deg * (kPi / 180.0f);
    const float c = std::cos(r), s = std::sin(r);
    return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
}

Vec3 rotY(Vec3 p, float deg) {
    const float r = deg * (kPi / 180.0f);
    const float c = std::cos(r), s = std::sin(r);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

Vec3 rotZ(Vec3 p, float deg) {
    const float r = deg * (kPi / 180.0f);
    const float c = std::cos(r), s = std::sin(r);
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}

float randRange(DeterministicRng& rng, float lo, float hi) noexcept {
    return rng.rangeFloat(lo, hi);
}

int randInt(DeterministicRng& rng, int lo, int hi) noexcept {
    return rng.rangeInt(lo, hi);
}

// ---------------------------------------------------------------------------
// Direct VectorImage Builders (Zero XML / Zero NanoSVG)
// ---------------------------------------------------------------------------
void addPolyShape(VectorImage& img, const std::vector<std::pair<float, float>>& pts, uint32_t argb) {
    if (pts.size() < 3) return;
    VectorShape shape{};
    shape.fillColor = argb;
    shape.flags = kVectorHasFill;
    shape.minX = shape.maxX = pts[0].first;
    shape.minY = shape.maxY = pts[0].second;

    VectorPath path{};
    path.closed = true;
    path.verbs.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const float x = pts[i].first;
        const float y = pts[i].second;
        shape.minX = std::min(shape.minX, x);
        shape.maxX = std::max(shape.maxX, x);
        shape.minY = std::min(shape.minY, y);
        shape.maxY = std::max(shape.maxY, y);
        VectorVerb v{};
        v.type = (i == 0) ? VectorVerbType::MoveTo : VectorVerbType::LineTo;
        v.x0 = x;
        v.y0 = y;
        path.verbs.push_back(v);
    }
    shape.paths.push_back(std::move(path));
    img.shapes.push_back(std::move(shape));
}

void addStrokeRectShape(VectorImage& img, float x, float y, float w, float h, uint32_t strokeArgb, float strokeW) {
    VectorShape shape{};
    shape.strokeColor = strokeArgb;
    shape.strokeWidth = strokeW;
    shape.flags = kVectorHasStroke;
    shape.minX = x;
    shape.minY = y;
    shape.maxX = x + w;
    shape.maxY = y + h;

    VectorPath path{};
    path.closed = true;
    path.verbs.push_back({VectorVerbType::MoveTo, x, y, 0, 0, 0, 0});
    path.verbs.push_back({VectorVerbType::LineTo, x + w, y, 0, 0, 0, 0});
    path.verbs.push_back({VectorVerbType::LineTo, x + w, y + h, 0, 0, 0, 0});
    path.verbs.push_back({VectorVerbType::LineTo, x, y + h, 0, 0, 0, 0});
    shape.paths.push_back(std::move(path));
    img.shapes.push_back(std::move(shape));
}

void addPolyEx(
    VectorImage& img,
    const std::vector<std::pair<float, float>>& pts,
    uint32_t fillArgb,
    uint32_t strokeArgb = 0,
    float strokeW = 0.0f
) {
    if (pts.size() < 3) return;
    VectorShape shape{};
    if (fillArgb != 0) {
        shape.fillColor = fillArgb;
        shape.flags |= kVectorHasFill;
    }
    if (strokeArgb != 0 && strokeW > 0.0f) {
        shape.strokeColor = strokeArgb;
        shape.strokeWidth = strokeW;
        shape.flags |= kVectorHasStroke;
    }
    if (shape.flags == 0) return;

    shape.minX = shape.maxX = pts[0].first;
    shape.minY = shape.maxY = pts[0].second;
    VectorPath path{};
    path.closed = true;
    path.verbs.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const float x = pts[i].first;
        const float y = pts[i].second;
        shape.minX = std::min(shape.minX, x);
        shape.maxX = std::max(shape.maxX, x);
        shape.minY = std::min(shape.minY, y);
        shape.maxY = std::max(shape.maxY, y);
        VectorVerb v{};
        v.type = (i == 0) ? VectorVerbType::MoveTo : VectorVerbType::LineTo;
        v.x0 = x;
        v.y0 = y;
        path.verbs.push_back(v);
    }
    shape.paths.push_back(std::move(path));
    img.shapes.push_back(std::move(shape));
}

void addLineShape(
    VectorImage& img,
    float x1,
    float y1,
    float x2,
    float y2,
    uint32_t strokeArgb,
    float strokeW
) {
    VectorShape shape{};
    shape.strokeColor = strokeArgb;
    shape.strokeWidth = strokeW;
    shape.flags = kVectorHasStroke;
    shape.minX = std::min(x1, x2);
    shape.maxX = std::max(x1, x2);
    shape.minY = std::min(y1, y2);
    shape.maxY = std::max(y1, y2);

    VectorPath path{};
    path.closed = false;
    path.verbs.push_back({VectorVerbType::MoveTo, x1, y1, 0, 0, 0, 0});
    path.verbs.push_back({VectorVerbType::LineTo, x2, y2, 0, 0, 0, 0});
    shape.paths.push_back(std::move(path));
    img.shapes.push_back(std::move(shape));
}

void addCircleApprox(
    VectorImage& img,
    float cx,
    float cy,
    float r,
    uint32_t fillArgb,
    uint32_t strokeArgb = 0,
    float strokeW = 0.0f,
    int segments = 20
) {
    std::vector<std::pair<float, float>> pts;
    pts.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
        pts.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    addPolyEx(img, pts, fillArgb, strokeArgb, strokeW);
}

void emitTris(
    VectorImage& img,
    const std::vector<Tri3>& tris,
    float cx,
    float cy,
    float scale = 1.0f,
    bool rotate180 = false,
    bool cullBackface = false
) {
    struct DrawTri {
        float depth;
        float x0, y0, x1, y1, x2, y2;
        uint32_t col;
    };
    const Vec3 lightDir = vNorm({-0.52f, -0.62f, 0.59f});
    std::vector<DrawTri> items;
    items.reserve(tris.size());

    const float s = rotate180 ? -scale : scale;
    for (const auto& t : tris) {
        Vec3 n = vCross(vSub(t.v1, t.v0), vSub(t.v2, t.v0));
        if (cullBackface && n.z <= 0.0f) continue;
        Vec3 nn = vNorm(n);
        if (nn.z < 0.0f) nn = {-nn.x, -nn.y, -nn.z};

        const float intensity = std::clamp(0.5f + 0.5f * vDot(nn, lightDir), 0.0f, 1.0f);
        const int idx = std::min(3, static_cast<int>(intensity * 4.0f));
        const uint32_t col = t.ramp[static_cast<size_t>(idx)];
        const float depth = (t.v0.z + t.v1.z + t.v2.z) / 3.0f;

        items.push_back({
            depth,
            cx + t.v0.x * s, cy + t.v0.y * s,
            cx + t.v1.x * s, cy + t.v1.y * s,
            cx + t.v2.x * s, cy + t.v2.y * s,
            col
        });
    }

    std::stable_sort(items.begin(), items.end(), [](const DrawTri& a, const DrawTri& b) {
        return a.depth < b.depth;
    });

    for (const auto& it : items) {
        addPolyShape(img, {{it.x0, it.y0}, {it.x1, it.y1}, {it.x2, it.y2}}, it.col);
    }
}

// ---------------------------------------------------------------------------
// Chamfered Low-Poly Rank Glyphs & 2-Tone Faceted Suit Pips (Another World style)
// ---------------------------------------------------------------------------
const char* rankGlyphPath(Rank rank) {
    switch (rank) {
        case Rank::Ace:   return "M3,24 L0,24 L0,10 L4,0 L12,0 L16,10 L16,24 L13,24 L13,15 L3,15 Z M3,12 L13,12 L13,9 L10.5,3 L5.5,3 L3,9 Z";
        case Rank::Two:   return "M0,3 L3,0 L13,0 L16,3 L16,10 L13,13 L3,19 L3,21 L16,21 L16,24 L0,24 L0,17 L13,10 L13,3 L3,3 L3,7 L0,7 Z";
        case Rank::Three: return "M0,3 L3,0 L13,0 L16,3 L16,10 L13,12 L16,14 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,14 L6,14 L6,10 L13,10 L13,3 L3,3 L3,6 L0,6 Z";
        case Rank::Four:  return "M10,0 L14,0 L14,15 L16,15 L16,18 L14,18 L14,24 L11,24 L11,18 L0,18 L0,14 L10,0 Z M11,15 L11,4 L3,15 Z";
        case Rank::Five:  return "M0,0 L16,0 L16,3 L3,3 L3,10 L13,10 L16,13 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,13 L0,13 Z";
        case Rank::Six:   return "M3,0 L13,0 L16,3 L16,6 L13,6 L13,3 L3,3 L3,10 L13,10 L16,13 L16,21 L13,24 L3,24 L0,21 L0,3 Z M3,13 L3,21 L13,21 L13,13 Z";
        case Rank::Seven: return "M0,0 L16,0 L16,4 L7,24 L3,24 L12,3 L0,3 Z";
        case Rank::Eight: return "M3,0 L13,0 L16,3 L16,10 L13,12 L16,14 L16,21 L13,24 L3,24 L0,21 L0,14 L3,12 L0,10 L0,3 Z M3,3 L3,10 L13,10 L13,3 Z M3,14 L3,21 L13,21 L13,14 Z";
        case Rank::Nine:  return "M3,0 L13,0 L16,3 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,14 L3,14 L0,11 L0,3 Z M3,3 L3,11 L13,11 L13,3 Z";
        case Rank::Ten:   return "M1,0 L4,0 L4,24 L1,24 Z M7,0 L13,0 L15,2 L15,22 L13,24 L7,24 L5,22 L5,2 Z M8,3 L8,21 L12,21 L12,3 Z";
        case Rank::Jack:  return "M12,0 L16,0 L16,21 L13,24 L3,24 L0,21 L0,16 L3,16 L3,21 L12,21 Z";
        case Rank::Queen: return "M3,0 L13,0 L16,3 L16,20 L14,22 L17,25 L14,26 L11,23 L3,23 L0,20 L0,3 Z M3,3 L3,20 L13,20 L13,3 Z";
        case Rank::King:  return "M0,0 L3,0 L3,10 L12,0 L16,0 L7,11 L16,24 L12,24 L4,13 L3,14 L3,24 L0,24 Z";
    }
    return "";
}

void addRankGlyphShape(
    VectorImage& img,
    Rank rank,
    float cx,
    float cy,
    float h,
    uint32_t argb,
    bool rotate180,
    uint32_t outlineArgb = 0,
    float outlineW = 0.0f
) {
    const char* p = rankGlyphPath(rank);
    if (!p || !*p) return;

    const float sc = h / 24.0f;
    const float tx = cx - 8.0f * sc;
    const float ty = cy - 12.0f * sc;

    VectorShape shape{};
    shape.fillColor = argb;
    shape.flags = kVectorHasFill | kVectorEvenOddFill;
    shape.minX = 1e9f; shape.minY = 1e9f;
    shape.maxX = -1e9f; shape.maxY = -1e9f;

    VectorPath curPath{};
    bool hasPath = false;

    auto mapPt = [&](float gx, float gy) -> std::pair<float, float> {
        float px = tx + gx * sc;
        float py = ty + gy * sc;
        if (rotate180) {
            px = 100.0f - px;
            py = 140.0f - py;
        }
        shape.minX = std::min(shape.minX, px);
        shape.maxX = std::max(shape.maxX, px);
        shape.minY = std::min(shape.minY, py);
        shape.maxY = std::max(shape.maxY, py);
        return {px, py};
    };

    while (*p) {
        while (*p == ' ' || *p == ',') ++p;
        if (!*p) break;
        char cmd = *p;
        if (cmd == 'M' || cmd == 'L') {
            ++p;
            char* end1 = nullptr;
            float gx = std::strtof(p, &end1);
            p = end1;
            while (*p == ' ' || *p == ',') ++p;
            char* end2 = nullptr;
            float gy = std::strtof(p, &end2);
            p = end2;
            auto [px, py] = mapPt(gx, gy);
            if (cmd == 'M') {
                if (hasPath && !curPath.verbs.empty()) {
                    shape.paths.push_back(std::move(curPath));
                    curPath = VectorPath{};
                }
                hasPath = true;
                curPath.closed = true;
                curPath.verbs.push_back({VectorVerbType::MoveTo, px, py, 0, 0, 0, 0});
            } else {
                curPath.verbs.push_back({VectorVerbType::LineTo, px, py, 0, 0, 0, 0});
            }
        } else if (cmd == 'Z') {
            ++p;
            if (hasPath && !curPath.verbs.empty()) {
                curPath.closed = true;
                shape.paths.push_back(std::move(curPath));
                curPath = VectorPath{};
                hasPath = false;
            }
        } else {
            ++p;
        }
    }
    if (hasPath && !curPath.verbs.empty()) {
        shape.paths.push_back(std::move(curPath));
    }
    if (!shape.paths.empty()) {
        if (outlineArgb != 0 && outlineW > 0.0f) {
            VectorShape outline = shape;
            outline.fillColor = 0;
            outline.strokeColor = outlineArgb;
            outline.strokeWidth = outlineW;
            outline.flags = static_cast<uint8_t>(kVectorHasStroke | (1u << 3) | (1u << 5));
            img.shapes.push_back(std::move(outline));
        }
        img.shapes.push_back(std::move(shape));
    }
}

void addCornerSuit(VectorImage& img, Suit suit, float cx, float cy, float size, bool darkBg, bool rotate180) {
    const float s = size * 0.5f;
    const bool isRed = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const uint32_t c1 = isRed ? (darkBg ? 0xFFE11D48u : 0xFF9E1B24u) : (darkBg ? 0xFF94A3B8u : 0xFF1A1733u);
    const uint32_t c2 = isRed ? (darkBg ? 0xFFFB7185u : 0xFFC8202Au) : (darkBg ? 0xFFE2E8F0u : 0xFF2E2952u);

    auto emitHalf = [&](std::initializer_list<std::pair<float, float>> normPts, uint32_t col) {
        std::vector<std::pair<float, float>> pts;
        pts.reserve(normPts.size());
        for (auto [nx, ny] : normPts) {
            float px = cx + nx * s;
            float py = cy + ny * s;
            if (rotate180) {
                px = 100.0f - px;
                py = 140.0f - py;
            }
            pts.emplace_back(px, py);
        }
        addPolyShape(img, pts, col);
    };

    if (suit == Suit::Hearts) {
        emitHalf({{0, 0.92f}, {-0.95f, -0.05f}, {-0.85f, -0.62f}, {-0.42f, -0.88f}, {0, -0.45f}}, c1);
        emitHalf({{0, 0.92f}, {0, -0.45f}, {0.42f, -0.88f}, {0.85f, -0.62f}, {0.95f, -0.05f}}, c2);
    } else if (suit == Suit::Diamonds) {
        emitHalf({{0, -1.0f}, {-0.76f, 0.0f}, {0, 1.0f}}, c1);
        emitHalf({{0, -1.0f}, {0.76f, 0.0f}, {0, 1.0f}}, c2);
    } else if (suit == Suit::Spades) {
        emitHalf({{0, -1.0f}, {-0.92f, 0.05f}, {-0.75f, 0.48f}, {-0.18f, 0.35f}, {-0.34f, 0.95f}, {0, 0.95f}}, c1);
        emitHalf({{0, -1.0f}, {0, 0.95f}, {0.34f, 0.95f}, {0.18f, 0.35f}, {0.75f, 0.48f}, {0.92f, 0.05f}}, c2);
    } else {
        emitHalf({{0, -0.98f}, {-0.42f, -0.72f}, {-0.36f, -0.25f}, {-0.86f, -0.15f}, {-0.96f, 0.25f}, {-0.58f, 0.52f}, {-0.14f, 0.35f}, {-0.30f, 0.95f}, {0, 0.95f}}, c1);
        emitHalf({{0, -0.98f}, {0, 0.95f}, {0.30f, 0.95f}, {0.14f, 0.35f}, {0.58f, 0.52f}, {0.96f, 0.25f}, {0.86f, -0.15f}, {0.36f, -0.25f}, {0.42f, -0.72f}}, c2);
    }
}

void addClassicSuitShape(
    VectorImage& img,
    Suit suit,
    float cx,
    float cy,
    float radius,
    float rotDeg,
    uint32_t fillArgb,
    uint32_t outlineArgb = 0,
    float outlineW = 0.0f
);

void addCornerIndices(VectorImage& img, Rank rank, Suit suit, const LowPolyDeckOptions& opts) {
    const bool darkBg = (opts.bg == LowPolyBg::Dark);
    const bool isRed = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const uint32_t col = isRed
        ? (darkBg ? 0xFFF2D8D5u : 0xFF9E1B24u)
        : (darkBg ? 0xFFE8DFD1u : 0xFF1A1733u);

    const bool isMobile = (opts.style == LowPolyStyle::Mobile);
    const float rx = isMobile ? 12.5f : 11.0f;
    const float ry = isMobile ? 15.0f : 14.0f;
    const float rSize = isMobile ? 13.2f : 10.5f;
    const float sy = isMobile ? 29.5f : 26.0f;
    const float sSize = isMobile ? 10.0f : 8.2f;

    if (isMobile) {
        const uint32_t whiteCol = 0xFFFFFFFFu;
        const uint32_t pipCol = isRed ? (darkBg ? 0xFFF2D8D5u : 0xFFCC1D24u) : (darkBg ? 0xFFE8DFD1u : 0xFF14161Eu);
        const uint32_t topCol = (suit == Suit::Hearts) ? whiteCol : pipCol;
        const uint32_t botCol = (suit == Suit::Clubs || suit == Suit::Spades) ? whiteCol : pipCol;

        addRankGlyphShape(img, rank, rx, ry, rSize, topCol, false);
        addClassicSuitShape(img, suit, rx, sy, sSize * 0.56f, 0.0f, topCol);
        if (!opts.clearBottomRight) {
            addRankGlyphShape(img, rank, rx, ry, rSize, botCol, true);
            addClassicSuitShape(img, suit, 100.0f - rx, 140.0f - sy, sSize * 0.56f, 180.0f, botCol);
        }
    } else {
        addRankGlyphShape(img, rank, rx, ry, rSize, col, false);
        addCornerSuit(img, suit, rx, sy, sSize, darkBg, false);
        if (!opts.clearBottomRight) {
            addRankGlyphShape(img, rank, rx, ry, rSize, col, true);
            addCornerSuit(img, suit, rx, sy, sSize, darkBg, true);
        }
    }
}

// ---------------------------------------------------------------------------
// Classic 3D Suit Gems (Aces + 2-10 Pips)
// ---------------------------------------------------------------------------
std::vector<Tri3> buildSuitTris(Suit suit, const Ramp4& ramp) {
    std::vector<Tri3> tris;
    auto sym = [&](Vec3 a, Vec3 b, Vec3 c) {
        tris.push_back({a, b, c, ramp});
        tris.push_back({{-a.x, a.y, a.z}, {-c.x, c.y, c.z}, {-b.x, b.y, b.z}, ramp});
    };

    if (suit == Suit::Diamonds) {
        const Vec3 top{0, -25, 1.5f}, bot{0, 25, 1.5f}, left{-18.5f, 0, 1.5f}, right{18.5f, 0, 1.5f};
        const Vec3 hub{0, 0, 9.0f}, inT{0, -10.5f, 7.5f}, inB{0, 10.5f, 7.5f}, inL{-7.5f, 0, 7.5f}, inR{7.5f, 0, 7.5f};
        tris.push_back({top, inT, left, ramp});
        tris.push_back({left, inT, inL, ramp});
        tris.push_back({top, right, inT, ramp});
        tris.push_back({inT, right, inR, ramp});
        tris.push_back({left, inL, bot, ramp});
        tris.push_back({inL, inB, bot, ramp});
        tris.push_back({inR, right, bot, ramp});
        tris.push_back({inB, inR, bot, ramp});
        tris.push_back({inT, hub, inL, ramp});
        tris.push_back({inT, inR, hub, ramp});
        tris.push_back({inL, hub, inB, ramp});
        tris.push_back({hub, inR, inB, ramp});
    } else if (suit == Suit::Hearts) {
        const Vec3 apex{0, -6, 8.5f}, cMid{0, 4, 7.0f}, bot{0, 24, 1.0f};
        const Vec3 lobeTop{11, -20, 2.0f}, lobeOut{22, -6, 2.0f}, cheek{16, 8, 2.5f};
        sym({0, -11, 5.0f}, lobeTop, apex);
        sym(lobeTop, lobeOut, apex);
        sym(apex, lobeOut, cMid);
        sym(lobeOut, cheek, cMid);
        sym(cMid, cheek, bot);
    } else if (suit == Suit::Spades) {
        const Vec3 tip{0, -26, 1.5f}, ridge{0, -6, 9.0f}, center{0, 6, 7.5f}, stemTop{0, 10, 5.0f}, stemBot{0, 24, 2.0f};
        const Vec3 wing{22, 4, 2.0f}, lobe{13, 14, 2.0f};
        sym(tip, wing, ridge);
        sym(ridge, wing, center);
        sym(center, wing, lobe);
        sym(center, lobe, stemTop);
        sym(stemTop, {8.5f, 24, 1.5f}, stemBot);
    } else {
        auto addLeaf = [&](float lx, float ly, float r) {
            const Vec3 hub{lx, ly, 7.0f};
            const Vec3 pts[5] = {
                {lx, ly - r, 2.0f},
                {lx + r * 0.9f, ly - r * 0.3f, 2.0f},
                {lx + r * 0.6f, ly + r * 0.8f, 2.0f},
                {lx - r * 0.6f, ly + r * 0.8f, 2.0f},
                {lx - r * 0.9f, ly - r * 0.3f, 2.0f},
            };
            for (int i = 0; i < 5; ++i) {
                tris.push_back({hub, pts[i], pts[(i + 1) % 5], ramp});
            }
        };
        addLeaf(0.0f, -14.0f, 11.5f);
        addLeaf(-13.0f, 4.0f, 10.5f);
        addLeaf(13.0f, 4.0f, 10.5f);
        tris.push_back({{-3.5f, 8.0f, 5.0f}, {3.5f, 8.0f, 5.0f}, {8.0f, 24.0f, 1.5f}, ramp});
        tris.push_back({{-3.5f, 8.0f, 5.0f}, {8.0f, 24.0f, 1.5f}, {-8.0f, 24.0f, 1.5f}, ramp});
        tris.push_back({{-3.5f, 8.0f, 5.0f}, {0.0f, 2.0f, 8.5f}, {3.5f, 8.0f, 5.0f}, ramp});
    }
    return tris;
}

void emitClassicCourt(VectorImage& img, Suit suit, Rank rank, DeterministicRng& rng, const LowPolyDeckOptions& opts) {
    const bool darkBg = (opts.bg == LowPolyBg::Dark);
    const Ramp4 skin   = {0xFFB9836Bu, 0xFFCF9982u, 0xFFE2B29Du, 0xFFF0CAB8u};
    const Ramp4 silver = {0xFF475569u, 0xFF64748Bu, 0xFF94A3B8u, 0xFFCBD5E1u};
    const Ramp4 gold   = {0xFF92400Eu, 0xFFB45309u, 0xFFD97706u, 0xFFF59E0Bu};

    Ramp4 robe, trim;
    if (suit == Suit::Hearts || suit == Suit::Diamonds) {
        robe = {0xFF7F1D1Du, 0xFF9E1B24u, 0xFFC8202Au, 0xFFE11D48u};
        trim = gold;
    } else if (suit == Suit::Spades) {
        robe = darkBg ? Ramp4{0xFF334155u, 0xFF475569u, 0xFF64748Bu, 0xFF94A3B8u}
                      : Ramp4{0xFF1E293Bu, 0xFF283548u, 0xFF334155u, 0xFF475569u};
        trim = silver;
    } else {
        robe = darkBg ? Ramp4{0xFF44403Cu, 0xFF57534Eu, 0xFF78716Cu, 0xFFA8A29Eu}
                      : Ramp4{0xFF262322u, 0xFF383432u, 0xFF4A4542u, 0xFF625B57u};
        trim = silver;
    }

    std::vector<Tri3> tris;
    auto sym = [&](Vec3 a, Vec3 b, Vec3 c, const Ramp4& mat) {
        tris.push_back({a, b, c, mat});
        tris.push_back({{-a.x, a.y, a.z}, {-c.x, c.y, c.z}, {-b.x, b.y, b.z}, mat});
    };

    const float shW = 26.0f + randRange(rng, -1.5f, 1.5f);
    sym({0, 12, 8}, {10, 8, 5}, {shW, 16, 2}, robe);
    sym({0, 12, 8}, {shW, 16, 2}, {0, 34, 4}, robe);

    const Ramp4& collarMat = (rank == Rank::King) ? gold : ((rank == Rank::Queen) ? silver : trim);
    sym({0, 10, 9}, {8.5f, 12, 7.5f}, {0, 20, 8.5f}, collarMat);

    const Vec3 forehead{0, -14, 8.5f}, cheek{10.5f, -2, 6}, chin{0, 10, 7.5f}, jaw{8, 5, 5};
    sym(forehead, cheek, {0, -2, 9.5f}, skin);
    sym({0, -2, 9.5f}, cheek, chin, skin);
    sym(cheek, jaw, chin, skin);

    const Vec3 hairTop{13, -14, 4}, hairMid{16.5f, 2, 3};
    sym(forehead, hairTop, cheek, robe);
    sym(cheek, hairTop, hairMid, robe);
    sym(cheek, hairMid, jaw, robe);

    if (rank == Rank::Jack) {
        sym({0, -25, 7}, {16.5f, -19, 5}, forehead, robe);
        sym(forehead, {16.5f, -19, 5}, hairTop, trim);
    } else if (rank == Rank::Queen) {
        sym(forehead, hairTop, {11, -21, 5}, silver);
        sym({0, -33, 6.5f}, {4.5f, -20, 6}, forehead, silver);
        sym({4.5f, -20, 6}, {11, -31, 4.5f}, {11, -21, 5}, silver);
        sym({11, -21, 5}, {17.5f, -27.5f, 3.5f}, hairTop, silver);
    } else {
        sym({0, -30, 7}, {5.5f, -21, 6.5f}, forehead, gold);
        sym({5.5f, -21, 6.5f}, {13, -29, 5}, hairTop, gold);
        sym(forehead, {5.5f, -21, 6.5f}, hairTop, gold);
        sym({0, 4, 9}, jaw, {0, 14, 7}, robe);
        sym(jaw, {12, 9, 4}, {0, 14, 7}, robe);
    }

    emitTris(img, tris, 50.0f, 72.0f, 1.02f);
}

// ---------------------------------------------------------------------------
// Scoundrel 3D Generators: Weapons (Diamonds), Potions (Hearts), Monsters (Spades/Clubs)
// ---------------------------------------------------------------------------
void emitScoundrelWeapon(VectorImage& img, int rnum, DeterministicRng& rng) {
    static const std::array<std::array<Ramp4, 4>, 3> kPalettes = {{
        {{ {0xFF283B5Bu, 0xFF39567Du, 0xFF4F74A3u, 0xFF6E96C7u}, {0xFF0284C7u, 0xFF0EA5E9u, 0xFF38BDF8u, 0xFF7DD3FCu}, {0xFFB45309u, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFDE047u}, {0xFF1E293Bu, 0xFF334155u, 0xFF475569u, 0xFF64748Bu} }},
        {{ {0xFF1F242Du, 0xFF2E3542u, 0xFF404959u, 0xFF5B667Au}, {0xFF991B1Bu, 0xFFDC2626u, 0xFFEF4444u, 0xFFFCA5A5u}, {0xFF92400Eu, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFEF08Au}, {0xFF18181Bu, 0xFF27272Au, 0xFF3F3F46u, 0xFF52525Bu} }},
        {{ {0xFF1E3A34u, 0xFF285249u, 0xFF346E61u, 0xFF499180u}, {0xFF059669u, 0xFF10B981u, 0xFF34D399u, 0xFFA7F3D0u}, {0xFF78350Fu, 0xFFB45309u, 0xFFD97706u, 0xFFFBBF24u}, {0xFF292524u, 0xFF44403Cu, 0xFF57534Eu, 0xFF78716Cu} }},
    }};
    const auto& pal = kPalettes[static_cast<size_t>(randInt(rng, 0, 2))];
    const auto& blade = pal[0];
    const auto& tip = pal[1];
    const auto& guard = pal[2];
    const auto& grip = pal[3];

    const float tier = static_cast<float>(rnum - 2) / 12.0f;
    const float bladeLen = 24.0f + tier * 16.0f + randRange(rng, -2.0f, 2.0f);
    const float bladeW = 4.2f + tier * 2.6f + randRange(rng, -0.5f, 0.6f);
    const float ridgeZ = 3.2f + randRange(rng, -0.4f, 0.8f);
    const float tipLen = 7.5f + tier * 4.5f + randRange(rng, -1.0f, 1.5f);
    const float guardW = 10.0f + tier * 5.0f + randRange(rng, -1.5f, 1.5f);
    const float gripLen = 10.0f + randRange(rng, -1.0f, 2.0f);
    const float angle = -42.0f + randRange(rng, -6.0f, 6.0f);

    std::vector<Tri3> tris;
    auto addQuadPrism = [&](float y0, float y1, float w0, float w1, float z0, float z1, const Ramp4& mat) {
        const Vec3 tl{-w0, y0, 0}, tr{w0, y0, 0}, tc{0, y0, z0};
        const Vec3 bl{-w1, y1, 0}, br{w1, y1, 0}, bc{0, y1, z1};
        tris.push_back({tl, tc, bc, mat});
        tris.push_back({tl, bc, bl, mat});
        tris.push_back({tc, tr, br, mat});
        tris.push_back({tc, br, bc, mat});
    };

    const float yGuard = 10.0f;
    const float yShoulder = yGuard - bladeLen;
    const float yTip = yShoulder - tipLen;
    const float yMid = (yGuard + yShoulder) * 0.5f;
    const float wMid = bladeW * randRange(rng, 0.92f, 1.08f);

    addQuadPrism(yShoulder, yMid, bladeW * 0.85f, wMid, ridgeZ * 0.9f, ridgeZ, blade);
    addQuadPrism(yMid, yGuard, wMid, bladeW, ridgeZ, ridgeZ * 0.85f, blade);

    const Vec3 tipPt{0, yTip, 0.5f};
    const Vec3 sl{-bladeW * 0.85f, yShoulder, 0}, sr{bladeW * 0.85f, yShoulder, 0}, sc{0, yShoulder, ridgeZ * 0.9f};
    tris.push_back({sl, tipPt, sc, tip});
    tris.push_back({sc, tipPt, sr, tip});

    const Vec3 gCT{0, yGuard - 3.2f, 3.5f}, gCB{0, yGuard + 3.2f, 3.5f};
    const float guardTilt = randRange(rng, -2.0f, 2.0f);
    const Vec3 gL{-guardW, yGuard + guardTilt, 0}, gR{guardW, yGuard + guardTilt, 0};
    tris.push_back({gL, gCT, gCB, guard});
    tris.push_back({gCT, gR, gCB, guard});

    const float yPom = yGuard + 3.2f + gripLen;
    addQuadPrism(yGuard + 2.5f, yPom, 2.0f, 1.8f, 2.2f, 2.0f, grip);
    const auto& pomMat = (rnum >= 8) ? tip : guard;
    tris.push_back({{-4.2f, yPom + 2.5f, 0}, {0, yPom - 1.0f, 2.8f}, {0, yPom + 6.0f, 0.5f}, pomMat});
    tris.push_back({{0, yPom - 1.0f, 2.8f}, {4.2f, yPom + 2.5f, 0}, {0, yPom + 6.0f, 0.5f}, pomMat});

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, angle);
        t.v1 = rotZ(t.v1, angle);
        t.v2 = rotZ(t.v2, angle);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

void emitScoundrelPotion(VectorImage& img, int rnum, DeterministicRng& rng) {
    static const std::array<std::array<Ramp4, 3>, 3> kPalettes = {{
        {{ {0xFF881337u, 0xFFBE123Cu, 0xFFE11D48u, 0xFFFB7185u}, {0xFF9F1239u, 0xFFE11D48u, 0xFFFB7185u, 0xFFFDA4AFu}, {0xFF78350Fu, 0xFF92400Eu, 0xFFB45309u, 0xFFD97706u} }},
        {{ {0xFF701A75u, 0xFFA21CAFu, 0xFFD946EFu, 0xFFF0ABFCu}, {0xFF86198Fu, 0xFFC026D3u, 0xFFE879F9u, 0xFFF5D0FEu}, {0xFF78350Fu, 0xFF92400Eu, 0xFFB45309u, 0xFFD97706u} }},
        {{ {0xFF9A3412u, 0xFFC2410Cu, 0xFFEA580Cu, 0xFFFB923Cu}, {0xFFB45309u, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFCD34Du}, {0xFF57534Eu, 0xFF78716Cu, 0xFFA8A29Eu, 0xFFD6D3D1u} }},
    }};
    const auto& pal = kPalettes[static_cast<size_t>(randInt(rng, 0, 2))];
    const float tier = static_cast<float>(rnum - 2) / 12.0f;
    const int sides = (rnum >= 6) ? randInt(rng, 5, 8) : randInt(rng, 4, 6);
    const float bellyR = 15.0f + tier * 5.0f + randRange(rng, -1.5f, 1.5f);

    struct RingSpec { float y, r; int matIdx; };
    const std::array<RingSpec, 9> profile = {{
        {-28.0f, 5.2f, 2},
        {-21.0f, 4.6f, 2},
        {-21.0f, 6.8f, 1},
        {-17.5f, 6.8f, 1},
        {-17.5f, 4.5f, 0},
        {-9.0f, 5.0f, 0},
        {1.0f + randRange(rng, -2.0f, 2.0f), bellyR, 0},
        {14.0f + randRange(rng, -2.0f, 2.0f), bellyR * randRange(rng, 0.65f, 0.88f), 0},
        {26.0f + tier * 3.0f, 0.5f, 0},
    }};

    const float phase = randRange(rng, 0.0f, kPi / static_cast<float>(sides));
    const float tiltX = randRange(rng, 10.0f, 18.0f);
    const float tiltY = randRange(rng, -14.0f, 14.0f);

    std::vector<std::vector<Vec3>> rings(profile.size());
    for (size_t ri = 0; ri < profile.size(); ++ri) {
        rings[ri].reserve(static_cast<size_t>(sides));
        for (int i = 0; i < sides; ++i) {
            const float ang = phase + (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(sides);
            Vec3 p{std::cos(ang) * profile[ri].r, profile[ri].y, std::sin(ang) * profile[ri].r};
            rings[ri].push_back(rotY(rotX(p, tiltX), tiltY));
        }
    }

    std::vector<Tri3> tris;
    for (size_t ri = 0; ri + 1 < profile.size(); ++ri) {
        const auto& mat = pal[static_cast<size_t>(profile[ri].matIdx)];
        for (int i = 0; i < sides; ++i) {
            const int j = (i + 1) % sides;
            tris.push_back({rings[ri][i], rings[ri][j], rings[ri + 1][j], mat});
            tris.push_back({rings[ri][i], rings[ri + 1][j], rings[ri + 1][i], mat});
        }
    }
    emitTris(img, tris, 50.0f, 74.0f, 1.0f, false, true);
}

void emitScoundrelMonster(VectorImage& img, Suit suit, int rnum, DeterministicRng& rng) {
    static const std::array<std::array<Ramp4, 3>, 2> kSpadePalettes = {{
        {{ {0xFF57534Eu, 0xFF78716Cu, 0xFFA8A29Eu, 0xFFE7E5E4u}, {0xFF334155u, 0xFF475569u, 0xFF64748Bu, 0xFF94A3B8u}, {0xFF11141Cu, 0xFF11141Cu, 0xFF181C26u, 0xFF181C26u} }},
        {{ {0xFF475569u, 0xFF64748Bu, 0xFF94A3B8u, 0xFFCBD5E1u}, {0xFF3B0764u, 0xFF581C87u, 0xFF7E22CEu, 0xFFA855F7u}, {0xFF0F1219u, 0xFF0F1219u, 0xFF171C28u, 0xFF171C28u} }},
    }};
    static const std::array<std::array<Ramp4, 3>, 2> kClubPalettes = {{
        {{ {0xFF6B685Eu, 0xFF948F82u, 0xFFBFB8A8u, 0xFFE6DFD1u}, {0xFF4A4842u, 0xFF6E6B62u, 0xFF918C7Fu, 0xFFB5AFA0u}, {0xFF16181Fu, 0xFF16181Fu, 0xFF1C1F28u, 0xFF1C1F28u} }},
        {{ {0xFF57534Eu, 0xFF78716Cu, 0xFFA8A29Eu, 0xFFD6D3D1u}, {0xFF7C2D12u, 0xFFB45309u, 0xFFD97706u, 0xFFF59E0Bu}, {0xFF161312u, 0xFF161312u, 0xFF1F1B19u, 0xFF1F1B19u} }},
    }};
    const auto& pal = (suit == Suit::Spades)
        ? kSpadePalettes[static_cast<size_t>(randInt(rng, 0, 1))]
        : kClubPalettes[static_cast<size_t>(randInt(rng, 0, 1))];
    const auto& bone = pal[0];
    const auto& horn = pal[1];
    const auto& voidMat = pal[2];

    const float tier = static_cast<float>(rnum - 2) / 12.0f;
    const float wBrow = 14.0f + tier * 3.0f + randRange(rng, -1.5f, 1.5f);
    const float wCheek = 12.0f + randRange(rng, -1.5f, 1.5f);
    const float wJaw = 7.5f + randRange(rng, -1.2f, 1.2f);
    const float hTop = -18.0f - tier * 2.0f;
    const float hBrow = -6.0f + randRange(rng, -1.5f, 1.5f);
    const float hCheek = 6.0f + randRange(rng, -1.0f, 1.5f);
    const float hJaw = 19.0f + tier * 2.5f;

    std::vector<Tri3> tris;
    auto sym = [&](Vec3 a, Vec3 b, Vec3 c, const Ramp4& mat) {
        tris.push_back({a, b, c, mat});
        tris.push_back({{-a.x, a.y, a.z}, {-c.x, c.y, c.z}, {-b.x, b.y, b.z}, mat});
    };

    const Vec3 crownC{0, hTop, 4}, browC{0, hBrow, 9}, noseTop{0, 2, 8.5f}, noseBot{0, 11, 6.5f}, chinC{0, hJaw, 5.5f};
    const Vec3 temple{wBrow * 0.85f, hTop + 6, 2}, browOut{wBrow, hBrow, 4.5f}, cheekOut{wCheek, hCheek, 3};
    const Vec3 jawOut{wJaw, hJaw - 3, 2}, fangTip{wJaw * 0.72f, hJaw + 5.5f, 1.5f};

    sym(crownC, temple, browC, bone);
    sym(temple, browOut, browC, bone);
    sym(browC, browOut, noseTop, bone);
    sym(browOut, cheekOut, noseTop, bone);
    sym(noseTop, cheekOut, noseBot, bone);
    sym(cheekOut, jawOut, noseBot, bone);
    sym(noseBot, jawOut, chinC, bone);
    sym(jawOut, {wJaw * 0.48f, hJaw, 3.0f}, fangTip, bone);

    sym({3.2f, hBrow + 4.0f, 9.2f}, {wBrow * 0.72f, hBrow + 2.5f, 6.2f}, {wCheek * 0.56f, hCheek - 1.5f, 6.2f}, voidMat);
    sym(noseTop, {2.8f, 9.5f, 8.8f}, noseBot, voidMat);

    const int hornStyle = (rnum >= 6) ? randInt(rng, 0, 2) : 3;
    if (hornStyle == 3) {
        sym({wBrow * 0.78f, hTop + 6, 2}, {wBrow + 9, hTop - 4, 0}, {wBrow * 0.95f, hTop + 10, 1}, horn);
    } else if (hornStyle == 2 && rnum >= 10) {
        for (int sIdx = 0; sIdx < 2; ++sIdx) {
            const float sx = (sIdx == 0) ? 4.5f : 11.5f;
            sym({sx - 2.5f, hTop + 2, 3}, {sx + 1.5f + sIdx * 3.0f, hTop - 12 + sIdx * 2.0f, 1}, {sx + 2.5f, hTop + 3, 2}, horn);
        }
    } else {
        const Vec3 hb0{wBrow * 0.76f, hTop + 7, 2.5f}, hb1{wBrow * 0.95f, hBrow - 1, 2.0f};
        const Vec3 hm0{wBrow + 9.5f, hTop + 2, 2.5f}, hm1{wBrow + 11.5f, hBrow - 5, 1.5f};
        const Vec3 htip{wBrow + 6.0f + tier * 3.0f, hTop - 13.0f - tier * 3.0f, 1.0f};
        sym(hb0, hm0, hb1, horn);
        sym(hb1, hm0, hm1, horn);
        sym(hm0, htip, hm1, horn);
    }
    emitTris(img, tris, 50.0f, 73.0f);
}

struct ClockworkPalette {
    uint32_t dark;
    uint32_t primary;
    uint32_t mid;
    uint32_t accent;
    uint32_t accentHi;
    uint32_t accentLo;
    uint32_t ink;
    uint32_t skinHi;
    uint32_t skinLo;
    uint32_t ivory;
};

ClockworkPalette getClockworkPalette(Suit suit) {
    switch (suit) {
        case Suit::Spades:
            return {0xFF18181Bu, 0xFF27272Au, 0xFF3F3F46u, 0xFFD49B35u, 0xFFF3C969u, 0xFF9A6B1Au, 0xFF09090Bu, 0xFFF4DCCBu, 0xFFDFB8A0u, 0xFFF5EFE6u};
        case Suit::Hearts:
            return {0xFF450A0Au, 0xFF7F1D1Du, 0xFF991B1Bu, 0xFFD98A36u, 0xFFF4C062u, 0xFF9C521Bu, 0xFF240606u, 0xFFF6DFD0u, 0xFFE2BBA3u, 0xFFF7F0E8u};
        case Suit::Clubs:
            return {0xFF52525Bu, 0xFF71717Au, 0xFFA1A1AAu, 0xFFC88D32u, 0xFFEAB854u, 0xFF8C5C19u, 0xFF18181Bu, 0xFFF2DAC9u, 0xFFDDB59Du, 0xFFF3EFE6u};
        case Suit::Diamonds:
            return {0xFF6B1229u, 0xFFC81E3Au, 0xFFE84558u, 0xFFD99126u, 0xFFFBCF58u, 0xFF9A5D12u, 0xFF291016u, 0xFFF6DFD0u, 0xFFE2BBA3u, 0xFFF8F1E7u};
    }
    return {0xFF18181Bu, 0xFF27272Au, 0xFF3F3F46u, 0xFFD49B35u, 0xFFF3C969u, 0xFF9A6B1Au, 0xFF09090Bu, 0xFFF4DCCBu, 0xFFDFB8A0u, 0xFFF5EFE6u};
}

std::vector<std::pair<float, float>> makeGearPts(
    float cx,
    float cy,
    float rOuter,
    float rInner,
    int teeth,
    float phaseDeg
) {
    std::vector<std::pair<float, float>> pts;
    pts.reserve(static_cast<size_t>(teeth * 4));
    const float step = 360.0f / static_cast<float>(teeth);
    const float deg2rad = kPi / 180.0f;
    for (int i = 0; i < teeth; ++i) {
        const float base = phaseDeg + static_cast<float>(i) * step;
        const float angles[4] = {
            base * deg2rad,
            (base + step * 0.22f) * deg2rad,
            (base + step * 0.50f) * deg2rad,
            (base + step * 0.72f) * deg2rad,
        };
        const float radii[4] = {rInner, rOuter, rOuter, rInner};
        for (int k = 0; k < 4; ++k) {
            pts.emplace_back(cx + radii[k] * std::cos(angles[k]), cy + radii[k] * std::sin(angles[k]));
        }
    }
    return pts;
}

void emitClockworkCourt(VectorImage& img, Suit suit, Rank rank, DeterministicRng& rng, const LowPolyDeckOptions& /*opts*/) {
    const ClockworkPalette p = getClockworkPalette(suit);

    // Map 252x388 court space into 100x140 card space centered at (50, 70)
    constexpr float kScale = 0.295f;
    auto mapPt = [&](float x, float y, bool flip) -> std::pair<float, float> {
        if (flip) {
            x = 252.0f - x;
            y = 388.0f - y;
        }
        return {50.0f + (x - 126.0f) * kScale, 70.0f + (y - 194.0f) * kScale};
    };

    auto poly = [&](const std::vector<std::pair<float, float>>& pts, bool flip, uint32_t fill, uint32_t stroke = 0, float sw = 0.0f) {
        std::vector<std::pair<float, float>> out;
        out.reserve(pts.size());
        for (const auto& pt : pts) out.push_back(mapPt(pt.first, pt.second, flip));
        addPolyEx(img, out, fill, stroke, sw * kScale);
    };
    auto line = [&](float x1, float y1, float x2, float y2, bool flip, uint32_t stroke, float sw) {
        const auto a = mapPt(x1, y1, flip);
        const auto b = mapPt(x2, y2, flip);
        addLineShape(img, a.first, a.second, b.first, b.second, stroke, std::max(0.25f, sw * kScale));
    };
    auto circle = [&](float cx, float cy, float r, bool flip, uint32_t fill, uint32_t stroke = 0, float sw = 0.0f) {
        const auto c = mapPt(cx, cy, flip);
        addCircleApprox(img, c.first, c.second, r * kScale, fill, stroke, sw * kScale);
    };

    // Seeded mechanical parameters (preserving crisp symmetry & linework)
    static constexpr int kToothChoices[3] = {6, 8, 10};
    const int hubTeeth = kToothChoices[randInt(rng, 0, 2)];
    const float hubPhase = (randInt(rng, 0, 1) == 0) ? 0.0f : (180.0f / static_cast<float>(hubTeeth));
    const float facetShift = randRange(rng, -5.0f, 5.0f);
    const int monocleDiag = randInt(rng, 0, 1);
    const int roseTeeth = kToothChoices[randInt(rng, 0, 2)];
    const float hourAng = randRange(rng, 0.0f, 2.0f * kPi);
    const float minAng = hourAng + randRange(rng, 0.9f, 2.4f);

    // Central horizon belt
    line(24.0f, 194.0f, 228.0f, 194.0f, false, p.ink, 2.0f);
    line(34.0f, 194.0f, 218.0f, 194.0f, false, p.accent, 1.0f);

    // Emit top half then 180-deg mirrored bottom half
    for (int pass = 0; pass < 2; ++pass) {
        const bool flip = (pass == 1);
        if (rank == Rank::Jack) {
            poly({{46, 194}, {56, 134}, {98, 112}, {112 + facetShift, 194}}, flip, p.primary);
            poly({{98, 112}, {154, 112}, {138 + facetShift, 194}, {112 + facetShift, 194}}, flip, p.dark);
            poly({{154, 112}, {194, 132}, {204, 194}, {138 + facetShift, 194}}, flip, p.mid);

            poly({{72, 124}, {92, 114}, {176, 194}, {148, 194}}, flip, p.accent);
            poly({{92, 114}, {104, 116}, {176, 194}, {162, 194}}, flip, p.accentHi);
            circle(116, 146, 3.2f, flip, p.dark, p.ivory, 1.0f);
            circle(136, 166, 3.2f, flip, p.dark, p.ivory, 1.0f);

            poly({{96, 108}, {126, 98}, {156, 108}, {126, 124}}, flip, p.ivory);
            poly({{114, 92}, {138, 92}, {134, 110}, {118, 110}}, flip, p.skinLo);
            poly({{106, 52}, {130, 52}, {126, 96}, {108, 86}}, flip, p.skinHi);
            poly({{130, 52}, {148, 58}, {144, 86}, {126, 96}}, flip, p.skinLo);

            poly({{98, 54}, {110, 30}, {148, 34}, {152, 54}}, flip, p.dark);
            poly({{96, 54}, {154, 50}, {150, 58}, {100, 60}}, flip, p.accent);

            circle(134, 68, 8.5f, flip, p.ivory, p.accentHi, 2.0f);
            circle(134, 68, 4.5f, flip, p.primary, p.ink, 1.0f);
            if (monocleDiag) {
                line(128, 62, 140, 74, flip, p.ink, 0.9f);
                line(128, 74, 140, 62, flip, p.ink, 0.9f);
            } else {
                line(126, 68, 142, 68, flip, p.ink, 0.9f);
                line(134, 60, 134, 76, flip, p.ink, 0.9f);
            }

            line(202, 50, 202, 194, flip, p.accentLo, 3.5f);
            line(202, 50, 202, 194, flip, p.accentHi, 1.4f);
            poly({{202, 34}, {218, 52}, {202, 74}, {190, 56}}, flip, p.mid);
            circle(202, 54, 5.0f, flip, p.accentHi, p.ink, 1.2f);
        } else if (rank == Rank::Queen) {
            poly({{62, 194}, {74, 140}, {106, 126}, {114 + facetShift * 0.5f, 194}}, flip, p.primary);
            poly({{190, 194}, {178, 140}, {146, 126}, {138 + facetShift * 0.5f, 194}}, flip, p.primary);
            poly({{106, 126}, {126, 126}, {126, 194}, {114 + facetShift * 0.5f, 194}}, flip, p.dark);
            poly({{126, 126}, {146, 126}, {138 + facetShift * 0.5f, 194}, {126, 194}}, flip, p.mid);

            poly({{92, 126}, {106, 112}, {146, 112}, {160, 126}, {126, 140}}, flip, p.ivory);
            line(109, 114, 119, 134, flip, p.accentLo, 1.1f);
            line(126, 113, 126, 138, flip, p.accentLo, 1.1f);
            line(143, 114, 133, 134, flip, p.accentLo, 1.1f);

            poly({{126, 148}, {136, 162}, {126, 176}, {116, 162}}, flip, p.accentHi);
            circle(126, 162, 3.8f, flip, p.primary, p.ink, 1.0f);

            poly({{98, 114}, {105, 70}, {117, 70}, {110, 114}}, flip, p.dark);
            poly({{154, 114}, {147, 70}, {135, 70}, {142, 114}}, flip, p.dark);
            poly({{109, 70}, {126, 70}, {126, 110}, {112, 99}}, flip, p.skinHi);
            poly({{143, 70}, {126, 70}, {126, 110}, {140, 99}}, flip, p.skinLo);

            poly({{102, 70}, {99, 51}, {114, 61}, {126, 44}, {138, 61}, {153, 51}, {150, 70}}, flip, p.accentHi);
            poly({{104, 70}, {148, 70}, {145, 63}, {107, 63}}, flip, p.accent);
            circle(126, 58, 3.5f, flip, p.primary, p.ink, 1.0f);

            line(182, 114, 162, 194, flip, p.accentHi, 2.2f);
            poly(makeGearPts(185, 104, 14, 9.5f, roseTeeth, 0.0f), flip, p.primary);
            circle(185, 104, 5.5f, flip, p.accentHi, p.ink, 1.1f);
        } else {
            poly({{40, 194}, {48, 122}, {92, 108}, {106 + facetShift * 0.6f, 194}}, flip, p.primary);
            poly({{212, 194}, {204, 122}, {160, 108}, {146 + facetShift * 0.6f, 194}}, flip, p.mid);
            poly({{92, 108}, {160, 108}, {146 + facetShift * 0.6f, 194}, {106 + facetShift * 0.6f, 194}}, flip, p.dark);

            poly({{48, 122}, {92, 108}, {86, 126}, {52, 138}}, flip, p.accent);
            poly({{204, 122}, {160, 108}, {166, 126}, {200, 138}}, flip, p.accentHi);

            circle(126, 152, 18.0f, flip, p.ivory, p.accentHi, 2.2f);
            circle(126, 152, 14.0f, flip, 0, p.ink, 1.1f);
            line(126, 152, 126 + 11.0f * std::cos(hourAng), 152 + 11.0f * std::sin(hourAng), flip, p.ink, 1.8f);
            line(126, 152, 126 + 9.0f * std::cos(minAng), 152 + 9.0f * std::sin(minAng), flip, p.primary, 1.8f);
            circle(126, 152, 2.5f, flip, p.accentLo, p.ink, 1.0f);

            poly({{104, 52}, {126, 52}, {126, 86}, {106, 82}}, flip, p.skinHi);
            poly({{148, 52}, {126, 52}, {126, 86}, {146, 82}}, flip, p.skinLo);
            poly({{102, 78}, {126, 84}, {126, 114}, {106, 102}}, flip, p.dark);
            poly({{150, 78}, {126, 84}, {126, 114}, {146, 102}}, flip, p.primary);
            line(108, 64, 122, 66, flip, p.ink, 2.0f);
            line(144, 64, 130, 66, flip, p.ink, 2.0f);

            poly({{94, 52}, {92, 28}, {104, 28}, {108, 40}, {118, 24}, {134, 24}, {144, 40}, {148, 28}, {160, 28}, {158, 52}}, flip, p.accentHi);
            poly({{95, 44}, {157, 44}, {157, 52}, {95, 52}}, flip, p.accent);
            poly({{126, 28}, {132, 36}, {126, 44}, {120, 36}}, flip, p.primary);

            poly({{204, 30}, {210, 44}, {210, 194}, {198, 194}, {198, 44}}, flip, p.ivory, p.ink, 1.2f);
            line(204, 34, 204, 194, flip, p.mid, 1.3f);
        }
    }

    // Central interlocking clockwork medallion at (126, 194)
    poly(makeGearPts(126, 194, 15, 11, hubTeeth, hubPhase), false, p.accent);
    circle(126, 194, 7.5f, false, p.dark, p.accentHi, 1.2f);
    poly({{126, 188}, {131, 194}, {126, 200}, {121, 194}}, false, p.ivory);
}

void addClassicSuitShape(
    VectorImage& img,
    Suit suit,
    float cx,
    float cy,
    float radius,
    float rotDeg,
    uint32_t fillArgb,
    uint32_t outlineArgb,
    float outlineW
) {
    std::string_view d;
    switch (suit) {
        case Suit::Spades:   d = kClassicSuitPathSpades; break;
        case Suit::Hearts:   d = kClassicSuitPathHearts; break;
        case Suit::Clubs:    d = kClassicSuitPathClubs; break;
        case Suit::Diamonds: d = kClassicSuitPathDiamonds; break;
    }

    const float rad = rotDeg * (kPi / 180.0f);
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);
    auto xform = [&](float x, float y) -> std::pair<float, float> {
        return {
            cx + (x * cs - y * sn) * radius,
            cy + (x * sn + y * cs) * radius
        };
    };

    VectorShape shape{};
    shape.fillColor = fillArgb;
    shape.flags = kVectorHasFill | kVectorEvenOddFill;
    shape.minX = 1e9f; shape.minY = 1e9f;
    shape.maxX = -1e9f; shape.maxY = -1e9f;

    auto expand = [&](float px, float py) {
        shape.minX = std::min(shape.minX, px);
        shape.maxX = std::max(shape.maxX, px);
        shape.minY = std::min(shape.minY, py);
        shape.maxY = std::max(shape.maxY, py);
    };

    const char* p = d.data();
    const char* end = p + d.size();
    auto skipSep = [&]() {
        while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    };
    auto readFloat = [&](float& out) -> bool {
        skipSep();
        if (p >= end) return false;
        char* next = nullptr;
        out = std::strtof(p, &next);
        if (next == p) return false;
        p = next;
        return true;
    };

    VectorPath curPath{};
    bool hasPath = false;
    char cmd = 0;

    while (p < end) {
        skipSep();
        if (p >= end) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            cmd = *p++;
            if (cmd == 'Z' || cmd == 'z') {
                if (hasPath && !curPath.verbs.empty()) {
                    curPath.closed = true;
                    shape.paths.push_back(std::move(curPath));
                    curPath = VectorPath{};
                    hasPath = false;
                }
                continue;
            }
        }
        if (cmd == 'M') {
            float x, y;
            if (!readFloat(x) || !readFloat(y)) break;
            if (hasPath && !curPath.verbs.empty()) {
                shape.paths.push_back(std::move(curPath));
                curPath = VectorPath{};
            }
            const auto pt = xform(x, y);
            expand(pt.first, pt.second);
            curPath.verbs.push_back({VectorVerbType::MoveTo, pt.first, pt.second, 0, 0, 0, 0});
            hasPath = true;
            cmd = 'L';
        } else if (cmd == 'L') {
            float x, y;
            if (!readFloat(x) || !readFloat(y)) break;
            const auto pt = xform(x, y);
            expand(pt.first, pt.second);
            curPath.verbs.push_back({VectorVerbType::LineTo, pt.first, pt.second, 0, 0, 0, 0});
        } else if (cmd == 'C') {
            float x1, y1, x2, y2, x, y;
            if (!readFloat(x1) || !readFloat(y1) || !readFloat(x2) || !readFloat(y2) || !readFloat(x) || !readFloat(y)) break;
            const auto p1 = xform(x1, y1);
            const auto p2 = xform(x2, y2);
            const auto p3 = xform(x, y);
            expand(p1.first, p1.second);
            expand(p2.first, p2.second);
            expand(p3.first, p3.second);
            curPath.verbs.push_back({VectorVerbType::CubicTo, p1.first, p1.second, p2.first, p2.second, p3.first, p3.second});
        } else {
            ++p;
        }
    }
    if (hasPath && !curPath.verbs.empty()) {
        shape.paths.push_back(std::move(curPath));
    }
    if (!shape.paths.empty()) {
        if (outlineArgb != 0 && outlineW > 0.0f) {
            VectorShape outline = shape;
            outline.fillColor = 0;
            outline.strokeColor = outlineArgb;
            outline.strokeWidth = outlineW;
            outline.flags = static_cast<uint8_t>(kVectorHasStroke | (1u << 3) | (1u << 5));
            img.shapes.push_back(std::move(outline));
        }
        img.shapes.push_back(std::move(shape));
    }
}

void emitMobileSuitBg(VectorImage& img, Suit suit, Rank rank, DeterministicRng& rng, LowPolyBg /*bg*/) {
    const bool isAce = (rank == Rank::Ace);
    const bool isRed = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const uint32_t suitCol = isRed ? 0xFFCC1D24u : 0xFF14161Eu;

    float tiltDeg = 0.0f;
    if (!isAce) {
        tiltDeg = randRange(rng, 5.0f, 12.0f);
        if (randInt(rng, 0, 1) == 0) tiltDeg = -tiltDeg;
    }

    float bgCx = 50.0f;
    float bgCy = 70.0f;
    float bgRadius = isAce ? 58.0f : (72.0f + randRange(rng, -2.5f, 2.5f));
    if (suit == Suit::Hearts) {
        // Position Hearts toward top-left so top-left index sits cleanly inside the heart lobe
        bgCx = 40.0f;
        bgCy = 58.0f;
        bgRadius = isAce ? 64.0f : (74.0f + randRange(rng, -2.0f, 2.0f));
    } else if (suit == Suit::Clubs || suit == Suit::Spades) {
        // Position Clubs & Spades toward bottom-right so stems stay offscreen and bottom-right index sits inside
        bgCx = 58.0f;
        bgCy = 84.0f;
        bgRadius = isAce ? 68.0f : (78.0f + randRange(rng, -2.0f, 2.0f));
    }

    addClassicSuitShape(img, suit, bgCx, bgCy, bgRadius, tiltDeg, suitCol);
}

void emitMobileCourtBust(VectorImage& img, Suit suit, Rank rank, DeterministicRng& rng) {
    // High-contrast monochrome silver/slate low-poly cameo bust sitting in front of the massive suit
    const Ramp4 silverHi  = {0xFF94A3B8u, 0xFFCBD5E1u, 0xFFE2E8F0u, 0xFFF8FAFCu};
    const Ramp4 silverMid = {0xFF475569u, 0xFF64748Bu, 0xFF94A3B8u, 0xFFCBD5E1u};
    const Ramp4 slateDark = {0xFF1E293Bu, 0xFF334155u, 0xFF475569u, 0xFF64748Bu};
    const Ramp4 faceLight = {0xFFCBD5E1u, 0xFFE2E8F0u, 0xFFF1F5F9u, 0xFFFFFFFFu};

    const float yaw = randRange(rng, -8.0f, 8.0f);
    std::vector<Tri3> tris;
    auto pushT = [&](Vec3 a, Vec3 b, Vec3 c, const Ramp4& r) {
        tris.push_back({rotY(a, yaw), rotY(b, yaw), rotY(c, yaw), r});
    };
    auto sym = [&](Vec3 a, Vec3 b, Vec3 c, const Ramp4& r) {
        pushT(a, b, c, r);
        pushT({-a.x, a.y, a.z}, {-c.x, c.y, c.z}, {-b.x, b.y, b.z}, r);
    };

    if (rank == Rank::Jack) {
        // Knight / Page bust with visor helmet & upright sword
        sym({0, 10, 10}, {13, 18, 6}, {0, 32, 8}, silverMid);
        sym({13, 18, 6}, {25, 24, 3}, {23, 38, 2}, silverHi);
        sym({13, 18, 6}, {23, 38, 2}, {0, 32, 8}, slateDark);
        // Collar / gorget
        sym({0, 4, 11}, {10, 10, 8}, {13, 18, 6}, silverHi);
        sym({0, 4, 11}, {13, 18, 6}, {0, 12, 10}, silverMid);
        // Faceplate
        sym({0, -14, 10}, {9, -10, 6}, {7, 2, 5}, faceLight);
        sym({0, -14, 10}, {7, 2, 5}, {0, 6, 9}, faceLight);
        // Visor helmet dome & crest
        sym({0, -28, 8}, {12, -20, 4}, {9, -10, 6}, silverMid);
        sym({0, -28, 8}, {9, -10, 6}, {0, -15, 11}, silverHi);
        pushT({0, -34, 6}, {-4, -26, 8}, {4, -26, 8}, slateDark);
        // Upright sword on left
        pushT({-26, -24, 9}, {-29, 24, 8}, {-23, 24, 8}, silverHi);
        pushT({-32, 24, 9}, {-20, 24, 9}, {-26, 28, 9}, slateDark);
        pushT({-27, 28, 8}, {-25, 28, 8}, {-26, 36, 8}, silverMid);
    } else if (rank == Rank::Queen) {
        // Faceted Queen with 3-point crown, veil, and V-neck gown
        sym({0, 14, 10}, {12, 16, 7}, {0, 34, 8}, silverMid);
        sym({12, 16, 7}, {24, 24, 3}, {22, 38, 2}, slateDark);
        sym({12, 16, 7}, {22, 38, 2}, {0, 34, 8}, silverHi);
        // V-neck collar
        sym({0, 16, 11}, {9, 8, 8}, {13, 16, 7}, faceLight);
        // Hair / veil framing face
        sym({9, -16, 5}, {15, -8, 3}, {14, 10, 4}, slateDark);
        sym({9, -16, 5}, {14, 10, 4}, {7, 4, 5}, silverMid);
        // Face
        sym({0, -16, 10}, {9, -12, 6}, {7, 2, 5}, faceLight);
        sym({0, -16, 10}, {7, 2, 5}, {0, 7, 9}, faceLight);
        // 3-point crown
        pushT({0, -31, 8}, {-5, -18, 9}, {5, -18, 9}, silverHi);
        sym({5, -18, 9}, {12, -26, 7}, {10, -16, 7}, silverMid);
        sym({0, -18, 10}, {10, -16, 7}, {0, -14, 10}, silverHi);
    } else {
        // King with heavy 5-point crown, sculpted beard, and royal mantle
        sym({0, 12, 10}, {14, 16, 7}, {0, 34, 8}, slateDark);
        sym({14, 16, 7}, {26, 22, 3}, {24, 38, 2}, silverMid);
        sym({14, 16, 7}, {24, 38, 2}, {0, 34, 8}, silverHi);
        // Mantle lapels
        sym({0, 14, 11}, {10, 6, 9}, {15, 16, 7}, silverHi);
        // Sculpted low-poly beard
        sym({0, 0, 11}, {9, -4, 7}, {8, 8, 6}, silverMid);
        sym({0, 0, 11}, {8, 8, 6}, {0, 14, 10}, slateDark);
        // Face
        sym({0, -16, 10}, {9, -12, 6}, {8, -2, 6}, faceLight);
        sym({0, -16, 10}, {8, -2, 6}, {0, 2, 11}, faceLight);
        // Heavy crown
        pushT({0, -31, 8}, {-6, -18, 9}, {6, -18, 9}, silverHi);
        sym({6, -18, 9}, {13, -27, 7}, {11, -16, 7}, silverHi);
        sym({0, -18, 10}, {11, -16, 7}, {0, -14, 10}, silverMid);
    }

    // Simple two-tone gray trapezoid body extending from behind the bust down to the bottom edge
    addPolyShape(img, {{30.0f, 94.0f}, {50.0f, 94.0f}, {50.0f, 140.0f}, {19.0f, 140.0f}}, 0xFF94A3B8u);
    addPolyShape(img, {{50.0f, 94.0f}, {70.0f, 94.0f}, {81.0f, 140.0f}, {50.0f, 140.0f}}, 0xFF64748Bu);

    emitTris(img, tris, 50.0f, 75.0f, 1.04f);

    // Small suit-colored chest crest so rank/suit identity is unmistakable
    const uint32_t crestCol = (suit == Suit::Hearts || suit == Suit::Diamonds) ? 0xFFCC1D24u : 0xFF14161Eu;
    addClassicSuitShape(img, suit, 50.0f, 99.0f, 5.2f, 0.0f, crestCol);
}

int rankToInt(Rank r) {
    return (r == Rank::Ace) ? 14 : static_cast<int>(r);
}

void addPlateAndBevel(VectorImage& img, LowPolyBg bg) {
    uint32_t plate = 0xFFF4F2ECu;
    uint32_t bevel = 0xFFE6E2D8u;
    if (bg == LowPolyBg::Ivory) {
        plate = 0xFFF2ECE1u;
        bevel = 0xFFE4DCCBu;
    } else if (bg == LowPolyBg::Dark) {
        plate = 0xFF2B303Au;
        bevel = 0xFF22262Fu;
    }
    addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, plate);
    addPolyShape(img, {{100, 84}, {56, 140}, {100, 140}}, bevel);
}

void emitLowPolyHelmet(VectorImage& img, Suit suit, DeterministicRng& rng) {
    const bool red = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const Ramp4 steel = {0xFF334155u, 0xFF475569u, 0xFF64748Bu, 0xFFCBD5E1u};
    const Ramp4 trim  = red ? Ramp4{0xFF92400Eu, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFDE047u}
                            : Ramp4{0xFF1E293Bu, 0xFF38BDF8u, 0xFF7DD3FCu, 0xFFE0F2FEu};
    const Ramp4 dark  = {0xFF090D14u, 0xFF0F172Au, 0xFF1E293Bu, 0xFF334155u};
    const float tilt = randRange(rng, -5.0f, 5.0f);

    std::vector<Tri3> tris;
    // Dome & crest
    tris.push_back({{0, -32, 8}, {-16, -14, 6}, {0, -14, 14}, steel});
    tris.push_back({{0, -32, 8}, {0, -14, 14}, {16, -14, 6}, steel});
    tris.push_back({{-16, -14, 6}, {-22, 4, 2}, {-8, 2, 10}, steel});
    tris.push_back({{16, -14, 6}, {8, 2, 10}, {22, 4, 2}, steel});
    // Plume / crest fin
    tris.push_back({{0, -38, 4}, {-4, -26, 9}, {0, -24, 13}, trim});
    tris.push_back({{0, -38, 4}, {0, -24, 13}, {4, -26, 9}, trim});
    // Brow band
    tris.push_back({{-16, -14, 7}, {-8, -10, 13}, {0, -14, 15}, trim});
    tris.push_back({{0, -14, 15}, {8, -10, 13}, {16, -14, 7}, trim});
    // Visor slit
    tris.push_back({{-12, -8, 9}, {0, -4, 11}, {12, -8, 9}, dark});
    // Cheek guards & nasal bar
    tris.push_back({{-20, 4, 3}, {-14, 26, 5}, {-5, 18, 10}, steel});
    tris.push_back({{-20, 4, 3}, {-5, 18, 10}, {-8, 0, 11}, steel});
    tris.push_back({{20, 4, 3}, {5, 18, 10}, {14, 26, 5}, steel});
    tris.push_back({{20, 4, 3}, {8, 0, 11}, {5, 18, 10}, steel});
    tris.push_back({{0, -10, 14}, {-3, 14, 12}, {3, 14, 12}, trim});

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, tilt);
        t.v1 = rotZ(t.v1, tilt);
        t.v2 = rotZ(t.v2, tilt);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

void emitLowPolyShield(VectorImage& img, Suit suit, DeterministicRng& rng) {
    const bool red = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const Ramp4 field = red ? Ramp4{0xFF7F1D1Du, 0xFF9E1B24u, 0xFFC8202Au, 0xFFEF4444u}
                            : Ramp4{0xFF0369A1u, 0xFF0284C7u, 0xFF38BDF8u, 0xFF7DD3FCu};
    const Ramp4 rim   = {0xFF475569u, 0xFF64748Bu, 0xFF94A3B8u, 0xFFE2E8F0u};
    const Ramp4 boss  = {0xFF92400Eu, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFDE047u};
    const float tilt = randRange(rng, -5.0f, 5.0f);

    std::vector<Tri3> tris;
    // Outer heater shield bevel
    tris.push_back({{-24, -26, 2}, {0, -28, 4}, {0, -22, 7}, rim});
    tris.push_back({{0, -28, 4}, {24, -26, 2}, {0, -22, 7}, rim});
    tris.push_back({{-24, -26, 2}, {-22, 4, 2}, {-17, 2, 7}, rim});
    tris.push_back({{24, -26, 2}, {17, 2, 7}, {22, 4, 2}, rim});
    tris.push_back({{-22, 4, 2}, {0, 32, 3}, {-17, 2, 7}, rim});
    tris.push_back({{22, 4, 2}, {17, 2, 7}, {0, 32, 3}, rim});
    // Heraldic quadrants
    tris.push_back({{-19, -21, 6}, {0, -22, 8}, {0, -2, 12}, field});
    tris.push_back({{-19, -21, 6}, {0, -2, 12}, {-17, 2, 7}, field});
    tris.push_back({{0, -22, 8}, {19, -21, 6}, {0, -2, 12}, boss});
    tris.push_back({{19, -21, 6}, {17, 2, 7}, {0, -2, 12}, boss});
    tris.push_back({{-17, 2, 7}, {0, -2, 12}, {0, 26, 7}, boss});
    tris.push_back({{17, 2, 7}, {0, 26, 7}, {0, -2, 12}, field});
    // Center boss
    tris.push_back({{0, -10, 14}, {-7, -2, 14}, {0, 6, 14}, rim});
    tris.push_back({{0, -10, 14}, {0, 6, 14}, {7, -2, 14}, rim});

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, tilt);
        t.v1 = rotZ(t.v1, tilt);
        t.v2 = rotZ(t.v2, tilt);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

void emitLowPolyMace(VectorImage& img, Suit suit, DeterministicRng& rng) {
    const bool red = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const Ramp4 head = {0xFF334155u, 0xFF475569u, 0xFF94A3B8u, 0xFFE2E8F0u};
    const Ramp4 trim = red ? Ramp4{0xFF92400Eu, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFDE047u}
                           : Ramp4{0xFF0369A1u, 0xFF0284C7u, 0xFF38BDF8u, 0xFF7DD3FCu};
    const Ramp4 haft = {0xFF451A03u, 0xFF78350Fu, 0xFF92400Eu, 0xFFB45309u};
    const float angle = -36.0f + randRange(rng, -6.0f, 6.0f);

    std::vector<Tri3> tris;
    // Shaft
    tris.push_back({{-2.5f, -4, 3}, {2.5f, -4, 3}, {2.5f, 32, 3}, haft});
    tris.push_back({{-2.5f, -4, 3}, {2.5f, 32, 3}, {-2.5f, 32, 3}, haft});
    // Pommel
    tris.push_back({{-5, 32, 2}, {0, 29, 6}, {5, 32, 2}, trim});
    tris.push_back({{-5, 32, 2}, {5, 32, 2}, {0, 38, 3}, trim});
    // Flanged mace head
    tris.push_back({{0, -28, 6}, {-12, -16, 8}, {0, -14, 14}, head});
    tris.push_back({{0, -28, 6}, {0, -14, 14}, {12, -16, 8}, head});
    tris.push_back({{-12, -16, 8}, {-8, -4, 6}, {0, -14, 14}, head});
    tris.push_back({{12, -16, 8}, {0, -14, 14}, {8, -4, 6}, head});
    // Side flanges & top spike
    tris.push_back({{0, -37, 4}, {-4, -27, 7}, {4, -27, 7}, trim});
    tris.push_back({{-19, -16, 3}, {-11, -21, 7}, {-11, -11, 7}, trim});
    tris.push_back({{19, -16, 3}, {11, -11, 7}, {11, -21, 7}, trim});

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, angle);
        t.v1 = rotZ(t.v1, angle);
        t.v2 = rotZ(t.v2, angle);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

void emitLowPolyBow(VectorImage& img, Suit suit, DeterministicRng& rng) {
    const bool red = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const Ramp4 wood  = {0xFF451A03u, 0xFF78350Fu, 0xFFB45309u, 0xFFD97706u};
    const Ramp4 arrow = {0xFF334155u, 0xFF64748Bu, 0xFF94A3B8u, 0xFFE2E8F0u};
    const Ramp4 fletch = red ? Ramp4{0xFF9E1B24u, 0xFFC8202Au, 0xFFEF4444u, 0xFFFCA5A5u}
                             : Ramp4{0xFF059669u, 0xFF10B981u, 0xFF34D399u, 0xFFA7F3D0u};
    const Ramp4 stringMat = {0xFFCBD5E1u, 0xFFE2E8F0u, 0xFFF1F5F9u, 0xFFFFFFFFu};
    const float angle = 32.0f + randRange(rng, -5.0f, 5.0f);

    std::vector<Tri3> tris;
    // Bow grip & limbs
    tris.push_back({{-3, -8, 6}, {3, -8, 6}, {3, 8, 6}, wood});
    tris.push_back({{-3, -8, 6}, {3, 8, 6}, {-3, 8, 6}, wood});
    tris.push_back({{-3, -8, 6}, {-14, -24, 4}, {-8, -26, 5}, wood});
    tris.push_back({{-3, -8, 6}, {-8, -26, 5}, {3, -8, 6}, wood});
    tris.push_back({{-14, -24, 4}, {-20, -34, 2}, {-13, -34, 3}, wood});
    tris.push_back({{-3, 8, 6}, {3, 8, 6}, {-8, 26, 5}, wood});
    tris.push_back({{-3, 8, 6}, {-8, 26, 5}, {-14, 24, 4}, wood});
    tris.push_back({{-14, 24, 4}, {-13, 34, 3}, {-20, 34, 2}, wood});
    // Taut bowstring
    tris.push_back({{-16, -32, 3}, {-15, -32, 3}, {-15, 32, 3}, stringMat});
    tris.push_back({{-16, -32, 3}, {-15, 32, 3}, {-16, 32, 3}, stringMat});
    // Nocked arrow pointing right
    tris.push_back({{-16, -1.5f, 7}, {18, -1.5f, 7}, {18, 1.5f, 7}, arrow});
    tris.push_back({{-16, -1.5f, 7}, {18, 1.5f, 7}, {-16, 1.5f, 7}, arrow});
    tris.push_back({{18, -5, 7}, {28, 0, 8}, {18, 5, 7}, arrow});
    tris.push_back({{-20, -5, 6}, {-12, -1.5f, 7}, {-14, 0, 7}, fletch});
    tris.push_back({{-20, 5, 6}, {-14, 0, 7}, {-12, 1.5f, 7}, fletch});

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, angle);
        t.v1 = rotZ(t.v1, angle);
        t.v2 = rotZ(t.v2, angle);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

void emitLowPolyGauntlet(VectorImage& img, Suit suit, DeterministicRng& rng) {
    const bool red = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const Ramp4 plate = {0xFF334155u, 0xFF475569u, 0xFF64748Bu, 0xFFCBD5E1u};
    const Ramp4 trim  = red ? Ramp4{0xFF92400Eu, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFDE047u}
                            : Ramp4{0xFF0369A1u, 0xFF0284C7u, 0xFF38BDF8u, 0xFF7DD3FCu};
    const float tilt = randRange(rng, -6.0f, 6.0f);

    std::vector<Tri3> tris;
    // Flared wrist cuff
    tris.push_back({{-14, 30, 3}, {-10, 10, 6}, {10, 10, 6}, plate});
    tris.push_back({{-14, 30, 3}, {10, 10, 6}, {14, 30, 3}, plate});
    tris.push_back({{-11, 10, 7}, {-11, 5, 9}, {11, 5, 9}, trim});
    tris.push_back({{-11, 10, 7}, {11, 5, 9}, {11, 10, 7}, trim});
    // Palm / fist block
    tris.push_back({{-14, 5, 8}, {-16, -14, 10}, {0, -14, 14}, plate});
    tris.push_back({{-14, 5, 8}, {0, -14, 14}, {14, 5, 8}, plate});
    tris.push_back({{0, -14, 14}, {16, -14, 10}, {14, 5, 8}, plate});
    // Articulated knuckle plates
    tris.push_back({{-16, -14, 10}, {-12, -26, 8}, {-4, -26, 11}, trim});
    tris.push_back({{-16, -14, 10}, {-4, -26, 11}, {0, -14, 14}, plate});
    tris.push_back({{0, -14, 14}, {-4, -26, 11}, {8, -26, 10}, trim});
    tris.push_back({{0, -14, 14}, {8, -26, 10}, {16, -14, 10}, plate});
    // Thumb guard
    tris.push_back({{-14, 4, 8}, {-23, -6, 9}, {-16, -12, 11}, trim});

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, tilt);
        t.v1 = rotZ(t.v1, tilt);
        t.v2 = rotZ(t.v2, tilt);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

void emitLowPolyGem(VectorImage& img, Suit suit, DeterministicRng& rng) {
    const Ramp4 ruby    = {0xFF881337u, 0xFFBE123Cu, 0xFFE11D48u, 0xFFFB7185u};
    const Ramp4 emerald = {0xFF065F46u, 0xFF059669u, 0xFF10B981u, 0xFF6EE7B7u};
    const Ramp4 sapph   = {0xFF1E3A8Au, 0xFF1D4ED8u, 0xFF3B82F6u, 0xFF93C5FDu};
    const Ramp4 amber   = {0xFF92400Eu, 0xFFD97706u, 0xFFF59E0Bu, 0xFFFDE047u};
    const Ramp4& gem = (suit == Suit::Hearts) ? ruby
                     : (suit == Suit::Diamonds) ? amber
                     : (suit == Suit::Clubs) ? emerald : sapph;
    const float tilt = randRange(rng, -5.0f, 5.0f);

    std::vector<Tri3> tris;
    // Octagonal brilliant-cut crown table & bevels
    const Vec3 ctr{0, 0, 16};
    const std::array<Vec3, 8> inner = {{
        {0, -15, 14}, {11, -11, 14}, {15, 0, 14}, {11, 11, 14},
        {0, 15, 14}, {-11, 11, 14}, {-15, 0, 14}, {-11, -11, 14}
    }};
    const std::array<Vec3, 8> outer = {{
        {0, -29, 4}, {21, -21, 4}, {29, 0, 4}, {21, 21, 4},
        {0, 29, 4}, {-21, 21, 4}, {-29, 0, 4}, {-21, -21, 4}
    }};
    for (size_t i = 0; i < 8; ++i) {
        size_t n = (i + 1) % 8;
        tris.push_back({ctr, inner[i], inner[n], gem});
        tris.push_back({inner[i], outer[i], outer[n], gem});
        tris.push_back({inner[i], outer[n], inner[n], gem});
    }

    for (auto& t : tris) {
        t.v0 = rotZ(t.v0, tilt);
        t.v1 = rotZ(t.v1, tilt);
        t.v2 = rotZ(t.v2, tilt);
    }
    emitTris(img, tris, 50.0f, 72.0f);
}

// ---------------------------------------------------------------------------
// Low Poly War (Another World 1991 Flat-Shaded Vector Aesthetic)
// ---------------------------------------------------------------------------
uint32_t shiftArgb(uint32_t argb, int dr, int dg, int db) {
    const int a = static_cast<int>((argb >> 24) & 0xFFu);
    const int r = std::clamp(static_cast<int>((argb >> 16) & 0xFFu) + dr, 0, 255);
    const int g = std::clamp(static_cast<int>((argb >> 8) & 0xFFu) + dg, 0, 255);
    const int b = std::clamp(static_cast<int>(argb & 0xFFu) + db, 0, 255);
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

struct WarPalette {
    uint32_t windowBg;
    uint32_t windowBeam;
    uint32_t windowHorizon;
    uint32_t navyDark;
    uint32_t indigoMid;
    uint32_t nearBlack;
    uint32_t suitDark;
    uint32_t suitBright;
    uint32_t goldBright;
    uint32_t goldShadow;
    uint32_t skinLight;
    uint32_t skinShadow;
    uint32_t offWhite;
    uint32_t steelLight;
    uint32_t steelShadow;
};

WarPalette makeWarPalette(Suit suit, uint32_t deckSeed, DeterministicRng& cardRng) {
    DeterministicRng globalRng(deckSeed ^ 0x57415231u);
    const int bgShiftR = randInt(globalRng, -10, 12);
    const int bgShiftG = randInt(globalRng, -8, 10);
    const int bgShiftB = randInt(globalRng, -10, 14);
    const int clothShift = randInt(cardRng, -8, 8);
    const bool isRed = (suit == Suit::Hearts || suit == Suit::Diamonds);

    WarPalette p{};
    p.windowBg      = shiftArgb(0xFF3C3748u, bgShiftR, bgShiftG, bgShiftB);
    p.windowBeam    = shiftArgb(0xFF4A4459u, bgShiftR, bgShiftG, bgShiftB);
    p.windowHorizon = shiftArgb(0xFF2E2A38u, bgShiftR, bgShiftG, bgShiftB);
    p.navyDark      = shiftArgb(0xFF1A1733u, clothShift, clothShift / 2, clothShift);
    p.indigoMid     = shiftArgb(0xFF2B2742u, clothShift, clothShift / 2, clothShift);
    p.nearBlack     = 0xFF0E0D14u;
    p.suitDark      = isRed ? shiftArgb(0xFF8A1824u, clothShift / 2, 0, 0) : shiftArgb(0xFF52576Eu, 0, clothShift / 2, clothShift);
    p.suitBright    = isRed ? shiftArgb(0xFFC8202Au, clothShift / 2, 0, 0) : shiftArgb(0xFF7E85A1u, 0, clothShift / 2, clothShift);
    p.goldBright    = 0xFFE5C947u;
    p.goldShadow    = 0xFFC29C2Eu;
    p.skinLight     = 0xFFEBC0B8u;
    p.skinShadow    = 0xFFC28787u;
    p.offWhite      = 0xFFECEBE2u;
    p.steelLight    = 0xFF9CA2B4u;
    p.steelShadow   = 0xFF686D80u;
    return p;
}

void addWarChamferWindow(VectorImage& img, const WarPalette& p, DeterministicRng& rng) {
    // Chamfered portrait window: x in [19, 81], y in [15.5, 124.5], chamfer = 7.2
    constexpr float wx = 19.0f, wy = 15.5f, ww = 62.0f, wh = 109.0f, ch = 7.2f;
    addPolyShape(img, {
        {wx + ch, wy}, {wx + ww, wy}, {wx + ww, wy + wh - ch},
        {wx + ww - ch, wy + wh}, {wx, wy + wh}, {wx, wy + ch}
    }, p.windowBg);

    // Subtle 1991 Another World atmospheric light beam & horizon band inside window
    const float beamShift = randRange(rng, -5.0f, 5.0f);
    addPolyShape(img, {
        {wx + ch + 8.0f + beamShift, wy},
        {wx + ww - 6.0f, wy},
        {wx + ww - 22.0f + beamShift, wy + wh * 0.65f},
        {wx + 4.0f, wy + wh * 0.48f}
    }, p.windowBeam);

    const float horY = wy + wh * randRange(rng, 0.58f, 0.68f);
    addPolyShape(img, {
        {wx, horY}, {wx + ww, horY - 4.0f},
        {wx + ww, wy + wh - ch}, {wx + ww - ch, wy + wh}, {wx, wy + wh}
    }, p.windowHorizon);
}

void addWarFacetedPip(VectorImage& img, Suit suit, float cx, float cy, float size, bool rotate180) {
    const float s = size * 0.5f;
    const bool isRed = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const uint32_t c1 = isRed ? 0xFF9E1B24u : 0xFF1A1733u;
    const uint32_t c2 = isRed ? 0xFFC8202Au : 0xFF2E2952u;

    auto emitHalf = [&](std::initializer_list<std::pair<float, float>> normPts, uint32_t col) {
        std::vector<std::pair<float, float>> pts;
        pts.reserve(normPts.size());
        for (auto [nx, ny] : normPts) {
            const float sx = rotate180 ? -nx : nx;
            const float sy = rotate180 ? -ny : ny;
            pts.emplace_back(cx + sx * s, cy + sy * s);
        }
        addPolyShape(img, pts, col);
    };

    if (suit == Suit::Hearts) {
        emitHalf({{0, 0.92f}, {-0.95f, -0.05f}, {-0.85f, -0.62f}, {-0.42f, -0.88f}, {0, -0.45f}}, c1);
        emitHalf({{0, 0.92f}, {0, -0.45f}, {0.42f, -0.88f}, {0.85f, -0.62f}, {0.95f, -0.05f}}, c2);
    } else if (suit == Suit::Diamonds) {
        emitHalf({{0, -1.0f}, {-0.76f, 0.0f}, {0, 1.0f}}, c1);
        emitHalf({{0, -1.0f}, {0.76f, 0.0f}, {0, 1.0f}}, c2);
    } else if (suit == Suit::Spades) {
        emitHalf({{0, -1.0f}, {-0.92f, 0.05f}, {-0.75f, 0.48f}, {-0.18f, 0.35f}, {-0.34f, 0.95f}, {0, 0.95f}}, c1);
        emitHalf({{0, -1.0f}, {0, 0.95f}, {0.34f, 0.95f}, {0.18f, 0.35f}, {0.75f, 0.48f}, {0.92f, 0.05f}}, c2);
    } else {
        emitHalf({{0, -0.98f}, {-0.42f, -0.72f}, {-0.36f, -0.25f}, {-0.86f, -0.15f}, {-0.96f, 0.25f}, {-0.58f, 0.52f}, {-0.14f, 0.35f}, {-0.30f, 0.95f}, {0, 0.95f}}, c1);
        emitHalf({{0, -0.98f}, {0, 0.95f}, {0.30f, 0.95f}, {0.14f, 0.35f}, {0.58f, 0.52f}, {0.96f, 0.25f}, {0.86f, -0.15f}, {0.36f, -0.25f}, {0.42f, -0.72f}}, c2);
    }
}

void emitWarCourt(VectorImage& img, Suit suit, Rank rank, const LowPolyDeckOptions& opts, DeterministicRng& rng) {
    const WarPalette p = makeWarPalette(suit, opts.seed, rng);
    addWarChamferWindow(img, p, rng);

    const float leanX = randRange(rng, -1.8f, 1.8f);
    const float leanY = randRange(rng, -1.5f, 1.5f);
    const float shW   = randRange(rng, -2.0f, 2.2f);
    const float crH   = randRange(rng, -2.0f, 2.4f);
    const float foldJ = randRange(rng, -2.2f, 2.2f);
    const float flipSign = (randInt(rng, 0, 1) == 0) ? 1.0f : -1.0f;
    const float yScale = (rank == Rank::Queen) ? randRange(rng, 0.85f, 0.91f) : 1.0f;

    // Helper to clamp polygon coordinates strictly inside the chamfered window [19..81, 15.5..124.5]
    auto emitP = [&](std::initializer_list<std::pair<float, float>> ptsIn, uint32_t col) {
        std::vector<std::pair<float, float>> pts;
        pts.reserve(ptsIn.size());
        for (auto [x, y] : ptsIn) {
            const float px = std::clamp(50.0f + (x - 50.0f) * flipSign, 19.5f, 80.5f);
            const float sy = 124.0f - (124.0f - y) * yScale;
            const float py = std::clamp(sy, 16.0f, 124.0f);
            pts.emplace_back(px, py);
        }
        addPolyShape(img, pts, col);
    };

    if (rank == Rank::Jack) {
        // Jack of Spades reference: 3/4 rogue/knight bust, swept cloak, high collar, upright blade
        // Cloak & torso base anchored to bottom of window (y = 124)
        emitP({{20, 124}, {23 - shW, 88}, {38, 76}, {54 + leanX, 82}, {52, 124}}, p.navyDark);
        emitP({{52, 124}, {54 + leanX, 82}, {74 + shW, 86}, {79, 124}}, p.indigoMid);
        // Diagonal cloak fold & suit-colored tunic sash
        emitP({{34, 80}, {48 + leanX, 78}, {68 + foldJ, 124}, {46 + foldJ, 124}}, p.suitDark);
        emitP({{44 + leanX, 80}, {54 + leanX, 82}, {72 + foldJ, 124}, {62 + foldJ, 124}}, p.suitBright);
        // Hard-edged cast shadow across chest
        emitP({{22, 124}, {30, 95}, {45 + foldJ * 0.5f, 108}, {38, 124}}, p.nearBlack);

        // High dramatic collar & steel gorget
        emitP({{36 + leanX, 78}, {44 + leanX, 66}, {56 + leanX, 68}, {50 + leanX, 84}}, p.offWhite);
        emitP({{48 + leanX, 72}, {62 + leanX, 68}, {60 + leanX, 82}, {50 + leanX, 84}}, p.steelShadow);
        emitP({{38 + leanX, 76}, {46 + leanX, 72}, {50 + leanX, 84}}, p.steelLight);

        // Neck & chiseled 3/4 face (lit left cheek, shadow right jaw)
        emitP({{43 + leanX, 63 + leanY}, {54 + leanX, 63 + leanY}, {52 + leanX, 74}, {44 + leanX, 74}}, p.skinShadow);
        emitP({{40 + leanX, 43 + leanY}, {51 + leanX, 41 + leanY}, {52 + leanX, 63 + leanY}, {42 + leanX, 64 + leanY}, {39 + leanX, 54 + leanY}}, p.skinLight);
        emitP({{51 + leanX, 41 + leanY}, {59 + leanX, 46 + leanY}, {57 + leanX, 60 + leanY}, {50 + leanX, 65 + leanY}, {49 + leanX, 53 + leanY}}, p.skinShadow);
        // Distinct left/right brow & eye shadow wedges + angular nose shadow
        emitP({{41 + leanX, 47 + leanY}, {48 + leanX, 48 + leanY}, {47 + leanX, 51 + leanY}, {42 + leanX, 50 + leanY}}, p.navyDark);
        emitP({{51 + leanX, 48 + leanY}, {57 + leanX, 47 + leanY}, {56 + leanX, 50 + leanY}, {51 + leanX, 51 + leanY}}, p.nearBlack);
        emitP({{49 + leanX, 52 + leanY}, {53 + leanX, 57 + leanY}, {48 + leanX, 57 + leanY}}, p.skinShadow);

        // Swept angular hair & war-beret / plumed helm
        emitP({{37 + leanX, 46 + leanY}, {41 + leanX, 34 + leanY}, {55 + leanX, 34 + leanY}, {62 + leanX, 46 + leanY}, {58 + leanX, 57 + leanY}, {54 + leanX, 44 + leanY}, {42 + leanX, 44 + leanY}}, p.navyDark);
        emitP({{35 + leanX, 41 + leanY}, {43 + leanX, 27 + leanY - crH}, {63 + leanX, 31 + leanY}, {59 + leanX, 42 + leanY}}, p.suitDark);
        emitP({{43 + leanX, 27 + leanY - crH}, {57 + leanX, 25 + leanY - crH}, {63 + leanX, 31 + leanY}, {48 + leanX, 35 + leanY}}, p.suitBright);
        // Gold plume / clasp
        emitP({{39 + leanX, 37 + leanY}, {33 + leanX, 24 + leanY - crH}, {44 + leanX, 32 + leanY}}, p.goldBright);

        // Upright faceted broadsword on the left
        emitP({{24, 30}, {26.5f, 24}, {26.5f, 92}, {24, 92}}, p.offWhite);
        emitP({{26.5f, 24}, {29, 30}, {29, 92}, {26.5f, 92}}, p.steelShadow);
        emitP({{19.5f, 92}, {33.5f, 92}, {31.5f, 95.5f}, {21.5f, 95.5f}}, p.goldBright);
        emitP({{25, 95.5f}, {28, 95.5f}, {27.5f, 108}, {25.5f, 108}}, p.goldShadow);
    } else if (rank == Rank::Queen) {
        // Queen of Clubs reference: regal bust, high collar, sculpted hair/veil, 3-5 pt crown
        // Royal gown & sweeping mantle
        emitP({{20, 124}, {24 - shW, 86}, {40, 76}, {50, 124}}, p.navyDark);
        emitP({{80, 124}, {76 + shW, 86}, {60, 76}, {50, 124}}, p.indigoMid);
        emitP({{37, 78}, {50, 86}, {63, 78}, {58 + foldJ * 0.4f, 124}, {42 + foldJ * 0.4f, 124}}, p.suitDark);
        emitP({{50, 86}, {63, 78}, {58 + foldJ * 0.4f, 124}, {50, 124}}, p.suitBright);
        // Hard shadow fold on skirt/torso
        emitP({{20, 124}, {28, 94}, {40, 108}, {33, 124}}, p.nearBlack);

        // High sculpted Queen's collar & rim-lit neckline
        emitP({{34 + leanX * 0.5f, 78}, {38 + leanX * 0.5f, 60}, {46 + leanX, 66}, {44 + leanX, 82}}, p.offWhite);
        emitP({{66 + leanX * 0.5f, 78}, {62 + leanX * 0.5f, 60}, {54 + leanX, 66}, {56 + leanX, 82}}, p.steelLight);
        emitP({{41 + leanX, 76}, {50 + leanX, 90}, {59 + leanX, 76}, {50 + leanX, 72}}, p.skinLight);
        // Suit chest medallion
        emitP({{50 + leanX, 88}, {54 + leanX, 95}, {50 + leanX, 102}, {46 + leanX, 95}}, p.goldBright);
        emitP({{50 + leanX, 91}, {52.5f + leanX, 95}, {50 + leanX, 99}, {47.5f + leanX, 95}}, p.suitBright);

        // Flowing dark hair/veil framing face (slimmer silhouette)
        emitP({{38 + leanX, 42 + leanY}, {41 + leanX, 35 + leanY}, {59 + leanX, 35 + leanY}, {62.5f + leanX, 44 + leanY}, {61.5f + leanX, 72}, {38.5f + leanX, 72}}, p.nearBlack);
        emitP({{38 + leanX, 44 + leanY}, {41 + leanX, 38 + leanY}, {42.5f + leanX, 68}, {38.5f + leanX, 72}}, p.indigoMid);

        // Sculpted Queen face (strong lit/shadow split)
        emitP({{42 + leanX, 40 + leanY}, {50 + leanX, 39 + leanY}, {50 + leanX, 65 + leanY}, {43 + leanX, 60 + leanY}, {41 + leanX, 49 + leanY}}, p.skinLight);
        emitP({{50 + leanX, 39 + leanY}, {58 + leanX, 41 + leanY}, {58 + leanX, 51 + leanY}, {56 + leanX, 60 + leanY}, {50 + leanX, 65 + leanY}}, p.skinShadow);
        // Brow/eye & cheekbone shadow wedges
        emitP({{42 + leanX, 46 + leanY}, {49 + leanX, 47 + leanY}, {48 + leanX, 50 + leanY}, {43 + leanX, 49 + leanY}}, p.navyDark);
        emitP({{51 + leanX, 47 + leanY}, {57 + leanX, 46 + leanY}, {56 + leanX, 50 + leanY}, {51 + leanX, 50 + leanY}}, p.nearBlack);
        emitP({{48 + leanX, 58 + leanY}, {52 + leanX, 58 + leanY}, {50 + leanX, 60 + leanY}}, p.suitBright);

        // Ornate 3-to-5-point faceted Crown
        emitP({{40 + leanX, 39 + leanY}, {37 + leanX, 24 + leanY - crH}, {45 + leanX, 32 + leanY}, {50 + leanX, 20 + leanY - crH}, {55 + leanX, 32 + leanY}, {63 + leanX, 24 + leanY - crH}, {60 + leanX, 39 + leanY}}, p.goldBright);
        emitP({{50 + leanX, 20 + leanY - crH}, {55 + leanX, 32 + leanY}, {63 + leanX, 24 + leanY - crH}, {60 + leanX, 39 + leanY}, {50 + leanX, 39 + leanY}}, p.goldShadow);
        emitP({{40 + leanX, 36 + leanY}, {60 + leanX, 36 + leanY}, {60 + leanX, 39 + leanY}, {40 + leanX, 39 + leanY}}, p.suitBright);
    } else {
        // King of Clubs reference: broad royal mantle, heavy 5-pt crown, sculpted beard, scepter
        // Heavy mantle & shoulder armor
        emitP({{20, 124}, {21 - shW, 82}, {38, 72}, {50, 124}}, p.navyDark);
        emitP({{80, 124}, {79 + shW, 82}, {62, 72}, {50, 124}}, p.indigoMid);
        emitP({{34, 75}, {50, 82}, {66, 75}, {64 + foldJ * 0.5f, 124}, {36 + foldJ * 0.5f, 124}}, p.suitDark);
        emitP({{50, 82}, {66, 75}, {64 + foldJ * 0.5f, 124}, {50, 124}}, p.suitBright);
        // Ermine/steel mantle lapels
        emitP({{32, 76}, {41, 70}, {48, 92}, {41, 96}}, p.offWhite);
        emitP({{68, 76}, {59, 70}, {52, 92}, {59, 96}}, p.steelLight);
        emitP({{20, 124}, {26, 92}, {39, 108}, {32, 124}}, p.nearBlack);

        // King face & distinct left/right eye/brow wedges
        emitP({{40 + leanX, 38 + leanY}, {50 + leanX, 37 + leanY}, {50 + leanX, 58 + leanY}, {40 + leanX, 54 + leanY}}, p.skinLight);
        emitP({{50 + leanX, 37 + leanY}, {60 + leanX, 39 + leanY}, {59 + leanX, 54 + leanY}, {50 + leanX, 58 + leanY}}, p.skinShadow);
        emitP({{41 + leanX, 44 + leanY}, {48.5f + leanX, 45 + leanY}, {47.5f + leanX, 48.5f + leanY}, {42 + leanX, 47.5f + leanY}}, p.navyDark);
        emitP({{51.5f + leanX, 45 + leanY}, {59 + leanX, 44 + leanY}, {58 + leanX, 47.5f + leanY}, {52.5f + leanX, 48.5f + leanY}}, p.nearBlack);
        emitP({{48 + leanX, 48 + leanY}, {52 + leanX, 48 + leanY}, {51 + leanX, 55 + leanY}, {49 + leanX, 55 + leanY}}, p.skinLight);

        // Sculpted low-poly beard & hair
        emitP({{37 + leanX, 42 + leanY}, {41 + leanX, 40 + leanY}, {41 + leanX, 60 + leanY}, {36 + leanX, 56 + leanY}}, p.navyDark);
        emitP({{59 + leanX, 40 + leanY}, {63 + leanX, 42 + leanY}, {64 + leanX, 56 + leanY}, {59 + leanX, 60 + leanY}}, p.nearBlack);
        emitP({{40 + leanX, 54 + leanY}, {50 + leanX, 57 + leanY}, {50 + leanX, 75 + leanY}, {42 + leanX, 68 + leanY}}, p.indigoMid);
        emitP({{50 + leanX, 57 + leanY}, {60 + leanX, 54 + leanY}, {58 + leanX, 68 + leanY}, {50 + leanX, 75 + leanY}}, p.navyDark);
        emitP({{44 + leanX, 56 + leanY}, {56 + leanX, 56 + leanY}, {53 + leanX, 61 + leanY}, {47 + leanX, 61 + leanY}}, p.steelLight);

        // Heavy 5-point faceted Crown
        emitP({{38 + leanX, 38 + leanY}, {36 + leanX, 23 + leanY - crH}, {43 + leanX, 31 + leanY}, {47 + leanX, 20 + leanY - crH}, {50 + leanX, 28 + leanY}, {53 + leanX, 20 + leanY - crH}, {57 + leanX, 31 + leanY}, {64 + leanX, 23 + leanY - crH}, {62 + leanX, 38 + leanY}}, p.goldBright);
        emitP({{50 + leanX, 28 + leanY}, {53 + leanX, 20 + leanY - crH}, {57 + leanX, 31 + leanY}, {64 + leanX, 23 + leanY - crH}, {62 + leanX, 38 + leanY}, {50 + leanX, 38 + leanY}}, p.goldShadow);
        emitP({{38 + leanX, 34 + leanY}, {62 + leanX, 34 + leanY}, {62 + leanX, 38 + leanY}, {38 + leanX, 38 + leanY}}, p.suitBright);

        // Royal scepter / greatsword hilt on right
        emitP({{71, 44}, {74, 44}, {74, 124}, {71, 124}}, p.goldShadow);
        emitP({{71, 44}, {72.5f, 44}, {72.5f, 124}, {71, 124}}, p.goldBright);
        emitP({{72.5f, 34}, {77.5f, 41}, {72.5f, 48}, {67.5f, 41}}, p.suitBright);
    }
}

void emitWarPips(VectorImage& img, Suit suit, Rank rank, const LowPolyDeckOptions& opts, DeterministicRng& rng) {
    if (rank == Rank::Ace) {
        const WarPalette p = makeWarPalette(suit, opts.seed, rng);
        const float tilt = randRange(rng, -3.0f, 3.0f);
        // Subtle chamfered diamond medallion behind the Ace pip
        addPolyShape(img, {
            {50.0f + tilt, 34.0f}, {74.0f, 70.0f}, {50.0f - tilt, 106.0f}, {26.0f, 70.0f}
        }, p.windowBg);
        addPolyShape(img, {
            {50.0f + tilt, 34.0f}, {74.0f, 70.0f}, {50.0f, 70.0f}
        }, p.windowBeam);
        addWarFacetedPip(img, suit, 50.0f, 70.0f, 38.0f, false);
        return;
    }

    struct PipPos { float x, y; bool flip; };
    static const std::vector<PipPos> kLayouts[9] = {
        {{50, 36, false}, {50, 104, true}},
        {{50, 34, false}, {50, 70, false}, {50, 106, true}},
        {{34, 36, false}, {66, 36, false}, {34, 104, true}, {66, 104, true}},
        {{34, 36, false}, {66, 36, false}, {50, 70, false}, {34, 104, true}, {66, 104, true}},
        {{34, 34, false}, {66, 34, false}, {34, 70, false}, {66, 70, false}, {34, 106, true}, {66, 106, true}},
        {{34, 34, false}, {66, 34, false}, {50, 52, false}, {34, 70, false}, {66, 70, false}, {34, 106, true}, {66, 106, true}},
        {{34, 34, false}, {66, 34, false}, {50, 52, false}, {34, 70, false}, {66, 70, false}, {50, 88, true}, {34, 106, true}, {66, 106, true}},
        {{34, 30, false}, {66, 30, false}, {34, 56, false}, {66, 56, false}, {50, 70, false}, {34, 84, true}, {66, 84, true}, {34, 110, true}, {66, 110, true}},
        {{34, 30, false}, {66, 30, false}, {50, 43, false}, {34, 56, false}, {66, 56, false}, {34, 84, true}, {66, 84, true}, {50, 97, true}, {34, 110, true}, {66, 110, true}},
    };
    const int lIdx = static_cast<int>(rank) - 2;
    const float pipSize = (lIdx >= 7) ? 13.5f : 15.5f;
    for (const auto& pt : kLayouts[lIdx]) {
        addWarFacetedPip(img, suit, pt.x, pt.y, pipSize, pt.flip);
    }
}

VectorImage buildCardFaceVector(Suit suit, Rank rank, const LowPolyDeckOptions& opts, uint32_t cardSeed) {
    VectorImage img{};
    img.width = 100.0f;
    img.height = 140.0f;

    if (opts.style == LowPolyStyle::War) {
        addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFFF4F2E6u);
        addPolyShape(img, {{100, 84}, {56, 140}, {100, 140}}, 0xFFE8E4D4u);
    } else {
        addPlateAndBevel(img, opts.bg);
    }

    DeterministicRng rng(cardSeed);
    if (opts.style == LowPolyStyle::War) {
        if (rank == Rank::Jack || rank == Rank::Queen || rank == Rank::King) {
            emitWarCourt(img, suit, rank, opts, rng);
        } else {
            emitWarPips(img, suit, rank, opts, rng);
        }
    } else if (opts.style == LowPolyStyle::Scoundrel) {
        const int rn = rankToInt(rank);
        if (suit == Suit::Diamonds) {
            emitScoundrelWeapon(img, rn, rng);
        } else if (suit == Suit::Hearts) {
            emitScoundrelPotion(img, rn, rng);
        } else {
            emitScoundrelMonster(img, suit, rn, rng);
        }
    } else if (opts.style == LowPolyStyle::Memory) {
        const Ramp4 blackRamp{0xFF334155u, 0xFF475569u, 0xFF94A3B8u, 0xFFE2E8F0u};
        const Ramp4 redRamp{0xFF7F1D1Du, 0xFF9E1B24u, 0xFFC8202Au, 0xFFEF4444u};
        const Ramp4& suitRamp = (suit == Suit::Hearts || suit == Suit::Diamonds) ? redRamp : blackRamp;
        const int rn = rankToInt(rank);
        if (rank == Rank::Ace) {
            emitTris(img, buildSuitTris(suit, suitRamp), 50.0f, 70.0f, 1.18f);
        } else if (rank == Rank::Jack || rank == Rank::Queen || rank == Rank::King) {
            emitClassicCourt(img, suit, rank, rng, opts);
        } else if (rn == 10) {
            emitScoundrelMonster(img, suit, 10, rng);
        } else if (rn == 9) {
            emitScoundrelWeapon(img, 9, rng);
        } else if (rn == 8) {
            emitScoundrelPotion(img, 8, rng);
        } else if (rn == 7) {
            emitLowPolyHelmet(img, suit, rng);
        } else if (rn == 6) {
            emitLowPolyShield(img, suit, rng);
        } else if (rn == 5) {
            emitLowPolyMace(img, suit, rng);
        } else if (rn == 4) {
            emitLowPolyBow(img, suit, rng);
        } else if (rn == 3) {
            emitLowPolyGauntlet(img, suit, rng);
        } else {
            emitLowPolyGem(img, suit, rng);
        }
    } else {
        const bool darkBg = (opts.bg == LowPolyBg::Dark);
        const Ramp4 blackRamp = darkBg
            ? Ramp4{0xFF334155u, 0xFF475569u, 0xFF94A3B8u, 0xFFE2E8F0u}
            : Ramp4{0xFF0F172Au, 0xFF1E293Bu, 0xFF334155u, 0xFF64748Bu};
        const Ramp4 redRamp = {0xFF7F1D1Du, 0xFF9E1B24u, 0xFFC8202Au, 0xFFEF4444u};
        const Ramp4& suitRamp = (suit == Suit::Hearts || suit == Suit::Diamonds) ? redRamp : blackRamp;

        if (opts.style == LowPolyStyle::Mobile) {
            emitMobileSuitBg(img, suit, rank, rng, opts.bg);
            if (rank == Rank::Jack || rank == Rank::Queen || rank == Rank::King) {
                emitMobileCourtBust(img, suit, rank, rng);
            }
        } else if (rank == Rank::Ace) {
            emitTris(img, buildSuitTris(suit, suitRamp), 50.0f, 70.0f, 1.18f);
        } else if (rank == Rank::Jack || rank == Rank::Queen || rank == Rank::King) {
            if (opts.style == LowPolyStyle::Clockwork) {
                emitClockworkCourt(img, suit, rank, rng, opts);
            } else {
                emitClassicCourt(img, suit, rank, rng, opts);
            }
        } else {
            struct PipPos { float x, y; bool flip; };
            static const std::vector<PipPos> kLayouts[9] = {
                {{50, 36, false}, {50, 104, true}},
                {{50, 34, false}, {50, 70, false}, {50, 106, true}},
                {{34, 36, false}, {66, 36, false}, {34, 104, true}, {66, 104, true}},
                {{34, 36, false}, {66, 36, false}, {50, 70, false}, {34, 104, true}, {66, 104, true}},
                {{34, 34, false}, {66, 34, false}, {34, 70, false}, {66, 70, false}, {34, 106, true}, {66, 106, true}},
                {{34, 34, false}, {66, 34, false}, {50, 52, false}, {34, 70, false}, {66, 70, false}, {34, 106, true}, {66, 106, true}},
                {{34, 34, false}, {66, 34, false}, {50, 52, false}, {34, 70, false}, {66, 70, false}, {50, 88, true}, {34, 106, true}, {66, 106, true}},
                {{34, 30, false}, {66, 30, false}, {34, 56, false}, {66, 56, false}, {50, 70, false}, {34, 84, true}, {66, 84, true}, {34, 110, true}, {66, 110, true}},
                {{34, 30, false}, {66, 30, false}, {50, 43, false}, {34, 56, false}, {66, 56, false}, {34, 84, true}, {66, 84, true}, {50, 97, true}, {34, 110, true}, {66, 110, true}},
            };
            const int lIdx = static_cast<int>(rank) - 2;
            const auto tris = buildSuitTris(suit, suitRamp);
            for (const auto& pt : kLayouts[lIdx]) {
                emitTris(img, tris, pt.x, pt.y, 0.31f, pt.flip);
            }
        }
    }

    addCornerIndices(img, rank, suit, opts);
    return img;
}

VectorImage buildJokerVector(bool red, const LowPolyDeckOptions& opts) {
    VectorImage img{};
    img.width = 100.0f;
    img.height = 140.0f;

    if (opts.style == LowPolyStyle::War) {
        addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFFF4F2E6u);
        addPolyShape(img, {{100, 84}, {56, 140}, {100, 140}}, 0xFFE8E4D4u);
        DeterministicRng rng(opts.seed ^ (red ? 0x4A4F4B31u : 0x4A4F4B32u));
        const WarPalette p = makeWarPalette(red ? Suit::Hearts : Suit::Spades, opts.seed, rng);
        addWarChamferWindow(img, p, rng);

        const float lean = randRange(rng, -2.0f, 2.0f);
        // Asymmetric Warlock Jester figure
        addPolyShape(img, {{20, 124}, {26, 86}, {50, 78}, {50, 124}}, red ? p.suitDark : p.navyDark);
        addPolyShape(img, {{80, 124}, {74, 86}, {50, 78}, {50, 124}}, red ? p.suitBright : p.indigoMid);
        // Jagged jester collar
        addPolyShape(img, {{32, 82}, {40, 72}, {50, 86}, {60, 72}, {68, 82}, {58, 92}, {50, 88}, {42, 92}}, p.goldBright);
        // Shadow-split face
        addPolyShape(img, {{41 + lean, 42}, {50 + lean, 40}, {50 + lean, 68}, {42 + lean, 60}}, p.skinLight);
        addPolyShape(img, {{50 + lean, 40}, {59 + lean, 42}, {58 + lean, 60}, {50 + lean, 68}}, p.nearBlack);
        // 2-horn jester cap
        addPolyShape(img, {{39 + lean, 42}, {25, 22}, {46 + lean, 34}, {50 + lean, 40}}, red ? p.suitBright : p.steelLight);
        addPolyShape(img, {{50 + lean, 40}, {54 + lean, 34}, {75, 22}, {61 + lean, 42}}, p.navyDark);
        addPolyShape(img, {{25, 22}, {22, 26}, {27, 27}}, p.goldBright);
        addPolyShape(img, {{75, 22}, {78, 26}, {73, 27}}, p.goldBright);

        const uint32_t starC1 = red ? 0xFF9E1B24u : 0xFF1A1733u;
        const uint32_t starC2 = red ? 0xFFC8202Au : 0xFF3E445Au;
        addPolyShape(img, {{11, 8}, {9, 14}, {3, 16}, {9, 18}, {11, 24}}, starC1);
        addPolyShape(img, {{11, 8}, {13, 14}, {19, 16}, {13, 18}, {11, 24}}, starC2);
        addPolyShape(img, {{89, 116}, {87, 122}, {81, 124}, {87, 126}, {89, 132}}, starC1);
        addPolyShape(img, {{89, 116}, {91, 122}, {97, 124}, {91, 126}, {89, 132}}, starC2);
        return img;
    }

    addPlateAndBevel(img, opts.bg);

    const bool darkBg = (opts.bg == LowPolyBg::Dark);
    const Ramp4 mainRamp   = red ? Ramp4{0xFF7F1D1Du, 0xFF9E1B24u, 0xFFC8202Au, 0xFFEF4444u} : Ramp4{0xFF1E293Bu, 0xFF334155u, 0xFF64748Bu, 0xFF94A3B8u};
    const Ramp4 accentRamp = {0xFF0369A1u, 0xFF0284C7u, 0xFF38BDF8u, 0xFF7DD3FCu};
    const Ramp4 goldRamp   = {0xFF92400Eu, 0xFFB45309u, 0xFFD97706u, 0xFFF59E0Bu};
    const Ramp4 skinRamp   = {0xFFB9836Bu, 0xFFCF9982u, 0xFFE2B29Du, 0xFFF0CAB8u};

    std::vector<Tri3> tris;
    tris.push_back({{0, -14, 9}, {-14, -24, 5}, {-22, -6, 3}, mainRamp});
    tris.push_back({{0, -14, 9}, {22, -6, 3}, {14, -24, 5}, accentRamp});
    tris.push_back({{0, -14, 9}, {-6, -24, 6}, {0, -34, 4}, mainRamp});
    tris.push_back({{0, -14, 9}, {0, -34, 4}, {6, -24, 6}, accentRamp});
    tris.push_back({{0, -14, 9}, {-11, -2, 6}, {0, 12, 8}, accentRamp});
    tris.push_back({{0, -14, 9}, {0, 12, 8}, {11, -2, 6}, goldRamp});
    tris.push_back({{0, 12, 8}, {-11, -2, 6}, {-8, 8, 5}, skinRamp});
    tris.push_back({{0, 12, 8}, {8, 8, 5}, {11, -2, 6}, mainRamp});
    tris.push_back({{0, 14, 8}, {-22, 18, 3}, {0, 32, 4}, mainRamp});
    tris.push_back({{0, 14, 8}, {0, 32, 4}, {22, 18, 3}, goldRamp});
    emitTris(img, tris, 50.0f, 72.0f, 1.02f);

    const uint32_t starCol = red ? (darkBg ? 0xFFFB7185u : 0xFFC8202Au) : (darkBg ? 0xFFE2E8F0u : 0xFF1A1733u);
    addPolyShape(img, {{11, 8}, {13, 14}, {19, 16}, {13, 18}, {11, 24}, {9, 18}, {3, 16}, {9, 14}}, starCol);
    return img;
}

// Helper: 4-way mirrored jittered-grid triangulated low-poly mosaic (used by SolPoly & SolPoly Dark)
void emitMirroredLowPolyMosaic(
    VectorImage& img,
    float minX, float minY, float maxX, float maxY,
    int colsHalf, int rowsHalf,
    const Ramp4& ramp,
    DeterministicRng& rng
) {
    const float midX = 0.5f * (minX + maxX);
    const float midY = 0.5f * (minY + maxY);
    const float cellW = (midX - minX) / static_cast<float>(colsHalf);
    const float cellH = (midY - minY) / static_cast<float>(rowsHalf);

    // Build quadrant vertex grid (0..colsHalf x 0..rowsHalf)
    std::vector<std::vector<std::pair<float, float>>> grid(
        static_cast<size_t>(rowsHalf + 1),
        std::vector<std::pair<float, float>>(static_cast<size_t>(colsHalf + 1))
    );
    for (int r = 0; r <= rowsHalf; ++r) {
        for (int c = 0; c <= colsHalf; ++c) {
            float x = minX + static_cast<float>(c) * cellW;
            float y = minY + static_cast<float>(r) * cellH;
            if (c > 0 && c < colsHalf) x += randRange(rng, -cellW * 0.34f, cellW * 0.34f);
            if (r > 0 && r < rowsHalf) y += randRange(rng, -cellH * 0.34f, cellH * 0.34f);
            grid[static_cast<size_t>(r)][static_cast<size_t>(c)] = {x, y};
        }
    }

    const float lightAng = randRange(rng, 0.0f, 2.0f * kPi);
    const float lx = std::cos(lightAng);
    const float ly = std::sin(lightAng);

    auto emitQuadMirrored = [&](std::pair<float, float> a, std::pair<float, float> b, std::pair<float, float> c) {
        const float cx = (a.first + b.first + c.first) / 3.0f;
        const float cy = (a.second + b.second + c.second) / 3.0f;
        const float nx = (cx - minX) / std::max(midX - minX, 1.0f);
        const float ny = (cy - minY) / std::max(midY - minY, 1.0f);
        const float lit = 0.5f + 0.35f * (nx * lx + ny * ly) + randRange(rng, -0.22f, 0.22f);
        const int idx = std::clamp(static_cast<int>(lit * 4.0f), 0, 3);
        const uint32_t col = ramp[static_cast<size_t>(idx)];

        // Mirror across all 4 quadrants around (midX, midY)
        const float sx[2] = {1.0f, -1.0f};
        const float sy[2] = {1.0f, -1.0f};
        for (float mx : sx) {
            for (float my : sy) {
                auto mp = [&](std::pair<float, float> p) -> std::pair<float, float> {
                    return {midX + (p.first - midX) * mx, midY + (p.second - midY) * my};
                };
                addPolyShape(img, {mp(a), mp(b), mp(c)}, col);
            }
        }
    };

    for (int r = 0; r < rowsHalf; ++r) {
        for (int c = 0; c < colsHalf; ++c) {
            auto p00 = grid[static_cast<size_t>(r)][static_cast<size_t>(c)];
            auto p10 = grid[static_cast<size_t>(r)][static_cast<size_t>(c + 1)];
            auto p01 = grid[static_cast<size_t>(r + 1)][static_cast<size_t>(c)];
            auto p11 = grid[static_cast<size_t>(r + 1)][static_cast<size_t>(c + 1)];
            if ((r + c + randInt(rng, 0, 1)) % 2 == 0) {
                emitQuadMirrored(p00, p10, p11);
                emitQuadMirrored(p00, p11, p01);
            } else {
                emitQuadMirrored(p00, p10, p01);
                emitQuadMirrored(p10, p11, p01);
            }
        }
    }
}

// Helper: 3D-projected faceted crystal/octahedron centerpiece
void emitFacetedCrystalMedallion(
    VectorImage& img,
    float cx, float cy,
    float radiusX, float radiusY,
    int sides,
    float yawDeg,
    float pitchDeg,
    const Ramp4& ramp
) {
    std::vector<Tri3> tris;
    const float zDepth = radiusX * 0.85f;
    auto xform = [&](Vec3 p) -> Vec3 {
        return rotX(rotY(p, yawDeg), pitchDeg);
    };
    Vec3 topV = xform({0.0f, -radiusY, 0.0f});
    Vec3 botV = xform({0.0f,  radiusY, 0.0f});

    for (int i = 0; i < sides; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(sides)) * 2.0f * kPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(sides)) * 2.0f * kPi;
        Vec3 r0 = xform({std::cos(a0) * radiusX, 0.0f, std::sin(a0) * zDepth});
        Vec3 r1 = xform({std::cos(a1) * radiusX, 0.0f, std::sin(a1) * zDepth});
        tris.push_back({topV, r0, r1, ramp});
        tris.push_back({botV, r1, r0, ramp});
    }
    emitTris(img, tris, cx, cy, 1.0f);
}

VectorImage buildCardBackVector(const LowPolyDeckOptions& opts) {
    VectorImage img{};
    img.width = 100.0f;
    img.height = 140.0f;

    DeterministicRng rng(opts.seed ^ 0x4241434Bu);

    if (opts.style == LowPolyStyle::War) {
        const WarPalette p = makeWarPalette(Suit::Hearts, opts.seed, rng);
        const float tilt = randRange(rng, -4.0f, 4.0f);

        addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, p.nearBlack);
        addPolyShape(img, {{100, 84}, {56, 140}, {100, 140}}, p.navyDark);
        // Chamfered inner slate-purple window
        addPolyShape(img, {{16, 10}, {90, 10}, {90, 122}, {82, 130}, {10, 130}, {10, 16}}, p.windowBg);
        // Atmospheric diagonal light beam & monolith silhouette
        addPolyShape(img, {{34 + tilt, 10}, {72 + tilt, 10}, {46, 130}, {14, 130}}, p.windowBeam);
        addPolyShape(img, {{50 + tilt * 0.5f, 24}, {26, 70}, {50, 70}}, p.indigoMid);
        addPolyShape(img, {{50 + tilt * 0.5f, 24}, {74, 70}, {50, 70}}, p.navyDark);
        addPolyShape(img, {{26, 70}, {50, 70}, {50 - tilt * 0.5f, 116}}, p.steelShadow);
        addPolyShape(img, {{74, 70}, {50, 70}, {50 - tilt * 0.5f, 116}}, p.nearBlack);
        // Glowing crimson & gold war-shard core
        addPolyShape(img, {{50, 44}, {36, 70}, {50, 70}}, p.suitBright);
        addPolyShape(img, {{50, 44}, {64, 70}, {50, 70}}, p.suitDark);
        addPolyShape(img, {{36, 70}, {50, 70}, {50, 96}}, p.goldBright);
        addPolyShape(img, {{64, 70}, {50, 70}, {50, 96}}, p.goldShadow);
        addStrokeRectShape(img, 6.0f, 6.3f, 88.0f, 127.4f, p.steelShadow, 0.65f);
        return img;
    }

    // -----------------------------------------------------------------------
    // 1. SolPoly Classic (LowPolyStyle::Clockwork): Horological Brass & Gears
    // -----------------------------------------------------------------------
    if (opts.style == LowPolyStyle::Clockwork) {
        // Seed-selected horological palette (Obsidian/Brass, Crimson/Gold, Navy/Bronze)
        const int palIdx = randInt(rng, 0, 2);
        const uint32_t bgDark  = (palIdx == 0) ? 0xFF18181Bu : (palIdx == 1 ? 0xFF450A0Au : 0xFF0F1E36u);
        const uint32_t bgMid   = (palIdx == 0) ? 0xFF27272Au : (palIdx == 1 ? 0xFF6B1418u : 0xFF1A3154u);
        const uint32_t brass   = 0xFFD49B35u;
        const uint32_t brassHi = 0xFFF3C969u;
        const uint32_t brassLo = 0xFF9A6B1Au;
        const uint32_t ink     = 0xFF09090Bu;

        // Warm ivory card stock border
        addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFFF5EFE6u);
        addPolyShape(img, {{100, 92}, {62, 140}, {100, 140}}, 0xFFEAE2D3u);

        // Inner panel with subtle diagonal faceted split
        addPolyShape(img, {{7, 7}, {93, 7}, {93, 133}, {7, 133}}, bgDark);
        addPolyShape(img, {{7, 7}, {93, 7}, {7, 133}}, bgMid);

        // Seed-randomized radial clockwork spokes behind gears
        const int spokeCount = randInt(rng, 8, 12);
        const float spokePhase = randRange(rng, 0.0f, 360.0f / static_cast<float>(spokeCount));
        for (int i = 0; i < spokeCount; ++i) {
            const float ang = (spokePhase + static_cast<float>(i) * (360.0f / static_cast<float>(spokeCount))) * (kPi / 180.0f);
            addLineShape(img, 50.0f, 70.0f, 50.0f + std::cos(ang) * 38.0f, 70.0f + std::sin(ang) * 48.0f, brassLo, 0.45f);
        }

        // Mirrored top/bottom satellite pinion gears (seed-varied teeth & vertical offset)
        const int satTeeth = randInt(rng, 8, 12);
        const float satOffsetY = randRange(rng, 32.0f, 39.0f);
        const float satPhase = randRange(rng, 0.0f, 45.0f);
        for (float dir : {-1.0f, 1.0f}) {
            const float gy = 70.0f + dir * satOffsetY;
            addPolyEx(img, makeGearPts(50.0f, gy, 13.5f, 10.5f, satTeeth, satPhase), brassLo, ink, 0.45f);
            addCircleApprox(img, 50.0f, gy, 7.2f, bgDark, brass, 0.55f, 16);
            addCircleApprox(img, 50.0f, gy, 3.0f, brassHi, ink, 0.35f, 12);
        }

        // Main central horological gear (seed-varied teeth & phase)
        const int mainTeeth = randInt(rng, 14, 20);
        const float mainPhase = randRange(rng, 0.0f, 30.0f);
        addPolyEx(img, makeGearPts(50.0f, 70.0f, 26.5f, 22.0f, mainTeeth, mainPhase), brass, ink, 0.55f);
        addCircleApprox(img, 50.0f, 70.0f, 17.5f, bgDark, brassHi, 0.70f, 24);
        addCircleApprox(img, 50.0f, 70.0f, 13.5f, bgMid, brassLo, 0.45f, 20);

        // 3D-projected faceted brass astrolabe gem in center
        const Ramp4 brassRamp = {brassLo, brass, brassHi, 0xFFFFF0B8u};
        const int coreSides = randInt(rng, 4, 6);
        emitFacetedCrystalMedallion(img, 50.0f, 70.0f, 10.5f, 12.5f, coreSides,
                                    randRange(rng, -22.0f, 22.0f), randRange(rng, -15.0f, 15.0f), brassRamp);

        // 4-way mirrored chamfered brass corner brackets
        for (float sx : {-1.0f, 1.0f}) {
            for (float sy : {-1.0f, 1.0f}) {
                const float x0 = 50.0f + sx * 41.0f;
                const float y0 = 70.0f + sy * 61.0f;
                addPolyEx(img, {
                    {x0, y0},
                    {x0 - sx * 12.0f, y0},
                    {x0 - sx * 8.0f, y0 - sy * 4.0f},
                    {x0 - sx * 4.0f, y0 - sy * 8.0f},
                    {x0, y0 - sy * 12.0f}
                }, brass, ink, 0.40f);
            }
        }

        addStrokeRectShape(img, 7.0f, 7.0f, 86.0f, 126.0f, brass, 1.1f);
        addStrokeRectShape(img, 10.0f, 10.0f, 80.0f, 120.0f, brassLo, 0.5f);
        return img;
    }

    // -----------------------------------------------------------------------
    // 2. SolPoly Mobile (LowPolyStyle::Mobile): Bold High-Contrast Graphic
    // -----------------------------------------------------------------------
    if (opts.style == LowPolyStyle::Mobile) {
        const int palIdx = randInt(rng, 0, 3);
        const uint32_t fieldMain = (palIdx == 0) ? 0xFFCC1D24u : (palIdx == 1 ? 0xFF14161Eu : (palIdx == 2 ? 0xFF1D4ED8u : 0xFF065F46u));
        const uint32_t fieldDark = (palIdx == 0) ? 0xFF991B1Bu : (palIdx == 1 ? 0xFF0A0C12u : (palIdx == 2 ? 0xFF1E3A8Au : 0xFF064E3Bu));
        const uint32_t watermark = (palIdx == 0) ? 0xFFB0161Cu : (palIdx == 1 ? 0xFF1E2230u : (palIdx == 2 ? 0xFF1A42B8u : 0xFF05523Cu));

        // Clean crisp white card border
        addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFFF8FAFCu);
        // Bold inner graphic panel
        addPolyShape(img, {{6.5f, 6.5f}, {93.5f, 6.5f}, {93.5f, 133.5f}, {6.5f, 133.5f}}, fieldMain);

        // Seed-tilted high-contrast diagonal slash
        const float slashShift = randRange(rng, -14.0f, 14.0f);
        addPolyShape(img, {
            {6.5f, 78.0f + slashShift},
            {93.5f, 52.0f - slashShift},
            {93.5f, 133.5f},
            {6.5f, 133.5f}
        }, fieldDark);

        // Oversized tilted graphic suit watermark (signature Mobile deck motif)
        const Suit wmSuit = static_cast<Suit>(randInt(rng, 0, 3));
        const float wmTilt = randRange(rng, -14.0f, 14.0f);
        addClassicSuitShape(img, wmSuit, 50.0f, 70.0f, 44.0f, wmTilt, watermark);

        // Bold monochrome silver/slate 3D faceted diamond badge in center
        const Ramp4 silverRamp = {0xFF475569u, 0xFF94A3B8u, 0xFFE2E8F0u, 0xFFFFFFFFu};
        const int sides = randInt(rng, 4, 6);
        // Crisp dark halo diamond behind badge
        addPolyShape(img, {{50.0f, 44.0f}, {70.0f, 70.0f}, {50.0f, 96.0f}, {30.0f, 70.0f}}, 0xFF0F172Au);
        emitFacetedCrystalMedallion(img, 50.0f, 70.0f, 15.0f, 20.0f, sides,
                                    randRange(rng, -25.0f, 25.0f), randRange(rng, -15.0f, 15.0f), silverRamp);

        addStrokeRectShape(img, 6.5f, 6.5f, 87.0f, 127.0f, 0xFF0F172Au, 1.0f);
        addStrokeRectShape(img, 10.5f, 10.5f, 79.0f, 119.0f, 0xFFF8FAFCu, 0.7f);
        return img;
    }

    // -----------------------------------------------------------------------
    // 3. SolPoly (Classic + White/Ivory) & SolPoly Dark (Classic + Dark):
    //    4-way Mirrored Procedural Low-Poly Origami Mosaic + 3D Crystal Core
    // -----------------------------------------------------------------------
    if (opts.style == LowPolyStyle::Classic) {
        const bool isDark = (opts.bg == LowPolyBg::Dark);
        const int palIdx = randInt(rng, 0, 3);

        if (isDark) {
            // SolPoly Dark: Obsidian border + midnight slate mosaic + glowing jewel crystal core
            addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFF141821u);
            const Ramp4 bgRamp = {0xFF181E2Bu, 0xFF222A3Bu, 0xFF2E394Fu, 0xFF3D4B66u};
            const int cols = randInt(rng, 3, 4);
            const int rows = randInt(rng, 4, 6);
            emitMirroredLowPolyMosaic(img, 7.0f, 7.0f, 93.0f, 133.0f, cols, rows, bgRamp, rng);

            // Seed-selected luminous jewel ramp (Cyan, Amber, Emerald, Amethyst)
            static const Ramp4 kJewels[4] = {
                {0xFF0369A1u, 0xFF0284C7u, 0xFF38BDF8u, 0xFFBAE6FDu},
                {0xFF92400Eu, 0xFFD97706u, 0xFFFBBF24u, 0xFFFEF08Au},
                {0xFF065F46u, 0xFF059669u, 0xFF34D399u, 0xFFA7F3D0u},
                {0xFF5B21B6u, 0xFF7C3AEDu, 0xFFA78BFAu, 0xFFDDD6FEu}
            };
            const Ramp4& gemRamp = kJewels[palIdx];
            const uint32_t borderCol = gemRamp[2];

            // Dark chamfered backing diamond + glowing 3D crystal core
            addPolyShape(img, {{50.0f, 38.0f}, {72.0f, 70.0f}, {50.0f, 102.0f}, {28.0f, 70.0f}}, 0xFF0F131Cu);
            emitFacetedCrystalMedallion(img, 50.0f, 70.0f, 16.0f, 24.0f, randInt(rng, 4, 6),
                                        randRange(rng, -28.0f, 28.0f), randRange(rng, -18.0f, 18.0f), gemRamp);

            addStrokeRectShape(img, 7.0f, 7.0f, 86.0f, 126.0f, borderCol, 0.85f);
            addStrokeRectShape(img, 10.5f, 10.5f, 79.0f, 119.0f, 0xFF334155u, 0.55f);
        } else {
            // SolPoly (White): Crisp Alabaster border + architectural color mosaic + 3D prism core
            addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFFF4F2ECu);
            addPolyShape(img, {{100, 88}, {58, 140}, {100, 140}}, 0xFFE6E2D8u);

            // Seed-selected crisp architectural palette (Arctic Sapphire, Vermilion Coral, Mint Slate, Warm Gold)
            static const Ramp4 kLightMosaics[4] = {
                {0xFF1E3A8Au, 0xFF2563EBu, 0xFF60A5FAu, 0xFFDBEAFEu},
                {0xFF991B1Bu, 0xFFDC2626u, 0xFFF87171u, 0xFFFEE2E2u},
                {0xFF0F766Eu, 0xFF14B8A6u, 0xFF5EEAD4u, 0xFFCCFBF1u},
                {0xFF334155u, 0xFF475569u, 0xFF94A3B8u, 0xFFE2E8F0u}
            };
            const Ramp4& bgRamp = kLightMosaics[palIdx];
            const int cols = randInt(rng, 3, 4);
            const int rows = randInt(rng, 4, 6);
            emitMirroredLowPolyMosaic(img, 7.5f, 7.5f, 92.5f, 132.5f, cols, rows, bgRamp, rng);

            // Crisp alabaster diamond frame + 3D faceted centerpiece
            addPolyEx(img, {{50.0f, 38.0f}, {72.0f, 70.0f}, {50.0f, 102.0f}, {28.0f, 70.0f}}, 0xFFF8FAFCu, bgRamp[0], 0.8f);
            emitFacetedCrystalMedallion(img, 50.0f, 70.0f, 15.0f, 23.0f, randInt(rng, 4, 6),
                                        randRange(rng, -28.0f, 28.0f), randRange(rng, -18.0f, 18.0f), bgRamp);

            addStrokeRectShape(img, 7.5f, 7.5f, 85.0f, 125.0f, bgRamp[0], 0.9f);
            addStrokeRectShape(img, 10.5f, 10.5f, 79.0f, 119.0f, 0xFFF8FAFCu, 0.55f);
        }
        return img;
    }

    // -----------------------------------------------------------------------
    // 4. Scoundrel Prism (LowPolyStyle::Scoundrel): Dungeon Basalt, Crossed
    //    Forged Blades & Torchlight Relic Crystal (Seed-Randomized)
    // -----------------------------------------------------------------------
    if (opts.style == LowPolyStyle::Scoundrel) {
        const int palIdx = randInt(rng, 0, 3);
        // Curated dungeon torchlight crystal ramps (Torch Amber, Bloodstone Ruby, Venom Jade, Cursed Amethyst)
        static const Ramp4 kRelics[4] = {
            {0xFF92400Eu, 0xFFD97706u, 0xFFFBBF24u, 0xFFFEF3C7u},
            {0xFF7F1D1Du, 0xFFDC2626u, 0xFFF87171u, 0xFFFECACAu},
            {0xFF065F46u, 0xFF059669u, 0xFF34D399u, 0xFFA7F3D0u},
            {0xFF581C87u, 0xFF9333EAu, 0xFFC084FCu, 0xFFF3E8FFu}
        };
        const Ramp4& relicRamp = kRelics[palIdx];
        const uint32_t ironHi   = 0xFF64748Bu;
        const uint32_t ironMid  = 0xFF334155u;
        const uint32_t ironDark = 0xFF1E293Bu;

        // Deep basalt obsidian outer frame
        addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFF10131Bu);
        addPolyShape(img, {{100, 86}, {54, 140}, {100, 140}}, 0xFF0A0C12u);

        // 4-way mirrored jagged basalt dungeon mosaic
        const Ramp4 stoneRamp = {0xFF141924u, 0xFF1C2332u, 0xFF252E42u, 0xFF2F3A52u};
        const int cols = randInt(rng, 3, 4);
        const int rows = randInt(rng, 4, 6);
        emitMirroredLowPolyMosaic(img, 7.0f, 7.0f, 93.0f, 133.0f, cols, rows, stoneRamp, rng);

        // Seed-angled torchlight beam cutting across the chamber
        const float beamTilt = randRange(rng, -12.0f, 12.0f);
        addPolyShape(img, {
            {32.0f + beamTilt, 7.0f},
            {68.0f + beamTilt, 7.0f},
            {62.0f - beamTilt, 133.0f},
            {38.0f - beamTilt, 133.0f}
        }, 0x18FBBF24u);

        // Crossed forged-steel low-poly daggers behind the central crystal (seed-varied splay angle)
        const float bladeSpread = randRange(rng, 28.0f, 38.0f) * (kPi / 180.0f);
        const float bLen = 40.0f;
        const float bWid = 4.6f;
        for (float dir : {-1.0f, 1.0f}) {
            const float ang = dir * bladeSpread;
            const float dx = std::sin(ang);
            const float dy = -std::cos(ang);
            const float px = -dy;
            const float py = dx;
            // Upper & lower blade halves (split down spine for 3D bevel)
            addPolyShape(img, {
                {50.0f + dx * bLen, 70.0f + dy * bLen},
                {50.0f + px * bWid, 70.0f + py * bWid},
                {50.0f - dx * bLen, 70.0f - dy * bLen}
            }, ironHi);
            addPolyShape(img, {
                {50.0f + dx * bLen, 70.0f + dy * bLen},
                {50.0f - px * bWid, 70.0f - py * bWid},
                {50.0f - dx * bLen, 70.0f - dy * bLen}
            }, ironMid);
        }

        // Chamfered obsidian shield backing + glowing 3D-projected Scoundrel Relic Crystal
        addPolyEx(img, {{50.0f, 36.0f}, {74.0f, 70.0f}, {50.0f, 104.0f}, {26.0f, 70.0f}}, 0xFF0E121Bu, relicRamp[1], 0.9f);
        emitFacetedCrystalMedallion(img, 50.0f, 70.0f, 17.0f, 25.0f, randInt(rng, 4, 6),
                                    randRange(rng, -26.0f, 26.0f), randRange(rng, -16.0f, 16.0f), relicRamp);

        // 4-way mirrored ironbound dungeon corner brackets + ember rivets
        for (float sx : {-1.0f, 1.0f}) {
            for (float sy : {-1.0f, 1.0f}) {
                const float x0 = 50.0f + sx * 43.0f;
                const float y0 = 70.0f + sy * 63.0f;
                addPolyEx(img, {
                    {x0, y0},
                    {x0 - sx * 14.0f, y0},
                    {x0 - sx * 10.0f, y0 - sy * 5.0f},
                    {x0 - sx * 5.0f,  y0 - sy * 10.0f},
                    {x0, y0 - sy * 14.0f}
                }, ironDark, ironHi, 0.55f);
                addCircleApprox(img, x0 - sx * 5.2f, y0 - sy * 5.2f, 1.6f, relicRamp[2], 0, 0.0f, 8);
            }
        }

        addStrokeRectShape(img, 7.0f, 7.0f, 86.0f, 126.0f, ironHi, 0.95f);
        addStrokeRectShape(img, 10.5f, 10.5f, 79.0f, 119.0f, relicRamp[1], 0.65f);
        return img;
    }

    // Fallback for Memory low-poly deck
    const bool isMemoryGem = (opts.style == LowPolyStyle::Memory);
    const uint32_t gemBright = isMemoryGem ? 0xFF34D399u : 0xFF38BDF8u;
    const uint32_t gemMid    = isMemoryGem ? 0xFF059669u : 0xFF0284C7u;
    const uint32_t gemLight  = isMemoryGem ? 0xFFA7F3D0u : 0xFF7DD3FCu;
    const uint32_t gemDark   = isMemoryGem ? 0xFF065F46u : 0xFF0369A1u;

    addPolyShape(img, {{0, 0}, {100, 0}, {100, 140}, {0, 140}}, 0xFF1E2430u);
    addPolyShape(img, {{100, 84}, {56, 140}, {100, 140}}, 0xFF171C26u);
    addPolyShape(img, {{50, 24}, {22, 70}, {50, 70}}, 0xFF334155u);
    addPolyShape(img, {{50, 24}, {78, 70}, {50, 70}}, 0xFF1E293Bu);
    addPolyShape(img, {{22, 70}, {50, 70}, {50, 116}}, 0xFF475569u);
    addPolyShape(img, {{78, 70}, {50, 70}, {50, 116}}, 0xFF0F172Au);
    addPolyShape(img, {{50, 48}, {36, 70}, {50, 70}}, gemBright);
    addPolyShape(img, {{50, 48}, {64, 70}, {50, 70}}, gemMid);
    addPolyShape(img, {{36, 70}, {50, 70}, {50, 92}}, gemLight);
    addPolyShape(img, {{64, 70}, {50, 70}, {50, 92}}, gemDark);
    addStrokeRectShape(img, 6.0f, 6.3f, 88.0f, 127.4f, 0xFF334155u, 0.55f);
    return img;
}

void rasterizeVectorIntoTexture(const VectorImage& img, int cardW, int cardH, CardTexture& tex) {
    tex.width = cardW;
    tex.height = cardH;
    tex.rgba.assign(static_cast<size_t>(cardW * cardH * 4), 0);
    VectorRasterizer::rasterizeToRgba(
        img,
        tex.rgba.data(),
        cardW,
        cardH,
        0.0f,
        0.0f,
        static_cast<float>(cardW),
        static_cast<float>(cardH),
        false
    );
}

} // namespace

bool LowPolyDeckGenerator::generateDeckTextures(
    const LowPolyDeckOptions& opts,
    int cardW,
    int cardH,
    std::array<CardTexture, CardTextureCache::kTotalTextures>& outBase,
    const ProgressCallback& onProgress
) {
    const Suit suits[4] = {Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs};

    for (int sIdx = 0; sIdx < 4; ++sIdx) {
        Suit s = suits[sIdx];
        for (int rIdx = 1; rIdx <= 13; ++rIdx) {
            const int cardNum = sIdx * 13 + rIdx;
            if (onProgress) {
                const float frac = 0.05f + 0.88f * (static_cast<float>(cardNum - 1) / static_cast<float>(CardTextureCache::kTotalTextures));
                if (!onProgress(frac, "Generating Low-Poly Card " + std::to_string(cardNum) + "/53...")) {
                    return false;
                }
            }
            Rank r = static_cast<Rank>(rIdx);
            const size_t destIdx = CardTextureCache::cardIndex(s, r);
            const uint32_t cardSeed = opts.seed ^ (static_cast<uint32_t>(sIdx) * 0x9E3779B9u + static_cast<uint32_t>(rIdx) * 0x85EBCA6Bu);
            VectorImage vec = buildCardFaceVector(s, r, opts, cardSeed);
            rasterizeVectorIntoTexture(vec, cardW, cardH, outBase[destIdx]);
        }
    }

    // Jokers
    rasterizeVectorIntoTexture(buildJokerVector(false, opts), cardW, cardH, outBase[CardTextureCache::kJokerBlackIndex]);
    rasterizeVectorIntoTexture(buildJokerVector(true, opts), cardW, cardH, outBase[CardTextureCache::kJokerRedIndex]);

    // Card Back
    rasterizeVectorIntoTexture(buildCardBackVector(opts), cardW, cardH, outBase[CardTextureCache::kCardBackIndex]);

    if (onProgress) onProgress(1.0f, "Low-Poly deck ready.");
    return true;
}

bool LowPolyDeckGenerator::generateCardBackTexture(
    const LowPolyDeckOptions& opts,
    int cardW,
    int cardH,
    CardTexture& outTex
) {
    if (cardW <= 0 || cardH <= 0) return false;
    rasterizeVectorIntoTexture(buildCardBackVector(opts), cardW, cardH, outTex);
    return !outTex.rgba.empty();
}

} // namespace solengine
