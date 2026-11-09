#include "eatstextures/png_io.hpp"
#include "stb_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace eatstextures {

bool readImageRGBA(const std::string& filePath,
                   std::vector<uint8_t>& outRgba,
                   uint32_t& outWidth,
                   uint32_t& outHeight) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(filePath.c_str(), &w, &h, &channels, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return false;
    }
    outWidth = static_cast<uint32_t>(w);
    outHeight = static_cast<uint32_t>(h);
    const size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    outRgba.assign(data, data + totalBytes);
    stbi_image_free(data);
    return true;
}

std::vector<uint8_t> resizeImageRGBA(const std::vector<uint8_t>& srcRgba,
                                     uint32_t srcW,
                                     uint32_t srcH,
                                     uint32_t dstW,
                                     uint32_t dstH) {
    if (srcW == dstW && srcH == dstH) {
        return srcRgba;
    }
    std::vector<uint8_t> dst(static_cast<size_t>(dstW) * dstH * 4u, 255u);
    if (srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) {
        return dst;
    }

    for (uint32_t y = 0; y < dstH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dstH) * static_cast<float>(srcH) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, static_cast<int>(srcH) - 1);
        const int y1 = std::clamp(y0 + 1, 0, static_cast<int>(srcH) - 1);
        const float fy = v - std::floor(v);

        for (uint32_t x = 0; x < dstW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dstW) * static_cast<float>(srcW) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, static_cast<int>(srcW) - 1);
            const int x1 = std::clamp(x0 + 1, 0, static_cast<int>(srcW) - 1);
            const float fx = u - std::floor(u);

            const size_t idx00 = (static_cast<size_t>(y0) * srcW + x0) * 4u;
            const size_t idx10 = (static_cast<size_t>(y0) * srcW + x1) * 4u;
            const size_t idx01 = (static_cast<size_t>(y1) * srcW + x0) * 4u;
            const size_t idx11 = (static_cast<size_t>(y1) * srcW + x1) * 4u;

            const size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * 4u;
            for (size_t c = 0; c < 4u; ++c) {
                const float c00 = static_cast<float>(srcRgba[idx00 + c]);
                const float c10 = static_cast<float>(srcRgba[idx10 + c]);
                const float c01 = static_cast<float>(srcRgba[idx01 + c]);
                const float c11 = static_cast<float>(srcRgba[idx11 + c]);
                const float top = c00 + (c10 - c00) * fx;
                const float bot = c01 + (c11 - c01) * fx;
                const float val = top + (bot - top) * fy;
                dst[dstIdx + c] = static_cast<uint8_t>(std::clamp(std::lround(val), 0L, 255L));
            }
        }
    }
    return dst;
}

