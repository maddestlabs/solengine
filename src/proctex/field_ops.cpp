#include "eatstextures/field_ops.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>

namespace eatstextures {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline int wrapPeriod(int v, int period) noexcept {
    if (period <= 1) return 0;
    int r = v % period;
    return (r < 0) ? (r + period) : r;
}

inline float fracPos(float v) noexcept {
    return v - std::floor(v);
}

inline float fadeQuintic(float t) noexcept {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float lerpF(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

inline float smoothstepF(float edge0, float edge1, float x) noexcept {
    if (std::abs(edge1 - edge0) < 1e-6f) return (x >= edge0) ? 1.0f : 0.0f;
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline uint32_t hashU32(int x, int y, uint32_t seed) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 668265263u +
                 seed * 2246822519u + 3266489917u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    return h ^ (h >> 16);
}

inline float hashFloat01(int x, int y, uint32_t seed) noexcept {
    return static_cast<float>(hashU32(x, y, seed) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

inline void hashGrad2D(int x, int y, uint32_t seed, float& gx, float& gy) noexcept {
    const float angle = hashFloat01(x, y, seed) * kTwoPi;
    gx = std::cos(angle);
    gy = std::sin(angle);
}

// Seamless periodic 2D Perlin noise in [-1, 1]
float perlinPeriodic(float u, float v, int periodX, int periodY, uint32_t seed) noexcept {
    const float px = u * static_cast<float>(periodX);
    const float py = v * static_cast<float>(periodY);
    const int ix = static_cast<int>(std::floor(px));
    const int iy = static_cast<int>(std::floor(py));
    const float fx = px - static_cast<float>(ix);
    const float fy = py - static_cast<float>(iy);

    const int x0 = wrapPeriod(ix, periodX);
    const int y0 = wrapPeriod(iy, periodY);
    const int x1 = wrapPeriod(ix + 1, periodX);
    const int y1 = wrapPeriod(iy + 1, periodY);

    float g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
    hashGrad2D(x0, y0, seed, g00x, g00y);
    hashGrad2D(x1, y0, seed, g10x, g10y);
    hashGrad2D(x0, y1, seed, g01x, g01y);
    hashGrad2D(x1, y1, seed, g11x, g11y);

    const float n00 = g00x * fx + g00y * fy;
    const float n10 = g10x * (fx - 1.0f) + g10y * fy;
    const float n01 = g01x * fx + g01y * (fy - 1.0f);
    const float n11 = g11x * (fx - 1.0f) + g11y * (fy - 1.0f);

    const float wx = fadeQuintic(fx);
    const float wy = fadeQuintic(fy);
    return lerpF(lerpF(n00, n10, wx), lerpF(n01, n11, wx), wy) * 1.41421356f;
}

// Seamless periodic 2D Value noise in [-1, 1]
float valuePeriodic(float u, float v, int periodX, int periodY, uint32_t seed) noexcept {
    const float px = u * static_cast<float>(periodX);
    const float py = v * static_cast<float>(periodY);
    const int ix = static_cast<int>(std::floor(px));
    const int iy = static_cast<int>(std::floor(py));
    const float fx = px - static_cast<float>(ix);
    const float fy = py - static_cast<float>(iy);

    const int x0 = wrapPeriod(ix, periodX);
    const int y0 = wrapPeriod(iy, periodY);
    const int x1 = wrapPeriod(ix + 1, periodX);
    const int y1 = wrapPeriod(iy + 1, periodY);

    const float v00 = hashFloat01(x0, y0, seed) * 2.0f - 1.0f;
    const float v10 = hashFloat01(x1, y0, seed) * 2.0f - 1.0f;
    const float v01 = hashFloat01(x0, y1, seed) * 2.0f - 1.0f;
    const float v11 = hashFloat01(x1, y1, seed) * 2.0f - 1.0f;

    const float wx = fadeQuintic(fx);
    const float wy = fadeQuintic(fy);
    return lerpF(lerpF(v00, v10, wx), lerpF(v01, v11, wx), wy);
}

} // namespace

float FloatField::atWrapped(int x, int y) const noexcept {
    if (width == 0 || height == 0 || data.empty()) return 0.0f;
    const int wx = wrapPeriod(x, static_cast<int>(width));
    const int wy = wrapPeriod(y, static_cast<int>(height));
    return data[static_cast<size_t>(wy) * width + static_cast<size_t>(wx)];
}

float FloatField::sampleWrapped(float u, float v) const noexcept {
    if (width == 0 || height == 0 || data.empty()) return 0.0f;
    const float wu = fracPos(u) * static_cast<float>(width) - 0.5f;
    const float wv = fracPos(v) * static_cast<float>(height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(wu));
    const int y0 = static_cast<int>(std::floor(wv));
    const float fx = wu - static_cast<float>(x0);
    const float fy = wv - static_cast<float>(y0);

    const float v00 = atWrapped(x0, y0);
    const float v10 = atWrapped(x0 + 1, y0);
    const float v01 = atWrapped(x0, y0 + 1);
    const float v11 = atWrapped(x0 + 1, y0 + 1);
    return lerpF(lerpF(v00, v10, fx), lerpF(v01, v11, fx), fy);
}

RGBAf ColorImage::sampleWrapped(float u, float v) const noexcept {
    if (width == 0 || height == 0 || pixels.empty()) return {};
    const float wu = fracPos(u) * static_cast<float>(width) - 0.5f;
    const float wv = fracPos(v) * static_cast<float>(height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(wu));
    const int y0 = static_cast<int>(std::floor(wv));
    const float fx = wu - static_cast<float>(x0);
    const float fy = wv - static_cast<float>(y0);

    auto atC = [&](int x, int y) -> const RGBAf& {
        const int wx = wrapPeriod(x, static_cast<int>(width));
        const int wy = wrapPeriod(y, static_cast<int>(height));
        return pixels[static_cast<size_t>(wy) * width + static_cast<size_t>(wx)];
    };

    const RGBAf& c00 = atC(x0, y0);
    const RGBAf& c10 = atC(x0 + 1, y0);
    const RGBAf& c01 = atC(x0, y0 + 1);
    const RGBAf& c11 = atC(x0 + 1, y0 + 1);

    return {
        lerpF(lerpF(c00.r, c10.r, fx), lerpF(c01.r, c11.r, fx), fy),
        lerpF(lerpF(c00.g, c10.g, fx), lerpF(c01.g, c11.g, fx), fy),
        lerpF(lerpF(c00.b, c10.b, fx), lerpF(c01.b, c11.b, fx), fy),
        lerpF(lerpF(c00.a, c10.a, fx), lerpF(c01.a, c11.a, fx), fy)
    };
}

std::vector<uint8_t> ColorImage::toRGBA8() const {
    std::vector<uint8_t> out(pixels.size() * 4u, 255u);
    for (size_t i = 0; i < pixels.size(); ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>(std::clamp(std::lround(pixels[i].r * 255.0f), 0L, 255L));
        out[i * 4 + 1] = static_cast<uint8_t>(std::clamp(std::lround(pixels[i].g * 255.0f), 0L, 255L));
        out[i * 4 + 2] = static_cast<uint8_t>(std::clamp(std::lround(pixels[i].b * 255.0f), 0L, 255L));
        out[i * 4 + 3] = static_cast<uint8_t>(std::clamp(std::lround(pixels[i].a * 255.0f), 0L, 255L));
    }
    return out;
}

RGBAf parseHexColor(const std::string& hexStr, RGBAf fallback) noexcept {
    std::string s = hexStr;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    auto hexDigit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (s.size() == 6 || s.size() == 8) {
        int vals[8]{};
        for (size_t i = 0; i < s.size(); ++i) {
            vals[i] = hexDigit(s[i]);
            if (vals[i] < 0) return fallback;
        }
        const float r = static_cast<float>((vals[0] << 4) | vals[1]) / 255.0f;
        const float g = static_cast<float>((vals[2] << 4) | vals[3]) / 255.0f;
        const float b = static_cast<float>((vals[4] << 4) | vals[5]) / 255.0f;
        const float a = (s.size() == 8) ? static_cast<float>((vals[6] << 4) | vals[7]) / 255.0f : 1.0f;
        return {r, g, b, a};
    }
    return fallback;
}

FloatField generateFbm(uint32_t width, uint32_t height,
                       float scaleX, float scaleY,
                       int octaves, float persistence, float lacunarity,
                       const std::string& mode, uint32_t seed) {
    FloatField out(width, height, 0.5f);
    octaves = std::clamp(octaves, 1, 10);
    const int basePx = std::max(1, static_cast<int>(std::lround(scaleX)));
    const int basePy = std::max(1, static_cast<int>(std::lround(scaleY)));
    const bool isValue = (mode == "value");
    const bool isRidge = (mode == "ridge");
    const bool isBillow = (mode == "billow" || mode == "turbulence");

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);

            float sum = 0.0f;
            float amp = 1.0f;
            float maxAmp = 0.0f;
            float freqMul = 1.0f;
            float weight = 1.0f;

            for (int o = 0; o < octaves; ++o) {
                const int px = std::max(1, static_cast<int>(std::lround(basePx * freqMul)));
                const int py = std::max(1, static_cast<int>(std::lround(basePy * freqMul)));
                const uint32_t octSeed = seed + static_cast<uint32_t>(o) * 1013u;

                float n = isValue ? valuePeriodic(u, v, px, py, octSeed)
                                  : perlinPeriodic(u, v, px, py, octSeed);
                if (isRidge) {
                    n = 1.0f - std::abs(n);
                    n = n * n * weight;
                    weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
                    sum += n * amp;
                } else if (isBillow) {
                    sum += std::abs(n) * amp;
                } else {
                    sum += (n * 0.5f + 0.5f) * amp;
                }
                maxAmp += amp;
                amp *= persistence;
                freqMul *= lacunarity;
            }

            float val = (maxAmp > 0.0f) ? (sum / maxAmp) : 0.5f;
            out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = std::clamp(val, 0.0f, 1.0f);
        }
    }
    return opNormalize(out);
}

FloatField generateWorley(uint32_t width, uint32_t height,
                          float scaleX, float scaleY,
                          float jitter, const std::string& mode,
                          const std::string& metric, uint32_t seed) {
    FloatField out(width, height, 0.0f);
    const int cellsX = std::max(1, static_cast<int>(std::lround(scaleX)));
    const int cellsY = std::max(1, static_cast<int>(std::lround(scaleY)));
    jitter = std::clamp(jitter, 0.0f, 1.0f);

    const int metricType = (metric == "manhattan") ? 1 : ((metric == "chebyshev") ? 2 : 0);
    const int modeType = (mode == "f2") ? 1 :
                         (mode == "f2_minus_f1") ? 2 :
                         (mode == "cracks") ? 3 :
                         (mode == "cell_id") ? 4 : 0;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float py = (static_cast<float>(y) / static_cast<float>(height)) * static_cast<float>(cellsY);
        const int cy = static_cast<int>(std::floor(py));
        const float fy = py - static_cast<float>(cy);

        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float px = (static_cast<float>(x) / static_cast<float>(width)) * static_cast<float>(cellsX);
            const int cx = static_cast<int>(std::floor(px));
            const float fx = px - static_cast<float>(cx);

            float f1 = 1e9f;
            float f2 = 1e9f;
            int bestCellX = 0;
            int bestCellY = 0;

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int wx = wrapPeriod(cx + dx, cellsX);
                    const int wy = wrapPeriod(cy + dy, cellsY);

                    const float rx = 0.5f + (hashFloat01(wx, wy, seed) - 0.5f) * jitter;
                    const float ry = 0.5f + (hashFloat01(wx, wy, seed + 7919u) - 0.5f) * jitter;

                    const float vx = static_cast<float>(dx) + rx - fx;
                    const float vy = static_cast<float>(dy) + ry - fy;

                    float dist = 0.0f;
                    if (metricType == 1) {
                        dist = std::abs(vx) + std::abs(vy);
                    } else if (metricType == 2) {
                        dist = std::max(std::abs(vx), std::abs(vy));
                    } else {
                        dist = std::sqrt(vx * vx + vy * vy);
                    }

                    if (dist < f1) {
                        f2 = f1;
                        f1 = dist;
                        bestCellX = wx;
                        bestCellY = wy;
                    } else if (dist < f2) {
                        f2 = dist;
                    }
                }
            }

            float val = 0.0f;
            if (modeType == 1) {
                val = std::clamp(f2 * 0.7f, 0.0f, 1.0f);
            } else if (modeType == 2) {
                val = std::clamp(f2 - f1, 0.0f, 1.0f);
            } else if (modeType == 3) {
                val = smoothstepF(0.01f, 0.14f, f2 - f1);
            } else if (modeType == 4) {
                val = hashFloat01(bestCellX, bestCellY, seed + 31337u);
            } else {
                val = std::clamp(f1 * 0.85f, 0.0f, 1.0f);
            }

            out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = val;
        }
    }
    return out;
}

