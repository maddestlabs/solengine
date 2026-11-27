#include "solengine/script_rule_engine.hpp"
#include "solengine/game_definition.hpp"
#include "solengine/deterministic_rng.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace solengine {

using eatsbits::eatscript::Value;

namespace {

bool validZone(const ScriptBoardContext* ctx, int zoneIdx) noexcept {
    return ctx && ctx->piles && zoneIdx >= 0 && static_cast<size_t>(zoneIdx) < ctx->piles->size();
}

bool validCard(const ScriptBoardContext* ctx, int zoneIdx, int cardIdx) noexcept {
    if (!validZone(ctx, zoneIdx)) return false;
    const auto& cards = (*ctx->piles)[static_cast<size_t>(zoneIdx)].cards;
    return cardIdx >= 0 && static_cast<size_t>(cardIdx) < cards.size();
}

// 2D Oriented Bounding Box (SAT) overlap test for arbitrary-pose card occlusion.
// Uses a small inset so edge-adjacent cards do not falsely block one another.
bool cardsOverlapOBB(float ax, float ay, float aAngle,
                     float bx, float by, float bAngle,
                     float w, float h, float inset) noexcept {
    const float hw = std::max(0.01f, w * 0.5f - inset);
    const float hh = std::max(0.01f, h * 0.5f - inset);

    const float acx = ax + w * 0.5f;
    const float acy = ay + h * 0.5f;
    const float bcx = bx + w * 0.5f;
    const float bcy = by + h * 0.5f;

    const float dx = bcx - acx;
    const float dy = bcy - acy;

    // Fast radial reject before SAT projections
    const float maxR = std::hypot(hw, hh) * 2.0f;
    if (dx * dx + dy * dy >= maxR * maxR) {
        return false;
    }

    const float ca = std::cos(aAngle);
    const float sa = std::sin(aAngle);
    const float cb = std::cos(bAngle);
    const float sb = std::sin(bAngle);

    // Local axes of A: uA = (ca, sa), vA = (-sa, ca)
    // Local axes of B: uB = (cb, sb), vB = (-sb, cb)
    const float axes[4][2] = {
        { ca,  sa},
        {-sa,  ca},
        { cb,  sb},
        {-sb,  cb}
    };

    for (const auto& axis : axes) {
        const float lx = axis[0];
        const float ly = axis[1];
        const float dist = std::abs(dx * lx + dy * ly);
        const float rA = hw * std::abs(ca * lx + sa * ly) + hh * std::abs(-sa * lx + ca * ly);
        const float rB = hw * std::abs(cb * lx + sb * ly) + hh * std::abs(-sb * lx + cb * ly);
        if (dist >= rA + rB) {
            return false;
        }
    }
    return true;
}

void recordAndApply(ScriptBoardContext* ctx, BoardOp op) {
    if (!ctx) return;
    if (ScriptRuleEngine::applyOp(*ctx, op) && ctx->activeTx) {
        ctx->activeTx->ops.push_back(std::move(op));
    }
}

} // namespace

ScriptRuleEngine::ScriptRuleEngine(const ScriptRuleEngine& other)
    : evaluator_(other.evaluator_)
    , boundCtx_(nullptr)
    , lastError_(other.lastError_)
    , loaded_(other.loaded_)
    , boundCallables_(other.boundCallables_)
    , hasOnConfigure_(other.hasOnConfigure_)
    , hasOnDeal_(other.hasOnDeal_)
    , hasCanPick_(other.hasCanPick_)
    , hasCanDrop_(other.hasCanDrop_)
    , hasOnDrop_(other.hasOnDrop_)
    , hasCanTarget_(other.hasCanTarget_)
    , hasOnTarget_(other.hasOnTarget_)
    , hasOnCardClick_(other.hasOnCardClick_)
    , hasOnStockClick_(other.hasOnStockClick_)
    , hasOnTurnEnd_(other.hasOnTurnEnd_)
    , hasOnVictory_(other.hasOnVictory_)
    , hasOnDemoStep_(other.hasOnDemoStep_)
    , hasGetStatus_(other.hasGetStatus_)
    , hasEvalHeuristic_(other.hasEvalHeuristic_)
    , docManifest_(other.docManifest_)
    , victoryChoreography_(other.victoryChoreography_) {
    if (loaded_) {
        registerHostApi();
    }
}

ScriptRuleEngine& ScriptRuleEngine::operator=(const ScriptRuleEngine& other) {
    if (this == &other) return *this;
    evaluator_ = other.evaluator_;
    boundCtx_ = nullptr;
    lastError_ = other.lastError_;
    loaded_ = other.loaded_;
    boundCallables_ = other.boundCallables_;
    hasOnConfigure_ = other.hasOnConfigure_;
    hasOnDeal_ = other.hasOnDeal_;
    hasCanPick_ = other.hasCanPick_;
    hasCanDrop_ = other.hasCanDrop_;
    hasOnDrop_ = other.hasOnDrop_;
    hasCanTarget_ = other.hasCanTarget_;
    hasOnTarget_ = other.hasOnTarget_;
    hasOnCardClick_ = other.hasOnCardClick_;
    hasOnStockClick_ = other.hasOnStockClick_;
    hasOnTurnEnd_ = other.hasOnTurnEnd_;
    hasOnVictory_ = other.hasOnVictory_;
    hasOnDemoStep_ = other.hasOnDemoStep_;
    hasOnLayout_ = other.hasOnLayout_;
    hasGetStatus_ = other.hasGetStatus_;
    hasEvalHeuristic_ = other.hasEvalHeuristic_;
    docManifest_ = other.docManifest_;
    victoryChoreography_ = other.victoryChoreography_;
    if (loaded_) {
        registerHostApi();
    }
    return *this;
}

ScriptRuleEngine::ScriptRuleEngine(ScriptRuleEngine&& other) noexcept
    : evaluator_(std::move(other.evaluator_))
    , boundCtx_(nullptr)
    , lastError_(std::move(other.lastError_))
    , loaded_(other.loaded_)
    , boundCallables_(std::move(other.boundCallables_))
    , hasOnConfigure_(other.hasOnConfigure_)
    , hasOnDeal_(other.hasOnDeal_)
    , hasCanPick_(other.hasCanPick_)
    , hasCanDrop_(other.hasCanDrop_)
    , hasOnDrop_(other.hasOnDrop_)
    , hasCanTarget_(other.hasCanTarget_)
    , hasOnTarget_(other.hasOnTarget_)
    , hasOnCardClick_(other.hasOnCardClick_)
    , hasOnStockClick_(other.hasOnStockClick_)
    , hasOnTurnEnd_(other.hasOnTurnEnd_)
    , hasOnVictory_(other.hasOnVictory_)
    , hasOnDemoStep_(other.hasOnDemoStep_)
    , hasGetStatus_(other.hasGetStatus_)
    , hasEvalHeuristic_(other.hasEvalHeuristic_)
    , docManifest_(std::move(other.docManifest_))
    , victoryChoreography_(std::move(other.victoryChoreography_)) {
    other.boundCtx_ = nullptr;
    if (loaded_) {
        registerHostApi();
    }
}

ScriptRuleEngine& ScriptRuleEngine::operator=(ScriptRuleEngine&& other) noexcept {
    if (this == &other) return *this;
    evaluator_ = std::move(other.evaluator_);
    boundCtx_ = nullptr;
    other.boundCtx_ = nullptr;
    lastError_ = std::move(other.lastError_);
    loaded_ = other.loaded_;
    boundCallables_ = std::move(other.boundCallables_);
    hasOnConfigure_ = other.hasOnConfigure_;
    hasOnDeal_ = other.hasOnDeal_;
    hasCanPick_ = other.hasCanPick_;
    hasCanDrop_ = other.hasCanDrop_;
    hasOnDrop_ = other.hasOnDrop_;
    hasCanTarget_ = other.hasCanTarget_;
    hasOnTarget_ = other.hasOnTarget_;
    hasOnCardClick_ = other.hasOnCardClick_;
    hasOnStockClick_ = other.hasOnStockClick_;
    hasOnTurnEnd_ = other.hasOnTurnEnd_;
    hasOnVictory_ = other.hasOnVictory_;
    hasOnDemoStep_ = other.hasOnDemoStep_;
    hasOnLayout_ = other.hasOnLayout_;
    hasGetStatus_ = other.hasGetStatus_;
    hasEvalHeuristic_ = other.hasEvalHeuristic_;
    docManifest_ = std::move(other.docManifest_);
    victoryChoreography_ = std::move(other.victoryChoreography_);
    if (loaded_) {
        registerHostApi();
    }
    return *this;
}

void ScriptRuleEngine::reset() noexcept {
    evaluator_ = eatsbits::eatscript::Evaluator{};
    boundCtx_ = nullptr;
    lastError_.clear();
    loaded_ = false;
    boundCallables_.clear();
    hasOnConfigure_ = false;
    hasOnDeal_ = false;
    hasCanPick_ = false;
    hasCanDrop_ = false;
    hasOnDrop_ = false;
    hasCanTarget_ = false;
    hasOnTarget_ = false;
    hasOnCardClick_ = false;
    hasOnStockClick_ = false;
    hasOnTurnEnd_ = false;
    hasOnVictory_ = false;
    hasOnDemoStep_ = false;
    hasOnLayout_ = false;
    hasGetStatus_ = false;
    hasEvalHeuristic_ = false;
    docManifest_.clear();
    victoryChoreography_.clear();
}

bool ScriptRuleEngine::hasAnyHooks() const noexcept {
    return hasOnDeal_ || hasCanPick_ || hasCanDrop_ || hasOnDrop_ || hasCanTarget_ ||
           hasOnTarget_ || hasOnCardClick_ || hasOnStockClick_ ||
           hasOnTurnEnd_ || hasGetStatus_;
}

bool ScriptRuleEngine::loadScriptFile(const std::string& filePath) {
    return loadFileForGame(filePath, "");
}

bool ScriptRuleEngine::loadFileForGame(const std::string& filePath, const std::string& targetGameId) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        lastError_ = "Failed to open script file: " + filePath;
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return loadScriptForGame(ss.str(), targetGameId);
}

bool ScriptRuleEngine::loadScript(const std::string& source) {
    return loadScriptForGame(source, "");
}

