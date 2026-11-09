#include "eatsbits/ui/svg_path.hpp"
#include <cmath>
#include <cctype>
#include <sstream>
#include <mutex>
#include <numbers>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif



namespace eatsbits::ui {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// In-memory static cache for parsed paths
std::unordered_map<std::string, SvgPath> g_pathCache;
std::mutex g_cacheMutex;

inline bool isCommandChar(char c) noexcept {
    switch (c) {
        case 'M': case 'm': case 'Z': case 'z':
        case 'L': case 'l': case 'H': case 'h': case 'V': case 'v':
        case 'C': case 'c': case 'S': case 's':
        case 'Q': case 'q': case 'T': case 't':
        case 'A': case 'a':
            return true;
        default:
            return false;
    }
}

// Tokenizes SVG path string into command characters and floating point values.
std::vector<std::string> tokenizePath(std::string_view d) {
    std::vector<std::string> tokens;
    tokens.reserve(64);

    size_t i = 0;
    const size_t len = d.size();

    while (i < len) {
        char c = d[i];

        // Skip whitespace and commas
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            i++;
            continue;
        }

        // SVG command single char token
        if (isCommandChar(c)) {
            tokens.emplace_back(1, c);
            i++;
            continue;
        }

        // Number token (handles signs, decimals, exponents)
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
            size_t start = i;
            if (c == '-' || c == '+') {
                i++;
            }
            bool hasDot = false;
            while (i < len) {
                char ch = d[i];
                if (std::isdigit(static_cast<unsigned char>(ch))) {
                    i++;
                } else if (ch == '.' && !hasDot) {
                    hasDot = true;
                    i++;
                } else if (ch == 'e' || ch == 'E') {
                    i++;
                    if (i < len && (d[i] == '+' || d[i] == '-')) {
                        i++;
                    }
                } else {
                    break;
                }
            }
            tokens.emplace_back(d.substr(start, i - start));
            continue;
        }

        // Unexpected character, skip
        i++;
    }

    return tokens;
}

// Subdivides cubic Bezier curve into polyline points
void subdivideCubicBezier(SvgPoint p0, SvgPoint p1, SvgPoint p2, SvgPoint p3,
                          std::vector<SvgPoint>& outPoints, int steps = 16) {
    for (int step = 1; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const float u = 1.0f - t;
        const float tt = t * t;
        const float uu = u * u;
        const float uuu = uu * u;
        const float ttt = tt * t;

        const float x = uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x;
        const float y = uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y;
        outPoints.emplace_back(x, y);
    }
}

// Subdivides quadratic Bezier curve
void subdivideQuadBezier(SvgPoint p0, SvgPoint p1, SvgPoint p2,
                         std::vector<SvgPoint>& outPoints, int steps = 12) {
    for (int step = 1; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const float u = 1.0f - t;
        const float x = u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x;
        const float y = u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y;
        outPoints.emplace_back(x, y);
    }
}