FloatField generateGabor(uint32_t width, uint32_t height,
                         float scaleX, float scaleY,
                         float angleDeg, float bandwidth, uint32_t seed) {
    FloatField out(width, height, 0.5f);
    const int cellsX = std::max(2, static_cast<int>(std::lround(scaleX)));
    const int cellsY = std::max(2, static_cast<int>(std::lround(scaleY)));
    const float rad = angleDeg * (kPi / 180.0f);
    const float dirX = std::cos(rad);
    const float dirY = std::sin(rad);
    const float freq = std::max(1.0f, bandwidth * 2.5f);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float py = (static_cast<float>(y) / static_cast<float>(height)) * static_cast<float>(cellsY);
        const int cy = static_cast<int>(std::floor(py));
        const float fy = py - static_cast<float>(cy);

        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float px = (static_cast<float>(x) / static_cast<float>(width)) * static_cast<float>(cellsX);
            const int cx = static_cast<int>(std::floor(px));
            const float fx = px - static_cast<float>(cx);

            float sum = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int wx = wrapPeriod(cx + dx, cellsX);
                    const int wy = wrapPeriod(cy + dy, cellsY);
                    for (int k = 0; k < 3; ++k) {
                        const uint32_t ks = seed + static_cast<uint32_t>(k) * 4099u;
                        const float rx = hashFloat01(wx, wy, ks);
                        const float ry = hashFloat01(wx, wy, ks + 17u);
                        const float phase = hashFloat01(wx, wy, ks + 53u) * kTwoPi;

                        const float vx = static_cast<float>(dx) + rx - fx;
                        const float vy = static_cast<float>(dy) + ry - fy;
                        const float r2 = vx * vx + vy * vy;
                        if (r2 < 1.8f) {
                            const float env = std::exp(-2.2f * r2);
                            const float proj = vx * dirX + vy * dirY;
                            sum += env * std::cos(kTwoPi * freq * proj + phase);
                        }
                    }
                }
            }
            out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = sum;
        }
    }
    return opNormalize(out);
}