bool ScriptRuleEngine::loadScriptForGame(const std::string& source, const std::string& targetGameId) {
    reset();
    registerHostApi();

    Value matchedGameDef;
    bool foundDefinedGame = false;
    auto noopFn = [](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(true);
    };

    evaluator_.setVariable("sol", Value(std::map<std::string, Value>{}));
    evaluator_.setVariable("ambience", Value(std::map<std::string, Value>{}));
    evaluator_.setVariable("audio", Value(std::map<std::string, Value>{}));

    evaluator_.registerMemberFunction("sol", "define_game",
        [&](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
            if (args.empty() || !args[0].isDict()) return Value(false);
            const std::string id = args[0].get("id").asString();
            if (!foundDefinedGame && (targetGameId.empty() || id == targetGameId)) {
                matchedGameDef = args[0];
                foundDefinedGame = true;
            }
            return Value(true);
        });
    for (const char* m : {"define_theme", "set_theme", "create_theme", "define_deck", "set_deck",
                          "set_background", "set_corner_radius", "define_ambience", "define_sound_pack",
                          "define_postfx", "set_postfx", "define_preset", "define_series"}) {
        evaluator_.registerMemberFunction("sol", m, noopFn);
    }
    evaluator_.registerMemberFunction("ambience", "define_environment", noopFn);
    for (const char* m : {"set_master_volume", "set_master_variance", "set_physical_params",
                          "define_sound", "define_sequence", "apply_preset", "embed_sound",
                          "embed_sample", "clear_embeds", "clear_embedded"}) {
        evaluator_.registerMemberFunction("audio", m, noopFn);
    }

    evaluator_.evaluateSource(source);
    if (!evaluator_.getLastError().empty()) {
        lastError_ = evaluator_.getLastError();
        return false;
    }

    auto bindFn = [&](const char* key, bool& hasFlag) {
        if (foundDefinedGame && matchedGameDef.isDict() && matchedGameDef.hasKey(key)) {
            Value v = matchedGameDef.get(key);
            if (v.isFunction()) {
                boundCallables_[key] = std::move(v);
                hasFlag = true;
                return;
            }
        }
        hasFlag = evaluator_.hasFunction(key);
    };

    bindFn("on_configure", hasOnConfigure_);
    bindFn("on_deal", hasOnDeal_);
    bindFn("can_pick", hasCanPick_);
    bindFn("can_drop", hasCanDrop_);
    bindFn("on_drop", hasOnDrop_);
    bindFn("can_target", hasCanTarget_);
    bindFn("on_target", hasOnTarget_);
    bindFn("on_card_click", hasOnCardClick_);
    bindFn("on_stock_click", hasOnStockClick_);
    bindFn("on_turn_end", hasOnTurnEnd_);
    bindFn("on_victory", hasOnVictory_);
    bindFn("on_demo_step", hasOnDemoStep_);
    bindFn("on_layout", hasOnLayout_);
    bindFn("get_status", hasGetStatus_);
    bindFn("eval_heuristic", hasEvalHeuristic_);

    if (foundDefinedGame && matchedGameDef.isDict() && matchedGameDef.hasKey("doc")) {
        const Value doc = matchedGameDef.get("doc");
        if (doc.isDict()) {
            if (doc.hasKey("meta") && doc.get("meta").isList()) {
                const auto& m = doc.get("meta").listVal;
                if (m.size() >= 2) {
                    docManifest_.timeRating = static_cast<uint8_t>(std::clamp(m[0].asInt(3), 1, 5));
                    docManifest_.timeLabel = m[1].asString();
                }
                if (m.size() >= 4) {
                    docManifest_.chanceRating = static_cast<uint8_t>(std::clamp(m[2].asInt(3), 1, 5));
                    docManifest_.chanceLabel = m[3].asString();
                }
                if (m.size() >= 6) {
                    docManifest_.skillRating = static_cast<uint8_t>(std::clamp(m[4].asInt(3), 1, 5));
                    docManifest_.skillLabel = m[5].asString();
                }
                docManifest_.hasExplicitRatings = true;
            }
            if (doc.hasKey("video") && doc.get("video").isString()) {
                docManifest_.videoId = doc.get("video").asString();
            }
            if (doc.hasKey("aliases") && doc.get("aliases").isList()) {
                for (const auto& a : doc.get("aliases").listVal) {
                    if (a.isString() && !a.asString().empty()) docManifest_.alternateNames.push_back(a.asString());
                }
            }
            if (doc.hasKey("goal") && doc.get("goal").isList()) {
                const auto& g = doc.get("goal").listVal;
                if (g.size() >= 1) docManifest_.winGoal = g[0].asString();
                if (g.size() >= 2) docManifest_.loseGoal = g[1].asString();
            }
            if (doc.hasKey("victory") && doc.get("victory").isList()) {
                const auto& v = doc.get("victory").listVal;
                if (v.size() >= 1) docManifest_.victoryTitle = v[0].asString();
                if (v.size() >= 2) docManifest_.victorySubtitle = v[1].asString();
            }
            if (doc.hasKey("zones") && doc.get("zones").isList()) {
                for (const auto& z : doc.get("zones").listVal) {
                    if (!z.isDict()) continue;
                    ZoneDoc zd{};
                    zd.zoneIndex = static_cast<uint8_t>(std::clamp(z.get("index").asInt(0), 0, 255));
                    zd.title = z.get("name").asString();
                    zd.description = z.get("desc").asString();
                    if (z.hasKey("examples") && z.get("examples").isList()) {
                        for (const auto& ex : z.get("examples").listVal) {
                            if (ex.isString()) zd.previewCards.push_back(ex.asString());
                        }
                    }
                    if (z.hasKey("bullets") && z.get("bullets").isList()) {
                        for (const auto& b : z.get("bullets").listVal) {
                            if (b.isString() && !b.asString().empty()) zd.bullets.push_back(b.asString());
                        }
                    } else if (z.hasKey("notes") && z.get("notes").isList()) {
                        for (const auto& b : z.get("notes").listVal) {
                            if (b.isString() && !b.asString().empty()) zd.bullets.push_back(b.asString());
                        }
                    }
                    docManifest_.zones.push_back(std::move(zd));
                }
            }
            if (doc.hasKey("rules") && doc.get("rules").isList()) {
                for (const auto& r : doc.get("rules").listVal) {
                    if (!r.isDict()) continue;
                    RuleDoc rd{};
                    rd.title = r.hasKey("title") ? r.get("title").asString() : r.get("name").asString();
                    if (r.hasKey("bullets") && r.get("bullets").isList()) {
                        for (const auto& b : r.get("bullets").listVal) {
                            if (b.isString() && !b.asString().empty()) rd.bullets.push_back(b.asString());
                        }
                    }
                    if (!rd.title.empty() && !rd.bullets.empty()) {
                        docManifest_.rules.push_back(std::move(rd));
                    }
                }
            }
            if (doc.hasKey("stats") && doc.get("stats").isList()) {
                for (const auto& s : doc.get("stats").listVal) {
                    if (!s.isDict()) continue;
                    StatDoc sd{};
                    sd.key = s.get("key").asString();
                    sd.title = s.hasKey("title") ? s.get("title").asString() : s.get("name").asString();
                    sd.description = s.hasKey("description") ? s.get("description").asString() : s.get("desc").asString();
                    if (!sd.key.empty()) {
                        docManifest_.stats.push_back(std::move(sd));
                    }
                }
            }
            if (doc.hasKey("tags") && doc.get("tags").isList()) {
                for (const auto& t : doc.get("tags").listVal) {
                    if (!t.isDict()) continue;
                    TagDoc td{};
                    td.tag = t.hasKey("tag") ? t.get("tag").asString() : t.get("key").asString();
                    td.title = t.hasKey("title") ? t.get("title").asString() : t.get("name").asString();
                    td.description = t.hasKey("description") ? t.get("description").asString() : t.get("desc").asString();
                    if (!td.tag.empty()) {
                        docManifest_.tags.push_back(std::move(td));
                    }
                }
            }
        }
    }

    if (docManifest_.victoryTitle.empty() && evaluator_.hasVariable("victory_message")) {
        docManifest_.victoryTitle = evaluator_.getVariable("victory_message").asString();
    }
    if (docManifest_.victorySubtitle.empty() && evaluator_.hasVariable("victory_subtitle")) {
        docManifest_.victorySubtitle = evaluator_.getVariable("victory_subtitle").asString();
    }

    loaded_ = true;
    return true;
}

