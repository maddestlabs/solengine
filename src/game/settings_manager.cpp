#include "solengine/settings_manager.hpp"
#include "solengine/json_parser.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/game_definition.hpp"
#include "solengine/procedural_background.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cmath>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace solengine {

bool GameSceneProfile::empty() const noexcept {
    return !tableTheme && !tableThemeSlug && !tableThemeOption && !tableTint &&
           !postFxId && !postFxOption && !postFxIntensity && !showTableMarkings &&
           !deckTheme && !deckSlug && !lowPolySeed && !cardBackId &&
           !cardWear && !cardStock && !cardCorners && !cardDeviation &&
           !lightingPreset && !lightAngle && !shadowGobo && !lightWaver &&
           !lightFlicker && !goboSoftness && !godRaysIntensity && !dustMotesIntensity &&
           !sceneDarkness && !lightStrength && !soundPackId && !ambienceId &&
           !ambienceVolume && !stockPosition && !autoFinish && !victoryAnimOverride;
}

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
            settings_.variant = static_cast<GameVariant>(std::clamp(g["variant"].asInt(0), 0, static_cast<int>(GameVariant::CustomScript)));
            settings_.randomizeOnNewDeal = g["randomizeOnNewDeal"].asBool(true);
            settings_.guaranteeSolvable = g["guaranteeSolvable"].asBool(false);
            settings_.lastSeed = g["lastSeed"].asUInt(2026u);
            settings_.autoFinish = g["autoFinish"].asBool(true);
            settings_.victoryAnimOverride = static_cast<VictoryAnimOverride>(
                std::clamp(g["victoryAnimOverride"].asInt(0), 0, static_cast<int>(VictoryAnimOverride::Fireworks)));
        }

        if (root.contains("appearance") && root["appearance"].isObject()) {
            const auto& a = root["appearance"];
            settings_.tableTheme = static_cast<TableThemeId>(std::clamp(a["tableTheme"].asInt(0), 0, static_cast<int>(TableThemeId::Count) - 1));
            if (a.contains("tableThemeSlug") && a["tableThemeSlug"].isString()) {
                settings_.tableThemeSlug = a["tableThemeSlug"].asString();
            }
            if (a.contains("tableThemeOption") && a["tableThemeOption"].isString()) {
                settings_.tableThemeOption = a["tableThemeOption"].asString();
            }
            if (a.contains("tableThemeOptions") && a["tableThemeOptions"].isObject()) {
                for (const auto& [k, v] : a["tableThemeOptions"].asObject()) {
                    if (v.isString()) {
                        settings_.tableThemeOptions[k] = v.asString();
                    }
                }
            }
            if (a.contains("postFxId") && a["postFxId"].isString()) {
                settings_.postFxId = a["postFxId"].asString();
            }
            if (a.contains("postFxOption") && a["postFxOption"].isString()) {
                settings_.postFxOption = a["postFxOption"].asString();
            }
            if (a.contains("postFxOptions") && a["postFxOptions"].isObject()) {
                for (const auto& [k, v] : a["postFxOptions"].asObject()) {
                    if (v.isString()) {
                        settings_.postFxOptions[k] = v.asString();
                    }
                }
            }
            if (a.contains("postFxIntensity") && a["postFxIntensity"].isNumber()) {
                settings_.postFxIntensity = std::clamp(a["postFxIntensity"].asFloat(1.0f), 0.0f, 1.0f);
            }
            if (a.contains("postFxAffectUi") && a["postFxAffectUi"].isBool()) {
                settings_.postFxAffectUi = a["postFxAffectUi"].asBool(false);
            }
            if (a.contains("renderResolution")) {
                if (a["renderResolution"].isString()) {
                    const std::string rs = a["renderResolution"].asString();
                    if (rs == "native") settings_.renderResolution = RenderResolutionMode::Native;
                    else if (rs == "1080p") settings_.renderResolution = RenderResolutionMode::Res1080p;
                    else if (rs == "720p") settings_.renderResolution = RenderResolutionMode::Res720p;
                    else if (rs == "540p") settings_.renderResolution = RenderResolutionMode::Res540p;
                    else settings_.renderResolution = RenderResolutionMode::Auto;
                } else if (a["renderResolution"].isNumber()) {
                    int rv = std::clamp(a["renderResolution"].asInt(0), 0, static_cast<int>(RenderResolutionMode::Count) - 1);
                    settings_.renderResolution = static_cast<RenderResolutionMode>(rv);
                }
            }
            if (a.contains("tableTint") && a["tableTint"].isString()) {
                settings_.tableTint = parseTableTint(a["tableTint"].asString());
            } else if (a.contains("tableTint") && a["tableTint"].isNumber()) {
                int tVal = a["tableTint"].asInt(0);
                if (tVal >= 0 && tVal < static_cast<int>(TableTintPreset::Count)) {
                    settings_.tableTint = static_cast<TableTintPreset>(tVal);
                }
            }
            settings_.deckTheme = static_cast<DeckThemeId>(std::clamp(a["deckTheme"].asInt(0), 0, static_cast<int>(DeckThemeId::Count) - 1));
            settings_.deckSlug = (a.contains("deckSlug") && a["deckSlug"].isString()) ? a["deckSlug"].asString() : "";
            settings_.lowPolySeed = static_cast<uint32_t>(std::max(1, a["lowPolySeed"].asInt(42)));
            settings_.cardBackId = (a.contains("cardBackId") && a["cardBackId"].isString()) ? a["cardBackId"].asString() : "auto";
            settings_.guiTheme = static_cast<GuiThemeStyle>(std::clamp(a["guiTheme"].asInt(static_cast<int>(GuiThemeStyle::ClockworkEngine)), 0, 1));
            settings_.cardWear = static_cast<CardWearLevel>(std::clamp(a["cardWear"].asInt(static_cast<int>(CardWearLevel::Pristine)), 0, static_cast<int>(CardWearLevel::Count) - 1));
            settings_.cardStock = static_cast<CardStock>(std::clamp(a["cardStock"].asInt(static_cast<int>(CardStock::None)), 0, static_cast<int>(CardStock::Count) - 1));
            settings_.cardCorners = static_cast<CardCornerStyle>(std::clamp(a["cardCorners"].asInt(0), 0, static_cast<int>(CardCornerStyle::Count) - 1));
            settings_.cardDeviation = std::clamp(a["cardDeviation"].asFloat(0.25f), 0.0f, 1.0f);
            settings_.layoutMode = static_cast<ResponsiveLayoutMode>(std::clamp(a["layoutMode"].asInt(0), 0, 2));
            settings_.stockPosition = static_cast<StockPosition>(std::clamp(a["stockPosition"].asInt(0), 0, 3));
            settings_.aaMode = 4; // Enforce 4x MSAA permanently
            settings_.lightingPreset = static_cast<LightingPreset>(std::clamp(a["lightingPreset"].asInt(static_cast<int>(LightingPreset::StudioSteady)), 0, 8));
            settings_.lightAngle = static_cast<LightAngle>(std::clamp(a["lightAngle"].asInt(0), 0, 3));
            settings_.shadowGobo = static_cast<ShadowGobo>(std::clamp(a["shadowGobo"].asInt(0), 0, 3));
            settings_.lightWaver = std::clamp(a["lightWaver"].asFloat(0.0f), 0.0f, 1.0f);
            settings_.lightFlicker = std::clamp(a["lightFlicker"].asFloat(0.0f), 0.0f, 1.0f);
            settings_.goboSoftness = std::clamp(a["goboSoftness"].asFloat(0.45f), 0.05f, 1.0f);
            settings_.godRaysIntensity = std::clamp(a["godRaysIntensity"].asFloat(0.0f), 0.0f, 1.0f);
            settings_.dustMotesIntensity = std::clamp(a["dustMotesIntensity"].asFloat(0.0f), 0.0f, 1.0f);
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
            if (au.contains("soundPackId") && au["soundPackId"].isString() && !au["soundPackId"].asString().empty()) {
                settings_.soundPackId = au["soundPackId"].asString();
            }
            if (au.contains("ambienceId") && au["ambienceId"].isString() && !au["ambienceId"].asString().empty()) {
                settings_.ambienceId = au["ambienceId"].asString();
            }
            settings_.ambienceVolume = std::clamp(au["ambienceVolume"].asFloat(0.55f), 0.0f, 1.0f);
        }

        if (root.contains("defaultPresetId") && root["defaultPresetId"].isString()) {
            settings_.defaultPresetId = root["defaultPresetId"].asString();
        }

        if (root.contains("gamePresets") && root["gamePresets"].isObject()) {
            settings_.gamePresets.clear();
            for (const auto& [gameId, pId] : root["gamePresets"].asObject()) {
                if (!gameId.empty() && pId.isString() && !pId.asString().empty()) {
                    settings_.gamePresets[gameId] = pId.asString();
                }
            }
        }

        if (root.contains("gameProfiles") && root["gameProfiles"].isObject()) {
            settings_.gameProfiles.clear();
            for (const auto& [gameId, pVal] : root["gameProfiles"].asObject()) {
                if (!pVal.isObject() || gameId.empty()) continue;
                GameSceneProfile prof;
                if (pVal.contains("tableTheme") && pVal["tableTheme"].isNumber()) {
                    prof.tableTheme = static_cast<TableThemeId>(std::clamp(pVal["tableTheme"].asInt(0), 0, static_cast<int>(TableThemeId::Count) - 1));
                }
                if (pVal.contains("tableThemeSlug") && pVal["tableThemeSlug"].isString()) {
                    prof.tableThemeSlug = pVal["tableThemeSlug"].asString();
                }
                if (pVal.contains("tableThemeOption") && pVal["tableThemeOption"].isString()) {
                    prof.tableThemeOption = pVal["tableThemeOption"].asString();
                }
                if (pVal.contains("tableTint")) {
                    if (pVal["tableTint"].isString()) prof.tableTint = parseTableTint(pVal["tableTint"].asString());
                    else if (pVal["tableTint"].isNumber()) {
                        int tv = pVal["tableTint"].asInt(0);
                        if (tv >= 0 && tv < static_cast<int>(TableTintPreset::Count)) prof.tableTint = static_cast<TableTintPreset>(tv);
                    }
                }
                if (pVal.contains("postFxId") && pVal["postFxId"].isString()) prof.postFxId = pVal["postFxId"].asString();
                if (pVal.contains("postFxOption") && pVal["postFxOption"].isString()) prof.postFxOption = pVal["postFxOption"].asString();
                if (pVal.contains("postFxIntensity") && pVal["postFxIntensity"].isNumber()) {
                    prof.postFxIntensity = std::clamp(pVal["postFxIntensity"].asFloat(1.0f), 0.0f, 1.0f);
                }
                if (pVal.contains("showTableMarkings") && pVal["showTableMarkings"].isBool()) {
                    prof.showTableMarkings = pVal["showTableMarkings"].asBool(true);
                }
                if (pVal.contains("deckTheme") && pVal["deckTheme"].isNumber()) {
                    prof.deckTheme = static_cast<DeckThemeId>(std::clamp(pVal["deckTheme"].asInt(0), 0, static_cast<int>(DeckThemeId::Count) - 1));
                }
                if (pVal.contains("deckSlug") && pVal["deckSlug"].isString()) prof.deckSlug = pVal["deckSlug"].asString();
                if (pVal.contains("lowPolySeed") && pVal["lowPolySeed"].isNumber()) {
                    prof.lowPolySeed = static_cast<uint32_t>(std::max(1, pVal["lowPolySeed"].asInt(42)));
                }
                if (pVal.contains("cardBackId") && pVal["cardBackId"].isString()) prof.cardBackId = pVal["cardBackId"].asString();
                if (pVal.contains("cardWear") && pVal["cardWear"].isNumber()) {
                    prof.cardWear = static_cast<CardWearLevel>(std::clamp(pVal["cardWear"].asInt(0), 0, static_cast<int>(CardWearLevel::Count) - 1));
                }
                if (pVal.contains("cardStock") && pVal["cardStock"].isNumber()) {
                    prof.cardStock = static_cast<CardStock>(std::clamp(pVal["cardStock"].asInt(0), 0, static_cast<int>(CardStock::Count) - 1));
                }
                if (pVal.contains("cardCorners") && pVal["cardCorners"].isNumber()) {
                    prof.cardCorners = static_cast<CardCornerStyle>(std::clamp(pVal["cardCorners"].asInt(0), 0, static_cast<int>(CardCornerStyle::Count) - 1));
                }
                if (pVal.contains("cardDeviation") && pVal["cardDeviation"].isNumber()) {
                    prof.cardDeviation = std::clamp(pVal["cardDeviation"].asFloat(0.5f), 0.0f, 1.0f);
                }
                if (pVal.contains("lightingPreset") && pVal["lightingPreset"].isNumber()) {
                    prof.lightingPreset = static_cast<LightingPreset>(std::clamp(pVal["lightingPreset"].asInt(0), 0, 8));
                }
                if (pVal.contains("lightAngle") && pVal["lightAngle"].isNumber()) {
                    prof.lightAngle = static_cast<LightAngle>(std::clamp(pVal["lightAngle"].asInt(0), 0, 3));
                }
                if (pVal.contains("shadowGobo") && pVal["shadowGobo"].isNumber()) {
                    prof.shadowGobo = static_cast<ShadowGobo>(std::clamp(pVal["shadowGobo"].asInt(0), 0, 3));
                }
                if (pVal.contains("lightWaver") && pVal["lightWaver"].isNumber()) prof.lightWaver = std::clamp(pVal["lightWaver"].asFloat(0.5f), 0.0f, 1.0f);
                if (pVal.contains("lightFlicker") && pVal["lightFlicker"].isNumber()) prof.lightFlicker = std::clamp(pVal["lightFlicker"].asFloat(0.0f), 0.0f, 1.0f);
                if (pVal.contains("goboSoftness") && pVal["goboSoftness"].isNumber()) prof.goboSoftness = std::clamp(pVal["goboSoftness"].asFloat(0.45f), 0.05f, 1.0f);
                if (pVal.contains("godRaysIntensity") && pVal["godRaysIntensity"].isNumber()) prof.godRaysIntensity = std::clamp(pVal["godRaysIntensity"].asFloat(0.0f), 0.0f, 1.0f);
                if (pVal.contains("dustMotesIntensity") && pVal["dustMotesIntensity"].isNumber()) prof.dustMotesIntensity = std::clamp(pVal["dustMotesIntensity"].asFloat(0.0f), 0.0f, 1.0f);
                if (pVal.contains("sceneDarkness") && pVal["sceneDarkness"].isNumber()) prof.sceneDarkness = std::clamp(pVal["sceneDarkness"].asFloat(0.22f), 0.0f, 0.85f);
                if (pVal.contains("lightStrength") && pVal["lightStrength"].isNumber()) prof.lightStrength = std::clamp(pVal["lightStrength"].asFloat(1.20f), 0.0f, 2.0f);
                if (pVal.contains("soundPackId") && pVal["soundPackId"].isString()) prof.soundPackId = pVal["soundPackId"].asString();
                if (pVal.contains("ambienceId") && pVal["ambienceId"].isString()) prof.ambienceId = pVal["ambienceId"].asString();
                if (pVal.contains("ambienceVolume") && pVal["ambienceVolume"].isNumber()) prof.ambienceVolume = std::clamp(pVal["ambienceVolume"].asFloat(0.55f), 0.0f, 1.0f);
                if (pVal.contains("stockPosition") && pVal["stockPosition"].isNumber()) prof.stockPosition = static_cast<StockPosition>(std::clamp(pVal["stockPosition"].asInt(0), 0, 3));
                if (pVal.contains("autoFinish") && pVal["autoFinish"].isBool()) prof.autoFinish = pVal["autoFinish"].asBool(true);
                if (pVal.contains("victoryAnimOverride") && pVal["victoryAnimOverride"].isNumber()) {
                    prof.victoryAnimOverride = static_cast<VictoryAnimOverride>(std::clamp(pVal["victoryAnimOverride"].asInt(0), 0, static_cast<int>(VictoryAnimOverride::Fireworks)));
                }
                if (!prof.empty()) {
                    settings_.gameProfiles[gameId] = std::move(prof);
                }
            }
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
    g["victoryAnimOverride"] = static_cast<int>(settings_.victoryAnimOverride);
    root["game"] = g;

    json::Object a;
    a["tableTheme"] = static_cast<int>(settings_.tableTheme);
    if (!settings_.tableThemeSlug.empty()) {
        a["tableThemeSlug"] = settings_.tableThemeSlug;
    }
    if (!settings_.tableThemeOption.empty()) {
        a["tableThemeOption"] = settings_.tableThemeOption;
    }
    if (!settings_.tableThemeOptions.empty()) {
        json::Object optsObj;
        for (const auto& [k, v] : settings_.tableThemeOptions) {
            optsObj[k] = v;
        }
        a["tableThemeOptions"] = optsObj;
    }
    if (!settings_.postFxId.empty() && settings_.postFxId != "none") {
        a["postFxId"] = settings_.postFxId;
    }
    if (!settings_.postFxOption.empty()) {
        a["postFxOption"] = settings_.postFxOption;
    }
    if (!settings_.postFxOptions.empty()) {
        json::Object pfxObj;
        for (const auto& [k, v] : settings_.postFxOptions) {
            pfxObj[k] = v;
        }
        a["postFxOptions"] = pfxObj;
    }
    a["postFxIntensity"] = static_cast<double>(settings_.postFxIntensity);
    a["postFxAffectUi"] = settings_.postFxAffectUi;
    {
        const char* resStr = "auto";
        switch (settings_.renderResolution) {
            case RenderResolutionMode::Native:   resStr = "native"; break;
            case RenderResolutionMode::Res1080p: resStr = "1080p"; break;
            case RenderResolutionMode::Res720p:  resStr = "720p"; break;
            case RenderResolutionMode::Res540p:  resStr = "540p"; break;
            default: break;
        }
        a["renderResolution"] = resStr;
    }
    const auto* tintInfo = getTableTintInfo(settings_.tableTint);
    if (tintInfo && settings_.tableTint != TableTintPreset::Auto) {
        a["tableTint"] = tintInfo->id;
    }
    a["deckTheme"] = static_cast<int>(settings_.deckTheme);
    if (!settings_.deckSlug.empty()) {
        a["deckSlug"] = settings_.deckSlug;
    }
    a["lowPolySeed"] = static_cast<int>(settings_.lowPolySeed);
    a["cardBackId"] = settings_.cardBackId;
    a["guiTheme"] = static_cast<int>(settings_.guiTheme);
    a["cardWear"] = static_cast<int>(settings_.cardWear);
    a["cardStock"] = static_cast<int>(settings_.cardStock);
    a["cardCorners"] = static_cast<int>(settings_.cardCorners);
    a["cardDeviation"] = settings_.cardDeviation;
    a["layoutMode"] = static_cast<int>(settings_.layoutMode);
    a["stockPosition"] = static_cast<int>(settings_.stockPosition);
    a["aaMode"] = settings_.aaMode;
    a["lightingPreset"] = static_cast<int>(settings_.lightingPreset);
    a["lightAngle"] = static_cast<int>(settings_.lightAngle);
    a["shadowGobo"] = static_cast<int>(settings_.shadowGobo);
    a["lightWaver"] = settings_.lightWaver;
    a["lightFlicker"] = settings_.lightFlicker;
    a["goboSoftness"] = settings_.goboSoftness;
    a["godRaysIntensity"] = settings_.godRaysIntensity;
    a["dustMotesIntensity"] = settings_.dustMotesIntensity;
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
    au["soundPackId"] = settings_.soundPackId;
    au["ambienceId"] = settings_.ambienceId;
    au["ambienceVolume"] = settings_.ambienceVolume;
    root["audio"] = au;

    json::Array favArray;
    for (const auto& favId : settings_.favorites) {
        favArray.push_back(json::Value{favId});
    }
    root["favorites"] = favArray;

    if (!settings_.defaultPresetId.empty()) {
        root["defaultPresetId"] = settings_.defaultPresetId;
    }

    if (!settings_.gamePresets.empty()) {
        json::Object gpIds;
        for (const auto& [gameId, presetId] : settings_.gamePresets) {
            if (!gameId.empty() && !presetId.empty()) {
                gpIds[gameId] = presetId;
            }
        }
        if (!gpIds.empty()) {
            root["gamePresets"] = gpIds;
        }
    }

    if (!settings_.gameProfiles.empty()) {
        json::Object gpObj;
        for (const auto& [gameId, prof] : settings_.gameProfiles) {
            if (gameId.empty() || prof.empty()) continue;
            json::Object p;
            if (prof.tableTheme) p["tableTheme"] = static_cast<int>(*prof.tableTheme);
            if (prof.tableThemeSlug) p["tableThemeSlug"] = *prof.tableThemeSlug;
            if (prof.tableThemeOption) p["tableThemeOption"] = *prof.tableThemeOption;
            if (prof.tableTint) {
                const auto* ti = getTableTintInfo(*prof.tableTint);
                p["tableTint"] = ti ? ti->id : "auto";
            }
            if (prof.postFxId) p["postFxId"] = *prof.postFxId;
            if (prof.postFxOption) p["postFxOption"] = *prof.postFxOption;
            if (prof.postFxIntensity) p["postFxIntensity"] = static_cast<double>(*prof.postFxIntensity);
            if (prof.showTableMarkings) p["showTableMarkings"] = *prof.showTableMarkings;
            if (prof.deckTheme) p["deckTheme"] = static_cast<int>(*prof.deckTheme);
            if (prof.deckSlug) p["deckSlug"] = *prof.deckSlug;
            if (prof.lowPolySeed) p["lowPolySeed"] = static_cast<int>(*prof.lowPolySeed);
            if (prof.cardBackId) p["cardBackId"] = *prof.cardBackId;
            if (prof.cardWear) p["cardWear"] = static_cast<int>(*prof.cardWear);
            if (prof.cardStock) p["cardStock"] = static_cast<int>(*prof.cardStock);
            if (prof.cardCorners) p["cardCorners"] = static_cast<int>(*prof.cardCorners);
            if (prof.cardDeviation) p["cardDeviation"] = *prof.cardDeviation;
            if (prof.lightingPreset) p["lightingPreset"] = static_cast<int>(*prof.lightingPreset);
            if (prof.lightAngle) p["lightAngle"] = static_cast<int>(*prof.lightAngle);
            if (prof.shadowGobo) p["shadowGobo"] = static_cast<int>(*prof.shadowGobo);
            if (prof.lightWaver) p["lightWaver"] = *prof.lightWaver;
            if (prof.lightFlicker) p["lightFlicker"] = *prof.lightFlicker;
            if (prof.goboSoftness) p["goboSoftness"] = *prof.goboSoftness;
            if (prof.godRaysIntensity) p["godRaysIntensity"] = *prof.godRaysIntensity;
            if (prof.dustMotesIntensity) p["dustMotesIntensity"] = *prof.dustMotesIntensity;
            if (prof.sceneDarkness) p["sceneDarkness"] = *prof.sceneDarkness;
            if (prof.lightStrength) p["lightStrength"] = *prof.lightStrength;
            if (prof.soundPackId) p["soundPackId"] = *prof.soundPackId;
            if (prof.ambienceId) p["ambienceId"] = *prof.ambienceId;
            if (prof.ambienceVolume) p["ambienceVolume"] = *prof.ambienceVolume;
            if (prof.stockPosition) p["stockPosition"] = static_cast<int>(*prof.stockPosition);
            if (prof.autoFinish) p["autoFinish"] = *prof.autoFinish;
            if (prof.victoryAnimOverride) p["victoryAnimOverride"] = static_cast<int>(*prof.victoryAnimOverride);
            gpObj[gameId] = p;
        }
        if (!gpObj.empty()) {
            root["gameProfiles"] = gpObj;
        }
    }

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
    const AppSettings eff = resolveForGame(board.activeGameDef().id);
    board.setTableTheme(eff.tableTheme);
    board.setTableThemeSlug(eff.tableThemeSlug);
    board.setTableThemeOption(eff.tableThemeOption);
    ProceduralBackgroundSystem::instance().setThemeOption(eff.tableThemeOption);
    ProceduralBackgroundSystem::instance().setTableTint(eff.tableTint);
    board.setCardDeviation(eff.cardDeviation);
    board.setLayoutMode(eff.layoutMode);
    board.setStockPosition(eff.stockPosition);
    auto& cache = CardTextureCache::instance();
    cache.setCornerStyle(eff.cardCorners);
    cache.setActiveCardBack(eff.cardBackId);
    if (applyTextures) {
        cache.loadDeckTheme(eff.deckTheme);
        cache.setActiveCardBack(eff.cardBackId);
        cache.applyConditioningAndSync(eff.cardWear, eff.cardStock);
    }

    foley.setMasterVolume(eff.masterVolume);
    foley.setMuted(eff.muted);
    foley.setShufflePreference(eff.shuffleStyle);
    foley.setSoundPackId(eff.soundPackId);
    foley.setAmbienceVolume(eff.ambienceVolume);
    foley.setAmbience(eff.ambienceId);
}