FloatField generateWoodGrain(uint32_t width, uint32_t height,
                             float rings, float turbulence,
                             float ringSharpness, float poreDensity,
                             float fiberStrength, float angleDeg, uint32_t seed,
                             const FloatField* cellIdField,
                             float centerU, float centerV) {
    FloatField out(width, height, 0.5f);
    const float rad = (angleDeg - 90.0f) * (kPi / 180.0f);
    const float cosA = std::cos(rad);
    const float sinA = std::sin(rad);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float v0 = static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float u0 = static_cast<float>(x) / static_cast<float>(width);
            const size_t pixIdx = static_cast<size_t>(y) * width + static_cast<size_t>(x);

            // Per-plank randomization when cellIdField is supplied
            float cellId = (cellIdField && pixIdx < cellIdField->data.size()) ? cellIdField->data[pixIdx] : 0.5f;
            const int plankHashIdx = static_cast<int>(std::lround(cellId * 4096.0f));
            const float offsetU = (cellIdField != nullptr) ? hashFloat01(plankHashIdx, 11, seed) * 7.3f : 0.0f;
            const float offsetV = (cellIdField != nullptr) ? hashFloat01(plankHashIdx, 29, seed) * 7.3f : 0.0f;
            const float plankToneOffset = (cellIdField != nullptr) ? (hashFloat01(plankHashIdx, 53, seed) - 0.5f) * 0.22f : 0.0f;
            const float plankRingScale = (cellIdField != nullptr) ? (0.75f + 0.55f * hashFloat01(plankHashIdx, 71, seed)) : 1.0f;

            // Rotate around center; ru is across-grain, rv is along-grain
            const float ru = (u0 - 0.5f) * cosA - (v0 - 0.5f) * sinA + 0.5f + offsetU;
            const float rv = (u0 - 0.5f) * sinA + (v0 - 0.5f) * cosA + 0.5f + offsetV;

            // Multi-octave organic growth turbulence (stretched along rv)
            const float warp1 = perlinPeriodic(ru, rv, 4, 2, seed + 11u);
            const float warp2 = perlinPeriodic(ru, rv, 8, 3, seed + 37u) * 0.45f;
            const float warp3 = perlinPeriodic(ru, rv, 16, 6, seed + 73u) * 0.20f;
            const float turbDisp = (warp1 + warp2 + warp3) * turbulence * 0.14f;

            // 3D cylindrical log slice: off-canvas center creates natural cathedral arches without bullseye dead-spots
            const float baseCenU = (cellIdField != nullptr) ? (-1.5f + offsetU + (hashFloat01(plankHashIdx, 97, seed) - 0.5f) * 0.6f) : centerU;
            const float baseCenV = (cellIdField != nullptr) ? (0.5f + offsetV) : centerV;
            const float dx = (ru - baseCenU) + turbDisp;
            const float dy = (rv - baseCenV) * 0.22f;
            const float cylDist = std::sqrt(dx * dx + dy * dy + 0.001f);

            const float ringPhase = fracPos(cylDist * std::max(1.0f, rings * plankRingScale));
            // Asymmetric earlywood (wide bright) vs latewood (narrow dark ring) profile
            const float sharp = std::clamp(ringSharpness, 0.1f, 0.95f);
            const float rise = smoothstepF(0.0f, 1.0f - sharp * 0.55f, ringPhase);
            const float fall = 1.0f - smoothstepF(1.0f - sharp * 0.55f, 1.0f, ringPhase);
            const float ringVal = 0.35f + 0.65f * (rise * fall * 1.35f);

            // High-frequency anisotropic cellulose fibers (dense across grain, continuous along grain, no block artifacts)
            const float f1 = perlinPeriodic(ru, rv, 128, 4, seed + 101u);
            const float f2 = perlinPeriodic(ru, rv, 256, 8, seed + 203u);
            const float f3 = perlinPeriodic(ru * 2.0f, rv, 256, 4, seed + 307u);
            const float fiber = (f1 * 0.50f + f2 * 0.35f + f3 * 0.15f) * 0.5f + 0.5f;

            // Fine sunken hardwood vessel pore ticks
            float poreDip = 0.0f;
            if (poreDensity > 0.001f) {
                const float pNoise = perlinPeriodic(ru, rv, 192, 16, seed + 509u) * 0.5f + 0.5f;
                const float thresh = 1.0f - std::clamp(poreDensity * 0.32f, 0.02f, 0.35f);
                if (pNoise > thresh) {
                    poreDip = smoothstepF(thresh, 1.0f, pNoise) * 0.24f;
                }
            }

            float h = ringVal * (1.0f - fiberStrength) + fiber * fiberStrength - poreDip + plankToneOffset;
            out.data[pixIdx] = std::clamp(h, 0.0f, 1.0f);
        }
    }
    return opNormalize(out);
}

FloatField generateBrickGrid(uint32_t width, uint32_t height,
                             int cols, int rows,
                             float mortarWidth, float bevelWidth,
                             float rowOffset, float heightVariance,
                             float slopeVariance, uint32_t seed,
                             const std::string& mode) {
    FloatField out(width, height, 0.0f);
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    mortarWidth = std::clamp(mortarWidth, 0.0f, 0.45f);
    bevelWidth = std::clamp(bevelWidth, 0.001f, 0.45f);
    const bool isCellIdMode = (mode == "cell_id" || mode == "id");

    const float aspect = static_cast<float>(cols) / static_cast<float>(rows);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(height);
        const float ry = v * static_cast<float>(rows);
        const int rowIdx = wrapPeriod(static_cast<int>(std::floor(ry)), rows);
        const float localY = ry - std::floor(ry);

        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float shiftedU = u + static_cast<float>(rowIdx) * rowOffset / static_cast<float>(cols);
            const float rx = fracPos(shiftedU) * static_cast<float>(cols);
            const int colIdx = wrapPeriod(static_cast<int>(std::floor(rx)), cols);
            const float localX = rx - std::floor(rx);

            if (isCellIdMode) {
                out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] =
                    hashFloat01(colIdx, rowIdx, seed);
                continue;
            }

            // Distance to brick edge corrected for brick aspect ratio
            const float dx = (cols > 1) ? std::min(localX, 1.0f - localX) : 1.0f;
            const float dy = (rows > 1) ? std::min(localY, 1.0f - localY) : 1.0f;
            const float edgeDist = (cols > 1 && rows > 1) ? std::min(dx, dy * aspect) : ((cols > 1) ? dx : dy);

            const float halfMortar = mortarWidth * 0.5f;
            const float bevelMask = smoothstepF(halfMortar, halfMortar + bevelWidth, edgeDist);

            // Per-brick random elevation and 2D planar tilt
            const float brickRandH = (hashFloat01(colIdx, rowIdx, seed) - 0.5f) * heightVariance;
            const float tiltX = (hashFloat01(colIdx, rowIdx, seed + 17u) - 0.5f) * slopeVariance;
            const float tiltY = (hashFloat01(colIdx, rowIdx, seed + 31u) - 0.5f) * slopeVariance;
            const float planarSlope = (localX - 0.5f) * tiltX + (localY - 0.5f) * tiltY;

            const float brickTop = std::clamp(0.84f + brickRandH + planarSlope, 0.15f, 1.0f);
            const float h = bevelMask * brickTop;
            out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = std::clamp(h, 0.0f, 1.0f);
        }
    }
    return out;
}

