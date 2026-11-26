// SolEngine Brand Asset Generator (solbrand)
// Standalone CLI tool for generating YouTube channel headers (2560x1440)
// and channel icons (800x800) directly from a REAL in-game SolEngine render
// using your currently configured in-game settings (deck, background, post-FX,
// lighting, shadow gobo, etc.), composited with SolEngine's live-lit logo and
// "Procedural Solitaire Sandbox" badge.
//
// Zero runtime bloat in solengine or solengine_web.

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/glyph/vector_font.hpp"
#include "eatstextures/png_io.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/dynamic_light.hpp"
#include "solengine/game_definition.hpp"
#include "solengine/settings_manager.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/ui/clockwork_theme.hpp"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

struct BrandOptions {
    std::string mode{"banner"}; // "banner", "icon", "all"
    std::string outputPath;
    std::string screenshotPath; // Optional: composite over an existing screenshot instead of live capture
    std::string gameQuery;      // Optional: game id, display name, variant, or .eats path (default: current setting)
    std::optional<uint32_t> seedOverride;
    std::string slogan{"PROCEDURAL SOLITAIRE SANDBOX"};
    std::string url{"SOLITAIREENGINE.ORG"};
    std::optional<solengine::LightingPreset> presetOverride;
    std::optional<solengine::LightAngle> lightAngleOverride;
    std::optional<solengine::ShadowGobo> goboOverride;
    std::optional<float> goboSoftnessOverride;
    bool listGames{false};
    bool showGuides{false};
    float plaqueOpacity{80.0f}; // 0..100 percent (0 = no plaque, 80 = default dark glass)
    int jpegQuality{94};        // 1..100 for .jpg/.jpeg outputs
};

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string normalizeKey(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

solengine::LightAngle parseLightAngle(const std::string& raw) {
    const std::string s = toLower(raw);
    if (s == "overhead" || s == "top") return solengine::LightAngle::Overhead;
    if (s == "topright" || s == "tr") return solengine::LightAngle::TopRight;
    if (s == "lowleft" || s == "ll") return solengine::LightAngle::LowLeft;
    return solengine::LightAngle::TopLeft;
}

solengine::LightingPreset parseLightingPreset(const std::string& raw) {
    const std::string s = toLower(raw);
    if (s == "steady" || s == "studio") return solengine::LightingPreset::StudioSteady;
    if (s == "chandelier" || s == "gentle") return solengine::LightingPreset::GentleChandelier;
    if (s == "candle" || s == "candlelight") return solengine::LightingPreset::Candlelight;
    if (s == "haunted" || s == "manor") return solengine::LightingPreset::HauntedManor;
    if (s == "cyberpunk" || s == "neon") return solengine::LightingPreset::CyberpunkNeon;
    if (s == "morning" || s == "window") return solengine::LightingPreset::MorningWindow;
    if (s == "evening") return solengine::LightingPreset::Evening;
    if (s == "night" || s == "darknight") return solengine::LightingPreset::DarkNight;
    return solengine::LightingPreset::GoldenHour;
}

solengine::ShadowGobo parseGobo(const std::string& raw) {
    const std::string s = toLower(raw);
    if (s == "none" || s == "off") return solengine::ShadowGobo::None;
    if (s == "window" || s == "windowframe" || s == "mullions") return solengine::ShadowGobo::WindowFrame;
    if (s == "canopy" || s == "tree" || s == "treecanopy" || s == "foliage" || s == "woodland") return solengine::ShadowGobo::TreeCanopy;
    return solengine::ShadowGobo::VenetianBlinds;
}

std::optional<solengine::GameVariant> parseBuiltinVariant(const std::string& query) {
    const std::string k = normalizeKey(query);
    if (k == "klondike" || k == "klondike1" || k == "klondikedraw1") return solengine::GameVariant::KlondikeDraw1;
    if (k == "klondike3" || k == "klondikedraw3") return solengine::GameVariant::KlondikeDraw3;
    if (k == "freecell") return solengine::GameVariant::FreeCell;
    if (k == "spider" || k == "spider1" || k == "spider1suit") return solengine::GameVariant::Spider1Suit;
    if (k == "spider2" || k == "spider2suits") return solengine::GameVariant::Spider2Suits;
    if (k == "spider4" || k == "spider4suits") return solengine::GameVariant::Spider4Suits;
    return std::nullopt;
}

const solengine::SolitaireGameDef* findGameByQuery(
    solengine::GameRegistry& reg,
    const std::string& query,
    solengine::SolitaireGameDef& loadedOut
) {
    if (const auto* g = reg.findById(query)) return g;
    if (const auto* g = reg.findByName(query)) return g;

    const std::string qNorm = normalizeKey(query);
    for (const auto& g : reg.allGames()) {
        if (normalizeKey(g.id) == qNorm || normalizeKey(g.name) == qNorm) {
            return &g;
        }
    }
    // Partial substring fallback (e.g. "black hole", "clockwork")
    for (const auto& g : reg.allGames()) {
        if (normalizeKey(g.name).find(qNorm) != std::string::npos ||
            normalizeKey(g.id).find(qNorm) != std::string::npos) {
            return &g;
        }
    }
    if (reg.loadGameFile(query, loadedOut)) {
        return &loadedOut;
    }
    return nullptr;
}

void printUsage() {
    std::cout
        << "SolEngine Brand Asset Generator (solbrand)\n"
        << "Captures a real in-game WebGPU frame using your current in-game settings\n"
        << "(deck, background, post-FX, lighting, shadow gobo) and composites the\n"
        << "SolEngine logo + \"Procedural Solitaire Sandbox\" badge.\n\n"
        << "Usage:\n"
        << "  solbrand [options]\n\n"
        << "Options:\n"
        << "  --mode <banner|icon|all>      Asset to generate (default: banner)\n"
        << "  -o, --output <path>           Output path (.jpg/.jpeg or .png; default: .jpg for banner, .png for icon)\n"
        << "  --quality <1..100>            JPEG quality for .jpg outputs (default: 94)\n"
        << "  --game <name|id|path>         Game to render (e.g. \"Black Hole\", \"Clockwork Dial\",\n"
        << "                                klondike, freecell, spider1, scoundrel)\n"
        << "  --list-games                  List all available games and exit\n"
        << "  --seed <N>                    Override deal seed (default: current in-game seed)\n"
        << "  --screenshot <path>           Composite onto an existing PNG/JPG instead of live capture\n"
        << "  --slogan <text>               Primary line (default: \"PROCEDURAL SOLITAIRE SANDBOX\")\n"
        << "  --url <text>                  Secondary line (default: \"SOLITAIREENGINE.ORG\", empty to hide)\n"
        << "  --plaque <0..100>             Plaque background opacity % (default: 80; 0 = no plaque)\n"
        << "  --no-plaque                   Shorthand for --plaque 0\n"
        << "  --guides                      Draw YouTube mobile/desktop safe-zone guide lines\n"
        << "\nOptional lighting overrides (by default, uses your current in-game settings):\n"
        << "  --preset <preset>             steady | chandelier | candlelight | golden | morning | ...\n"
        << "  --light-angle <angle>         overhead | topleft | topright | lowleft\n"
        << "  --gobo <venetian|window|canopy|none> Override shadow gobo\n"
        << "  --gobo-softness <0..1>        Override shadow gobo softness\n"
        << "  -h, --help                    Show this help message\n";
}

// Convert FilamentBatchBackend's packed 0xAARRGGBB framebuffer to packed 3-channel RGB8 bytes.
void unpackArgbToRgb(const uint32_t* argb, uint32_t w, uint32_t h, std::vector<uint8_t>& outRgb) {
    const size_t pixelCount = static_cast<size_t>(w) * h;
    outRgb.resize(pixelCount * 3u);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint32_t px = argb[i];
        outRgb[i * 3u + 0u] = static_cast<uint8_t>((px >> 16) & 0xFFu);
        outRgb[i * 3u + 1u] = static_cast<uint8_t>((px >> 8) & 0xFFu);
        outRgb[i * 3u + 2u] = static_cast<uint8_t>(px & 0xFFu);
    }
}