// Converts standard SVG elliptical arc into cubic Bezier curves and subdivides
void convertArcToCubic(SvgPoint p0, float rx, float ry, float xAxisRotationDeg,
                       bool largeArc, bool sweep, SvgPoint p1,
                       std::vector<SvgPoint>& outPoints) {
    if (rx <= 0.0f || ry <= 0.0f) {
        outPoints.push_back(p1);
        return;
    }

    rx = std::abs(rx);
    ry = std::abs(ry);

    const float phi = xAxisRotationDeg * (kPi / 180.0f);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);

    // Compute (x1', y1')
    const float dx = (p0.x - p1.x) * 0.5f;
    const float dy = (p0.y - p1.y) * 0.5f;
    const float x1p = cosPhi * dx + sinPhi * dy;
    const float y1p = -sinPhi * dx + cosPhi * dy;

    // Correct radii if too small
    float prx = rx * rx;
    float pry = ry * ry;
    const float px1p = x1p * x1p;
    const float py1p = y1p * y1p;

    const float check = px1p / prx + py1p / pry;
    if (check > 1.0f) {
        const float factor = std::sqrt(check);
        rx *= factor;
        ry *= factor;
        prx = rx * rx;
        pry = ry * ry;
    }

    // Compute (cx', cy')
    const float sign = (largeArc == sweep) ? -1.0f : 1.0f;
    float sq = ((prx * pry) - (prx * py1p) - (pry * px1p)) / ((prx * py1p) + (pry * px1p));
    if (sq < 0.0f) sq = 0.0f;
    const float coef = sign * std::sqrt(sq);
    const float cxp = coef * ((rx * y1p) / ry);
    const float cyp = coef * (-(ry * x1p) / rx);

    // Compute (cx, cy) from (cx', cy')
    const float cx = cosPhi * cxp - sinPhi * cyp + (p0.x + p1.x) * 0.5f;
    const float cy = sinPhi * cxp + cosPhi * cyp + (p0.y + p1.y) * 0.5f;

    // Compute theta1 and deltaTheta
    auto vectorAngle = [](float ux, float uy, float vx, float vy) -> float {
        const float dot = ux * vx + uy * vy;
        const float len = std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy);
        float ang = std::acos(std::clamp(dot / len, -1.0f, 1.0f));
        if ((ux * vy - uy * vx) < 0.0f) ang = -ang;
        return ang;
    };

    const float theta1 = vectorAngle(1.0f, 0.0f, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float deltaTheta = vectorAngle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);

    if (!sweep && deltaTheta > 0.0f) {
        deltaTheta -= kTwoPi;
    } else if (sweep && deltaTheta < 0.0f) {
        deltaTheta += kTwoPi;
    }

    // Subdivide arc into angular segments
    const int segments = std::max(4, static_cast<int>(std::ceil(std::abs(deltaTheta) / (kPi * 0.25f))));
    const float segAngle = deltaTheta / static_cast<float>(segments);

    for (int s = 1; s <= segments; ++s) {
        const float theta = theta1 + segAngle * static_cast<float>(s);
        const float ex = rx * std::cos(theta);
        const float ey = ry * std::sin(theta);
        const float px = cosPhi * ex - sinPhi * ey + cx;
        const float py = sinPhi * ex + cosPhi * ey + cy;
        outPoints.emplace_back(px, py);
    }
}

// 2D Cross product / winding calculation
inline float cross2D(SvgPoint a, SvgPoint b, SvgPoint c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Tests if point P is inside triangle ABC
bool pointInTriangle(SvgPoint p, SvgPoint a, SvgPoint b, SvgPoint c) noexcept {
    const float cp1 = cross2D(a, b, p);
    const float cp2 = cross2D(b, c, p);
    const float cp3 = cross2D(c, a, p);
    const bool hasNeg = (cp1 < -1e-5f) || (cp2 < -1e-5f) || (cp3 < -1e-5f);
    const bool hasPos = (cp1 > 1e-5f) || (cp2 > 1e-5f) || (cp3 > 1e-5f);
    return !(hasNeg && hasPos);
}

} // namespace

// ============================================================================
// SvgPath Implementation
// ============================================================================

