#include "solengine/script_rule_engine.hpp"
#include <algorithm>
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
    , hasOnDeal_(other.hasOnDeal_)
    , hasCanPick_(other.hasCanPick_)
    , hasCanDrop_(other.hasCanDrop_)
    , hasOnDrop_(other.hasOnDrop_)
    , hasCanTarget_(other.hasCanTarget_)
    , hasOnTarget_(other.hasOnTarget_)
    , hasOnCardClick_(other.hasOnCardClick_)
    , hasOnStockClick_(other.hasOnStockClick_)
    , hasOnTurnEnd_(other.hasOnTurnEnd_)
    , hasGetStatus_(other.hasGetStatus_)
    , hasEvalHeuristic_(other.hasEvalHeuristic_)
    , docManifest_(other.docManifest_) {
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
    hasOnDeal_ = other.hasOnDeal_;
    hasCanPick_ = other.hasCanPick_;
    hasCanDrop_ = other.hasCanDrop_;
    hasOnDrop_ = other.hasOnDrop_;
    hasCanTarget_ = other.hasCanTarget_;
    hasOnTarget_ = other.hasOnTarget_;
    hasOnCardClick_ = other.hasOnCardClick_;
    hasOnStockClick_ = other.hasOnStockClick_;
    hasOnTurnEnd_ = other.hasOnTurnEnd_;
    hasGetStatus_ = other.hasGetStatus_;
    hasEvalHeuristic_ = other.hasEvalHeuristic_;
    docManifest_ = other.docManifest_;
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
    , hasOnDeal_(other.hasOnDeal_)
    , hasCanPick_(other.hasCanPick_)
    , hasCanDrop_(other.hasCanDrop_)
    , hasOnDrop_(other.hasOnDrop_)
    , hasCanTarget_(other.hasCanTarget_)
    , hasOnTarget_(other.hasOnTarget_)
    , hasOnCardClick_(other.hasOnCardClick_)
    , hasOnStockClick_(other.hasOnStockClick_)
    , hasOnTurnEnd_(other.hasOnTurnEnd_)
    , hasGetStatus_(other.hasGetStatus_)
    , hasEvalHeuristic_(other.hasEvalHeuristic_)
    , docManifest_(std::move(other.docManifest_)) {
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
    hasOnDeal_ = other.hasOnDeal_;
    hasCanPick_ = other.hasCanPick_;
    hasCanDrop_ = other.hasCanDrop_;
    hasOnDrop_ = other.hasOnDrop_;
    hasCanTarget_ = other.hasCanTarget_;
    hasOnTarget_ = other.hasOnTarget_;
    hasOnCardClick_ = other.hasOnCardClick_;
    hasOnStockClick_ = other.hasOnStockClick_;
    hasOnTurnEnd_ = other.hasOnTurnEnd_;
    hasGetStatus_ = other.hasGetStatus_;
    hasEvalHeuristic_ = other.hasEvalHeuristic_;
    docManifest_ = std::move(other.docManifest_);
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
    hasOnDeal_ = false;
    hasCanPick_ = false;
    hasCanDrop_ = false;
    hasOnDrop_ = false;
    hasCanTarget_ = false;
    hasOnTarget_ = false;
    hasOnCardClick_ = false;
    hasOnStockClick_ = false;
    hasOnTurnEnd_ = false;
    hasGetStatus_ = false;
    hasEvalHeuristic_ = false;
    docManifest_.clear();
}

bool ScriptRuleEngine::hasAnyHooks() const noexcept {
    return hasOnDeal_ || hasCanPick_ || hasCanDrop_ || hasOnDrop_ || hasCanTarget_ ||
           hasOnTarget_ || hasOnCardClick_ || hasOnStockClick_ ||
           hasOnTurnEnd_ || hasGetStatus_;
}

bool ScriptRuleEngine::loadScriptFile(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        lastError_ = "Failed to open script file: " + filePath;
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return loadScript(ss.str());
}

bool ScriptRuleEngine::loadScript(const std::string& source) {
    reset();
    registerHostApi();
    evaluator_.evaluateSource(source);
    if (!evaluator_.getLastError().empty()) {
        lastError_ = evaluator_.getLastError();
        return false;
    }

    hasOnDeal_      = evaluator_.hasFunction("on_deal");
    hasCanPick_     = evaluator_.hasFunction("can_pick");
    hasCanDrop_     = evaluator_.hasFunction("can_drop");
    hasOnDrop_      = evaluator_.hasFunction("on_drop");
    hasCanTarget_   = evaluator_.hasFunction("can_target");
    hasOnTarget_    = evaluator_.hasFunction("on_target");
    hasOnCardClick_ = evaluator_.hasFunction("on_card_click");
    hasOnStockClick_= evaluator_.hasFunction("on_stock_click");
    hasOnTurnEnd_   = evaluator_.hasFunction("on_turn_end");
    hasGetStatus_   = evaluator_.hasFunction("get_status");
    hasEvalHeuristic_ = evaluator_.hasFunction("eval_heuristic");

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
}

// --- Hook Invocations ---

bool ScriptRuleEngine::invokeOnDeal(ScriptBoardContext& ctx) {
    if (!hasOnDeal_) return false;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("on_deal");
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeCanPick(ScriptBoardContext& ctx, uint8_t zoneIdx, size_t cardIdx) {
    if (!hasCanPick_) return false;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("can_pick", {Value(static_cast<int>(zoneIdx)), Value(static_cast<int>(cardIdx))});
    bindContext(nullptr);
    return res.asBoolean(false);
}

bool ScriptRuleEngine::invokeCanDrop(ScriptBoardContext& ctx, uint8_t fromZone, size_t cardIdx, uint8_t toZone) {
    if (!hasCanDrop_) return false;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("can_drop", {
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
    Value res = evaluator_.callFunction("on_drop", {
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
    Value res = evaluator_.callFunction("can_target", {
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
    Value res = evaluator_.callFunction("on_target", {
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
    Value res = evaluator_.callFunction("on_card_click", {
        Value(static_cast<int>(zoneIdx)),
        Value(static_cast<int>(cardIdx))
    });
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnStockClick(ScriptBoardContext& ctx) {
    if (!hasOnStockClick_) return false;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("on_stock_click");
    bindContext(nullptr);
    return res.asBoolean(true);
}

bool ScriptRuleEngine::invokeOnTurnEnd(ScriptBoardContext& ctx) {
    if (!hasOnTurnEnd_) return false;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("on_turn_end");
    bindContext(nullptr);
    return res.asBoolean(true);
}

GameStatus ScriptRuleEngine::invokeGetStatus(ScriptBoardContext& ctx) {
    if (!hasGetStatus_) return GameStatus::Playing;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("get_status");
    bindContext(nullptr);
    const std::string& s = res.asString();
    if (s == "victory" || s == "win") return GameStatus::Victory;
    if (s == "defeat" || s == "loss" || s == "lose") return GameStatus::Defeat;
    return GameStatus::Playing;
}

int32_t ScriptRuleEngine::invokeEvalHeuristic(ScriptBoardContext& ctx) {
    if (!hasEvalHeuristic_) return 0;
    bindContext(&ctx);
    Value res = evaluator_.callFunction("eval_heuristic");
    bindContext(nullptr);
    return res.asInt(0);
}

// --- Atomic BoardOp Apply / Revert ---

bool ScriptRuleEngine::applyOp(ScriptBoardContext& ctx, const BoardOp& op) {
    switch (op.type) {
        case BoardOpType::MoveCards: {
            if (!validZone(&ctx, op.fromPile) || !validZone(&ctx, op.toPile)) return false;
            auto& src = (*ctx.piles)[op.fromPile].cards;
            auto& dst = (*ctx.piles)[op.toPile].cards;
            if (op.cardIdx > src.size() || op.cardIdx + op.count > src.size()) return false;
            if (op.nextBool) {
                dst.insert(dst.begin(), src.begin() + op.cardIdx, src.begin() + op.cardIdx + op.count);
            } else {
                dst.insert(dst.end(), src.begin() + op.cardIdx, src.begin() + op.cardIdx + op.count);
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

} // namespace solengine