void ScriptRuleEngine::registerHostApi() {
    // --- Zone & Card Queries ---
    evaluator_.registerFunction("zone_count", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        if (!boundCtx_ || !boundCtx_->piles) return Value(0);
        return Value(static_cast<int>(boundCtx_->piles->size()));
    });

    evaluator_.registerFunction("zone_size", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty()) return Value(0);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(0);
        return Value(static_cast<int>((*boundCtx_->piles)[static_cast<size_t>(z)].cards.size()));
    });

    evaluator_.registerFunction("zone_role", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty()) return Value("");
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value("");
        return Value((*boundCtx_->piles)[static_cast<size_t>(z)].role);
    });

    evaluator_.registerFunction("card_rank", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2) return Value(0);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(0);
        return Value(static_cast<int>((*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].rank));
    });

    evaluator_.registerFunction("card_suit", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2) return Value(0);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(0);
        return Value(static_cast<int>((*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].suit));
    });

    evaluator_.registerFunction("card_is_red", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        return Value(isRedSuit((*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].suit));
    });

    evaluator_.registerFunction("card_face_up", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        return Value((*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].faceUp);
    });

    evaluator_.registerFunction("card_stat", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3) return Value(0);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(args.size() >= 4 ? args[3].asInt(0) : 0);
        int fallback = args.size() >= 4 ? args[3].asInt(0) : 0;
        return Value((*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].stat(args[2].asString(), fallback));
    });

    evaluator_.registerFunction("card_has_tag", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        return Value((*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].hasTag(args[2].asString()));
    });

    evaluator_.registerFunction("get_stat", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->blackboard) return Value(0);
        int fallback = args.size() >= 2 ? args[1].asInt(0) : 0;
        return Value(boundCtx_->blackboard->getInt(args[0].asString(), fallback));
    });

    evaluator_.registerFunction("get_str", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->blackboard) return Value("");
        std::string fallback = args.size() >= 2 ? args[1].asString() : "";
        return Value(boundCtx_->blackboard->getStr(args[0].asString(), fallback));
    });

    // --- Atomic Recorded Mutations ---
    evaluator_.registerFunction("move_cards", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_) return Value(false);
        int fromZ = args[0].asInt(-1);
        int startI = args[1].asInt(-1);
        int toZ = args[2].asInt(-1);
        if (!validCard(boundCtx_, fromZ, startI) || !validZone(boundCtx_, toZ)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::MoveCards;
        op.fromPile = static_cast<uint8_t>(fromZ);
        op.toPile = static_cast<uint8_t>(toZ);
        op.cardIdx = static_cast<uint32_t>(startI);
        op.count = static_cast<uint32_t>((*boundCtx_->piles)[static_cast<size_t>(fromZ)].cards.size() - static_cast<size_t>(startI));
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("move_cards_bottom", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_) return Value(false);
        int fromZ = args[0].asInt(-1);
        int startI = args[1].asInt(-1);
        int toZ = args[2].asInt(-1);
        if (!validCard(boundCtx_, fromZ, startI) || !validZone(boundCtx_, toZ)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::MoveCards;
        op.fromPile = static_cast<uint8_t>(fromZ);
        op.toPile = static_cast<uint8_t>(toZ);
        op.cardIdx = static_cast<uint32_t>(startI);
        op.count = static_cast<uint32_t>((*boundCtx_->piles)[static_cast<size_t>(fromZ)].cards.size() - static_cast<size_t>(startI));
        op.nextBool = true; // Insert at bottom (index 0) of destination zone
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("extract_card", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_) return Value(false);
        int fromZ = args[0].asInt(-1);
        int idx = args[1].asInt(-1);
        int toZ = args[2].asInt(-1);
        if (!validCard(boundCtx_, fromZ, idx) || !validZone(boundCtx_, toZ)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::MoveCards;
        op.fromPile = static_cast<uint8_t>(fromZ);
        op.toPile = static_cast<uint8_t>(toZ);
        op.cardIdx = static_cast<uint32_t>(idx);
        op.count = 1;
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("flip_card", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::FlipCard;
        op.fromPile = static_cast<uint8_t>(z);
        op.cardIdx = static_cast<uint32_t>(i);
        op.prevBool = (*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].faceUp;
        op.nextBool = args[2].asBoolean(true);
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("remove_card", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::RemoveCard;
        op.fromPile = static_cast<uint8_t>(z);
        op.cardIdx = static_cast<uint32_t>(i);
        op.snapshotCard = (*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)];
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("set_card_stat", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 4 || !boundCtx_) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        std::string key = args[2].asString();
        BoardOp op{};
        op.type = BoardOpType::SetCardStat;
        op.fromPile = static_cast<uint8_t>(z);
        op.cardIdx = static_cast<uint32_t>(i);
        op.key = key;
        op.prevInt = (*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].stat(key, 0);
        op.nextInt = args[3].asInt(0);
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("add_card_tag", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::AddCardTag;
        op.fromPile = static_cast<uint8_t>(z);
        op.cardIdx = static_cast<uint32_t>(i);
        op.key = args[2].asString();
        op.prevBool = (*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].hasTag(op.key);
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("remove_card_tag", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::RemoveCardTag;
        op.fromPile = static_cast<uint8_t>(z);
        op.cardIdx = static_cast<uint32_t>(i);
        op.key = args[2].asString();
        op.prevBool = (*boundCtx_->piles)[static_cast<size_t>(z)].cards[static_cast<size_t>(i)].hasTag(op.key);
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("set_stat", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        std::string key = args[0].asString();
        BoardOp op{};
        op.type = BoardOpType::SetGlobalStat;
        op.key = key;
        op.prevInt = boundCtx_->blackboard->getInt(key, 0);
        op.nextInt = args[1].asInt(0);
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("set_str", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        std::string key = args[0].asString();
        BoardOp op{};
        op.type = BoardOpType::SetGlobalStr;
        op.key = key;
        op.prevStr = boundCtx_->blackboard->getStr(key, "");
        op.nextStr = args[1].asString();
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("add_score", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->score) return Value(false);
        BoardOp op{};
        op.type = BoardOpType::AddScore;
        op.nextInt = args[0].asInt(0);
        recordAndApply(boundCtx_, std::move(op));
        return Value(true);
    });

    // --- Diegetic Tabletop Info Cards & Zone Badges ---
    auto captureWidgetSnapshotBeforeMutate = [this]() {
        if (!boundCtx_ || !boundCtx_->activeTx || !boundCtx_->blackboard) return;
        if (!boundCtx_->activeTx->hasWidgetSnapshot) {
            boundCtx_->activeTx->prevWidgetCards = boundCtx_->blackboard->widgetCards;
            boundCtx_->activeTx->prevBadges = boundCtx_->blackboard->badges;
            boundCtx_->activeTx->hasWidgetSnapshot = true;
        }
    };

    evaluator_.registerFunction("clear_board_widgets", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        if (!boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (boundCtx_->suppressWidgets) return Value(true);
        captureWidgetSnapshotBeforeMutate();
        boundCtx_->blackboard->widgetCards.clear();
        boundCtx_->blackboard->badges.clear();
        return Value(true);
    });

    evaluator_.registerFunction("set_board_card", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (boundCtx_->suppressWidgets) return Value(true);
        captureWidgetSnapshotBeforeMutate();
        const std::string id = args[0].asString();
        if (id.empty()) return Value(false);
        const std::string title = args[1].asString();
        const std::string anchor = (args.size() >= 3) ? args[2].asString() : "right_of_grid";
        auto& cards = boundCtx_->blackboard->widgetCards;
        BoardWidgetCard* target = nullptr;
        for (auto& c : cards) {
            if (c.id == id) { target = &c; break; }
        }
        if (!target) {
            cards.push_back(BoardWidgetCard{});
            target = &cards.back();
            target->id = id;
        }
        target->title = title;
        target->anchor = anchor;
        if (args.size() >= 5) {
            target->x = static_cast<float>(args[3].asNumber(0.0));
            target->y = static_cast<float>(args[4].asNumber(0.0));
        }
        if (args.size() >= 7) {
            target->w = static_cast<float>(args[5].asNumber(0.0));
            target->h = static_cast<float>(args[6].asNumber(0.0));
        }
        return Value(true);
    });

    evaluator_.registerFunction("clear_card_rows", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (boundCtx_->suppressWidgets) return Value(true);
        captureWidgetSnapshotBeforeMutate();
        const std::string id = args[0].asString();
        for (auto& c : boundCtx_->blackboard->widgetCards) {
            if (c.id == id) { c.rows.clear(); return Value(true); }
        }
        return Value(false);
    });

    evaluator_.registerFunction("add_card_row", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (boundCtx_->suppressWidgets) return Value(true);
        captureWidgetSnapshotBeforeMutate();
        const std::string id = args[0].asString();
        for (auto& c : boundCtx_->blackboard->widgetCards) {
            if (c.id == id) {
                BoardCardRow row{};
                row.kind = BoardCardRow::Kind::KeyValue;
                row.label = args[1].isString() ? args[1].asString() : args[1].toString();
                row.value = args[2].isString() ? args[2].asString() : args[2].toString();
                c.rows.push_back(std::move(row));
                return Value(true);
            }
        }
        return Value(false);
    });

    evaluator_.registerFunction("add_card_bar", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 4 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (boundCtx_->suppressWidgets) return Value(true);
        captureWidgetSnapshotBeforeMutate();
        const std::string id = args[0].asString();
        for (auto& c : boundCtx_->blackboard->widgetCards) {
            if (c.id == id) {
                BoardCardRow row{};
                row.kind = BoardCardRow::Kind::Bar;
                row.label = args[1].asString();
                row.barVal = args[2].asInt(0);
                row.barMax = std::max(1, args[3].asInt(100));
                c.rows.push_back(std::move(row));
                return Value(true);
            }
        }
        return Value(false);
    });

    evaluator_.registerFunction("add_card_pill", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 4 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (boundCtx_->suppressWidgets) return Value(true);
        captureWidgetSnapshotBeforeMutate();
        const std::string id = args[0].asString();
        for (auto& c : boundCtx_->blackboard->widgetCards) {
            if (c.id == id) {
                BoardCardRow row{};
                row.kind = BoardCardRow::Kind::Pill;
                row.label = args[1].isString() ? args[1].asString() : args[1].toString();
                row.value = args[2].isString() ? args[2].asString() : args[2].toString();
                row.active = args[3].asBoolean(false);
                c.rows.push_back(std::move(row));
                return Value(true);
            }
        }
        return Value(false);
    });

    evaluator_.registerFunction("set_zone_badge", [this, captureWidgetSnapshotBeforeMutate](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_ || !boundCtx_->blackboard) return Value(false);
        if (!boundCtx_->suppressWidgets) captureWidgetSnapshotBeforeMutate();
        if (boundCtx_->suppressWidgets) return Value(true);
        const int z = args[0].asInt(-1);
        const std::string anchor = args[1].asString();
        const std::string text = args[2].isString() ? args[2].asString() : args[2].toString();
        const bool highlight = (args.size() >= 4) ? args[3].asBoolean(false) : false;
        const std::string id = "z_" + std::to_string(z) + "_" + anchor;
        auto& badges = boundCtx_->blackboard->badges;
        BoardBadge* target = nullptr;
        for (auto& b : badges) {
            if (b.id == id) { target = &b; break; }
        }
        if (!target) {
            badges.push_back(BoardBadge{});
            target = &badges.back();
            target->id = id;
        }
        target->zoneIdx = z;
        target->anchor = anchor;
        target->text = text;
        target->highlight = highlight;
        return Value(true);
    });

    evaluator_.registerFunction("rand_int", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->rngState) return Value(0);
        int lo = args[0].asInt(0);
        int hi = args[1].asInt(0);
        if (hi <= lo) return Value(lo);
        uint32_t span = static_cast<uint32_t>(hi - lo + 1);
        uint32_t r = mulberry32Next(*boundCtx_->rngState) % span;
        return Value(lo + static_cast<int>(r));
    });

    evaluator_.registerMemberFunction("math", "random", [this, fallbackState = kDefaultSeed](const std::vector<Value>&, const std::map<std::string, Value>&) mutable -> Value {
        uint32_t& state = (boundCtx_ && boundCtx_->rngState) ? *boundCtx_->rngState : fallbackState;
        const uint32_t r = mulberry32Next(state);
        return Value(static_cast<double>(r) / 4294967296.0);
    });

    // --- Unified Help / Documentation Introspection API ---
    evaluator_.registerFunction("doc_goal", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() >= 1) docManifest_.winGoal = args[0].asString();
        if (args.size() >= 2) docManifest_.loseGoal = args[1].asString();
        return Value(true);
    });

    evaluator_.registerFunction("doc_victory", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() >= 1) docManifest_.victoryTitle = args[0].asString();
        if (args.size() >= 2) docManifest_.victorySubtitle = args[1].asString();
        return Value(true);
    });

    auto parseVictoryStage = [](const std::vector<Value>& args, const std::map<std::string, Value>& kwargs) -> VictoryStageSpec {
        VictoryStageSpec spec{};
        const std::string typeStr = eatsbits::eatscript::resolveArg(args, kwargs, 0, "type", Value("cascade_bounce")).asString();
        if (typeStr == "figure_eight" || typeStr == "figure8") {
            spec.type = VictoryAnimType::FigureEight;
        } else if (typeStr == "spiral_vortex" || typeStr == "spiral") {
            spec.type = VictoryAnimType::SpiralVortex;
        } else if (typeStr == "orbit_ring" || typeStr == "orbit") {
            spec.type = VictoryAnimType::OrbitRing;
        } else if (typeStr == "fireworks" || typeStr == "firework_burst") {
            spec.type = VictoryAnimType::Fireworks;
        } else {
            spec.type = VictoryAnimType::CascadeBounce;
        }

        const std::string srcStr = eatsbits::eatscript::resolveArg(args, kwargs, 1, "source", Value("foundation")).asString();
        if (srcStr == "all_piles" || srcStr == "all") {
            spec.source = VictoryCardSource::AllPiles;
        } else if (srcStr == "synthetic_deck" || srcStr == "deck" || srcStr == "synthetic") {
            spec.source = VictoryCardSource::SyntheticDeck;
        } else {
            spec.source = VictoryCardSource::Foundation;
        }

        spec.delay = std::max(0.0f, eatsbits::eatscript::resolveArg(args, kwargs, 2, "delay", Value(0.0)).asFloat(0.0f));
        spec.duration = std::max(0.0f, eatsbits::eatscript::resolveArg(args, kwargs, 3, "duration", Value(0.0)).asFloat(0.0f));
        spec.stagger = std::max(0.0f, eatsbits::eatscript::resolveArg(args, kwargs, 4, "stagger", Value(0.08)).asFloat(0.08f));
        spec.speed = std::max(0.05f, eatsbits::eatscript::resolveArg(args, kwargs, 5, "speed", Value(1.0)).asFloat(1.0f));
        spec.ampX = std::max(0.01f, eatsbits::eatscript::resolveArg(args, kwargs, 6, "amp_x", Value(0.34)).asFloat(0.34f));
        spec.ampY = std::max(0.01f, eatsbits::eatscript::resolveArg(args, kwargs, 7, "amp_y", Value(0.26)).asFloat(0.26f));
        spec.gravity = std::max(0.0f, eatsbits::eatscript::resolveArg(args, kwargs, 8, "gravity", Value(680.0)).asFloat(680.0f));
        spec.spin = eatsbits::eatscript::resolveArg(args, kwargs, 9, "spin", Value(1.0)).asFloat(1.0f);
        spec.count = std::max(0, eatsbits::eatscript::resolveArg(args, kwargs, 10, "count", Value(0)).asInt(0));
        spec.burstCount = std::clamp(eatsbits::eatscript::resolveArg(args, kwargs, 11, "burst_count", Value(13)).asInt(13), 1, 52);
        spec.loop = eatsbits::eatscript::resolveArg(args, kwargs, 12, "loop", Value(true)).asBoolean(true);
        return spec;
    };

    evaluator_.registerFunction("victory_clear_stages", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        victoryChoreography_.clear();
        return Value(true);
    });

    evaluator_.registerFunction("victory_add_stage", [this, parseVictoryStage](const std::vector<Value>& args, const std::map<std::string, Value>& kwargs) -> Value {
        victoryChoreography_.stages.push_back(parseVictoryStage(args, kwargs));
        return Value(true);
    });

    evaluator_.registerFunction("doc_victory_anim", [this, parseVictoryStage](const std::vector<Value>& args, const std::map<std::string, Value>& kwargs) -> Value {
        victoryChoreography_.clear();
        victoryChoreography_.stages.push_back(parseVictoryStage(args, kwargs));
        return Value(true);
    });

    evaluator_.registerFunction("doc_zone", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3) return Value(false);
        ZoneDoc zd{};
        zd.zoneIndex = static_cast<uint8_t>(std::max(0, args[0].asInt(0)));
        zd.title = args[1].asString();
        zd.description = args[2].asString();
        if (args.size() >= 4 && args[3].isList()) {
            for (const auto& c : args[3].listVal) {
                if (c.isString() && !c.asString().empty()) zd.previewCards.push_back(c.asString());
            }
        }
        if (args.size() >= 5 && args[4].isList()) {
            for (const auto& b : args[4].listVal) {
                if (b.isString() && !b.asString().empty()) zd.bullets.push_back(b.asString());
            }
        }
        docManifest_.zones.push_back(std::move(zd));
        return Value(true);
    });

    evaluator_.registerFunction("doc_meta", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() >= 2) {
            docManifest_.timeRating = static_cast<uint8_t>(std::clamp(args[0].asInt(3), 1, 5));
            docManifest_.timeLabel = args[1].asString();
        }
        if (args.size() >= 4) {
            docManifest_.chanceRating = static_cast<uint8_t>(std::clamp(args[2].asInt(3), 1, 5));
            docManifest_.chanceLabel = args[3].asString();
        }
        if (args.size() >= 6) {
            docManifest_.skillRating = static_cast<uint8_t>(std::clamp(args[4].asInt(3), 1, 5));
            docManifest_.skillLabel = args[5].asString();
        }
        docManifest_.hasExplicitRatings = true;
        return Value(true);
    });

    evaluator_.registerFunction("doc_aliases", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !args[0].isList()) return Value(false);
        for (const auto& v : args[0].listVal) {
            if (v.isString() && !v.asString().empty()) {
                docManifest_.alternateNames.push_back(v.asString());
            }
        }
        return Value(true);
    });

    evaluator_.registerFunction("doc_video", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !args[0].isString()) return Value(false);
        docManifest_.videoId = args[0].asString();
        if (args.size() >= 2 && args[1].isString()) {
            docManifest_.videoThumbBase64 = args[1].asString();
        }
        return Value(true);
    });

    evaluator_.registerFunction("doc_stat", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3) return Value(false);
        StatDoc sd{};
        sd.key = args[0].asString();
        sd.title = args[1].asString();
        sd.description = args[2].asString();
        docManifest_.stats.push_back(std::move(sd));
        return Value(true);
    });

    evaluator_.registerFunction("doc_tag", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3) return Value(false);
        TagDoc td{};
        td.tag = args[0].asString();
        td.title = args[1].asString();
        td.description = args[2].asString();
        docManifest_.tags.push_back(std::move(td));
        return Value(true);
    });

    evaluator_.registerFunction("doc_rule", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2) return Value(false);
        RuleDoc rd{};
        rd.title = args[0].asString();
        if (args[1].isList()) {
            for (const auto& b : args[1].listVal) {
                if (b.isString() && !b.asString().empty()) rd.bullets.push_back(b.asString());
            }
        } else if (args[1].isString() && !args[1].asString().empty()) {
            rd.bullets.push_back(args[1].asString());
        }
        if (!rd.title.empty() && !rd.bullets.empty()) {
            docManifest_.rules.push_back(std::move(rd));
            return Value(true);
        }
        return Value(false);
    });

    // --- 2D Planar Graph & Spatial Layout Helpers ---
    evaluator_.registerFunction("is_card_blocked", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        int i = args[1].asInt(-1);
        if (!validCard(boundCtx_, z, i)) return Value(false);
        return Value(ScriptRuleEngine::isCardBlocked(*boundCtx_->piles, static_cast<size_t>(z), static_cast<size_t>(i),
                                                     boundCtx_->cardW, boundCtx_->cardH));
    });

    evaluator_.registerFunction("is_zone_blocked", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(false);
        const auto& cards = (*boundCtx_->piles)[static_cast<size_t>(z)].cards;
        if (cards.empty()) return Value(false);
        return Value(ScriptRuleEngine::isCardBlocked(*boundCtx_->piles, static_cast<size_t>(z), cards.size() - 1,
                                                     boundCtx_->cardW, boundCtx_->cardH));
    });

    evaluator_.registerFunction("set_zone_pose", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(false);
        Pile& p = (*boundCtx_->piles)[static_cast<size_t>(z)];
        p.x = static_cast<float>(args[1].asNumber(0.0));
        p.y = static_cast<float>(args[2].asNumber(0.0));
        if (args.size() >= 4) {
            constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
            p.angle = static_cast<float>(args[3].asNumber(0.0)) * kDegToRad;
        }
        if (args.size() >= 5) {
            p.zIndex = static_cast<int32_t>(args[4].asInt(0));
        }
        return Value(true);
    });

    evaluator_.registerFunction("set_zone_fan", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(false);
        Pile& p = (*boundCtx_->piles)[static_cast<size_t>(z)];
        p.fanDirX = static_cast<float>(args[1].asNumber(0.0));
        p.fanDirY = static_cast<float>(args[2].asNumber(1.0));
        return Value(true);
    });

    evaluator_.registerFunction("set_zone_fan_step", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(false);
        Pile& p = (*boundCtx_->piles)[static_cast<size_t>(z)];
        const float upStep = std::max(0.0f, static_cast<float>(args[1].asNumber(0.0)));
        const float downStep = (args.size() >= 3)
            ? std::max(0.0f, static_cast<float>(args[2].asNumber(0.0)))
            : upStep * 0.5f;
        p.customFanStepUp = upStep;
        p.customFanStepDown = downStep;
        return Value(true);
    });

    evaluator_.registerFunction("set_card_size", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_) return Value(false);
        constexpr float kCardAspect = 1.40f;
        const float w = std::clamp(static_cast<float>(args[0].asNumber(96.0)), 28.0f, 320.0f);
        const float h = (args.size() >= 2)
            ? std::clamp(static_cast<float>(args[1].asNumber(w * kCardAspect)), 39.0f, 448.0f)
            : std::round(w * kCardAspect);
        boundCtx_->cardW = w;
        boundCtx_->cardH = h;
        return Value(true);
    });

    evaluator_.registerFunction("get_board_width", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->viewportW) : 1280.0);
    });

    evaluator_.registerFunction("get_board_height", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->viewportH) : 720.0);
    });

    evaluator_.registerFunction("get_card_width", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->cardW) : 96.0);
    });

    evaluator_.registerFunction("get_card_height", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->cardH) : 136.0);
    });

    evaluator_.registerFunction("get_safe_top", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->safeTop) : 0.0);
    });

    evaluator_.registerFunction("get_safe_bottom", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->safeBottom) : 0.0);
    });

    evaluator_.registerFunction("get_safe_left", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->safeLeft) : 0.0);
    });

    evaluator_.registerFunction("get_safe_right", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(boundCtx_ ? static_cast<double>(boundCtx_->safeRight) : 0.0);
    });

    evaluator_.registerFunction("schedule_action", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->pendingActions) return Value(false);
        float delaySec = static_cast<float>(std::max(0.0, args[0].asNumber(0.0)));
        std::string fnName = args[1].asString();
        if (fnName.empty()) return Value(false);
        bool lockInput = (args.size() >= 3) ? args[2].asBoolean(true) : true;
        boundCtx_->pendingActions->push_back(PendingScriptAction{delaySec, std::move(fnName), lockInput});
        return Value(true);
    });

    evaluator_.registerFunction("shuffle_zone", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(false);
        auto& cards = (*boundCtx_->piles)[static_cast<size_t>(z)].cards;
        if (cards.size() <= 1) return Value(true);
        BoardOp op{};
        op.type = BoardOpType::ShuffleZone;
        op.fromPile = static_cast<uint8_t>(z);
        op.prevCards = cards;
        op.nextCards = cards;
        uint32_t localRng = boundCtx_->rngState ? *boundCtx_->rngState : kDefaultSeed;
        DeterministicRng::shuffle(op.nextCards, localRng);
        if (boundCtx_->rngState) *boundCtx_->rngState = localRng;
        if (!applyOp(*boundCtx_, op)) return Value(false);
        if (boundCtx_->activeTx) boundCtx_->activeTx->ops.push_back(std::move(op));
        return Value(true);
    });

    evaluator_.registerFunction("spawn_card", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_ || !boundCtx_->piles) return Value(false);
        int z = args[0].asInt(-1);
        if (!validZone(boundCtx_, z)) return Value(false);
        int s = std::clamp(args[1].asInt(0), 0, 3);
        int r = std::clamp(args[2].asInt(1), 1, 13);
        bool faceUp = (args.size() >= 4) ? args[3].asBoolean(true) : true;
        auto& cards = (*boundCtx_->piles)[static_cast<size_t>(z)].cards;
        size_t insertIdx = (args.size() >= 5)
            ? static_cast<size_t>(std::clamp(args[4].asInt(static_cast<int>(cards.size())), 0, static_cast<int>(cards.size())))
            : cards.size();
        Card c{};
        c.id = static_cast<uint8_t>((s * 13 + (r - 1)) & 0xFF);
        c.suit = static_cast<Suit>(s);
        c.rank = static_cast<Rank>(r);
        c.faceUp = faceUp;
        c.flipAngle = faceUp ? 0.0f : 3.14159265358979323846f;
        BoardOp op{};
        op.type = BoardOpType::SpawnCard;
        op.toPile = static_cast<uint8_t>(z);
        op.cardIdx = static_cast<uint32_t>(insertIdx);
        op.snapshotCard = c;
        if (!applyOp(*boundCtx_, op)) return Value(false);
        if (boundCtx_->activeTx) boundCtx_->activeTx->ops.push_back(std::move(op));
        return Value(true);
    });

    // --- Procedural Layout & Stateless Seed API ---
    auto statelessMix = [](uint32_t seed, uint32_t idx) noexcept -> uint32_t {
        uint32_t h = seed ^ (idx * 0x9E3779B9u + 0x7F4A7C15u);
        h ^= h >> 16;
        h *= 0x85EBCA6Bu;
        h ^= h >> 13;
        h *= 0xC2B2AE35u;
        h ^= h >> 16;
        return h;
    };

    evaluator_.registerFunction("get_seed", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(static_cast<double>(boundCtx_ ? boundCtx_->seed : kDefaultSeed));
    });

    evaluator_.registerFunction("get_layout_seed", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(static_cast<double>(boundCtx_ ? boundCtx_->layoutSeed : kDefaultSeed));
    });

    evaluator_.registerFunction("seed_hash", [statelessMix](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        uint32_t s = (args.size() >= 1) ? static_cast<uint32_t>(std::max(0.0, args[0].asNumber(2026.0))) : kDefaultSeed;
        uint32_t idx = (args.size() >= 2) ? static_cast<uint32_t>(args[1].asInt(0)) : 0u;
        return Value(static_cast<int>(statelessMix(s, idx) & 0x7FFFFFFFu));
    });

    evaluator_.registerFunction("seed_float", [statelessMix](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        uint32_t s = (args.size() >= 1) ? static_cast<uint32_t>(std::max(0.0, args[0].asNumber(2026.0))) : kDefaultSeed;
        uint32_t idx = (args.size() >= 2) ? static_cast<uint32_t>(args[1].asInt(0)) : 0u;
        uint32_t h = statelessMix(s, idx);
        return Value(static_cast<double>(h & 0x00FFFFFFu) / 16777216.0);
    });

    evaluator_.registerFunction("seed_int", [statelessMix](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 4) return Value(0);
        uint32_t s = static_cast<uint32_t>(std::max(0.0, args[0].asNumber(2026.0)));
        uint32_t idx = static_cast<uint32_t>(args[1].asInt(0));
        int lo = args[2].asInt(0);
        int hi = args[3].asInt(0);
        if (hi < lo) std::swap(lo, hi);
        uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return Value(lo + static_cast<int>(statelessMix(s, idx) % span));
    });

    evaluator_.registerFunction("check_obb_overlap", [](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 8) return Value(false);
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        float ax = static_cast<float>(args[0].asNumber(0.0));
        float ay = static_cast<float>(args[1].asNumber(0.0));
        float aRad = static_cast<float>(args[2].asNumber(0.0)) * kDegToRad;
        float bx = static_cast<float>(args[3].asNumber(0.0));
        float by = static_cast<float>(args[4].asNumber(0.0));
        float bRad = static_cast<float>(args[5].asNumber(0.0)) * kDegToRad;
        float w = static_cast<float>(args[6].asNumber(1.0));
        float h = static_cast<float>(args[7].asNumber(1.4));
        float inset = (args.size() >= 9) ? static_cast<float>(args[8].asNumber(0.0)) : 0.0f;
        return Value(cardsOverlapOBB(ax, ay, aRad, bx, by, bRad, w, h, inset));
    });

    evaluator_.registerFunction("config_tableaus", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->gameDef) return Value(false);
        int count = std::clamp(args[0].asInt(1), 1, 104);
        int perTab = (args.size() >= 2) ? std::clamp(args[1].asInt(1), 0, 52) : 1;
        boundCtx_->gameDef->tableaus = static_cast<uint8_t>(count);
        boundCtx_->gameDef->tableauDealCounts.assign(static_cast<size_t>(count), static_cast<uint8_t>(perTab));
        return Value(true);
    });

    evaluator_.registerFunction("config_deal_counts", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !args[0].isList() || !boundCtx_ || !boundCtx_->gameDef) return Value(false);
        boundCtx_->gameDef->tableauDealCounts.clear();
        for (const auto& v : args[0].listVal) {
            boundCtx_->gameDef->tableauDealCounts.push_back(static_cast<uint8_t>(std::clamp(v.asInt(1), 0, 52)));
        }
        boundCtx_->gameDef->tableaus = static_cast<uint8_t>(std::min<size_t>(104, boundCtx_->gameDef->tableauDealCounts.size()));
        return Value(true);
    });

    evaluator_.registerFunction("config_deck", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->gameDef) return Value(false);
        boundCtx_->gameDef->decks = static_cast<uint8_t>(std::clamp(args[0].asInt(1), 1, 4));
        boundCtx_->gameDef->suitCount = static_cast<uint8_t>(std::clamp(args[1].asInt(4), 1, 4));
        boundCtx_->gameDef->excludeRanks.clear();
        if (args.size() >= 3 && args[2].isList()) {
            for (const auto& v : args[2].listVal) {
                int r = v.asInt(0);
                if (r >= 1 && r <= 13) {
                    boundCtx_->gameDef->excludeRanks.push_back(static_cast<uint8_t>(r));
                }
            }
        }
        return Value(true);
    });

    evaluator_.registerFunction("config_all_face_up", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty() || !boundCtx_ || !boundCtx_->gameDef) return Value(false);
        boundCtx_->gameDef->tableauAllFaceUp = args[0].asBoolean(false);
        return Value(true);
    });

    // --- Card-Unit Procedural Layout Builder (Curves + Motifs + Symmetry + Auto-Fit) ---
    evaluator_.registerFunction("layout_begin", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        layoutSlots_.clear();
        return Value(true);
    });

    evaluator_.registerFunction("layout_add", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2) return Value(false);
        LayoutSlot s{};
        s.ux = static_cast<float>(args[0].asNumber(0.0));
        s.uy = static_cast<float>(args[1].asNumber(0.0));
        s.deg = (args.size() >= 3) ? static_cast<float>(args[2].asNumber(0.0)) : 0.0f;
        s.z = (args.size() >= 4) ? static_cast<int32_t>(args[3].asInt(static_cast<int>(layoutSlots_.size()))) : static_cast<int32_t>(layoutSlots_.size());
        layoutSlots_.push_back(s);
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_mirror_x", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        const size_t n = layoutSlots_.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& s = layoutSlots_[i];
            if (std::abs(s.ux) > 0.05f) {
                layoutSlots_.push_back(LayoutSlot{-s.ux, s.uy, -s.deg, s.z});
            }
        }
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_mirror_y", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        const size_t n = layoutSlots_.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& s = layoutSlots_[i];
            if (std::abs(s.uy) > 0.05f) {
                layoutSlots_.push_back(LayoutSlot{s.ux, -s.uy, -s.deg, s.z});
            }
        }
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_mirror_xy", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        const size_t n1 = layoutSlots_.size();
        for (size_t i = 0; i < n1; ++i) {
            const auto& s = layoutSlots_[i];
            if (std::abs(s.ux) > 0.05f) {
                layoutSlots_.push_back(LayoutSlot{-s.ux, s.uy, -s.deg, s.z});
            }
        }
        const size_t n2 = layoutSlots_.size();
        for (size_t i = 0; i < n2; ++i) {
            const auto& s = layoutSlots_[i];
            if (std::abs(s.uy) > 0.05f) {
                layoutSlots_.push_back(LayoutSlot{s.ux, -s.uy, -s.deg, s.z});
            }
        }
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_rotate_n", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        const int copies = std::clamp(args.empty() ? 4 : args[0].asInt(4), 2, 16);
        const auto base = layoutSlots_;
        layoutSlots_.clear();
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        for (int k = 0; k < copies; ++k) {
            const float stepDeg = static_cast<float>(k) * (360.0f / static_cast<float>(copies));
            const float rad = stepDeg * kDegToRad;
            const float ca = std::cos(rad);
            const float sa = std::sin(rad);
            for (const auto& s : base) {
                const float rx = s.ux * ca - s.uy * sa;
                const float ry = s.ux * sa + s.uy * ca;
                layoutSlots_.push_back(LayoutSlot{rx, ry, s.deg + stepDeg, s.z});
            }
        }
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_prune_overlaps", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        constexpr float kAspect = 1.40f;
        const float pad = args.empty() ? 0.06f : static_cast<float>(args[0].asNumber(0.06));
        const bool enforceEven = (args.size() >= 2) ? args[1].asBoolean(true) : true;
        std::vector<LayoutSlot> kept;
        kept.reserve(layoutSlots_.size());
        for (const auto& cand : layoutSlots_) {
            bool hit = false;
            const float ax = cand.ux - 0.5f;
            const float ay = cand.uy - 0.5f * kAspect;
            const float aRad = cand.deg * kDegToRad;
            for (const auto& ex : kept) {
                const float bx = ex.ux - 0.5f;
                const float by = ex.uy - 0.5f * kAspect;
                const float bRad = ex.deg * kDegToRad;
                // Negative inset expands the OBB by |pad| for guaranteed visual breathing room
                if (cardsOverlapOBB(ax, ay, aRad, bx, by, bRad, 1.0f, kAspect, -pad)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                kept.push_back(cand);
            }
        }
        if (enforceEven && (kept.size() % 2 != 0) && !kept.empty()) {
            kept.pop_back();
        }
        layoutSlots_ = std::move(kept);
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_trim_to", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty()) return Value(static_cast<int>(layoutSlots_.size()));
        const size_t maxN = static_cast<size_t>(std::max(0, args[0].asInt(0)));
        if (layoutSlots_.size() > maxN) {
            layoutSlots_.resize(maxN);
        }
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("layout_count", [this](const std::vector<Value>&, const std::map<std::string, Value>&) -> Value {
        return Value(static_cast<int>(layoutSlots_.size()));
    });

    evaluator_.registerFunction("deal_solvable_pairs", [this, statelessMix](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->piles) return Value(false);
        const int firstZ = std::max(0, args[0].asInt(1));
        const int lastZ = std::min(static_cast<int>(boundCtx_->piles->size()) - 1, args[1].asInt(0));
        if (lastZ <= firstZ) return Value(false);

        auto& piles = *boundCtx_->piles;
        std::vector<size_t> activeZones;
        std::vector<Card> pool;
        for (int z = firstZ; z <= lastZ; ++z) {
            if (!piles[static_cast<size_t>(z)].cards.empty()) {
                activeZones.push_back(static_cast<size_t>(z));
                pool.push_back(piles[static_cast<size_t>(z)].cards[0]);
            }
        }
        const size_t n = activeZones.size();
        if (n < 2 || (n % 2) != 0) return Value(false);

        // Group pool cards into exact equal-rank pairs
        std::vector<std::pair<Card, Card>> pairs;
        pairs.reserve(n / 2);
        std::vector<bool> used(n, false);
        for (size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (!used[j] && pool[i].rank == pool[j].rank) {
                    used[i] = true;
                    used[j] = true;
                    pairs.push_back({pool[i], pool[j]});
                    break;
                }
            }
        }
        if (pairs.size() * 2 != n) return Value(false);

        // Shuffle pairs deterministically using the deal seed
        uint32_t localRng = boundCtx_->seed ^ 0xA511E9B3u;
        if (localRng == 0u) localRng = 0x9E3779B9u;
        for (size_t i = pairs.size() - 1; i > 0; --i) {
            uint32_t r = statelessMix(localRng, static_cast<uint32_t>(i));
            std::swap(pairs[i], pairs[r % (i + 1)]);
        }

        // Precompute OBB blocking adjacency between active zones
        std::vector<std::vector<size_t>> blockers(n);
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                if (ScriptRuleEngine::isCardBlockedByPile(piles, activeZones[i], 0, activeZones[j],
                                                          boundCtx_->cardW, boundCtx_->cardH)) {
                    blockers[i].push_back(j);
                }
            }
        }

        // Reverse-play constructive peeling: repeatedly pick 2 currently unblocked slots
        // and assign them a matching pair -> 100% mathematically guaranteed solvable!
        std::vector<bool> peeled(n, false);
        std::vector<Card> assigned(n);
        uint32_t stepMix = 100u;

        for (size_t pIdx = 0; pIdx < pairs.size(); ++pIdx) {
            std::vector<size_t> freeSlots;
            freeSlots.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                if (peeled[i]) continue;
                bool blocked = false;
                for (size_t b : blockers[i]) {
                    if (!peeled[b]) {
                        blocked = true;
                        break;
                    }
                }
                if (!blocked) {
                    freeSlots.push_back(i);
                }
            }

            // Fallback if a custom map has < 2 unblocked slots: pick highest-zIndex remaining
            if (freeSlots.size() < 2) {
                freeSlots.clear();
                for (size_t i = 0; i < n; ++i) {
                    if (!peeled[i]) freeSlots.push_back(i);
                }
                std::sort(freeSlots.begin(), freeSlots.end(), [&](size_t a, size_t b) {
                    return piles[activeZones[a]].zIndex > piles[activeZones[b]].zIndex;
                });
                if (freeSlots.size() > 2) freeSlots.resize(2);
            }
            if (freeSlots.size() < 2) return Value(false);

            // Pick 2 distinct free slots deterministically, preferring upper layers first
            // so the peel frontier always widens and never starves down to 1 card
            std::sort(freeSlots.begin(), freeSlots.end(), [&](size_t a, size_t b) {
                if (piles[activeZones[a]].zIndex != piles[activeZones[b]].zIndex) {
                    return piles[activeZones[a]].zIndex > piles[activeZones[b]].zIndex;
                }
                return a < b;
            });
            const size_t topPool1 = std::min<size_t>(freeSlots.size(), std::max<size_t>(4u, freeSlots.size() / 2u));
            const size_t pick1Idx = statelessMix(localRng, stepMix++) % topPool1;
            const size_t s1 = freeSlots[pick1Idx];
            freeSlots.erase(freeSlots.begin() + static_cast<ptrdiff_t>(pick1Idx));

            const size_t topPool2 = std::min<size_t>(freeSlots.size(), std::max<size_t>(4u, freeSlots.size() / 2u));
            const size_t pick2Idx = statelessMix(localRng, stepMix++) % topPool2;
            const size_t s2 = freeSlots[pick2Idx];

            peeled[s1] = true;
            peeled[s2] = true;
            assigned[s1] = pairs[pIdx].first;
            assigned[s2] = pairs[pIdx].second;
        }

        // Write the guaranteed-solvable assignment back to the board piles
        for (size_t i = 0; i < n; ++i) {
            auto& c = piles[activeZones[i]].cards[0];
            c.id = assigned[i].id;
            c.suit = assigned[i].suit;
            c.rank = assigned[i].rank;
            c.faceUp = true;
            c.flipAngle = 0.0f;
        }
        return Value(true);
    });

    evaluator_.registerFunction("find_solvable_pair", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->piles) return Value(std::vector<Value>{});
        const int firstZ = std::max(0, args[0].asInt(1));
        const int lastZ = std::min(static_cast<int>(boundCtx_->piles->size()) - 1, args[1].asInt(0));
        if (lastZ <= firstZ) return Value(std::vector<Value>{});

        const auto& piles = *boundCtx_->piles;
        std::vector<size_t> zones;
        std::vector<uint8_t> ranks;
        for (int z = firstZ; z <= lastZ && zones.size() < 64u; ++z) {
            const auto& p = piles[static_cast<size_t>(z)];
            if (!p.cards.empty()) {
                zones.push_back(static_cast<size_t>(z));
                ranks.push_back(static_cast<uint8_t>(p.cards.back().rank));
            }
        }
        const size_t n = zones.size();
        if (n < 2) return Value(std::vector<Value>{});

        std::array<uint64_t, 64> blockMask{};
        std::array<int, 64> unlockWeight{};
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                if (ScriptRuleEngine::isCardBlockedByPile(piles, zones[i], 0, zones[j],
                                                          boundCtx_->cardW, boundCtx_->cardH)) {
                    blockMask[i] |= (1ULL << j);
                    unlockWeight[j] += 1;
                }
            }
        }

        uint64_t startOcc = (n == 64u) ? ~0ULL : ((1ULL << n) - 1ULL);
        std::unordered_set<uint64_t> failed;
        failed.reserve(4096);
        size_t visitedCount = 0;
        constexpr size_t kMaxNodes = 25000;
        int bestA = -1, bestB = -1;

        auto dfs = [&](auto&& self, uint64_t occ, int depth) -> bool {
            if (occ == 0ULL) return true;
            if (visitedCount >= kMaxNodes || failed.count(occ)) return false;
            ++visitedCount;

            // Collect currently unblocked indices
            uint8_t freeIdx[64];
            size_t freeCount = 0;
            for (size_t i = 0; i < n; ++i) {
                if ((occ & (1ULL << i)) && ((occ & blockMask[i]) == 0ULL)) {
                    freeIdx[freeCount++] = static_cast<uint8_t>(i);
                }
            }

            // Build candidate pairs sorted by how many cards they unlock (highest-impact first)
            struct Cand { uint8_t a, b; int score; };
            Cand cands[128];
            size_t candCount = 0;
            for (size_t i = 0; i < freeCount && candCount < 128; ++i) {
                const uint8_t a = freeIdx[i];
                for (size_t j = i + 1; j < freeCount && candCount < 128; ++j) {
                    const uint8_t b = freeIdx[j];
                    if (ranks[a] == ranks[b]) {
                        int sc = (unlockWeight[a] + unlockWeight[b]) * 10
                               + (piles[zones[a]].zIndex + piles[zones[b]].zIndex);
                        cands[candCount++] = Cand{a, b, sc};
                    }
                }
            }
            if (candCount == 0) {
                failed.insert(occ);
                return false;
            }
            std::sort(cands, cands + candCount, [](const Cand& x, const Cand& y) {
                return x.score > y.score;
            });

            for (size_t k = 0; k < candCount; ++k) {
                const uint64_t nextOcc = occ & ~(1ULL << cands[k].a) & ~(1ULL << cands[k].b);
                if (self(self, nextOcc, depth + 1)) {
                    if (depth == 0) {
                        bestA = static_cast<int>(zones[cands[k].a]);
                        bestB = static_cast<int>(zones[cands[k].b]);
                    }
                    return true;
                }
            }
            failed.insert(occ);
            return false;
        };

        if (dfs(dfs, startOcc, 0) && bestA >= 0 && bestB >= 0) {
            return Value(std::vector<Value>{Value(bestA), Value(bestB)});
        }
        return Value(std::vector<Value>{});
    });

    evaluator_.registerFunction("layout_from_ascii", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.empty()) return Value(0);
        std::vector<std::string> lines;
        if (args[0].isList()) {
            for (const auto& v : args[0].listVal) {
                lines.push_back(v.asString());
            }
        } else {
            const std::string raw = args[0].asString();
            std::string cur;
            for (char c : raw) {
                if (c == '\r') continue;
                if (c == '\n') {
                    lines.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) lines.push_back(cur);
        }

        // Trim leading and trailing blank lines
        auto isBlank = [](const std::string& s) {
            for (char c : s) {
                if (c != ' ' && c != '\t') return false;
            }
            return true;
        };
        while (!lines.empty() && isBlank(lines.front())) lines.erase(lines.begin());
        while (!lines.empty() && isBlank(lines.back())) lines.pop_back();
        if (lines.empty()) return Value(0);

        const float colStep = (args.size() >= 2) ? static_cast<float>(args[1].asNumber(1.25)) : 1.25f;
        const float rowStep = (args.size() >= 3) ? static_cast<float>(args[2].asNumber(1.62)) : 1.62f;
        const int32_t baseZ = (args.size() >= 4) ? static_cast<int32_t>(args[3].asInt(static_cast<int>(layoutSlots_.size())))
                                                 : static_cast<int32_t>(layoutSlots_.size());
        const float ox = (args.size() >= 5) ? static_cast<float>(args[4].asNumber(0.0)) : 0.0f;
        const float oy = (args.size() >= 6) ? static_cast<float>(args[5].asNumber(0.0)) : 0.0f;

        struct ParsedGlyph {
            int col{0};
            int row{0};
            float deg{0.0f};
            int32_t zOff{0};
        };
        std::vector<ParsedGlyph> glyphs;
        int minCol = 1000000, maxCol = -1000000;
        int minRow = 1000000, maxRow = -1000000;

        for (int r = 0; r < static_cast<int>(lines.size()); ++r) {
            const std::string& line = lines[static_cast<size_t>(r)];
            for (int c = 0; c < static_cast<int>(line.size()); ++c) {
                const char ch = line[static_cast<size_t>(c)];
                float deg = 0.0f;
                int32_t zOff = 0;
                bool isCard = true;
                if (ch == '|' || ch == '#' || ch == 'O' || ch == 'o') {
                    deg = 0.0f;
                } else if (ch == '-') {
                    deg = 90.0f;
                } else if (ch == '/') {
                    deg = 45.0f;
                } else if (ch == '\\') {
                    deg = -45.0f;
                } else if (ch == '(' || ch == '<') {
                    deg = -22.5f;
                } else if (ch == ')' || ch == '>') {
                    deg = 22.5f;
                } else if (ch >= '1' && ch <= '9') {
                    deg = 0.0f;
                    zOff = static_cast<int32_t>(ch - '1') * 10;
                } else {
                    isCard = false;
                }
                if (isCard) {
                    glyphs.push_back(ParsedGlyph{c, r, deg, zOff});
                    minCol = std::min(minCol, c);
                    maxCol = std::max(maxCol, c);
                    minRow = std::min(minRow, r);
                    maxRow = std::max(maxRow, r);
                }
            }
        }
        if (glyphs.empty()) return Value(0);

        const float midC = 0.5f * static_cast<float>(minCol + maxCol);
        const float midR = 0.5f * static_cast<float>(minRow + maxRow);
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& g = glyphs[i];
            const float ux = ox + (static_cast<float>(g.col) - midC) * colStep;
            const float uy = oy + (static_cast<float>(g.row) - midR) * rowStep;
            const int32_t z = baseZ + g.zOff + static_cast<int32_t>(i);
            layoutSlots_.push_back(LayoutSlot{ux, uy, g.deg, z});
        }
        return Value(static_cast<int>(glyphs.size()));
    });

    evaluator_.registerFunction("layout_preset", [this, statelessMix](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        constexpr float kPi = 3.14159265358979323846f;
        std::string name = (args.size() >= 1) ? args[0].asString() : "random";
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const uint32_t lseed = (args.size() >= 2)
            ? static_cast<uint32_t>(std::max(0.0, args[1].asNumber(boundCtx_ ? static_cast<double>(boundCtx_->layoutSeed) : 2026.0)))
            : (boundCtx_ ? boundCtx_->layoutSeed : kDefaultSeed);

        auto sInt = [&](uint32_t idx, int lo, int hi) -> int {
            if (hi < lo) std::swap(lo, hi);
            uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
            return lo + static_cast<int>(statelessMix(lseed, idx) % span);
        };
        auto sFloat = [&](uint32_t idx) -> float {
            return static_cast<float>(statelessMix(lseed, idx) & 0x00FFFFFFu) / 16777216.0f;
        };

        int arch = 0;
        if (name == "clockwork" || name == "clock" || name == "dial" || name == "ring") arch = 0;
        else if (name == "figure8" || name == "figure_8" || name == "infinity") arch = 1;
        else if (name == "archway" || name == "arch") arch = 2;
        else if (name == "wave" || name == "twin_wave") arch = 3;
        else if (name == "diamond" || name == "diamond_palace") arch = 4;
        else if (name == "symmetry" || name == "quad" || name == "motif") arch = 5;
        else if (name == "grid") arch = 6;
        else arch = sInt(1, 0, 6);

        layoutSlots_.clear();
        auto addSlot = [&](float ux, float uy, float deg, int32_t z) {
            layoutSlots_.push_back(LayoutSlot{ux, uy, deg, z});
        };

        const char* displayName = "GRID";
        if (arch == 0) {
            displayName = "CLOCKWORK";
            const int sub = sInt(2, 0, 2);
            if (sub == 0) {
                const int outerN = 12 + sInt(3, 0, 2) * 2;
                const float rad = 3.30f + static_cast<float>(outerN - 12) * 0.25f;
                const int gapMode = sInt(4, 0, 2);
                const int gapStart = sInt(5, 0, outerN - 1);
                const int gapLen = (gapMode == 1) ? 2 : ((gapMode == 2) ? 4 : 0);
                const float step = 360.0f / static_cast<float>(outerN);
                for (int i = 0; i < outerN; ++i) {
                    if (((i - gapStart + outerN) % outerN) >= gapLen) {
                        const float deg = -90.0f + static_cast<float>(i) * step;
                        const float th = deg * kPi / 180.0f;
                        addSlot(std::cos(th) * rad, std::sin(th) * rad, deg + 90.0f, i);
                    }
                }
                const float hOff = 1.38f;
                const float rotOff = static_cast<float>(sInt(6, 0, 1)) * 45.0f;
                for (int k = 0; k < 4; ++k) {
                    const float deg = -90.0f + rotOff + static_cast<float>(k) * 90.0f;
                    const float th = deg * kPi / 180.0f;
                    addSlot(std::cos(th) * hOff, std::sin(th) * hOff, deg + 90.0f, outerN + k);
                }
            } else if (sub == 1) {
                const int outerN = 14 + sInt(3, 0, 1) * 2;
                const int innerN = 6 + sInt(4, 0, 1) * 2;
                const float rOut = 4.05f + static_cast<float>(outerN - 14) * 0.25f;
                const float rIn = 1.95f + static_cast<float>(innerN - 6) * 0.22f;
                const int outGapStart = sInt(5, 0, outerN - 1);
                const int outGapLen = sInt(6, 0, 2) * 2;
                const int inGapStart = sInt(7, 0, innerN - 1);
                const int inGapLen = sInt(8, 0, 1) * 2;
                for (int i = 0; i < outerN; ++i) {
                    if (((i - outGapStart + outerN) % outerN) >= outGapLen) {
                        const float deg = -90.0f + static_cast<float>(i) * (360.0f / static_cast<float>(outerN));
                        const float th = deg * kPi / 180.0f;
                        addSlot(std::cos(th) * rOut, std::sin(th) * rOut, deg + 90.0f, i);
                    }
                }
                for (int j = 0; j < innerN; ++j) {
                    if (((j - inGapStart + innerN) % innerN) >= inGapLen) {
                        const float deg = -90.0f + static_cast<float>(j) * (360.0f / static_cast<float>(innerN));
                        const float th = deg * kPi / 180.0f;
                        addSlot(std::cos(th) * rIn, std::sin(th) * rIn, deg + 90.0f, outerN + j);
                    }
                }
            } else {
                const int spokes = 6 + sInt(3, 0, 1) * 2;
                int skipA = -1, skipB = -1;
                if (sInt(4, 0, 1) == 1) {
                    skipA = sInt(5, 0, spokes - 1);
                    if (spokes == 8 && sInt(6, 0, 1) == 1) {
                        skipB = (skipA + sInt(7, 1, 3)) % spokes;
                    }
                }
                const float r1 = (spokes == 8) ? 2.35f : 1.85f;
                const float r2 = r1 + 1.65f;
                const float r3 = r2 + 1.65f;
                int idx = 0;
                for (int s = 0; s < spokes; ++s) {
                    if (s == skipA || s == skipB) continue;
                    const float deg = -90.0f + static_cast<float>(s) * (360.0f / static_cast<float>(spokes));
                    const float th = deg * kPi / 180.0f;
                    addSlot(std::cos(th) * r1, std::sin(th) * r1, deg + 90.0f, idx++);
                    addSlot(std::cos(th) * r2, std::sin(th) * r2, deg + 90.0f, idx++);
                    if (spokes == 6) {
                        addSlot(std::cos(th) * r3, std::sin(th) * r3, deg + 90.0f, idx++);
                    }
                }
            }
        } else if (arch == 1) {
            displayName = "FIGURE-8";
            const int asym = sInt(2, 0, 2);
            const int leftN = (asym == 2) ? 8 : 12;
            const int rightN = (asym == 1) ? 8 : 12;
            const float radL = 2.15f + static_cast<float>(leftN - 8) * 0.22f;
            const float radR = 2.15f + static_cast<float>(rightN - 8) * 0.22f;
            const float cxL = -(radL * 1.15f);
            const float cxR = radR * 1.15f;
            const float tiltDeg = (sInt(3, 0, 1) == 1) ? ((sFloat(4) - 0.5f) * 32.0f) : 0.0f;
            const float tiltRad = tiltDeg * kPi / 180.0f;
            const float ct = std::cos(tiltRad), st = std::sin(tiltRad);
            const int gapSide = sInt(5, 0, 2);
            const int gapPos = sInt(6, 2, 6);
            for (int i = 0; i < leftN; ++i) {
                if (gapSide == 1 && (i == gapPos || i == gapPos + 1)) continue;
                const float deg = static_cast<float>(i) * (360.0f / static_cast<float>(leftN));
                const float th = deg * kPi / 180.0f;
                float ux = cxL + std::cos(th) * radL;
                float uy = std::sin(th) * radL;
                float ang = deg + 90.0f;
                if (i == 0) { ux -= 0.12f; uy -= 0.30f; ang = 52.0f; }
                addSlot(ux * ct - uy * st, ux * st + uy * ct, ang + tiltDeg, i);
            }
            for (int j = 0; j < rightN; ++j) {
                if (gapSide == 2 && (j == gapPos || j == gapPos + 1)) continue;
                const float deg = 180.0f - static_cast<float>(j) * (360.0f / static_cast<float>(rightN));
                const float th = deg * kPi / 180.0f;
                float ux = cxR + std::cos(th) * radR;
                float uy = std::sin(th) * radR;
                float ang = deg - 90.0f;
                if (j == 0) { ux += 0.12f; uy += 0.30f; ang = 52.0f; }
                addSlot(ux * ct - uy * st, ux * st + uy * ct, ang + tiltDeg, leftN + j);
            }
        } else if (arch == 2) {
            displayName = "ARCHWAY";
            const int outerN = 10 + sInt(2, 0, 1) * 2;
            const int innerN = 6 + sInt(3, 0, 1) * 2;
            const float rOut = (outerN == 10) ? 4.30f : 4.90f;
            const float rIn = rOut - 1.85f;
            for (int i = 0; i < outerN; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(outerN - 1);
                const float deg = 180.0f + t * 180.0f;
                const float th = deg * kPi / 180.0f;
                addSlot(std::cos(th) * rOut, std::sin(th) * rOut + 1.1f, deg + 90.0f, i);
            }
            for (int j = 0; j < innerN; ++j) {
                const float t = static_cast<float>(j) / static_cast<float>(innerN - 1);
                const float deg = 192.0f + t * 156.0f;
                const float th = deg * kPi / 180.0f;
                addSlot(std::cos(th) * rIn, std::sin(th) * rIn + 1.1f, deg + 90.0f, outerN + j);
            }
            if (sInt(4, 0, 1) == 1) {
                addSlot(-rOut * 0.62f, 2.40f, 0.0f, outerN + innerN);
                addSlot(rOut * 0.62f, 2.40f, 0.0f, outerN + innerN + 1);
            }
        } else if (arch == 3) {
            displayName = "TWIN WAVE";
            const int rows = (sInt(2, 0, 2) == 2) ? 3 : 2;
            const int cols = (rows == 3) ? (6 + sInt(3, 0, 1) * 2) : (8 + sInt(3, 0, 2) * 2);
            const float amp = 0.45f + sFloat(4) * 0.45f;
            const float cycles = (sInt(5, 0, 1) == 1) ? 1.5f : 1.0f;
            const float phase = sFloat(6) * kPi;
            const float xStep = 1.48f, rowGap = 2.05f;
            int idx = 0;
            for (int r = 0; r < rows; ++r) {
                const float yBase = (static_cast<float>(r) - 0.5f * static_cast<float>(rows - 1)) * rowGap;
                for (int i = 0; i < cols; ++i) {
                    const float ux = (static_cast<float>(i) - 0.5f * static_cast<float>(cols - 1)) * xStep;
                    const float t = (static_cast<float>(i) / static_cast<float>(cols - 1)) * cycles * 2.0f * kPi + phase;
                    const float dy = std::sin(t) * amp;
                    const float slope = std::cos(t) * amp * (cycles * 2.0f * kPi / (static_cast<float>(cols - 1) * xStep));
                    const float ang = std::clamp(std::atan2(slope, 1.0f) * 180.0f / kPi, -26.0f, 26.0f);
                    addSlot(ux, yBase + dy, ang, idx++);
                }
            }
        } else if (arch == 4) {
            displayName = "DIAMOND PALACE";
            const int cutMode = sInt(2, 0, 3);
            const float dx = 1.30f, dy = 1.66f;
            int idx = 0;
            for (int gy = -3; gy <= 3; ++gy) {
                for (int gx = -3; gx <= 3; ++gx) {
                    const int dist = std::abs(gx) + std::abs(gy);
                    if (dist > 3) continue;
                    bool keep = true;
                    if (cutMode == 0) {
                        if (dist == 0) keep = false;
                        else if (dist == 1 && sInt(10 + idx, 0, 2) == 0) keep = false;
                    } else if (cutMode == 1) {
                        if (dist == 3 && sInt(20 + idx, 0, 2) == 0) keep = false;
                        else if (dist == 0) keep = false;
                    } else if (cutMode == 2) {
                        if ((dist == 0 || dist == 1) && sInt(30 + idx, 0, 1) == 0) keep = false;
                        else if (dist == 3 && sInt(40 + idx, 0, 2) == 0) keep = false;
                    } else {
                        if (gx == 0 && gy == 0) keep = false;
                        else if (((gx + gy) == 3 || (gx - gy) == -3) && sInt(50 + idx, 0, 1) == 0) keep = false;
                    }
                    if (keep) {
                        const float ang = (dist == 3 && gx != 0 && gy != 0) ? static_cast<float>(gx * gy) * 4.0f : 0.0f;
                        addSlot(static_cast<float>(gx) * dx, static_cast<float>(gy) * dy, ang, idx);
                    }
                    ++idx;
                }
            }
        } else if (arch == 5) {
            displayName = "SYMMETRY";
            const int motif = sInt(2, 0, 4);
            const float tilt = (sFloat(3) - 0.5f) * 24.0f;
            if (motif == 0) {
                addSlot(0.82f, 0.95f, tilt, 0);
                addSlot(2.12f, 1.48f, tilt * 0.8f, 1);
                addSlot(3.42f, 2.00f, tilt * 0.6f, 2);
                addSlot(1.48f, 2.80f, -tilt * 0.5f, 3);
                addSlot(2.90f, 3.45f, -tilt * 0.5f, 4);
            } else if (motif == 1) {
                addSlot(0.78f, 0.92f, 0.0f, 0);
                addSlot(2.04f, 0.92f, 0.0f, 1);
                addSlot(3.30f, 0.92f, 0.0f, 2);
                addSlot(1.42f, 2.58f, 0.0f, 3);
                addSlot(2.68f, 2.58f, 0.0f, 4);
                addSlot(2.04f, 4.22f, 0.0f, 5);
            } else if (motif == 2) {
                addSlot(0.85f, 1.05f, 18.0f, 0);
                addSlot(2.20f, 0.95f, 6.0f, 1);
                addSlot(3.55f, 1.58f, -24.0f, 2);
                addSlot(3.92f, 3.10f, -52.0f, 3);
                addSlot(2.45f, 3.20f, -18.0f, 4);
                addSlot(0.95f, 2.78f, 10.0f, 5);
            } else if (motif == 3) {
                addSlot(0.80f, 0.95f, 0.0f, 0);
                addSlot(0.80f, 2.60f, 0.0f, 1);
                addSlot(2.28f, 1.78f, 15.0f, 2);
                addSlot(3.60f, 0.95f, -15.0f, 3);
                addSlot(3.60f, 2.60f, 15.0f, 4);
            } else {
                addSlot(0.85f, 0.95f, 12.0f, 0);
                addSlot(2.25f, 0.95f, 24.0f, 1);
                addSlot(1.65f, 2.55f, -12.0f, 2);
                addSlot(3.25f, 2.35f, -24.0f, 3);
                addSlot(0.85f, 3.85f, 8.0f, 4);
            }
            // 4-way quad mirror
            const size_t n1 = layoutSlots_.size();
            for (size_t i = 0; i < n1; ++i) {
                const auto s = layoutSlots_[i];
                if (std::abs(s.ux) > 0.05f) addSlot(-s.ux, s.uy, -s.deg, static_cast<int32_t>(layoutSlots_.size()));
            }
            const size_t n2 = layoutSlots_.size();
            for (size_t i = 0; i < n2; ++i) {
                const auto s = layoutSlots_[i];
                if (std::abs(s.uy) > 0.05f) addSlot(s.ux, -s.uy, -s.deg, static_cast<int32_t>(layoutSlots_.size()));
            }
        } else {
            displayName = "GRID";
            const int preset = sInt(2, 0, 5);
            int cols = 4, rows = 4, carve = 0;
            if (preset == 0) { cols = 4; rows = 3; }
            else if (preset == 1) { cols = 4; rows = 4; }
            else if (preset == 2) { cols = 6; rows = 3; }
            else if (preset == 3) { cols = 5; rows = 4; }
            else if (preset == 4) { cols = 6; rows = 4; }
            else { cols = 5; rows = 3; carve = 1; }
            if (preset >= 1 && preset <= 4 && sInt(3, 0, 2) == 2) carve = 2;
            const int total = cols * rows;
            const int skipA = sInt(4, 0, total - 1);
            const int skipB = (skipA + sInt(5, 1, total - 2)) % total;
            int idx = 0;
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    bool keep = true;
                    if (carve == 1 && r == 1 && c == 2) keep = false;
                    else if (carve == 2 && (idx == skipA || idx == skipB)) keep = false;
                    if (keep) {
                        addSlot((static_cast<float>(c) - 0.5f * static_cast<float>(cols - 1)) * 1.22f,
                                (static_cast<float>(r) - 0.5f * static_cast<float>(rows - 1)) * 1.62f,
                                0.0f, idx);
                    }
                    ++idx;
                }
            }
        }
        return Value(std::string(displayName));
    });

    evaluator_.registerFunction("layout_apply", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 3 || !boundCtx_ || !boundCtx_->piles || layoutSlots_.empty()) return Value(false);
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        constexpr float kAspect = 1.40f;

        const int firstZone = args[0].asInt(1);
        const float bw = static_cast<float>(args[1].asNumber(boundCtx_->viewportW));
        const float bh = static_cast<float>(args[2].asNumber(boundCtx_->viewportH));
        const float padTop = (args.size() >= 4) ? static_cast<float>(args[3].asNumber(boundCtx_->safeTop + 20.0f)) : (boundCtx_->safeTop + 20.0f);
        const float padBot = (args.size() >= 5) ? static_cast<float>(args[4].asNumber(boundCtx_->safeBottom + 48.0f)) : (boundCtx_->safeBottom + 48.0f);
        const float padX = (args.size() >= 6) ? static_cast<float>(args[5].asNumber(std::max(boundCtx_->safeLeft, boundCtx_->safeRight) + 20.0f))
                                              : (std::max(boundCtx_->safeLeft, boundCtx_->safeRight) + 20.0f);

        const float availW = std::max(180.0f, bw - 2.0f * padX);
        const float availH = std::max(180.0f, bh - padTop - padBot);

        // Compute exact rotated bounding box in card-width units
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (const auto& s : layoutSlots_) {
            const float rad = s.deg * kDegToRad;
            const float ca = std::abs(std::cos(rad));
            const float sa = std::abs(std::sin(rad));
            const float halfW = 0.5f * ca + 0.5f * kAspect * sa;
            const float halfH = 0.5f * sa + 0.5f * kAspect * ca;
            minX = std::min(minX, s.ux - halfW);
            maxX = std::max(maxX, s.ux + halfW);
            minY = std::min(minY, s.uy - halfH);
            maxY = std::max(maxY, s.uy + halfH);
        }

        const float spanW = std::max(1.0f, maxX - minX);
        const float spanH = std::max(kAspect, maxY - minY);
        const float cw = std::clamp(std::min(availW / spanW, availH / spanH), 34.0f, 152.0f);
        const float ch = std::round(cw * kAspect);
        boundCtx_->cardW = cw;
        boundCtx_->cardH = ch;

        const float centerX = padX + availW * 0.5f;
        const float centerY = padTop + availH * 0.5f;
        const float midUx = 0.5f * (minX + maxX);
        const float midUy = 0.5f * (minY + maxY);

        for (size_t i = 0; i < layoutSlots_.size(); ++i) {
            const int zIdx = firstZone + static_cast<int>(i);
            if (!validZone(boundCtx_, zIdx)) break;
            const auto& s = layoutSlots_[i];
            Pile& p = (*boundCtx_->piles)[static_cast<size_t>(zIdx)];
            p.x = centerX + (s.ux - midUx) * cw - cw * 0.5f;
            p.y = centerY + (s.uy - midUy) * cw - ch * 0.5f;
            p.angle = s.deg * kDegToRad;
            p.zIndex = s.z;
        }
        return Value(true);
    });
}