SvgPath SvgPath::parse(std::string_view d) {
    const std::string key(d);
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_pathCache.find(key);
        if (it != g_pathCache.end()) {
            return it->second;
        }
    }

    SvgPath path;
    const auto tokens = tokenizePath(d);
    if (tokens.empty()) return path;

    size_t i = 0;
    char currentCmd = 'M';
    SvgPoint curPt{0.0f, 0.0f};
    SvgPoint startPt{0.0f, 0.0f};
    SvgPoint lastCtrl{0.0f, 0.0f};
    std::vector<SvgPoint> currentContour;

    auto parseFloat = [&tokens, &i]() -> float {
        if (i < tokens.size()) {
            try {
                return std::stof(tokens[i++]);
            } catch (...) {
                return 0.0f;
            }
        }
        return 0.0f;
    };

    while (i < tokens.size()) {
        const std::string& token = tokens[i];
        if (token.size() == 1 && isCommandChar(token[0])) {
            currentCmd = token[0];
            i++;
        }

        switch (currentCmd) {
            case 'M': // Absolute MoveTo
                if (!currentContour.empty()) {
                    path.addContour(std::move(currentContour));
                    currentContour.clear();
                }
                curPt.x = parseFloat();
                curPt.y = parseFloat();
                startPt = curPt;
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                currentCmd = 'L'; // Subsequent pairs are implicit LineTo
                break;

            case 'm': // Relative MoveTo
                if (!currentContour.empty()) {
                    path.addContour(std::move(currentContour));
                    currentContour.clear();
                }
                curPt.x += parseFloat();
                curPt.y += parseFloat();
                startPt = curPt;
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                currentCmd = 'l';
                break;

            case 'L': // Absolute LineTo
                curPt.x = parseFloat();
                curPt.y = parseFloat();
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                break;

            case 'l': // Relative LineTo
                curPt.x += parseFloat();
                curPt.y += parseFloat();
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                break;

            case 'H': // Absolute Horizontal LineTo
                curPt.x = parseFloat();
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                break;

            case 'h': // Relative Horizontal LineTo
                curPt.x += parseFloat();
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                break;

            case 'V': // Absolute Vertical LineTo
                curPt.y = parseFloat();
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                break;

            case 'v': // Relative Vertical LineTo
                curPt.y += parseFloat();
                lastCtrl = curPt;
                currentContour.push_back(curPt);
                break;

            case 'C': { // Absolute Cubic Bezier
                SvgPoint p1{parseFloat(), parseFloat()};
                SvgPoint p2{parseFloat(), parseFloat()};
                SvgPoint p3{parseFloat(), parseFloat()};
                subdivideCubicBezier(curPt, p1, p2, p3, currentContour);
                lastCtrl = p2;
                curPt = p3;
                break;
            }

            case 'c': { // Relative Cubic Bezier
                SvgPoint p1{curPt.x + parseFloat(), curPt.y + parseFloat()};
                SvgPoint p2{curPt.x + parseFloat(), curPt.y + parseFloat()};
                SvgPoint p3{curPt.x + parseFloat(), curPt.y + parseFloat()};
                subdivideCubicBezier(curPt, p1, p2, p3, currentContour);
                lastCtrl = p2;
                curPt = p3;
                break;
            }

            case 'S': { // Absolute Smooth Cubic Bezier
                SvgPoint p1{2.0f * curPt.x - lastCtrl.x, 2.0f * curPt.y - lastCtrl.y};
                SvgPoint p2{parseFloat(), parseFloat()};
                SvgPoint p3{parseFloat(), parseFloat()};
                subdivideCubicBezier(curPt, p1, p2, p3, currentContour);
                lastCtrl = p2;
                curPt = p3;
                break;
            }

            case 's': { // Relative Smooth Cubic Bezier
                SvgPoint p1{2.0f * curPt.x - lastCtrl.x, 2.0f * curPt.y - lastCtrl.y};
                SvgPoint p2{curPt.x + parseFloat(), curPt.y + parseFloat()};
                SvgPoint p3{curPt.x + parseFloat(), curPt.y + parseFloat()};
                subdivideCubicBezier(curPt, p1, p2, p3, currentContour);
                lastCtrl = p2;
                curPt = p3;
                break;
            }

            case 'Q': { // Absolute Quadratic Bezier
                SvgPoint p1{parseFloat(), parseFloat()};
                SvgPoint p2{parseFloat(), parseFloat()};
                subdivideQuadBezier(curPt, p1, p2, currentContour);
                lastCtrl = p1;
                curPt = p2;
                break;
            }

            case 'q': { // Relative Quadratic Bezier
                SvgPoint p1{curPt.x + parseFloat(), curPt.y + parseFloat()};
                SvgPoint p2{curPt.x + parseFloat(), curPt.y + parseFloat()};
                subdivideQuadBezier(curPt, p1, p2, currentContour);
                lastCtrl = p1;
                curPt = p2;
                break;
            }

            case 'T': { // Absolute Smooth Quadratic Bezier
                SvgPoint p1{2.0f * curPt.x - lastCtrl.x, 2.0f * curPt.y - lastCtrl.y};
                SvgPoint p2{parseFloat(), parseFloat()};
                subdivideQuadBezier(curPt, p1, p2, currentContour);
                lastCtrl = p1;
                curPt = p2;
                break;
            }

            case 't': { // Relative Smooth Quadratic Bezier
                SvgPoint p1{2.0f * curPt.x - lastCtrl.x, 2.0f * curPt.y - lastCtrl.y};
                SvgPoint p2{curPt.x + parseFloat(), curPt.y + parseFloat()};
                subdivideQuadBezier(curPt, p1, p2, currentContour);
                lastCtrl = p1;
                curPt = p2;
                break;
            }

            case 'A': { // Absolute Elliptical Arc
                const float rx = parseFloat();
                const float ry = parseFloat();
                const float rot = parseFloat();
                const bool largeArc = (parseFloat() != 0.0f);
                const bool sweep = (parseFloat() != 0.0f);
                SvgPoint p1{parseFloat(), parseFloat()};
                convertArcToCubic(curPt, rx, ry, rot, largeArc, sweep, p1, currentContour);
                lastCtrl = p1;
                curPt = p1;
                break;
            }

            case 'a': { // Relative Elliptical Arc
                const float rx = parseFloat();
                const float ry = parseFloat();
                const float rot = parseFloat();
                const bool largeArc = (parseFloat() != 0.0f);
                const bool sweep = (parseFloat() != 0.0f);
                SvgPoint p1{curPt.x + parseFloat(), curPt.y + parseFloat()};
                convertArcToCubic(curPt, rx, ry, rot, largeArc, sweep, p1, currentContour);
                lastCtrl = p1;
                curPt = p1;
                break;
            }

            case 'Z':
            case 'z': // ClosePath
                if (!currentContour.empty() && !(currentContour.back() == startPt)) {
                    currentContour.push_back(startPt);
                }
                curPt = startPt;
                lastCtrl = startPt;
                break;

            default:
                i++;
                break;
        }
    }

    if (!currentContour.empty()) {
        path.addContour(std::move(currentContour));
    }

    path.computeBounds();

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_pathCache.emplace(key, path);
    }

    return path;
}

