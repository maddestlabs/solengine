#include "solengine/solitaire_board.hpp"
#include <algorithm>
#include <cmath>

namespace solengine {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

SolitaireBoard::SolitaireBoard() {
    newDeal(2026u, GameVariant::KlondikeDraw1);
}

uint32_t SolitaireBoard::nextRandom() noexcept {
    // Mulberry32 deterministic PRNG matching Eatsbits & Eatscharted world seeds
    uint32_t z = (rngState_ += 0x6D2B79F5u);
    z = (z ^ (z >> 15)) * (z | 1u);
    z ^= z + (z ^ (z >> 7)) * (z | 61u);
    return z ^ (z >> 14);
}

const char* SolitaireBoard::variantName(GameVariant v) noexcept {
    switch (v) {
        case GameVariant::KlondikeDraw1: return "KLONDIKE (DRAW 1)";
        case GameVariant::KlondikeDraw3: return "KLONDIKE (DRAW 3)";
        case GameVariant::FreeCell:      return "FREECELL";
        case GameVariant::Spider1Suit:   return "SPIDER (1 SUIT)";
        case GameVariant::Spider2Suits:  return "SPIDER (2 SUITS)";
        case GameVariant::Spider3Suits:  return "SPIDER (3 SUITS)";
        case GameVariant::Spider4Suits:  return "SPIDER (4 SUITS)";
        case GameVariant::Golf:          return "GOLF";
        case GameVariant::CustomScript:  return "CUSTOM SCRIPT";
        default:                         return "KLONDIKE";
    }
}

const char* SolitaireBoard::layoutModeName(ResponsiveLayoutMode m) noexcept {
    switch (m) {
        case ResponsiveLayoutMode::Auto:            return "AUTO (ADAPTIVE)";
        case ResponsiveLayoutMode::DesktopStandard: return "DESKTOP (CLASSIC)";
        case ResponsiveLayoutMode::MobileCompact:   return "MOBILE (COMPACT)";
        default: return "AUTO (ADAPTIVE)";
    }
}

const char* SolitaireBoard::stockPositionName(StockPosition p) noexcept {
    switch (p) {
        case StockPosition::Auto:        return "AUTO (SMART)";
        case StockPosition::TopLeft:     return "TOP-LEFT (CLASSIC)";
        case StockPosition::BottomRight: return "BOT-RIGHT (THUMB)";
        case StockPosition::BottomLeft:  return "BOT-LEFT (LEFTY)";
        default: return "AUTO (SMART)";
    }
}

void SolitaireBoard::setLayoutMode(ResponsiveLayoutMode mode) noexcept {
    layoutMode_ = mode;
    computeLayout(viewportW_, viewportH_);
}

void SolitaireBoard::setStockPosition(StockPosition pos) noexcept {
    stockPosition_ = pos;
    computeLayout(viewportW_, viewportH_);
}

bool SolitaireBoard::isMobileLayout() const noexcept {
    if (layoutMode_ == ResponsiveLayoutMode::MobileCompact) return true;
    if (layoutMode_ == ResponsiveLayoutMode::DesktopStandard) return false;
    const float aspect = viewportW_ / std::max(1.0f, viewportH_);
    return (aspect < 1.05f) || (viewportW_ < 600.0f);
}

const char* SolitaireBoard::themeName(TableThemeId t) noexcept {
    switch (t) {
        case TableThemeId::PastelTan:       return "PASTEL";
        case TableThemeId::EmeraldLawn:     return "EMERALD LAWN";
        case TableThemeId::HopsackTweed:    return "HOPSACK TWEED";
        case TableThemeId::DenimChambray:   return "DENIM CHAMBRAY";
        case TableThemeId::StripedJersey:   return "STRIPED JERSEY";
        case TableThemeId::SunkenSanctum:   return "SUNKEN SANCTUM";
        case TableThemeId::PerilousShores:  return "PERILOUS SHORES";
        case TableThemeId::PebbledCowhide:  return "PEBBLED COWHIDE";
        case TableThemeId::LeatherNubuck:   return "DISTRESSED LEATHER";
        case TableThemeId::PaintedWood:     return "PAINTED WOOD";
        case TableThemeId::PixelDungeon:    return "PIXEL DUNGEON";
        case TableThemeId::PixelWater:            return "PIXEL WATER";
        case TableThemeId::ShaderVelvetNoir:      return "VELVET NOIR";
        case TableThemeId::ShaderCarraraMarble:   return "CARRARA MARBLE";
        case TableThemeId::ShaderWateredSilk:     return "WATERED SILK";
        case TableThemeId::ShaderPoolCaustics:    return "POOL CAUSTICS";
        case TableThemeId::ShaderArcaneParchment: return "ARCANE PARCHMENT";
        case TableThemeId::ShaderCosmicNebula:    return "COSMIC NEBULA";
        case TableThemeId::ShaderSynthwaveSunset: return "SYNTHWAVE SUNSET";
        case TableThemeId::ShaderTopographic:     return "TOPOGRAPHIC MAP";
        default:                                  return "EMERALD LAWN";
    }
}

bool SolitaireBoard::restoreSessionState(
    uint32_t seed,
    uint32_t rngState,
    GameVariant variant,
    int32_t score,
    uint32_t moveCount,
    float elapsedSeconds,
    const std::vector<Pile>& piles,
    const std::vector<MoveCommand>& undoStack,
    const std::vector<MoveCommand>& redoStack,
    const std::string& gameId
) {
    if (piles.empty()) return false;
    constexpr float kLocalPi = 3.14159265358979323846f;

    GameRegistry::instance().initialize();
    const SolitaireGameDef* def = !gameId.empty() ? GameRegistry::instance().findById(gameId) : nullptr;
    if (!def) {
        def = GameRegistry::instance().findByVariant(variant);
    }
    if (def) {
        activeDef_ = *def;
        variant_ = def->nativeVariant;
    } else {
        activeDef_ = SolitaireGameDef{};
        activeDef_.nativeVariant = variant;
        variant_ = variant;
    }

    statusCacheDirty_ = true;
    seed_ = seed;
    rngState_ = rngState;
    score_ = score;
    moveCount_ = moveCount;
    elapsedSeconds_ = elapsedSeconds;
    victorySeconds_ = 0.0f;
    activeHint_ = {};
    piles_ = piles;
    undoStack_ = undoStack;
    redoStack_ = redoStack;
    txUndoStack_.clear();
    txRedoStack_.clear();
    blackboard_ = {};
    scriptEngine_.reset();
    if (!activeDef_.filePath.empty()) {
        scriptEngine_.loadScriptFile(activeDef_.filePath);
    }
    computeLayout(viewportW_, viewportH_);
    updateCardTargets();
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            c.x = c.targetX;
            c.y = c.targetY;
            c.elevation = c.targetElevation;
            c.angle = c.targetAngle;
            c.flipAngle = c.faceUp ? 0.0f : kLocalPi;
            c.vx = 0.0f;
            c.vy = 0.0f;
        }
    }
    return true;
}

void SolitaireBoard::restartDeal() {
    newDeal(seed_, activeDef_.id.empty() ? *GameRegistry::instance().findByVariant(variant_) : activeDef_);
}

void SolitaireBoard::newDeal(uint32_t seed, GameVariant variant) {
    const SolitaireGameDef* def = GameRegistry::instance().findByVariant(variant);
    if (def) {
        newDeal(seed, *def);
    } else {
        SolitaireGameDef fallback{};
        fallback.nativeVariant = variant;
        newDeal(seed, fallback);
    }
}

