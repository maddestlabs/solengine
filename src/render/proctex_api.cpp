#include "solengine/proctex_api.hpp"
#include "eatstextures/png_io.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace solengine {

namespace {

bool readFileToString(const std::string& path, std::string& outStr) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    outStr = ss.str();
    return true;
}

} // namespace

bool isProceduralScript(const std::string& scriptPathOrSource) {
    if (scriptPathOrSource.empty()) return false;

    // 1. Check file extension if it's a path
    if (scriptPathOrSource.ends_with(".eat") || scriptPathOrSource.ends_with(".EAT")) {
        return true;
    }

    // 2. Check for signature proctex / background Eatscript keywords
    if (scriptPathOrSource.find("proctex.canvas") != std::string::npos ||
        scriptPathOrSource.find("background.create") != std::string::npos ||
        scriptPathOrSource.find("tex.output_pbr") != std::string::npos ||
        scriptPathOrSource.find("tex.output(") != std::string::npos ||
        scriptPathOrSource.find("tex.shade_pbr") != std::string::npos ||
        scriptPathOrSource.find("on_draw_background") != std::string::npos ||
        scriptPathOrSource.find("on_draw_table") != std::string::npos ||
        scriptPathOrSource.find("background_shader") != std::string::npos) {
        return true;
    }

    // 3. If it looks like a path, try reading the file header
    std::error_code ec;
    if (std::filesystem::exists(scriptPathOrSource, ec)) {
        std::string content;
        if (readFileToString(scriptPathOrSource, content)) {
            return isProceduralScript(content);
        }
    }

    return false;
}

std::string resolveProceduralScriptPath(const std::string& nameOrPath) {
    if (nameOrPath.empty()) return "";

    std::string base = nameOrPath;
    std::string withEat = nameOrPath.ends_with(".eat") ? nameOrPath : (nameOrPath + ".eat");
    std::string withEats = nameOrPath.ends_with(".eats") ? nameOrPath : (nameOrPath + ".eats");

    // Standard local candidate search roots
    const std::vector<std::string> searchBases = {
        "",
        "/themes/",
        "themes/",
        "../themes/",
        "themes/procedural/",
        "../themes/procedural/",
        "assets/themes/",
        "../assets/themes/",
        "c:/git/eatstextures/library/",
        "../eatstextures/library/",
        "../../eatstextures/library/"
    };

    const std::vector<std::string> categories = {
        "",
        "wood/",
        "fabric/",
        "carpet/",
        "leather/",
        "nature/",
        "stone/",
        "metal/",
        "retro/"
    };

    std::error_code ec;

    // Check direct path first
    for (const auto& sb : searchBases) {
        std::string cand1 = sb + withEat;
        if (std::filesystem::exists(cand1, ec) && !std::filesystem::is_directory(cand1, ec)) {
            return cand1;
        }
        std::string candEats = sb + withEats;
        if (std::filesystem::exists(candEats, ec) && !std::filesystem::is_directory(candEats, ec)) {
            return candEats;
        }
        std::string cand2 = sb + base;
        if (std::filesystem::exists(cand2, ec) && !std::filesystem::is_directory(cand2, ec)) {
            return cand2;
        }

        // Search category subfolders in library
        for (const auto& cat : categories) {
            if (cat.empty()) continue;
            std::string catCand1 = sb + cat + withEat;
            if (std::filesystem::exists(catCand1, ec) && !std::filesystem::is_directory(catCand1, ec)) {
                return catCand1;
            }
            std::string catCandEats = sb + cat + withEats;
            if (std::filesystem::exists(catCandEats, ec) && !std::filesystem::is_directory(catCandEats, ec)) {
                return catCandEats;
            }
            std::string catCand2 = sb + cat + base;
            if (std::filesystem::exists(catCand2, ec) && !std::filesystem::is_directory(catCand2, ec)) {
                return catCand2;
            }
        }
    }

    return "";
}