bool writeCompressedRgbImage(const std::string& outPath, uint32_t w, uint32_t h,
                             const std::vector<uint8_t>& rgb, int jpegQuality) {
    const std::string ext = toLower(std::filesystem::path(outPath).extension().string());
    int ok = 0;
    if (ext == ".jpg" || ext == ".jpeg") {
        ok = stbi_write_jpg(outPath.c_str(), static_cast<int>(w), static_cast<int>(h),
                            3, rgb.data(), std::clamp(jpegQuality, 1, 100));
    } else {
        stbi_write_png_compression_level = 9;
        ok = stbi_write_png(outPath.c_str(), static_cast<int>(w), static_cast<int>(h),
                            3, rgb.data(), static_cast<int>(w * 3u));
    }
    if (!ok) {
        std::cerr << "[solbrand] Failed to write image: " << outPath << "\n";
        return false;
    }
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(outPath, ec);
    if (!ec) {
        const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        std::cout << "[solbrand] Wrote " << outPath << " (" << w << "x" << h << ", "
                  << std::fixed << std::setprecision(2) << mb << " MB)\n";
        if (bytes > 6u * 1024u * 1024u) {
            std::cout << "[solbrand] Note: file exceeds YouTube's 6 MB limit — use a .jpg output path for ~1 MB.\n";
        }
    }
    return true;
}

