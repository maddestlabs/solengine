#include "solengine/eats_package_loader.hpp"
#include "solengine/game_definition.hpp"
#if !defined(SOLENGINE_HEADLESS_CLI)
#include "solengine/audio_foley.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/postfx_system.hpp"
#include "solengine/procedural_background.hpp"
#include "solengine/settings_manager.hpp"
#endif
#include "eatsbits/eatscript/evaluator.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace solengine {

using eatsbits::eatscript::Value;

namespace {

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string stemFromPath(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return base;
}

std::string serializeSoundPackScript(const Value& d) {
    std::ostringstream ss;
    if (d.hasKey("master_volume")) {
        ss << "audio.set_master_volume(" << d.get("master_volume").asNumber(0.85) << ")\n";
    }
    if (d.hasKey("master_variance")) {
        ss << "audio.set_master_variance(" << d.get("master_variance").asNumber(1.0) << ")\n";
    }
    if (d.hasKey("physical_params") && d.get("physical_params").isDict()) {
        const auto& pp = d.get("physical_params").dictVal;
        ss << "audio.set_physical_params({\n";
        bool first = true;
        for (const auto& [k, v] : pp) {
            if (!first) ss << ",\n";
            ss << "    " << k << ": " << v.asNumber(0.0);
            first = false;
        }
        ss << "\n})\n";
    }
    return ss.str();
}

} // namespace

bool EatsPackageLoader::loadFile(const std::string& filePath,
                                 EatsLoadResult& outResult,
                                 AudioFoleyEngine* foley) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        outResult.ok = false;
        outResult.error = "Failed to open .eats file: " + filePath;
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return loadSource(ss.str(), filePath, outResult, foley);
}