FloatField generateWeave(uint32_t width, uint32_t height,
                         int scaleX, int scaleY,
                         const std::string& pattern,
                         float threadWidth, float slub, uint32_t seed,
                         const std::string& mode) {
    FloatField out(width, height, 0.0f);
    scaleX = std::max(2, scaleX);
    scaleY = std::max(2, scaleY);
    threadWidth = std::clamp(threadWidth, 0.25f, 0.98f);
    slub = std::clamp(slub, 0.0f, 0.6f);

    const bool isWarpMask = (mode == "warp_mask");
    const bool isWeftMask = (mode == "weft_mask");
    const bool isThreadId = (mode == "thread_id");

    const int patType = (pattern == "basket") ? 1 :
                        (pattern == "twill")  ? 2 :
                        (pattern == "satin")  ? 3 :
                        (pattern == "knit")   ? 4 : 0;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(height);
        const float gy = v * static_cast<float>(scaleY);
        const int iy = wrapPeriod(static_cast<int>(std::floor(gy)), scaleY);
        const float fy = gy - std::floor(gy);

        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float gx = u * static_cast<float>(scaleX);
            const int ix = wrapPeriod(static_cast<int>(std::floor(gx)), scaleX);
            const float fx = gx - std::floor(gx);

            if (patType == 4) {
                // Interlocking V-shaped Jersey Knit Wales
                const float chevronOffset = (fx < 0.5f) ? (fx * 1.1f) : ((1.0f - fx) * 1.1f);
                const float loopY = fracPos(fy + chevronOffset);
                const float ribProfile = std::sin(fx * kPi);
                const float loopProfile = 0.5f + 0.5f * std::sin(loopY * kTwoPi);
                const float yarnNoise = perlinPeriodic(u, v, scaleX * 2, scaleY * 2, seed) * slub;
                if (isWarpMask) {
                    out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = std::clamp(ribProfile, 0.0f, 1.0f);
                } else if (isWeftMask) {
                    out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = 1.0f - std::clamp(ribProfile, 0.0f, 1.0f);
                } else if (isThreadId) {
                    out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = (fx < 0.5f) ? 1.0f : 0.0f;
                } else {
                    const float h = std::clamp(ribProfile * (0.55f + 0.45f * loopProfile) + yarnNoise, 0.0f, 1.0f);
                    out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = h;
                }
                continue;
            }

            // Determine if Warp (vertical X-indexed thread) is on top of Weft (horizontal Y-indexed thread)
            bool warpOnTop = false;
            if (patType == 1) {
                // 2x2 Basketweave / Hopsack
                warpOnTop = (((ix / 2) + (iy / 2)) % 2 == 0);
            } else if (patType == 2) {
                // 2/1 Diagonal Denim Twill
                warpOnTop = (((ix + iy) % 3) != 0);
            } else if (patType == 3) {
                // 4/1 Satin weave
                warpOnTop = (((ix + iy * 2) % 5) != 0);
            } else {
                // 1/1 Plain Weave
                warpOnTop = (((ix + iy) % 2) == 0);
            }

            if (isThreadId) {
                out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = warpOnTop ? 1.0f : 0.0f;
                continue;
            }

            // Longitudinal slub modulation along each thread
            const float warpSlub = 1.0f + perlinPeriodic(0.1f, v, 4, std::max(2, scaleY / 2), seed + static_cast<uint32_t>(ix) * 19u) * slub;
            const float weftSlub = 1.0f + perlinPeriodic(u, 0.1f, std::max(2, scaleX / 2), 4, seed + static_cast<uint32_t>(iy) * 37u) * slub;

            const float halfW = 0.5f * threadWidth;
            const float distWarp = std::abs(fx - 0.5f) / std::max(0.1f, halfW * warpSlub);
            const float distWeft = std::abs(fy - 0.5f) / std::max(0.1f, halfW * weftSlub);

            if (isWarpMask || isWeftMask) {
                float warpMask = 0.0f;
                if (distWarp < 1.0f && distWeft < 1.0f) {
                    if (warpOnTop) {
                        const float edge = std::clamp((1.0f - distWarp) * 6.0f, 0.0f, 1.0f);
                        warpMask = edge;
                    } else {
                        const float weftEdge = std::clamp((1.0f - distWeft) * 6.0f, 0.0f, 1.0f);
                        warpMask = 1.0f - weftEdge;
                    }
                } else if (distWarp < 1.0f) {
                    warpMask = 1.0f;
                } else if (distWeft < 1.0f) {
                    warpMask = 0.0f;
                } else {
                    warpMask = warpOnTop ? 0.6f : 0.4f;
                }

                out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = isWeftMask ? (1.0f - warpMask) : warpMask;
                continue;
            }

            const float warpProfile = (distWarp < 1.0f) ? std::sqrt(1.0f - distWarp * distWarp) : 0.0f;
            const float weftProfile = (distWeft < 1.0f) ? std::sqrt(1.0f - distWeft * distWeft) : 0.0f;

            // Over-under wave crimp along thread length
            const float warpCrimp = warpOnTop ? (0.72f + 0.28f * std::sin(fy * kPi))
                                              : (0.32f - 0.15f * std::sin(fy * kPi));
            const float weftCrimp = (!warpOnTop) ? (0.72f + 0.28f * std::sin(fx * kPi))
                                                 : (0.32f - 0.15f * std::sin(fx * kPi));

            const float hWarp = warpProfile * warpCrimp;
            const float hWeft = weftProfile * weftCrimp;
            out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = std::clamp(std::max(hWarp, hWeft), 0.0f, 1.0f);
        }
    }
    return out;
}

FloatField generateScratches(uint32_t width, uint32_t height,
                             int count, float length, float widthNorm,
                             float angleDeg, float angleSpreadDeg, uint32_t seed) {
    FloatField out(width, height, 0.0f);
    count = std::clamp(count, 1, 2000);
    length = std::clamp(length, 0.01f, 0.9f);
    widthNorm = std::clamp(widthNorm, 0.0005f, 0.05f);

    struct ScratchSeg {
        float x0, y0, dx, dy, len2, strength, halfW;
    };
    std::vector<ScratchSeg> segs;
    segs.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        const uint32_t s = seed + static_cast<uint32_t>(i) * 7919u;
        const float cx = hashFloat01(i, 0, s);
        const float cy = hashFloat01(i, 1, s);
        const float ang = (angleDeg + (hashFloat01(i, 2, s) - 0.5f) * angleSpreadDeg) * (kPi / 180.0f);
        const float segLen = length * (0.4f + 0.8f * hashFloat01(i, 3, s));
        const float dx = std::cos(ang) * segLen;
        const float dy = std::sin(ang) * segLen;
        const float str = 0.4f + 0.6f * hashFloat01(i, 4, s);
        const float hw = widthNorm * (0.6f + 0.7f * hashFloat01(i, 5, s));
        segs.push_back({cx - dx * 0.5f, cy - dy * 0.5f, dx, dy, std::max(1e-7f, dx * dx + dy * dy), str, hw});
    }

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(height); ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            float maxScratch = 0.0f;

            for (const auto& seg : segs) {
                // Toroidal shortest delta from seg.x0, seg.y0
                float px = u - seg.x0;
                float py = v - seg.y0;
                px -= std::round(px);
                py -= std::round(py);

                const float t = std::clamp((px * seg.dx + py * seg.dy) / seg.len2, 0.0f, 1.0f);
                const float ex = px - t * seg.dx;
                const float ey = py - t * seg.dy;
                const float dist = std::sqrt(ex * ex + ey * ey);
                if (dist < seg.halfW) {
                    const float taper = std::sin(t * kPi);
                    const float profile = (1.0f - dist / seg.halfW) * taper * seg.strength;
                    maxScratch = std::max(maxScratch, profile);
                }
            }
            out.data[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = std::clamp(maxScratch, 0.0f, 1.0f);
        }
    }
    return out;
}

FloatField opWarp(const FloatField& src,
                  const FloatField& warpX,
                  const FloatField& warpY,
                  float strength) {
    FloatField out(src.width, src.height, 0.0f);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.height);
        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.width);
            const float wx = (warpX.sampleWrapped(u, v) - 0.5f) * strength;
            const float wy = (warpY.sampleWrapped(u, v) - 0.5f) * strength;
            out.data[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] = src.sampleWrapped(u + wx, v + wy);
        }
    }
    return out;
}

FloatField opDirectionalWarp(const FloatField& src,
                             const FloatField& intensityField,
                             float angleDeg,
                             float strength) {
    FloatField out(src.width, src.height, 0.0f);
    const float rad = angleDeg * (kPi / 180.0f);
    const float dirX = std::cos(rad) * strength;
    const float dirY = std::sin(rad) * strength;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.height);
        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.width);
            const float m = intensityField.sampleWrapped(u, v) - 0.5f;
            out.data[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] = src.sampleWrapped(u + dirX * m, v + dirY * m);
        }
    }
    return out;
}

FloatField opSlopeBlur(const FloatField& src,
                       const FloatField& slopeField,
                       float strength,
                       int samples,
                       const std::string& mode) {
    FloatField out(src.width, src.height, 0.0f);
    samples = std::clamp(samples, 1, 32);
    const int modeType = (mode == "min") ? 0 : ((mode == "max") ? 1 : 2);
    const float stepScale = strength / static_cast<float>(samples);
    const float du = 2.0f / static_cast<float>(std::max(1u, slopeField.width));
    const float dv = 2.0f / static_cast<float>(std::max(1u, slopeField.height));

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        const float v0 = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.height);
        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            const float u0 = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.width);

            float cu = u0;
            float cv = v0;
            float accum = src.sampleWrapped(cu, cv);
            float minVal = accum;
            float maxVal = accum;

            for (int s = 0; s < samples; ++s) {
                const float gx = (slopeField.sampleWrapped(cu + du, cv) - slopeField.sampleWrapped(cu - du, cv));
                const float gy = (slopeField.sampleWrapped(cu, cv + dv) - slopeField.sampleWrapped(cu, cv - dv));
                cu += gx * stepScale * 0.45f;
                cv += gy * stepScale * 0.45f;
                const float val = src.sampleWrapped(cu, cv);
                accum += val;
                minVal = std::min(minVal, val);
                maxVal = std::max(maxVal, val);
            }

            float finalVal = accum / static_cast<float>(samples + 1);
            if (modeType == 0) finalVal = minVal;
            else if (modeType == 1) finalVal = maxVal;
            out.data[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] = finalVal;
        }
    }
    return out;
}