// Very gentle edge vignette so the compact center badge reads crisply while preserving the real board.
void applySubtleBannerGrade(uint32_t w, uint32_t h, std::vector<uint8_t>& rgba) {
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    const float cx = fw * 0.5f;
    const float cy = fh * 0.5f;

    for (uint32_t y = 0; y < h; ++y) {
        const float ny = (static_cast<float>(y) - cy) / (fh * 0.5f);
        for (uint32_t x = 0; x < w; ++x) {
            const float nx = (static_cast<float>(x) - cx) / (fw * 0.5f);
            const float edgeVig = std::clamp(1.0f - std::hypot(nx * 0.85f, ny * 0.95f) * 0.22f, 0.66f, 1.0f);

            const size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            rgba[idx + 0u] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba[idx + 0u]) * edgeVig, 0.0f, 255.0f));
            rgba[idx + 1u] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba[idx + 1u]) * edgeVig, 0.0f, 255.0f));
            rgba[idx + 2u] = static_cast<uint8_t>(std::clamp(static_cast<float>(rgba[idx + 2u]) * edgeVig, 0.0f, 255.0f));
            rgba[idx + 3u] = 255u;
        }
    }
}

// Capture a REAL in-game WebGPU board at (w x h) using the user's current in-game settings.
bool captureInGameBoardFrame(
    const BrandOptions& opts,
    uint32_t w, uint32_t h,
    std::vector<uint8_t>& outRgba,
    solengine::AppSettings& outEffectiveSettings
) {
    auto& settingsMgr = solengine::SettingsManager::instance();
    settingsMgr.load();
    auto& s = settingsMgr.settings();

    // Apply ONLY explicit CLI overrides; otherwise keep 100% of current in-game settings!
    if (opts.presetOverride) s.lightingPreset = *opts.presetOverride;
    if (opts.lightAngleOverride) s.lightAngle = *opts.lightAngleOverride;
    if (opts.goboOverride) s.shadowGobo = *opts.goboOverride;
    if (opts.goboSoftnessOverride) s.goboSoftness = *opts.goboSoftnessOverride;

    auto board = std::make_unique<solengine::SolitaireBoard>();
    board->setTableTheme(s.tableTheme);
    board->setCardDeviation(s.cardDeviation);
    board->setLayoutMode(s.layoutMode);
    board->setStockPosition(s.stockPosition);

    auto& reg = solengine::GameRegistry::instance();
    reg.scanDirectory("games");

    const uint32_t baseSeed = opts.seedOverride.value_or(s.lastSeed);
    std::string resolvedGameId;

    if (!opts.gameQuery.empty()) {
        solengine::SolitaireGameDef loadedDef;
        if (const auto* gdef = findGameByQuery(reg, opts.gameQuery, loadedDef)) {
            const uint32_t dealSeed = (!opts.seedOverride && gdef->demoSeed > 0) ? gdef->demoSeed : baseSeed;
            board->newDeal(dealSeed, *gdef);
            resolvedGameId = gdef->id;
            std::cout << "[solbrand] Game: " << gdef->name << " (" << gdef->id << "), seed #" << dealSeed << "\n";
        } else if (auto builtin = parseBuiltinVariant(opts.gameQuery)) {
            board->newDeal(baseSeed, *builtin);
            resolvedGameId = board->activeGameDef().id;
            std::cout << "[solbrand] Game: " << solengine::SolitaireBoard::variantName(*builtin)
                      << ", seed #" << baseSeed << "\n";
        } else {
            std::cerr << "[solbrand] Error: unknown game '" << opts.gameQuery
                      << "'. Run with --list-games to see available games.\n";
            return false;
        }
    } else {
        board->newDeal(baseSeed, s.variant);
        resolvedGameId = board->activeGameDef().id;
        std::cout << "[solbrand] Game (from current settings): "
                  << solengine::SolitaireBoard::variantName(s.variant)
                  << ", seed #" << baseSeed << "\n";
    }

    // Resolve effective lighting/profile for this game so the logo overlay matches the board
    outEffectiveSettings = !resolvedGameId.empty() ? settingsMgr.resolveForGame(resolvedGameId) : s;
    if (opts.presetOverride) outEffectiveSettings.lightingPreset = *opts.presetOverride;
    if (opts.lightAngleOverride) outEffectiveSettings.lightAngle = *opts.lightAngleOverride;

    board->computeLayout(static_cast<float>(w), static_cast<float>(h));

    // Audio disabled for clean headless capture
    auto foley = std::make_unique<solengine::AudioFoleyEngine>();
    foley->init(false);

    solengine::DirectorOptions dOpts;
    dOpts.hideHud = true;
    dOpts.skipSessionRestore = true;
    dOpts.exportSingleFrameRgba = &outRgba;

    std::cout << "[solbrand] Capturing live WebGPU frame (" << w << "x" << h << ")...\n";
    int rc = solengine::TableRenderer::runInteractiveWindow(*board, *foley, w, h, s.aaMode, false, dOpts);
    return (rc == 0) && (outRgba.size() == static_cast<size_t>(w) * h * 4u);
}