void SettingsManager::syncFromGame(const SolitaireBoard& board, const AudioFoleyEngine& foley) {
    settings_.variant = board.variant();
    settings_.lastSeed = board.seed();
    settings_.masterVolume = foley.getMasterVolume();
    settings_.muted = foley.isMuted();
    settings_.shuffleStyle = foley.getShufflePreference();

    const std::string& gameId = board.activeGameDef().id;
    if (!gameId.empty() && hasGameProfile(gameId)) {
        return; // Keep global defaults isolated from active game-specific profile
    }

    settings_.tableTheme = board.tableTheme();
    settings_.tableThemeSlug = board.tableThemeSlug();
    settings_.tableThemeOption = ProceduralBackgroundSystem::instance().themeOption();
    settings_.tableTint = ProceduralBackgroundSystem::instance().tableTint();
    settings_.cardDeviation = board.cardDeviation();
    settings_.layoutMode = board.layoutMode();
    settings_.stockPosition = board.stockPosition();
    const auto& cache = CardTextureCache::instance();
    settings_.deckTheme = cache.currentDeckTheme();
    settings_.cardBackId = cache.currentCardBackId();
    settings_.cardWear = cache.cardWearLevel();
    settings_.cardStock = cache.cardStock();
    settings_.cardCorners = cache.cornerStyle();

    settings_.soundPackId = foley.getSoundPackId();
    settings_.ambienceId = foley.getActiveAmbienceId();
    settings_.ambienceVolume = foley.getAmbienceVolume();
}

