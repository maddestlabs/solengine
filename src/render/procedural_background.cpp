#include "solengine/procedural_background.hpp"
#include "solengine/card_canvas.hpp"
#include "solengine/solitaire_board.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <mutex>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace solengine {

namespace {

using eatsbits::ui::Point2D;

// Fast deterministic integer hash for procedural variations
[[maybe_unused]] inline float hash2D(int x, int y, uint32_t seed) noexcept {
    uint32_t n = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 668265263u +
                 seed * 1442695040888963407u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return static_cast<float>(n & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF);
}

void renderVectorFallback(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    TableThemeId theme,
    [[maybe_unused]] const eatsbits::ui::LightSource2D& light,
    [[maybe_unused]] uint32_t seed
) {
    switch (theme) {
        case TableThemeId::PastelTan:
            r.drawRect(x, y, w, h, 0.83f, 0.77f, 0.69f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.15f, 0.68f, 0.61f, 0.52f);
            break;
        case TableThemeId::EmeraldLawn:
            r.drawRect(x, y, w, h, 0.08f, 0.28f, 0.14f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.02f, 0.12f, 0.05f);
            break;
        case TableThemeId::HopsackTweed:
            r.drawRect(x, y, w, h, 0.32f, 0.32f, 0.30f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.16f, 0.16f, 0.15f);
            break;
        case TableThemeId::DenimChambray:
            r.drawRect(x, y, w, h, 0.12f, 0.20f, 0.32f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.05f, 0.08f, 0.16f);
            break;
        case TableThemeId::StripedJersey:
            r.drawRect(x, y, w, h, 0.18f, 0.19f, 0.21f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.08f, 0.09f, 0.10f);
            break;
        case TableThemeId::SunkenSanctum:
            r.drawRect(x, y, w, h, 0.06f, 0.10f, 0.15f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.40f, 0.02f, 0.04f, 0.08f);
            break;
        case TableThemeId::PerilousShores:
            r.drawRect(x, y, w, h, 0.16f, 0.22f, 0.12f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.08f, 0.12f, 0.06f);
            break;
        case TableThemeId::PebbledCowhide:
            r.drawRect(x, y, w, h, 0.20f, 0.13f, 0.08f, 1.0f);
            r.drawDustSpecks(x, y, w, h, 60, seed, 0.12f, 0.07f, 0.04f, 0.25f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.08f, 0.05f, 0.03f);
            break;
        case TableThemeId::LeatherNubuck:
            r.drawRect(x, y, w, h, 0.22f, 0.16f, 0.12f, 1.0f);
            r.drawDustSpecks(x, y, w, h, 50, seed, 0.15f, 0.10f, 0.07f, 0.20f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.30f, 0.10f, 0.07f, 0.05f);
            break;
        case TableThemeId::PaintedWood:
            r.drawRect(x, y, w, h, 0.24f, 0.27f, 0.26f, 1.0f);
            r.drawPatternedRectGradient(x, y, w, h,
                                        0.24f, 0.27f, 0.26f, 0.18f, 0.20f, 0.20f, 1.0f,
                                        eatsbits::ui::VectorPatternType::WoodGrain, 0.12f, 20.0f, 0.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.30f, 0.08f, 0.10f, 0.10f);
            break;
        case TableThemeId::PixelDungeon:
            r.drawRect(x, y, w, h, 0.15f, 0.17f, 0.27f, 1.0f);
            r.drawPatternedRectGradient(x, y, w, h,
                                        0.18f, 0.20f, 0.30f, 0.12f, 0.14f, 0.22f, 1.0f,
                                        eatsbits::ui::VectorPatternType::DotGrid, 0.18f, 24.0f, 0.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.45f, 0.04f, 0.05f, 0.10f);
            break;
        case TableThemeId::PixelWater:
            r.drawRect(x, y, w, h, 0.07f, 0.31f, 0.54f, 1.0f);
            r.drawPatternedRectGradient(x, y, w, h,
                                        0.10f, 0.40f, 0.65f, 0.04f, 0.22f, 0.42f, 1.0f,
                                        eatsbits::ui::VectorPatternType::DiagonalMesh, 0.15f, 16.0f, 0.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.02f, 0.12f, 0.25f);
            break;
        case TableThemeId::ShaderVelvetNoir:
            r.drawRect(x, y, w, h, 0.07f, 0.08f, 0.09f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.45f, 0.02f, 0.02f, 0.03f);
            break;
        case TableThemeId::ShaderCarraraMarble:
            r.drawRect(x, y, w, h, 0.88f, 0.88f, 0.90f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.25f, 0.65f, 0.65f, 0.68f);
            break;
        case TableThemeId::ShaderWateredSilk:
            r.drawRect(x, y, w, h, 0.08f, 0.14f, 0.28f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.02f, 0.04f, 0.10f);
            break;
        case TableThemeId::ShaderPoolCaustics:
            r.drawRect(x, y, w, h, 0.05f, 0.24f, 0.38f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.01f, 0.10f, 0.18f);
            break;
        case TableThemeId::ShaderArcaneParchment:
            r.drawRect(x, y, w, h, 0.85f, 0.78f, 0.66f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.40f, 0.45f, 0.32f, 0.18f);
            break;
        case TableThemeId::ShaderCosmicNebula:
            r.drawRect(x, y, w, h, 0.05f, 0.06f, 0.12f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.45f, 0.02f, 0.02f, 0.05f);
            break;
        case TableThemeId::ShaderSynthwaveSunset:
            r.drawRect(x, y, w, h, 0.08f, 0.05f, 0.14f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.03f, 0.01f, 0.06f);
            break;
        case TableThemeId::ShaderTopographic:
            r.drawRect(x, y, w, h, 0.84f, 0.80f, 0.72f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.55f, 0.48f, 0.38f);
            break;
        default:
            r.drawRect(x, y, w, h, 0.08f, 0.28f, 0.14f, 1.0f);
            r.drawRectVignette(x, y, w, h, 0.0f, 0.35f, 0.02f, 0.12f, 0.05f);
            break;
    }
}

#pragma pack(push, 1)
struct BackgroundBlobHeader {
    char magic[4];          // "EBKG" (Eats Background)
    uint32_t version;       // 1
    uint32_t colorOffset;   // Byte offset from start of this header
    uint32_t colorSize;     // Byte size of Color image (JPEG/PNG)
    uint32_t normalOffset;  // Byte offset from start of this header
    uint32_t normalSize;    // Byte size of Normal image (JPEG/PNG)
    uint32_t ormOffset;     // Byte offset from start of this header
    uint32_t ormSize;       // Byte size of ORM image (JPEG/PNG)
};
#pragma pack(pop)

struct PbrMaterialDef {
    const char* materialName;
    const char* themeSlug;
    float uvScale;
    float normalStrength;
    float specularIntensity;
    float ambientLevel;
    float diffuseLevel;
    float tintR;
    float tintG;
    float tintB;
};

struct PbrTextureData {
    int width{0};
    int height{0};
    std::vector<uint8_t> color;     // RGB
    std::vector<uint8_t> normal;    // RGB
    std::vector<uint8_t> orm;       // RGB (Channel packed: R=AO, G=Roughness, B=Displacement/Metallic)
    std::vector<uint8_t> roughness; // 1-ch (fallback if loose textures loaded)
    std::vector<uint8_t> ao;        // 1-ch (fallback if loose textures loaded)
    bool loaded{false};
    bool loadAttempted{false};
};

std::string resolveThemePath(const std::string& filename) {
    const std::string candidates[] = {
        "/themes/" + filename,
        "themes/" + filename,
        "../themes/" + filename,
        "../../themes/" + filename,
        "assets/themes/" + filename,
        "../assets/themes/" + filename,
        filename
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    return "";
}

const PbrMaterialDef* getMaterialDefForTheme(TableThemeId theme) {
    switch (theme) {
        case TableThemeId::PastelTan:
            return nullptr;
        case TableThemeId::EmeraldLawn: {
            static const PbrMaterialDef d{
                "Fabric037", "emerald_lawn", 2.8f, 1.25f, 0.18f, 0.26f, 0.85f, 0.50f, 1.15f, 0.65f
            };
            return &d;
        }
        case TableThemeId::HopsackTweed: {
            static const PbrMaterialDef d{
                "Fabric063", "hopsack_tweed", 2.5f, 1.50f, 0.15f, 0.24f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::DenimChambray: {
            static const PbrMaterialDef d{
                "Fabric077", "denim_chambray", 3.0f, 1.25f, 0.18f, 0.22f, 0.85f, 0.65f, 0.80f, 1.15f
            };
            return &d;
        }
        case TableThemeId::StripedJersey: {
            static const PbrMaterialDef d{
                "Fabric079", "striped_jersey", 2.6f, 1.40f, 0.20f, 0.24f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::SunkenSanctum: {
            static const PbrMaterialDef d{
                "Onyx013", "sunken_sanctum", 1.7f, 1.15f, 0.85f, 0.16f, 0.84f, 0.75f, 0.88f, 1.05f
            };
            return &d;
        }
        case TableThemeId::PerilousShores: {
            static const PbrMaterialDef d{
                "Ground091", "perilous_shores", 2.0f, 1.50f, 0.20f, 0.22f, 0.88f, 1.0f, 0.98f, 0.92f
            };
            return &d;
        }
        case TableThemeId::PebbledCowhide: {
            static const PbrMaterialDef d{
                "LeatherPebbled", "pebbled_cowhide", 2.0f, 1.60f, 0.35f, 0.20f, 0.88f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::LeatherNubuck: {
            static const PbrMaterialDef d{
                "Leather006", "leather006", 2.0f, 1.40f, 0.25f, 0.22f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::PaintedWood: {
            static const PbrMaterialDef d{
                "PaintedWood008C", "paintedwood008c", 2.2f, 1.50f, 0.30f, 0.22f, 0.88f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::PixelDungeon: {
            static const PbrMaterialDef d{
                "PixelDungeon", "pixel_dungeon_brick", 1.5f, 1.80f, 0.20f, 0.25f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::PixelWater: {
            static const PbrMaterialDef d{
                "PixelWater", "pixel_emerald_water", 1.5f, 1.20f, 0.70f, 0.25f, 0.85f, 1.0f, 1.0f, 1.0f
            };
            return &d;
        }
        case TableThemeId::ShaderVelvetNoir: {
            static const PbrMaterialDef d{ "ShaderVelvetNoir", "shader_velvet_noir", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderCarraraMarble: {
            static const PbrMaterialDef d{ "ShaderCarraraMarble", "shader_carrara_marble", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderWateredSilk: {
            static const PbrMaterialDef d{ "ShaderWateredSilk", "shader_watered_silk", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderPoolCaustics: {
            static const PbrMaterialDef d{ "ShaderPoolCaustics", "shader_pool_caustics", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderArcaneParchment: {
            static const PbrMaterialDef d{ "ShaderArcaneParchment", "shader_arcane_parchment", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderCosmicNebula: {
            static const PbrMaterialDef d{ "ShaderCosmicNebula", "shader_cosmic_nebula", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderSynthwaveSunset: {
            static const PbrMaterialDef d{ "ShaderSynthwaveSunset", "shader_synthwave_sunset", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        case TableThemeId::ShaderTopographic: {
            static const PbrMaterialDef d{ "ShaderTopographic", "shader_topographic", 1.0f, 1.0f, 0.5f, 0.2f, 0.8f, 1.0f, 1.0f, 1.0f };
            return &d;
        }
        default:
            return nullptr;
    }
}

bool loadMaterialFromBytes(const uint8_t* blobPtr, size_t blobSize, PbrTextureData& outTex, [[maybe_unused]] PbrMaterialDef& ioDef) {
    if (!blobPtr || blobSize == 0) return false;

    // 1. Check for EBKG container (Channel-packed PBR)
    if (blobSize >= sizeof(BackgroundBlobHeader) &&
        std::memcmp(blobPtr, "EBKG", 4) == 0) {
        const auto* header = reinterpret_cast<const BackgroundBlobHeader*>(blobPtr);
        if (header->version != 1) return false;

        if (static_cast<size_t>(header->colorOffset) + header->colorSize > blobSize ||
            static_cast<size_t>(header->normalOffset) + header->normalSize > blobSize ||
            static_cast<size_t>(header->ormOffset) + header->ormSize > blobSize) {
            return false;
        }

        int w = 0, h = 0, c = 0;
        stbi_uc* colorData = stbi_load_from_memory(blobPtr + header->colorOffset, static_cast<int>(header->colorSize), &w, &h, &c, 3);
        if (!colorData || w <= 0 || h <= 0) {
            if (colorData) stbi_image_free(colorData);
            return false;
        }
        outTex.width = w;
        outTex.height = h;
        outTex.color.assign(colorData, colorData + static_cast<size_t>(w * h * 3));
        stbi_image_free(colorData);

        int nw = 0, nh = 0;
        stbi_uc* normalData = stbi_load_from_memory(blobPtr + header->normalOffset, static_cast<int>(header->normalSize), &nw, &nh, &c, 3);
        if (!normalData || nw != w || nh != h) {
            if (normalData) stbi_image_free(normalData);
            outTex.color.clear();
            return false;
        }
        outTex.normal.assign(normalData, normalData + static_cast<size_t>(w * h * 3));
        stbi_image_free(normalData);

        int ow = 0, oh = 0;
        stbi_uc* ormData = stbi_load_from_memory(blobPtr + header->ormOffset, static_cast<int>(header->ormSize), &ow, &oh, &c, 3);
        if (!ormData || ow != w || oh != h) {
            if (ormData) stbi_image_free(ormData);
            outTex.color.clear();
            outTex.normal.clear();
            return false;
        }
        outTex.orm.assign(ormData, ormData + static_cast<size_t>(w * h * 3));
        stbi_image_free(ormData);

        outTex.loaded = true;
        return true;
    }

    // 2. Generic image fallback (PNG, JPEG, WebP, etc.)
    int w = 0, h = 0, c = 0;
    stbi_uc* imgData = stbi_load_from_memory(blobPtr, static_cast<int>(blobSize), &w, &h, &c, 3);
    if (imgData && w > 0 && h > 0) {
        outTex.width = w;
        outTex.height = h;
        outTex.color.assign(imgData, imgData + static_cast<size_t>(w * h * 3));
        stbi_image_free(imgData);

        // Synthesize flat tangent normal (128, 128, 255)
        outTex.normal.resize(static_cast<size_t>(w * h * 3));
        for (size_t i = 0; i < static_cast<size_t>(w * h); ++i) {
            outTex.normal[i * 3 + 0] = 128;
            outTex.normal[i * 3 + 1] = 128;
            outTex.normal[i * 3 + 2] = 255;
        }

        // Synthesize neutral ORM (R=AO=255, G=Roughness=140, B=Metallic=0)
        outTex.orm.resize(static_cast<size_t>(w * h * 3));
        for (size_t i = 0; i < static_cast<size_t>(w * h); ++i) {
            outTex.orm[i * 3 + 0] = 255;
            outTex.orm[i * 3 + 1] = 140;
            outTex.orm[i * 3 + 2] = 0;
        }

        outTex.loaded = true;
        return true;
    }

    return false;
}

bool loadMaterialFromEats(const std::string& filePath, PbrTextureData& outTex, PbrMaterialDef& ioDef) {
    if (outTex.loaded) return true;
    outTex.loadAttempted = true;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Check if this package or script contains procedural Eatscript synthesis
    if (isProceduralScript(content)) {
        ProceduralPbrMaps maps;
        ProceduralBakeOptions opts;
        opts.width = 512;
        opts.height = 512;
        opts.overrideDimensions = true;
        if (bakeProceduralPbrBundle(content, maps, opts)) {
            outTex.width = maps.width;
            outTex.height = maps.height;
            outTex.color = std::move(maps.colorRgb);
            outTex.normal = std::move(maps.normalRgb);
            outTex.orm = std::move(maps.orm);
            outTex.loaded = true;
            std::cout << "[SolEngine] Successfully synthesized procedural theme script: " << filePath
                      << " (" << outTex.width << "x" << outTex.height << ")\n";
            return true;
        }
    }

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

    // Evaluate script with API bindings
    eatsbits::eatscript::Evaluator evaluator;
    bool apiCalled = false;
    std::vector<uint8_t> apiBlob;

    auto setBgFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
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

        auto getOptFloat = [&](const std::string& key, float defVal) {
            if (kwargs.find(key) != kwargs.end()) return kwargs.at(key).asFloat(defVal);
            if (optsDict && optsDict->hasKey(key)) return optsDict->get(key).asFloat(defVal);
            return defVal;
        };

        ioDef.uvScale = getOptFloat("uv_scale", ioDef.uvScale);
        ioDef.normalStrength = getOptFloat("normal_strength", ioDef.normalStrength);
        ioDef.specularIntensity = getOptFloat("specular_intensity", ioDef.specularIntensity);
        ioDef.ambientLevel = getOptFloat("ambient_level", ioDef.ambientLevel);
        ioDef.diffuseLevel = getOptFloat("diffuse_level", ioDef.diffuseLevel);
        ioDef.tintR = getOptFloat("tint_r", ioDef.tintR);
        ioDef.tintG = getOptFloat("tint_g", ioDef.tintG);
        ioDef.tintB = getOptFloat("tint_b", ioDef.tintB);

        // Check array tint: [r, g, b]
        if (optsDict && optsDict->hasKey("tint")) {
            auto tintVal = optsDict->get("tint");
            if (tintVal.isList() && tintVal.listVal.size() >= 3) {
                ioDef.tintR = tintVal.listVal[0].asFloat(ioDef.tintR);
                ioDef.tintG = tintVal.listVal[1].asFloat(ioDef.tintG);
                ioDef.tintB = tintVal.listVal[2].asFloat(ioDef.tintB);
            }
        }

        return eatsbits::eatscript::Value(true);
    };

    auto themeDummyFn = [](const auto&, const auto&) { return eatsbits::eatscript::Value(true); };
    evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("sol", "create_theme", themeDummyFn);
    evaluator.registerMemberFunction("sol", "set_theme", themeDummyFn);
    evaluator.registerMemberFunction("sol", "set_background", setBgFn);
    evaluator.registerFunction("create_theme", themeDummyFn);
    evaluator.registerFunction("set_theme", themeDummyFn);
    evaluator.registerFunction("set_background", setBgFn);

    evaluator.evaluateSource(scriptPart);

    std::vector<uint8_t> rawBytes;
    if (apiCalled && !apiBlob.empty()) {
        rawBytes = std::move(apiBlob);
    } else {
        if (evaluator.hasVariable("uv_scale")) {
            ioDef.uvScale = evaluator.getVariable("uv_scale").asFloat(ioDef.uvScale);
        }
        if (evaluator.hasVariable("normal_strength")) {
            ioDef.normalStrength = evaluator.getVariable("normal_strength").asFloat(ioDef.normalStrength);
        }
        if (evaluator.hasVariable("specular_intensity")) {
            ioDef.specularIntensity = evaluator.getVariable("specular_intensity").asFloat(ioDef.specularIntensity);
        }
        if (evaluator.hasVariable("ambient_level")) {
            ioDef.ambientLevel = evaluator.getVariable("ambient_level").asFloat(ioDef.ambientLevel);
        }
        if (evaluator.hasVariable("diffuse_level")) {
            ioDef.diffuseLevel = evaluator.getVariable("diffuse_level").asFloat(ioDef.diffuseLevel);
        }
        if (evaluator.hasVariable("tint_r")) {
            ioDef.tintR = evaluator.getVariable("tint_r").asFloat(ioDef.tintR);
        }
        if (evaluator.hasVariable("tint_g")) {
            ioDef.tintG = evaluator.getVariable("tint_g").asFloat(ioDef.tintG);
        }
        if (evaluator.hasVariable("tint_b")) {
            ioDef.tintB = evaluator.getVariable("tint_b").asFloat(ioDef.tintB);
        }

        if (!blobPart.empty()) {
            rawBytes.assign(blobPart.begin(), blobPart.end());
        } else {
            std::string blobStr;
            if (evaluator.hasVariable("bg_blob")) {
                blobStr = evaluator.getVariable("bg_blob").asString();
            } else if (evaluator.hasVariable("background_blob")) {
                blobStr = evaluator.getVariable("background_blob").asString();
            } else if (evaluator.hasVariable("blob")) {
                blobStr = evaluator.getVariable("blob").asString();
            }
            if (!blobStr.empty()) {
                rawBytes = eatsbits::ui::base64Decode(blobStr);
            }
        }
    }

    if (rawBytes.empty()) {
        std::cerr << "[SolEngine] No background payload found in Eatscript theme: " << filePath << "\n";
        return false;
    }

    if (!loadMaterialFromBytes(rawBytes.data(), rawBytes.size(), outTex, ioDef)) {
        std::cerr << "[SolEngine] Failed to decode background material in: " << filePath << "\n";
        return false;
    }

    std::cout << "[SolEngine] Successfully loaded single-file .eats theme: " << filePath
              << " (" << outTex.width << "x" << outTex.height << ", channel-packed ORM)\n";
    return true;
}

bool bakePbrSurface(
    const PbrTextureData& tex,
    const PbrMaterialDef& def,
    float x, float y, float w, float h,
    std::vector<uint8_t>& outRgba,
    int& outW, int& outH
) {
    int bakeW = static_cast<int>(std::ceil(w));
    int bakeH = static_cast<int>(std::ceil(h));
    const int texW = tex.width;
    const int texH = tex.height;
    if (bakeW <= 0 || bakeH <= 0 || !tex.loaded || texW <= 0 || texH <= 0 ||
        tex.color.size() < static_cast<size_t>(texW * texH * 3)) {
        return false;
    }

    outW = bakeW;
    outH = bakeH;
    outRgba.resize(static_cast<size_t>(bakeW) * bakeH * 4);

    const float uvScale = def.uvScale;
    const float normStrength = def.normalStrength;
    const float specIntensity = def.specularIntensity;
    const float amb = def.ambientLevel;
    const float diffLev = def.diffuseLevel;
    const float tintR = def.tintR;
    const float tintG = def.tintG;
    const float tintB = def.tintB;

    const bool hasNormal = (tex.normal.size() >= static_cast<size_t>(texW * texH * 3));
    const bool hasOrm = (tex.orm.size() >= static_cast<size_t>(texW * texH * 3));

    // Neutral overhead directional reference vector for tactile surface relief
    // Slightly offset from top-left (0.12, 0.20, 0.96) to reveal normal-mapped weave/relief
    constexpr float lDirX = 0.12f;
    constexpr float lDirY = 0.20f;
    constexpr float lDirZ = 0.96f;

    // Blinn-Phong half-vector with view vector V = (0, 0, 1)
    constexpr float hx = lDirX * 0.505f;
    constexpr float hy = lDirY * 0.505f;
    constexpr float hz = (lDirZ + 1.0f) * 0.505f;

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (int py = 0; py < bakeH; ++py) {
        float worldY = y + static_cast<float>(py);

        float vNorm = (worldY * uvScale) / static_cast<float>(texH);
        float vWrap = vNorm - std::floor(vNorm);
        float fy = vWrap * static_cast<float>(texH) - 0.5f;
        int y0 = static_cast<int>(std::floor(fy));
        float wy = fy - static_cast<float>(y0);
        int y1 = y0 + 1;
        y0 = (y0 % texH + texH) % texH;
        y1 = (y1 % texH + texH) % texH;

        uint8_t* dstRow = &outRgba[static_cast<size_t>(py * bakeW) * 4];

        for (int px = 0; px < bakeW; ++px) {
            float worldX = x + static_cast<float>(px);

            float uNorm = (worldX * uvScale) / static_cast<float>(texW);
            float uWrap = uNorm - std::floor(uNorm);
            float fx = uWrap * static_cast<float>(texW) - 0.5f;
            int x0 = static_cast<int>(std::floor(fx));
            float wx = fx - static_cast<float>(x0);
            int x1 = x0 + 1;
            x0 = (x0 % texW + texW) % texW;
            x1 = (x1 % texW + texW) % texW;

            float w00 = (1.0f - wx) * (1.0f - wy);
            float w10 = wx * (1.0f - wy);
            float w01 = (1.0f - wx) * wy;
            float w11 = wx * wy;

            size_t idx00 = static_cast<size_t>(y0 * texW + x0);
            size_t idx10 = static_cast<size_t>(y0 * texW + x1);
            size_t idx01 = static_cast<size_t>(y1 * texW + x0);
            size_t idx11 = static_cast<size_t>(y1 * texW + x1);

            // 1. Bilinear Sample Color (RGB)
            float colR = (tex.color[idx00 * 3 + 0] * w00 +
                          tex.color[idx10 * 3 + 0] * w10 +
                          tex.color[idx01 * 3 + 0] * w01 +
                          tex.color[idx11 * 3 + 0] * w11) * (1.0f / 255.0f) * tintR;
            float colG = (tex.color[idx00 * 3 + 1] * w00 +
                          tex.color[idx10 * 3 + 1] * w10 +
                          tex.color[idx01 * 3 + 1] * w01 +
                          tex.color[idx11 * 3 + 1] * w11) * (1.0f / 255.0f) * tintG;
            float colB = (tex.color[idx00 * 3 + 2] * w00 +
                          tex.color[idx10 * 3 + 2] * w10 +
                          tex.color[idx01 * 3 + 2] * w01 +
                          tex.color[idx11 * 3 + 2] * w11) * (1.0f / 255.0f) * tintB;

            // 2. Bilinear Sample Normal
            float nx = 0.0f, ny = 0.0f, nz = 1.0f;
            if (hasNormal) {
                float nr = (tex.normal[idx00 * 3 + 0] * w00 +
                            tex.normal[idx10 * 3 + 0] * w10 +
                            tex.normal[idx01 * 3 + 0] * w01 +
                            tex.normal[idx11 * 3 + 0] * w11) * (1.0f / 255.0f);
                float ng = (tex.normal[idx00 * 3 + 1] * w00 +
                            tex.normal[idx10 * 3 + 1] * w10 +
                            tex.normal[idx01 * 3 + 1] * w01 +
                            tex.normal[idx11 * 3 + 1] * w11) * (1.0f / 255.0f);
                float nb = (tex.normal[idx00 * 3 + 2] * w00 +
                            tex.normal[idx10 * 3 + 2] * w10 +
                            tex.normal[idx01 * 3 + 2] * w01 +
                            tex.normal[idx11 * 3 + 2] * w11) * (1.0f / 255.0f);

                nx = (nr * 2.0f - 1.0f) * normStrength;
                ny = -(ng * 2.0f - 1.0f) * normStrength;
                nz = (nb * 2.0f - 1.0f);
                float invLenN = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz + 1e-6f);
                nx *= invLenN;
                ny *= invLenN;
                nz *= invLenN;
            }

            // 3. Bilinear Sample Roughness and AO
            float rough = 0.5f;
            float aoVal = 1.0f;
            if (hasOrm) {
                aoVal = (tex.orm[idx00 * 3 + 0] * w00 +
                         tex.orm[idx10 * 3 + 0] * w10 +
                         tex.orm[idx01 * 3 + 0] * w01 +
                         tex.orm[idx11 * 3 + 0] * w11) * (1.0f / 255.0f);
                rough = (tex.orm[idx00 * 3 + 1] * w00 +
                         tex.orm[idx10 * 3 + 1] * w10 +
                         tex.orm[idx01 * 3 + 1] * w01 +
                         tex.orm[idx11 * 3 + 1] * w11) * (1.0f / 255.0f);
            } else {
                if (!tex.roughness.empty()) {
                    rough = (tex.roughness[idx00] * w00 +
                             tex.roughness[idx10] * w10 +
                             tex.roughness[idx01] * w01 +
                             tex.roughness[idx11] * w11) * (1.0f / 255.0f);
                }
                if (!tex.ao.empty()) {
                    aoVal = (tex.ao[idx00] * w00 +
                             tex.ao[idx10] * w10 +
                             tex.ao[idx01] * w01 +
                             tex.ao[idx11] * w11) * (1.0f / 255.0f);
                }
            }

            // Wrapped Lambertian diffuse for micro-relief
            float nDotL = nx * lDirX + ny * lDirY + nz * lDirZ;
            float diff = 0.35f + 0.65f * std::max(0.0f, nDotL);

            // Blinn-Phong specular sheen
            float nDotH = std::max(0.0f, nx * hx + ny * hy + nz * hz);
            float gloss = 1.0f - rough;
            float specPower = 4.0f + gloss * gloss * 128.0f;
            float spec = std::pow(nDotH, specPower) * (gloss * gloss) * specIntensity * 0.45f;

            // Composite physical tactile surface texture
            float litR = colR * aoVal * (amb + diffLev * diff) + spec;
            float litG = colG * aoVal * (amb + diffLev * diff) + spec;
            float litB = colB * aoVal * (amb + diffLev * diff) + spec;

            size_t pOffset = static_cast<size_t>(px) * 4;
            dstRow[pOffset + 0] = static_cast<uint8_t>(std::clamp(litR * 255.0f, 0.0f, 255.0f));
            dstRow[pOffset + 1] = static_cast<uint8_t>(std::clamp(litG * 255.0f, 0.0f, 255.0f));
            dstRow[pOffset + 2] = static_cast<uint8_t>(std::clamp(litB * 255.0f, 0.0f, 255.0f));
            dstRow[pOffset + 3] = 255;
        }
    }
    return true;
}

inline bool readTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

inline bool hasCanvasBackground(const std::string& script) {
    return script.find("on_draw_background") != std::string::npos ||
           script.find("on_draw_table") != std::string::npos ||
           script.find("background_shader") != std::string::npos;
}

bool bakeCanvasSurface(
    const std::string& scriptSource,
    float x, float y, float w, float h,
    const eatsbits::ui::LightSource2D& light,
    std::vector<uint8_t>& outRgba,
    int& outW, int& outH
) {
    int bakeW = static_cast<int>(std::ceil(w));
    int bakeH = static_cast<int>(std::ceil(h));
    if (bakeW <= 0 || bakeH <= 0) return false;

    CardCanvas canvas(bakeW, bakeH);
    canvas.setCornerRadius(0.0f);

    eatsbits::eatscript::Evaluator evaluator;
    CardCanvas::bindToEvaluator(evaluator, &canvas);

    // Context & layout variables
    evaluator.setVariable("w", eatsbits::eatscript::Value(static_cast<double>(bakeW)));
    evaluator.setVariable("h", eatsbits::eatscript::Value(static_cast<double>(bakeH)));
    evaluator.setVariable("canvas_w", eatsbits::eatscript::Value(static_cast<double>(bakeW)));
    evaluator.setVariable("canvas_h", eatsbits::eatscript::Value(static_cast<double>(bakeH)));
    evaluator.setVariable("width", eatsbits::eatscript::Value(static_cast<double>(bakeW)));
    evaluator.setVariable("height", eatsbits::eatscript::Value(static_cast<double>(bakeH)));
    evaluator.setVariable("table_x", eatsbits::eatscript::Value(static_cast<double>(x)));
    evaluator.setVariable("table_y", eatsbits::eatscript::Value(static_cast<double>(y)));
    evaluator.setVariable("light_x", eatsbits::eatscript::Value(static_cast<double>(light.x)));
    evaluator.setVariable("light_y", eatsbits::eatscript::Value(static_cast<double>(light.y)));

    // Theme metadata registration dummies
    auto dummyFn = [](const auto&, const auto&) { return eatsbits::eatscript::Value(true); };
    evaluator.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("sol", "set_theme", dummyFn);
    evaluator.registerMemberFunction("sol", "create_theme", dummyFn);
    evaluator.registerMemberFunction("sol", "set_background", dummyFn);
    evaluator.registerFunction("set_theme", dummyFn);
    evaluator.registerFunction("create_theme", dummyFn);
    evaluator.registerFunction("set_background", dummyFn);

    evaluator.evaluateSource(scriptSource);

    if (evaluator.hasFunction("on_draw_background")) {
        evaluator.callFunction("on_draw_background", {
            eatsbits::eatscript::Value("ctx"),
            eatsbits::eatscript::Value(static_cast<double>(bakeW)),
            eatsbits::eatscript::Value(static_cast<double>(bakeH))
        });
        if (!evaluator.getLastError().empty()) {
            std::cerr << "[SolEngine] on_draw_background error: " << evaluator.getLastError() << "\n";
        }
    } else if (evaluator.hasFunction("on_draw_table")) {
        evaluator.callFunction("on_draw_table", {
            eatsbits::eatscript::Value("ctx"),
            eatsbits::eatscript::Value(static_cast<double>(bakeW)),
            eatsbits::eatscript::Value(static_cast<double>(bakeH))
        });
        if (!evaluator.getLastError().empty()) {
            std::cerr << "[SolEngine] on_draw_table error: " << evaluator.getLastError() << "\n";
        }
    } else if (evaluator.hasFunction("background_shader")) {
        auto fnVal = evaluator.getVariable("background_shader");
        if (fnVal.isFunction() && fnVal.funcDef) {
            canvas.applyShader(fnVal.funcDef.get(), "", 1, Suit::Spades, 1.0f);
        }
    }

    outW = bakeW;
    outH = bakeH;
    outRgba = std::move(canvas.rgba());
    return true;
}

static inline std::string toLowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static TableThemeId mapSlugToLegacyTheme(const std::string& id, const std::string& stem) {
    std::string s = toLowerStr(id + " " + stem);
    if (s.find("carpet012") != std::string::npos || s.find("emerald") != std::string::npos) return TableThemeId::EmeraldLawn;
    if (s.find("fabric063") != std::string::npos || s.find("hopsack") != std::string::npos) return TableThemeId::HopsackTweed;
    if (s.find("fabric061") != std::string::npos || s.find("denim") != std::string::npos || s.find("chambray") != std::string::npos) return TableThemeId::DenimChambray;
    if (s.find("fabric079") != std::string::npos || s.find("jersey") != std::string::npos) return TableThemeId::StripedJersey;
    if (s.find("onyx013") != std::string::npos || s.find("sanctum") != std::string::npos) return TableThemeId::SunkenSanctum;
    if (s.find("moss002") != std::string::npos || s.find("perilous") != std::string::npos || s.find("shores") != std::string::npos) return TableThemeId::PerilousShores;
    if (s.find("pebbled") != std::string::npos || s.find("cowhide") != std::string::npos) return TableThemeId::PebbledCowhide;
    if (s.find("leather006") != std::string::npos || s.find("distressed") != std::string::npos) return TableThemeId::LeatherNubuck;
    if (s.find("paintedwood008c") != std::string::npos) return TableThemeId::PaintedWood;
    if (s.find("pixel_dungeon") != std::string::npos) return TableThemeId::PixelDungeon;
    if (s.find("pixel_emerald_water") != std::string::npos || s.find("pixel_water") != std::string::npos) return TableThemeId::PixelWater;
    if (s.find("velvet_noir") != std::string::npos) return TableThemeId::ShaderVelvetNoir;
    if (s.find("carrara_marble") != std::string::npos) return TableThemeId::ShaderCarraraMarble;
    if (s.find("watered_silk") != std::string::npos) return TableThemeId::ShaderWateredSilk;
    if (s.find("pool_caustics") != std::string::npos) return TableThemeId::ShaderPoolCaustics;
    if (s.find("arcane_parchment") != std::string::npos) return TableThemeId::ShaderArcaneParchment;
    if (s.find("cosmic_nebula") != std::string::npos) return TableThemeId::ShaderCosmicNebula;
    if (s.find("synthwave_sunset") != std::string::npos) return TableThemeId::ShaderSynthwaveSunset;
    if (s.find("topographic") != std::string::npos) return TableThemeId::ShaderTopographic;
    return TableThemeId::Count;
}

} // namespace

struct ProceduralBackgroundSystem::Impl {
    mutable std::recursive_mutex cacheMutex;
    bool pbrEnabled{true};
    std::unordered_map<TableThemeId, PbrTextureData> materials;
    std::unordered_map<TableThemeId, PbrMaterialDef> materialDefs;
    std::unordered_map<TableThemeId, std::string> themeCanvasSources;

    bool customThemeLoaded{false};
    bool isCanvasTheme{false};
    std::string canvasThemeSource;
    PbrTextureData customTexture;
    PbrMaterialDef customDef;

    std::vector<TableThemeDef> scannedThemes;
    bool themesScanned{false};

    TableThemeId cachedTheme{static_cast<TableThemeId>(-1)};
    bool cachedCustom{false};
    int cachedW{0};
    int cachedH{0};
    std::vector<uint8_t> cachedBakedRgba;

    void invalidate() noexcept {
        std::lock_guard<std::recursive_mutex> lock(cacheMutex);
        cachedTheme = static_cast<TableThemeId>(-1);
        cachedCustom = false;
        cachedW = 0;
        cachedH = 0;
        cachedBakedRgba.clear();
    }

    void scanThemes(const std::string& themesDir = "themes") {
        std::lock_guard<std::recursive_mutex> lock(cacheMutex);
        scannedThemes.clear();

        // 1. Built-in clean non-shader table
        scannedThemes.push_back(TableThemeDef{
            "pastel_tan",
            "PASTEL TAN",
            "",
            TableThemeId::PastelTan,
            false
        });

        std::error_code ec;
        const std::string searchPaths[] = {
            themesDir,
            "/" + themesDir,
            "../" + themesDir,
            "../../" + themesDir,
            "../../../" + themesDir,
            "assets/" + themesDir,
            "../assets/" + themesDir,
            "./" + themesDir
        };

        std::string targetDir;
        for (const auto& sp : searchPaths) {
            if (std::filesystem::exists(sp, ec) && std::filesystem::is_directory(sp, ec)) {
                targetDir = sp;
                break;
            }
        }

        if (targetDir.empty()) {
            for (uint8_t i = 1; i < static_cast<uint8_t>(TableThemeId::Count); ++i) {
                TableThemeId t = static_cast<TableThemeId>(i);
                const char* nm = SolitaireBoard::themeName(t);
                scannedThemes.push_back(TableThemeDef{
                    toLowerStr(nm),
                    nm,
                    "",
                    t,
                    true
                });
            }
            themesScanned = true;
            return;
        }

        std::unordered_map<std::string, size_t> idToIndex;
        std::unordered_map<std::string, size_t> nameToIndex;

        for (const auto& entry : std::filesystem::directory_iterator(targetDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".eats") continue;

            std::string filePath = entry.path().string();
            std::string stem = entry.path().stem().string();

            std::ifstream f(filePath);
            if (!f.is_open()) continue;
            std::string line;
            std::string parsedId;
            std::string parsedName;
            int linesRead = 0;
            while (std::getline(f, line) && linesRead++ < 50) {
                size_t cPos = line.find('#');
                if (cPos != std::string::npos && line.find('"') == std::string::npos && line.find('\'') == std::string::npos) {
                    line = line.substr(0, cPos);
                }
                auto extractQuoted = [](const std::string& l, const std::string& key) -> std::string {
                    size_t k = l.find(key);
                    if (k == std::string::npos) return "";
                    size_t q1 = l.find_first_of("\"'", k + key.size());
                    if (q1 == std::string::npos) return "";
                    char qChar = l[q1];
                    size_t q2 = l.find(qChar, q1 + 1);
                    if (q2 == std::string::npos) return "";
                    return l.substr(q1 + 1, q2 - q1 - 1);
                };
                if (parsedId.empty()) {
                    parsedId = extractQuoted(line, "id");
                }
                if (parsedName.empty()) {
                    parsedName = extractQuoted(line, "name");
                }
            }

            if (parsedId.empty()) parsedId = stem;
            if (parsedName.empty()) {
                std::string s = stem;
                if (s.rfind("shader_", 0) == 0) s = s.substr(7);
                for (char& c : s) if (c == '_') c = ' ';
                parsedName = s;
            }

            for (char& c : parsedName) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            TableThemeId legacy = mapSlugToLegacyTheme(parsedId, stem);

            std::string lowerId = toLowerStr(parsedId);
            std::string lowerName = toLowerStr(parsedName);

            auto itName = nameToIndex.find(lowerName);
            if (itName != nameToIndex.end()) {
                size_t existingIdx = itName->second;
                if (stem.find("carpet") == std::string::npos && stem.find("fabric") == std::string::npos && stem.find("moss") == std::string::npos) {
                    scannedThemes[existingIdx].filePath = filePath;
                    scannedThemes[existingIdx].id = parsedId;
                }
                continue;
            }

            auto itId = idToIndex.find(lowerId);
            if (itId != idToIndex.end()) {
                size_t existingIdx = itId->second;
                if (stem.find("carpet") == std::string::npos && stem.find("fabric") == std::string::npos && stem.find("moss") == std::string::npos) {
                    scannedThemes[existingIdx].filePath = filePath;
                    scannedThemes[existingIdx].name = parsedName;
                }
                continue;
            }

            size_t newIdx = scannedThemes.size();
            scannedThemes.push_back(TableThemeDef{
                parsedId,
                parsedName,
                filePath,
                legacy,
                true
            });
            idToIndex[lowerId] = newIdx;
            nameToIndex[lowerName] = newIdx;
        }

        // Sort dynamically discovered themes alphabetically after index 0 (Pastel Tan remains index 0)
        if (scannedThemes.size() > 2) {
            std::sort(scannedThemes.begin() + 1, scannedThemes.end(), [](const TableThemeDef& a, const TableThemeDef& b) {
                return a.name < b.name;
            });
        }

        themesScanned = true;
    }

    bool ensureMaterial(TableThemeId theme, const PbrMaterialDef& defaultDef) {
        {
            std::lock_guard<std::recursive_mutex> lock(cacheMutex);
            auto it = materials.find(theme);
            if (it != materials.end()) {
                if (it->second.loaded) return true;
                if (it->second.loadAttempted) return false;
            }
            materials[theme].loadAttempted = true;
        }

        PbrTextureData localTex;
        PbrMaterialDef activeDef = defaultDef;
        bool loaded = false;

        // 1. Priority 1: Check single-file .eats theme package FIRST
        std::string themeFile = std::string(defaultDef.themeSlug) + ".eats";
        std::string themePath = resolveThemePath(themeFile);
        if (themePath.empty()) {
            themePath = resolveThemePath(std::string(defaultDef.materialName) + ".eats");
        }
        if (!themePath.empty()) {
            std::string scriptContent;
            if (readTextFile(themePath, scriptContent) && hasCanvasBackground(scriptContent)) {
                std::lock_guard<std::recursive_mutex> lock(cacheMutex);
                themeCanvasSources[theme] = std::move(scriptContent);
                materials[theme].loaded = true;
                return true;
            }
            if (loadMaterialFromEats(themePath, localTex, activeDef)) {
                loaded = true;
            }
        }

        // 2. Priority 2: Check procedural .eat script (Zero heavy PNGs, CPU SIMD synthesis)
        if (!loaded) {
            std::string procPath = resolveProceduralScriptPath(defaultDef.themeSlug);
            if (procPath.empty()) {
                procPath = resolveProceduralScriptPath(defaultDef.materialName);
            }
            if (!procPath.empty()) {
                std::string scriptContent;
                if (readTextFile(procPath, scriptContent) && hasCanvasBackground(scriptContent)) {
                    std::lock_guard<std::recursive_mutex> lock(cacheMutex);
                    themeCanvasSources[theme] = std::move(scriptContent);
                    materials[theme].loaded = true;
                    return true;
                }
                ProceduralPbrMaps maps;
                ProceduralBakeOptions opts;
                opts.width = 512;
                opts.height = 512;
                opts.overrideDimensions = true;
                if (bakeProceduralPbrBundle(procPath, maps, opts)) {
                    localTex.width = maps.width;
                    localTex.height = maps.height;
                    localTex.color = std::move(maps.colorRgb);
                    localTex.normal = std::move(maps.normalRgb);
                    localTex.orm = std::move(maps.orm);
                    localTex.loaded = true;
                    loaded = true;
                    std::cout << "[SolEngine] Synthesized procedural PBR theme: " << defaultDef.themeSlug
                              << " from " << procPath << " (" << localTex.width << "x" << localTex.height << ")\n";
                }
            }
        }

        std::lock_guard<std::recursive_mutex> lock(cacheMutex);
        if (loaded && localTex.loaded) {
            materials[theme] = std::move(localTex);
            materialDefs[theme] = activeDef;
            return true;
        }
        materials[theme].loaded = false;
        return false;
    }
};

ProceduralBackgroundSystem::ProceduralBackgroundSystem()
    : impl_(std::make_unique<Impl>()) {
    impl_->scanThemes("themes");
}

ProceduralBackgroundSystem::~ProceduralBackgroundSystem() = default;

ProceduralBackgroundSystem& ProceduralBackgroundSystem::instance() {
    static ProceduralBackgroundSystem s_instance;
    return s_instance;
}

const std::vector<TableThemeDef>& ProceduralBackgroundSystem::availableThemes() const noexcept {
    if (impl_) {
        if (!impl_->themesScanned) {
            impl_->scanThemes("themes");
        }
        return impl_->scannedThemes;
    }
    static const std::vector<TableThemeDef> s_empty;
    return s_empty;
}

const TableThemeDef* ProceduralBackgroundSystem::getThemeDef(size_t index) const noexcept {
    const auto& list = availableThemes();
    if (index < list.size()) {
        return &list[index];
    }
    return nullptr;
}

size_t ProceduralBackgroundSystem::findThemeIndex(const std::string& idOrName, TableThemeId fallback) const noexcept {
    const auto& list = availableThemes();
    if (list.empty()) return 0;

    if (!idOrName.empty()) {
        std::string lower = toLowerStr(idOrName);
        for (size_t i = 0; i < list.size(); ++i) {
            std::string idLower = toLowerStr(list[i].id);
            std::string nameLower = toLowerStr(list[i].name);
            if (idLower == lower || nameLower == lower) {
                return i;
            }
        }
        for (size_t i = 0; i < list.size(); ++i) {
            std::string idLower = toLowerStr(list[i].id);
            std::string nameLower = toLowerStr(list[i].name);
            std::string pathLower = toLowerStr(list[i].filePath);
            if (idLower.find(lower) != std::string::npos || nameLower.find(lower) != std::string::npos ||
                pathLower.find(lower) != std::string::npos || lower.find(idLower) != std::string::npos) {
                return i;
            }
        }
    }

    if (fallback != TableThemeId::Count) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].legacyId == fallback) {
                return i;
            }
        }
    }
    return 0;
}

void ProceduralBackgroundSystem::scanThemesFolder(const std::string& themesDir) {
    if (impl_) {
        impl_->scanThemes(themesDir);
    }
}

bool ProceduralBackgroundSystem::prebakeThemeDef(
    const TableThemeDef& def,
    float w, float h,
    const eatsbits::ui::LightSource2D& light,
    const std::function<bool(float progress, const std::string& status)>& onProgress
) {
    if (!impl_) return false;

    if (def.filePath.empty()) {
        clearCustomEatsTheme();
        if (onProgress) onProgress(1.0f, "Vector table surface ready.");
        return true;
    }

    if (onProgress && !onProgress(0.15f, "Loading theme: " + def.name + "...")) {
        return false;
    }

    if (!loadCustomEatsTheme(def.filePath)) {
        if (def.legacyId != TableThemeId::Count) {
            return prebakeTheme(def.legacyId, w, h, light, onProgress);
        }
        return false;
    }

    if (onProgress && !onProgress(0.60f, "Baking tactile surface relief...")) {
        return false;
    }

    int bakeW = static_cast<int>(std::ceil(w));
    int bakeH = static_cast<int>(std::ceil(h));
    {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        if (impl_->isCanvasTheme && !impl_->canvasThemeSource.empty()) {
            bakeCanvasSurface(impl_->canvasThemeSource, 0.0f, 0.0f, w, h, light,
                              impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH);
            impl_->cachedCustom = true;
            impl_->cachedTheme = def.legacyId;
        } else if (impl_->customTexture.loaded) {
            bakePbrSurface(impl_->customTexture, impl_->customDef, 0.0f, 0.0f, w, h,
                           impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH);
            impl_->cachedCustom = true;
            impl_->cachedTheme = def.legacyId;
        }
    }

    if (onProgress && !onProgress(1.0f, "Table surface synthesis complete.")) {
        return false;
    }
    return true;
}

void ProceduralBackgroundSystem::invalidateCache() noexcept {
    if (impl_) {
        impl_->invalidate();
    }
}

bool ProceduralBackgroundSystem::prebakeTheme(
    TableThemeId theme,
    float w, float h,
    [[maybe_unused]] const eatsbits::ui::LightSource2D& light,
    const std::function<bool(float progress, const std::string& status)>& onProgress
) {
    if (!impl_) return false;

    if (onProgress && !onProgress(0.10f, "Analyzing theme material definition...")) {
        return false;
    }

    const PbrMaterialDef* def = getMaterialDefForTheme(theme);
    if (!def) {
        if (onProgress) onProgress(1.0f, "Vector table surface ready.");
        return true;
    }

    if (onProgress && !onProgress(0.30f, "Synthesizing procedural PBR normal & color maps...")) {
        return false;
    }

    if (!impl_->ensureMaterial(theme, *def)) {
        if (onProgress) onProgress(1.0f, "Procedural surface fallback ready.");
        return true;
    }

    if (onProgress && !onProgress(0.60f, "Baking initial tactile surface relief...")) {
        return false;
    }

    PbrTextureData texCopy;
    PbrMaterialDef activeDef;
    std::string canvasSrc;
    {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        texCopy = impl_->materials[theme];
        activeDef = impl_->materialDefs[theme];
        auto it = impl_->themeCanvasSources.find(theme);
        if (it != impl_->themeCanvasSources.end()) {
            canvasSrc = it->second;
        }
    }

    std::vector<uint8_t> localBaked;
    int localW = 0;
    int localH = 0;
    if (!canvasSrc.empty()) {
        if (bakeCanvasSurface(canvasSrc, 0.0f, 0.0f, w, h, light, localBaked, localW, localH)) {
            std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
            impl_->cachedBakedRgba = std::move(localBaked);
            impl_->cachedW = localW;
            impl_->cachedH = localH;
            impl_->cachedCustom = false;
            impl_->cachedTheme = theme;
        }
    } else {
        if (bakePbrSurface(texCopy, activeDef, 0.0f, 0.0f, w, h, localBaked, localW, localH)) {
            std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
            impl_->cachedBakedRgba = std::move(localBaked);
            impl_->cachedW = localW;
            impl_->cachedH = localH;
            impl_->cachedCustom = false;
            impl_->cachedTheme = theme;
        }
    }

    if (onProgress && !onProgress(1.0f, "Table surface synthesis complete.")) {
        return false;
    }
    return true;
}

void ProceduralBackgroundSystem::setPbrTexturesEnabled(bool enable) noexcept {
    if (impl_) {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        impl_->pbrEnabled = enable;
        impl_->cachedTheme = static_cast<TableThemeId>(-1);
        impl_->cachedCustom = false;
        impl_->cachedW = 0;
        impl_->cachedH = 0;
        impl_->cachedBakedRgba.clear();
    }
}

bool ProceduralBackgroundSystem::isPbrTexturesEnabled() const noexcept {
    if (!impl_) return false;
    std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
    return impl_->pbrEnabled;
}

bool ProceduralBackgroundSystem::hasProceduralMaterial(TableThemeId theme) const noexcept {
    const PbrMaterialDef* def = getMaterialDefForTheme(theme);
    if (!def) return false;
    if (!resolveProceduralScriptPath(def->themeSlug).empty()) return true;
    if (!resolveProceduralScriptPath(def->materialName).empty()) return true;
    return false;
}

bool ProceduralBackgroundSystem::hasPbrMaterial(TableThemeId theme) const noexcept {
    if (theme == TableThemeId::PastelTan) return false;
    if (impl_ && impl_->customThemeLoaded) return true;
    if (hasProceduralMaterial(theme)) return true;
    const PbrMaterialDef* def = getMaterialDefForTheme(theme);
    if (!def) return false;
    std::string themeFile = std::string(def->themeSlug) + ".eats";
    if (!resolveThemePath(themeFile).empty()) return true;
    return !resolveThemePath(std::string(def->materialName) + ".eats").empty();
}

bool ProceduralBackgroundSystem::loadCustomThemeFromMemory(
    const uint8_t* blobData,
    size_t blobSize,
    float uvScale,
    float normalStrength,
    float specularIntensity,
    float ambientLevel,
    float diffuseLevel,
    float tintR,
    float tintG,
    float tintB
) {
    if (!impl_ || !blobData || blobSize == 0) return false;

    PbrMaterialDef customDef{
        "CustomMaterial", "custom",
        uvScale, normalStrength, specularIntensity,
        ambientLevel, diffuseLevel,
        tintR, tintG, tintB
    };
    PbrTextureData customTex;
    if (loadMaterialFromBytes(blobData, blobSize, customTex, customDef)) {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        impl_->customThemeLoaded = true;
        impl_->customTexture = std::move(customTex);
        impl_->customDef = customDef;
        impl_->cachedTheme = static_cast<TableThemeId>(-1);
        impl_->cachedCustom = false;
        impl_->cachedW = 0;
        impl_->cachedH = 0;
        impl_->cachedBakedRgba.clear();
        return true;
    }
    return false;
}

bool ProceduralBackgroundSystem::loadProceduralTheme(
    const std::string& scriptPathOrSource,
    const ProceduralBakeOptions& options
) {
    if (!impl_) return false;

    std::string content = scriptPathOrSource;
    std::error_code ec;
    if (std::filesystem::exists(scriptPathOrSource, ec)) {
        readTextFile(scriptPathOrSource, content);
    } else {
        std::string resolved = resolveProceduralScriptPath(scriptPathOrSource);
        if (!resolved.empty() && std::filesystem::exists(resolved, ec)) {
            readTextFile(resolved, content);
        }
    }

    if (hasCanvasBackground(content)) {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        impl_->customThemeLoaded = true;
        impl_->isCanvasTheme = true;
        impl_->canvasThemeSource = std::move(content);
        impl_->customTexture = PbrTextureData{};
        impl_->invalidate();
        std::cout << "[SolEngine] Successfully loaded canvas background theme script\n";
        return true;
    }

    ProceduralPbrMaps maps;
    if (!bakeProceduralPbrBundle(scriptPathOrSource, maps, options)) {
        std::cerr << "[SolEngine] Failed to synthesize procedural theme: " << maps.errorMessage << "\n";
        return false;
    }

    PbrTextureData customTex;
    customTex.width = maps.width;
    customTex.height = maps.height;
    customTex.color = std::move(maps.colorRgb);
    customTex.normal = std::move(maps.normalRgb);
    customTex.orm = std::move(maps.orm);
    customTex.loaded = true;

    PbrMaterialDef customDef{
        "ProceduralTheme", "procedural",
        2.0f, 1.35f, 0.65f, 0.22f, 0.88f,
        1.0f, 1.0f, 1.0f
    };

    {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        impl_->customThemeLoaded = true;
        impl_->isCanvasTheme = false;
        impl_->canvasThemeSource.clear();
        impl_->customTexture = std::move(customTex);
        impl_->customDef = customDef;
        impl_->cachedTheme = static_cast<TableThemeId>(-1);
        impl_->cachedCustom = false;
        impl_->cachedW = 0;
        impl_->cachedH = 0;
        impl_->cachedBakedRgba.clear();
    }
    std::cout << "[SolEngine] Successfully loaded procedural theme ("
              << impl_->customTexture.width << "x" << impl_->customTexture.height << ")\n";
    return true;
}

bool ProceduralBackgroundSystem::loadCustomEatsTheme(const std::string& eatsFilePath) {
    if (!impl_) return false;
    std::string resolved = resolveProceduralScriptPath(eatsFilePath);
    if (resolved.empty()) resolved = resolveThemePath(eatsFilePath);
    if (resolved.empty()) resolved = eatsFilePath;

    std::string lower = resolved;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));
    if (lower.ends_with(".png") || lower.ends_with(".jpg") || lower.ends_with(".jpeg") || lower.ends_with(".webp")) {
        std::ifstream f(resolved, std::ios::binary);
        if (f.is_open()) {
            std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return loadCustomThemeFromMemory(buf.data(), buf.size());
        }
    }

    if (isProceduralScript(resolved)) {
        return loadProceduralTheme(resolved);
    }

    PbrMaterialDef customDef{
        "CustomMaterial", "custom", 2.0f, 1.35f, 0.65f, 0.22f, 0.88f, 1.0f, 1.0f, 1.0f
    };
    PbrTextureData customTex;
    if (loadMaterialFromEats(resolved, customTex, customDef)) {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        impl_->customThemeLoaded = true;
        impl_->isCanvasTheme = false;
        impl_->canvasThemeSource.clear();
        impl_->customTexture = std::move(customTex);
        impl_->customDef = customDef;
        impl_->cachedTheme = static_cast<TableThemeId>(-1);
        impl_->cachedCustom = false;
        impl_->cachedW = 0;
        impl_->cachedH = 0;
        impl_->cachedBakedRgba.clear();
        return true;
    }
    return false;
}

void ProceduralBackgroundSystem::clearCustomEatsTheme() noexcept {
    if (impl_) {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        impl_->customThemeLoaded = false;
        impl_->isCanvasTheme = false;
        impl_->canvasThemeSource.clear();
        impl_->customTexture = PbrTextureData{};
        impl_->cachedTheme = static_cast<TableThemeId>(-1);
        impl_->cachedCustom = false;
        impl_->cachedW = 0;
        impl_->cachedH = 0;
        impl_->cachedBakedRgba.clear();
    }
}

bool ProceduralBackgroundSystem::hasCustomEatsTheme() const noexcept {
    if (!impl_) return false;
    std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
    return impl_->customThemeLoaded;
}

void ProceduralBackgroundSystem::renderBackground(
    eatsbits::ui::BatchRenderer2D& r,
    float x, float y, float w, float h,
    TableThemeId theme,
    const eatsbits::ui::LightSource2D& light,
    uint32_t seed
) {
    if (w <= 0.0f || h <= 0.0f) return;

    if (theme == TableThemeId::PastelTan) {
        // Pure solid pastel tan background: zero baking, instant clear & draw.
        // Physical radial illumination & natural vignette are applied dynamically by the 3D scene light source.
        r.drawRect(x, y, w, h, 0.83f, 0.77f, 0.69f, 1.0f);
        r.drawRectVignette(x, y, w, h, 0.0f, 0.15f, 0.68f, 0.61f, 0.52f);
        return;
    }

    if (impl_ && impl_->pbrEnabled) {
        std::lock_guard<std::recursive_mutex> lock(impl_->cacheMutex);
        // 1. Check if custom user-loaded .eats theme is active
        if (impl_->customThemeLoaded) {
            int bakeW = static_cast<int>(std::ceil(w));
            int bakeH = static_cast<int>(std::ceil(h));

            if (impl_->isCanvasTheme && !impl_->canvasThemeSource.empty()) {
                if (!impl_->cachedCustom ||
                    impl_->cachedW != bakeW ||
                    impl_->cachedH != bakeH ||
                    impl_->cachedBakedRgba.empty()) {
                    if (bakeCanvasSurface(impl_->canvasThemeSource, x, y, w, h, light, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
                        impl_->cachedCustom = true;
                        impl_->cachedTheme = static_cast<TableThemeId>(-1);
                    }
                }

                if (!impl_->cachedBakedRgba.empty() && impl_->cachedW > 0 && impl_->cachedH > 0) {
                    r.drawRgbaBitmap(x, y, w, h, impl_->cachedBakedRgba.data(), impl_->cachedW, impl_->cachedH, 1.0f);
                    return;
                }
            } else if (impl_->customTexture.loaded) {
                // Bake ONCE for the initial render or viewport resize:
                if (!impl_->cachedCustom ||
                    impl_->cachedW != bakeW ||
                    impl_->cachedH != bakeH ||
                    impl_->cachedBakedRgba.empty()) {
                    if (bakePbrSurface(impl_->customTexture, impl_->customDef, x, y, w, h, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
                        impl_->cachedCustom = true;
                        impl_->cachedTheme = static_cast<TableThemeId>(-1);
                    }
                }

                if (!impl_->cachedBakedRgba.empty() && impl_->cachedW > 0 && impl_->cachedH > 0) {
                    r.drawRgbaBitmap(x, y, w, h, impl_->cachedBakedRgba.data(), impl_->cachedW, impl_->cachedH, 1.0f);
                    return;
                }
            }
        }

        // 2. Built-in theme: single-file .eats package or procedural synthesis
        const PbrMaterialDef* def = getMaterialDefForTheme(theme);
        if (def && impl_->ensureMaterial(theme, *def)) {
            int bakeW = static_cast<int>(std::ceil(w));
            int bakeH = static_cast<int>(std::ceil(h));

            auto canvasIt = impl_->themeCanvasSources.find(theme);
            if (canvasIt != impl_->themeCanvasSources.end()) {
                if (impl_->cachedCustom ||
                    impl_->cachedTheme != theme ||
                    impl_->cachedW != bakeW ||
                    impl_->cachedH != bakeH ||
                    impl_->cachedBakedRgba.empty()) {
                    if (bakeCanvasSurface(canvasIt->second, x, y, w, h, light, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
                        impl_->cachedCustom = false;
                        impl_->cachedTheme = theme;
                    }
                }

                if (!impl_->cachedBakedRgba.empty() && impl_->cachedW > 0 && impl_->cachedH > 0) {
                    r.drawRgbaBitmap(x, y, w, h, impl_->cachedBakedRgba.data(), impl_->cachedW, impl_->cachedH, 1.0f);
                    return;
                }
            } else {
                const auto& tex = impl_->materials[theme];
                const auto& activeDef = impl_->materialDefs[theme];

                // Bake ONCE for the initial render or viewport resize:
                if (impl_->cachedCustom ||
                    impl_->cachedTheme != theme ||
                    impl_->cachedW != bakeW ||
                    impl_->cachedH != bakeH ||
                    impl_->cachedBakedRgba.empty()) {
                    if (bakePbrSurface(tex, activeDef, x, y, w, h, impl_->cachedBakedRgba, impl_->cachedW, impl_->cachedH)) {
                        impl_->cachedCustom = false;
                        impl_->cachedTheme = theme;
                    }
                }

                if (!impl_->cachedBakedRgba.empty() && impl_->cachedW > 0 && impl_->cachedH > 0) {
                    r.drawRgbaBitmap(x, y, w, h, impl_->cachedBakedRgba.data(), impl_->cachedW, impl_->cachedH, 1.0f);
                    return;
                }
            }
        }
    }

    renderVectorFallback(r, x, y, w, h, theme, light, seed);
}

} // namespace solengine