bool renderYouTubeBanner(const BrandOptions& opts, const std::string& outPath) {
    constexpr uint32_t kW = 2560;
    constexpr uint32_t kH = 1440;

    solengine::AppSettings effectiveSettings;
    auto& settingsMgr = solengine::SettingsManager::instance();
    settingsMgr.load();
    effectiveSettings = settingsMgr.settings();
    if (opts.presetOverride) effectiveSettings.lightingPreset = *opts.presetOverride;
    if (opts.lightAngleOverride) effectiveSettings.lightAngle = *opts.lightAngleOverride;

    // 1. Obtain the real in-game board RGBA image
    std::vector<uint8_t> boardRgba;
    if (!opts.screenshotPath.empty()) {
        uint32_t srcW = 0, srcH = 0;
        std::vector<uint8_t> raw;
        if (!eatstextures::readImageRGBA(opts.screenshotPath, raw, srcW, srcH)) {
            std::cerr << "[solbrand] Failed to read screenshot: " << opts.screenshotPath << "\n";
            return false;
        }
        boardRgba = eatstextures::resizeImageRGBA(raw, srcW, srcH, kW, kH);
        std::cout << "[solbrand] Loaded screenshot " << opts.screenshotPath
                  << " (" << srcW << "x" << srcH << " -> " << kW << "x" << kH << ")\n";
    } else {
        if (!captureInGameBoardFrame(opts, kW, kH, boardRgba, effectiveSettings)) {
            std::cerr << "[solbrand] Failed to capture in-game WebGPU frame.\n";
            return false;
        }
    }

    applySubtleBannerGrade(kW, kH, boardRgba);

    // 2. Composite compact SolEngine Logo + "Procedural Solitaire Sandbox" badge via pure-CPU BatchRenderer2D
    solengine::DynamicLightController lightCtrl(effectiveSettings.lightingPreset);
    lightCtrl.setAngle(effectiveSettings.lightAngle);
    const auto light = lightCtrl.getSteadyLight(static_cast<float>(kW), static_cast<float>(kH));
    const auto lCol = lightCtrl.getLightColor();
    const auto aCol = lightCtrl.getAmbientColor();

    eatsbits::ui::BatchRenderer2D r;
    if (!r.initialize(nullptr, kW, kH, eatsbits::ui::RenderBackendType::Filament)) {
        std::cerr << "[solbrand] Failed to initialize CPU compositor.\n";
        return false;
    }
    r.setDirectPresent(false);
    r.setAntiAliasingMode(2);

    r.beginFrame(static_cast<float>(kW), static_cast<float>(kH));
    r.setGlobalLight(light, lCol[0], lCol[1], lCol[2], aCol[0], aCol[1], aCol[2], 0.0f, effectiveSettings.lightStrength);

    // Real in-game board background
    r.drawRgbaBitmap(0.0f, 0.0f, static_cast<float>(kW), static_cast<float>(kH),
                     boardRgba.data(), static_cast<int>(kW), static_cast<int>(kH), 1.0f);

    const float cx = static_cast<float>(kW) * 0.5f;
    const float cy = static_cast<float>(kH) * 0.5f;
    const float stripTop = cy - 211.5f;

    // 3. Compact Half-Size Badge centered inside YouTube's 1546x423 Universal Safe Zone
    //    [Logo]  |  PROCEDURAL SOLITAIRE SANDBOX
    //               SOLITAIREENGINE.ORG
    const float logoRadius = 72.0f;
    const float sloganSize = 36.0f;
    const float urlSize = 24.0f;

    const float sloganW = eatsbits::ui::glyph::getVectorTextWidth(sloganSize, opts.slogan);
    const float urlW = opts.url.empty() ? 0.0f : eatsbits::ui::glyph::getVectorTextWidth(urlSize, opts.url);
    const float textBlockW = std::max(sloganW, urlW);

    const float gapLogoToRule = 28.0f;
    const float gapRuleToText = 26.0f;
    const float totalGroupW = (logoRadius * 2.0f) + gapLogoToRule + gapRuleToText + textBlockW;

    const float groupLeft = cx - totalGroupW * 0.5f;
    const float logoCx = groupLeft + logoRadius;
    const float logoCy = cy;
    const float ruleX = groupLeft + logoRadius * 2.0f + gapLogoToRule;
    const float textX = ruleX + gapRuleToText;

    const float plaqueAlpha = std::clamp(opts.plaqueOpacity, 0.0f, 100.0f) / 100.0f;
    if (plaqueAlpha > 0.005f) {
        // Compact dark obsidian-glass plaque with configurable opacity
        const float plaquePadX = 34.0f;
        const float plaqueH = 180.0f;
        const float plaqueRad = 18.0f;
        r.drawRoundedRect(
            groupLeft - plaquePadX + 6.0f, cy - plaqueH * 0.5f + 8.0f,
            totalGroupW + plaquePadX * 2.0f, plaqueH, plaqueRad,
            0.0f, 0.0f, 0.0f, 0.42f * plaqueAlpha, 10
        );
        r.drawRoundedRectGradient(
            groupLeft - plaquePadX, cy - plaqueH * 0.5f,
            totalGroupW + plaquePadX * 2.0f, plaqueH, plaqueRad,
            0.06f, 0.08f, 0.07f,
            0.04f, 0.05f, 0.04f,
            plaqueAlpha, 10
        );
        const float borderAlpha = std::min(0.62f, plaqueAlpha * 0.85f);
        r.drawRoundedRectOutline(
            groupLeft - plaquePadX, cy - plaqueH * 0.5f,
            totalGroupW + plaquePadX * 2.0f, plaqueH, plaqueRad,
            0.84f, 0.67f, 0.28f, borderAlpha, 1.8f, 10
        );
    }

    // Live-lit SolEngine Clockwork Logo (vector gear silhouette shadow only — clean gaps between cogs!)
    solengine::clockwork_ui::ClockworkLogoConfig logoCfg;
    logoCfg.animated = false;
    logoCfg.useSceneLight = true;
    logoCfg.alpha = 1.0f;
    solengine::clockwork_ui::drawClockworkLogo(r, logoCx, logoCy, logoRadius, 0.0f, logoCfg);

    // Outline strength smoothly increases as plaque opacity decreases
    const float outlineStrength = std::clamp((0.65f - plaqueAlpha) / 0.65f, 0.0f, 1.0f);

    // Compact vertical brass divider (with crisp dark outline + drop shadow at low plaque opacity)
    if (outlineStrength > 0.01f) {
        r.drawLine(ruleX + 3.0f, cy - 52.0f + 4.0f, ruleX + 3.0f, cy + 52.0f + 4.0f,
                   0.0f, 0.0f, 0.0f, 0.55f * outlineStrength, 5.5f);
        r.drawLine(ruleX, cy - 52.0f, ruleX, cy + 52.0f,
                   0.05f, 0.05f, 0.05f, 0.85f * outlineStrength, 6.0f);
        r.drawCircle(ruleX, cy - 52.0f, 5.2f, 0.05f, 0.05f, 0.05f, 0.85f * outlineStrength, 10);
        r.drawCircle(ruleX, cy + 52.0f, 5.2f, 0.05f, 0.05f, 0.05f, 0.85f * outlineStrength, 10);
    }
    r.drawLine(ruleX, cy - 52.0f, ruleX, cy + 52.0f, 0.88f, 0.70f, 0.28f, 0.88f, 2.4f);
    r.drawCircle(ruleX, cy - 52.0f, 3.2f, 1.00f, 0.90f, 0.64f, 0.95f, 10);
    r.drawCircle(ruleX, cy + 52.0f, 3.2f, 1.00f, 0.90f, 0.64f, 0.95f, 10);

    // Two-line typography stack: "PROCEDURAL SOLITAIRE SANDBOX" + "SOLITAIREENGINE.ORG"
    const float lineGap = 18.0f;
    const float stackH = sloganSize + (opts.url.empty() ? 0.0f : (lineGap + urlSize));
    const float sloganY = cy - stackH * 0.5f;
    const float urlY = sloganY + sloganSize + lineGap;

    auto drawOutlinedText = [&](const std::string& text, float x, float y, float size,
                                float tr, float tg, float tb, float ta, float outlinePx) {
        if (outlineStrength > 0.01f) {
            // Soft offset drop shadow
            eatsbits::ui::glyph::drawVectorText(r, text, x + outlinePx * 1.9f, y + outlinePx * 2.2f, size,
                                                0.0f, 0.0f, 0.0f, 0.55f * outlineStrength);
            eatsbits::ui::glyph::drawVectorText(r, text, x + outlinePx * 1.1f, y + outlinePx * 1.3f, size,
                                                0.0f, 0.0f, 0.0f, 0.72f);
            // 8-direction crisp dark outline
            static constexpr float kDirs[8][2] = {
                {-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f},
                {-0.707f, -0.707f}, {0.707f, -0.707f}, {-0.707f, 0.707f}, {0.707f, 0.707f}
            };
            for (const auto& d : kDirs) {
                eatsbits::ui::glyph::drawVectorText(r, text, x + d[0] * outlinePx, y + d[1] * outlinePx, size,
                                                    0.04f, 0.04f, 0.04f, 0.92f * outlineStrength);
            }
        } else {
            eatsbits::ui::glyph::drawVectorText(r, text, x + 2.5f, y + 3.0f, size, 0.0f, 0.0f, 0.0f, 0.68f);
        }
        eatsbits::ui::glyph::drawVectorText(r, text, x, y, size, tr, tg, tb, ta);
    };

    drawOutlinedText(opts.slogan, textX, sloganY, sloganSize, 0.99f, 0.86f, 0.45f, 1.0f, 2.4f);

    if (!opts.url.empty()) {
        drawOutlinedText(opts.url, textX, urlY, urlSize, 0.95f, 0.93f, 0.88f, 0.98f, 1.8f);
    }

    if (opts.showGuides) {
        r.drawRectOutline(1.0f, stripTop, static_cast<float>(kW) - 2.0f, 423.0f, 0.25f, 0.80f, 1.0f, 0.75f, 2.0f);
        r.drawRectOutline(cx - 773.0f, stripTop, 1546.0f, 423.0f, 0.30f, 1.0f, 0.45f, 0.85f, 3.0f);
    }

    r.endFrame();

    const uint32_t* fb = r.getFramebuffer();
    if (!fb) {
        std::cerr << "[solbrand] Framebuffer unavailable.\n";
        r.shutdown();
        return false;
    }

    std::vector<uint8_t> outRgb;
    unpackArgbToRgb(fb, kW, kH, outRgb);
    r.shutdown();

    return writeCompressedRgbImage(outPath, kW, kH, outRgb, opts.jpegQuality);
}

