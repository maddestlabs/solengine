#include "solengine/lowpoly_deck_generator.hpp"
#include "solengine/vector_binary.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <random>
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

float randRange(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

int randInt(std::mt19937& rng, int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
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

void addRankGlyphShape(VectorImage& img, Rank rank, float cx, float cy, float h, uint32_t argb, bool rotate180) {
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

void addCornerIndices(VectorImage& img, Rank rank, Suit suit, const LowPolyDeckOptions& opts) {
    const bool darkBg = (opts.bg == LowPolyBg::Dark);
    const bool isRed = (suit == Suit::Hearts || suit == Suit::Diamonds);
    const uint32_t col = isRed
        ? (darkBg ? 0xFFF2D8D5u : 0xFF9E1B24u)
        : (darkBg ? 0xFFE8DFD1u : 0xFF1A1733u);

    addRankGlyphShape(img, rank, 11.0f, 14.0f, 10.5f, col, false);
    addCornerSuit(img, suit, 11.0f, 26.0f, 8.2f, darkBg, false);
    if (!opts.clearBottomRight) {
        addRankGlyphShape(img, rank, 11.0f, 14.0f, 10.5f, col, true);
        addCornerSuit(img, suit, 11.0f, 26.0f, 8.2f, darkBg, true);
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

void emitClassicCourt(VectorImage& img, Suit suit, Rank rank, std::mt19937& rng, const LowPolyDeckOptions& opts) {
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
void emitScoundrelWeapon(VectorImage& img, int rnum, std::mt19937& rng) {
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

void emitScoundrelPotion(VectorImage& img, int rnum, std::mt19937& rng) {
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

void emitScoundrelMonster(VectorImage& img, Suit suit, int rnum, std::mt19937& rng) {
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

VectorImage buildCardFaceVector(Suit suit, Rank rank, const LowPolyDeckOptions& opts, uint32_t cardSeed) {
    VectorImage img{};
    img.width = 100.0f;
    img.height = 140.0f;

    addPlateAndBevel(img, opts.bg);

    std::mt19937 rng(cardSeed);
    if (opts.style == LowPolyStyle::Scoundrel) {
        const int rn = rankToInt(rank);
        if (suit == Suit::Diamonds) {
            emitScoundrelWeapon(img, rn, rng);
        } else if (suit == Suit::Hearts) {
            emitScoundrelPotion(img, rn, rng);
        } else {
            emitScoundrelMonster(img, suit, rn, rng);
        }
    } else {
        const bool darkBg = (opts.bg == LowPolyBg::Dark);
        const Ramp4 blackRamp = darkBg
            ? Ramp4{0xFF334155u, 0xFF475569u, 0xFF94A3B8u, 0xFFE2E8F0u}
            : Ramp4{0xFF0F172Au, 0xFF1E293Bu, 0xFF334155u, 0xFF64748Bu};
        const Ramp4 redRamp = {0xFF7F1D1Du, 0xFF9E1B24u, 0xFFC8202Au, 0xFFEF4444u};
        const Ramp4& suitRamp = (suit == Suit::Hearts || suit == Suit::Diamonds) ? redRamp : blackRamp;

        if (rank == Rank::Ace) {
            emitTris(img, buildSuitTris(suit, suitRamp), 50.0f, 70.0f, 1.18f);
        } else if (rank == Rank::Jack || rank == Rank::Queen || rank == Rank::King) {
            emitClassicCourt(img, suit, rank, rng, opts);
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

VectorImage buildCardBackVector(const LowPolyDeckOptions& opts) {
    VectorImage img{};
    img.width = 100.0f;
    img.height = 140.0f;

    const bool isScoundrel = (opts.style == LowPolyStyle::Scoundrel);
    const uint32_t gemBright = isScoundrel ? 0xFFFBBF24u : 0xFF38BDF8u;
    const uint32_t gemMid    = isScoundrel ? 0xFFD97706u : 0xFF0284C7u;
    const uint32_t gemLight  = isScoundrel ? 0xFFFDE68Au : 0xFF7DD3FCu;
    const uint32_t gemDark   = isScoundrel ? 0xFF92400Eu : 0xFF0369A1u;

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

} // namespace solengine