FloatField opBlur(const FloatField& src, float radius) {
    const int r = std::clamp(static_cast<int>(std::ceil(radius)), 1, 32);
    FloatField temp(src.width, src.height, 0.0f);
    FloatField out(src.width, src.height, 0.0f);

    std::vector<float> weights(static_cast<size_t>(2 * r + 1));
    float wSum = 0.0f;
    const float sigma = std::max(0.5f, radius * 0.5f);
    for (int i = -r; i <= r; ++i) {
        const float w = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
        weights[static_cast<size_t>(i + r)] = w;
        wSum += w;
    }
    for (float& w : weights) w /= wSum;

    // Horizontal pass
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            float sum = 0.0f;
            for (int k = -r; k <= r; ++k) {
                sum += src.atWrapped(x + k, y) * weights[static_cast<size_t>(k + r)];
            }
            temp.data[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] = sum;
        }
    }

    // Vertical pass
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            float sum = 0.0f;
            for (int k = -r; k <= r; ++k) {
                sum += temp.atWrapped(x, y + k) * weights[static_cast<size_t>(k + r)];
            }
            out.data[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] = sum;
        }
    }
    return out;
}

FloatField opBevel(const FloatField& src, float widthNorm) {
    const float radiusPx = std::max(1.0f, widthNorm * static_cast<float>(std::max(src.width, src.height)));
    return opBlur(src, radiusPx);
}

FloatField opMakeSeamless(const FloatField& src, float blendWidth) {
    if (src.width == 0 || src.height == 0 || src.data.empty()) return src;
    blendWidth = std::clamp(blendWidth, 0.01f, 0.49f);
    FloatField out(src.width, src.height);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.height);
        float wy = 1.0f;
        if (v < blendWidth) {
            wy = smoothstepF(0.0f, blendWidth, v);
        } else if (v > 1.0f - blendWidth) {
            wy = 1.0f - smoothstepF(1.0f - blendWidth, 1.0f, v);
        }

        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.width);
            float wx = 1.0f;
            if (u < blendWidth) {
                wx = smoothstepF(0.0f, blendWidth, u);
            } else if (u > 1.0f - blendWidth) {
                wx = 1.0f - smoothstepF(1.0f - blendWidth, 1.0f, u);
            }

            const float wPrimary = wx * wy;
            const float valCenter = src.atWrapped(x, y);
            const float valShifted = src.sampleWrapped(u + 0.5f, v + 0.5f);

            out.data[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] =
                lerpF(valShifted, valCenter, wPrimary);
        }
    }
    return out;
}

ColorImage opMakeSeamlessColor(const ColorImage& src, float blendWidth) {
    if (src.width == 0 || src.height == 0 || src.pixels.empty()) return src;
    blendWidth = std::clamp(blendWidth, 0.01f, 0.49f);
    ColorImage out(src.width, src.height);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(src.height); ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(src.height);
        float wy = 1.0f;
        if (v < blendWidth) {
            wy = smoothstepF(0.0f, blendWidth, v);
        } else if (v > 1.0f - blendWidth) {
            wy = 1.0f - smoothstepF(1.0f - blendWidth, 1.0f, v);
        }

        for (int x = 0; x < static_cast<int>(src.width); ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(src.width);
            float wx = 1.0f;
            if (u < blendWidth) {
                wx = smoothstepF(0.0f, blendWidth, u);
            } else if (u > 1.0f - blendWidth) {
                wx = 1.0f - smoothstepF(1.0f - blendWidth, 1.0f, u);
            }

            const float wPrimary = wx * wy;
            const RGBAf colCenter = src.pixels[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)];
            const RGBAf colShifted = src.sampleWrapped(u + 0.5f, v + 0.5f);

            RGBAf res;
            res.r = lerpF(colShifted.r, colCenter.r, wPrimary);
            res.g = lerpF(colShifted.g, colCenter.g, wPrimary);
            res.b = lerpF(colShifted.b, colCenter.b, wPrimary);
            res.a = lerpF(colShifted.a, colCenter.a, wPrimary);
            out.pixels[static_cast<size_t>(y) * src.width + static_cast<size_t>(x)] = res;
        }
    }
    return out;
}

FloatField opRemap(const FloatField& src, float inMin, float inMax, float outMin, float outMax) {
    FloatField out(src.width, src.height, 0.0f);
    const float denom = (std::abs(inMax - inMin) < 1e-6f) ? 1.0f : (inMax - inMin);
    for (size_t i = 0; i < src.data.size(); ++i) {
        const float t = std::clamp((src.data[i] - inMin) / denom, 0.0f, 1.0f);
        out.data[i] = lerpF(outMin, outMax, t);
    }
    return out;
}

FloatField opHistogramScan(const FloatField& src, float position, float contrast) {
    FloatField out(src.width, src.height, 0.0f);
    position = std::clamp(position, 0.0f, 1.0f);
    contrast = std::clamp(contrast, 0.0f, 0.9999f);

    const float span = std::max(1.0f - contrast, 1e-4f);
    const float low = position * (1.0f + span) - span;
    const float invSpan = 1.0f / span;

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(src.data.size()); ++i) {
        const float val = src.data[static_cast<size_t>(i)];
        out.data[static_cast<size_t>(i)] = std::clamp((val - low) * invSpan, 0.0f, 1.0f);
    }
    return out;
}

FloatField opHighpass(const FloatField& src, float radius, float strength) {
    FloatField blurred = opBlur(src, radius);
    FloatField out(src.width, src.height, 0.0f);
    strength = std::max(0.0f, strength);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(src.data.size()); ++i) {
        const float diff = src.data[static_cast<size_t>(i)] - blurred.data[static_cast<size_t>(i)];
        out.data[static_cast<size_t>(i)] = std::clamp(diff * strength + 0.5f, 0.0f, 1.0f);
    }
    return out;
}

FloatField opSmoothstep(const FloatField& src, float edge0, float edge1) {
    FloatField out(src.width, src.height, 0.0f);
    for (size_t i = 0; i < src.data.size(); ++i) {
        out.data[i] = smoothstepF(edge0, edge1, src.data[i]);
    }
    return out;
}

FloatField opPow(const FloatField& src, float exponent) {
    FloatField out(src.width, src.height, 0.0f);
    const float expClamped = std::max(0.01f, exponent);
    for (size_t i = 0; i < src.data.size(); ++i) {
        out.data[i] = std::pow(std::clamp(src.data[i], 0.0f, 1.0f), expClamped);
    }
    return out;
}

FloatField opInvert(const FloatField& src) {
    FloatField out(src.width, src.height, 0.0f);
    for (size_t i = 0; i < src.data.size(); ++i) {
        out.data[i] = 1.0f - std::clamp(src.data[i], 0.0f, 1.0f);
    }
    return out;
}