void SvgPath::clearCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_pathCache.clear();
}

void SvgPath::addContour(std::vector<SvgPoint> contour) {
    if (!contour.empty()) {
        contours_.push_back(std::move(contour));
        computeBounds();
    }
}

void SvgPath::computeBounds() {
    bounds_ = SvgRect{};
    for (const auto& contour : contours_) {
        for (const auto& p : contour) {
            bounds_.expandToInclude(p);
        }
    }
}

// Ear-clipping triangulation for arbitrary 2D polygons
std::vector<SvgPoint> SvgPath::triangulate() const {
    std::vector<SvgPoint> triangles;

    for (const auto& rawContour : contours_) {
        if (rawContour.size() < 3) continue;

        // Copy and deduplicate adjacent identical vertices
        std::vector<SvgPoint> poly;
        poly.reserve(rawContour.size());
        for (const auto& p : rawContour) {
            if (poly.empty() || !(poly.back() == p)) {
                poly.push_back(p);
            }
        }
        // Remove closing point if identical to first
        if (poly.size() > 1 && (poly.front() == poly.back())) {
            poly.pop_back();
        }
        if (poly.size() < 3) continue;

        // Compute polygon signed area (winding)
        float area = 0.0f;
        for (size_t idx = 0; idx < poly.size(); ++idx) {
            const auto& p0 = poly[idx];
            const auto& p1 = poly[(idx + 1) % poly.size()];
            area += (p0.x * p1.y - p1.x * p0.y);
        }

        // If clockwise, reverse to counter-clockwise
        if (area < 0.0f) {
            std::reverse(poly.begin(), poly.end());
        }

        // Indexed ear clipping
        std::vector<int> V(poly.size());
        for (size_t idx = 0; idx < poly.size(); ++idx) V[idx] = static_cast<int>(idx);

        int count = static_cast<int>(poly.size());
        int safeguard = 2 * count;

        while (count > 2 && safeguard-- > 0) {
            bool earFound = false;

            for (int vi = 0; vi < count; ++vi) {
                int prev = V[(vi + count - 1) % count];
                int curr = V[vi];
                int next = V[(vi + 1) % count];

                SvgPoint a = poly[prev];
                SvgPoint b = poly[curr];
                SvgPoint c = poly[next];

                // Check convexity: cross product must be positive
                if (cross2D(a, b, c) <= 1e-6f) continue;

                // Check if any other vertex lies inside triangle abc
                bool hasPointInside = false;
                for (int checkIdx = 0; checkIdx < count; ++checkIdx) {
                    if (checkIdx == ((vi + count - 1) % count) ||
                        checkIdx == vi ||
                        checkIdx == ((vi + 1) % count)) {
                        continue;
                    }
                    if (pointInTriangle(poly[V[checkIdx]], a, b, c)) {
                        hasPointInside = true;
                        break;
                    }
                }

                if (!hasPointInside) {
                    // Triangle found!
                    triangles.push_back(a);
                    triangles.push_back(b);
                    triangles.push_back(c);

                    // Remove current vertex
                    V.erase(V.begin() + vi);
                    count--;
                    earFound = true;
                    break;
                }
            }

            if (!earFound) {
                // Degenerate/non-simple fallback: fan triangulation
                for (int fi = 1; fi < count - 1; ++fi) {
                    triangles.push_back(poly[V[0]]);
                    triangles.push_back(poly[V[fi]]);
                    triangles.push_back(poly[V[fi + 1]]);
                }
                break;
            }
        }
    }

    return triangles;
}