bool EatsPackageLoader::loadSource(const std::string& source,
                                   const std::string& virtualPath,
                                   EatsLoadResult& outResult,
                                   AudioFoleyEngine* foley) {
    outResult = EatsLoadResult{};
    if (source.empty()) {
        outResult.error = "Empty .eats source";
        return false;
    }

    eatsbits::eatscript::Evaluator ev;
    ev.setVariable("sol", Value(std::map<std::string, Value>{}));
    ev.setVariable("ambience", Value(std::map<std::string, Value>{}));
    ev.setVariable("audio", Value(std::map<std::string, Value>{}));

    auto noop = [](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(true);
    };

    // 1. sol.define_game({...})
    ev.registerMemberFunction("sol", "define_game",
        [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
            if (args.empty() || !args[0].isDict()) return Value(false);
            const auto& d = args[0];
            SolitaireGameDef def{};
            def.id = toLowerAscii(d.get("id").asString());
            def.name = d.get("name").asString();
            if (def.id.empty() && !def.name.empty()) {
                def.id = toLowerAscii(def.name);
                for (char& c : def.id) {
                    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                }
            }
            if (def.id.empty()) def.id = toLowerAscii(stemFromPath(virtualPath));
            if (def.name.empty()) def.name = def.id;

            def.family = d.get("family").asString();
            def.category = d.get("category").asString();
            if (def.category.empty()) def.category = "CUSTOM";
            def.mechanic = toLowerAscii(d.get("mechanic").asString());
            def.visibility = toLowerAscii(d.get("visibility").asString());
            if (d.hasKey("tags") && d.get("tags").isList()) {
                for (const auto& t : d.get("tags").listVal) {
                    std::string ts = toLowerAscii(t.asString());
                    if (!ts.empty()) def.tags.push_back(std::move(ts));
                }
            }
            def.layout = d.get("layout", Value("columns")).asString();
            def.wrapRanks = d.get("wrap_ranks", Value(false)).asBoolean(false);
            def.kingDeadEnd = d.get("king_dead_end", Value(false)).asBoolean(false);
            def.tableauBuild = toLowerAscii(d.get("tableau_build").asString());
            def.emptyTableau = toLowerAscii(d.get("empty_tableau").asString());
            def.moveGroup = toLowerAscii(d.get("move_group").asString());
            def.foundationMode = toLowerAscii(d.get("foundation_mode").asString());
            def.ribbonRule = toLowerAscii(d.get("ribbon_rule").asString());
            def.description = d.get("description").asString();
            def.decks = static_cast<uint8_t>(std::clamp(d.get("decks", Value(1)).asInt(1), 1, 8));
            def.deckCards = static_cast<uint16_t>(std::max(0, d.get("deck_cards", Value(0)).asInt(0)));
            def.excludeRedRoyals = d.get("exclude_red_royals", Value(false)).asBoolean(false);
            def.suitCount = static_cast<uint8_t>(std::clamp(d.get("suit_count", Value(4)).asInt(4), 1, 4));
            def.jokers = static_cast<uint8_t>(std::clamp(d.get("jokers", Value(0)).asInt(0), 0, 2));
            def.gridCols = static_cast<uint8_t>(std::clamp(d.get("grid_cols", Value(0)).asInt(0), 0, 20));
            def.supportsSolver = d.get("supports_solver", Value(true)).asBoolean(true);
            def.preferCuratedSolvable = d.get("prefer_curated_solvable", Value(false)).asBoolean(false);
            def.dynamicLayout = d.get("dynamic_layout", Value(false)).asBoolean(false);
            def.allowUndo = d.get("allow_undo", Value(true)).asBoolean(true);
            if (d.hasKey("exclude_ranks") && d.get("exclude_ranks").isList()) {
                for (const auto& v : d.get("exclude_ranks").listVal) {
                    int r = v.asInt(0);
                    if (r >= 1 && r <= 13) def.excludeRanks.push_back(static_cast<uint8_t>(r));
                }
            }
            def.tableaus = static_cast<uint8_t>(std::clamp(d.get("tableaus", Value(7)).asInt(7), 0, 104));
            def.foundations = static_cast<uint8_t>(std::clamp(d.get("foundations", Value(4)).asInt(4), 0, 16));
            def.freecells = static_cast<uint8_t>(std::clamp(d.get("freecells", Value(0)).asInt(0), 0, 16));

            if (d.hasKey("tableau_deal_counts") && d.get("tableau_deal_counts").isList()) {
                for (const auto& v : d.get("tableau_deal_counts").listVal) {
                    def.tableauDealCounts.push_back(static_cast<uint8_t>(std::max(0, v.asInt(0))));
                }
            }
            def.tableauAllFaceUp = d.get("tableau_all_face_up", Value(false)).asBoolean(false);
            def.foundationStarterCard = d.get("foundation_starter_card", Value(false)).asBoolean(false);
            def.stockDealMode = d.get("stock_deal_mode", Value("waste_draw_1")).asString();
            def.stockDealCount = d.get("stock_deal_count", Value(1)).asInt(1);
            def.maxRedeals = d.get("max_redeals", Value(0)).asInt(0);
            def.demoSeed = static_cast<uint32_t>(std::max(1, d.get("demo_seed", Value(2026)).asInt(2026)));
            if (d.hasKey("demo_seeds") && d.get("demo_seeds").isList()) {
                for (const auto& v : d.get("demo_seeds").listVal) {
                    int s = v.asInt(0);
                    if (s > 0) def.demoSeeds.push_back(static_cast<uint32_t>(s));
                }
            }
            def.defaultDeckSlug = d.get("deck").asString();
            if (def.defaultDeckSlug.empty()) def.defaultDeckSlug = d.get("default_deck").asString();
            def.defaultTableThemeSlug = d.get("theme").asString();
            def.defaultPostFxId = d.get("postfx").asString();
            def.defaultAmbienceId = d.get("ambience").asString();
            def.defaultSoundPackId = d.get("sound_pack").asString();
            def.defaultPresetId = d.get("preset").asString();
            def.seriesId = toLowerAscii(d.get("series_id").asString());
            def.seriesName = d.get("series_name").asString();
            def.seriesOrder = d.get("series_order", Value(0)).asInt(0);

            def.filePath = virtualPath;
            def.scriptSource = source;
            def.isScripted = true;

            const std::string idLow = def.id;
            const std::string nameLow = toLowerAscii(def.name);
            const std::string famLow = toLowerAscii(def.family);
            if (idLow == "spiderette" || nameLow == "spiderette") {
                def.nativeVariant = GameVariant::Spider1Suit;
            } else if (idLow == "golf" || nameLow == "golf") {
                def.nativeVariant = GameVariant::Golf;
            } else if (nameLow.find("spider") != std::string::npos || famLow == "spider") {
                if (nameLow.find("1") != std::string::npos || idLow.find("_1") != std::string::npos) {
                    def.nativeVariant = GameVariant::Spider1Suit;
                } else if (nameLow.find("2") != std::string::npos || idLow.find("_2") != std::string::npos) {
                    def.nativeVariant = GameVariant::Spider2Suits;
                } else if (nameLow.find("3") != std::string::npos || idLow.find("_3") != std::string::npos) {
                    def.nativeVariant = GameVariant::Spider3Suits;
                } else {
                    def.nativeVariant = GameVariant::Spider4Suits;
                }
            } else if (nameLow.find("freecell") != std::string::npos || idLow == "freecell") {
                def.nativeVariant = GameVariant::FreeCell;
            } else if (nameLow.find("draw 3") != std::string::npos || idLow.find("draw_3") != std::string::npos) {
                def.nativeVariant = GameVariant::KlondikeDraw3;
            } else if (nameLow.find("draw 1") != std::string::npos || idLow.find("draw_1") != std::string::npos) {
                def.nativeVariant = GameVariant::KlondikeDraw1;
            } else {
                def.nativeVariant = GameVariant::CustomScript;
            }

            GameRegistry::instance().registerGame(def);
            outResult.gameIds.push_back(def.id);
            return Value(true);
        });

    // 2. sol.define_series({...})
    ev.registerMemberFunction("sol", "define_series",
        [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
            if (args.empty() || !args[0].isDict()) return Value(false);
            const auto& d = args[0];
            GameSeriesDef series{};
            series.id = toLowerAscii(d.get("id").asString());
            series.name = d.get("name").asString();
            if (series.id.empty()) return Value(false);
            if (series.name.empty()) series.name = series.id;
            series.author = d.get("author").asString();
            series.description = d.get("description").asString();
            series.presetId = toLowerAscii(d.get("preset").asString());
            series.filePath = virtualPath;
            if (d.hasKey("games") && d.get("games").isList()) {
                for (const auto& g : d.get("games").listVal) {
                    if (g.isString() && !g.asString().empty()) {
                        series.gameIds.push_back(toLowerAscii(g.asString()));
                    }
                }
            }
            GameRegistry::instance().registerSeries(series);
            outResult.seriesIds.push_back(series.id);
            return Value(true);
        });

#if !defined(SOLENGINE_HEADLESS_CLI)
    // 3. sol.define_theme / sol.set_theme
    auto defineThemeFn = [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !args[0].isDict()) return Value(false);
        const auto& d = args[0];
        TableThemeDef def{};
        def.id = toLowerAscii(d.get("id").asString());
        def.name = d.get("name").asString();
        if (def.id.empty()) return Value(false);
        if (def.name.empty()) def.name = def.id;
        def.filePath = virtualPath;
        def.isProcedural = d.get("procedural", Value(true)).asBoolean(true);
        def.lacquer = d.get("lacquer", Value(0.0)).asFloat(0.0f);
        def.reliefBump = d.get("relief_bump", Value(2.4)).asFloat(2.4f);
        def.optionLabel = d.get("option_label").asString();
        if (d.hasKey("options") && d.get("options").isList()) {
            for (const auto& opt : d.get("options").listVal) {
                if (!opt.isDict()) continue;
                ThemeOptionChoice ch{};
                ch.id = toLowerAscii(opt.get("id").asString());
                ch.name = opt.get("name").asString();
                if (opt.hasKey("u") && opt.get("u").isList()) {
                    const auto& ulist = opt.get("u").listVal;
                    for (size_t i = 0; i < 4 && i < ulist.size(); ++i) {
                        ch.u[i] = ulist[i].asFloat(0.0f);
                    }
                }
                def.options.push_back(std::move(ch));
            }
        }
        ProceduralBackgroundSystem::instance().registerTheme(def, source);
        outResult.themeIds.push_back(def.id);
        return Value(true);
    };
    ev.registerMemberFunction("sol", "define_theme", defineThemeFn);
    ev.registerMemberFunction("sol", "set_theme", defineThemeFn);
    ev.registerMemberFunction("sol", "create_theme", defineThemeFn);

    // 4. sol.define_deck({...})
    ev.registerMemberFunction("sol", "define_deck",
        [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
            if (args.empty() || !args[0].isDict()) return Value(false);
            const auto& d = args[0];
            CardTextureCache::DeckDef def{};
            def.id = toLowerAscii(d.get("id").asString());
            def.name = d.get("name").asString();
            if (def.id.empty()) return Value(false);
            if (def.name.empty()) def.name = def.id;
            def.filePath = virtualPath;
            def.format = toLowerAscii(d.get("format").asString());
            def.blobBase64 = d.get("blob").asString();
            if (def.blobBase64.empty()) def.blobBase64 = d.get("deck_blob").asString();
            def.cornerRadius = d.get("corner_radius", Value(-1.0)).asFloat(-1.0f);
            CardTextureCache::instance().registerDeck(def);
            outResult.deckIds.push_back(def.id);
            return Value(true);
        });
    ev.registerMemberFunction("sol", "set_deck", noop);
    ev.registerMemberFunction("sol", "set_background", noop);
    ev.registerMemberFunction("sol", "set_corner_radius", noop);

    // 5. sol.define_ambience / ambience.define_environment
    auto defineAmbienceFn = [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty()) return Value(false);
        AmbienceEnvDef env{};
        if (!AudioFoleyEngine::parseAmbienceDict(args[0], env) || env.id.empty()) {
            return Value(false);
        }
        if (foley) {
            foley->registerAmbience(env);
        }
        outResult.ambienceIds.push_back(env.id);
        return Value(true);
    };
    ev.registerMemberFunction("sol", "define_ambience", defineAmbienceFn);
    ev.registerMemberFunction("ambience", "define_environment", defineAmbienceFn);

    // 6. sol.define_sound_pack({...})
    ev.registerMemberFunction("sol", "define_sound_pack",
        [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
            if (args.empty() || !args[0].isDict()) return Value(false);
            const auto& d = args[0];
            SoundPackDef sp{};
            sp.id = toLowerAscii(d.get("id").asString());
            sp.name = d.get("name").asString();
            if (sp.id.empty()) return Value(false);
            if (sp.name.empty()) sp.name = sp.id;
            sp.author = d.get("author").asString();
            sp.description = d.get("description").asString();
            sp.filePath = virtualPath;
            sp.scriptSource = d.hasKey("configure") ? source : serializeSoundPackScript(d);
            if (foley) {
                foley->registerSoundPack(sp);
            }
            outResult.soundPackIds.push_back(sp.id);
            return Value(true);
        });
    for (const char* m : {"set_master_volume", "set_master_variance", "set_physical_params",
                          "define_sound", "define_sequence", "apply_preset", "embed_sound",
                          "embed_sample", "clear_embeds", "clear_embedded"}) {
        ev.registerMemberFunction("audio", m, noop);
    }

    // 7. sol.define_postfx / sol.set_postfx
    auto definePostFxFn = [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !args[0].isDict()) return Value(false);
        const auto& d = args[0];
        PostFxDef def{};
        def.id = toLowerAscii(d.get("id").asString());
        def.name = d.get("name").asString();
        if (def.id.empty()) return Value(false);
        if (def.name.empty()) def.name = def.id;
        def.author = d.get("author").asString();
        def.description = d.get("description").asString();
        def.filePath = virtualPath;
        def.builtinFilter = toLowerAscii(d.get("filter").asString());
        def.optionLabel = d.get("option_label").asString();
        def.gpuCost = d.get("gpu_cost", Value("Low")).asString();
        if (!def.builtinFilter.empty()) {
            def.wgslSource = PostFxSystem::buildBuiltinFilterWgsl(def.builtinFilter);
        }
        if (d.hasKey("options") && d.get("options").isList()) {
            for (const auto& opt : d.get("options").listVal) {
                if (!opt.isDict()) continue;
                PostFxOptionChoice ch{};
                ch.id = toLowerAscii(opt.get("id").asString());
                ch.name = opt.get("name").asString();
                ch.param1 = opt.get("param1", Value(0.0)).asFloat(0.0f);
                ch.param2 = opt.get("param2", Value(0.0)).asFloat(0.0f);
                def.options.push_back(std::move(ch));
            }
        }
        PostFxSystem::instance().registerEffect(def);
        outResult.postFxIds.push_back(def.id);
        return Value(true);
    };
    ev.registerMemberFunction("sol", "define_postfx", definePostFxFn);
    ev.registerMemberFunction("sol", "set_postfx", definePostFxFn);

    // 8. sol.define_preset({...})
    ev.registerMemberFunction("sol", "define_preset",
        [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
            if (args.empty() || !args[0].isDict()) return Value(false);
            const auto& d = args[0];
            ScenePreset sp{};
            sp.id = toLowerAscii(d.get("id").asString());
            sp.name = d.get("name").asString();
            if (sp.id.empty()) return Value(false);
            if (sp.name.empty()) sp.name = sp.id;
            sp.description = d.get("description").asString();
            sp.filePath = virtualPath;
            sp.isBuiltin = false;

            auto& p = sp.profile;
            if (d.hasKey("deck")) p.deckSlug = d.get("deck").asString();
            if (d.hasKey("low_poly_seed")) p.lowPolySeed = static_cast<uint32_t>(std::max(1, d.get("low_poly_seed").asInt(42)));
            if (d.hasKey("card_back")) p.cardBackId = d.get("card_back").asString();
            if (d.hasKey("card_wear")) p.cardWear = static_cast<CardWearLevel>(d.get("card_wear").asInt(1));
            if (d.hasKey("card_stock")) p.cardStock = static_cast<CardStock>(d.get("card_stock").asInt(1));
            if (d.hasKey("card_corners")) p.cardCorners = static_cast<CardCornerStyle>(d.get("card_corners").asInt(0));
            if (d.hasKey("card_deviation")) p.cardDeviation = d.get("card_deviation").asFloat(0.35f);
            if (d.hasKey("theme")) p.tableThemeSlug = d.get("theme").asString();
            if (d.hasKey("theme_option")) p.tableThemeOption = d.get("theme_option").asString();
            if (d.hasKey("postfx")) p.postFxId = d.get("postfx").asString();
            if (d.hasKey("postfx_intensity")) p.postFxIntensity = d.get("postfx_intensity").asFloat(1.0f);
            if (d.hasKey("show_table_markings")) p.showTableMarkings = d.get("show_table_markings").asBoolean(true);
            if (d.hasKey("sound_pack")) p.soundPackId = d.get("sound_pack").asString();
            if (d.hasKey("ambience")) p.ambienceId = d.get("ambience").asString();
            if (d.hasKey("ambience_volume")) p.ambienceVolume = d.get("ambience_volume").asFloat(0.55f);

            if (d.hasKey("lighting") && d.get("lighting").isDict()) {
                const auto& l = d.get("lighting");
                if (l.hasKey("preset")) p.lightingPreset = static_cast<LightingPreset>(l.get("preset").asInt(6));
                if (l.hasKey("angle")) p.lightAngle = static_cast<LightAngle>(l.get("angle").asInt(0));
                if (l.hasKey("shadow_gobo")) p.shadowGobo = static_cast<ShadowGobo>(l.get("shadow_gobo").asInt(1));
                if (l.hasKey("waver")) p.lightWaver = l.get("waver").asFloat(0.45f);
                if (l.hasKey("waver_speed")) p.lightWaverSpeed = l.get("waver_speed").asFloat(0.30f);
                if (l.hasKey("flicker")) p.lightFlicker = l.get("flicker").asFloat(0.0f);
                if (l.hasKey("gobo_softness")) p.goboSoftness = l.get("gobo_softness").asFloat(0.50f);
                if (l.hasKey("cloud_opacity")) p.cloudOpacity = l.get("cloud_opacity").asFloat(0.15f);
                if (l.hasKey("dust_motes")) p.dustMotesIntensity = l.get("dust_motes").asFloat(0.35f);
                if (l.hasKey("scene_darkness")) p.sceneDarkness = l.get("scene_darkness").asFloat(0.22f);
                if (l.hasKey("light_strength")) p.lightStrength = l.get("light_strength").asFloat(1.20f);
            }

            SettingsManager::instance().registerPreset(sp);
            outResult.presetIds.push_back(sp.id);
            return Value(true);
        });
#else
    (void)foley;
    for (const char* m : {"define_theme", "set_theme", "create_theme", "define_deck",
                          "set_deck", "set_background", "set_corner_radius",
                          "define_ambience", "define_sound_pack", "define_postfx",
                          "set_postfx", "define_preset"}) {
        ev.registerMemberFunction("sol", m, noop);
    }
    ev.registerMemberFunction("ambience", "define_environment", noop);
    for (const char* m : {"set_master_volume", "set_master_variance", "set_physical_params",
                          "define_sound", "define_sequence", "apply_preset", "embed_sound",
                          "embed_sample", "clear_embeds", "clear_embedded"}) {
        ev.registerMemberFunction("audio", m, noop);
    }
#endif

    ev.evaluateSource(source);
    if (!ev.getLastError().empty()) {
        outResult.ok = false;
        outResult.error = ev.getLastError();
        return false;
    }

    outResult.ok = outResult.hasAnyAsset();
    return outResult.ok;
}

} // namespace solengine
