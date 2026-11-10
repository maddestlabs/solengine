#include "solengine/settings_manager.hpp"
#include "solengine/json_parser.hpp"
#include "solengine/solitaire_board.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace solengine {

SettingsManager& SettingsManager::instance() {
    static SettingsManager sInstance;
    return sInstance;
}

std::string SettingsManager::getSettingsFilePath() const {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData) {
        std::filesystem::path p(appData);
        std::filesystem::path newP = p / "SolEngine" / "settings.json";
        std::filesystem::path oldP = p / "SolEnshaded" / "settings.json";
        if (!std::filesystem::exists(newP) && std::filesystem::exists(oldP)) {
            return oldP.string();
        }
        return newP.string();
    }
#elif !defined(__EMSCRIPTEN__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        std::filesystem::path p(home);
        std::filesystem::path newP = p / ".config" / "solengine" / "settings.json";
        std::filesystem::path oldP = p / ".config" / "solenshaded" / "settings.json";
        if (!std::filesystem::exists(newP) && std::filesystem::exists(oldP)) {
            return oldP.string();
        }
        return newP.string();
    }
#endif
    return "settings.json";
}

bool SettingsManager::load() {
    std::string content;
#if defined(__EMSCRIPTEN__)
    char* jsVal = reinterpret_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        try {
            var s = localStorage.getItem("solengine_settings") || localStorage.getItem("solenshaded_settings");
            if (!s) return 0;
            var len = lengthBytesUTF8(s) + 1;
            var buf = _malloc(len);
            stringToUTF8(s, buf, len);
            return buf;
        } catch (e) {
            return 0;
        }
    }));
    if (jsVal) {
        content = jsVal;
        free(jsVal);
    }
#else
    std::string path = getSettingsFilePath();
    try {
        std::ifstream ifs(path);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            content = ss.str();
        }
    } catch (...) {}