void SolitaireBoard::newDeal(uint32_t seed, const SolitaireGameDef& gameDef) {
    statusCacheDirty_ = true;
    activeDef_ = gameDef;
    seed_ = seed;
    rngState_ = seed;
    variant_ = gameDef.nativeVariant;
    score_ = 0;
    moveCount_ = 0;
    elapsedSeconds_ = 0.0f;
    victorySeconds_ = 0.0f;
    undoStack_.clear();
    redoStack_.clear();
    txUndoStack_.clear();
    txRedoStack_.clear();
    blackboard_ = {};
    scriptEngine_.reset();
    if (!gameDef.filePath.empty()) {
        scriptEngine_.loadScriptFile(gameDef.filePath);
    }
    activeHint_ = {};
    piles_.clear();

    // Build deck (52 cards for Klondike/FreeCell/Golf/Yukon/Spiderette, 104 cards for Spider/Forty Thieves)
    std::vector<Card> deck;
    if (isSpiderette()) {
        deck.reserve(52);
        uint8_t idCounter = 0;
        const uint8_t suits = spiderSuitCount(variant_);
        std::vector<Suit> suitDistribution;
        suitDistribution.reserve(4);
        if (suits == 1) {
            for (int k = 0; k < 4; ++k) suitDistribution.push_back(Suit::Spades);
        } else if (suits == 2) {
            for (int k = 0; k < 2; ++k) suitDistribution.push_back(Suit::Spades);
            for (int k = 0; k < 2; ++k) suitDistribution.push_back(Suit::Hearts);
        } else {
            suitDistribution.push_back(Suit::Spades);
            suitDistribution.push_back(Suit::Hearts);
            suitDistribution.push_back(Suit::Diamonds);
            suitDistribution.push_back(Suit::Clubs);
        }
        for (Suit s : suitDistribution) {
            for (uint8_t r = 1; r <= 13; ++r) {
                Card c{};
                c.id = idCounter++;
                c.suit = s;
                c.rank = static_cast<Rank>(r);
                c.faceUp = false;
                c.flipAngle = 0.0f;
                deck.push_back(c);
            }
        }
    } else if (isSpiderVariant(variant_)) {
        deck.reserve(104);
        uint8_t idCounter = 0;
        const uint8_t suits = spiderSuitCount(variant_);
        std::vector<Suit> suitDistribution;
        suitDistribution.reserve(8);
        if (suits == 1) {
            for (int k = 0; k < 8; ++k) suitDistribution.push_back(Suit::Spades);
        } else if (suits == 2) {
            for (int k = 0; k < 4; ++k) suitDistribution.push_back(Suit::Spades);
            for (int k = 0; k < 4; ++k) suitDistribution.push_back(Suit::Hearts);
        } else if (suits == 3) {
            for (int k = 0; k < 3; ++k) suitDistribution.push_back(Suit::Spades);
            for (int k = 0; k < 3; ++k) suitDistribution.push_back(Suit::Hearts);
            for (int k = 0; k < 2; ++k) suitDistribution.push_back(Suit::Diamonds);
        } else {
            for (int k = 0; k < 2; ++k) {
                suitDistribution.push_back(Suit::Spades);
                suitDistribution.push_back(Suit::Hearts);
                suitDistribution.push_back(Suit::Diamonds);
                suitDistribution.push_back(Suit::Clubs);
            }
        }
        for (Suit s : suitDistribution) {
            for (uint8_t r = 1; r <= 13; ++r) {
                Card c{};
                c.id = idCounter++;
                c.suit = s;
                c.rank = static_cast<Rank>(r);
                c.faceUp = false;
                c.flipAngle = 0.0f;
                deck.push_back(c);
            }
        }
    } else if (activeDef_.decks == 2 || activeDef_.family == "FortyThieves") {
        deck.reserve(104);
        uint8_t idCounter = 0;
        for (int copy = 0; copy < 2; ++copy) {
            for (uint8_t s = 0; s < 4; ++s) {
                for (uint8_t r = 1; r <= 13; ++r) {
                    Card c{};
                    c.id = idCounter++;
                    c.suit = static_cast<Suit>(s);
                    c.rank = static_cast<Rank>(r);
                    c.faceUp = false;
                    c.flipAngle = 0.0f;
                    deck.push_back(c);
                }
            }
        }
    } else {
        deck.reserve(52);
        uint8_t idCounter = 0;
        for (uint8_t s = 0; s < 4; ++s) {
            for (uint8_t r = 1; r <= 13; ++r) {
                Card c{};
                c.id = idCounter++;
                c.suit = static_cast<Suit>(s);
                c.rank = static_cast<Rank>(r);
                c.faceUp = false;
                c.flipAngle = 0.0f;
                deck.push_back(c);
            }
        }
    }

    if (activeDef_.excludeRedRoyals) {
        deck.erase(
            std::remove_if(deck.begin(), deck.end(), [](const Card& c) {
                return isRedSuit(c.suit) && (c.rank == Rank::Ace || c.rank >= Rank::Jack);
            }),
            deck.end()
        );
    }

    // Fisher-Yates shuffle with deterministic Mulberry32 seed
    for (size_t i = deck.size() - 1; i > 0; --i) {
        size_t j = static_cast<size_t>(nextRandom() % static_cast<uint32_t>(i + 1));
        std::swap(deck[i], deck[j]);
    }

    if (activeDef_.family == "Yukon") {
        // Yukon Layout: 0..3 = Foundations, 4..10 = 7 Tableau columns
        for (uint8_t i = 0; i < 4; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }
        for (uint8_t i = 0; i < 7; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }
        size_t cursor = 0;
        for (uint8_t col = 0; col < 7 && cursor < deck.size(); ++col) {
            if (col == 0) {
                Card c = deck[cursor++];
                c.faceUp = true;
                c.flipAngle = kPi * 0.75f;
                piles_[4].cards.push_back(c);
            } else {
                for (uint8_t d = 0; d < col && cursor < deck.size(); ++d) {
                    Card c = deck[cursor++];
                    c.faceUp = false;
                    piles_[4 + col].cards.push_back(c);
                }
                for (uint8_t u = 0; u < 5 && cursor < deck.size(); ++u) {
                    Card c = deck[cursor++];
                    c.faceUp = true;
                    c.flipAngle = kPi * 0.75f;
                    piles_[4 + col].cards.push_back(c);
                }
            }
        }
    } else if (activeDef_.family == "FortyThieves") {
        // Forty Thieves Layout: 0 = Stock, 1 = Waste, 2..9 = 8 Foundations, 10..19 = 10 Tableau columns
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        Pile waste{};
        waste.type = PileType::Waste;
        waste.index = 0;
        piles_.push_back(waste);

        for (uint8_t i = 0; i < 8; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }
        for (uint8_t i = 0; i < 10; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }
        size_t cursor = 0;
        for (uint8_t col = 0; col < 10; ++col) {
            for (uint8_t row = 0; row < 4 && cursor < deck.size(); ++row) {
                Card c = deck[cursor++];
                c.faceUp = true;
                c.flipAngle = kPi * 0.75f;
                piles_[10 + col].cards.push_back(c);
            }
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    } else if (isSpiderVariant(variant_)) {
        // Spider / Spiderette Layout:
        // Stock at pile 0; Foundations 1..N; Tableaus (1+N)..end
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        const uint8_t numFoundations = (activeDef_.foundations > 0) ? activeDef_.foundations : (isSpiderette() ? 4u : 8u);
        const uint8_t numTableaus = (activeDef_.tableaus > 0) ? activeDef_.tableaus : (isSpiderette() ? 7u : 10u);

        for (uint8_t i = 0; i < numFoundations; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }
        for (uint8_t i = 0; i < numTableaus; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }

        size_t cursor = 0;
        const size_t tabBase = 1 + numFoundations;
        if (isSpiderette()) {
            for (uint8_t col = 0; col < numTableaus; ++col) {
                const uint8_t count = col + 1; // 1, 2, 3, 4, 5, 6, 7 cards
                for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                    Card c = deck[cursor++];
                    c.faceUp = (row == count - 1);
                    if (c.faceUp) {
                        c.flipAngle = kPi * 0.75f;
                    }
                    piles_[tabBase + col].cards.push_back(c);
                }
            }
        } else {
            for (uint8_t col = 0; col < 10 && (tabBase + col) < piles_.size(); ++col) {
                const uint8_t count = (col < 4) ? 6u : 5u;
                for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                    Card c = deck[cursor++];
                    c.faceUp = (row == count - 1);
                    if (c.faceUp) {
                        c.flipAngle = kPi * 0.75f;
                    }
                    piles_[tabBase + col].cards.push_back(c);
                }
            }
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    } else if (activeDef_.layout == "tripeaks") {
        // Classic Overlapping TriPeaks Layout:
        // 0 = Stock (23 cards face down)
        // 1 = Foundation / Waste (1 starter card face up)
        // 2..29 = 28 overlapping peak nodes (rows 0..2 face down, row 3 face up)
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        stock.zIndex = 10;
        piles_.push_back(stock);

        Pile found{};
        found.type = PileType::Foundation;
        found.index = 0;
        found.zIndex = 10;
        piles_.push_back(found);

        for (uint8_t i = 0; i < 28; ++i) {
            Pile node{};
            node.type = PileType::Tableau;
            node.index = i;
            if (i < 3) node.zIndex = 0;
            else if (i < 9) node.zIndex = 1;
            else if (i < 18) node.zIndex = 2;
            else node.zIndex = 3;
            piles_.push_back(node);
        }

        size_t cursor = 0;
        for (uint8_t i = 0; i < 28 && cursor < deck.size(); ++i) {
            Card c = deck[cursor++];
            c.faceUp = activeDef_.tableauAllFaceUp || (i >= 18);
            if (c.faceUp) {
                c.flipAngle = kPi * 0.75f;
            }
            piles_[2 + i].cards.push_back(c);
        }
        if (cursor < deck.size()) {
            Card startCard = deck[cursor++];
            startCard.faceUp = true;
            startCard.flipAngle = kPi * 0.75f;
            piles_[1].cards.push_back(startCard);
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    } else if (activeDef_.layout == "radial_clock") {
        // Radial Clock Layout:
        // 0..numTab-1 = Clock hour piles (default 12), numTab..numTab+numFound-1 = Center foundation/king piles
        const uint8_t numTab = activeDef_.tableaus > 0 ? activeDef_.tableaus : 12;
        const uint8_t numFound = activeDef_.foundations > 0 ? activeDef_.foundations : 1;
        for (uint8_t i = 0; i < numTab; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }
        for (uint8_t i = 0; i < numFound; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            f.zIndex = 2;
            piles_.push_back(f);
        }
        const bool hasStock = (activeDef_.stockDealMode != "none");
        if (hasStock) {
            Pile stock{};
            stock.type = PileType::Stock;
            stock.index = 0;
            stock.zIndex = 2;
            piles_.push_back(stock);
        }

        size_t cursor = 0;
        for (uint8_t i = 0; i < numTab; ++i) {
            const uint8_t count = (i < activeDef_.tableauDealCounts.size()) ? activeDef_.tableauDealCounts[i] : 4;
            for (uint8_t r = 0; r < count && cursor < deck.size(); ++r) {
                Card c = deck[cursor++];
                c.faceUp = activeDef_.tableauAllFaceUp || (r + 1 == count);
                if (c.faceUp) {
                    c.flipAngle = kPi * 0.75f;
                }
                piles_[i].cards.push_back(c);
            }
        }
        if (activeDef_.foundationStarterCard && numFound > 0 && cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = true;
            c.flipAngle = kPi * 0.75f;
            piles_[numTab].cards.push_back(c);
        }
        if (hasStock) {
            const size_t stockIdx = static_cast<size_t>(numTab) + numFound;
            while (cursor < deck.size()) {
                Card c = deck[cursor++];
                c.faceUp = false;
                piles_[stockIdx].cards.push_back(c);
            }
        }
    } else if (variant_ == GameVariant::Golf) {
        // Golf Layout:
        // 0 = Stock (16 cards face down)
        // 1 = Foundation / Waste (1 starter card face up)
        // 2..8 = 7 Tableau columns (5 cards face up each)
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        Pile found{};
        found.type = PileType::Foundation;
        found.index = 0;
        piles_.push_back(found);

        for (uint8_t i = 0; i < 7; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }

        size_t cursor = 0;
        for (uint8_t col = 0; col < 7; ++col) {
            const uint8_t count = (col < activeDef_.tableauDealCounts.size()) ? activeDef_.tableauDealCounts[col] : 5;
            for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                Card c = deck[cursor++];
                c.faceUp = activeDef_.tableauAllFaceUp || (row + 1 == count);
                if (c.faceUp) {
                    c.flipAngle = kPi * 0.75f;
                }
                piles_[2 + col].cards.push_back(c);
            }
        }
        if (cursor < deck.size()) {
            Card startCard = deck[cursor++];
            startCard.faceUp = true;
            startCard.flipAngle = kPi * 0.75f;
            piles_[1].cards.push_back(startCard);
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    } else if (isPyramidGame()) {
        // Pyramid Layout:
        // 0 = Stock, 1 = Waste, 2 = Foundation, 3..30 = 28 Pyramid piles, 31..38 = Giza Reserve (if Giza)
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        Pile waste{};
        waste.type = PileType::Waste;
        waste.index = 0;
        piles_.push_back(waste);

        Pile found{};
        found.type = PileType::Foundation;
        found.index = 0;
        piles_.push_back(found);

        for (uint8_t i = 0; i < 28; ++i) {
            Pile pyr{};
            pyr.type = PileType::Tableau;
            pyr.index = i;
            piles_.push_back(pyr);
        }

        const bool isGiza = (activeDef_.id == "giza");
        if (isGiza) {
            for (uint8_t i = 0; i < 8; ++i) {
                Pile res{};
                res.type = PileType::Tableau;
                res.index = 28 + i;
                piles_.push_back(res);
            }
        }

        size_t cursor = 0;
        for (uint8_t i = 0; i < 28 && cursor < deck.size(); ++i) {
            Card c = deck[cursor++];
            c.faceUp = true;
            c.flipAngle = kPi * 0.75f;
            piles_[3 + i].cards.push_back(c);
        }

        if (isGiza) {
            for (uint8_t col = 0; col < 8; ++col) {
                for (uint8_t row = 0; row < 3 && cursor < deck.size(); ++row) {
                    Card c = deck[cursor++];
                    c.faceUp = true;
                    c.flipAngle = kPi * 0.75f;
                    piles_[31 + col].cards.push_back(c);
                }
            }
        } else {
            while (cursor < deck.size()) {
                Card c = deck[cursor++];
                c.faceUp = false;
                piles_[0].cards.push_back(c);
            }
        }
    } else if (activeDef_.layout == "fan_grid" || activeDef_.family == "Fan" || (activeDef_.layout == "columns" && activeDef_.tableaus > 12)) {
        // 4 Foundations (0..3)
        for (uint8_t i = 0; i < 4; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }
        // Tableaus (4..)
        const uint8_t numTabs = activeDef_.tableaus > 0 ? activeDef_.tableaus : 18;
        for (uint8_t i = 0; i < numTabs; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }
        size_t cursor = 0;
        for (uint8_t col = 0; col < numTabs; ++col) {
            uint8_t count = (col < activeDef_.tableauDealCounts.size()) ? activeDef_.tableauDealCounts[col] : 3;
            for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                Card c = deck[cursor++];
                c.faceUp = true;
                c.flipAngle = kPi * 0.75f;
                piles_[4 + col].cards.push_back(c);
            }
        }
    } else if (activeDef_.layout == "canfield" || activeDef_.family == "Canfield") {
        // 0 = Stock, 1 = Waste, 2..5 = 4 Foundations, 6 = Reserve (13 cards), 7..10 = 4 Tableaus
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        Pile waste{};
        waste.type = PileType::Waste;
        waste.index = 0;
        piles_.push_back(waste);

        for (uint8_t i = 0; i < 4; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }

        Pile res{};
        res.type = PileType::FreeCell;
        res.index = 0;
        piles_.push_back(res);

        for (uint8_t i = 0; i < 4; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }

        size_t cursor = 0;
        for (uint8_t i = 0; i < 13 && cursor < deck.size(); ++i) {
            Card c = deck[cursor++];
            c.faceUp = (i == 12);
            if (c.faceUp) c.flipAngle = kPi * 0.75f;
            piles_[6].cards.push_back(c);
        }

        if (cursor < deck.size()) {
            Card st = deck[cursor++];
            st.faceUp = true;
            st.flipAngle = kPi * 0.75f;
            piles_[2].cards.push_back(st);
        }

        for (uint8_t i = 0; i < 4 && cursor < deck.size(); ++i) {
            Card c = deck[cursor++];
            c.faceUp = true;
            c.flipAngle = kPi * 0.75f;
            piles_[7 + i].cards.push_back(c);
        }

        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    } else if (variant_ == GameVariant::FreeCell || (activeDef_.freecells > 0 && activeDef_.layout != "canfield")) {
        // FreeCells (0..numFC-1), Foundations (numFC..numFC+numF-1), Tableaus (numFC+numF..)
        const uint8_t numFC = activeDef_.freecells > 0 ? activeDef_.freecells : 4;
        const uint8_t numF = activeDef_.foundations > 0 ? activeDef_.foundations : 4;
        const uint8_t numT = activeDef_.tableaus > 0 ? activeDef_.tableaus : 8;
        for (uint8_t i = 0; i < numFC; ++i) {
            Pile p{};
            p.type = PileType::FreeCell;
            p.index = i;
            piles_.push_back(p);
        }
        for (uint8_t i = 0; i < numF; ++i) {
            Pile p{};
            p.type = PileType::Foundation;
            p.index = i;
            piles_.push_back(p);
        }
        for (uint8_t i = 0; i < numT; ++i) {
            Pile p{};
            p.type = PileType::Tableau;
            p.index = i;
            piles_.push_back(p);
        }
        const size_t tabBase = static_cast<size_t>(numFC) + numF;
        if (!activeDef_.tableauDealCounts.empty()) {
            size_t cursor = 0;
            for (uint8_t col = 0; col < numT; ++col) {
                uint8_t count = (col < activeDef_.tableauDealCounts.size()) ? activeDef_.tableauDealCounts[col] : 0;
                for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                    Card c = deck[cursor++];
                    c.faceUp = true;
                    c.flipAngle = kPi * 0.65f;
                    piles_[tabBase + col].cards.push_back(c);
                }
            }
            for (uint8_t fc = 0; fc < numFC && cursor < deck.size(); ++fc) {
                Card c = deck[cursor++];
                c.faceUp = true;
                c.flipAngle = kPi * 0.65f;
                piles_[fc].cards.push_back(c);
            }
        } else {
            for (size_t i = 0; i < deck.size(); ++i) {
                Card c = deck[i];
                c.faceUp = true;
                c.flipAngle = kPi * 0.65f;
                piles_[tabBase + (i % numT)].cards.push_back(c);
            }
        }
    } else if (hasScriptHooks() && activeDef_.layout == "columns") {
        // Generic data-driven columns layout for Eatscript games:
        // 0 = Stock, 1 = Waste, 2..(2+F-1) = Foundations, (2+F).. = Tableaus
        const uint8_t numF = activeDef_.foundations > 0 ? activeDef_.foundations : 1;
        const uint8_t numT = activeDef_.tableaus > 0 ? activeDef_.tableaus : 4;

        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        stock.role = "stock";
        piles_.push_back(stock);

        Pile waste{};
        waste.type = PileType::Waste;
        waste.index = 0;
        waste.role = "waste";
        piles_.push_back(waste);

        for (uint8_t i = 0; i < numF; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            f.role = "foundation";
            piles_.push_back(f);
        }
        for (uint8_t i = 0; i < numT; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            t.role = "tableau";
            piles_.push_back(t);
        }

        size_t cursor = 0;
        const size_t tabBase = 2 + numF;
        for (uint8_t col = 0; col < numT; ++col) {
            uint8_t count = (col < activeDef_.tableauDealCounts.size()) ? activeDef_.tableauDealCounts[col] : 1;
            for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                Card c = deck[cursor++];
                c.faceUp = activeDef_.tableauAllFaceUp || (row == count - 1);
                if (c.faceUp) {
                    c.flipAngle = kPi * 0.75f;
                }
                piles_[tabBase + col].cards.push_back(c);
            }
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    } else {
        // Klondike:
        // 0 = Stock, 1 = Waste, 2..5 = Foundations, 6..12 = 7 Tableau columns
        Pile stock{};
        stock.type = PileType::Stock;
        stock.index = 0;
        piles_.push_back(stock);

        Pile waste{};
        waste.type = PileType::Waste;
        waste.index = 0;
        piles_.push_back(waste);

        for (uint8_t i = 0; i < 4; ++i) {
            Pile f{};
            f.type = PileType::Foundation;
            f.index = i;
            piles_.push_back(f);
        }
        for (uint8_t i = 0; i < 7; ++i) {
            Pile t{};
            t.type = PileType::Tableau;
            t.index = i;
            piles_.push_back(t);
        }

        size_t cursor = 0;
        for (uint8_t col = 0; col < 7; ++col) {
            uint8_t count = col + 1;
            for (uint8_t row = 0; row < count && cursor < deck.size(); ++row) {
                Card c = deck[cursor++];
                c.faceUp = (row == count - 1);
                if (c.faceUp) {
                    c.flipAngle = kPi * 0.75f;
                }
                piles_[6 + col].cards.push_back(c);
            }
        }
        while (cursor < deck.size()) {
            Card c = deck[cursor++];
            c.faceUp = false;
            piles_[0].cards.push_back(c);
        }
    }

    if (scriptEngine_.hasOnDeal()) {
        TurnTransaction setupTx;
        auto ctx = makeScriptContext(&setupTx);
        scriptEngine_.invokeOnDeal(ctx);
    }

    computeLayout(viewportW_, viewportH_);
    const float originX = piles_.empty() ? 0.0f : piles_[0].x;
    const float originY = piles_.empty() ? 0.0f : piles_[0].y;
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            c.x = originX;
            c.y = originY;
            c.elevation = 6.0f;
        }
    }
}