bool renderYouTubeIcon(const BrandOptions& opts, const std::string& outPath) {
    constexpr uint32_t kSize = 800;

    auto& settingsMgr = solengine::SettingsManager::instance();
    settingsMgr.load();
    const auto& s = settingsMgr.settings();

    const auto preset = opts.presetOverride.value_or(s.lightingPreset);
    const auto angle = opts.lightAngleOverride.value_or(s.lightAngle);

    solengine::DynamicLightController lightCtrl(preset);
    lightCtrl.setAngle(angle);
    const auto light = lightCtrl.getSteadyLight(static_cast<float>(kSize), static_cast<float>(kSize));
    const auto lCol = lightCtrl.getLightColor();
    const auto aCol = lightCtrl.getAmbientColor();

    eatsbits::ui::BatchRenderer2D r;
    if (!r.initialize(nullptr, kSize, kSize, eatsbits::ui::RenderBackendType::Filament)) {
        std::cerr << "[solbrand] Failed to initialize CPU compositor.\n";
        return false;
    }
    r.setDirectPresent(false);
    r.setAntiAliasingMode(2);

    r.beginFrame(static_cast<float>(kSize), static_cast<float>(kSize));
    r.setGlobalLight(light, lCol[0], lCol[1], lCol[2], aCol[0], aCol[1], aCol[2], 0.0f, s.lightStrength);

    r.drawRect(0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize),
               13.0f / 255.0f, 20.0f / 255.0f, 14.0f / 255.0f, 1.0f);

    solengine::clockwork_ui::ClockworkLogoConfig logoCfg;
    logoCfg.animated = false;
    logoCfg.useSceneLight = true;
    logoCfg.alpha = 1.0f;
    solengine::clockwork_ui::drawClockworkLogo(r, kSize * 0.5f, kSize * 0.5f, 330.0f, 0.0f, logoCfg);

    r.endFrame();

    const uint32_t* fb = r.getFramebuffer();
    if (!fb) {
        std::cerr << "[solbrand] Framebuffer unavailable.\n";
        r.shutdown();
        return false;
    }

    std::vector<uint8_t> outRgb;
    unpackArgbToRgb(fb, kSize, kSize, outRgb);
    r.shutdown();

    return writeCompressedRgbImage(outPath, kSize, kSize, outRgb, opts.jpegQuality);
}