// High-precision anti-aliased scanline rasterizer (produces RGBA pixel buffer)
std::vector<uint8_t> SvgPath::rasterize(int width, int height, uint32_t color,
                                       bool isFill, float strokeWidth, float padding,
                                       const SvgRect* customBounds) const {
    std::vector<uint8_t> pixels(width * height * 4, 0);
    const SvgRect& refBounds = customBounds ? *customBounds : bounds_;
    if (width <= 0 || height <= 0 || refBounds.isEmpty()) return pixels;

    const float srcW = refBounds.width();
    const float srcH = refBounds.height();
    if (srcW <= 0.0f || srcH <= 0.0f) return pixels;

    const float targetW = static_cast<float>(width) - 2.0f * padding;
    const float targetH = static_cast<float>(height) - 2.0f * padding;
    const float scale = std::min(targetW / srcW, targetH / srcH);

    const float offsetX = padding + (targetW - srcW * scale) * 0.5f - refBounds.minX * scale;
    const float offsetY = padding + (targetH - srcH * scale) * 0.5f - refBounds.minY * scale;

    // Unpack color (RGBA)
    const uint8_t cr = static_cast<uint8_t>((color) & 0xFF);
    const uint8_t cg = static_cast<uint8_t>((color >> 8) & 0xFF);
    const uint8_t cb = static_cast<uint8_t>((color >> 16) & 0xFF);
    const float ca   = static_cast<float>((color >> 24) & 0xFF) / 255.0f;

    // Transform contours to target raster pixel coordinates
    std::vector<std::vector<SvgPoint>> rasterContours;
    for (const auto& c : contours_) {
        std::vector<SvgPoint> rc;
        rc.reserve(c.size());
        for (const auto& p : c) {
            rc.emplace_back(p.x * scale + offsetX, p.y * scale + offsetY);
        }
        rasterContours.push_back(std::move(rc));
    }

    // 2x2 subpixel supersampling grid for crisp anti-aliasing
    constexpr float subOffsets[4][2] = {
        {0.25f, 0.25f}, {0.75f, 0.25f},
        {0.25f, 0.75f}, {0.75f, 0.75f}
    };

    if (isFill) {
        // Even-Odd / Non-zero winding fill rasterizer
        for (int py = 0; py < height; ++py) {
            for (int px = 0; px < width; ++px) {
                int hitCount = 0;

                for (const auto& off : subOffsets) {
                    const float sx = static_cast<float>(px) + off[0];
                    const float sy = static_cast<float>(py) + off[1];

                    int windings = 0;
                    for (const auto& contour : rasterContours) {
                        const size_t n = contour.size();
                        for (size_t i = 0; i < n; ++i) {
                            const SvgPoint& p0 = contour[i];
                            const SvgPoint& p1 = contour[(i + 1) % n];

                            if ((p0.y <= sy && p1.y > sy) || (p1.y <= sy && p0.y > sy)) {
                                const float vt = (sy - p0.y) / (p1.y - p0.y);
                                if (sx < p0.x + vt * (p1.x - p0.x)) {
                                    windings++;
                                }
                            }
                        }
                    }

                    // Even-Odd test
                    if (windings % 2 != 0) {
                        hitCount++;
                    }
                }

                if (hitCount > 0) {
                    const float alpha = (static_cast<float>(hitCount) / 4.0f) * ca;
                    const size_t idx = (py * width + px) * 4;

                    // Blend source onto destination
                    const float dstA = static_cast<float>(pixels[idx + 3]) / 255.0f;
                    const float outA = alpha + dstA * (1.0f - alpha);
                    if (outA > 0.0f) {
                        pixels[idx + 0] = static_cast<uint8_t>(std::clamp((cr * alpha + pixels[idx + 0] * dstA * (1.0f - alpha)) / outA, 0.0f, 255.0f));
                        pixels[idx + 1] = static_cast<uint8_t>(std::clamp((cg * alpha + pixels[idx + 1] * dstA * (1.0f - alpha)) / outA, 0.0f, 255.0f));
                        pixels[idx + 2] = static_cast<uint8_t>(std::clamp((cb * alpha + pixels[idx + 2] * dstA * (1.0f - alpha)) / outA, 0.0f, 255.0f));
                        pixels[idx + 3] = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
                    }
                }
            }
        }
    } else {
        // Stroke rasterizer: distance to line segments
        const float halfWidth = std::max(0.5f, strokeWidth * 0.5f);
        const float halfWidthSq = halfWidth * halfWidth;

        for (int py = 0; py < height; ++py) {
            for (int px = 0; px < width; ++px) {
                int hitCount = 0;

                for (const auto& off : subOffsets) {
                    const float sx = static_cast<float>(px) + off[0];
                    const float sy = static_cast<float>(py) + off[1];

                    bool inside = false;
                    for (const auto& contour : rasterContours) {
                        const size_t n = contour.size();
                        for (size_t i = 0; i < n; ++i) {
                            const SvgPoint& p0 = contour[i];
                            const SvgPoint& p1 = contour[(i + 1) % n];

                            const float l2 = (p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y);
                            float distSq = 0.0f;
                            if (l2 < 1e-5f) {
                                distSq = (sx - p0.x) * (sx - p0.x) + (sy - p0.y) * (sy - p0.y);
                            } else {
                                const float t = std::clamp(((sx - p0.x) * (p1.x - p0.x) + (sy - p0.y) * (p1.y - p0.y)) / l2, 0.0f, 1.0f);
                                const float projX = p0.x + t * (p1.x - p0.x);
                                const float projY = p0.y + t * (p1.y - p0.y);
                                distSq = (sx - projX) * (sx - projX) + (sy - projY) * (sy - projY);
                            }

                            if (distSq <= halfWidthSq) {
                                inside = true;
                                break;
                            }
                        }
                        if (inside) break;
                    }

                    if (inside) hitCount++;
                }

                if (hitCount > 0) {
                    const float alpha = (static_cast<float>(hitCount) / 4.0f) * ca;
                    const size_t idx = (py * width + px) * 4;

                    const float dstA = static_cast<float>(pixels[idx + 3]) / 255.0f;
                    const float outA = alpha + dstA * (1.0f - alpha);
                    if (outA > 0.0f) {
                        pixels[idx + 0] = static_cast<uint8_t>(std::clamp((cr * alpha + pixels[idx + 0] * dstA * (1.0f - alpha)) / outA, 0.0f, 255.0f));
                        pixels[idx + 1] = static_cast<uint8_t>(std::clamp((cg * alpha + pixels[idx + 1] * dstA * (1.0f - alpha)) / outA, 0.0f, 255.0f));
                        pixels[idx + 2] = static_cast<uint8_t>(std::clamp((cb * alpha + pixels[idx + 2] * dstA * (1.0f - alpha)) / outA, 0.0f, 255.0f));
                        pixels[idx + 3] = static_cast<uint8_t>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
                    }
                }
            }
        }
    }

    return pixels;
}