void SolitaireBoard::computeLayout(float viewportWidth, float viewportHeight) {
    viewportW_ = std::max(240.0f, viewportWidth);
    viewportH_ = std::max(240.0f, viewportHeight);

    uint32_t topCols = 0;
    if (activeDef_.freecells > 0) {
        topCols = activeDef_.freecells + (activeDef_.foundations > 0 ? activeDef_.foundations : 4);
    } else if (activeDef_.foundations > 0) {
        const bool hasWaste = (activeDef_.stockDealMode == "waste_draw_1" || activeDef_.stockDealMode == "waste_draw_3");
        topCols = (hasWaste ? 2u : 1u) + activeDef_.foundations;
    }
    const uint32_t tabCols = (activeDef_.tableaus > 0) ? activeDef_.tableaus :
        (isSpiderVariant(variant_) ? (isSpiderette() ? 7u : 10u) : ((variant_ == GameVariant::FreeCell) ? 8u : 7u));
    const uint32_t numCols = std::max(tabCols, topCols);
    const bool isMobile = isMobileLayout();

    StockPosition effectiveStock = stockPosition_;
    if (effectiveStock == StockPosition::Auto) {
        effectiveStock = isMobile ? StockPosition::BottomRight : StockPosition::TopLeft;
    }

    // Maximized Full-Felt Responsive Framing:
    // With header cleared and single-line telemetry docked at bottom edge,
    // we use lean modern margins (4-12px on mobile, 10-28px on desktop) to maximize card scale.
    const float marginX = isMobile
        ? std::clamp(viewportW_ * 0.015f, 4.0f, 12.0f)
        : std::clamp(viewportW_ * 0.018f, 10.0f, 28.0f);
    const float marginY = isMobile
        ? std::clamp(viewportH_ * 0.012f, 6.0f, 12.0f)
        : std::clamp(viewportH_ * 0.014f, 8.0f, 18.0f);
    constexpr float bottomReserved = 24.0f; // Minimal reservation for bottom telemetry watermark

    const float availW = viewportW_ - 2.0f * marginX;
    const float playfieldH = viewportH_ - bottomReserved;
    const float availH = playfieldH - marginY;

    // Canonical proportions in units of Card Width (w):
    // Card aspect ratio: 1.40 (height = 1.40 * width)
    constexpr float kCardAspect = 1.40f;
    // On mobile portrait, squeeze column gap ratio down to 4%..8% so cards stay as large as possible
    const float kGapXRatio = isMobile
        ? std::clamp(viewportW_ * 0.00015f, 0.04f, 0.08f)
        : 0.15f;
    const float kGapYRatio = isMobile ? 0.10f : 0.16f;

    if (isPyramidGame()) {
        const bool isGiza = (activeDef_.id == "giza");

        if (!isGiza) {
            const bool useShoulders = (availW >= availH * 0.85f);
            const float stepYRatio = 0.44f;
            const float spanWPerCard = 7.0f + 6.0f * kGapXRatio;
            const float spanHPerCard = useShoulders
                ? (kCardAspect * (6.0f * stepYRatio + 1.0f))
                : (kCardAspect * (6.0f * stepYRatio + 2.0f + kGapYRatio));

            const float maxW_fromWidth = availW / spanWPerCard;
            const float maxW_fromHeight = availH / spanHPerCard;
            const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
            cardW_ = std::clamp(optimalCardW, 32.0f, 320.0f);
            cardH_ = std::round(cardW_ * kCardAspect);

            const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
            const float rowGap = std::max(4.0f, std::round(cardH_ * kGapYRatio));
            const float stepY = std::round(cardH_ * stepYRatio);

            const float pyrGridW = 7.0f * cardW_ + 6.0f * colGap;
            const float pyrStartX = std::round((viewportW_ - pyrGridW) * 0.5f);

            if (useShoulders) {
                const float pyrTotalH = 6.0f * stepY + cardH_;
                float pyrTopY = marginY;
                if (playfieldH > pyrTotalH) {
                    pyrTopY = std::round(marginY + (playfieldH - pyrTotalH) * 0.45f);
                }

                size_t pIdx = 3;
                for (uint8_t r = 0; r < 7; ++r) {
                    uint8_t count = r + 1;
                    float rowSpan = static_cast<float>(count) * cardW_ + static_cast<float>(count - 1) * colGap;
                    float rowX = std::round((viewportW_ - rowSpan) * 0.5f);
                    float rowY = pyrTopY + static_cast<float>(r) * stepY;
                    for (uint8_t c = 0; c < count; ++c) {
                        if (pIdx < piles_.size()) {
                            piles_[pIdx].x = rowX + static_cast<float>(c) * (cardW_ + colGap);
                            piles_[pIdx].y = rowY;
                        }
                        ++pIdx;
                    }
                }

                piles_[0].x = pyrStartX;
                piles_[0].y = pyrTopY;
                piles_[1].x = pyrStartX + cardW_ + colGap;
                piles_[1].y = pyrTopY;

                piles_[2].x = pyrStartX + 6.0f * (cardW_ + colGap);
                piles_[2].y = pyrTopY;
            } else {
                const float pyrTotalH = 6.0f * stepY + cardH_ + rowGap + cardH_;
                float pyrTopY = marginY;
                if (playfieldH > pyrTotalH) {
                    pyrTopY = std::round(marginY + (playfieldH - pyrTotalH) * 0.25f);
                }

                size_t pIdx = 3;
                for (uint8_t r = 0; r < 7; ++r) {
                    uint8_t count = r + 1;
                    float rowSpan = static_cast<float>(count) * cardW_ + static_cast<float>(count - 1) * colGap;
                    float rowX = std::round((viewportW_ - rowSpan) * 0.5f);
                    float rowY = pyrTopY + static_cast<float>(r) * stepY;
                    for (uint8_t c = 0; c < count; ++c) {
                        if (pIdx < piles_.size()) {
                            piles_[pIdx].x = rowX + static_cast<float>(c) * (cardW_ + colGap);
                            piles_[pIdx].y = rowY;
                        }
                        ++pIdx;
                    }
                }

                const float bottomY = pyrTopY + 6.0f * stepY + cardH_ + rowGap;
                const float bottomSpan = 3.0f * cardW_ + 2.0f * colGap + cardW_ * 0.5f;
                const float bStartX = std::round((viewportW_ - bottomSpan) * 0.5f);
                piles_[0].x = bStartX;
                piles_[0].y = bottomY;
                piles_[1].x = bStartX + cardW_ + colGap;
                piles_[1].y = bottomY;
                piles_[2].x = bStartX + 2.0f * (cardW_ + colGap) + cardW_ * 0.5f;
                piles_[2].y = bottomY;
            }
        } else {
            // Giza
            const bool useFlanks = (availW >= availH * 1.55f);
            const float stepYRatio = 0.44f;

            if (useFlanks) {
                const float spanWPerCard = 15.0f + 14.0f * kGapXRatio + 1.0f;
                const float spanHPerCard = kCardAspect * (6.0f * stepYRatio + 1.0f + 0.4f);
                const float maxW_fromWidth = availW / spanWPerCard;
                const float maxW_fromHeight = availH / spanHPerCard;
                const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
                cardW_ = std::clamp(optimalCardW, 30.0f, 320.0f);
                cardH_ = std::round(cardW_ * kCardAspect);

                const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
                const float stepY = std::round(cardH_ * stepYRatio);

                const float wingW = 4.0f * cardW_ + 3.0f * colGap;
                const float pyrW = 7.0f * cardW_ + 6.0f * colGap;
                const float aisle = std::max(colGap * 2.0f, cardW_ * 0.4f);
                const float totalSpan = 2.0f * wingW + pyrW + 2.0f * aisle;
                const float overallStartX = std::round((viewportW_ - totalSpan) * 0.5f);

                const float leftWingX = overallStartX;
                const float pyrStartX = leftWingX + wingW + aisle;
                const float rightWingX = pyrStartX + pyrW + aisle;

                const float pyrTotalH = 6.0f * stepY + cardH_;
                float pyrTopY = marginY;
                if (playfieldH > pyrTotalH) {
                    pyrTopY = std::round(marginY + (playfieldH - pyrTotalH) * 0.45f);
                }

                size_t pIdx = 3;
                for (uint8_t r = 0; r < 7; ++r) {
                    uint8_t count = r + 1;
                    float rowSpan = static_cast<float>(count) * cardW_ + static_cast<float>(count - 1) * colGap;
                    float rowX = pyrStartX + (pyrW - rowSpan) * 0.5f;
                    float rowY = pyrTopY + static_cast<float>(r) * stepY;
                    for (uint8_t c = 0; c < count; ++c) {
                        if (pIdx < piles_.size()) {
                            piles_[pIdx].x = rowX + static_cast<float>(c) * (cardW_ + colGap);
                            piles_[pIdx].y = rowY;
                        }
                        ++pIdx;
                    }
                }

                piles_[2].x = rightWingX + 3.0f * (cardW_ + colGap);
                piles_[2].y = std::max(marginY, pyrTopY - cardH_ * 0.4f);

                const float wingTopY = pyrTopY + cardH_ * 0.25f;
                for (uint8_t i = 0; i < 4 && (31 + i) < piles_.size(); ++i) {
                    piles_[31 + i].x = leftWingX + static_cast<float>(i) * (cardW_ + colGap);
                    piles_[31 + i].y = wingTopY;
                }
                for (uint8_t i = 0; i < 4 && (35 + i) < piles_.size(); ++i) {
                    piles_[35 + i].x = rightWingX + static_cast<float>(i) * (cardW_ + colGap);
                    piles_[35 + i].y = wingTopY;
                }

                piles_[0].x = -1000.0f;
                piles_[0].y = -1000.0f;
                piles_[1].x = -1000.0f;
                piles_[1].y = -1000.0f;
            } else {
                const float cascadeDepth = 1.0f + 2.0f * 0.20f;
                const float spanWPerCard = 8.0f + 7.0f * kGapXRatio;
                const float spanHPerCard = kCardAspect * (6.0f * stepYRatio + 1.0f + kGapYRatio + cascadeDepth);

                const float maxW_fromWidth = availW / spanWPerCard;
                const float maxW_fromHeight = availH / spanHPerCard;
                const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
                cardW_ = std::clamp(optimalCardW, 30.0f, 320.0f);
                cardH_ = std::round(cardW_ * kCardAspect);

                const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
                const float rowGap = std::max(4.0f, std::round(cardH_ * kGapYRatio));
                const float stepY = std::round(cardH_ * stepYRatio);

                const float pyrGridW = 7.0f * cardW_ + 6.0f * colGap;
                const float gizaGridW = 8.0f * cardW_ + 7.0f * colGap;
                const float pyrStartX = std::round((viewportW_ - pyrGridW) * 0.5f);
                const float gizaStartX = std::round((viewportW_ - gizaGridW) * 0.5f);

                const float totalGizaH = 6.0f * stepY + cardH_ + rowGap + cardH_ * cascadeDepth;
                float pyrTopY = marginY;
                if (playfieldH > totalGizaH) {
                    pyrTopY = std::round(marginY + (playfieldH - totalGizaH) * 0.20f);
                }

                size_t pIdx = 3;
                for (uint8_t r = 0; r < 7; ++r) {
                    uint8_t count = r + 1;
                    float rowSpan = static_cast<float>(count) * cardW_ + static_cast<float>(count - 1) * colGap;
                    float rowX = std::round((viewportW_ - rowSpan) * 0.5f);
                    float rowY = pyrTopY + static_cast<float>(r) * stepY;
                    for (uint8_t c = 0; c < count; ++c) {
                        if (pIdx < piles_.size()) {
                            piles_[pIdx].x = rowX + static_cast<float>(c) * (cardW_ + colGap);
                            piles_[pIdx].y = rowY;
                        }
                        ++pIdx;
                    }
                }

                piles_[2].x = pyrStartX + 6.0f * (cardW_ + colGap);
                piles_[2].y = pyrTopY;

                const float bottomY = pyrTopY + 6.0f * stepY + cardH_ + rowGap;
                for (uint8_t i = 0; i < 8 && (31 + i) < piles_.size(); ++i) {
                    piles_[31 + i].x = gizaStartX + static_cast<float>(i) * (cardW_ + colGap);
                    piles_[31 + i].y = bottomY;
                }

                piles_[0].x = -1000.0f;
                piles_[0].y = -1000.0f;
                piles_[1].x = -1000.0f;
                piles_[1].y = -1000.0f;
            }
        }

        updateCardTargets();
        return;
    }

    if (activeDef_.layout == "fan_grid" || activeDef_.family == "Fan" || (activeDef_.layout == "columns" && activeDef_.tableaus > 12)) {
        const float spanWPerCard = 6.0f + 5.0f * kGapXRatio;
        const float spanHPerCard = kCardAspect * (4.0f + 3.0f * kGapYRatio + 0.35f);
        const float maxW_fromWidth = availW / spanWPerCard;
        const float maxW_fromHeight = availH / spanHPerCard;
        const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
        cardW_ = std::clamp(optimalCardW, 36.0f, 320.0f);
        cardH_ = std::round(cardW_ * kCardAspect);

        const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
        const float rowGap = std::max(4.0f, std::round(cardH_ * kGapYRatio));

        const float fGridW = 4.0f * cardW_ + 3.0f * colGap;
        const float fStartX = std::round((viewportW_ - fGridW) * 0.5f);
        for (uint8_t i = 0; i < 4 && i < piles_.size(); ++i) {
            piles_[i].x = fStartX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[i].y = marginY;
        }

        const float tGridW = 6.0f * cardW_ + 5.0f * colGap;
        const float tStartX = std::round((viewportW_ - tGridW) * 0.5f);
        const float tBaseY = marginY + cardH_ + rowGap;
        for (uint8_t i = 0; i < 18 && (4 + i) < piles_.size(); ++i) {
            uint8_t row = i / 6;
            uint8_t col = i % 6;
            piles_[4 + i].x = tStartX + static_cast<float>(col) * (cardW_ + colGap);
            piles_[4 + i].y = tBaseY + static_cast<float>(row) * (cardH_ + rowGap);
        }

        updateCardTargets();
        return;
    }

    if (activeDef_.layout == "canfield" || activeDef_.family == "Canfield") {
        const float spanWPerCard = 5.2f + 4.0f * kGapXRatio;
        const float spanHPerCard = kCardAspect * (2.0f + kGapYRatio + 1.45f);
        const float maxW_fromWidth = availW / spanWPerCard;
        const float maxW_fromHeight = availH / spanHPerCard;
        const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
        cardW_ = std::clamp(optimalCardW, 36.0f, 320.0f);
        cardH_ = std::round(cardW_ * kCardAspect);

        const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
        const float rowGap = std::max(4.0f, std::round(cardH_ * kGapYRatio));

        const float totalGridW = 5.2f * cardW_ + 4.0f * colGap;
        const float startX = std::round((viewportW_ - totalGridW) * 0.5f);
        const float topRowY = marginY;
        const float tableauY = topRowY + cardH_ + rowGap;

        if (piles_.size() > 6) {
            piles_[6].x = startX;
            piles_[6].y = topRowY;
        }
        if (piles_.size() > 1) {
            piles_[0].x = startX;
            piles_[0].y = tableauY;
            piles_[1].x = startX;
            piles_[1].y = tableauY + cardH_ + rowGap;
        }
        const float rightBaseX = startX + 1.2f * (cardW_ + colGap);
        for (uint8_t i = 0; i < 4 && (2 + i) < piles_.size(); ++i) {
            piles_[2 + i].x = rightBaseX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[2 + i].y = topRowY;
        }
        for (uint8_t i = 0; i < 4 && (7 + i) < piles_.size(); ++i) {
            piles_[7 + i].x = rightBaseX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[7 + i].y = tableauY;
        }

        updateCardTargets();
        return;
    }

    if (activeDef_.layout == "tripeaks") {
        // 10-card-wide base, 4 overlapping peak rows (stepY = 0.48 * cardH) + Stock & Foundation row below
        constexpr float stepYRatio = 0.48f;
        const float spanWPerCard = 10.0f + 9.0f * kGapXRatio;
        const float spanHPerCard = kCardAspect * (3.0f * stepYRatio + 2.0f + kGapYRatio * 1.4f);
        const float maxW_fromWidth = availW / spanWPerCard;
        const float maxW_fromHeight = availH / spanHPerCard;
        cardW_ = std::clamp(std::min(maxW_fromWidth, maxW_fromHeight), 32.0f, 320.0f);
        cardH_ = std::round(cardW_ * kCardAspect);

        const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
        const float rowGap = std::max(6.0f, std::round(cardH_ * kGapYRatio * 1.25f));
        const float stepX = cardW_ + colGap;
        const float stepY = std::round(cardH_ * stepYRatio);

        const float baseW = 10.0f * cardW_ + 9.0f * colGap;
        const float baseStartX = std::round((viewportW_ - baseW) * 0.5f);
        const float totalH = 3.0f * stepY + 2.0f * cardH_ + rowGap;
        const float topY = std::round(marginY + std::max(0.0f, (playfieldH - totalH) * 0.35f));

        // 28 peak nodes at piles_[2..29]
        static constexpr float kPeakCols[28] = {
            // Row 0 (3 peak tops)
            1.5f, 4.5f, 7.5f,
            // Row 1 (6 cards)
            1.0f, 2.0f, 4.0f, 5.0f, 7.0f, 8.0f,
            // Row 2 (9 cards)
            0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f, 8.5f,
            // Row 3 (10 base cards)
            0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f
        };
        for (uint8_t i = 0; i < 28 && (2 + i) < piles_.size(); ++i) {
            uint8_t row = 3;
            if (i < 3) row = 0;
            else if (i < 9) row = 1;
            else if (i < 18) row = 2;
            piles_[2 + i].x = std::round(baseStartX + kPeakCols[i] * stepX);
            piles_[2 + i].y = topY + static_cast<float>(row) * stepY;
            piles_[2 + i].angle = 0.0f;
            piles_[2 + i].zIndex = static_cast<int32_t>(row);
        }

        // Stock & Foundation centered below the 3 peaks
        if (piles_.size() >= 2) {
            const float botY = topY + 3.0f * stepY + cardH_ + rowGap;
            const float pairW = 2.0f * cardW_ + colGap * 2.0f;
            const float pairX = std::round((viewportW_ - pairW) * 0.5f);
            piles_[0].x = pairX;
            piles_[0].y = botY;
            piles_[0].zIndex = 10;
            piles_[1].x = pairX + cardW_ + colGap * 2.0f;
            piles_[1].y = botY;
            piles_[1].zIndex = 10;
        }

        updateCardTargets();
        return;
    }

    if (activeDef_.layout == "radial_clock") {
        const uint8_t numTab = activeDef_.tableaus > 0 ? activeDef_.tableaus : 12;
        const float minDim = std::min(availW, availH);
        // Fit a ring of rotated cards + slight outward fan inside minDim
        cardW_ = std::clamp(minDim * 0.135f, 36.0f, 160.0f);
        cardH_ = std::round(cardW_ * kCardAspect);

        const float cx = viewportW_ * 0.5f;
        const float cy = marginY + playfieldH * 0.5f;
        const float radius = std::max(cardH_ * 1.15f, minDim * 0.5f - cardH_ * 0.72f);

        for (uint8_t i = 0; i < numTab && i < piles_.size(); ++i) {
            // Clockwise from 12 o'clock (-pi/2)
            const float theta = -kPi * 0.5f + (static_cast<float>(i) / static_cast<float>(numTab)) * (2.0f * kPi);
            const float px = cx + std::cos(theta) * radius - cardW_ * 0.5f;
            const float py = cy + std::sin(theta) * radius - cardH_ * 0.5f;
            piles_[i].x = px;
            piles_[i].y = py;
            piles_[i].angle = theta + kPi * 0.5f;
            piles_[i].fanDirX = std::cos(theta);
            piles_[i].fanDirY = std::sin(theta);
            piles_[i].zIndex = 0;
        }
        // Center foundation / reserve piles
        const size_t remCount = (piles_.size() > numTab) ? (piles_.size() - numTab) : 0;
        const float gap = std::max(4.0f, cardW_ * kGapXRatio);
        const float centerSpanW = static_cast<float>(remCount) * cardW_ + (remCount > 1 ? static_cast<float>(remCount - 1) * gap : 0.0f);
        const float centerStartX = cx - centerSpanW * 0.5f;
        for (size_t k = numTab; k < piles_.size(); ++k) {
            piles_[k].x = centerStartX + static_cast<float>(k - numTab) * (cardW_ + gap);
            piles_[k].y = cy - cardH_ * 0.5f;
            piles_[k].angle = 0.0f;
            piles_[k].zIndex = 2;
        }

        updateCardTargets();
        return;
    }

    // Nominal cascade depth budget (in card heights) below the top tableau card:
    const float cascadeBudgetRatio = (variant_ == GameVariant::FreeCell) ? 1.38f : (isSpiderVariant(variant_) ? 1.55f : 1.48f);

    // Solve for maximum card width constrained by both width and height:
    // Horizontal span = numCols * w + (numCols - 1) * (kGapXRatio * w)
    const float spanWPerCard = static_cast<float>(numCols) + static_cast<float>(numCols - 1) * kGapXRatio;
    const float maxW_fromWidth = availW / spanWPerCard;

    // In bottom-stock mode, bottom rail needs a 1.0*h card row
    const bool hasBottomStock = (effectiveStock != StockPosition::TopLeft) &&
        (variant_ == GameVariant::KlondikeDraw1 || variant_ == GameVariant::KlondikeDraw3 || isSpiderVariant(variant_));
    const float spanHPerCard = hasBottomStock
        ? (kCardAspect * (3.0f + 2.0f * kGapYRatio + cascadeBudgetRatio))
        : (kCardAspect * (2.0f + kGapYRatio + cascadeBudgetRatio));
    const float maxW_fromHeight = availH / spanHPerCard;

    // Optimal card scale: perfectly fits in center of display maximizing card size
    // High-res monitors get cards up to 320px; small mobile viewports scale down smoothly without clipping
    const float optimalCardW = std::min(maxW_fromWidth, maxW_fromHeight);
    const float minCardW = std::min(36.0f, maxW_fromWidth);
    cardW_ = std::clamp(optimalCardW, minCardW, 320.0f);
    cardH_ = std::round(cardW_ * kCardAspect);

    const float colGap = std::max(2.0f, std::round(cardW_ * kGapXRatio));
    const float rowGap = std::max(4.0f, std::round(cardH_ * kGapYRatio));

    // 2D Centering: Center the entire board both horizontally and vertically
    const float totalGridW = static_cast<float>(numCols) * cardW_ + static_cast<float>(numCols - 1) * colGap;
    const float startX = std::round((viewportW_ - totalGridW) * 0.5f);

    const float nominalGridH = cardH_ * (hasBottomStock ? 3.0f : 2.0f) + rowGap * (hasBottomStock ? 2.0f : 1.0f) + std::round(cardH_ * cascadeBudgetRatio);
    float topRowY = marginY;
    if (playfieldH > nominalGridH) {
        const float slackY = playfieldH - nominalGridH;
        topRowY = std::round(marginY + slackY * (hasBottomStock ? 0.15f : 0.28f));
    }
    const float tableauY = topRowY + cardH_ + rowGap;

    if (activeDef_.family == "Yukon") {
        for (uint8_t i = 0; i < 4; ++i) {
            piles_[i].x = startX + static_cast<float>(3 + i) * (cardW_ + colGap);
            piles_[i].y = topRowY;
        }
        for (uint8_t i = 0; i < 7; ++i) {
            piles_[4 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[4 + i].y = tableauY;
        }
    } else if (activeDef_.family == "FortyThieves") {
        piles_[0].x = startX;
        piles_[0].y = topRowY;
        piles_[1].x = startX + (cardW_ + colGap);
        piles_[1].y = topRowY;
        for (uint8_t i = 0; i < 8; ++i) {
            piles_[2 + i].x = startX + static_cast<float>(2 + i) * (cardW_ + colGap);
            piles_[2 + i].y = topRowY;
        }
        for (uint8_t i = 0; i < 10; ++i) {
            piles_[10 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[10 + i].y = tableauY;
        }
    } else if (isSpiderVariant(variant_)) {
        const uint8_t numFoundations = (activeDef_.foundations > 0) ? activeDef_.foundations : (isSpiderette() ? 4u : 8u);
        const uint8_t numTableaus = (activeDef_.tableaus > 0) ? activeDef_.tableaus : (isSpiderette() ? 7u : 10u);
        const size_t tabBase = 1 + numFoundations;

        if (isSpiderette()) {
            if (hasBottomStock) {
                const float bottomRailY = std::round(viewportH_ - bottomReserved - cardH_ - 4.0f);
                piles_[0].x = (effectiveStock == StockPosition::BottomRight)
                    ? (startX + 6.0f * (cardW_ + colGap))
                    : startX;
                piles_[0].y = bottomRailY;
                for (uint8_t i = 0; i < 4; ++i) {
                    piles_[1 + i].x = startX + static_cast<float>(3 + i) * (cardW_ + colGap);
                    piles_[1 + i].y = topRowY;
                }
            } else {
                piles_[0].x = startX;
                piles_[0].y = topRowY;
                for (uint8_t i = 0; i < 4; ++i) {
                    piles_[1 + i].x = startX + static_cast<float>(3 + i) * (cardW_ + colGap);
                    piles_[1 + i].y = topRowY;
                }
            }
            for (uint8_t i = 0; i < numTableaus && (tabBase + i) < piles_.size(); ++i) {
                piles_[tabBase + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
                piles_[tabBase + i].y = tableauY;
            }
        } else {
            if (hasBottomStock) {
                const float bottomRailY = std::round(viewportH_ - bottomReserved - cardH_ - 4.0f);
                piles_[0].x = (effectiveStock == StockPosition::BottomRight)
                    ? (startX + 9.0f * (cardW_ + colGap))
                    : startX;
                piles_[0].y = bottomRailY;
                for (uint8_t i = 0; i < 8 && (1 + i) < piles_.size(); ++i) {
                    piles_[1 + i].x = startX + static_cast<float>(1 + i) * (cardW_ + colGap);
                    piles_[1 + i].y = topRowY;
                }
            } else {
                piles_[0].x = startX;
                piles_[0].y = topRowY;
                for (uint8_t i = 0; i < 8 && (1 + i) < piles_.size(); ++i) {
                    piles_[1 + i].x = startX + static_cast<float>(2 + i) * (cardW_ + colGap);
                    piles_[1 + i].y = topRowY;
                }
            }
            for (uint8_t i = 0; i < numTableaus && (tabBase + i) < piles_.size(); ++i) {
                piles_[tabBase + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
                piles_[tabBase + i].y = tableauY;
            }
        }
    } else if (variant_ == GameVariant::Golf) {
        // Golf Layout: Stock at col 2, Foundation at col 4, Tableaux cols 0..6
        piles_[0].x = startX + 2.0f * (cardW_ + colGap);
        piles_[0].y = topRowY;
        piles_[1].x = startX + 4.0f * (cardW_ + colGap);
        piles_[1].y = topRowY;
        for (uint8_t i = 0; i < 7; ++i) {
            piles_[2 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[2 + i].y = tableauY;
        }
    } else if (variant_ == GameVariant::FreeCell || (activeDef_.freecells > 0 && activeDef_.layout != "canfield")) {
        const uint8_t numFC = activeDef_.freecells > 0 ? activeDef_.freecells : 4;
        const uint8_t numF = activeDef_.foundations > 0 ? activeDef_.foundations : 4;
        const uint8_t numT = activeDef_.tableaus > 0 ? activeDef_.tableaus : 8;
        const size_t tabBase = static_cast<size_t>(numFC) + numF;
        for (uint8_t i = 0; i < numFC && i < piles_.size(); ++i) {
            piles_[i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[i].y = topRowY;
        }
        for (uint8_t i = 0; i < numF && (numFC + i) < piles_.size(); ++i) {
            piles_[numFC + i].x = startX + static_cast<float>(numFC + i) * (cardW_ + colGap);
            piles_[numFC + i].y = topRowY;
        }
        const float tabOffset = (numCols > numT) ? 0.5f * static_cast<float>(numCols - numT) * (cardW_ + colGap) : 0.0f;
        for (uint8_t i = 0; i < numT && (tabBase + i) < piles_.size(); ++i) {
            piles_[tabBase + i].x = std::round(startX + tabOffset + static_cast<float>(i) * (cardW_ + colGap));
            piles_[tabBase + i].y = tableauY;
        }
    } else if (hasScriptHooks() && activeDef_.layout == "columns" && piles_.size() >= 3) {
        const uint8_t numF = activeDef_.foundations > 0 ? activeDef_.foundations : 1;
        const uint8_t numT = activeDef_.tableaus > 0 ? activeDef_.tableaus : 4;
        const size_t tabBase = 2 + numF;
        piles_[0].x = startX;
        piles_[0].y = topRowY;
        piles_[1].x = startX + (cardW_ + colGap);
        piles_[1].y = topRowY;
        for (uint8_t i = 0; i < numF && (2 + i) < piles_.size(); ++i) {
            piles_[2 + i].x = startX + static_cast<float>(2 + i) * (cardW_ + colGap);
            piles_[2 + i].y = topRowY;
        }
        for (uint8_t i = 0; i < numT && (tabBase + i) < piles_.size(); ++i) {
            piles_[tabBase + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
            piles_[tabBase + i].y = tableauY;
        }
    } else {
        // Klondike Draw 1 & Draw 3
        if (hasBottomStock) {
            // Thumb-zone layout: 4 Foundations centered at top, Stock and Waste at bottom rail
            const float foundStartX = startX + 1.5f * (cardW_ + colGap);
            for (uint8_t i = 0; i < 4; ++i) {
                piles_[2 + i].x = std::round(foundStartX + static_cast<float>(i) * (cardW_ + colGap));
                piles_[2 + i].y = topRowY;
            }
            for (uint8_t i = 0; i < 7; ++i) {
                piles_[6 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
                piles_[6 + i].y = tableauY;
            }

            const float bottomRailY = std::round(viewportH_ - bottomReserved - cardH_ - 4.0f);
            if (effectiveStock == StockPosition::BottomRight) {
                // Stock at column 6 (rightmost), Waste at column 5
                piles_[0].x = startX + 6.0f * (cardW_ + colGap);
                piles_[0].y = bottomRailY;
                piles_[1].x = startX + 5.0f * (cardW_ + colGap);
                piles_[1].y = bottomRailY;
            } else {
                // BottomLeft: Stock at column 0 (leftmost), Waste at column 1
                piles_[0].x = startX;
                piles_[0].y = bottomRailY;
                piles_[1].x = startX + (cardW_ + colGap);
                piles_[1].y = bottomRailY;
            }
        } else {
            // Traditional Top-Left stock layout
            piles_[0].x = startX;
            piles_[0].y = topRowY;
            piles_[1].x = startX + (cardW_ + colGap);
            piles_[1].y = topRowY;

            for (uint8_t i = 0; i < 4; ++i) {
                piles_[2 + i].x = startX + static_cast<float>(3 + i) * (cardW_ + colGap);
                piles_[2 + i].y = topRowY;
            }
            for (uint8_t i = 0; i < 7; ++i) {
                piles_[6 + i].x = startX + static_cast<float>(i) * (cardW_ + colGap);
                piles_[6 + i].y = tableauY;
            }
        }
    }

    updateCardTargets();
}

void SolitaireBoard::setCardDeviation(float dev) noexcept {
    cardDeviation_ = std::clamp(dev, 0.0f, 1.0f);
    updateCardTargets();
}

void SolitaireBoard::updateCardTargets() {
    statusCacheDirty_ = true;
    bool bottomStockActive = false;
    float bottomRailStockY = viewportH_ - 56.0f;
    if (variant_ == GameVariant::KlondikeDraw1 || variant_ == GameVariant::KlondikeDraw3) {
        if (!piles_.empty() && piles_[0].y > (viewportH_ * 0.5f)) {
            bottomStockActive = true;
            bottomRailStockY = piles_[0].y - 8.0f;
        }
    }
    // 14px safety clearance above bottom molding bar (or clearance above bottom stock)
    const float maxTableauBottom = bottomStockActive ? bottomRailStockY : (viewportH_ - 56.0f);

    // Vector-proportional deviation: 0% = 0px, 0 deg; 100% = up to +-10px, +-5 deg
    const float cardScale = cardW_ / 96.0f;
    const float maxPosDev = 10.0f * cardScale * cardDeviation_;
    const float maxAngleRad = (5.0f * (kPi / 180.0f)) * cardDeviation_;

    for (size_t pIdx = 0; pIdx < piles_.size(); ++pIdx) {
        auto& p = piles_[pIdx];
        if ((isPyramidGame() && pIdx >= 3 && pIdx <= 30) ||
            (activeDef_.layout == "tripeaks" && pIdx >= 2 && pIdx <= 29)) {
            for (auto& c : p.cards) {
                c.targetX = p.x;
                c.targetY = p.y;
                c.targetAngle = p.angle;
                c.targetElevation = (static_cast<int>(pIdx) == selectedPileIdx_) ? 14.0f : 0.0f;
                c.enshadedGlow = (static_cast<int>(pIdx) == selectedPileIdx_);
            }
            continue;
        }
        if (p.type == PileType::Tableau) {
            float downStep = std::round(cardH_ * 0.14f);
            float upStep = std::round(cardH_ * 0.24f);

            const bool isStandardVerticalFan = (std::abs(p.fanDirX) < 0.001f && p.fanDirY > 0.999f);
            if (isStandardVerticalFan) {
                size_t downCount = 0;
                size_t upCount = 0;
                for (const auto& c : p.cards) {
                    if (c.faceUp) ++upCount;
                    else ++downCount;
                }
                const float projectedH = p.y + cardH_ + static_cast<float>(downCount) * downStep +
                                         (upCount > 0 ? static_cast<float>(upCount - 1) * upStep : 0.0f);
                if (projectedH > maxTableauBottom && (downCount + upCount) > 1) {
                    const float availableSpan = maxTableauBottom - p.y - cardH_;
                    const float neededSpan = static_cast<float>(downCount) * downStep +
                                             static_cast<float>(upCount > 0 ? upCount - 1 : 0) * upStep;
                    const float scale = availableSpan / std::max(1.0f, neededSpan);

                    // Minimum step bounds proportional to card scale so indices/ranks are always clearly legible
                    const float minDownStep = std::max(6.0f, std::round(cardH_ * 0.06f));
                    const float minUpStep = std::max(14.0f, std::round(cardH_ * 0.12f));

                    downStep = std::max(minDownStep, std::floor(downStep * scale));
                    upStep = std::max(minUpStep, std::floor(upStep * scale));
                }
            } else {
                downStep = std::round(cardH_ * 0.08f);
                upStep = std::round(cardH_ * 0.16f);
            }
            p.fanOffsetDown = downStep;
            p.fanOffsetUp = upStep;

            float curX = p.x;
            float curY = p.y;
            for (size_t i = 0; i < p.cards.size(); ++i) {
                // Subtle organic tactile micro-jitter for hand-dealt tableau feel
                const float jx = cardHashNoise(seed_, p.cards[i].id, 101) * maxPosDev;
                const float jy = cardHashNoise(seed_, p.cards[i].id, 102) * (maxPosDev * 0.60f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 103) * maxAngleRad;

                p.cards[i].targetX = curX + jx;
                p.cards[i].targetY = curY + jy;
                p.cards[i].targetAngle = p.angle + ja;
                p.cards[i].targetElevation = 0.0f;
                const float step = p.cards[i].faceUp ? upStep : downStep;
                curX += p.fanDirX * step;
                curY += p.fanDirY * step;
            }
        } else if (p.type == PileType::Waste) {
            const size_t n = p.cards.size();
            for (size_t i = 0; i < n; ++i) {
                float fanOffset = 0.0f;
                if (variant_ == GameVariant::KlondikeDraw3 && n > 1) {
                    size_t visibleStart = (n > 3) ? (n - 3) : 0;
                    size_t visibleIdx = (i >= visibleStart) ? (i - visibleStart) : 0;
                    fanOffset = static_cast<float>(visibleIdx) * std::round(cardW_ * 0.22f);
                }
                const float jx = cardHashNoise(seed_, p.cards[i].id, 201) * (maxPosDev * 0.80f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 202) * (maxPosDev * 0.70f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 203) * (maxAngleRad * 0.85f);

                p.cards[i].targetX = p.x + fanOffset + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = p.angle + ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else if (p.type == PileType::Stock) {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                // Stratified physical deck edge thickness & organic stacking
                const float jx = cardHashNoise(seed_, p.cards[i].id, 301) * (maxPosDev * 0.75f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 302) * (maxPosDev * 0.75f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 303) * (maxAngleRad * 0.75f);

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = p.angle + ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else if (p.type == PileType::Foundation) {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                const float jx = cardHashNoise(seed_, p.cards[i].id, 401) * (maxPosDev * 0.60f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 402) * (maxPosDev * 0.60f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 403) * (maxAngleRad * 0.65f);

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = p.angle + ja;
                p.cards[i].targetElevation = 0.0f;
            }
        } else {
            for (size_t i = 0; i < p.cards.size(); ++i) {
                const float jx = cardHashNoise(seed_, p.cards[i].id, 501) * (maxPosDev * 0.60f);
                const float jy = cardHashNoise(seed_, p.cards[i].id, 502) * (maxPosDev * 0.60f);
                const float ja = cardHashNoise(seed_, p.cards[i].id, 503) * (maxAngleRad * 0.65f);

                p.cards[i].targetX = p.x + jx;
                p.cards[i].targetY = p.y + jy;
                p.cards[i].targetAngle = p.angle + ja;
                p.cards[i].targetElevation = 0.0f;
            }
        }
    }

    if (selectedPileIdx_ >= 0 && static_cast<size_t>(selectedPileIdx_) < piles_.size()) {
        auto& sp = piles_[static_cast<size_t>(selectedPileIdx_)];
        if (!sp.cards.empty()) {
            sp.cards.back().targetElevation = 14.0f;
            sp.cards.back().enshadedGlow = true;
        }
    }
}

void SolitaireBoard::updateAnimations(float dt) {
    const float clampedDt = std::min(dt, 0.05f);
    if (!isVictory()) {
        elapsedSeconds_ += clampedDt;
        victorySeconds_ = 0.0f;
    } else {
        victorySeconds_ += clampedDt;
    }

    const float springLerp = 1.0f - std::exp(-18.0f * clampedDt);
    const float flipLerp = 1.0f - std::exp(-10.0f * clampedDt);
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            float dx = c.targetX - c.x;
            float dy = c.targetY - c.y;
            float dist = std::hypot(dx, dy);
            c.x += dx * springLerp;
            c.y += dy * springLerp;

            float flightLift = std::clamp(dist * 0.09f, 0.0f, 14.0f);
            float desiredElev = std::max(c.targetElevation, flightLift);
            c.elevation += (desiredElev - c.elevation) * springLerp;

            // Aerodynamic flight banking proportional to horizontal motion + spring settling to resting angle
            const float flightBank = std::clamp(dx * 0.0016f, -0.065f, 0.065f);
            const float desiredAngle = (dist > 3.0f) ? (c.targetAngle + flightBank) : c.targetAngle;
            c.angle += (desiredAngle - c.angle) * springLerp;

            if (c.flipAngle > 0.001f) {
                c.flipAngle *= (1.0f - flipLerp);
                if (c.flipAngle < 0.01f) {
                    c.flipAngle = 0.0f;
                }
            }
        }
    }
}

bool SolitaireBoard::drawFromStock() {
    if (scriptEngine_.hasOnStockClick()) {
        clearHint();
        TurnTransaction tx;
        auto ctx = makeScriptContext(&tx);
        if (!scriptEngine_.invokeOnStockClick(ctx)) {
            return false;
        }
        if (scriptEngine_.hasOnTurnEnd()) {
            scriptEngine_.invokeOnTurnEnd(ctx);
        }
        if (!tx.ops.empty()) {
            txUndoStack_.push_back(std::move(tx));
            txRedoStack_.clear();
        }
        ++moveCount_;
        updateCardTargets();
        return true;
    }

    if (piles_.empty() || piles_[0].type != PileType::Stock) {
        return false;
    }
    clearHint();
    Pile& stock = piles_[0];

    if (isSpiderVariant(variant_)) {
        if (stock.cards.empty()) return false;
        // In standard Spider / Spiderette, no tableau column can be empty to deal from stock
        std::vector<size_t> tabIndices;
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type == PileType::Tableau) {
                if (piles_[i].cards.empty()) {
                    return false;
                }
                tabIndices.push_back(i);
            }
        }
        const size_t dealCount = std::min(tabIndices.size(), stock.cards.size());
        for (size_t col = 0; col < dealCount; ++col) {
            if (stock.cards.empty()) break;
            Card c = stock.cards.back();
            stock.cards.pop_back();
            c.faceUp = true;
            c.flipAngle = kPi;
            c.elevation = std::max(c.elevation, 6.0f);
            piles_[tabIndices[col]].cards.push_back(c);
        }
        MoveCommand cmd{};
        cmd.fromPile = 0;
        cmd.toPile = 0;
        cmd.cardCount = static_cast<uint8_t>(dealCount);
        cmd.spiderStockDeal = true;
        undoStack_.push_back(cmd);
        redoStack_.clear();
        ++moveCount_;
        collectCompletedSpiderRuns();
        updateCardTargets();
        return true;
    }

    if (isGolf()) {
        if (stock.cards.empty()) return false;
        Pile& found = piles_[1];
        Card c = stock.cards.back();
        stock.cards.pop_back();
        c.faceUp = true;
        c.flipAngle = kPi;
        c.elevation = std::max(c.elevation, 6.0f);
        found.cards.push_back(c);

        MoveCommand cmd{};
        cmd.fromPile = 0;
        cmd.toPile = 1;
        cmd.cardCount = 1;
        undoStack_.push_back(cmd);
        redoStack_.clear();
        ++moveCount_;
        updateCardTargets();
        return true;
    }

    if (piles_.size() < 2) {
        return false;
    }
    Pile& waste = piles_[1];

    if (!stock.cards.empty()) {
        const uint8_t drawCount = (variant_ == GameVariant::KlondikeDraw3)
            ? static_cast<uint8_t>(std::min<size_t>(3, stock.cards.size()))
            : 1u;
        for (uint8_t i = 0; i < drawCount; ++i) {
            Card c = stock.cards.back();
            stock.cards.pop_back();
            c.faceUp = true;
            c.flipAngle = kPi; // 2.5D turn-over animation
            c.elevation = 9.0f;
            waste.cards.push_back(c);
        }
        MoveCommand cmd{};
        cmd.fromPile = 0;
        cmd.toPile = 1;
        cmd.cardCount = drawCount;
        undoStack_.push_back(cmd);
        redoStack_.clear();
        ++moveCount_;
        updateCardTargets();
        return true;
    } else if (!waste.cards.empty()) {
        const uint8_t recycledCount = static_cast<uint8_t>(waste.cards.size());
        while (!waste.cards.empty()) {
            Card c = waste.cards.back();
            waste.cards.pop_back();
            c.faceUp = false;
            c.flipAngle = 0.0f;
            c.elevation = 6.0f;
            stock.cards.push_back(c);
        }
        MoveCommand cmd{};
        cmd.fromPile = 1;
        cmd.toPile = 0;
        cmd.cardCount = recycledCount;
        cmd.recycledWaste = true;
        undoStack_.push_back(cmd);
        redoStack_.clear();
        ++moveCount_;
        updateCardTargets();
        return true;
    }
    return false;
}

bool SolitaireBoard::isPyramidGame() const noexcept {
    return activeDef_.layout == "pyramid" || activeDef_.family == "Pyramid" || activeDef_.id == "pyramid" || activeDef_.id == "giza";
}

bool SolitaireBoard::isCardExposedInPyramid(size_t pileIdx) const noexcept {
    if (pileIdx < 3 || pileIdx > 30) return false;
    if (piles_[pileIdx].cards.empty()) return false;
    const size_t k = pileIdx - 3;
    uint8_t r = 0;
    if (k >= 21) r = 6;
    else if (k >= 15) r = 5;
    else if (k >= 10) r = 4;
    else if (k >= 6) r = 3;
    else if (k >= 3) r = 2;
    else if (k >= 1) r = 1;
    else r = 0;

    if (r == 6) return true;

    const size_t child1 = 3 + k + r + 1;
    const size_t child2 = 3 + k + r + 2;
    if (child1 >= piles_.size() || child2 >= piles_.size()) return true;

    return piles_[child1].cards.empty() && piles_[child2].cards.empty();
}

bool SolitaireBoard::isCardPlayableInPyramid(size_t pileIdx) const noexcept {
    if (pileIdx >= piles_.size()) return false;
    if (piles_[pileIdx].cards.empty()) return false;
    if (pileIdx == 1) return true; // Top card of Waste
    if (pileIdx >= 3 && pileIdx <= 30) return isCardExposedInPyramid(pileIdx);
    if (pileIdx >= 31 && pileIdx <= 38) return true; // Top card of Giza reserve
    return false;
}

void SolitaireBoard::clearSelection() noexcept {
    if (selectedPileIdx_ >= 0 && static_cast<size_t>(selectedPileIdx_) < piles_.size()) {
        auto& sp = piles_[static_cast<size_t>(selectedPileIdx_)];
        if (!sp.cards.empty()) {
            sp.cards.back().targetElevation = 0.0f;
            sp.cards.back().enshadedGlow = false;
        }
    }
    selectedPileIdx_ = -1;
}

bool SolitaireBoard::executePyramidPair(uint8_t pileA, uint8_t pileB, uint8_t foundationPile) {
    if (pileA >= piles_.size() || pileB >= piles_.size() || foundationPile >= piles_.size()) return false;
    if (scriptEngine_.hasCanTarget() && scriptEngine_.hasOnTarget()) {
        if (piles_[pileA].cards.empty() || piles_[pileB].cards.empty()) return false;
        return executeCardTarget(pileA, piles_[pileA].cards.size() - 1, pileB, piles_[pileB].cards.size() - 1, true);
    }
    Pile& srcA = piles_[pileA];
    Pile& srcB = piles_[pileB];
    Pile& dst = piles_[foundationPile];
    if (srcA.cards.empty() || srcB.cards.empty()) return false;

    Card cA = srcA.cards.back();
    srcA.cards.pop_back();
    cA.elevation = 14.0f;
    cA.enshadedGlow = false;
    dst.cards.push_back(cA);

    Card cB = srcB.cards.back();
    srcB.cards.pop_back();
    cB.elevation = 14.0f;
    cB.enshadedGlow = false;
    dst.cards.push_back(cB);

    MoveCommand cmd{};
    cmd.fromPile = pileA;
    cmd.toPile = foundationPile;
    cmd.cardCount = 1;
    cmd.isPairMove = true;
    cmd.secondFromPile = pileB;
    cmd.scoreDelta = 100;
    undoStack_.push_back(cmd);
    redoStack_.clear();

    score_ += 100;
    ++moveCount_;
    updateCardTargets();
    return true;
}

bool SolitaireBoard::canMoveCards(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx) const {
    if (fromPileIdx >= piles_.size() || toPileIdx >= piles_.size() || fromPileIdx == toPileIdx) {
        return false;
    }
    const Pile& src = piles_[fromPileIdx];
    const Pile& dst = piles_[toPileIdx];
    if (startCardIdx >= src.cards.size()) {
        return false;
    }

    const Card& lead = src.cards[startCardIdx];
    if (scriptEngine_.hasCanPick() || scriptEngine_.hasCanDrop()) {
        auto ctx = makeConstScriptContext();
        if (scriptEngine_.hasCanPick() && !scriptEngine_.invokeCanPick(ctx, fromPileIdx, startCardIdx)) {
            return false;
        }
        if (scriptEngine_.hasCanDrop()) {
            return scriptEngine_.invokeCanDrop(ctx, fromPileIdx, startCardIdx, toPileIdx);
        }
    }
    if (!lead.faceUp) {
        return false;
    }
    const size_t moveCount = src.cards.size() - startCardIdx;

    if (isPyramidGame()) {
        if (toPileIdx == 2) {
            if (moveCount != 1) return false;
            if (!isCardPlayableInPyramid(fromPileIdx)) return false;
            return lead.rank == Rank::King;
        }
        if (!isCardPlayableInPyramid(fromPileIdx) || !isCardPlayableInPyramid(toPileIdx)) return false;
        if (dst.cards.empty()) return false;
        const Card& top = dst.cards.back();
        return (static_cast<int>(lead.rank) + static_cast<int>(top.rank) == 13);
    }

    if (moveCount > 1 && src.type != PileType::Tableau) {
        return false;
    }

    if (isGolf()) {
        // In Golf, only top exposed card of a Tableau column can move to Foundation (Pile 1)
        if (src.type != PileType::Tableau || dst.type != PileType::Foundation || moveCount != 1) {
            return false;
        }
        if (startCardIdx != src.cards.size() - 1) {
            return false;
        }
        if (dst.cards.empty()) {
            return true;
        }
        const Card& top = dst.cards.back();
        // In classic Golf, Kings cannot be built upon
        if (top.rank == Rank::King) {
            return false;
        }
        const int movingRank = static_cast<int>(lead.rank);
        const int topRank = static_cast<int>(top.rank);
        return (movingRank == topRank + 1) || (movingRank == topRank - 1);
    }

    if (activeDef_.family == "Yukon") {
        if (dst.type == PileType::Foundation) {
            if (moveCount != 1) return false;
            if (dst.cards.empty()) return lead.rank == Rank::Ace;
            const Card& top = dst.cards.back();
            return (lead.suit == top.suit) && (static_cast<int>(lead.rank) == static_cast<int>(top.rank) + 1);
        }
        if (dst.type == PileType::Tableau) {
            if (dst.cards.empty()) return lead.rank == Rank::King;
            const Card& top = dst.cards.back();
            if (!top.faceUp) return false;
            if (activeDef_.id == "russian_solitaire" || activeDef_.name.find("Russian") != std::string::npos) {
                return (lead.suit == top.suit) && (static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1);
            }
            return (isRedSuit(lead.suit) != isRedSuit(top.suit)) && (static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1);
        }
        return false;
    }

    if (activeDef_.family == "FortyThieves") {
        if (moveCount != 1) return false;
        if (dst.type == PileType::Foundation) {
            if (dst.cards.empty()) return lead.rank == Rank::Ace;
            const Card& top = dst.cards.back();
            return (lead.suit == top.suit) && (static_cast<int>(lead.rank) == static_cast<int>(top.rank) + 1);
        }
        if (dst.type == PileType::Tableau) {
            if (dst.cards.empty()) return true;
            const Card& top = dst.cards.back();
            if (!top.faceUp) return false;
            return (lead.suit == top.suit) && (static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1);
        }
        return false;
    }

    if (isSpiderVariant(variant_)) {
        // In Spider, multi-card moves must be of the EXACT SAME SUIT and in descending rank sequence!
        if (moveCount > 1) {
            for (size_t i = startCardIdx; i + 1 < src.cards.size(); ++i) {
                const Card& upper = src.cards[i];
                const Card& lower = src.cards[i + 1];
                if (upper.suit != lower.suit || static_cast<int>(upper.rank) != static_cast<int>(lower.rank) + 1) {
                    return false;
                }
            }
        }
        // In Spider, destinations must be Tableau columns
        if (dst.type != PileType::Tableau) {
            return false;
        }
        if (dst.cards.empty()) {
            return true; // Any card or legal sequence can move to empty column
        }
        const Card& top = dst.cards.back();
        if (!top.faceUp) return false;
        // In Spider: can drop on any card of rank + 1, REGARDLESS OF SUIT!
        return static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1;
    }

    if (moveCount > 1) {
        for (size_t i = startCardIdx; i + 1 < src.cards.size(); ++i) {
            const Card& upper = src.cards[i];
            const Card& lower = src.cards[i + 1];
            if (activeDef_.id == "bakers_game" || activeDef_.name.find("Baker") != std::string::npos) {
                if (upper.suit != lower.suit || static_cast<int>(upper.rank) != static_cast<int>(lower.rank) + 1) {
                    return false;
                }
            } else {
                if (isRedSuit(upper.suit) == isRedSuit(lower.suit) ||
                    static_cast<int>(upper.rank) != static_cast<int>(lower.rank) + 1) {
                    return false;
                }
            }
        }
    }

    if (dst.type == PileType::Foundation) {
        if (moveCount != 1) return false;
        if (dst.cards.empty()) {
            return lead.rank == Rank::Ace;
        }
        const Card& top = dst.cards.back();
        return (lead.suit == top.suit) &&
               (static_cast<int>(lead.rank) == static_cast<int>(top.rank) + 1);
    }

    if (dst.type == PileType::FreeCell) {
        return moveCount == 1 && dst.cards.empty();
    }

    if (dst.type == PileType::Tableau) {
        if (dst.cards.empty()) {
            if (variant_ == GameVariant::FreeCell || (activeDef_.freecells == 4 && activeDef_.tableaus == 8)) {
                return true;
            }
            return lead.rank == Rank::King;
        }
        const Card& top = dst.cards.back();
        if (!top.faceUp) return false;
        if (activeDef_.id == "bakers_game" || activeDef_.name.find("Baker") != std::string::npos) {
            return (lead.suit == top.suit) && (static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1);
        }
        return (isRedSuit(lead.suit) != isRedSuit(top.suit)) &&
               (static_cast<int>(top.rank) == static_cast<int>(lead.rank) + 1);
    }

    return false;
}

bool SolitaireBoard::executeMove(uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx, bool recordUndo) {
    if (hasScriptHooks() && (scriptEngine_.hasCanTarget() || scriptEngine_.hasCanDrop() || scriptEngine_.hasCanPick())) {
        if (fromPileIdx < piles_.size() && toPileIdx < piles_.size() && fromPileIdx != toPileIdx && !piles_[toPileIdx].cards.empty()) {
            const size_t dstTop = piles_[toPileIdx].cards.size() - 1;
            auto ctx = makeConstScriptContext();
            if (scriptEngine_.hasCanTarget() && scriptEngine_.invokeCanTarget(ctx, fromPileIdx, startCardIdx, toPileIdx, dstTop)) {
                return executeCardTarget(fromPileIdx, startCardIdx, toPileIdx, dstTop, recordUndo);
            }
        }
        if (!canMoveCards(fromPileIdx, startCardIdx, toPileIdx)) {
            return false;
        }
        clearHint();
        TurnTransaction tx;
        auto ctx = makeScriptContext(&tx);
        if (scriptEngine_.hasOnDrop()) {
            if (!scriptEngine_.invokeOnDrop(ctx, fromPileIdx, startCardIdx, toPileIdx)) {
                ScriptRuleEngine::revertTransaction(ctx, tx);
                return false;
            }
        } else {
            BoardOp op{};
            op.type = BoardOpType::MoveCards;
            op.fromPile = fromPileIdx;
            op.toPile = toPileIdx;
            op.cardIdx = static_cast<uint32_t>(startCardIdx);
            op.count = static_cast<uint32_t>(piles_[fromPileIdx].cards.size() - startCardIdx);
            if (ScriptRuleEngine::applyOp(ctx, op)) {
                tx.ops.push_back(std::move(op));
            }
        }
        if (scriptEngine_.hasOnTurnEnd()) {
            scriptEngine_.invokeOnTurnEnd(ctx);
        }
        if (recordUndo && !tx.ops.empty()) {
            txUndoStack_.push_back(std::move(tx));
            txRedoStack_.clear();
        }
        ++moveCount_;
        updateCardTargets();
        return true;
    }

    if (!canMoveCards(fromPileIdx, startCardIdx, toPileIdx)) {
        return false;
    }
    clearHint();

    if (isPyramidGame()) {
        if (toPileIdx == 2) {
            Pile& src = piles_[fromPileIdx];
            Pile& dst = piles_[2];
            Card c = src.cards.back();
            src.cards.pop_back();
            c.elevation = 14.0f;
            c.enshadedGlow = false;
            dst.cards.push_back(c);

            MoveCommand cmd{};
            cmd.fromPile = fromPileIdx;
            cmd.toPile = 2;
            cmd.cardCount = 1;
            cmd.scoreDelta = 100;
            if (recordUndo) {
                undoStack_.push_back(cmd);
                redoStack_.clear();
            }
            score_ += 100;
            ++moveCount_;
            clearSelection();
            updateCardTargets();
            return true;
        } else {
            clearSelection();
            return executePyramidPair(fromPileIdx, toPileIdx, 2);
        }
    }

    Pile& src = piles_[fromPileIdx];
    Pile& dst = piles_[toPileIdx];
    const uint8_t count = static_cast<uint8_t>(src.cards.size() - startCardIdx);

    for (size_t i = startCardIdx; i < src.cards.size(); ++i) {
        Card c = src.cards[i];
        c.elevation = std::max(c.elevation, 11.0f);
        dst.cards.push_back(c);
    }
    src.cards.erase(src.cards.begin() + static_cast<std::ptrdiff_t>(startCardIdx), src.cards.end());

    bool flippedTop = false;
    if (src.type == PileType::Tableau && !src.cards.empty() && !src.cards.back().faceUp) {
        src.cards.back().faceUp = true;
        src.cards.back().flipAngle = kPi; // 2.5D card flip animation
        flippedTop = true;
    }

    int32_t scoreDelta = 0;
    if (dst.type == PileType::Foundation) scoreDelta += 10;
    if (src.type == PileType::Waste && dst.type == PileType::Tableau) scoreDelta += 5;
    if (flippedTop) scoreDelta += 5;
    score_ += scoreDelta;
    ++moveCount_;

    if (recordUndo) {
        MoveCommand cmd{};
        cmd.fromPile = fromPileIdx;
        cmd.toPile = toPileIdx;
        cmd.cardCount = count;
        cmd.flippedSourceTop = flippedTop;
        cmd.scoreDelta = scoreDelta;
        undoStack_.push_back(cmd);
        redoStack_.clear();
    }

    if (isSpiderVariant(variant_)) {
        collectCompletedSpiderRuns();
    }

    updateCardTargets();
    return true;
}

bool SolitaireBoard::collectCompletedSpiderRuns() {
    if (!isSpiderVariant(variant_)) return false;
    bool collectedAny = false;
    for (size_t tIdx = 0; tIdx < piles_.size(); ++tIdx) {
        if (piles_[tIdx].type != PileType::Tableau) continue;
        Pile& tab = piles_[tIdx];
        if (tab.cards.size() < 13) continue;
        const size_t n = tab.cards.size();
        if (!tab.cards.back().faceUp || tab.cards.back().rank != Rank::Ace) continue;
        const Suit leadSuit = tab.cards.back().suit;

        bool isRun = true;
        for (size_t i = 0; i < 13; ++i) {
            const Card& c = tab.cards[n - 1 - i];
            if (!c.faceUp || c.suit != leadSuit || static_cast<int>(c.rank) != static_cast<int>(i + 1)) {
                isRun = false;
                break;
            }
        }
        if (!isRun) continue;

        // Find empty foundation
        int foundIdx = -1;
        for (size_t f = 0; f < piles_.size(); ++f) {
            if (piles_[f].type == PileType::Foundation && piles_[f].cards.empty()) {
                foundIdx = static_cast<int>(f);
                break;
            }
        }
        if (foundIdx < 0) continue;

        Pile& found = piles_[static_cast<size_t>(foundIdx)];
        for (size_t i = n - 13; i < n; ++i) {
            Card c = tab.cards[i];
            c.elevation = 14.0f;
            found.cards.push_back(c);
        }
        tab.cards.erase(tab.cards.begin() + static_cast<std::ptrdiff_t>(n - 13), tab.cards.end());

        if (!tab.cards.empty() && !tab.cards.back().faceUp) {
            tab.cards.back().faceUp = true;
            tab.cards.back().flipAngle = kPi;
        }
        score_ += 100;
        collectedAny = true;
    }
    if (collectedAny) {
        updateCardTargets();
    }
    return collectedAny;
}

bool SolitaireBoard::executeCardTarget(uint8_t fromPileIdx, size_t fromCardIdx, uint8_t toPileIdx, size_t toCardIdx, bool recordUndo) {
    if (!scriptEngine_.hasOnTarget()) return false;
    auto constCtx = makeConstScriptContext();
    if (scriptEngine_.hasCanTarget() && !scriptEngine_.invokeCanTarget(constCtx, fromPileIdx, fromCardIdx, toPileIdx, toCardIdx)) {
        return false;
    }
    clearHint();
    TurnTransaction tx;
    auto ctx = makeScriptContext(&tx);
    if (!scriptEngine_.invokeOnTarget(ctx, fromPileIdx, fromCardIdx, toPileIdx, toCardIdx)) {
        return false;
    }
    if (scriptEngine_.hasOnTurnEnd()) {
        scriptEngine_.invokeOnTurnEnd(ctx);
    }
    if (recordUndo && !tx.ops.empty()) {
        txUndoStack_.push_back(std::move(tx));
        txRedoStack_.clear();
    }
    ++moveCount_;
    updateCardTargets();
    return true;
}

bool SolitaireBoard::smartMove(uint8_t fromPileIdx, size_t startCardIdx) {
    if (fromPileIdx >= piles_.size()) return false;
    const Pile& src = piles_[fromPileIdx];
    if (startCardIdx >= src.cards.size()) return false;

    if (scriptEngine_.hasOnCardClick()) {
        clearHint();
        TurnTransaction tx;
        auto ctx = makeScriptContext(&tx);
        if (scriptEngine_.invokeOnCardClick(ctx, fromPileIdx, startCardIdx)) {
            if (scriptEngine_.hasOnTurnEnd()) {
                scriptEngine_.invokeOnTurnEnd(ctx);
            }
            if (!tx.ops.empty()) {
                txUndoStack_.push_back(std::move(tx));
                txRedoStack_.clear();
            }
            ++moveCount_;
            updateCardTargets();
            return true;
        }
    }

    if (hasScriptHooks() && (scriptEngine_.hasCanDrop() || scriptEngine_.hasCanTarget())) {
        for (uint8_t dst = 0; dst < piles_.size(); ++dst) {
            if (dst == fromPileIdx) continue;
            if (executeMove(fromPileIdx, startCardIdx, dst)) {
                return true;
            }
        }
        return false;
    }

    if (isPyramidGame()) {
        if (!isCardPlayableInPyramid(fromPileIdx)) return false;
        const Card& card = src.cards.back();

        if (card.rank == Rank::King) {
            clearSelection();
            return executeMove(fromPileIdx, src.cards.size() - 1, 2);
        }

        if (selectedPileIdx_ >= 0) {
            if (selectedPileIdx_ == static_cast<int>(fromPileIdx)) {
                clearSelection();
                updateCardTargets();
                return true;
            }
            Pile& selPile = piles_[static_cast<size_t>(selectedPileIdx_)];
            if (!selPile.cards.empty() && isCardPlayableInPyramid(static_cast<size_t>(selectedPileIdx_))) {
                const int rankA = static_cast<int>(selPile.cards.back().rank);
                const int rankB = static_cast<int>(card.rank);
                if (rankA + rankB == 13) {
                    const uint8_t pA = static_cast<uint8_t>(selectedPileIdx_);
                    clearSelection();
                    return executePyramidPair(pA, fromPileIdx, 2);
                }
            }
            clearSelection();
            selectedPileIdx_ = static_cast<int>(fromPileIdx);
            updateCardTargets();
            return true;
        }

        if (fromPileIdx != 1 && !piles_[1].cards.empty()) {
            const int rankW = static_cast<int>(piles_[1].cards.back().rank);
            if (rankW + static_cast<int>(card.rank) == 13) {
                clearSelection();
                return executePyramidPair(1, fromPileIdx, 2);
            }
        }

        selectedPileIdx_ = static_cast<int>(fromPileIdx);
        updateCardTargets();
        return true;
    }

    if (isGolf()) {
        if (src.type != PileType::Tableau || startCardIdx + 1 != src.cards.size()) return false;
        if (canMoveCards(fromPileIdx, startCardIdx, 1)) {
            return executeMove(fromPileIdx, startCardIdx, 1);
        }
        return false;
    }

    if (isSpiderVariant(variant_)) {
        const size_t moveCount = src.cards.size() - startCardIdx;
        const Card& lead = src.cards[startCardIdx];
        if (!lead.faceUp) return false;
        if (moveCount > 1) {
            for (size_t i = startCardIdx; i + 1 < src.cards.size(); ++i) {
                if (src.cards[i].suit != src.cards[i + 1].suit ||
                    static_cast<int>(src.cards[i].rank) != static_cast<int>(src.cards[i + 1].rank) + 1) {
                    return false;
                }
            }
        }
        // Priority 1: Destination with same suit!
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type != PileType::Tableau || i == fromPileIdx) continue;
            const Pile& dst = piles_[i];
            if (!dst.cards.empty() && dst.cards.back().faceUp && dst.cards.back().suit == lead.suit &&
                canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
            }
        }
        // Priority 2: Destination with different suit
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type != PileType::Tableau || i == fromPileIdx) continue;
            const Pile& dst = piles_[i];
            if (!dst.cards.empty() && canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
            }
        }
        // Priority 3: Empty column
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type != PileType::Tableau || i == fromPileIdx) continue;
            if (piles_[i].cards.empty() && canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
            }
        }
        return false;
    }

    // 1. If single top card, try Foundation first (Hardwood 3.0 priority)
    if (startCardIdx + 1 == src.cards.size()) {
        const Card& lead = src.cards[startCardIdx];
        // If Ace, prioritize the matching foundation suit slot (Spades=0, Hearts=1, Diamonds=2, Clubs=3)
        if (lead.rank == Rank::Ace) {
            for (size_t i = 0; i < piles_.size(); ++i) {
                if (piles_[i].type == PileType::Foundation &&
                    (piles_[i].index % 4) == static_cast<uint8_t>(lead.suit) &&
                    canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                    return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
                }
            }
        }
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type == PileType::Foundation &&
                canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
                return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
            }
        }
    }

    // 2. Try non-empty Tableau columns first (preferring building on existing cards)
    for (size_t i = 0; i < piles_.size(); ++i) {
        if (piles_[i].type == PileType::Tableau && !piles_[i].cards.empty() &&
            canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
            return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
        }
    }

    // 3. Try empty Tableau column
    for (size_t i = 0; i < piles_.size(); ++i) {
        if (piles_[i].type == PileType::Tableau && piles_[i].cards.empty() &&
            canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
            return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
        }
    }

    // 4. Try FreeCell slot if single card
    for (size_t i = 0; i < piles_.size(); ++i) {
        if (piles_[i].type == PileType::FreeCell &&
            canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(i))) {
            return executeMove(fromPileIdx, startCardIdx, static_cast<uint8_t>(i));
        }
    }

    return false;
}

