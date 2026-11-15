#include "solengine/vector_binary.hpp"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <unordered_map>

namespace solengine {

namespace {

inline uint16_t readU16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readU32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline float readF32(const uint8_t* p) noexcept {
    uint32_t u = readU32(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}

std::vector<uint8_t> decodeBase64(const std::string& input) {
    static constexpr int8_t kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<uint8_t> out;
    out.reserve((input.size() * 3) / 4);
    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        int8_t d = kDec[c];
        if (d == -1) continue;
        val = (val << 6) + static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

struct Point2D {
    float x{0.0f};
    float y{0.0f};
};

struct Edge {
    float x0, y0, x1, y1;
    int dir; // +1 if y1 > y0, -1 if y1 < y0
};

void flattenCubic(float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3,
                  float tolSq, int depth, std::vector<Point2D>& outPts) {
    if (depth < 10) {
        float dx = x3 - x0;
        float dy = y3 - y0;
        float d2 = std::abs((x1 - x3) * dy - (y1 - y3) * dx);
        float d3 = std::abs((x2 - x3) * dy - (y2 - y3) * dx);
        float lenSq = dx * dx + dy * dy;
        float maxD = d2 + d3;
        if (maxD * maxD > tolSq * (lenSq + 1e-4f)) {
            float x01 = (x0 + x1) * 0.5f, y01 = (y0 + y1) * 0.5f;
            float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
            float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
            float x012 = (x01 + x12) * 0.5f, y012 = (y01 + y12) * 0.5f;
            float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
            float x0123 = (x012 + x123) * 0.5f, y0123 = (y012 + y123) * 0.5f;
            flattenCubic(x0, y0, x01, y01, x012, y012, x0123, y0123, tolSq, depth + 1, outPts);
            flattenCubic(x0123, y0123, x123, y123, x23, y23, x3, y3, tolSq, depth + 1, outPts);
            return;
        }
    }
    outPts.push_back({x3, y3});
}

void addEdge(float x0, float y0, float x1, float y1, std::vector<Edge>& edges) {
    if (std::abs(y1 - y0) < 1e-5f) return;
    if (y0 < y1) {
        edges.push_back({x0, y0, x1, y1, 1});
    } else {
        edges.push_back({x1, y1, x0, y0, -1});
    }
}

void buildShapeGeometry(const VectorShape& shape, float tx, float ty, float scale,
                        std::vector<Edge>& outEdges,
                        std::vector<std::vector<Point2D>>& outPolylines,
                        bool needPolylines) {
    outEdges.clear();
    if (needPolylines) {
        outPolylines.clear();
    }
    constexpr float kTolSq = 0.20f * 0.20f;
    thread_local static std::vector<Point2D> sPts;

    for (const auto& path : shape.paths) {
        if (path.verbs.empty()) continue;
        sPts.clear();
        float startX = 0.0f, startY = 0.0f;
        float curX = 0.0f, curY = 0.0f;
        bool hasSubpath = false;

        auto flushSubpath = [&](bool closePath) {
            if (!hasSubpath || sPts.size() < 2) return;
            if (closePath) {
                if (std::abs(sPts.back().x - startX) > 1e-4f || std::abs(sPts.back().y - startY) > 1e-4f) {
                    sPts.push_back({startX, startY});
                }
            }
            if (shape.flags & kVectorHasFill) {
                for (size_t i = 0; i + 1 < sPts.size(); ++i) {
                    addEdge(sPts[i].x, sPts[i].y, sPts[i + 1].x, sPts[i + 1].y, outEdges);
                }
                if (!closePath && (std::abs(sPts.back().x - startX) > 1e-4f || std::abs(sPts.back().y - startY) > 1e-4f)) {
                    addEdge(sPts.back().x, sPts.back().y, startX, startY, outEdges);
                }
            }
            if (needPolylines) {
                outPolylines.push_back(sPts);
            }
        };

        for (const auto& v : path.verbs) {
            switch (v.type) {
                case VectorVerbType::MoveTo: {
                    if (hasSubpath) {
                        flushSubpath(path.closed);
                        sPts.clear();
                    }
                    curX = tx + v.x0 * scale;
                    curY = ty + v.y0 * scale;
                    startX = curX;
                    startY = curY;
                    sPts.push_back({curX, curY});
                    hasSubpath = true;
                    break;
                }
                case VectorVerbType::LineTo: {
                    float nx = tx + v.x0 * scale;
                    float ny = ty + v.y0 * scale;
                    if (!hasSubpath) {
                        startX = nx; startY = ny;
                        hasSubpath = true;
                    }
                    sPts.push_back({nx, ny});
                    curX = nx; curY = ny;
                    break;
                }
                case VectorVerbType::CubicTo: {
                    float cx1 = tx + v.x0 * scale;
                    float cy1 = ty + v.y0 * scale;
                    float cx2 = tx + v.x1 * scale;
                    float cy2 = ty + v.y1 * scale;
                    float nx = tx + v.x2 * scale;
                    float ny = ty + v.y2 * scale;
                    if (!hasSubpath) {
                        startX = curX; startY = curY;
                        sPts.push_back({curX, curY});
                        hasSubpath = true;
                    }
                    flattenCubic(curX, curY, cx1, cy1, cx2, cy2, nx, ny, kTolSq, 0, sPts);
                    curX = nx; curY = ny;
                    break;
                }
            }
        }
        if (hasSubpath) {
            flushSubpath(path.closed);
        }
    }
}

struct Crossing {
    float x;
    int dir;
};

void rasterizeFillTight(const std::vector<Edge>& edges, bool evenOdd,
                        int boxX, int boxY, int boxW, int boxH,
                        std::vector<float>& covBuf) {
    if (edges.empty() || boxW <= 0 || boxH <= 0) return;

    constexpr int kSubY = 4;
    constexpr float kInvSubY = 1.0f / static_cast<float>(kSubY);
    thread_local static std::vector<Crossing> sCrossings;

    for (int row = 0; row < boxH; ++row) {
        const float baseY = static_cast<float>(boxY + row);
        float* rowCov = &covBuf[static_cast<size_t>(row * boxW)];

        for (int sub = 0; sub < kSubY; ++sub) {
            const float sy = baseY + (static_cast<float>(sub) + 0.5f) * kInvSubY;
            sCrossings.clear();

            for (const auto& e : edges) {
                if (sy >= e.y0 && sy < e.y1) {
                    float t = (sy - e.y0) / (e.y1 - e.y0);
                    sCrossings.push_back({e.x0 + t * (e.x1 - e.x0), e.dir});
                }
            }
            if (sCrossings.empty()) continue;

            std::sort(sCrossings.begin(), sCrossings.end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

            int winding = 0;
            size_t i = 0;
            while (i < sCrossings.size()) {
                float xStart = sCrossings[i].x;
                winding += sCrossings[i].dir;
                ++i;
                while (i < sCrossings.size() && std::abs(sCrossings[i].x - xStart) < 1e-4f) {
                    winding += sCrossings[i].dir;
                    ++i;
                }
                bool inside = evenOdd ? ((winding & 1) != 0) : (winding != 0);
                if (!inside || i >= sCrossings.size()) continue;

                float xEnd = sCrossings[i].x;
                int ix0 = std::max(0, static_cast<int>(std::floor(xStart)) - boxX);
                int ix1 = std::min(boxW - 1, static_cast<int>(std::floor(xEnd)) - boxX);
                for (int cx = ix0; cx <= ix1; ++cx) {
                    float px0 = static_cast<float>(boxX + cx);
                    float px1 = px0 + 1.0f;
                    float overlap = std::min(px1, xEnd) - std::max(px0, xStart);
                    if (overlap > 0.0f) {
                        rowCov[cx] += overlap * kInvSubY;
                    }
                }
            }
        }
    }
}

void rasterizeStrokeTight(const std::vector<std::vector<Point2D>>& polylines,
                          float halfWidth, uint8_t capStyle,
                          int boxX, int boxY, int boxW, int boxH,
                          std::vector<float>& covBuf) {
    if (polylines.empty() || halfWidth <= 0.0f || boxW <= 0 || boxH <= 0) return;

    const float r = halfWidth;
    for (const auto& poly : polylines) {
        if (poly.size() < 2) continue;
        for (size_t i = 0; i + 1 < poly.size(); ++i) {
            const float x0 = poly[i].x;
            const float y0 = poly[i].y;
            const float x1 = poly[i + 1].x;
            const float y1 = poly[i + 1].y;
            const float abx = x1 - x0;
            const float aby = y1 - y0;
            const float lenSq = abx * abx + aby * aby;

            const int minX = std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - r - 1.0f)) - boxX);
            const int minY = std::max(0, static_cast<int>(std::floor(std::min(y0, y1) - r - 1.0f)) - boxY);
            const int maxX = std::min(boxW, static_cast<int>(std::ceil(std::max(x0, x1) + r + 1.0f)) - boxX);
            const int maxY = std::min(boxH, static_cast<int>(std::ceil(std::max(y0, y1) + r + 1.0f)) - boxY);

            const bool isFirst = (i == 0);
            const bool isLast = (i + 2 == poly.size());
            const float invLen = (lenSq > 1e-8f) ? (1.0f / std::sqrt(lenSq)) : 0.0f;
            const float ux = abx * invLen;
            const float uy = aby * invLen;

            for (int py = minY; py < maxY; ++py) {
                const float fy = static_cast<float>(boxY + py) + 0.5f;
                float* rowCov = &covBuf[static_cast<size_t>(py * boxW)];
                for (int px = minX; px < maxX; ++px) {
                    const float fx = static_cast<float>(boxX + px) + 0.5f;
                    const float dx = fx - x0;
                    const float dy = fy - y0;
                    float t = (lenSq > 1e-8f) ? ((dx * abx + dy * aby) / lenSq) : 0.0f;

                    float dist = 0.0f;
                    if (capStyle == 0 && ((isFirst && t < 0.0f) || (isLast && t > 1.0f))) {
                        // Butt cap: clip perpendicular to segment end
                        float proj = dx * ux + dy * uy;
                        float endDist = (t < 0.0f) ? -proj : (proj - std::sqrt(lenSq));
                        float perpDist = std::abs(-dx * uy + dy * ux) - r;
                        dist = std::max(endDist, perpDist);
                    } else if (capStyle == 2 && ((isFirst && t < 0.0f) || (isLast && t > 1.0f))) {
                        // Square cap: extend by r along segment
                        float proj = dx * ux + dy * uy;
                        float endDist = ((t < 0.0f) ? -proj : (proj - std::sqrt(lenSq))) - r;
                        float perpDist = std::abs(-dx * uy + dy * ux) - r;
                        dist = std::max(endDist, perpDist);
                    } else {
                        t = std::clamp(t, 0.0f, 1.0f);
                        dist = std::hypot(fx - (x0 + t * abx), fy - (y0 + t * aby)) - r;
                    }

                    float cov = std::clamp(0.5f - dist, 0.0f, 1.0f);
                    if (cov > rowCov[px]) {
                        rowCov[px] = cov;
                    }
                }
            }
        }
    }
}

void blendCoverageIntoRgba(const std::vector<float>& covBuf, uint32_t color,
                           int boxW, int boxH, uint8_t* dstRgba, int dstStridePx,
                           int dstOffsetX, int dstOffsetY) {
    const uint8_t ca = static_cast<uint8_t>((color >> 24) & 0xFF);
    if (ca == 0) return;
    const float sr = static_cast<float>((color >> 16) & 0xFF);
    const float sg = static_cast<float>((color >> 8) & 0xFF);
    const float sb = static_cast<float>(color & 0xFF);
    const float baseA = static_cast<float>(ca) / 255.0f;

    for (int y = 0; y < boxH; ++y) {
        const float* covRow = &covBuf[static_cast<size_t>(y * boxW)];
        uint8_t* dstRow = &dstRgba[(static_cast<size_t>(dstOffsetY + y) * static_cast<size_t>(dstStridePx) +
                                    static_cast<size_t>(dstOffsetX)) * 4];
        for (int x = 0; x < boxW; ++x, dstRow += 4) {
            const float cov = std::min(covRow[x], 1.0f);
            if (cov <= 0.001f) continue;
            const float sa = baseA * cov;
            if (sa >= 0.999f) {
                dstRow[0] = static_cast<uint8_t>(sr);
                dstRow[1] = static_cast<uint8_t>(sg);
                dstRow[2] = static_cast<uint8_t>(sb);
                dstRow[3] = 255;
            } else {
                const float da = static_cast<float>(dstRow[3]) / 255.0f;
                const float outA = sa + da * (1.0f - sa);
                if (outA > 0.0001f) {
                    const float invOutA = 1.0f / outA;
                    const float daInvSa = da * (1.0f - sa);
                    dstRow[0] = static_cast<uint8_t>(std::clamp((sr * sa + static_cast<float>(dstRow[0]) * daInvSa) * invOutA, 0.0f, 255.0f));
                    dstRow[1] = static_cast<uint8_t>(std::clamp((sg * sa + static_cast<float>(dstRow[1]) * daInvSa) * invOutA, 0.0f, 255.0f));
                    dstRow[2] = static_cast<uint8_t>(std::clamp((sb * sa + static_cast<float>(dstRow[2]) * daInvSa) * invOutA, 0.0f, 255.0f));
                    dstRow[3] = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
                }
            }
        }
    }
}

} // namespace

bool VectorImage::parse(const uint8_t* data, size_t size, VectorImage& out) {
    if (!data || size < 16) return false;
    if (data[0] != 'E' || data[1] != 'V' || data[2] != 'E' || data[3] != 'C') return false;

    const uint16_t version = readU16(data + 4);
    if (version != 1) return false;

    out.width = readF32(data + 8);
    out.height = readF32(data + 12);
    const uint32_t shapeCount = readU32(data + 16);
    if (size < 20) return false;

    size_t pos = 20;
    out.shapes.clear();
    out.shapes.reserve(std::min<uint32_t>(shapeCount, 65536u));

    for (uint32_t s = 0; s < shapeCount; ++s) {
        if (pos + 34 > size) return false;
        VectorShape shape;
        shape.fillColor = readU32(data + pos); pos += 4;
        shape.strokeColor = readU32(data + pos); pos += 4;
        shape.strokeWidth = readF32(data + pos); pos += 4;
        shape.flags = data[pos]; pos += 4; // 1 byte flags + 3 bytes reserved
        shape.minX = readF32(data + pos); pos += 4;
        shape.minY = readF32(data + pos); pos += 4;
        shape.maxX = readF32(data + pos); pos += 4;
        shape.maxY = readF32(data + pos); pos += 4;
        const uint16_t pathCount = readU16(data + pos); pos += 2;

        shape.paths.reserve(pathCount);
        for (uint16_t p = 0; p < pathCount; ++p) {
            if (pos + 5 > size) return false;
            VectorPath path;
            path.closed = (data[pos] != 0); pos += 1;
            const uint32_t verbCount = readU32(data + pos); pos += 4;
            path.verbs.reserve(verbCount);
            for (uint32_t v = 0; v < verbCount; ++v) {
                if (pos >= size) return false;
                uint8_t verbByte = data[pos++];
                VectorVerb verb;
                if (verbByte == 0) {
                    if (pos + 8 > size) return false;
                    verb.type = VectorVerbType::MoveTo;
                    verb.x0 = readF32(data + pos);
                    verb.y0 = readF32(data + pos + 4);
                    pos += 8;
                } else if (verbByte == 1) {
                    if (pos + 8 > size) return false;
                    verb.type = VectorVerbType::LineTo;
                    verb.x0 = readF32(data + pos);
                    verb.y0 = readF32(data + pos + 4);
                    pos += 8;
                } else if (verbByte == 2) {
                    if (pos + 24 > size) return false;
                    verb.type = VectorVerbType::CubicTo;
                    verb.x0 = readF32(data + pos);
                    verb.y0 = readF32(data + pos + 4);
                    verb.x1 = readF32(data + pos + 8);
                    verb.y1 = readF32(data + pos + 12);
                    verb.x2 = readF32(data + pos + 16);
                    verb.y2 = readF32(data + pos + 20);
                    pos += 24;
                } else {
                    return false;
                }
                path.verbs.push_back(verb);
            }
            shape.paths.push_back(std::move(path));
        }
        out.shapes.push_back(std::move(shape));
    }

    return true;
}

bool VectorImage::parseAuto(const uint8_t* data, size_t size, VectorImage& out) {
    if (!data || size < 4) return false;
    if (data[0] == 'E' && data[1] == 'V' && data[2] == 'E' && data[3] == 'C') {
        return parse(data, size, out);
    }

    int outLen = 0;
    char* decomp = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(data),
                                           static_cast<int>(size), &outLen);
    if (!decomp) {
        decomp = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(data),
                                                  static_cast<int>(size), &outLen);
    }
    if (!decomp || outLen <= 0) {
        if (decomp) free(decomp);
        return false;
    }

    bool ok = parse(reinterpret_cast<const uint8_t*>(decomp), static_cast<size_t>(outLen), out);
    free(decomp);
    return ok;
}