bool bakeProceduralTexture(
    const std::string& scriptPathOrSource,
    std::vector<uint8_t>& outRgba,
    uint32_t& outW,
    uint32_t& outH,
    const ProceduralBakeOptions& options
) {
    outRgba.clear();
    outW = 0;
    outH = 0;

    eatstextures::ProcTexRuntime runtime(options.width, options.height, options.seed);
    if (options.overrideDimensions) {
        runtime.setOverrideDimensions(options.width, options.height);
    }
    if (options.overrideSeed) {
        runtime.setOverrideSeed(options.seed, true);
    }

    std::error_code ec;
    bool success = false;
    if (std::filesystem::exists(scriptPathOrSource, ec)) {
        success = runtime.executeScriptFile(scriptPathOrSource);
    } else {
        std::string resolved = resolveProceduralScriptPath(scriptPathOrSource);
        if (!resolved.empty() && std::filesystem::exists(resolved, ec)) {
            success = runtime.executeScriptFile(resolved);
        } else {
            success = runtime.executeScriptSource(scriptPathOrSource);
        }
    }

    if (!success) {
        std::cerr << "[SolEngine ProcTex] Error baking texture: " << runtime.lastError() << "\n";
        return false;
    }

    const auto& img = runtime.outputImage();
    if (img.width == 0 || img.height == 0) {
        // If outputImage wasn't explicitly populated but pbrBundle is valid, use albedo
        if (runtime.pbrBundle().valid && runtime.pbrBundle().albedo.width > 0) {
            outW = runtime.pbrBundle().albedo.width;
            outH = runtime.pbrBundle().albedo.height;
            outRgba = runtime.pbrBundle().albedo.toRGBA8();
            return true;
        }
        std::cerr << "[SolEngine ProcTex] Script executed but yielded empty output image.\n";
        return false;
    }

    outW = img.width;
    outH = img.height;
    outRgba = img.toRGBA8();
    return true;
}

bool bakeProceduralPbrBundle(
    const std::string& scriptPathOrSource,
    ProceduralPbrMaps& outMaps,
    const ProceduralBakeOptions& options
) {
    outMaps = ProceduralPbrMaps{};

    eatstextures::ProcTexRuntime runtime(options.width, options.height, options.seed);
    if (options.overrideDimensions) {
        runtime.setOverrideDimensions(options.width, options.height);
    }
    if (options.overrideSeed) {
        runtime.setOverrideSeed(options.seed, true);
    }

    std::error_code ec;
    bool success = false;
    if (std::filesystem::exists(scriptPathOrSource, ec)) {
        success = runtime.executeScriptFile(scriptPathOrSource);
    } else {
        std::string resolved = resolveProceduralScriptPath(scriptPathOrSource);
        if (!resolved.empty() && std::filesystem::exists(resolved, ec)) {
            success = runtime.executeScriptFile(resolved);
        } else {
            success = runtime.executeScriptSource(scriptPathOrSource);
        }
    }

    if (!success) {
        outMaps.errorMessage = runtime.lastError();
        std::cerr << "[SolEngine ProcTex] Error baking PBR bundle: " << runtime.lastError() << "\n";
        return false;
    }

    const auto& pbr = runtime.pbrBundle();
    if (pbr.valid && pbr.albedo.width > 0 && pbr.albedo.height > 0) {
        outMaps.width = static_cast<int>(pbr.albedo.width);
        outMaps.height = static_cast<int>(pbr.albedo.height);
        outMaps.albedo = pbr.albedo.toRGBA8();
        outMaps.normal = pbr.normal.toRGBA8();
        outMaps.roughness = pbr.roughness.toRGBA8();
        outMaps.heightMap = pbr.height.toRGBA8();
        outMaps.ao = pbr.ao.toRGBA8();

        const size_t numPixels = static_cast<size_t>(outMaps.width) * outMaps.height;
        outMaps.colorRgb.resize(numPixels * 3);
        outMaps.normalRgb.resize(numPixels * 3);
        outMaps.orm.resize(numPixels * 3);

        for (size_t i = 0; i < numPixels; ++i) {
            outMaps.colorRgb[i * 3 + 0] = outMaps.albedo[i * 4 + 0];
            outMaps.colorRgb[i * 3 + 1] = outMaps.albedo[i * 4 + 1];
            outMaps.colorRgb[i * 3 + 2] = outMaps.albedo[i * 4 + 2];

            outMaps.normalRgb[i * 3 + 0] = outMaps.normal[i * 4 + 0];
            outMaps.normalRgb[i * 3 + 1] = outMaps.normal[i * 4 + 1];
            outMaps.normalRgb[i * 3 + 2] = outMaps.normal[i * 4 + 2];

            // ORM: Red=AO, Green=Roughness, Blue=Metallic/Displacement
            outMaps.orm[i * 3 + 0] = outMaps.ao[i * 4 + 0];
            outMaps.orm[i * 3 + 1] = outMaps.roughness[i * 4 + 0];
            outMaps.orm[i * 3 + 2] = outMaps.heightMap[i * 4 + 0];
        }

        outMaps.valid = true;
        return true;
    }

    // Fallback: If script didn't emit a full PBR bundle but emitted a final composite image
    const auto& outImg = runtime.outputImage();
    if (outImg.width > 0 && outImg.height > 0) {
        outMaps.width = static_cast<int>(outImg.width);
        outMaps.height = static_cast<int>(outImg.height);
        outMaps.albedo = outImg.toRGBA8();

        const size_t numPixels = static_cast<size_t>(outMaps.width) * outMaps.height;
        outMaps.colorRgb.resize(numPixels * 3);
        outMaps.normalRgb.resize(numPixels * 3);
        outMaps.orm.resize(numPixels * 3);

        for (size_t i = 0; i < numPixels; ++i) {
            outMaps.colorRgb[i * 3 + 0] = outMaps.albedo[i * 4 + 0];
            outMaps.colorRgb[i * 3 + 1] = outMaps.albedo[i * 4 + 1];
            outMaps.colorRgb[i * 3 + 2] = outMaps.albedo[i * 4 + 2];

            // Flat tangent normal (128, 128, 255)
            outMaps.normalRgb[i * 3 + 0] = 128;
            outMaps.normalRgb[i * 3 + 1] = 128;
            outMaps.normalRgb[i * 3 + 2] = 255;

            // Neutral ORM (AO=255, Roughness=140, Metallic=0)
            outMaps.orm[i * 3 + 0] = 255;
            outMaps.orm[i * 3 + 1] = 140;
            outMaps.orm[i * 3 + 2] = 0;
        }

        outMaps.valid = true;
        return true;
    }

    outMaps.errorMessage = "Procedural script executed but produced no image or PBR output.";
    return false;
}