bool SolitaireBoard::stepAutoFoundation() {
    if (isSpider()) {
        return collectCompletedSpiderRuns();
    }
    if (isGolf()) {
        return false;
    }
    for (size_t s = 0; s < piles_.size(); ++s) {
        const Pile& src = piles_[s];
        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.cards.empty()) {
            continue;
        }
        const size_t topIdx = src.cards.size() - 1;
        const Card& lead = src.cards[topIdx];
        if (lead.rank == Rank::Ace) {
            for (size_t d = 0; d < piles_.size(); ++d) {
                if (piles_[d].type == PileType::Foundation &&
                    (piles_[d].index % 4) == static_cast<uint8_t>(lead.suit) &&
                    canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                    return executeMove(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d));
                }
            }
        }
        for (size_t d = 0; d < piles_.size(); ++d) {
            if (piles_[d].type == PileType::Foundation &&
                canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                return executeMove(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d));
            }
        }
    }
    return false;
}

bool SolitaireBoard::canAutoFinish() const noexcept {
    if (isVictory()) return false;
    if (isGolf() || isSpider()) return false;
    for (const auto& p : piles_) {
        if (p.type == PileType::Stock || p.type == PileType::Waste) {
            if (!p.cards.empty()) return false;
        } else if (p.type == PileType::Tableau) {
            for (const auto& c : p.cards) {
                if (!c.faceUp) return false;
            }
        }
    }
    return true;
}