const VectorImage* VectorImage::parseCached(const std::string& payload) {
    if (payload.empty()) return nullptr;
    constexpr size_t kMaxCached = 64;
    struct CacheState {
        std::list<std::string> lru;
        std::unordered_map<std::string, std::pair<std::unique_ptr<VectorImage>, std::list<std::string>::iterator>> map;
    };
    thread_local static CacheState sCache;

    auto it = sCache.map.find(payload);
    if (it != sCache.map.end()) {
        sCache.lru.splice(sCache.lru.begin(), sCache.lru, it->second.second);
        return it->second.first.get();
    }

    auto img = std::make_unique<VectorImage>();
    bool ok = false;
    if (payload.size() >= 4 && payload[0] == 'E' && payload[1] == 'V' && payload[2] == 'E' && payload[3] == 'C') {
        ok = parse(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), *img);
    } else {
        std::vector<uint8_t> raw = decodeBase64(payload);
        if (!raw.empty()) {
            ok = parseAuto(raw.data(), raw.size(), *img);
        }
    }
    if (!ok) return nullptr;

    if (sCache.map.size() >= kMaxCached && !sCache.lru.empty()) {
        sCache.map.erase(sCache.lru.back());
        sCache.lru.pop_back();
    }
    sCache.lru.push_front(payload);
    const VectorImage* rawPtr = img.get();
    sCache.map.emplace(sCache.lru.front(), std::make_pair(std::move(img), sCache.lru.begin()));
    return rawPtr;
}