bool SettingsManager::hasGameProfile(const std::string& gameId) const {
    if (gameId.empty()) return false;
    auto itP = settings_.gamePresets.find(gameId);
    if (itP != settings_.gamePresets.end() && !itP->second.empty()) return true;
    auto it = settings_.gameProfiles.find(gameId);
    return it != settings_.gameProfiles.end() && !it->second.empty();
}

const GameSceneProfile* SettingsManager::getGameProfile(const std::string& gameId) const {
    if (gameId.empty()) return nullptr;
    auto it = settings_.gameProfiles.find(gameId);
    if (it == settings_.gameProfiles.end() || it->second.empty()) return nullptr;
    return &it->second;
}

void SettingsManager::setGameProfile(const std::string& gameId, const GameSceneProfile& profile) {
    if (gameId.empty()) return;
    if (profile.empty()) {
        settings_.gameProfiles.erase(gameId);
    } else {
        settings_.gameProfiles[gameId] = profile;
    }
}

void SettingsManager::clearGameProfile(const std::string& gameId) {
    if (gameId.empty()) return;
    settings_.gamePresets.erase(gameId);
    settings_.gameProfiles.erase(gameId);
}

void SettingsManager::applyProfileToSettings(const GameSceneProfile& p, AppSettings& resolved) {
    if (p.tableTheme) resolved.tableTheme = *p.tableTheme;
    if (p.tableThemeSlug) resolved.tableThemeSlug = *p.tableThemeSlug;
    if (p.tableThemeOption) resolved.tableThemeOption = *p.tableThemeOption;
    if (p.tableTint) resolved.tableTint = *p.tableTint;
    if (p.postFxId) resolved.postFxId = *p.postFxId;
    if (p.postFxOption) resolved.postFxOption = *p.postFxOption;
    if (p.postFxIntensity) resolved.postFxIntensity = *p.postFxIntensity;
    if (p.showTableMarkings) resolved.showTableMarkings = *p.showTableMarkings;

    if (p.deckTheme) resolved.deckTheme = *p.deckTheme;
    if (p.deckSlug) resolved.deckSlug = *p.deckSlug;
    if (p.lowPolySeed) resolved.lowPolySeed = *p.lowPolySeed;
    if (p.cardBackId) resolved.cardBackId = *p.cardBackId;
    if (p.cardWear) resolved.cardWear = *p.cardWear;
    if (p.cardStock) resolved.cardStock = *p.cardStock;
    if (p.cardCorners) resolved.cardCorners = *p.cardCorners;
    if (p.cardDeviation) resolved.cardDeviation = *p.cardDeviation;

    if (p.lightingPreset) resolved.lightingPreset = *p.lightingPreset;
    if (p.lightAngle) resolved.lightAngle = *p.lightAngle;
    if (p.shadowGobo) resolved.shadowGobo = *p.shadowGobo;
    if (p.lightWaver) resolved.lightWaver = *p.lightWaver;
    if (p.lightFlicker) resolved.lightFlicker = *p.lightFlicker;
    if (p.goboSoftness) resolved.goboSoftness = *p.goboSoftness;
    if (p.godRaysIntensity) resolved.godRaysIntensity = *p.godRaysIntensity;
    if (p.dustMotesIntensity) resolved.dustMotesIntensity = *p.dustMotesIntensity;
    if (p.sceneDarkness) resolved.sceneDarkness = *p.sceneDarkness;
    if (p.lightStrength) resolved.lightStrength = *p.lightStrength;

    if (p.soundPackId) resolved.soundPackId = *p.soundPackId;
    if (p.ambienceId) resolved.ambienceId = *p.ambienceId;
    if (p.ambienceVolume) resolved.ambienceVolume = *p.ambienceVolume;
    if (p.stockPosition) resolved.stockPosition = *p.stockPosition;
    if (p.autoFinish) resolved.autoFinish = *p.autoFinish;
    if (p.victoryAnimOverride) resolved.victoryAnimOverride = *p.victoryAnimOverride;
}