// --- Hook Invocations ---

Value ScriptRuleEngine::invokeHook(const std::string& name, const std::vector<Value>& args) {
    auto it = boundCallables_.find(name);
    if (it != boundCallables_.end() && it->second.isFunction()) {
        return evaluator_.invokeCallable(it->second, args);
    }
    return evaluator_.callFunction(name, args);
}

bool ScriptRuleEngine::invokeOnConfigure(ScriptBoardContext& ctx, uint32_t layoutSeed) {
    if (!hasOnConfigure_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_configure", {Value(static_cast<double>(layoutSeed))});
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnDeal(ScriptBoardContext& ctx) {
    if (!hasOnDeal_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_deal");
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeCanPick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx) {
    if (!hasCanPick_) return false;
    bindContext(&ctx);
    Value res = invokeHook("can_pick", {Value(static_cast<int>(zoneIdx)), Value(static_cast<int>(cardIdx))});
    bindContext(nullptr);
    return res.asBoolean(false);
}

bool ScriptRuleEngine::invokeCanDrop(ScriptBoardContext& ctx, uint8_t fromZone, size_t cardIdx, uint8_t toZone) {
    if (!hasCanDrop_) return false;
    bindContext(&ctx);
    Value res = invokeHook("can_drop", {
        Value(static_cast<int>(fromZone)),
        Value(static_cast<int>(cardIdx)),
        Value(static_cast<int>(toZone))
    });
    bindContext(nullptr);
    return res.asBoolean(false);
}

bool ScriptRuleEngine::invokeOnDrop(ScriptBoardContext& ctx, uint8_t fromZone, size_t cardIdx, uint8_t toZone) {
    if (!hasOnDrop_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_drop", {
        Value(static_cast<int>(fromZone)),
        Value(static_cast<int>(cardIdx)),
        Value(static_cast<int>(toZone))
    });
    bindContext(nullptr);
    return res.asBoolean(false);
}

bool ScriptRuleEngine::invokeCanTarget(ScriptBoardContext& ctx, uint8_t fromZone, size_t fromIdx, uint8_t toZone, size_t toIdx) {
    if (!hasCanTarget_) return false;
    bindContext(&ctx);
    Value res = invokeHook("can_target", {
        Value(static_cast<int>(fromZone)),
        Value(static_cast<int>(fromIdx)),
        Value(static_cast<int>(toZone)),
        Value(static_cast<int>(toIdx))
    });
    bindContext(nullptr);
    return res.asBoolean(false);
}

bool ScriptRuleEngine::invokeOnTarget(ScriptBoardContext& ctx, uint8_t fromZone, size_t fromIdx, uint8_t toZone, size_t toIdx) {
    if (!hasOnTarget_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_target", {
        Value(static_cast<int>(fromZone)),
        Value(static_cast<int>(fromIdx)),
        Value(static_cast<int>(toZone)),
        Value(static_cast<int>(toIdx))
    });
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnCardClick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx) {
    if (!hasOnCardClick_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_card_click", {
        Value(static_cast<int>(zoneIdx)),
        Value(static_cast<int>(cardIdx))
    });
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnStockClick(ScriptBoardContext& ctx) {
    if (!hasOnStockClick_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_stock_click");
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnTurnEnd(ScriptBoardContext& ctx) {
    if (!hasOnTurnEnd_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_turn_end");
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnVictory(ScriptBoardContext& ctx) {
    if (!hasOnVictory_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_victory");
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnDemoStep(ScriptBoardContext& ctx) {
    if (!hasOnDemoStep_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_demo_step");
    bindContext(nullptr);
    return res.asBoolean(false);
}

bool ScriptRuleEngine::invokeOnLayout(ScriptBoardContext& ctx, float boardW, float boardH) {
    if (!hasOnLayout_) return false;
    bindContext(&ctx);
    Value res = invokeHook("on_layout", {
        Value(static_cast<double>(boardW)),
        Value(static_cast<double>(boardH))
    });
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeNamedAction(ScriptBoardContext& ctx, const std::string& fnName) {
    if (!loaded_ || fnName.empty()) return false;
    bindContext(&ctx);
    Value res = invokeHook(fnName);
    bindContext(nullptr);
    return res.asBoolean(true);
}

GameStatus ScriptRuleEngine::invokeGetStatus(ScriptBoardContext& ctx) {
    if (!hasGetStatus_) return GameStatus::Playing;
    bindContext(&ctx);
    Value res = invokeHook("get_status");
    bindContext(nullptr);
    const std::string& s = res.asString();
    if (s == "victory" || s == "win") return GameStatus::Victory;
    if (s == "defeat" || s == "loss" || s == "lose") return GameStatus::Defeat;
    return GameStatus::Playing;
}

int32_t ScriptRuleEngine::invokeEvalHeuristic(ScriptBoardContext& ctx) {
    if (!hasEvalHeuristic_) return 0;
    bindContext(&ctx);
    Value res = invokeHook("eval_heuristic");
    bindContext(nullptr);
    return res.asInt(0);
}

// --- Atomic BoardOp Apply / Revert ---

bool ScriptRuleEngine::applyOp(ScriptBoardContext& ctx, const BoardOp& op) {
    switch (op.type) {
        case BoardOpType::MoveCards: {
            if (!validZone(&ctx, op.fromPile) || !validZone(&ctx, op.toPile)) return false;
            auto& src = (*ctx.piles)[op.fromPile].cards;
            auto& dstPile = (*ctx.piles)[op.toPile];
            auto& dst = dstPile.cards;
            if (op.cardIdx > src.size() || op.cardIdx + op.count > src.size()) return false;
            const size_t insertIdx = op.nextBool ? 0 : dst.size();
            dst.insert(dst.begin() + insertIdx, src.begin() + op.cardIdx, src.begin() + op.cardIdx + op.count);
            for (size_t k = 0; k < op.count; ++k) {
                dst[insertIdx + k].targetX = dstPile.x;
                dst[insertIdx + k].targetY = dstPile.y;
                dst[insertIdx + k].targetAngle = dstPile.angle;
                dst[insertIdx + k].elevation = std::max(dst[insertIdx + k].elevation, 9.0f);
            }
            src.erase(src.begin() + op.cardIdx, src.begin() + op.cardIdx + op.count);
            return true;
        }
        case BoardOpType::FlipCard: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            auto& card = (*ctx.piles)[op.fromPile].cards[op.cardIdx];
            const bool wasFaceUp = card.faceUp;
            card.faceUp = op.nextBool;
            if (!wasFaceUp && op.nextBool) {
                constexpr float kPi = 3.14159265358979323846f;
                card.flipAngle = kPi;
                card.elevation = std::max(card.elevation, 9.0f);
            } else if (wasFaceUp && !op.nextBool) {
                card.flipAngle = 0.0f;
            }
            return true;
        }
        case BoardOpType::RemoveCard: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            auto& cards = (*ctx.piles)[op.fromPile].cards;
            cards.erase(cards.begin() + op.cardIdx);
            return true;
        }
        case BoardOpType::SpawnCard: {
            if (!validZone(&ctx, op.toPile)) return false;
            auto& cards = (*ctx.piles)[op.toPile].cards;
            size_t insertPos = std::min(static_cast<size_t>(op.cardIdx), cards.size());
            cards.insert(cards.begin() + insertPos, op.snapshotCard);
            return true;
        }
        case BoardOpType::SetCardStat: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            (*ctx.piles)[op.fromPile].cards[op.cardIdx].setStat(op.key, op.nextInt);
            return true;
        }
        case BoardOpType::AddCardTag: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            (*ctx.piles)[op.fromPile].cards[op.cardIdx].addTag(op.key);
            return true;
        }
        case BoardOpType::RemoveCardTag: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            (*ctx.piles)[op.fromPile].cards[op.cardIdx].removeTag(op.key);
            return true;
        }
        case BoardOpType::SetGlobalStat: {
            if (!ctx.blackboard) return false;
            ctx.blackboard->setInt(op.key, op.nextInt);
            return true;
        }
        case BoardOpType::SetGlobalStr: {
            if (!ctx.blackboard) return false;
            ctx.blackboard->setStr(op.key, op.nextStr);
            return true;
        }
        case BoardOpType::AddScore: {
            if (!ctx.score) return false;
            *ctx.score += op.nextInt;
            return true;
        }
        case BoardOpType::ShuffleZone: {
            if (!validZone(&ctx, op.fromPile)) return false;
            (*ctx.piles)[op.fromPile].cards = op.nextCards;
            return true;
        }
    }
    return false;
}

bool ScriptRuleEngine::revertOp(ScriptBoardContext& ctx, const BoardOp& op) {
    switch (op.type) {
        case BoardOpType::MoveCards: {
            if (!validZone(&ctx, op.fromPile) || !validZone(&ctx, op.toPile)) return false;
            auto& src = (*ctx.piles)[op.fromPile].cards;
            auto& dst = (*ctx.piles)[op.toPile].cards;
            if (dst.size() < op.count) return false;
            size_t dstStart = op.nextBool ? 0 : (dst.size() - op.count);
            size_t insertPos = std::min(static_cast<size_t>(op.cardIdx), src.size());
            src.insert(src.begin() + insertPos, dst.begin() + dstStart, dst.begin() + dstStart + op.count);
            dst.erase(dst.begin() + dstStart, dst.begin() + dstStart + op.count);
            return true;
        }
        case BoardOpType::FlipCard: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            auto& card = (*ctx.piles)[op.fromPile].cards[op.cardIdx];
            card.faceUp = op.prevBool;
            if (!op.prevBool) {
                card.flipAngle = 0.0f;
            }
            return true;
        }
        case BoardOpType::RemoveCard: {
            if (!validZone(&ctx, op.fromPile)) return false;
            auto& cards = (*ctx.piles)[op.fromPile].cards;
            size_t insertPos = std::min(static_cast<size_t>(op.cardIdx), cards.size());
            cards.insert(cards.begin() + insertPos, op.snapshotCard);
            return true;
        }
        case BoardOpType::SpawnCard: {
            if (!validCard(&ctx, op.toPile, static_cast<int>(op.cardIdx))) return false;
            auto& cards = (*ctx.piles)[op.toPile].cards;
            cards.erase(cards.begin() + op.cardIdx);
            return true;
        }
        case BoardOpType::SetCardStat: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            (*ctx.piles)[op.fromPile].cards[op.cardIdx].setStat(op.key, op.prevInt);
            return true;
        }
        case BoardOpType::AddCardTag: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            if (!op.prevBool) {
                (*ctx.piles)[op.fromPile].cards[op.cardIdx].removeTag(op.key);
            }
            return true;
        }
        case BoardOpType::RemoveCardTag: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            if (op.prevBool) {
                (*ctx.piles)[op.fromPile].cards[op.cardIdx].addTag(op.key);
            }
            return true;
        }
        case BoardOpType::SetGlobalStat: {
            if (!ctx.blackboard) return false;
            ctx.blackboard->setInt(op.key, op.prevInt);
            return true;
        }
        case BoardOpType::SetGlobalStr: {
            if (!ctx.blackboard) return false;
            ctx.blackboard->setStr(op.key, op.prevStr);
            return true;
        }
        case BoardOpType::AddScore: {
            if (!ctx.score) return false;
            *ctx.score -= op.nextInt;
            return true;
        }
        case BoardOpType::ShuffleZone: {
            if (!validZone(&ctx, op.fromPile)) return false;
            (*ctx.piles)[op.fromPile].cards = op.prevCards;
            return true;
        }
    }
    return false;
}