HintMove SolitaireBoard::findBestHint() {
    clearHint();

    if (isPyramidGame()) {
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (isCardPlayableInPyramid(i) && piles_[i].cards.back().rank == Rank::King) {
                activeHint_ = {true, static_cast<uint8_t>(i), static_cast<uint8_t>(piles_[i].cards.size() - 1), 2};
                piles_[i].cards.back().enshadedGlow = true;
                return activeHint_;
            }
        }
        for (size_t a = 0; a < piles_.size(); ++a) {
            if (!isCardPlayableInPyramid(a)) continue;
            const int rankA = static_cast<int>(piles_[a].cards.back().rank);
            for (size_t b = a + 1; b < piles_.size(); ++b) {
                if (!isCardPlayableInPyramid(b)) continue;
                const int rankB = static_cast<int>(piles_[b].cards.back().rank);
                if (rankA + rankB == 13) {
                    activeHint_ = {true, static_cast<uint8_t>(a), static_cast<uint8_t>(piles_[a].cards.size() - 1), static_cast<uint8_t>(b)};
                    piles_[a].cards.back().enshadedGlow = true;
                    piles_[b].cards.back().enshadedGlow = true;
                    return activeHint_;
                }
            }
        }
        if (!piles_.empty() && !piles_[0].cards.empty()) {
            activeHint_ = {true, 0, static_cast<uint8_t>(piles_[0].cards.size() - 1), 1};
            piles_[0].cards.back().enshadedGlow = true;
            return activeHint_;
        }
        return activeHint_;
    }

    if (isGolf()) {
        uint8_t foundIdx = 1;
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type == PileType::Foundation) {
                foundIdx = static_cast<uint8_t>(i);
                break;
            }
        }
        for (size_t s = 0; s < piles_.size(); ++s) {
            const Pile& src = piles_[s];
            if (src.type != PileType::Tableau || src.cards.empty()) continue;
            const size_t topIdx = src.cards.size() - 1;
            if (canMoveCards(static_cast<uint8_t>(s), topIdx, foundIdx)) {
                activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), foundIdx};
                piles_[s].cards[topIdx].enshadedGlow = true;
                return activeHint_;
            }
        }
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type == PileType::Stock && !piles_[i].cards.empty()) {
                activeHint_ = {true, static_cast<uint8_t>(i), static_cast<uint8_t>(piles_[i].cards.size() - 1), foundIdx};
                piles_[i].cards.back().enshadedGlow = true;
                return activeHint_;
            }
        }
        return activeHint_;
    }

    for (size_t s = 0; s < piles_.size(); ++s) {
        const Pile& src = piles_[s];
        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.cards.empty()) continue;
        size_t topIdx = src.cards.size() - 1;
        const Card& lead = src.cards[topIdx];
        if (lead.rank == Rank::Ace) {
            for (size_t d = 0; d < piles_.size(); ++d) {
                if (piles_[d].type == PileType::Foundation &&
                    (piles_[d].index % 4) == static_cast<uint8_t>(lead.suit) &&
                    canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                    activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                    piles_[s].cards[topIdx].enshadedGlow = true;
                    return activeHint_;
                }
            }
        }
        for (size_t d = 0; d < piles_.size(); ++d) {
            if (piles_[d].type == PileType::Foundation &&
                canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                piles_[s].cards[topIdx].enshadedGlow = true;
                return activeHint_;
            }
        }
    }

    for (size_t s = 0; s < piles_.size(); ++s) {
        const Pile& src = piles_[s];
        if (src.type != PileType::Tableau && src.type != PileType::Waste && src.type != PileType::FreeCell) continue;
        for (size_t cIdx = 0; cIdx < src.cards.size(); ++cIdx) {
            if (!src.cards[cIdx].faceUp) continue;
            if (src.type == PileType::Tableau && cIdx == 0 && src.cards[cIdx].rank == Rank::King) continue;

            for (size_t d = 0; d < piles_.size(); ++d) {
                if (piles_[d].type == PileType::Tableau &&
                    canMoveCards(static_cast<uint8_t>(s), cIdx, static_cast<uint8_t>(d))) {
                    activeHint_ = {true, static_cast<uint8_t>(s), static_cast<uint8_t>(cIdx), static_cast<uint8_t>(d)};
                    piles_[s].cards[cIdx].enshadedGlow = true;
                    return activeHint_;
                }
            }
        }
    }

    return activeHint_;
}