void printAvailableGames() {
    auto& reg = solengine::GameRegistry::instance();
    reg.scanDirectory("games");
    std::cout << "Built-in variants:\n"
              << "  klondike, klondike3, freecell, spider1, spider2, spider4\n\n"
              << "Eatscript games (games/):\n";
    for (const auto& g : reg.allGames()) {
        std::cout << "  " << g.id << "  —  " << g.name << "\n";
    }
}

} // anonymous namespace

int main(int argc, char** argv) {
    BrandOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "--list-games") {
            opts.listGames = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            opts.mode = toLower(argv[++i]);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            opts.outputPath = argv[++i];
        } else if (arg == "--screenshot" && i + 1 < argc) {
            opts.screenshotPath = argv[++i];
        } else if (arg == "--game" && i + 1 < argc) {
            opts.gameQuery = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            opts.seedOverride = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--gobo" && i + 1 < argc) {
            opts.goboOverride = parseGobo(argv[++i]);
        } else if (arg == "--gobo-softness" && i + 1 < argc) {
            opts.goboSoftnessOverride = std::clamp(std::stof(argv[++i]), 0.05f, 1.0f);
        } else if (arg == "--light-angle" && i + 1 < argc) {
            opts.lightAngleOverride = parseLightAngle(argv[++i]);
        } else if (arg == "--preset" && i + 1 < argc) {
            opts.presetOverride = parseLightingPreset(argv[++i]);
        } else if (arg == "--slogan" && i + 1 < argc) {
            opts.slogan = argv[++i];
        } else if (arg == "--url" && i + 1 < argc) {
            opts.url = argv[++i];
        } else if (arg == "--plaque" && i + 1 < argc) {
            opts.plaqueOpacity = std::clamp(std::stof(argv[++i]), 0.0f, 100.0f);
        } else if (arg == "--quality" && i + 1 < argc) {
            opts.jpegQuality = std::clamp(std::stoi(argv[++i]), 1, 100);
        } else if (arg == "--no-plaque" || arg == "--no-box") {
            opts.plaqueOpacity = 0.0f;
        } else if (arg == "--guides") {
            opts.showGuides = true;
        } else {
            std::cerr << "[solbrand] Unknown argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    if (opts.listGames) {
        printAvailableGames();
        return 0;
    }

    if (opts.mode == "banner") {
        const std::string out = opts.outputPath.empty() ? "web/youtube-banner-2560x1440.jpg" : opts.outputPath;
        return renderYouTubeBanner(opts, out) ? 0 : 1;
    }
    if (opts.mode == "icon") {
        const std::string out = opts.outputPath.empty() ? "web/youtube-icon-800.png" : opts.outputPath;
        return renderYouTubeIcon(opts, out) ? 0 : 1;
    }
    if (opts.mode == "all") {
        const bool ok1 = renderYouTubeBanner(opts, "web/youtube-banner-2560x1440.jpg");
        const bool ok2 = renderYouTubeIcon(opts, "web/youtube-icon-800.png");
        return (ok1 && ok2) ? 0 : 1;
    }

    std::cerr << "[solbrand] Unknown mode '" << opts.mode << "'. Expected banner, icon, or all.\n";
    return 1;
}