FloatField opNormalize(const FloatField& src) {
    FloatField out = src;
    if (out.data.empty()) return out;
    float minV = std::numeric_limits<float>::max();
    float maxV = std::numeric_limits<float>::lowest();
    for (float v : out.data) {
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    const float span = maxV - minV;
    if (span > 1e-6f) {
        for (float& v : out.data) {
            v = (v - minV) / span;
        }
    }
    return out;
}

FloatField opBlendFields(const FloatField& a,
                         const FloatField& b,
                         const std::string& mode,
                         float weight) {
    FloatField out(a.width, a.height, 0.0f);
    weight = std::clamp(weight, 0.0f, 1.0f);
    const size_t n = std::min(a.data.size(), b.data.size());

    for (size_t i = 0; i < n; ++i) {
        const float va = a.data[i];
        const float vb = b.data[i];
        float res = va;
        if (mode == "add") {
            res = va + vb * weight;
        } else if (mode == "subtract" || mode == "sub") {
            res = va - vb * weight;
        } else if (mode == "multiply" || mode == "mul") {
            res = lerpF(va, va * vb, weight);
        } else if (mode == "screen") {
            const float sc = 1.0f - (1.0f - va) * (1.0f - vb);
            res = lerpF(va, sc, weight);
        } else if (mode == "overlay") {
            const float ov = (va < 0.5f) ? (2.0f * va * vb) : (1.0f - 2.0f * (1.0f - va) * (1.0f - vb));
            res = lerpF(va, ov, weight);
        } else if (mode == "soft_light") {
            const float sl = (1.0f - 2.0f * vb) * va * va + 2.0f * vb * va;
            res = lerpF(va, sl, weight);
        } else if (mode == "min") {
            res = lerpF(va, std::min(va, vb), weight);
        } else if (mode == "max") {
            res = lerpF(va, std::max(va, vb), weight);
        } else if (mode == "abs_diff") {
            res = lerpF(va, std::abs(va - vb), weight);
        } else {
            res = lerpF(va, vb, weight);
        }
        out.data[i] = std::clamp(res, 0.0f, 1.0f);
    }
    return out;
}

FloatField opHeightBlend(const FloatField& heightA,
                         const FloatField& heightB,
                         float offset,
                         float contrast) {
    FloatField mask(heightA.width, heightA.height, 0.0f);
    const float halfWidth = std::max(0.005f, (1.0f - std::clamp(contrast, 0.0f, 0.99f)) * 0.5f);
    const size_t n = std::min(heightA.data.size(), heightB.data.size());
    for (size_t i = 0; i < n; ++i) {
        const float diff = (heightB.data[i] + offset) - heightA.data[i];
        mask.data[i] = smoothstepF(-halfWidth, halfWidth, diff);
    }
    return mask;
}

FloatField computeCurvature(const FloatField& heightField, float strength, int radius) {
    FloatField out(heightField.width, heightField.height, 0.5f);
    radius = std::clamp(radius, 1, 16);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(heightField.height); ++y) {
        for (int x = 0; x < static_cast<int>(heightField.width); ++x) {
            const float c = heightField.atWrapped(x, y);
            const float l = heightField.atWrapped(x - radius, y);
            const float r = heightField.atWrapped(x + radius, y);
            const float u = heightField.atWrapped(x, y - radius);
            const float d = heightField.atWrapped(x, y + radius);
            // Positive curvature when center is higher than neighbors (convex ridge)
            const float lap = (4.0f * c - (l + r + u + d)) * strength;
            out.data[static_cast<size_t>(y) * heightField.width + static_cast<size_t>(x)] =
                std::clamp(0.5f + lap, 0.0f, 1.0f);
        }
    }
    return out;
}

FloatField computeAmbientOcclusion(const FloatField& heightField, int radius, float intensity) {
    FloatField out(heightField.width, heightField.height, 1.0f);
    radius = std::clamp(radius, 1, 32);

    constexpr int kDirs = 8;
    const int dx[kDirs] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy[kDirs] = {0, 1, 1, 1, 0, -1, -1, -1};

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(heightField.height); ++y) {
        for (int x = 0; x < static_cast<int>(heightField.width); ++x) {
            const float h0 = heightField.atWrapped(x, y);
            float occSum = 0.0f;

            for (int d = 0; d < kDirs; ++d) {
                float maxSlope = 0.0f;
                for (int s = 1; s <= radius; ++s) {
                    const float hs = heightField.atWrapped(x + dx[d] * s, y + dy[d] * s);
                    const float dh = hs - h0;
                    if (dh > 0.0f) {
                        const float dist = std::hypot(static_cast<float>(dx[d] * s), static_cast<float>(dy[d] * s));
                        maxSlope = std::max(maxSlope, (dh * 12.0f) / dist);
                    }
                }
                occSum += std::atan(maxSlope) / (0.5f * kPi);
            }

            const float ao = 1.0f - (occSum / static_cast<float>(kDirs)) * intensity;
            out.data[static_cast<size_t>(y) * heightField.width + static_cast<size_t>(x)] =
                std::clamp(ao, 0.0f, 1.0f);
        }
    }
    return out;
}

ColorImage computeNormalMap(const FloatField& heightField, float strength) {
    ColorImage out(heightField.width, heightField.height);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < static_cast<int>(heightField.height); ++y) {
        for (int x = 0; x < static_cast<int>(heightField.width); ++x) {
            const float tl = heightField.atWrapped(x - 1, y - 1);
            const float l  = heightField.atWrapped(x - 1, y);
            const float bl = heightField.atWrapped(x - 1, y + 1);
            const float t  = heightField.atWrapped(x, y - 1);
            const float b  = heightField.atWrapped(x, y + 1);
            const float tr = heightField.atWrapped(x + 1, y - 1);
            const float r  = heightField.atWrapped(x + 1, y);
            const float br = heightField.atWrapped(x + 1, y + 1);

            const float dX = ((tr + 2.0f * r + br) - (tl + 2.0f * l + bl)) * 0.25f * strength;
            const float dY = ((bl + 2.0f * b + br) - (tl + 2.0f * t + tr)) * 0.25f * strength;

            float nx = -dX;
            float ny = -dY;
            float nz = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;

            out.pixels[static_cast<size_t>(y) * heightField.width + static_cast<size_t>(x)] = {
                nx * 0.5f + 0.5f,
                ny * 0.5f + 0.5f,
                nz * 0.5f + 0.5f,
                1.0f
            };
        }
    }
    return out;
}

ColorImage floatFieldToGrayscaleImage(const FloatField& field) {
    ColorImage out(field.width, field.height);
    for (size_t i = 0; i < field.data.size(); ++i) {
        const float v = std::clamp(field.data[i], 0.0f, 1.0f);
        out.pixels[i] = {v, v, v, 1.0f};
    }
    return out;
}

ColorImage applyGradientMap(const FloatField& field, const std::vector<ColorStop>& stops) {
    ColorImage out(field.width, field.height);
    if (stops.empty()) {
        return floatFieldToGrayscaleImage(field);
    }

    std::vector<ColorStop> sorted = stops;
    std::sort(sorted.begin(), sorted.end(), [](const ColorStop& a, const ColorStop& b) {
        return a.pos < b.pos;
    });

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(field.data.size()); ++i) {
        const float t = std::clamp(field.data[static_cast<size_t>(i)], 0.0f, 1.0f);
        if (t <= sorted.front().pos) {
            out.pixels[static_cast<size_t>(i)] = sorted.front().color;
            continue;
        }
        if (t >= sorted.back().pos) {
            out.pixels[static_cast<size_t>(i)] = sorted.back().color;
            continue;
        }
        for (size_t s = 0; s + 1 < sorted.size(); ++s) {
            if (t >= sorted[s].pos && t <= sorted[s + 1].pos) {
                const float span = std::max(1e-6f, sorted[s + 1].pos - sorted[s].pos);
                const float localT = (t - sorted[s].pos) / span;
                out.pixels[static_cast<size_t>(i)] = {
                    lerpF(sorted[s].color.r, sorted[s + 1].color.r, localT),
                    lerpF(sorted[s].color.g, sorted[s + 1].color.g, localT),
                    lerpF(sorted[s].color.b, sorted[s + 1].color.b, localT),
                    lerpF(sorted[s].color.a, sorted[s + 1].color.a, localT)
                };
                break;
            }
        }
    }
    return out;
}