void SolitaireBoard::clearHint() noexcept {
    activeHint_ = {};
    for (auto& p : piles_) {
        for (auto& c : p.cards) {
            c.enshadedGlow = false;
        }
    }
}

bool SolitaireBoard::undo() {
    if (!txUndoStack_.empty()) {
        clearHint();
        clearSelection();
        TurnTransaction tx = std::move(txUndoStack_.back());
        txUndoStack_.pop_back();
        auto ctx = makeScriptContext(nullptr);
        ScriptRuleEngine::revertTransaction(ctx, tx);
        txRedoStack_.push_back(std::move(tx));
        if (moveCount_ > 0) --moveCount_;
        updateCardTargets();
        return true;
    }

    if (undoStack_.empty()) return false;
    clearHint();
    clearSelection();

    MoveCommand cmd = undoStack_.back();
    undoStack_.pop_back();

    Pile& src = piles_[cmd.fromPile];
    Pile& dst = piles_[cmd.toPile];

    if (cmd.isPairMove && cmd.secondFromPile < piles_.size()) {
        Pile& firstSrc = piles_[cmd.fromPile];
        Pile& secondSrc = piles_[cmd.secondFromPile];
        if (dst.cards.size() >= 2) {
            Card cB = dst.cards.back();
            dst.cards.pop_back();
            cB.faceUp = true;
            cB.elevation = 6.0f;
            secondSrc.cards.push_back(cB);

            Card cA = dst.cards.back();
            dst.cards.pop_back();
            cA.faceUp = true;
            cA.elevation = 6.0f;
            firstSrc.cards.push_back(cA);
        }
        score_ = std::max(0, score_ - cmd.scoreDelta);
        redoStack_.push_back(cmd);
        updateCardTargets();
        return true;
    }

    if (cmd.spiderStockDeal) {
        std::vector<size_t> tabIndices;
        for (size_t i = 0; i < piles_.size(); ++i) {
            if (piles_[i].type == PileType::Tableau) {
                tabIndices.push_back(i);
            }
        }
        const size_t count = std::min<size_t>(cmd.cardCount, tabIndices.size());
        for (size_t i = count; i > 0; --i) {
            Pile& tab = piles_[tabIndices[i - 1]];
            if (!tab.cards.empty()) {
                Card c = tab.cards.back();
                tab.cards.pop_back();
                c.faceUp = false;
                c.elevation = 6.0f;
                piles_[0].cards.push_back(c);
            }
        }
    } else if (cmd.recycledWaste) {
        while (!dst.cards.empty()) {
            Card c = dst.cards.back();
            dst.cards.pop_back();
            c.faceUp = true;
            c.elevation = 6.0f;
            src.cards.push_back(c);
        }
    } else {
        if (cmd.flippedSourceTop && !src.cards.empty()) {
            src.cards.back().faceUp = false;
            src.cards.back().flipAngle = 0.0f;
        }
        const size_t startIdx = dst.cards.size() - cmd.cardCount;
        for (size_t i = startIdx; i < dst.cards.size(); ++i) {
            Card c = dst.cards[i];
            if (src.type == PileType::Stock) {
                c.faceUp = false;
            }
            c.elevation = 8.0f;
            src.cards.push_back(c);
        }
        dst.cards.erase(dst.cards.begin() + static_cast<std::ptrdiff_t>(startIdx), dst.cards.end());
    }

    score_ = std::max(0, score_ - cmd.scoreDelta);
    redoStack_.push_back(cmd);
    updateCardTargets();
    return true;
}

