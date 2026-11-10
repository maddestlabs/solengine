#include "solengine/solitaire_solver.hpp"
#include <chrono>
#include <unordered_set>
#include <queue>
#include <memory>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <string>

namespace solengine {

std::string formatCardNotation(Suit suit, Rank rank) {
    const char* suits[] = { "♠", "♡", "♢", "♣" };
    const char* ranks[] = { "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
    uint8_t sIdx = static_cast<uint8_t>(suit) % 4;
    uint8_t rIdx = static_cast<uint8_t>(rank);
    if (rIdx < 1 || rIdx > 13) rIdx = 1;
    return std::string(suits[sIdx]) + ranks[rIdx];
}

std::string buildMoveNotation(const SolitaireBoard& board, const SolverMove& m) {
    const auto& piles = board.piles();
    if (m.action == SolverMove::Action::DrawStock) {
        if (!piles.empty() && piles[0].cards.empty() && piles.size() > 1 && !piles[1].cards.empty()) {
            return "Recycle Waste to Stock";
        }
        return "Draw from Stock";
    }

    if (m.action == SolverMove::Action::PairPyramid) {
        if (m.fromPile < piles.size() && !piles[m.fromPile].cards.empty()) {
            const auto& cA = piles[m.fromPile].cards.back();
            if (cA.rank == Rank::King) {
                return "Discard " + formatCardNotation(cA.suit, cA.rank);
            }
            if (m.secondFromPile < piles.size() && !piles[m.secondFromPile].cards.empty()) {
                const auto& cB = piles[m.secondFromPile].cards.back();
                return "Pair " + formatCardNotation(cA.suit, cA.rank) + " + " + formatCardNotation(cB.suit, cB.rank);
            }
        }
        return "Pair Pyramid Cards";
    }

    if (m.fromPile < piles.size() && m.toPile < piles.size()) {
        const auto& src = piles[m.fromPile];
        const auto& dst = piles[m.toPile];
        if (m.cardIndex < src.cards.size()) {
            const auto& lead = src.cards[m.cardIndex];
            std::string cardStr = formatCardNotation(lead.suit, lead.rank);
            if (src.cards.size() - m.cardIndex > 1) {
                cardStr += " (" + std::to_string(src.cards.size() - m.cardIndex) + " cards)";
            }

            if (dst.type == PileType::Foundation) {
                return cardStr + " -> Foundation";
            }
            if (dst.type == PileType::FreeCell) {
                return cardStr + " -> FreeCell";
            }
            if (dst.type == PileType::Tableau) {
                if (dst.cards.empty()) {
                    return cardStr + " -> Empty Column " + std::to_string(dst.index + 1);
                } else {
                    const auto& target = dst.cards.back();
                    return cardStr + " -> " + formatCardNotation(target.suit, target.rank);
                }
            }
        }
    }
    return "Move Card";
}

namespace {

// ============================================================================
// Compact Bitboard & Value-Type Game Model (Zero Heap Allocation)
// ============================================================================

struct CompactCard {
    uint8_t raw{0}; // bits 0..3: rank (1..13), bits 4..5: suit (0..3), bit 7: faceUp (0x80)

    constexpr CompactCard() = default;
    constexpr CompactCard(uint8_t r) : raw(r) {}
    constexpr CompactCard(Suit s, Rank r, bool faceUp)
        : raw((static_cast<uint8_t>(r) & 0x0F) |
              ((static_cast<uint8_t>(s) & 0x03) << 4) |
              (faceUp ? 0x80 : 0x00)) {}

    [[nodiscard]] constexpr Rank rank() const noexcept { return static_cast<Rank>(raw & 0x0F); }
    [[nodiscard]] constexpr Suit suit() const noexcept { return static_cast<Suit>((raw >> 4) & 0x03); }
    [[nodiscard]] constexpr bool faceUp() const noexcept { return (raw & 0x80) != 0; }
    constexpr void setFaceUp(bool up) noexcept {
        if (up) raw |= 0x80;
        else raw &= ~0x80;
    }
};

static constexpr size_t kMaxCardsPerPile = 52;
static constexpr size_t kMaxPiles = 32;

struct CompactPile {
    PileType type{PileType::Tableau};
    uint8_t count{0};
    CompactCard cards[kMaxCardsPerPile];

    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] size_t size() const noexcept { return count; }
    [[nodiscard]] const CompactCard& back() const noexcept { return cards[count - 1]; }
    [[nodiscard]] CompactCard& back() noexcept { return cards[count - 1]; }
    void push_back(CompactCard c) noexcept {
        if (count < kMaxCardsPerPile) {
            cards[count++] = c;
        }
    }
    void pop_back() noexcept { if (count > 0) --count; }
};

struct CompactBoard {
    uint8_t numPiles{0};
    CompactPile piles[kMaxPiles];
};

struct CompactMove {
    SolverMove::Action action{SolverMove::Action::MoveCards};
    uint8_t fromPile{0};
    uint8_t cardIndex{0};
    uint8_t toPile{0};
    uint8_t secondFromPile{255};
};

struct GameRules {
    GameVariant variant{GameVariant::KlondikeDraw1};
    uint8_t decks{1};
    uint32_t targetFoundationCards{52};
    bool isSpider{false};
    bool isGolf{false};
    bool isPyramid{false};
    bool isYukon{false};
    bool isRussian{false};
    bool isFortyThieves{false};
    bool isBakersGame{false};
    bool isFreeCell{false};
};

CompactBoard fromSolitaireBoard(const SolitaireBoard& board) {
    CompactBoard cb{};
    const auto& piles = board.piles();
    cb.numPiles = static_cast<uint8_t>(std::min(piles.size(), kMaxPiles));
    for (size_t p = 0; p < cb.numPiles; ++p) {
        cb.piles[p].type = piles[p].type;
        cb.piles[p].count = 0;
        for (const auto& c : piles[p].cards) {
            cb.piles[p].push_back(CompactCard(c.suit, c.rank, c.faceUp));
        }
    }
    return cb;
}

GameRules extractGameRules(const SolitaireBoard& board) {
    GameRules rules{};
    rules.variant = board.variant();
    rules.decks = board.activeGameDef().decks > 0 ? board.activeGameDef().decks : (board.isSpider() ? 2 : 1);
    rules.targetFoundationCards = static_cast<uint32_t>(rules.decks) * 52u;
    rules.isSpider = board.isSpider();
    rules.isGolf = board.isGolf();
    rules.isPyramid = board.isPyramidGame();
    rules.isYukon = (board.activeGameDef().family == "Yukon");
    rules.isRussian = (board.activeGameDef().id == "russian_solitaire" || board.activeGameDef().name.find("Russian") != std::string::npos);
    rules.isFortyThieves = (board.activeGameDef().family == "FortyThieves");
    rules.isBakersGame = (board.activeGameDef().id == "bakers_game" || board.activeGameDef().name.find("Baker") != std::string::npos);
    rules.isFreeCell = (rules.variant == GameVariant::FreeCell || (board.activeGameDef().freecells == 4 && board.activeGameDef().tableaus == 8));
    return rules;
}

// ============================================================================
// Fast Stateless Hashing & State Evaluation
// ============================================================================

uint64_t computeBoardHash(const CompactBoard& board) {
    uint64_t h = 14695981039346656037ULL;
    auto hashByte = [&h](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };

    uint8_t freeCellCards[8];
    uint8_t fcCount = 0;

    for (size_t pIdx = 0; pIdx < board.numPiles; ++pIdx) {
        const auto& p = board.piles[pIdx];

        if (p.type == PileType::FreeCell) {
            if (p.empty()) {
                if (fcCount < 8) freeCellCards[fcCount++] = 0;
            } else {
                const auto& c = p.back();
                uint8_t val = (static_cast<uint8_t>(c.rank()) & 0x0F) |
                              ((static_cast<uint8_t>(c.suit()) & 0x03) << 4);
                if (fcCount < 8) freeCellCards[fcCount++] = val;
            }
            continue;
        }

        hashByte(static_cast<uint8_t>(p.type));
        hashByte(p.count);

        if (p.type == PileType::Foundation) {
            if (!p.empty()) {
                const auto& c = p.back();
                hashByte(static_cast<uint8_t>(c.suit()));
                hashByte(static_cast<uint8_t>(c.rank()));
            }
            continue;
        }

        for (size_t i = 0; i < p.count; ++i) {
            hashByte(p.cards[i].raw);
        }
    }

    if (fcCount > 0) {
        std::sort(freeCellCards, freeCellCards + fcCount);
        for (uint8_t i = 0; i < fcCount; ++i) {
            hashByte(freeCellCards[i]);
        }
    }

    return h;
}

bool isVictory(const CompactBoard& b, const GameRules& rules) noexcept {
    if (rules.isPyramid) {
        const size_t pyrBase = 3;
        for (size_t i = 0; i < 28 && (pyrBase + i) < b.numPiles; ++i) {
            if (!b.piles[pyrBase + i].empty()) return false;
        }
        return true;
    }
    if (rules.isGolf) {
        for (size_t i = 0; i < b.numPiles; ++i) {
            if (b.piles[i].type == PileType::Tableau && !b.piles[i].empty()) return false;
        }
        return true;
    }
    uint32_t foundationCards = 0;
    for (size_t i = 0; i < b.numPiles; ++i) {
        if (b.piles[i].type == PileType::Foundation) {
            foundationCards += static_cast<uint32_t>(b.piles[i].count);
        }
    }
    return foundationCards == rules.targetFoundationCards;
}

int evaluateHeuristic(const CompactBoard& board, const GameRules& rules) {
    if (isVictory(board, rules)) {
        return 1000000;
    }

    if (rules.isPyramid) {
        int pyrRemaining = 0;
        for (size_t p = 0; p < board.numPiles; ++p) {
            if (board.piles[p].type == PileType::Tableau) {
                pyrRemaining += static_cast<int>(board.piles[p].count);
            }
        }
        return (28 - pyrRemaining) * 200;
    }

    if (rules.isGolf) {
        int tabRemaining = 0;
        for (size_t p = 0; p < board.numPiles; ++p) {
            if (board.piles[p].type == PileType::Tableau) {
                tabRemaining += static_cast<int>(board.piles[p].count);
            }
        }
        return (52 - tabRemaining) * 200;
    }

    if (rules.isSpider) {
        int completedFoundations = 0;
        int inSuitSequences = 0;
        int faceUpCount = 0;
        int emptyCols = 0;

        for (size_t p = 0; p < board.numPiles; ++p) {
            const auto& pile = board.piles[p];
            if (pile.type == PileType::Foundation) {
                completedFoundations += static_cast<int>(pile.count / 13);
            } else if (pile.type == PileType::Tableau) {
                if (pile.empty()) {
                    ++emptyCols;
                } else {
                    for (size_t i = 0; i < pile.count; ++i) {
                        if (pile.cards[i].faceUp()) {
                            ++faceUpCount;
                            if (i + 1 < pile.count && pile.cards[i + 1].faceUp()) {
                                if (static_cast<int>(pile.cards[i].rank()) == static_cast<int>(pile.cards[i + 1].rank()) + 1) {
                                    if (pile.cards[i].suit() == pile.cards[i + 1].suit()) {
                                        inSuitSequences += 4;
                                    } else {
                                        inSuitSequences += 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return completedFoundations * 3000 + inSuitSequences * 60 + faceUpCount * 30 + emptyCols * 80;
    }

    // Classic Foundation Games (Klondike, FreeCell, Yukon, Forty Thieves, Baker's, etc.)
    int foundationCards = 0;
    int faceUpCards = 0;
    int emptyColumns = 0;
    int emptyFreeCells = 0;
    int tableauSequences = 0;

    for (size_t p = 0; p < board.numPiles; ++p) {
        const auto& pile = board.piles[p];
        if (pile.type == PileType::Foundation) {
            foundationCards += static_cast<int>(pile.count);
        } else if (pile.type == PileType::FreeCell) {
            if (pile.empty()) ++emptyFreeCells;
        } else if (pile.type == PileType::Tableau) {
            if (pile.empty()) {
                ++emptyColumns;
            } else {
                for (size_t i = 0; i < pile.count; ++i) {
                    if (pile.cards[i].faceUp()) {
                        ++faceUpCards;
                        if (i + 1 < pile.count && pile.cards[i + 1].faceUp()) {
                            const auto& cA = pile.cards[i];
                            const auto& cB = pile.cards[i + 1];
                            if (static_cast<int>(cA.rank()) == static_cast<int>(cB.rank()) + 1) {
                                if (isRedSuit(cA.suit()) != isRedSuit(cB.suit())) {
                                    tableauSequences += 2;
                                    if (cA.suit() == cB.suit()) tableauSequences += 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return (foundationCards * 400) + (emptyColumns * 120) + (emptyFreeCells * 80) + (tableauSequences * 30) + (faceUpCards * 20);
}

// ============================================================================
// Pyramid & Spider Specific Rules
// ============================================================================

bool isCardExposedInPyramid(const CompactBoard& b, size_t pileIdx) noexcept {
    if (pileIdx < 3 || pileIdx > 30) return false;
    if (b.piles[pileIdx].empty()) return false;
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
    if (child1 >= b.numPiles || child2 >= b.numPiles) return true;

    return b.piles[child1].empty() && b.piles[child2].empty();
}

bool isCardPlayableInPyramid(const CompactBoard& b, size_t pileIdx) noexcept {
    if (pileIdx >= b.numPiles) return false;
    if (b.piles[pileIdx].empty()) return false;
    if (pileIdx == 1) return true; // Top card of Waste
    if (pileIdx >= 3 && pileIdx <= 30) return isCardExposedInPyramid(b, pileIdx);
    if (pileIdx >= 31 && pileIdx <= 38) return true; // Top card of Giza reserve
    return false;
}

bool collectCompletedSpiderRuns(CompactBoard& b) {
    bool collectedAny = false;
    for (size_t tIdx = 0; tIdx < b.numPiles; ++tIdx) {
        if (b.piles[tIdx].type != PileType::Tableau) continue;
        CompactPile& tab = b.piles[tIdx];
        if (tab.count < 13) continue;
        const size_t n = tab.count;
        if (!tab.back().faceUp() || tab.back().rank() != Rank::Ace) continue;
        const Suit leadSuit = tab.back().suit();

        bool isRun = true;
        for (size_t i = 0; i < 13; ++i) {
            const CompactCard& c = tab.cards[n - 1 - i];
            if (!c.faceUp() || c.suit() != leadSuit || static_cast<int>(c.rank()) != static_cast<int>(i + 1)) {
                isRun = false;
                break;
            }
        }
        if (!isRun) continue;

        int foundIdx = -1;
        for (size_t f = 0; f < b.numPiles; ++f) {
            if (b.piles[f].type == PileType::Foundation && b.piles[f].empty()) {
                foundIdx = static_cast<int>(f);
                break;
            }
        }
        if (foundIdx < 0) continue;

        CompactPile& found = b.piles[static_cast<size_t>(foundIdx)];
        for (size_t i = n - 13; i < n; ++i) {
            found.push_back(tab.cards[i]);
        }
        tab.count -= 13;

        if (!tab.empty() && !tab.back().faceUp()) {
            tab.back().setFaceUp(true);
        }
        collectedAny = true;
    }
    return collectedAny;
}

// ============================================================================
// Core Rule Verification & Move Execution on CompactBoard
// ============================================================================

bool canMoveCards(const CompactBoard& b, uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx, const GameRules& rules) {
    if (fromPileIdx >= b.numPiles || toPileIdx >= b.numPiles || fromPileIdx == toPileIdx) {
        return false;
    }
    const CompactPile& src = b.piles[fromPileIdx];
    const CompactPile& dst = b.piles[toPileIdx];
    if (startCardIdx >= src.count) {
        return false;
    }

    const CompactCard& lead = src.cards[startCardIdx];
    if (!lead.faceUp()) {
        return false;
    }
    const size_t moveCount = src.count - startCardIdx;

    if (rules.isPyramid) {
        if (toPileIdx == 2) {
            if (moveCount != 1) return false;
            if (!isCardPlayableInPyramid(b, fromPileIdx)) return false;
            return lead.rank() == Rank::King;
        }
        if (!isCardPlayableInPyramid(b, fromPileIdx) || !isCardPlayableInPyramid(b, toPileIdx)) return false;
        if (dst.empty()) return false;
        const CompactCard& top = dst.back();
        return (static_cast<int>(lead.rank()) + static_cast<int>(top.rank()) == 13);
    }

    if (moveCount > 1 && src.type != PileType::Tableau) {
        return false;
    }

    if (rules.isGolf) {
        if (src.type != PileType::Tableau || dst.type != PileType::Foundation || moveCount != 1) {
            return false;
        }
        if (startCardIdx != src.count - 1) {
            return false;
        }
        if (dst.empty()) {
            return true;
        }
        const CompactCard& top = dst.back();
        if (top.rank() == Rank::King) {
            return false;
        }
        const int movingRank = static_cast<int>(lead.rank());
        const int topRank = static_cast<int>(top.rank());
        return (movingRank == topRank + 1) || (movingRank == topRank - 1);
    }

    if (rules.isYukon) {
        if (dst.type == PileType::Foundation) {
            if (moveCount != 1) return false;
            if (dst.empty()) return lead.rank() == Rank::Ace;
            const CompactCard& top = dst.back();
            return (lead.suit() == top.suit()) && (static_cast<int>(lead.rank()) == static_cast<int>(top.rank()) + 1);
        }
        if (dst.type == PileType::Tableau) {
            if (dst.empty()) return lead.rank() == Rank::King;
            const CompactCard& top = dst.back();
            if (!top.faceUp()) return false;
            if (rules.isRussian) {
                return (lead.suit() == top.suit()) && (static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1);
            }
            return (isRedSuit(lead.suit()) != isRedSuit(top.suit())) && (static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1);
        }
        return false;
    }

    if (rules.isFortyThieves) {
        if (moveCount != 1) return false;
        if (dst.type == PileType::Foundation) {
            if (dst.empty()) return lead.rank() == Rank::Ace;
            const CompactCard& top = dst.back();
            return (lead.suit() == top.suit()) && (static_cast<int>(lead.rank()) == static_cast<int>(top.rank()) + 1);
        }
        if (dst.type == PileType::Tableau) {
            if (dst.empty()) return true;
            const CompactCard& top = dst.back();
            if (!top.faceUp()) return false;
            return (lead.suit() == top.suit()) && (static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1);
        }
        return false;
    }

    if (rules.isSpider) {
        if (moveCount > 1) {
            for (size_t i = startCardIdx; i + 1 < src.count; ++i) {
                const CompactCard& upper = src.cards[i];
                const CompactCard& lower = src.cards[i + 1];
                if (upper.suit() != lower.suit() || static_cast<int>(upper.rank()) != static_cast<int>(lower.rank()) + 1) {
                    return false;
                }
            }
        }
        if (dst.type != PileType::Tableau) {
            return false;
        }
        if (dst.empty()) {
            return true;
        }
        const CompactCard& top = dst.back();
        if (!top.faceUp()) return false;
        return static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1;
    }

    if (moveCount > 1) {
        for (size_t i = startCardIdx; i + 1 < src.count; ++i) {
            const CompactCard& upper = src.cards[i];
            const CompactCard& lower = src.cards[i + 1];
            if (rules.isBakersGame) {
                if (upper.suit() != lower.suit() || static_cast<int>(upper.rank()) != static_cast<int>(lower.rank()) + 1) {
                    return false;
                }
            } else {
                if (isRedSuit(upper.suit()) == isRedSuit(lower.suit()) ||
                    static_cast<int>(upper.rank()) != static_cast<int>(lower.rank()) + 1) {
                    return false;
                }
            }
        }
    }

    if (dst.type == PileType::Foundation) {
        if (moveCount != 1) return false;
        if (dst.empty()) {
            return lead.rank() == Rank::Ace;
        }
        const CompactCard& top = dst.back();
        return (lead.suit() == top.suit()) &&
               (static_cast<int>(lead.rank()) == static_cast<int>(top.rank()) + 1);
    }

    if (dst.type == PileType::FreeCell) {
        return moveCount == 1 && dst.empty();
    }

    if (dst.type == PileType::Tableau) {
        if (dst.empty()) {
            if (rules.isFreeCell) {
                return true;
            }
            return lead.rank() == Rank::King;
        }
        const CompactCard& top = dst.back();
        if (!top.faceUp()) return false;
        if (rules.isBakersGame) {
            return (lead.suit() == top.suit()) && (static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1);
        }
        return (isRedSuit(lead.suit()) != isRedSuit(top.suit())) &&
               (static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1);
    }

    return false;
}

bool executeMove(CompactBoard& b, uint8_t fromPileIdx, size_t startCardIdx, uint8_t toPileIdx, const GameRules& rules) {
    if (!canMoveCards(b, fromPileIdx, startCardIdx, toPileIdx, rules)) {
        return false;
    }

    if (rules.isPyramid) {
        if (toPileIdx == 2) {
            CompactPile& src = b.piles[fromPileIdx];
            CompactPile& dst = b.piles[2];
            CompactCard c = src.back();
            src.pop_back();
            dst.push_back(c);
            return true;
        }
        return false;
    }

    CompactPile& src = b.piles[fromPileIdx];
    CompactPile& dst = b.piles[toPileIdx];

    for (size_t i = startCardIdx; i < src.count; ++i) {
        dst.push_back(src.cards[i]);
    }
    src.count = static_cast<uint8_t>(startCardIdx);

    if (src.type == PileType::Tableau && !src.empty() && !src.back().faceUp()) {
        src.back().setFaceUp(true);
    }

    if (rules.isSpider) {
        collectCompletedSpiderRuns(b);
    }

    return true;
}

bool drawFromStock(CompactBoard& b, const GameRules& rules) {
    if (b.numPiles == 0 || b.piles[0].type != PileType::Stock) {
        return false;
    }
    CompactPile& stock = b.piles[0];

    if (rules.isSpider) {
        if (stock.empty()) return false;
        std::vector<size_t> tabIndices;
        for (size_t i = 0; i < b.numPiles; ++i) {
            if (b.piles[i].type == PileType::Tableau) {
                if (b.piles[i].empty()) {
                    return false;
                }
                tabIndices.push_back(i);
            }
        }
        const size_t dealCount = std::min(tabIndices.size(), static_cast<size_t>(stock.count));
        for (size_t col = 0; col < dealCount; ++col) {
            if (stock.empty()) break;
            CompactCard c = stock.back();
            stock.pop_back();
            c.setFaceUp(true);
            b.piles[tabIndices[col]].push_back(c);
        }
        collectCompletedSpiderRuns(b);
        return true;
    }

    if (rules.isGolf) {
        if (stock.empty()) return false;
        CompactCard c = stock.back();
        stock.pop_back();
        c.setFaceUp(true);
        if (b.numPiles > 1) {
            b.piles[1].push_back(c);
        }
        return true;
    }

    if (b.numPiles < 2) {
        return false;
    }
    CompactPile& waste = b.piles[1];

    if (!stock.empty()) {
        const uint8_t drawCount = (rules.variant == GameVariant::KlondikeDraw3)
            ? static_cast<uint8_t>(std::min<size_t>(3, stock.count))
            : 1u;
        for (uint8_t i = 0; i < drawCount; ++i) {
            CompactCard c = stock.back();
            stock.pop_back();
            c.setFaceUp(true);
            waste.push_back(c);
        }
        return true;
    } else if (!waste.empty()) {
        while (!waste.empty()) {
            CompactCard c = waste.back();
            waste.pop_back();
            c.setFaceUp(false);
            stock.push_back(c);
        }
        return true;
    }
    return false;
}

bool executePyramidPair(CompactBoard& b, uint8_t pileA, uint8_t pileB, uint8_t foundationPile) {
    if (pileA >= b.numPiles || pileB >= b.numPiles || foundationPile >= b.numPiles) return false;
    CompactPile& srcA = b.piles[pileA];
    CompactPile& srcB = b.piles[pileB];
    CompactPile& dst = b.piles[foundationPile];
    if (srcA.empty() || srcB.empty()) return false;

    CompactCard cA = srcA.back();
    srcA.pop_back();
    dst.push_back(cA);

    CompactCard cB = srcB.back();
    srcB.pop_back();
    dst.push_back(cB);

    return true;
}

// ============================================================================
// Flat Array Search Arena Node
// ============================================================================

struct SearchNode {
    uint32_t id{0};
    uint32_t parentId{0};
    CompactMove move{};
    int score{0};
    uint16_t depth{0};
    uint8_t stockRecycles{0};
    CompactBoard board{};
};

struct NodeComparator {
    const std::vector<SearchNode>* allNodes{nullptr};
    bool operator()(uint32_t a, uint32_t b) const noexcept {
        const auto& na = (*allNodes)[a];
        const auto& nb = (*allNodes)[b];
        if (na.score != nb.score) {
            return na.score < nb.score; // Max-heap: highest heuristic score first
        }
        return na.depth > nb.depth; // Tie breaker: prefer shallower paths
    }
};

} // namespace

// ============================================================================
// SolitaireSolver Public Implementation
// ============================================================================

SolveResult SolitaireSolver::solve(
    const SolitaireBoard& initialBoard,
    uint32_t maxStates,
    float timeoutMs,
    std::atomic<bool>* cancelSignal
) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result{};
    result.solved = false;

    if (initialBoard.isVictory()) {
        result.solved = true;
        return result;
    }

    const GameRules rules = extractGameRules(initialBoard);
    CompactBoard rootBoard = fromSolitaireBoard(initialBoard);

    if (isVictory(rootBoard, rules)) {
        result.solved = true;
        return result;
    }

    std::vector<SearchNode> allNodes;
    allNodes.reserve(std::min<size_t>(maxStates + 1024, 65536u));

    std::priority_queue<uint32_t, std::vector<uint32_t>, NodeComparator> openQueue(NodeComparator{&allNodes});

    std::unordered_set<uint64_t> visited;
    visited.reserve(std::min<size_t>(maxStates * 2, 65536u));

    SearchNode root;
    root.id = 0;
    root.parentId = 0;
    root.depth = 0;
    root.stockRecycles = 0;
    root.board = rootBoard;
    root.score = evaluateHeuristic(root.board, rules);

    uint64_t rootHash = computeBoardHash(root.board);
    visited.insert(rootHash);
    allNodes.push_back(root);
    openQueue.push(0);

    uint32_t statesCount = 0;
    const uint32_t maxDepth = 220;

    std::vector<CompactMove> candidates;
    candidates.reserve(32);

    while (!openQueue.empty()) {
        uint32_t currentId = openQueue.top();
        openQueue.pop();
        ++statesCount;

        // Check cancellation & timeout every 32 states for instant responsiveness
        if ((statesCount & 31u) == 0) {
            if (cancelSignal && cancelSignal->load(std::memory_order_relaxed)) {
                result.cancelled = true;
                break;
            }
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float, std::milli>(now - startTime).count();
            if (elapsed >= timeoutMs || statesCount >= maxStates) {
                result.timedOut = true;
                break;
            }
        }

        const CompactBoard currentBoard = allNodes[currentId].board;
        const uint16_t currentDepth = allNodes[currentId].depth;
        const uint8_t currentStockRecycles = allNodes[currentId].stockRecycles;

        if (isVictory(currentBoard, rules)) {
            // Reconstruct path from current node back to root
            std::vector<CompactMove> compactPath;
            uint32_t curr = currentId;
            while (curr != 0) {
                compactPath.push_back(allNodes[curr].move);
                curr = allNodes[curr].parentId;
            }
            std::reverse(compactPath.begin(), compactPath.end());

            // Build human-readable notations by replaying the winning moves on initialBoard
            SolitaireBoard replayBoard = initialBoard;
            std::vector<SolverMove> path;
            path.reserve(compactPath.size());
            for (const auto& cm : compactPath) {
                SolverMove sm;
                sm.action = cm.action;
                sm.fromPile = cm.fromPile;
                sm.cardIndex = cm.cardIndex;
                sm.toPile = cm.toPile;
                sm.secondFromPile = cm.secondFromPile;
                sm.notation = buildMoveNotation(replayBoard, sm);
                path.push_back(sm);

                if (sm.action == SolverMove::Action::DrawStock) {
                    replayBoard.drawFromStock();
                } else if (sm.action == SolverMove::Action::PairPyramid) {
                    replayBoard.executePyramidPair(sm.fromPile, sm.secondFromPile, sm.toPile);
                } else {
                    replayBoard.executeMove(sm.fromPile, sm.cardIndex, sm.toPile, true);
                }
            }

            result.solved = true;
            result.moves = std::move(path);
            break;
        }

        if (currentDepth >= maxDepth) {
            continue;
        }

        candidates.clear();

        if (rules.isPyramid) {
            // Pyramid Kings
            for (size_t i = 0; i < currentBoard.numPiles; ++i) {
                if (!isCardPlayableInPyramid(currentBoard, i) || currentBoard.piles[i].empty()) continue;
                const auto& c = currentBoard.piles[i].back();
                if (c.rank() == Rank::King) {
                    CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(i), static_cast<uint8_t>(currentBoard.piles[i].count - 1), 2};
                    candidates.push_back(cm);
                }
            }

            // Pyramid Pairs totaling 13
            for (size_t a = 0; a < currentBoard.numPiles; ++a) {
                if (!isCardPlayableInPyramid(currentBoard, a) || currentBoard.piles[a].empty()) continue;
                int rankA = static_cast<int>(currentBoard.piles[a].back().rank());
                for (size_t b = a + 1; b < currentBoard.numPiles; ++b) {
                    if (!isCardPlayableInPyramid(currentBoard, b) || currentBoard.piles[b].empty()) continue;
                    int rankB = static_cast<int>(currentBoard.piles[b].back().rank());
                    if (rankA + rankB == 13) {
                        CompactMove cm{SolverMove::Action::PairPyramid, static_cast<uint8_t>(a), static_cast<uint8_t>(currentBoard.piles[a].count - 1), 2, static_cast<uint8_t>(b)};
                        candidates.push_back(cm);
                    }
                }
            }

            // Draw Stock
            if (currentBoard.numPiles > 0 && (!currentBoard.piles[0].empty() || (currentBoard.numPiles > 1 && !currentBoard.piles[1].empty() && currentStockRecycles < 3))) {
                CompactMove cm{SolverMove::Action::DrawStock, 0, 0, 1};
                candidates.push_back(cm);
            }
        } else if (rules.isGolf) {
            uint8_t foundIdx = 1;
            for (size_t i = 0; i < currentBoard.numPiles; ++i) {
                if (currentBoard.piles[i].type == PileType::Foundation) {
                    foundIdx = static_cast<uint8_t>(i);
                    break;
                }
            }
            for (size_t s = 0; s < currentBoard.numPiles; ++s) {
                if (currentBoard.piles[s].type != PileType::Tableau || currentBoard.piles[s].empty()) continue;
                size_t topIdx = currentBoard.piles[s].count - 1;
                if (canMoveCards(currentBoard, static_cast<uint8_t>(s), topIdx, foundIdx, rules)) {
                    CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), foundIdx};
                    candidates.push_back(cm);
                }
            }
            if (currentBoard.numPiles > 0 && currentBoard.piles[0].type == PileType::Stock && !currentBoard.piles[0].empty()) {
                CompactMove cm{SolverMove::Action::DrawStock, 0, 0, foundIdx};
                candidates.push_back(cm);
            }
        } else {
            bool foundDominant = false;

            // 1. Dominant Safe Moves (Ace or 2 to Foundation - not in Spider)
            if (!rules.isSpider) {
                for (size_t s = 0; s < currentBoard.numPiles; ++s) {
                    const auto& src = currentBoard.piles[s];
                    if (src.type == PileType::Foundation || src.type == PileType::Stock || src.empty()) continue;
                    size_t topIdx = src.count - 1;
                    const auto& card = src.cards[topIdx];
                    if (!card.faceUp()) continue;

                    if (card.rank() == Rank::Ace || card.rank() == Rank::Two) {
                        for (size_t d = 0; d < currentBoard.numPiles; ++d) {
                            if (currentBoard.piles[d].type == PileType::Foundation && canMoveCards(currentBoard, static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d), rules)) {
                                CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                                candidates.clear();
                                candidates.push_back(cm);
                                foundDominant = true;
                                break;
                            }
                        }
                    }
                    if (foundDominant) break;
                }
            }

            if (!foundDominant) {
                // 2. Foundation Moves
                if (!rules.isSpider) {
                    for (size_t s = 0; s < currentBoard.numPiles; ++s) {
                        const auto& src = currentBoard.piles[s];
                        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.empty()) continue;
                        size_t topIdx = src.count - 1;
                        if (!src.cards[topIdx].faceUp()) continue;

                        for (size_t d = 0; d < currentBoard.numPiles; ++d) {
                            if (currentBoard.piles[d].type == PileType::Foundation && canMoveCards(currentBoard, static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d), rules)) {
                                CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                                candidates.push_back(cm);
                                break;
                            }
                        }
                    }
                }

                // 3. Tableau-to-Tableau, Tableau-to-FreeCell, and FreeCell-to-Tableau Moves
                for (size_t s = 0; s < currentBoard.numPiles; ++s) {
                    const auto& src = currentBoard.piles[s];
                    if (src.type != PileType::Tableau && src.type != PileType::Waste && src.type != PileType::FreeCell) continue;
                    if (src.empty()) continue;

                    bool pushedFreeCellForThisPile = false;

                    for (size_t cIdx = 0; cIdx < src.count; ++cIdx) {
                        if (!src.cards[cIdx].faceUp()) continue;

                        if ((src.type == PileType::Waste || src.type == PileType::FreeCell) && cIdx + 1 != src.count) {
                            continue;
                        }

                        bool pushedEmptyTableau = false;

                        for (size_t d = 0; d < currentBoard.numPiles; ++d) {
                            if (d == s) continue;
                            const auto& dst = currentBoard.piles[d];

                            if (src.type == PileType::FreeCell && dst.type == PileType::FreeCell) {
                                continue;
                            }

                            if (src.type == PileType::Tableau && cIdx == 0 && dst.type == PileType::Tableau && dst.empty()) {
                                continue;
                            }

                            if (dst.type == PileType::Tableau && dst.empty()) {
                                if (pushedEmptyTableau) continue;
                            }

                            if (src.type == PileType::Tableau && dst.type == PileType::Tableau && cIdx > 0 && !dst.empty()) {
                                const auto& below = src.cards[cIdx - 1];
                                const auto& target = dst.back();
                                if (below.faceUp() && below.rank() == target.rank()) {
                                    if (below.suit() == target.suit()) {
                                        continue;
                                    }
                                    if (!rules.isSpider && isRedSuit(below.suit()) == isRedSuit(target.suit())) {
                                        continue;
                                    }
                                }
                            }

                            if (dst.type == PileType::Tableau) {
                                if (canMoveCards(currentBoard, static_cast<uint8_t>(s), cIdx, static_cast<uint8_t>(d), rules)) {
                                    CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(cIdx), static_cast<uint8_t>(d)};
                                    candidates.push_back(cm);
                                    if (dst.empty()) pushedEmptyTableau = true;
                                }
                            } else if (dst.type == PileType::FreeCell) {
                                if (cIdx + 1 == src.count && dst.empty() && !pushedFreeCellForThisPile) {
                                    if (canMoveCards(currentBoard, static_cast<uint8_t>(s), cIdx, static_cast<uint8_t>(d), rules)) {
                                        CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(cIdx), static_cast<uint8_t>(d)};
                                        candidates.push_back(cm);
                                        pushedFreeCellForThisPile = true;
                                    }
                                }
                            }
                        }
                    }
                }

                // 4. Stock Draw Move
                if (currentBoard.numPiles > 0 && currentBoard.piles[0].type == PileType::Stock) {
                    const auto& stock = currentBoard.piles[0];
                    bool canDraw = !stock.empty();
                    if (canDraw && rules.isSpider) {
                        for (size_t p = 0; p < currentBoard.numPiles; ++p) {
                            if (currentBoard.piles[p].type == PileType::Tableau && currentBoard.piles[p].empty()) {
                                canDraw = false;
                                break;
                            }
                        }
                    }
                    if (!canDraw && currentBoard.numPiles > 1 && currentBoard.piles[1].type == PileType::Waste && !currentBoard.piles[1].empty() && currentStockRecycles < 3) {
                        canDraw = true;
                    }
                    if (canDraw) {
                        CompactMove cm{SolverMove::Action::DrawStock, 0, 0, 1};
                        candidates.push_back(cm);
                    }
                }
            }
        }

        // Apply candidate moves and push valid, unvisited children
        for (const auto& sm : candidates) {
            CompactBoard nextBoard = currentBoard;
            bool ok = false;
            uint8_t nextRecycle = currentStockRecycles;

            if (sm.action == SolverMove::Action::DrawStock) {
                if (nextBoard.piles[0].empty()) {
                    ++nextRecycle;
                }
                ok = drawFromStock(nextBoard, rules);
            } else if (sm.action == SolverMove::Action::PairPyramid) {
                ok = executePyramidPair(nextBoard, sm.fromPile, sm.secondFromPile, sm.toPile);
            } else {
                ok = executeMove(nextBoard, sm.fromPile, sm.cardIndex, sm.toPile, rules);
            }

            if (!ok) continue;

            uint64_t nextHash = computeBoardHash(nextBoard);
            if (!visited.insert(nextHash).second) {
                continue; // Already visited this state topology
            }

            uint32_t nextId = static_cast<uint32_t>(allNodes.size());
            SearchNode child;
            child.id = nextId;
            child.parentId = currentId;
            child.move = sm;
            child.depth = currentDepth + 1;
            child.stockRecycles = nextRecycle;
            child.board = nextBoard;
            child.score = evaluateHeuristic(child.board, rules);

            allNodes.push_back(child);
            openQueue.push(nextId);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    result.statesExplored = statesCount;

    return result;
}

bool SolitaireSolver::isDealSolvable(uint32_t seed, GameVariant variant, float timeoutMs, std::atomic<bool>* cancelSignal) {
    SolitaireBoard testBoard;
    testBoard.newDeal(seed, variant);
    SolveResult res = solve(testBoard, 15000, timeoutMs, cancelSignal);
    return res.solved;
}

bool SolitaireSolver::isDealSolvable(uint32_t seed, const SolitaireGameDef& def, float timeoutMs, std::atomic<bool>* cancelSignal) {
    SolitaireBoard testBoard;
    testBoard.newDeal(seed, def);
    SolveResult res = solve(testBoard, 15000, timeoutMs, cancelSignal);
    return res.solved;
}

} // namespace solengine