ColorImage blendColorImages(const ColorImage& a,
                            const ColorImage& b,
                            const std::string& mode,
                            const FloatField* mask,
                            float weight) {
    ColorImage out(a.width, a.height);
    weight = std::clamp(weight, 0.0f, 1.0f);
    const size_t n = std::min(a.pixels.size(), b.pixels.size());

    auto blendChan = [&](float ca, float cb) -> float {
        if (mode == "multiply" || mode == "mul") return ca * cb;
        if (mode == "screen") return 1.0f - (1.0f - ca) * (1.0f - cb);
        if (mode == "overlay") return (ca < 0.5f) ? (2.0f * ca * cb) : (1.0f - 2.0f * (1.0f - ca) * (1.0f - cb));
        if (mode == "soft_light") return (1.0f - 2.0f * cb) * ca * ca + 2.0f * cb * ca;
        if (mode == "add") return std::min(1.0f, ca + cb);
        if (mode == "min") return std::min(ca, cb);
        if (mode == "max") return std::max(ca, cb);
        return cb;
    };

    for (size_t i = 0; i < n; ++i) {
        const float w = (mask && i < mask->data.size())
                            ? std::clamp(mask->data[i] * weight, 0.0f, 1.0f)
                            : weight;
        const RGBAf& pa = a.pixels[i];
        const RGBAf& pb = b.pixels[i];
        out.pixels[i] = {
            std::clamp(lerpF(pa.r, blendChan(pa.r, pb.r), w), 0.0f, 1.0f),
            std::clamp(lerpF(pa.g, blendChan(pa.g, pb.g), w), 0.0f, 1.0f),
            std::clamp(lerpF(pa.b, blendChan(pa.b, pb.b), w), 0.0f, 1.0f),
            1.0f
        };
    }
    return out;
}

ColorImage adjustColorImage(const ColorImage& src,
                            float brightness,
                            float contrast,
                            float saturation,
                            float gamma) {
    ColorImage out = src;
    const float invGamma = 1.0f / std::max(0.05f, gamma);
    for (auto& p : out.pixels) {
        float r = (p.r - 0.5f) * contrast + 0.5f + brightness;
        float g = (p.g - 0.5f) * contrast + 0.5f + brightness;
        float b = (p.b - 0.5f) * contrast + 0.5f + brightness;
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = lerpF(lum, r, saturation);
        g = lerpF(lum, g, saturation);
        b = lerpF(lum, b, saturation);
        p.r = std::pow(std::clamp(r, 0.0f, 1.0f), invGamma);
        p.g = std::pow(std::clamp(g, 0.0f, 1.0f), invGamma);
        p.b = std::pow(std::clamp(b, 0.0f, 1.0f), invGamma);
    }
    return out;
}

ColorImage splatterSpecks(const ColorImage& src,
                          int count,
                          RGBAf speckColor,
                          float radiusPx,
                          float opacity,
                          uint32_t seed) {
    ColorImage out = src;
    if (out.width == 0 || out.height == 0) return out;
    count = std::clamp(count, 0, 5000);
    radiusPx = std::clamp(radiusPx, 0.5f, 32.0f);
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    const int radInt = static_cast<int>(std::ceil(radiusPx));

    for (int i = 0; i < count; ++i) {
        const uint32_t s = seed + static_cast<uint32_t>(i) * 4099u;
        const float cx = hashFloat01(i, 0, s) * static_cast<float>(out.width);
        const float cy = hashFloat01(i, 1, s) * static_cast<float>(out.height);
        const float rCur = radiusPx * (0.5f + 0.7f * hashFloat01(i, 2, s));
        const float opCur = opacity * (0.5f + 0.5f * hashFloat01(i, 3, s));

        const int ix0 = static_cast<int>(std::floor(cx));
        const int iy0 = static_cast<int>(std::floor(cy));
        for (int dy = -radInt; dy <= radInt; ++dy) {
            for (int dx = -radInt; dx <= radInt; ++dx) {
                const float dist = std::hypot(static_cast<float>(ix0 + dx) - cx,
                                              static_cast<float>(iy0 + dy) - cy);
                if (dist <= rCur) {
                    const float falloff = (1.0f - dist / rCur) * opCur;
                    const int wx = wrapPeriod(ix0 + dx, static_cast<int>(out.width));
                    const int wy = wrapPeriod(iy0 + dy, static_cast<int>(out.height));
                    RGBAf& dst = out.pixels[static_cast<size_t>(wy) * out.width + static_cast<size_t>(wx)];
                    dst.r = lerpF(dst.r, speckColor.r, falloff);
                    dst.g = lerpF(dst.g, speckColor.g, falloff);
                    dst.b = lerpF(dst.b, speckColor.b, falloff);
                }
            }
        }
    }
    return out;
}

ColorImage shadePBR(const ColorImage& albedo,
                    const FloatField& heightField,
                    const FloatField* roughnessField,
                    float roughnessScalar,
                    float metallicScalar,
                    float normalStrength,
                    float aoIntensity,
                    float lightAzimuthDeg,
                    float lightElevationDeg,
                    float lightIntensity,
                    float ambientIntensity) {
    ColorImage out(albedo.width, albedo.height);
    const ColorImage normalMap = computeNormalMap(heightField, normalStrength);
    const FloatField aoMap = (aoIntensity > 0.01f)
                                 ? computeAmbientOcclusion(heightField, 6, aoIntensity)
                                 : FloatField(albedo.width, albedo.height, 1.0f);
    const FloatField curvMap = computeCurvature(heightField, 0.85f, 1);

    const float azRad = lightAzimuthDeg * (kPi / 180.0f);
    const float elRad = std::clamp(lightElevationDeg, 5.0f, 89.0f) * (kPi / 180.0f);
    const float lx = std::cos(elRad) * std::cos(azRad);
    const float ly = std::cos(elRad) * std::sin(azRad);
    const float lz = std::sin(elRad);

    // Halfway vector H between Light L and View V=(0, 0, 1)
    float hx = lx;
    float hy = ly;
    float hz = lz + 1.0f;
    const float hLen = std::sqrt(hx * hx + hy * hy + hz * hz);
    hx /= hLen;
    hy /= hLen;
    hz /= hLen;

    metallicScalar = std::clamp(metallicScalar, 0.0f, 1.0f);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(albedo.pixels.size()); ++i) {
        const size_t idx = static_cast<size_t>(i);
        const RGBAf& baseCol = albedo.pixels[idx];
        const RGBAf& nCol = normalMap.pixels[idx];
        const float nx = nCol.r * 2.0f - 1.0f;
        const float ny = nCol.g * 2.0f - 1.0f;
        const float nz = nCol.b * 2.0f - 1.0f;

        const float rough = std::clamp(
            (roughnessField && idx < roughnessField->data.size()) ? roughnessField->data[idx] : roughnessScalar,
            0.04f, 1.0f
        );
        const float ao = aoMap.data[idx];
        const float curv = curvMap.data[idx]; // 0.5 = neutral, >0.5 ridge, <0.5 crevice

        const float nDotL = std::max(0.0f, nx * lx + ny * ly + nz * lz);
        const float nDotH = std::max(0.0f, nx * hx + ny * hy + nz * hz);

        // GGX / Blinn-Phong microfacet specular approximation
        const float shininess = std::pow(2.0f, (1.0f - rough) * 9.5f);
        const float specStrength = (1.0f - rough * 0.82f) * (0.08f + 0.75f * metallicScalar);
        const float spec = std::pow(nDotH, shininess) * specStrength * lightIntensity * ao;

        // Subtle micro-curvature ridge highlight & crevice darkening
        const float cavityMod = 1.0f + (curv - 0.5f) * 0.24f;

        const float diff = (ambientIntensity * ao + nDotL * lightIntensity * (0.4f + 0.6f * ao)) * cavityMod;
        const float specR = lerpF(1.0f, baseCol.r, metallicScalar) * spec;
        const float specG = lerpF(1.0f, baseCol.g, metallicScalar) * spec;
        const float specB = lerpF(1.0f, baseCol.b, metallicScalar) * spec;

        const float diffWeight = 1.0f - metallicScalar * 0.65f;
        out.pixels[idx] = {
            std::clamp(baseCol.r * diff * diffWeight + specR, 0.0f, 1.0f),
            std::clamp(baseCol.g * diff * diffWeight + specG, 0.0f, 1.0f),
            std::clamp(baseCol.b * diff * diffWeight + specB, 0.0f, 1.0f),
            1.0f
        };
    }
    return out;
}