bool SolitaireBoard::redo() {
    if (!txRedoStack_.empty()) {
        clearHint();
        clearSelection();
        TurnTransaction tx = std::move(txRedoStack_.back());
        txRedoStack_.pop_back();
        auto ctx = makeScriptContext(nullptr);
        ScriptRuleEngine::applyTransaction(ctx, tx);
        txUndoStack_.push_back(std::move(tx));
        ++moveCount_;
        updateCardTargets();
        return true;
    }

    if (redoStack_.empty()) return false;
    MoveCommand cmd = redoStack_.back();
    redoStack_.pop_back();

    if (cmd.spiderStockDeal) {
        return drawFromStock();
    }
    if (cmd.fromPile == 0 && cmd.toPile == 1 && piles_[0].type == PileType::Stock) {
        return drawFromStock();
    }
    if (cmd.recycledWaste) {
        return drawFromStock();
    }
    if (cmd.isPairMove && cmd.secondFromPile < piles_.size()) {
        return executePyramidPair(cmd.fromPile, cmd.secondFromPile, cmd.toPile);
    }
    const Pile& src = piles_[cmd.fromPile];
    if (src.cards.size() >= cmd.cardCount) {
        return executeMove(cmd.fromPile, src.cards.size() - cmd.cardCount, cmd.toPile, true);
    }
    return false;
}