bool writePngRGBA(const std::string& filePath,
                  uint32_t width,
                  uint32_t height,
                  const uint8_t* rgbaPixels) {
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
        const size_t typeOffset = png.size();
        png.push_back(static_cast<uint8_t>(type[0]));
        png.push_back(static_cast<uint8_t>(type[1]));
        png.push_back(static_cast<uint8_t>(type[2]));
        png.push_back(static_cast<uint8_t>(type[3]));
        png.insert(png.end(), data.begin(), data.end());
        const uint32_t crc = crc32(png.data() + typeOffset, 4u + data.size());
        appendU32BE(png, crc);
    };

    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    appendU32BE(ihdr, width);
    appendU32BE(ihdr, height);
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // RGBA color type
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendChunk(png, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    const size_t rowBytes = static_cast<size_t>(width) * 4u + 1u;
    raw.reserve(rowBytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // filter type 0
        const uint8_t* row = rgbaPixels + static_cast<size_t>(y) * width * 4u;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 4u);
    }

    std::vector<uint8_t> zdata;
    zdata.push_back(0x78);
    zdata.push_back(0x01);
    size_t pos = 0;
    uint32_t s1 = 1, s2 = 0;
    while (pos < raw.size()) {
        const size_t blockLen = std::min<size_t>(65535u, raw.size() - pos);
        const bool finalBlock = (pos + blockLen == raw.size());
        zdata.push_back(finalBlock ? 0x01 : 0x00);
        zdata.push_back(static_cast<uint8_t>(blockLen & 0xFF));
        zdata.push_back(static_cast<uint8_t>((blockLen >> 8) & 0xFF));
        const uint16_t nlen = static_cast<uint16_t>(~blockLen);
        zdata.push_back(static_cast<uint8_t>(nlen & 0xFF));
        zdata.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        for (size_t i = 0; i < blockLen; ++i) {
            const uint8_t b = raw[pos + i];
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

ComparisonMetrics computeComparisonMetrics(const uint8_t* targetRgba,
                                           const uint8_t* renderedRgba,
                                           uint32_t width,
                                           uint32_t height) {
    ComparisonMetrics m{};
    if (!targetRgba || !renderedRgba || width == 0 || height == 0) {
        return m;
    }

    const size_t numPixels = static_cast<size_t>(width) * height;
    double sumAbsErr = 0.0;
    double sumSqErr = 0.0;

    // 1. 4x4x4 = 64-bin 3D RGB Color Histogram + Per-channel luminance
    std::array<double, 64> histA{};
    std::array<double, 64> histB{};
    std::vector<float> lumA(numPixels, 0.0f);
    std::vector<float> lumB(numPixels, 0.0f);

    for (size_t i = 0; i < numPixels; ++i) {
        const float rA = targetRgba[i * 4 + 0] / 255.0f;
        const float gA = targetRgba[i * 4 + 1] / 255.0f;
        const float bA = targetRgba[i * 4 + 2] / 255.0f;

        const float rB = renderedRgba[i * 4 + 0] / 255.0f;
        const float gB = renderedRgba[i * 4 + 1] / 255.0f;
        const float bB = renderedRgba[i * 4 + 2] / 255.0f;

        const double dr = std::abs(rA - rB);
        const double dg = std::abs(gA - gB);
        const double db = std::abs(bA - bB);
        sumAbsErr += (dr + dg + db) / 3.0;
        sumSqErr += (dr * dr + dg * dg + db * db) / 3.0;

        lumA[i] = 0.2126f * rA + 0.7152f * gA + 0.0722f * bA;
        lumB[i] = 0.2126f * rB + 0.7152f * gB + 0.0722f * bB;

        const int binA = (std::min(3, static_cast<int>(rA * 4.0f)) << 4) |
                         (std::min(3, static_cast<int>(gA * 4.0f)) << 2) |
                         std::min(3, static_cast<int>(bA * 4.0f));
        const int binB = (std::min(3, static_cast<int>(rB * 4.0f)) << 4) |
                         (std::min(3, static_cast<int>(gB * 4.0f)) << 2) |
                         std::min(3, static_cast<int>(bB * 4.0f));
        histA[static_cast<size_t>(binA)] += 1.0;
        histB[static_cast<size_t>(binB)] += 1.0;
    }

    m.mae = sumAbsErr / static_cast<double>(numPixels);
    const double mse = sumSqErr / static_cast<double>(numPixels);
    m.psnr = (mse <= 1e-10) ? 99.9 : (10.0 * std::log10(1.0 / mse));

    double histOverlap = 0.0;
    for (size_t b = 0; b < 64u; ++b) {
        histOverlap += std::min(histA[b], histB[b]);
    }
    m.histogramSimilarity = std::clamp(histOverlap / static_cast<double>(numPixels), 0.0, 1.0);

    // 2. Windowed SSIM (8x8 blocks)
    const uint32_t win = std::min<uint32_t>(8u, std::min(width, height));
    const double c1 = 0.01 * 0.01;
    const double c2 = 0.03 * 0.03;
    double ssimSum = 0.0;
    size_t ssimCount = 0;

    for (uint32_t by = 0; by + win <= height; by += win) {
        for (uint32_t bx = 0; bx + win <= width; bx += win) {
            double meanA = 0.0, meanB = 0.0;
            const double invN = 1.0 / static_cast<double>(win * win);
            for (uint32_t dy = 0; dy < win; ++dy) {
                for (uint32_t dx = 0; dx < win; ++dx) {
                    const size_t idx = static_cast<size_t>(by + dy) * width + (bx + dx);
                    meanA += lumA[idx];
                    meanB += lumB[idx];
                }
            }
            meanA *= invN;
            meanB *= invN;

            double varA = 0.0, varB = 0.0, covAB = 0.0;
            for (uint32_t dy = 0; dy < win; ++dy) {
                for (uint32_t dx = 0; dx < win; ++dx) {
                    const size_t idx = static_cast<size_t>(by + dy) * width + (bx + dx);
                    const double da = lumA[idx] - meanA;
                    const double db = lumB[idx] - meanB;
                    varA += da * da;
                    varB += db * db;
                    covAB += da * db;
                }
            }
            varA *= invN;
            varB *= invN;
            covAB *= invN;

            const double num = (2.0 * meanA * meanB + c1) * (2.0 * covAB + c2);
            const double den = (meanA * meanA + meanB * meanB + c1) * (varA + varB + c2);
            ssimSum += (den > 1e-12) ? (num / den) : 1.0;
            ++ssimCount;
        }
    }
    m.ssim = (ssimCount > 0) ? std::clamp(ssimSum / static_cast<double>(ssimCount), 0.0, 1.0) : (1.0 - m.mae);

    // 3. Sobel Edge / Frequency Energy Similarity
    double edgeEnergyA = 0.0;
    double edgeEnergyB = 0.0;
    if (width >= 3 && height >= 3) {
        for (uint32_t y = 1; y + 1 < height; ++y) {
            for (uint32_t x = 1; x + 1 < width; ++x) {
                auto sampleL = [&](const std::vector<float>& buf, uint32_t sx, uint32_t sy) {
                    return buf[static_cast<size_t>(sy) * width + sx];
                };
                const float gxA = -sampleL(lumA, x - 1, y - 1) + sampleL(lumA, x + 1, y - 1)
                                  - 2.0f * sampleL(lumA, x - 1, y) + 2.0f * sampleL(lumA, x + 1, y)
                                  - sampleL(lumA, x - 1, y + 1) + sampleL(lumA, x + 1, y + 1);
                const float gyA = -sampleL(lumA, x - 1, y - 1) - 2.0f * sampleL(lumA, x, y - 1) - sampleL(lumA, x + 1, y - 1)
                                  + sampleL(lumA, x - 1, y + 1) + 2.0f * sampleL(lumA, x, y + 1) + sampleL(lumA, x + 1, y + 1);

                const float gxB = -sampleL(lumB, x - 1, y - 1) + sampleL(lumB, x + 1, y - 1)
                                  - 2.0f * sampleL(lumB, x - 1, y) + 2.0f * sampleL(lumB, x + 1, y)
                                  - sampleL(lumB, x - 1, y + 1) + sampleL(lumB, x + 1, y + 1);
                const float gyB = -sampleL(lumB, x - 1, y - 1) - 2.0f * sampleL(lumB, x, y - 1) - sampleL(lumB, x + 1, y - 1)
                                  + sampleL(lumB, x - 1, y + 1) + 2.0f * sampleL(lumB, x, y + 1) + sampleL(lumB, x + 1, y + 1);

                edgeEnergyA += std::hypot(gxA, gyA);
                edgeEnergyB += std::hypot(gxB, gyB);
            }
        }
        const double maxE = std::max({edgeEnergyA, edgeEnergyB, 1e-6});
        const double minE = std::min(edgeEnergyA, edgeEnergyB);
        m.edgeSimilarity = std::clamp(minE / maxE, 0.0, 1.0);
    } else {
        m.edgeSimilarity = m.ssim;
    }

    // 4. Composite Perceptual Score [0..100]
    // Weights: 40% Color Distribution, 35% Structural Similarity, 25% Frequency/Edge Energy
    m.compositeScore = std::clamp(
        (0.40 * m.histogramSimilarity + 0.35 * m.ssim + 0.25 * m.edgeSimilarity) * 100.0,
        0.0, 100.0
    );
    return m;
}

void buildComparisonContactSheet(const uint8_t* targetRgba,
                                 const uint8_t* renderedRgba,
                                 uint32_t width,
                                 uint32_t height,
                                 std::vector<uint8_t>& outSheetRgba,
                                 uint32_t& outSheetW,
                                 uint32_t& outSheetH) {
    const uint32_t gap = 6u;
    outSheetW = width * 3u + gap * 2u;
    outSheetH = height;
    outSheetRgba.assign(static_cast<size_t>(outSheetW) * outSheetH * 4u, 24u);
    for (size_t i = 3; i < outSheetRgba.size(); i += 4u) {
        outSheetRgba[i] = 255u;
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t srcIdx = (static_cast<size_t>(y) * width + x) * 4u;

            // Panel 1: Target
            const size_t dst1 = (static_cast<size_t>(y) * outSheetW + x) * 4u;
            for (size_t c = 0; c < 4u; ++c) outSheetRgba[dst1 + c] = targetRgba[srcIdx + c];

            // Panel 2: Rendered
            const size_t dst2 = (static_cast<size_t>(y) * outSheetW + (width + gap + x)) * 4u;
            for (size_t c = 0; c < 4u; ++c) outSheetRgba[dst2 + c] = renderedRgba[srcIdx + c];

            // Panel 3: Thermal Error Heatmap (false color blue -> cyan -> yellow -> red)
            const float dr = std::abs(static_cast<float>(targetRgba[srcIdx + 0]) - static_cast<float>(renderedRgba[srcIdx + 0])) / 255.0f;
            const float dg = std::abs(static_cast<float>(targetRgba[srcIdx + 1]) - static_cast<float>(renderedRgba[srcIdx + 1])) / 255.0f;
            const float db = std::abs(static_cast<float>(targetRgba[srcIdx + 2]) - static_cast<float>(renderedRgba[srcIdx + 2])) / 255.0f;
            const float err = std::clamp((dr + dg + db) * 0.85f, 0.0f, 1.0f);

            const float hr = std::clamp(err * 2.5f - 0.4f, 0.0f, 1.0f);
            const float hg = std::clamp(1.0f - std::abs(err - 0.5f) * 2.2f, 0.0f, 1.0f) * (err > 0.05f ? 1.0f : err * 20.0f);
            const float hb = std::clamp(1.0f - err * 1.8f, 0.08f, 1.0f);

            const size_t dst3 = (static_cast<size_t>(y) * outSheetW + (width * 2u + gap * 2u + x)) * 4u;
            outSheetRgba[dst3 + 0] = static_cast<uint8_t>(hr * 255.0f);
            outSheetRgba[dst3 + 1] = static_cast<uint8_t>(hg * 255.0f);
            outSheetRgba[dst3 + 2] = static_cast<uint8_t>(hb * 255.0f);
            outSheetRgba[dst3 + 3] = 255u;
        }
    }
}

} // namespace eatstextures