GameSceneProfile SettingsManager::extractSceneProfile(const AppSettings& s) {
    GameSceneProfile p;
    p.tableTheme = s.tableTheme;
    p.tableThemeSlug = s.tableThemeSlug;
    p.tableThemeOption = s.tableThemeOption;
    p.tableTint = s.tableTint;
    p.postFxId = s.postFxId;
    p.postFxOption = s.postFxOption;
    p.postFxIntensity = s.postFxIntensity;
    p.showTableMarkings = s.showTableMarkings;
    p.deckTheme = s.deckTheme;
    p.deckSlug = s.deckSlug;
    p.lowPolySeed = s.lowPolySeed;
    p.cardBackId = s.cardBackId;
    p.cardWear = s.cardWear;
    p.cardStock = s.cardStock;
    p.cardCorners = s.cardCorners;
    p.cardDeviation = s.cardDeviation;
    p.lightingPreset = s.lightingPreset;
    p.lightAngle = s.lightAngle;
    p.shadowGobo = s.shadowGobo;
    p.lightWaver = s.lightWaver;
    p.lightFlicker = s.lightFlicker;
    p.goboSoftness = s.goboSoftness;
    p.godRaysIntensity = s.godRaysIntensity;
    p.dustMotesIntensity = s.dustMotesIntensity;
    p.sceneDarkness = s.sceneDarkness;
    p.lightStrength = s.lightStrength;
    p.soundPackId = s.soundPackId;
    p.ambienceId = s.ambienceId;
    p.ambienceVolume = s.ambienceVolume;
    return p;
}