bool SolitaireBoard::applyTransaction(const TurnTransaction& tx) {
    clearHint();
    clearSelection();
    statusCacheDirty_ = true;
    auto ctx = makeScriptContext(nullptr);
    ScriptRuleEngine::applyTransaction(ctx, tx);
    txUndoStack_.push_back(tx);
    txRedoStack_.clear();
    ++moveCount_;
    updateCardTargets();
    return true;
}

bool SolitaireBoard::isCardBlocked(size_t pileIdx, size_t cardIdx) const noexcept {
    return ScriptRuleEngine::isCardBlocked(piles_, pileIdx, cardIdx, cardW_, cardH_);
}

bool SolitaireBoard::isPileBlocked(size_t pileIdx) const noexcept {
    if (pileIdx >= piles_.size() || piles_[pileIdx].cards.empty()) return false;
    return isCardBlocked(pileIdx, piles_[pileIdx].cards.size() - 1);
}

bool SolitaireBoard::hitTestCard(float px, float py, int& outPileIdx, int& outCardIdx) const noexcept {
    outPileIdx = -1;
    outCardIdx = -1;

    // Responsive touch halo: expand hit targets on narrow/small cards for finger tapping
    const float padX = (cardW_ < 64.0f) ? std::min(10.0f, (64.0f - cardW_) * 0.5f) : 0.0f;
    const float padY = (cardH_ < 90.0f) ? std::min(10.0f, (90.0f - cardH_) * 0.5f) : 0.0f;

    auto pointInRotatedRect = [padX, padY](float testX, float testY, float rx, float ry, float rw, float rh, float angleRad) noexcept -> bool {
        if (std::abs(angleRad) > 0.0001f) {
            const float cx = rx + rw * 0.5f;
            const float cy = ry + rh * 0.5f;
            const float dx = testX - cx;
            const float dy = testY - cy;
            const float c = std::cos(-angleRad);
            const float s = std::sin(-angleRad);
            testX = cx + dx * c - dy * s;
            testY = cy + dx * s + dy * c;
        }
        return testX >= rx - padX && testX <= rx + rw + padX && testY >= ry - padY && testY <= ry + rh + padY;
    };

    // Sort pile indices by descending (zIndex, pIdx) so higher-zIndex piles are hit-tested first
    std::vector<size_t> hitOrder(piles_.size());
    for (size_t i = 0; i < piles_.size(); ++i) hitOrder[i] = i;
    std::sort(hitOrder.begin(), hitOrder.end(), [this](size_t a, size_t b) noexcept {
        if (piles_[a].zIndex != piles_[b].zIndex) {
            return piles_[a].zIndex > piles_[b].zIndex;
        }
        return a > b;
    });

    for (size_t idx : hitOrder) {
        const int pIdx = static_cast<int>(idx);
        const Pile& p = piles_[idx];
        if (p.type == PileType::Stock) {
            if (pointInRotatedRect(px, py, p.x, p.y, cardW_, cardH_, p.angle)) {
                outPileIdx = pIdx;
                outCardIdx = p.cards.empty() ? -1 : static_cast<int>(p.cards.size()) - 1;
                return true;
            }
            continue;
        }

        for (int cIdx = static_cast<int>(p.cards.size()) - 1; cIdx >= 0; --cIdx) {
            const Card& c = p.cards[static_cast<size_t>(cIdx)];
            if (pointInRotatedRect(px, py, c.x, c.y, cardW_, cardH_, c.angle)) {
                // In Waste/Foundation/FreeCell, only the top card is interactive
                if (p.type != PileType::Tableau && cIdx != static_cast<int>(p.cards.size()) - 1) {
                    continue;
                }
                if (!c.faceUp) {
                    return false;
                }
                outPileIdx = pIdx;
                outCardIdx = cIdx;
                return true;
            }
        }
    }
    return false;
}

int SolitaireBoard::findBestDropPile(uint8_t fromPileIdx, size_t startCardIdx, float dropCenterX, float dropCenterY) const noexcept {
    int bestPile = -1;
    float bestDist = std::max(cardW_ * 1.15f, 54.0f); // Generous Hardwood 3.0 magnetic snap radius

    for (size_t d = 0; d < piles_.size(); ++d) {
        if (d == fromPileIdx) continue;
        if (!canMoveCards(fromPileIdx, startCardIdx, static_cast<uint8_t>(d))) continue;

        const Pile& dst = piles_[d];
        float targetX = dst.x + cardW_ * 0.5f;
        float targetY = dst.y + cardH_ * 0.5f;
        if (!dst.cards.empty()) {
            targetX = dst.cards.back().targetX + cardW_ * 0.5f;
            targetY = dst.cards.back().targetY + cardH_ * 0.5f;
        }

        float dist = std::hypot(dropCenterX - targetX, dropCenterY - targetY);
        if (dist < bestDist) {
            bestDist = dist;
            bestPile = static_cast<int>(d);
        }
    }
    return bestPile;
}

ScriptBoardContext SolitaireBoard::makeScriptContext(TurnTransaction* tx) noexcept {
    ScriptBoardContext ctx{};
    ctx.piles = &piles_;
    ctx.blackboard = &blackboard_;
    ctx.score = &score_;
    ctx.rngState = &rngState_;
    ctx.activeTx = tx;
    ctx.cardW = cardW_;
    ctx.cardH = cardH_;
    ctx.viewportW = viewportW_;
    ctx.viewportH = viewportH_;
    return ctx;
}

ScriptBoardContext SolitaireBoard::makeConstScriptContext() const noexcept {
    ScriptBoardContext ctx{};
    ctx.piles = const_cast<std::vector<Pile>*>(&piles_);
    ctx.blackboard = const_cast<BoardBlackboard*>(&blackboard_);
    ctx.score = const_cast<int32_t*>(&score_);
    ctx.rngState = const_cast<uint32_t*>(&rngState_);
    ctx.activeTx = nullptr;
    ctx.cardW = cardW_;
    ctx.cardH = cardH_;
    ctx.viewportW = viewportW_;
    ctx.viewportH = viewportH_;
    return ctx;
}

GameStatus SolitaireBoard::gameStatus() const {
    if (scriptEngine_.hasGetStatus()) {
        if (statusCacheDirty_) {
            auto ctx = makeConstScriptContext();
            cachedGameStatus_ = scriptEngine_.invokeGetStatus(ctx);
            statusCacheDirty_ = false;
        }
        return cachedGameStatus_;
    }
    return isVictory() ? GameStatus::Victory : GameStatus::Playing;
}

int32_t SolitaireBoard::evaluateScriptHeuristic() const {
    if (scriptEngine_.hasEvalHeuristic()) {
        auto ctx = makeConstScriptContext();
        return scriptEngine_.invokeEvalHeuristic(ctx);
    }
    int32_t h = score_;
    h += blackboard_.getInt("HP", 0) * 100;
    for (const auto& p : piles_) {
        if (p.type == PileType::Foundation) {
            h += static_cast<int32_t>(p.cards.size()) * 50;
        }
    }
    return h;
}

bool SolitaireBoard::canPickScriptCard(uint8_t pileIdx, size_t cardIdx) const {
    if (!scriptEngine_.hasCanPick()) return true;
    auto ctx = makeConstScriptContext();
    return const_cast<ScriptRuleEngine&>(scriptEngine_).invokeCanPick(ctx, pileIdx, cardIdx);
}

SolitaireBoard::ScriptedSnapshot SolitaireBoard::captureSnapshot() const {
    ScriptedSnapshot snap;
    snap.piles = piles_;
    snap.blackboard = blackboard_;
    snap.score = score_;
    snap.rngState = rngState_;
    snap.moveCount = moveCount_;
    return snap;
}

void SolitaireBoard::restoreSnapshot(const ScriptedSnapshot& snap) {
    piles_ = snap.piles;
    blackboard_ = snap.blackboard;
    score_ = snap.score;
    rngState_ = snap.rngState;
    moveCount_ = snap.moveCount;
    statusCacheDirty_ = true;
}

bool SolitaireBoard::isVictory() const noexcept {
    if (scriptEngine_.hasGetStatus()) {
        return gameStatus() == GameStatus::Victory;
    }
    if (isPyramidGame()) {
        const size_t pyrBase = 3;
        for (size_t i = 0; i < 28 && (pyrBase + i) < piles_.size(); ++i) {
            if (!piles_[pyrBase + i].cards.empty()) return false;
        }
        return true;
    }
    if (isGolf()) {
        for (const auto& p : piles_) {
            if (p.type == PileType::Tableau && !p.cards.empty()) return false;
        }
        return true;
    }
    uint32_t foundationCards = 0;
    for (const auto& p : piles_) {
        if (p.type == PileType::Foundation) {
            foundationCards += static_cast<uint32_t>(p.cards.size());
        }
    }
    const uint32_t targetCount = (activeDef_.decks > 0 ? static_cast<uint32_t>(activeDef_.decks) : (isSpiderVariant(variant_) ? 2u : 1u)) * 52u;
    return foundationCards == targetCount;
}

} // namespace solengine
