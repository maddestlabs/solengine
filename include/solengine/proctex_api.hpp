#ifndef SOLENGINE_PROCTEX_API_HPP
#define SOLENGINE_PROCTEX_API_HPP

#include "eatstextures/proctex_runtime.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <future>
#include <functional>
#include <memory>

namespace eatsbits::ui {
class BatchRenderer2D;
}

namespace solengine {

/**
 * Options for procedural texture synthesis and baking.
 */
struct ProceduralBakeOptions {
    uint32_t width{512};
    uint32_t height{512};
    uint32_t seed{42};
    bool overrideDimensions{false};
    bool overrideSeed{false};
    bool seamless{true};
    std::string optionId;
    float u[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * PBR Texture Maps produced by procedural synthesis.
 */
struct ProceduralPbrMaps {
    int width{0};
    int height{0};

    // Standard 32-bit RGBA maps (4 bytes per pixel)
    std::vector<uint8_t> albedo;
    std::vector<uint8_t> normal;
    std::vector<uint8_t> roughness;
    std::vector<uint8_t> heightMap;
    std::vector<uint8_t> ao;

    // SolEngine Internal Packed Buffers (3 bytes per pixel)
    std::vector<uint8_t> colorRgb;   // RGB albedo
    std::vector<uint8_t> normalRgb;  // RGB tangent normal
    std::vector<uint8_t> orm;        // R=AO, G=Roughness, B=Metallic/Displacement

    bool valid{false};
    std::string errorMessage;
};

/**
 * Check if the given file path or string content represents an Eatscript procedural script.
 */
[[nodiscard]] bool isProceduralScript(const std::string& scriptPathOrSource);

/**
 * Resolve the path to an .eat procedural texture script, searching across:
 * 1. Direct path / working directory
 * 2. themes/procedural/
 * 3. themes/
 * 4. assets/themes/
 * 5. c:/git/eatstextures/library/
 * 6. ../eatstextures/library/
 */
[[nodiscard]] std::string resolveProceduralScriptPath(const std::string& nameOrPath);

/**
 * Synchronously bake a procedural texture into a 32-bit RGBA byte array.
 * Pure CPU & host-memory execution; safe for background threads.
 */
bool bakeProceduralTexture(
    const std::string& scriptPathOrSource,
    std::vector<uint8_t>& outRgba,
    uint32_t& outW,
    uint32_t& outH,
    const ProceduralBakeOptions& options = {}
);

/**
 * Synchronously bake a full procedural PBR bundle (Albedo, Normal, Roughness, Height, AO, ORM).
 * Pure CPU & host-memory execution.
 */
bool bakeProceduralPbrBundle(
    const std::string& scriptPathOrSource,
    ProceduralPbrMaps& outMaps,
    const ProceduralBakeOptions& options = {}
);

/**
 * Asynchronously synthesize a full PBR bundle on a background thread.
 * Guarantees zero GPU/audio pipeline stalls.
 */
[[nodiscard]] std::future<ProceduralPbrMaps> bakeProceduralPbrAsync(
    std::string scriptPathOrSource,
    ProceduralBakeOptions options = {}
);

/**
 * Bake procedural maps and export them to disk as PNG files:
 * - <outputPrefix>_albedo.png
 * - <outputPrefix>_normal.png
 * - <outputPrefix>_roughness.png
 * - <outputPrefix>_height.png
 * - <outputPrefix>_ao.png
 */
bool exportProceduralPbrToDisk(
    const std::string& scriptPathOrSource,
    const std::string& outputPrefix,
    const ProceduralBakeOptions& options = {}
);

/**
 * Synthesize a procedural texture and register it directly into BatchRenderer2D.
 * Returns the GPU texture handle (>0 on success, 0 on failure).
 */
uint32_t createProceduralTexture(
    eatsbits::ui::BatchRenderer2D& renderer,
    const std::string& scriptPathOrSource,
    const ProceduralBakeOptions& options = {}
);

} // namespace solengine

#endif // SOLENGINE_PROCTEX_API_HPP

