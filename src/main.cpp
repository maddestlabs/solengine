#include "solengine/solitaire_board.hpp"
#include "solengine/audio_foley.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/procedural_background.hpp"
#include "solengine/proctex_api.hpp"
#include "solengine/settings_manager.hpp"
#include "eatstextures/png_io.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <memory>

int main(int argc, char** argv) {
    solengine::SettingsManager::instance().load();
    const auto& loadedSettings = solengine::SettingsManager::instance().settings();

    uint32_t seed = loadedSettings.lastSeed;
    uint32_t width = 1280u;
    uint32_t height = 720u;
    std::string outputPngPath;
    solengine::GameVariant variant = loadedSettings.variant;
    solengine::TableThemeId theme = loadedSettings.tableTheme;
    int effectiveAa = loadedSettings.aaMode;
    float cardDeviation = loadedSettings.cardDeviation;
    std::string bakeScript;
    std::string exportPbrPrefix;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--render-png" || arg == "--snapshot") && i + 1 < argc) {
            outputPngPath = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--freecell") {
            variant = solengine::GameVariant::FreeCell;
        } else if (arg == "--spider" || arg == "--spider-4") {
            variant = solengine::GameVariant::Spider4Suits;
        } else if (arg == "--spider-1") {
            variant = solengine::GameVariant::Spider1Suit;
        } else if (arg == "--spider-2") {
            variant = solengine::GameVariant::Spider2Suits;
        } else if (arg == "--spider-3") {
            variant = solengine::GameVariant::Spider3Suits;
        } else if (arg == "--golf") {
            variant = solengine::GameVariant::Golf;
        } else if (arg == "--theme" && i + 1 < argc) {
            std::string tName = argv[++i];
            std::string lowerT;
            for (char c : tName) lowerT += static_cast<char>(std::tolower(c));
            if (lowerT == "tan" || lowerT == "pasteltan" || lowerT == "pastel" || lowerT == "brown") {
                theme = solengine::TableThemeId::PastelTan;
            } else if (lowerT == "emerald" || lowerT == "lawn" || lowerT == "linen" || lowerT == "green") {
                theme = solengine::TableThemeId::EmeraldLawn;
            } else if (lowerT == "tweed" || lowerT == "hopsack") {
                theme = solengine::TableThemeId::HopsackTweed;
            } else if (lowerT == "denim" || lowerT == "chambray" || lowerT == "blue") {
                theme = solengine::TableThemeId::DenimChambray;
            } else if (lowerT == "jersey" || lowerT == "knit" || lowerT == "striped") {
                theme = solengine::TableThemeId::StripedJersey;
            } else if (lowerT == "sanctum" || lowerT == "onyx" || lowerT == "obsidian") {
                theme = solengine::TableThemeId::SunkenSanctum;
            } else if (lowerT == "shores" || lowerT == "perilous" || lowerT == "ground" || lowerT == "moss") {
                theme = solengine::TableThemeId::PerilousShores;
            } else if (lowerT == "cowhide" || lowerT == "pebbled") {
                theme = solengine::TableThemeId::PebbledCowhide;
            } else if (lowerT == "leather" || lowerT == "nubuck" || lowerT == "distressed") {
                theme = solengine::TableThemeId::LeatherNubuck;
            } else if (lowerT == "paintedwood" || lowerT == "wood" || lowerT == "barnwood") {
                theme = solengine::TableThemeId::PaintedWood;
            } else if (lowerT == "dungeon" || lowerT == "brick" || lowerT == "pixeldungeon") {
                theme = solengine::TableThemeId::PixelDungeon;
            } else if (lowerT == "water" || lowerT == "ocean" || lowerT == "pixelwater") {
                theme = solengine::TableThemeId::PixelWater;
            } else if (!lowerT.empty() && std::isdigit(lowerT[0])) {
                int tIdx = std::clamp(std::stoi(lowerT), 0, static_cast<int>(solengine::TableThemeId::Count) - 1);
                theme = static_cast<solengine::TableThemeId>(tIdx);
            }
        } else if (arg == "--no-pbr") {
            solengine::ProceduralBackgroundSystem::instance().setPbrTexturesEnabled(false);
        } else if (arg == "--pbr") {
            solengine::ProceduralBackgroundSystem::instance().setPbrTexturesEnabled(true);
        } else if ((arg == "--bake-texture" || arg == "--bake-proctex" || arg == "--bake") && i + 1 < argc) {
            bakeScript = argv[++i];
        } else if ((arg == "--export-pbr" || arg == "--pbr-export") && i + 1 < argc) {
            exportPbrPrefix = argv[++i];
        } else if ((arg == "--deviation" || arg == "--skew" || arg == "--jitter") && i + 1 < argc) {
            cardDeviation = std::clamp(std::stof(argv[++i]), 0.0f, 1.0f);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputPngPath = argv[++i];
        }
    }

    // 0. Check for Headless Procedural Texture Baking CLI Command
    if (!bakeScript.empty()) {
        solengine::ProceduralBakeOptions opts;
        opts.width = width;
        opts.height = height;
        opts.seed = seed;
        opts.overrideDimensions = true;
        opts.overrideSeed = true;

        if (!exportPbrPrefix.empty()) {
            std::cout << "[SolEngine] Baking procedural PBR bundle: " << bakeScript << " ("
                      << opts.width << "x" << opts.height << ") -> " << exportPbrPrefix << "_*.png ...\n";
            if (solengine::exportProceduralPbrToDisk(bakeScript, exportPbrPrefix, opts)) {
                std::cout << "[SolEngine] Successfully exported PBR maps to " << exportPbrPrefix << "_*.png\n";
                return 0;
            }
            std::cerr << "[SolEngine] Failed to export procedural PBR maps.\n";
            return 1;
        } else {
            std::string outPath = !outputPngPath.empty() ? outputPngPath : "baked_texture.png";
            std::cout << "[SolEngine] Baking procedural texture: " << bakeScript << " ("
                      << opts.width << "x" << opts.height << ") -> " << outPath << " ...\n";
            std::vector<uint8_t> rgba;
            uint32_t outW = 0, outH = 0;
            if (solengine::bakeProceduralTexture(bakeScript, rgba, outW, outH, opts)) {
                if (eatstextures::writePngRGBA(outPath, outW, outH, rgba.data())) {
                    std::cout << "[SolEngine] Successfully wrote " << outPath << " (" << outW << "x" << outH << ")\n";
                    return 0;
                }
            }
            std::cerr << "[SolEngine] Failed to bake procedural texture.\n";
            return 1;
        }
    }

    auto board = std::make_unique<solengine::SolitaireBoard>();
    board->setTableTheme(theme);
    board->setCardDeviation(cardDeviation);
    board->setLayoutMode(loadedSettings.layoutMode);
    board->setStockPosition(loadedSettings.stockPosition);
    board->newDeal(seed, variant);
    board->computeLayout(static_cast<float>(width), static_cast<float>(height));

    std::cout << "[SolEngine] Initialized Deal #" << board->seed()
              << " | Variant: " << solengine::SolitaireBoard::variantName(board->variant())
              << " | Theme: " << solengine::SolitaireBoard::themeName(board->tableTheme())
              << "\n";

    if (!outputPngPath.empty()) {
        std::cout << "[SolEngine] Rendering procedural table snapshot (" << width << "x" << height
                  << ") -> " << outputPngPath << " ...\n";
        auto& cache = solengine::CardTextureCache::instance();
        cache.setCornerStyle(loadedSettings.cardCorners);
        cache.loadDeckTheme(loadedSettings.deckTheme);
        cache.applyConditioningAndSync(loadedSettings.cardWear, loadedSettings.cardStock);

        if (solengine::TableRenderer::renderToPng(*board, width, height, outputPngPath, effectiveAa)) {
            std::cout << "[SolEngine] Successfully wrote " << outputPngPath << "\n";
            return 0;
        }
        std::cerr << "[SolEngine] Failed to write PNG snapshot.\n";
        return 1;
    }

    auto foley = std::make_unique<solengine::AudioFoleyEngine>();
    foley->init(true);
    return solengine::TableRenderer::runInteractiveWindow(*board, *foley, width, height, effectiveAa);
}