AppSettings SettingsManager::resolveForGame(const std::string& gameId) const {
    AppSettings resolved = settings_;

    // Layer 1b: Global Default Preset (applies to all games unless overridden)
    if (!settings_.defaultPresetId.empty()) {
        if (const auto* defPreset = findPreset(settings_.defaultPresetId)) {
            applyProfileToSettings(defPreset->profile, resolved);
        }
    }

    if (gameId.empty()) return resolved;

    // Layer 2: optional .eats recommended aesthetic defaults (only when game has no assigned preset)
    auto gpIt = settings_.gamePresets.find(gameId);
    const bool hasAssignedPreset = (gpIt != settings_.gamePresets.end() && !gpIt->second.empty());
    if (!hasAssignedPreset) {
        if (const auto* def = GameRegistry::instance().findById(gameId)) {
            if (!def->defaultDeckSlug.empty()) resolved.deckSlug = def->defaultDeckSlug;
            if (!def->defaultTableThemeSlug.empty()) resolved.tableThemeSlug = def->defaultTableThemeSlug;
            if (!def->defaultPostFxId.empty()) resolved.postFxId = def->defaultPostFxId;
            if (!def->defaultAmbienceId.empty()) resolved.ambienceId = def->defaultAmbienceId;
        }
    } else {
        // Layer 2b: Per-game assigned Preset
        if (const auto* gamePreset = findPreset(gpIt->second)) {
            applyProfileToSettings(gamePreset->profile, resolved);
        }
    }

    // Layer 3: user's explicit per-game tweaks
    auto it = settings_.gameProfiles.find(gameId);
    if (it != settings_.gameProfiles.end()) {
        applyProfileToSettings(it->second, resolved);
    }

    return resolved;
}

