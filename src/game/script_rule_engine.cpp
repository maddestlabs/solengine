#include "solengine/script_rule_engine.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace solengine {

using eatsbits::eatscript::Value;

namespace {

uint32_t nextMulberry32(uint32_t& state) noexcept {
    state += 0x6D2B79F5u;
    uint32_t t = state;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

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
    const float hw = std::max(1.0f, w * 0.5f - inset);
    const float hh = std::max(1.0f, h * 0.5f - inset);

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
                    docManifest_.zones.push_back(std::move(zd));
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

    evaluator_.registerFunction("rand_int", [this](const std::vector<Value>& args, const std::map<std::string, Value>&) -> Value {
        if (args.size() < 2 || !boundCtx_ || !boundCtx_->rngState) return Value(0);
        int lo = args[0].asInt(0);
        int hi = args[1].asInt(0);
        if (hi <= lo) return Value(lo);
        uint32_t span = static_cast<uint32_t>(hi - lo + 1);
        uint32_t r = nextMulberry32(*boundCtx_->rngState) % span;
        return Value(lo + static_cast<int>(r));
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
}

// --- Hook Invocations ---

Value ScriptRuleEngine::invokeHook(const std::string& name, const std::vector<Value>& args) {
    auto it = boundCallables_.find(name);
    if (it != boundCallables_.end() && it->second.isFunction()) {
        return evaluator_.invokeCallable(it->second, args);
    }
    return evaluator_.callFunction(name, args);
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
            }
            src.erase(src.begin() + op.cardIdx, src.begin() + op.cardIdx + op.count);
            return true;
        }
        case BoardOpType::FlipCard: {
            if (!validCard(&ctx, op.fromPile, static_cast<int>(op.cardIdx))) return false;
            (*ctx.piles)[op.fromPile].cards[op.cardIdx].faceUp = op.nextBool;
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
            (*ctx.piles)[op.fromPile].cards[op.cardIdx].faceUp = op.prevBool;
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
    }
    return false;
}

void ScriptRuleEngine::applyTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx) {
    for (const auto& op : tx.ops) {
        applyOp(ctx, op);
    }
}

void ScriptRuleEngine::revertTransaction(ScriptBoardContext& ctx, const TurnTransaction& tx) {
    for (auto it = tx.ops.rbegin(); it != tx.ops.rend(); ++it) {
        revertOp(ctx, *it);
    }
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
    const Card& targetCard = targetPile.cards[cardIdx];
    const float inset = std::max(2.0f, cardW * 0.04f);

    for (size_t otherIdx = 0; otherIdx < piles.size(); ++otherIdx) {
        if (otherIdx == pileIdx) continue;
        const Pile& otherPile = piles[otherIdx];
        if (otherPile.cards.empty()) continue;

        const bool otherAbove = (otherPile.zIndex > targetPile.zIndex) ||
                                (otherPile.zIndex == targetPile.zIndex && otherIdx > pileIdx);
        if (!otherAbove) continue;

        for (const Card& otherCard : otherPile.cards) {
            if (cardsOverlapOBB(targetCard.targetX, targetCard.targetY, targetCard.targetAngle,
                                otherCard.targetX, otherCard.targetY, otherCard.targetAngle,
                                cardW, cardH, inset)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace solengine