// ============================================================================
// SvgLogo Implementation
// ============================================================================

std::vector<SvgLayerDef> SvgLogo::getLayers() {
    return {
        // Layer 1: Warm Amber rounded chassis square
        SvgLayerDef(kAmberChassisPath, kColorAmber, 1.0f, true),
        // Layer 2: Weathered dark monster head, open mouth, and eating bits
        SvgLayerDef(kMonsterHeadAndBitsPath, kColorDark, 1.0f, true),
        // Layer 3: Warm Amber circular monster eye
        SvgLayerDef(kMonsterEyePath, kColorAmber, 1.0f, true)
    };
}

std::vector<SvgLayerDef> SvgLogo::getInvertedLayers() {
    return {
        // Layer 1: Weathered dark rounded chassis square
        SvgLayerDef(kAmberChassisPath, kColorDark, 1.0f, true),
        // Layer 2: Crisp Warm Amber border rim around dark chassis
        SvgLayerDef(kAmberChassisPath, kColorAmber, 1.0f, false, 8.0f),
        // Layer 3: Warm Amber monster head, open mouth, and eating bits
        SvgLayerDef(kMonsterHeadAndBitsPath, kColorAmber, 1.0f, true),
        // Layer 4: Weathered dark circular monster eye
        SvgLayerDef(kMonsterEyePath, kColorDark, 1.0f, true)
    };
}

std::vector<SvgLayerDef> SvgLogo::getPlainLayers(uint32_t color) {
    return {
        // Single unified layer with outer contour, cutout eye, and eating bits
        SvgLayerDef(kPlainLogoPath, color, 1.0f, true)
    };
}

std::vector<SvgLayerDef> SvgLogo::getPlainInvertedLayers(uint32_t accentColor, uint32_t darkColor) {
    return {
        // Layer 1: Dark monster silhouette
        SvgLayerDef(kMonsterHeadAndBitsPath, darkColor, 1.0f, true),
        // Layer 2: Illuminated accent border rim around creature head
        SvgLayerDef(kMonsterHeadAndBitsPath, accentColor, 1.0f, false, 12.0f),
        // Layer 3: Illuminated accent monster eye
        SvgLayerDef(kMonsterEyePath, accentColor, 1.0f, true),
        // Layer 4: Illuminated accent eating bits
        SvgLayerDef(kMonsterBitsPath, accentColor, 1.0f, true)
    };
}

} // namespace eatsbits::ui