std::future<ProceduralPbrMaps> bakeProceduralPbrAsync(
    std::string scriptPathOrSource,
    ProceduralBakeOptions options
) {
    return std::async(
        std::launch::async,
        [script = std::move(scriptPathOrSource), opts = options]() {
            ProceduralPbrMaps maps;
            bakeProceduralPbrBundle(script, maps, opts);
            return maps;
        }
    );
}

bool exportProceduralPbrToDisk(
    const std::string& scriptPathOrSource,
    const std::string& outputPrefix,
    const ProceduralBakeOptions& options
) {
    ProceduralPbrMaps maps;
    if (!bakeProceduralPbrBundle(scriptPathOrSource, maps, options)) {
        return false;
    }

    uint32_t w = static_cast<uint32_t>(maps.width);
    uint32_t h = static_cast<uint32_t>(maps.height);

    bool ok = true;
    if (!maps.albedo.empty()) {
        ok &= eatstextures::writePngRGBA(outputPrefix + "_albedo.png", w, h, maps.albedo.data());
    }
    if (!maps.normal.empty()) {
        ok &= eatstextures::writePngRGBA(outputPrefix + "_normal.png", w, h, maps.normal.data());
    }
    if (!maps.roughness.empty()) {
        ok &= eatstextures::writePngRGBA(outputPrefix + "_roughness.png", w, h, maps.roughness.data());
    }
    if (!maps.heightMap.empty()) {
        ok &= eatstextures::writePngRGBA(outputPrefix + "_height.png", w, h, maps.heightMap.data());
    }
    if (!maps.ao.empty()) {
        ok &= eatstextures::writePngRGBA(outputPrefix + "_ao.png", w, h, maps.ao.data());
    }

    return ok;
}

uint32_t createProceduralTexture(
    eatsbits::ui::BatchRenderer2D& renderer,
    const std::string& scriptPathOrSource,
    const ProceduralBakeOptions& options
) {
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!bakeProceduralTexture(scriptPathOrSource, rgba, w, h, options)) {
        return 0;
    }

    return renderer.createTexture(static_cast<int>(w), static_cast<int>(h), rgba.data());
}

} // namespace solengine