#endif

    if (content.empty()) {
        return false;
    }

    try {
        json::Value root = json::parse(content);
        if (!root.isObject()) return false;

        if (root.contains("game") && root["game"].isObject()) {
            const auto& g = root["game"];
            settings_.variant = static_cast<GameVariant>(std::clamp(g["variant"].asInt(0), 0, 7));
            settings_.randomizeOnNewDeal = g["randomizeOnNewDeal"].asBool(true);
            settings_.guaranteeSolvable = g["guaranteeSolvable"].asBool(false);
            settings_.lastSeed = g["lastSeed"].asUInt(2026u);
            settings_.autoFinish = g["autoFinish"].asBool(true);
        }

        if (root.contains("appearance") && root["appearance"].isObject()) {
            const auto& a = root["appearance"];
            settings_.tableTheme = static_cast<TableThemeId>(std::clamp(a["tableTheme"].asInt(0), 0, static_cast<int>(TableThemeId::Count) - 1));
            if (a.contains("tableThemeSlug") && a["tableThemeSlug"].isString()) {
                settings_.tableThemeSlug = a["tableThemeSlug"].asString();
            }
            settings_.deckTheme = static_cast<DeckThemeId>(std::clamp(a["deckTheme"].asInt(0), 0, static_cast<int>(DeckThemeId::Count) - 1));
            settings_.guiTheme = static_cast<GuiThemeStyle>(std::clamp(a["guiTheme"].asInt(static_cast<int>(GuiThemeStyle::ClockworkEngine)), 0, 1));
            settings_.cardWear = static_cast<CardWearLevel>(std::clamp(a["cardWear"].asInt(static_cast<int>(CardWearLevel::Pristine)), 0, static_cast<int>(CardWearLevel::Count) - 1));
            settings_.cardStock = static_cast<CardStock>(std::clamp(a["cardStock"].asInt(static_cast<int>(CardStock::None)), 0, static_cast<int>(CardStock::Count) - 1));
            settings_.cardCorners = static_cast<CardCornerStyle>(std::clamp(a["cardCorners"].asInt(0), 0, static_cast<int>(CardCornerStyle::Count) - 1));
            settings_.cardDeviation = std::clamp(a["cardDeviation"].asFloat(0.25f), 0.0f, 1.0f);
            settings_.layoutMode = static_cast<ResponsiveLayoutMode>(std::clamp(a["layoutMode"].asInt(0), 0, 2));
            settings_.stockPosition = static_cast<StockPosition>(std::clamp(a["stockPosition"].asInt(0), 0, 3));
            settings_.aaMode = 4; // Enforce 4x MSAA permanently
            settings_.lightingPreset = static_cast<LightingPreset>(std::clamp(a["lightingPreset"].asInt(static_cast<int>(LightingPreset::StudioSteady)), 0, 4));
            settings_.sceneDarkness = std::clamp(a["sceneDarkness"].asFloat(0.22f), 0.0f, 0.85f);
            settings_.lightStrength = std::clamp(a["lightStrength"].asFloat(1.20f), 0.0f, 2.0f);
            settings_.menuAnchor = static_cast<MenuAnchor>(std::clamp(a["menuAnchor"].asInt(0), 0, 3));
            settings_.menuFlowVertical = a["menuFlowVertical"].asBool(false);
            settings_.menuDisplayMode = static_cast<MenuDisplayMode>(std::clamp(a["menuDisplayMode"].asInt(0), 0, 2));
            settings_.toolbarScale = static_cast<ToolbarScale>(std::clamp(a["toolbarScale"].asInt(static_cast<int>(ToolbarScale::Regular)), 0, 2));
            settings_.toolbarItems = static_cast<uint32_t>(a["toolbarItems"].asInt(static_cast<int>(ToolbarItem_Default)));
            settings_.showTableMarkings = a["showTableMarkings"].asBool(true);
        }

        if (root.contains("favorites") && root["favorites"].isArray()) {
            settings_.favorites.clear();
            for (const auto& item : root["favorites"].asArray()) {
                if (item.isString() && !item.asString().empty()) {
                    settings_.favorites.push_back(item.asString());
                }
            }
        }

        if (root.contains("audio") && root["audio"].isObject()) {
            const auto& au = root["audio"];
            settings_.masterVolume = std::clamp(au["masterVolume"].asFloat(0.85f), 0.0f, 1.0f);
            settings_.muted = au["muted"].asBool(false);
            settings_.shuffleStyle = static_cast<ShuffleStyle>(std::clamp(au["shuffleStyle"].asInt(0), 0, 1));
        }

        std::cout << "[SolEngine] Loaded settings successfully.\n";
        return true;
    } catch (...) {
        return false;
    }
}