void VectorRasterizer::rasterizeShapesTight(const VectorImage& img,
                                            float dstX, float dstY, float dstW, float dstH,
                                            bool cover, int clipW, int clipH,
                                            const ShapeEmitFn& emitFn) {
    if (img.empty() || img.width <= 0.0f || img.height <= 0.0f || clipW <= 0 || clipH <= 0) return;

    float targetW = (dstW > 0.0f) ? dstW : img.width;
    float targetH = (dstH > 0.0f) ? dstH : img.height;
    float scaleX = targetW / img.width;
    float scaleY = targetH / img.height;
    float scale = cover ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
    float tx = dstX + (cover ? (targetW - img.width * scale) * 0.5f : 0.0f);
    float ty = dstY + (cover ? (targetH - img.height * scale) * 0.5f : 0.0f);

    thread_local static std::vector<Edge> sEdges;
    thread_local static std::vector<std::vector<Point2D>> sPolylines;
    thread_local static std::vector<float> sCovBuf;
    thread_local static std::vector<uint8_t> sTightRgba;

    const int shapeCount = static_cast<int>(img.shapes.size());
    for (int idx = 0; idx < shapeCount; ++idx) {
        const VectorShape& shape = img.shapes[static_cast<size_t>(idx)];
        const bool hasFill = (shape.flags & kVectorHasFill) != 0 && ((shape.fillColor >> 24) & 0xFF) > 0;
        const bool hasStroke = (shape.flags & kVectorHasStroke) != 0 && shape.strokeWidth > 0.0f &&
                               ((shape.strokeColor >> 24) & 0xFF) > 0;
        if (!hasFill && !hasStroke) continue;

        const float pad = (hasStroke ? (shape.strokeWidth * scale * 0.5f) : 0.0f) + 1.5f;
        const int boxX = std::max(0, static_cast<int>(std::floor(tx + shape.minX * scale - pad)));
        const int boxY = std::max(0, static_cast<int>(std::floor(ty + shape.minY * scale - pad)));
        const int boxMaxX = std::min(clipW, static_cast<int>(std::ceil(tx + shape.maxX * scale + pad)));
        const int boxMaxY = std::min(clipH, static_cast<int>(std::ceil(ty + shape.maxY * scale + pad)));
        const int boxW = boxMaxX - boxX;
        const int boxH = boxMaxY - boxY;
        if (boxW <= 0 || boxH <= 0) continue;

        buildShapeGeometry(shape, tx, ty, scale, sEdges, sPolylines, hasStroke);

        const size_t pixCount = static_cast<size_t>(boxW * boxH);
        sTightRgba.assign(pixCount * 4, 0);

        if (hasFill && !sEdges.empty()) {
            sCovBuf.assign(pixCount, 0.0f);
            rasterizeFillTight(sEdges, (shape.flags & kVectorEvenOddFill) != 0,
                               boxX, boxY, boxW, boxH, sCovBuf);
            blendCoverageIntoRgba(sCovBuf, shape.fillColor, boxW, boxH, sTightRgba.data(), boxW, 0, 0);
        }

        if (hasStroke && !sPolylines.empty()) {
            sCovBuf.assign(pixCount, 0.0f);
            const uint8_t capStyle = static_cast<uint8_t>((shape.flags & kVectorCapMask) >> 3);
            rasterizeStrokeTight(sPolylines, shape.strokeWidth * scale * 0.5f, capStyle,
                                 boxX, boxY, boxW, boxH, sCovBuf);
            blendCoverageIntoRgba(sCovBuf, shape.strokeColor, boxW, boxH, sTightRgba.data(), boxW, 0, 0);
        }

        emitFn(idx, shapeCount, boxX, boxY, boxW, boxH, sTightRgba.data());
    }
}

