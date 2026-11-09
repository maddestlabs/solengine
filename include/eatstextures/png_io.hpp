#ifndef EATSTEXTURES_PNG_IO_HPP
#define EATSTEXTURES_PNG_IO_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace eatstextures {

struct ComparisonMetrics {
    double ssim{0.0};               // Structural Similarity Index [0.0, 1.0]
    double histogramSimilarity{0.0}; // 64-bin RGB Histogram Intersection [0.0, 1.0]
    double edgeSimilarity{0.0};      // Sobel gradient magnitude energy & orientation similarity [0.0, 1.0]
    double psnr{0.0};               // Peak Signal-to-Noise Ratio (dB)
    double mae{0.0};                // Mean Absolute Error [0.0, 1.0]
    double compositeScore{0.0};     // Weighted perceptual score [0.0, 100.0]
};

/**
 * Reads any image format supported by stb_image (PNG, JPG, BMP, TGA) into 8-bit RGBA pixels.
 */
bool readImageRGBA(const std::string& filePath,
                   std::vector<uint8_t>& outRgba,
                   uint32_t& outWidth,
                   uint32_t& outHeight);

/**
 * Resizes an 8-bit RGBA image using bilinear or box sampling.
 */
std::vector<uint8_t> resizeImageRGBA(const std::vector<uint8_t>& srcRgba,
                                     uint32_t srcW,
                                     uint32_t srcH,
                                     uint32_t dstW,
                                     uint32_t dstH);

/**
 * Writes an 8-bit RGBA buffer to a valid PNG file.
 */
bool writePngRGBA(const std::string& filePath,
                  uint32_t width,
                  uint32_t height,
                  const uint8_t* rgbaPixels);

/**
 * Computes perceptual & structural similarity metrics between a target and rendered RGBA buffer
 * of identical dimensions (width x height).
 */
ComparisonMetrics computeComparisonMetrics(const uint8_t* targetRgba,
                                           const uint8_t* renderedRgba,
                                           uint32_t width,
                                           uint32_t height);

/**
 * Builds a 3-panel horizontal contact sheet:
 * [ Panel 1: Target Reference | Panel 2: Eatscript Render | Panel 3: 3x Error Heatmap ]
 */
void buildComparisonContactSheet(const uint8_t* targetRgba,
                                 const uint8_t* renderedRgba,
                                 uint32_t width,
                                 uint32_t height,
                                 std::vector<uint8_t>& outSheetRgba,
                                 uint32_t& outSheetW,
                                 uint32_t& outSheetH);

} // namespace eatstextures

#endif // EATSTEXTURES_PNG_IO_HPP