std::vector<RGBAf> getBuiltinPalette(const std::string& name) {
    auto parseList = [](std::initializer_list<const char*> hexes) {
        std::vector<RGBAf> pal;
        pal.reserve(hexes.size());
        for (const char* h : hexes) pal.push_back(parseHexColor(h));
        return pal;
    };

    if (name == "pico8") {
        return parseList({
            "#000000", "#1D2B53", "#7E2553", "#008751",
            "#AB5236", "#5F574F", "#C2C3C7", "#FFF1E8",
            "#FF004D", "#FFA300", "#FFEC27", "#00E436",
            "#29ADFF", "#83769C", "#FF77A8", "#FFCCAA"
        });
    }
    if (name == "gameboy") {
        return parseList({"#0f380f", "#306230", "#8bac0f", "#9bbc0f"});
    }
    if (name == "cga") {
        return parseList({"#000000", "#55ffff", "#ff55ff", "#ffffff"});
    }
    if (name == "grayscale4") {
        return parseList({"#141414", "#555555", "#aaaaaa", "#f0f0f0"});
    }
    if (name == "grayscale8") {
        return parseList({"#0d0d0d", "#2b2b2b", "#494949", "#686868", "#8a8a8a", "#acacac", "#cecece", "#f2f2f2"});
    }
    // Default: Endesga 32 (Classic rich 32-color pixel-art palette)
    return parseList({
        "#be4a2f", "#d77643", "#ead4aa", "#e4a672", "#b86f50", "#733e39", "#3e2731", "#a22633",
        "#e43b44", "#f77622", "#feae34", "#fee761", "#63c74d", "#3e8948", "#265c42", "#193c3e",
        "#124e89", "#0099db", "#2ce8f5", "#ffffff", "#c0cbdc", "#8b9bb4", "#5a6988", "#3a4466",
        "#262b44", "#181425", "#ff0044", "#68386c", "#b55088", "#f6757a", "#e8b796", "#c28569"
    });
}

ColorImage quantizePixelArt(const ColorImage& src,
                            uint32_t targetW,
                            uint32_t targetH,
                            const std::vector<RGBAf>& palette,
                            const std::string& ditherMode,
                            float ditherStrength,
                            bool upscaleToCanvas) {
    targetW = std::clamp(targetW, 4u, 2048u);
    targetH = std::clamp(targetH, 4u, 2048u);
    ditherStrength = std::clamp(ditherStrength, 0.0f, 1.0f);

    // 1. Box-average downsample to (targetW, targetH)
    ColorImage lowRes(targetW, targetH);
    for (uint32_t y = 0; y < targetH; ++y) {
        const float v0 = static_cast<float>(y) / static_cast<float>(targetH);
        const float v1 = static_cast<float>(y + 1) / static_cast<float>(targetH);
        for (uint32_t x = 0; x < targetW; ++x) {
            const float u0 = static_cast<float>(x) / static_cast<float>(targetW);
            const float u1 = static_cast<float>(x + 1) / static_cast<float>(targetW);
            const RGBAf c0 = src.sampleWrapped((u0 + u1) * 0.5f, (v0 + v1) * 0.5f);
            lowRes.pixels[static_cast<size_t>(y) * targetW + x] = c0;
        }
    }

    const std::vector<RGBAf> activePal = palette.empty() ? getBuiltinPalette("endesga32") : palette;

    auto findNearest = [&](float r, float g, float b) -> RGBAf {
        float bestDist = 1e9f;
        RGBAf bestCol = activePal[0];
        for (const auto& pc : activePal) {
            // Perceptual redmean-weighted RGB distance
            const float rmean = (r + pc.r) * 0.5f;
            const float dr = r - pc.r;
            const float dg = g - pc.g;
            const float db = b - pc.b;
            const float dist = (2.0f + rmean) * dr * dr + 4.0f * dg * dg + (3.0f - rmean) * db * db;
            if (dist < bestDist) {
                bestDist = dist;
                bestCol = pc;
            }
        }
        return bestCol;
    };

    static const float kBayer4x4[4][4] = {
        { -0.46875f,  0.03125f, -0.34375f,  0.15625f },
        {  0.28125f, -0.21875f,  0.40625f, -0.09375f },
        { -0.28125f,  0.21875f, -0.40625f,  0.09375f },
        {  0.46875f, -0.03125f,  0.34375f, -0.15625f }
    };

    static const float kBayer2x2[2][2] = {
        { -0.375f,  0.125f },
        {  0.375f, -0.125f }
    };

    if (ditherMode == "floyd_steinberg" || ditherMode == "atkinson") {
        const bool isAtkinson = (ditherMode == "atkinson");
        for (uint32_t y = 0; y < targetH; ++y) {
            for (uint32_t x = 0; x < targetW; ++x) {
                RGBAf& oldP = lowRes.pixels[static_cast<size_t>(y) * targetW + x];
                const RGBAf newP = findNearest(std::clamp(oldP.r, 0.0f, 1.0f),
                                               std::clamp(oldP.g, 0.0f, 1.0f),
                                               std::clamp(oldP.b, 0.0f, 1.0f));
                const float errR = (oldP.r - newP.r) * ditherStrength;
                const float errG = (oldP.g - newP.g) * ditherStrength;
                const float errB = (oldP.b - newP.b) * ditherStrength;
                oldP = newP;

                auto addErr = [&](int nx, int ny, float w) {
                    if (nx >= 0 && nx < static_cast<int>(targetW) && ny >= 0 && ny < static_cast<int>(targetH)) {
                        RGBAf& p = lowRes.pixels[static_cast<size_t>(ny) * targetW + static_cast<size_t>(nx)];
                        p.r += errR * w;
                        p.g += errG * w;
                        p.b += errB * w;
                    }
                };

                if (isAtkinson) {
                    addErr(static_cast<int>(x) + 1, static_cast<int>(y), 1.0f / 8.0f);
                    addErr(static_cast<int>(x) + 2, static_cast<int>(y), 1.0f / 8.0f);
                    addErr(static_cast<int>(x) - 1, static_cast<int>(y) + 1, 1.0f / 8.0f);
                    addErr(static_cast<int>(x),     static_cast<int>(y) + 1, 1.0f / 8.0f);
                    addErr(static_cast<int>(x) + 1, static_cast<int>(y) + 1, 1.0f / 8.0f);
                    addErr(static_cast<int>(x),     static_cast<int>(y) + 2, 1.0f / 8.0f);
                } else {
                    addErr(static_cast<int>(x) + 1, static_cast<int>(y),     7.0f / 16.0f);
                    addErr(static_cast<int>(x) - 1, static_cast<int>(y) + 1, 3.0f / 16.0f);
                    addErr(static_cast<int>(x),     static_cast<int>(y) + 1, 5.0f / 16.0f);
                    addErr(static_cast<int>(x) + 1, static_cast<int>(y) + 1, 1.0f / 16.0f);
                }
            }
        }
    } else {
        const bool isBayer2 = (ditherMode == "bayer2x2");
        const bool noDither = (ditherMode == "none");
        const float spread = noDither ? 0.0f : (ditherStrength * 0.22f);

        for (uint32_t y = 0; y < targetH; ++y) {
            for (uint32_t x = 0; x < targetW; ++x) {
                RGBAf& p = lowRes.pixels[static_cast<size_t>(y) * targetW + x];
                const float bayerOffset = noDither ? 0.0f
                                                   : (isBayer2 ? kBayer2x2[y & 1u][x & 1u]
                                                               : kBayer4x4[y & 3u][x & 3u]) * spread;
                p = findNearest(std::clamp(p.r + bayerOffset, 0.0f, 1.0f),
                                std::clamp(p.g + bayerOffset, 0.0f, 1.0f),
                                std::clamp(p.b + bayerOffset, 0.0f, 1.0f));
            }
        }
    }

    if (!upscaleToCanvas || (targetW == src.width && targetH == src.height)) {
        return lowRes;
    }

    // Crisp nearest-neighbor upscale back to canvas resolution
    ColorImage upscaled(src.width, src.height);
    for (uint32_t y = 0; y < src.height; ++y) {
        const uint32_t sy = std::min(targetH - 1u, (y * targetH) / src.height);
        for (uint32_t x = 0; x < src.width; ++x) {
            const uint32_t sx = std::min(targetW - 1u, (x * targetW) / src.width);
            upscaled.pixels[static_cast<size_t>(y) * src.width + x] =
                lowRes.pixels[static_cast<size_t>(sy) * targetW + sx];
        }
    }
    return upscaled;
}

} // namespace eatstextures