void ScriptRuleEngine::applyTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx) {
    for (const auto& op : tx.ops) {
        applyOp(ctx, op);
    }
    if (tx.hasWidgetSnapshot && ctx.blackboard) {
        ctx.blackboard->widgetCards = tx.nextWidgetCards;
        ctx.blackboard->badges = tx.nextBadges;
    }
}

void ScriptRuleEngine::revertTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx) {
    for (auto it = tx.ops.rbegin(); it != tx.ops.rend(); ++it) {
        revertOp(ctx, *it);
    }
    if (tx.hasWidgetSnapshot && ctx.blackboard) {
        ctx.blackboard->widgetCards = tx.prevWidgetCards;
        ctx.blackboard->badges = tx.prevBadges;
    }
}

bool ScriptRuleEngine::isCardBlockedByPile(const std::vector<Pile>& piles, size_t pileIdx, size_t cardIdx, size_t otherPileIdx, float cardW, float cardH) noexcept {
    if (pileIdx >= piles.size() || otherPileIdx >= piles.size() || pileIdx == otherPileIdx) return false;
    const Pile& targetPile = piles[pileIdx];
    const Pile& otherPile = piles[otherPileIdx];
    if (cardIdx >= targetPile.cards.size() || otherPile.cards.empty()) return false;
    if (targetPile.type == PileType::Tableau && otherPile.type != PileType::Tableau) return false;

    const bool otherAbove = (otherPile.zIndex > targetPile.zIndex) ||
                            (otherPile.zIndex == targetPile.zIndex && otherPileIdx > pileIdx);
    if (!otherAbove) return false;

    const Card& targetCard = targetPile.cards[cardIdx];
    const float inset = std::max(2.0f, cardW * 0.04f);
    for (const Card& otherCard : otherPile.cards) {
        if (cardsOverlapOBB(targetCard.targetX, targetCard.targetY, targetCard.targetAngle,
                            otherCard.targetX, otherCard.targetY, otherCard.targetAngle,
                            cardW, cardH, inset)) {
            return true;
        }
    }
    return false;
}

bool ScriptRuleEngine::isCardBlocked(const std::vector<Pile>& piles, size_t pileIdx, size_t cardIdx, float cardW, float cardH) noexcept {
    if (pileIdx >= piles.size()) return false;
    const Pile& targetPile = piles[pileIdx];
    if (cardIdx >= targetPile.cards.size()) return false;

    // 1. Buried under a higher card in the same pile
    if (cardIdx + 1 < targetPile.cards.size()) {
        return true;
    }

    // 2. Occluded by any card in a higher-zIndex (or later-index at equal zIndex) pile
    for (size_t otherIdx = 0; otherIdx < piles.size(); ++otherIdx) {
        if (isCardBlockedByPile(piles, pileIdx, cardIdx, otherIdx, cardW, cardH)) {
            return true;
        }
    }
    return false;
}

} // namespace solengine