GameSceneProfile SettingsManager::diffFromGlobal(const AppSettings& global, const AppSettings& effective) {
    GameSceneProfile p;
    auto diffF = [](float a, float b) { return std::fabs(a - b) > 0.001f; };

    if (effective.tableTheme != global.tableTheme) p.tableTheme = effective.tableTheme;
    if (effective.tableThemeSlug != global.tableThemeSlug) p.tableThemeSlug = effective.tableThemeSlug;
    if (effective.tableThemeOption != global.tableThemeOption) p.tableThemeOption = effective.tableThemeOption;
    if (effective.tableTint != global.tableTint) p.tableTint = effective.tableTint;
    if (effective.postFxId != global.postFxId) p.postFxId = effective.postFxId;
    if (effective.postFxOption != global.postFxOption) p.postFxOption = effective.postFxOption;
    if (diffF(effective.postFxIntensity, global.postFxIntensity)) p.postFxIntensity = effective.postFxIntensity;
    if (effective.showTableMarkings != global.showTableMarkings) p.showTableMarkings = effective.showTableMarkings;

    if (effective.deckTheme != global.deckTheme) p.deckTheme = effective.deckTheme;
    if (effective.deckSlug != global.deckSlug) p.deckSlug = effective.deckSlug;
    if (effective.lowPolySeed != global.lowPolySeed) p.lowPolySeed = effective.lowPolySeed;
    if (effective.cardBackId != global.cardBackId) p.cardBackId = effective.cardBackId;
    if (effective.cardWear != global.cardWear) p.cardWear = effective.cardWear;
    if (effective.cardStock != global.cardStock) p.cardStock = effective.cardStock;
    if (effective.cardCorners != global.cardCorners) p.cardCorners = effective.cardCorners;
    if (diffF(effective.cardDeviation, global.cardDeviation)) p.cardDeviation = effective.cardDeviation;

    if (effective.lightingPreset != global.lightingPreset) p.lightingPreset = effective.lightingPreset;
    if (effective.lightAngle != global.lightAngle) p.lightAngle = effective.lightAngle;
    if (effective.shadowGobo != global.shadowGobo) p.shadowGobo = effective.shadowGobo;
    if (diffF(effective.lightWaver, global.lightWaver)) p.lightWaver = effective.lightWaver;
    if (diffF(effective.lightFlicker, global.lightFlicker)) p.lightFlicker = effective.lightFlicker;
    if (diffF(effective.goboSoftness, global.goboSoftness)) p.goboSoftness = effective.goboSoftness;
    if (diffF(effective.godRaysIntensity, global.godRaysIntensity)) p.godRaysIntensity = effective.godRaysIntensity;
    if (diffF(effective.dustMotesIntensity, global.dustMotesIntensity)) p.dustMotesIntensity = effective.dustMotesIntensity;
    if (diffF(effective.sceneDarkness, global.sceneDarkness)) p.sceneDarkness = effective.sceneDarkness;
    if (diffF(effective.lightStrength, global.lightStrength)) p.lightStrength = effective.lightStrength;

    if (effective.soundPackId != global.soundPackId) p.soundPackId = effective.soundPackId;
    if (effective.ambienceId != global.ambienceId) p.ambienceId = effective.ambienceId;
    if (diffF(effective.ambienceVolume, global.ambienceVolume)) p.ambienceVolume = effective.ambienceVolume;
    if (effective.stockPosition != global.stockPosition) p.stockPosition = effective.stockPosition;
    if (effective.autoFinish != global.autoFinish) p.autoFinish = effective.autoFinish;
    if (effective.victoryAnimOverride != global.victoryAnimOverride) p.victoryAnimOverride = effective.victoryAnimOverride;

    return p;
}

namespace {
bool parseBoolStr(const std::string& v) {
    return (v == "true" || v == "True" || v == "1");
}

bool parsePresetEatsFile(const std::string& filePath, bool isBuiltin, ScenePreset& out) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    out = ScenePreset{};
    out.filePath = filePath;
    out.isBuiltin = isBuiltin;
    out.id = std::filesystem::path(filePath).stem().string();
    out.name = out.id;