bool SettingsManager::save() {
    json::Object root;
    root["version"] = 1;

    json::Object g;
    g["variant"] = static_cast<int>(settings_.variant);
    g["randomizeOnNewDeal"] = settings_.randomizeOnNewDeal;
    g["guaranteeSolvable"] = settings_.guaranteeSolvable;
    g["lastSeed"] = static_cast<double>(settings_.lastSeed);
    g["autoFinish"] = settings_.autoFinish;
    root["game"] = g;

    json::Object a;
    a["tableTheme"] = static_cast<int>(settings_.tableTheme);
    if (!settings_.tableThemeSlug.empty()) {
        a["tableThemeSlug"] = settings_.tableThemeSlug;
    }
    a["deckTheme"] = static_cast<int>(settings_.deckTheme);
    a["guiTheme"] = static_cast<int>(settings_.guiTheme);
    a["cardWear"] = static_cast<int>(settings_.cardWear);
    a["cardStock"] = static_cast<int>(settings_.cardStock);
    a["cardCorners"] = static_cast<int>(settings_.cardCorners);
    a["cardDeviation"] = settings_.cardDeviation;
    a["layoutMode"] = static_cast<int>(settings_.layoutMode);
    a["stockPosition"] = static_cast<int>(settings_.stockPosition);
    a["aaMode"] = settings_.aaMode;
    a["lightingPreset"] = static_cast<int>(settings_.lightingPreset);
    a["sceneDarkness"] = settings_.sceneDarkness;
    a["lightStrength"] = settings_.lightStrength;
    a["menuAnchor"] = static_cast<int>(settings_.menuAnchor);
    a["menuFlowVertical"] = settings_.menuFlowVertical;
    a["menuDisplayMode"] = static_cast<int>(settings_.menuDisplayMode);
    a["toolbarScale"] = static_cast<int>(settings_.toolbarScale);
    a["toolbarItems"] = static_cast<int>(settings_.toolbarItems);
    a["showTableMarkings"] = settings_.showTableMarkings;
    root["appearance"] = a;

    json::Object au;
    au["masterVolume"] = settings_.masterVolume;
    au["muted"] = settings_.muted;
    au["shuffleStyle"] = static_cast<int>(settings_.shuffleStyle);
    root["audio"] = au;

    json::Array favArray;
    for (const auto& favId : settings_.favorites) {
        favArray.push_back(json::Value{favId});
    }
    root["favorites"] = favArray;

    std::string jsonStr = json::stringify(json::Value{root});

#if defined(__EMSCRIPTEN__)
    MAIN_THREAD_EM_ASM({
        try {
            localStorage.setItem("solengine_settings", UTF8ToString($0));
        } catch (e) {
            console.error("Failed to save solengine_settings to localStorage:", e);
        }
    }, jsonStr.c_str());
    return true;
#else
    std::string path = getSettingsFilePath();
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        std::ofstream ofs(path);
        if (ofs.is_open()) {
            ofs << jsonStr;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[SolEngine] Failed to write settings to " << path << ": " << e.what() << "\n";
    }
    return false;
#endif
}

void SettingsManager::applyToGame(SolitaireBoard& board, AudioFoleyEngine& foley, bool applyTextures) {
    board.setTableTheme(settings_.tableTheme);
    board.setTableThemeSlug(settings_.tableThemeSlug);
    board.setCardDeviation(settings_.cardDeviation);
    board.setLayoutMode(settings_.layoutMode);
    board.setStockPosition(settings_.stockPosition);
    auto& cache = CardTextureCache::instance();
    cache.setCornerStyle(settings_.cardCorners);
    if (applyTextures) {
        cache.loadDeckTheme(settings_.deckTheme);
        cache.applyConditioningAndSync(settings_.cardWear, settings_.cardStock);
    }

    foley.setMasterVolume(settings_.masterVolume);
    foley.setMuted(settings_.muted);
    foley.setShufflePreference(settings_.shuffleStyle);
}

void SettingsManager::syncFromGame(const SolitaireBoard& board, const AudioFoleyEngine& foley) {
    settings_.variant = board.variant();
    settings_.tableTheme = board.tableTheme();
    settings_.tableThemeSlug = board.tableThemeSlug();
    settings_.cardDeviation = board.cardDeviation();
    settings_.layoutMode = board.layoutMode();
    settings_.stockPosition = board.stockPosition();
    const auto& cache = CardTextureCache::instance();
    settings_.deckTheme = cache.currentDeckTheme();
    settings_.cardWear = cache.cardWearLevel();
    settings_.cardStock = cache.cardStock();
    settings_.cardCorners = cache.cornerStyle();
    settings_.lastSeed = board.seed();

    settings_.masterVolume = foley.getMasterVolume();
    settings_.muted = foley.isMuted();
    settings_.shuffleStyle = foley.getShufflePreference();
}

bool SettingsManager::isFavorite(const std::string& gameId) const {
    if (gameId.empty()) return false;
    return std::find(settings_.favorites.begin(), settings_.favorites.end(), gameId) != settings_.favorites.end();
}

void SettingsManager::toggleFavorite(const std::string& gameId) {
    if (gameId.empty()) return;
    auto it = std::find(settings_.favorites.begin(), settings_.favorites.end(), gameId);
    if (it != settings_.favorites.end()) {
        settings_.favorites.erase(it);
    } else {
        settings_.favorites.push_back(gameId);
    }
    save();
}

} // namespace solengine