void VectorRasterizer::rasterizeToRgba(const VectorImage& img,
                                       uint8_t* dstRgba, int dstW, int dstH,
                                       float dstX, float dstY, float dstW_, float dstH_,
                                       bool cover) {
    if (!dstRgba || dstW <= 0 || dstH <= 0 || img.empty() || img.width <= 0.0f || img.height <= 0.0f) return;

    float targetW = (dstW_ > 0.0f) ? dstW_ : static_cast<float>(dstW);
    float targetH = (dstH_ > 0.0f) ? dstH_ : static_cast<float>(dstH);
    float scaleX = targetW / img.width;
    float scaleY = targetH / img.height;
    float scale = cover ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
    float tx = dstX + (cover ? (targetW - img.width * scale) * 0.5f : 0.0f);
    float ty = dstY + (cover ? (targetH - img.height * scale) * 0.5f : 0.0f);

    thread_local static std::vector<Edge> sEdges;
    thread_local static std::vector<std::vector<Point2D>> sPolylines;
    thread_local static std::vector<float> sCovBuf;

    for (const auto& shape : img.shapes) {
        const bool hasFill = (shape.flags & kVectorHasFill) != 0 && ((shape.fillColor >> 24) & 0xFF) > 0;
        const bool hasStroke = (shape.flags & kVectorHasStroke) != 0 && shape.strokeWidth > 0.0f &&
                               ((shape.strokeColor >> 24) & 0xFF) > 0;
        if (!hasFill && !hasStroke) continue;

        const float pad = (hasStroke ? (shape.strokeWidth * scale * 0.5f) : 0.0f) + 1.5f;
        const int boxX = std::max(0, static_cast<int>(std::floor(tx + shape.minX * scale - pad)));
        const int boxY = std::max(0, static_cast<int>(std::floor(ty + shape.minY * scale - pad)));
        const int boxMaxX = std::min(dstW, static_cast<int>(std::ceil(tx + shape.maxX * scale + pad)));
        const int boxMaxY = std::min(dstH, static_cast<int>(std::ceil(ty + shape.maxY * scale + pad)));
        const int boxW = boxMaxX - boxX;
        const int boxH = boxMaxY - boxY;
        if (boxW <= 0 || boxH <= 0) continue;

        buildShapeGeometry(shape, tx, ty, scale, sEdges, sPolylines, hasStroke);
        const size_t pixCount = static_cast<size_t>(boxW * boxH);

        if (hasFill && !sEdges.empty()) {
            sCovBuf.assign(pixCount, 0.0f);
            rasterizeFillTight(sEdges, (shape.flags & kVectorEvenOddFill) != 0,
                               boxX, boxY, boxW, boxH, sCovBuf);
            blendCoverageIntoRgba(sCovBuf, shape.fillColor, boxW, boxH, dstRgba, dstW, boxX, boxY);
        }

        if (hasStroke && !sPolylines.empty()) {
            sCovBuf.assign(pixCount, 0.0f);
            const uint8_t capStyle = static_cast<uint8_t>((shape.flags & kVectorCapMask) >> 3);
            rasterizeStrokeTight(sPolylines, shape.strokeWidth * scale * 0.5f, capStyle,
                                 boxX, boxY, boxW, boxH, sCovBuf);
            blendCoverageIntoRgba(sCovBuf, shape.strokeColor, boxW, boxH, dstRgba, dstW, boxX, boxY);
        }
    }
}

} // namespace solengine