    std::string line;
    while (std::getline(ifs, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || line[start] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(start, eq - start);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::string val = line.substr(eq + 1);
        size_t vs = val.find_first_not_of(" \t\r\n");
        val = (vs == std::string::npos) ? "" : val.substr(vs);
        size_t hash = val.find('#');
        if (hash != std::string::npos) val = val.substr(0, hash);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r' || val.back() == '\n')) val.pop_back();
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        if (val.empty()) continue;

        try {
            if (key == "id") out.id = val;
            else if (key == "name") out.name = val;
            else if (key == "description") out.description = val;
            else if (key == "deck_slug") out.profile.deckSlug = val;
            else if (key == "deck_theme") out.profile.deckTheme = static_cast<DeckThemeId>(std::clamp(std::stoi(val), 0, static_cast<int>(DeckThemeId::Count) - 1));
            else if (key == "low_poly_seed") out.profile.lowPolySeed = static_cast<uint32_t>(std::max(1, std::stoi(val)));
            else if (key == "card_back") out.profile.cardBackId = val;
            else if (key == "card_wear") out.profile.cardWear = static_cast<CardWearLevel>(std::clamp(std::stoi(val), 0, static_cast<int>(CardWearLevel::Count) - 1));
            else if (key == "card_stock") out.profile.cardStock = static_cast<CardStock>(std::clamp(std::stoi(val), 0, static_cast<int>(CardStock::Count) - 1));
            else if (key == "card_corners") out.profile.cardCorners = static_cast<CardCornerStyle>(std::clamp(std::stoi(val), 0, static_cast<int>(CardCornerStyle::Count) - 1));
            else if (key == "card_deviation") out.profile.cardDeviation = std::clamp(std::stof(val), 0.0f, 1.0f);
            else if (key == "table_theme") out.profile.tableTheme = static_cast<TableThemeId>(std::clamp(std::stoi(val), 0, static_cast<int>(TableThemeId::Count) - 1));
            else if (key == "table_theme_slug") out.profile.tableThemeSlug = val;
            else if (key == "table_theme_option") out.profile.tableThemeOption = val;
            else if (key == "table_tint") out.profile.tableTint = parseTableTint(val);
            else if (key == "postfx_id") out.profile.postFxId = val;
            else if (key == "postfx_option") out.profile.postFxOption = val;
            else if (key == "postfx_intensity") out.profile.postFxIntensity = std::clamp(std::stof(val), 0.0f, 1.0f);
            else if (key == "show_table_markings") out.profile.showTableMarkings = parseBoolStr(val);
            else if (key == "lighting_preset") out.profile.lightingPreset = static_cast<LightingPreset>(std::clamp(std::stoi(val), 0, 8));
            else if (key == "light_angle") out.profile.lightAngle = static_cast<LightAngle>(std::clamp(std::stoi(val), 0, 3));
            else if (key == "shadow_gobo") out.profile.shadowGobo = static_cast<ShadowGobo>(std::clamp(std::stoi(val), 0, 3));
            else if (key == "light_waver") out.profile.lightWaver = std::clamp(std::stof(val), 0.0f, 1.0f);
            else if (key == "light_flicker") out.profile.lightFlicker = std::clamp(std::stof(val), 0.0f, 1.0f);
            else if (key == "gobo_softness") out.profile.goboSoftness = std::clamp(std::stof(val), 0.05f, 1.0f);
            else if (key == "god_rays") out.profile.godRaysIntensity = std::clamp(std::stof(val), 0.0f, 1.0f);
            else if (key == "dust_motes") out.profile.dustMotesIntensity = std::clamp(std::stof(val), 0.0f, 1.0f);
            else if (key == "scene_darkness") out.profile.sceneDarkness = std::clamp(std::stof(val), 0.0f, 0.85f);
            else if (key == "light_strength") out.profile.lightStrength = std::clamp(std::stof(val), 0.0f, 2.0f);
            else if (key == "sound_pack") out.profile.soundPackId = val;
            else if (key == "ambience") out.profile.ambienceId = val;
            else if (key == "ambience_volume") out.profile.ambienceVolume = std::clamp(std::stof(val), 0.0f, 1.0f);
        } catch (...) {}
    }
    return !out.id.empty();
}
} // namespace

std::string SettingsManager::getUserPresetsDir() const {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData) {
        return (std::filesystem::path(appData) / "SolEngine" / "presets").string();
    }
#elif !defined(__EMSCRIPTEN__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return (std::filesystem::path(home) / ".config" / "solengine" / "presets").string();
    }
#endif
    return "presets";
}

void SettingsManager::loadPresets(const std::string& presetsDir) {
    presets_.clear();
    auto scanDir = [&](const std::string& dir, bool isBuiltin) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".eats") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& fp : files) {
            ScenePreset sp;
            if (parsePresetEatsFile(fp.string(), isBuiltin, sp)) {
                auto it = std::find_if(presets_.begin(), presets_.end(), [&](const ScenePreset& x) { return x.id == sp.id; });
                if (it != presets_.end()) *it = std::move(sp);
                else presets_.push_back(std::move(sp));
            }
        }
    };

    scanDir(presetsDir, true);
    if (presets_.empty()) {
        const std::vector<std::string> searchPaths = {
            "../" + presetsDir,
            "../../" + presetsDir,
            "../../../" + presetsDir,
            "./" + presetsDir,
            "/" + presetsDir
        };
        for (const auto& sp : searchPaths) {
            std::error_code ec;
            if (std::filesystem::exists(sp, ec) && std::filesystem::is_directory(sp, ec)) {
                scanDir(sp, true);
                if (!presets_.empty()) break;
            }
        }
    }
    const std::string userDir = getUserPresetsDir();
    if (userDir != presetsDir) {
        scanDir(userDir, false);
    }
}

const ScenePreset* SettingsManager::findPreset(const std::string& presetId) const {
    if (presetId.empty()) return nullptr;
    for (const auto& p : presets_) {
        if (p.id == presetId) return &p;
    }
    return nullptr;
}

namespace {
void writePresetEatsFile(const std::filesystem::path& fp, const ScenePreset& sp) {
#if !defined(__EMSCRIPTEN__)
    std::ofstream ofs(fp);
    if (!ofs.is_open()) return;
    const auto& profile = sp.profile;
    ofs << "# SolEngine User Preset (.eats)\n";
    ofs << "id = \"" << sp.id << "\"\n";
    ofs << "name = \"" << sp.name << "\"\n";
    ofs << "description = \"" << sp.description << "\"\n\n";
    if (profile.deckSlug) ofs << "deck_slug = \"" << *profile.deckSlug << "\"\n";
    if (profile.deckTheme) ofs << "deck_theme = " << static_cast<int>(*profile.deckTheme) << "\n";
    if (profile.lowPolySeed) ofs << "low_poly_seed = " << *profile.lowPolySeed << "\n";
    if (profile.cardBackId) ofs << "card_back = \"" << *profile.cardBackId << "\"\n";
    if (profile.cardWear) ofs << "card_wear = " << static_cast<int>(*profile.cardWear) << "\n";
    if (profile.cardStock) ofs << "card_stock = " << static_cast<int>(*profile.cardStock) << "\n";
    if (profile.cardCorners) ofs << "card_corners = " << static_cast<int>(*profile.cardCorners) << "\n";
    if (profile.cardDeviation) ofs << "card_deviation = " << *profile.cardDeviation << "\n";
    if (profile.tableTheme) ofs << "table_theme = " << static_cast<int>(*profile.tableTheme) << "\n";
    if (profile.tableThemeSlug) ofs << "table_theme_slug = \"" << *profile.tableThemeSlug << "\"\n";
    if (profile.tableThemeOption) ofs << "table_theme_option = \"" << *profile.tableThemeOption << "\"\n";
    if (profile.tableTint) {
        const auto* ti = getTableTintInfo(*profile.tableTint);
        ofs << "table_tint = \"" << (ti ? ti->id : "auto") << "\"\n";
    }
    if (profile.postFxId) ofs << "postfx_id = \"" << *profile.postFxId << "\"\n";
    if (profile.postFxOption) ofs << "postfx_option = \"" << *profile.postFxOption << "\"\n";
    if (profile.postFxIntensity) ofs << "postfx_intensity = " << *profile.postFxIntensity << "\n";
    if (profile.showTableMarkings) ofs << "show_table_markings = " << (*profile.showTableMarkings ? "true" : "false") << "\n";
    if (profile.lightingPreset) ofs << "lighting_preset = " << static_cast<int>(*profile.lightingPreset) << "\n";
    if (profile.lightAngle) ofs << "light_angle = " << static_cast<int>(*profile.lightAngle) << "\n";
    if (profile.shadowGobo) ofs << "shadow_gobo = " << static_cast<int>(*profile.shadowGobo) << "\n";
    if (profile.lightWaver) ofs << "light_waver = " << *profile.lightWaver << "\n";
    if (profile.lightFlicker) ofs << "light_flicker = " << *profile.lightFlicker << "\n";
    if (profile.goboSoftness) ofs << "gobo_softness = " << *profile.goboSoftness << "\n";
    if (profile.godRaysIntensity) ofs << "god_rays = " << *profile.godRaysIntensity << "\n";
    if (profile.dustMotesIntensity) ofs << "dust_motes = " << *profile.dustMotesIntensity << "\n";
    if (profile.sceneDarkness) ofs << "scene_darkness = " << *profile.sceneDarkness << "\n";
    if (profile.lightStrength) ofs << "light_strength = " << *profile.lightStrength << "\n";
    if (profile.soundPackId) ofs << "sound_pack = \"" << *profile.soundPackId << "\"\n";
    if (profile.ambienceId) ofs << "ambience = \"" << *profile.ambienceId << "\"\n";
    if (profile.ambienceVolume) ofs << "ambience_volume = " << *profile.ambienceVolume << "\n";
#else
    (void)fp;
    (void)sp;
#endif
}
} // namespace

bool SettingsManager::saveUserPreset(const std::string& name, const GameSceneProfile& profile, std::string& outPresetId) {
    std::string baseSlug;
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) baseSlug += static_cast<char>(std::tolower(c));
        else if (ch == ' ' || ch == '-' || ch == '_') {
            if (baseSlug.empty() || baseSlug.back() != '_') baseSlug += '_';
        }
    }
    while (!baseSlug.empty() && baseSlug.back() == '_') baseSlug.pop_back();
    if (baseSlug.empty()) baseSlug = "custom_preset";

    std::string slug = baseSlug;
    int suffix = 2;
    while (const auto* existing = findPreset(slug)) {
        if (!existing->isBuiltin) break; // Allow overwriting same user preset if desired
        slug = baseSlug + "_" + std::to_string(suffix++);
    }
    outPresetId = slug;

    ScenePreset sp;
    sp.id = slug;
    sp.name = name.empty() ? slug : name;
    sp.description = "User-saved deck & scene preset.";
    sp.isBuiltin = false;
    sp.profile = profile;

#if !defined(__EMSCRIPTEN__)
    const std::string userDir = getUserPresetsDir();
    try {
        std::filesystem::create_directories(userDir);
        const std::filesystem::path fp = std::filesystem::path(userDir) / (slug + ".eats");
        sp.filePath = fp.string();
        writePresetEatsFile(fp, sp);
    } catch (...) {}
#endif

    auto it = std::find_if(presets_.begin(), presets_.end(), [&](const ScenePreset& x) { return x.id == sp.id; });
    if (it != presets_.end()) *it = std::move(sp);
    else presets_.push_back(std::move(sp));
    return true;
}

bool SettingsManager::updateUserPreset(const std::string& presetId, const GameSceneProfile& profile) {
    if (presetId.empty()) return false;
    auto it = std::find_if(presets_.begin(), presets_.end(), [&](const ScenePreset& x) { return x.id == presetId; });
    if (it == presets_.end() || it->isBuiltin) return false;

    it->profile = profile;
#if !defined(__EMSCRIPTEN__)
    const std::string userDir = getUserPresetsDir();
    try {
        std::filesystem::create_directories(userDir);
        const std::filesystem::path fp = it->filePath.empty()
            ? (std::filesystem::path(userDir) / (it->id + ".eats"))
            : std::filesystem::path(it->filePath);
        it->filePath = fp.string();
        writePresetEatsFile(fp, *it);
    } catch (...) {}
#endif
    return true;
}

std::string SettingsManager::activePresetForGame(const std::string& gameId) const {
    if (!gameId.empty()) {
        auto it = settings_.gamePresets.find(gameId);
        if (it != settings_.gamePresets.end() && !it->second.empty()) {
            return it->second;
        }
    }
    return settings_.defaultPresetId;
}

void SettingsManager::setDefaultPreset(const std::string& presetId) {
    settings_.defaultPresetId = presetId;
}

void SettingsManager::setGamePreset(const std::string& gameId, const std::string& presetId) {
    if (gameId.empty()) return;
    if (presetId.empty()) {
        settings_.gamePresets.erase(gameId);
    } else {
        settings_.gamePresets[gameId] = presetId;
    }
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

