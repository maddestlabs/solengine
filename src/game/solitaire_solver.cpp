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

    if (m.action == SolverMove::Action::ClickCard) {
        if (m.fromPile < piles.size() && m.cardIndex < piles[m.fromPile].cards.size()) {
            const auto& c = piles[m.fromPile].cards[m.cardIndex];
            return "Activate " + formatCardNotation(c.suit, c.rank);
        }
        return "Activate Card";
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

uint64_t hashScriptedSnapshot(const SolitaireBoard::ScriptedSnapshot& snap) noexcept {
    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v) noexcept {
        h ^= v;
        h *= 1099511628211ull;
    };
    for (const auto& p : snap.piles) {
        mix(0xFF00u | static_cast<uint64_t>(p.cards.size()));
        for (const auto& c : p.cards) {
            mix(static_cast<uint64_t>(c.id) | (c.faceUp ? 0x100ull : 0ull));
        }
    }
    for (const auto& [k, v] : snap.blackboard.intStats) {
        for (char ch : k) mix(static_cast<uint8_t>(ch));
        mix(static_cast<uint64_t>(static_cast<uint32_t>(v)));
    }
    return h;
}

SolveResult solveScriptedBoard(
    const SolitaireBoard& initialBoard,
    uint32_t maxStates,
    float timeoutMs,
    std::atomic<bool>* cancelSignal
) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result{};

    SolitaireBoard simBoard;
    simBoard.newDeal(initialBoard.seed(), initialBoard.activeGameDef());
    simBoard.restoreSnapshot(initialBoard.captureSnapshot());

    if (simBoard.gameStatus() == GameStatus::Victory) {
        result.solved = true;
        return result;
    }

    struct ScriptNode {
        uint32_t id{0};
        uint32_t parentId{0};
        uint32_t depth{0};
        int32_t score{0};
        SolverMove move{};
        SolitaireBoard::ScriptedSnapshot snapshot;
    };

    std::vector<ScriptNode> nodes;
    nodes.reserve(std::min<size_t>(maxStates, 4096u));

    auto cmp = [&nodes](uint32_t a, uint32_t b) {
        return nodes[a].score < nodes[b].score;
    };
    std::priority_queue<uint32_t, std::vector<uint32_t>, decltype(cmp)> open(cmp);
    std::unordered_set<uint64_t> visited;
    visited.reserve(std::min<size_t>(maxStates * 2, 8192u));

    ScriptNode root{};
    root.id = 0;
    root.snapshot = simBoard.captureSnapshot();
    root.score = simBoard.evaluateScriptHeuristic();
    visited.insert(hashScriptedSnapshot(root.snapshot));
    nodes.push_back(std::move(root));
    open.push(0);

    const uint32_t cappedMax = std::min<uint32_t>(maxStates, 16000u);
    uint32_t bestPartialId = 0;
    int32_t bestPartialScore = -2147483647;

    while (!open.empty()) {
        if (cancelSignal && cancelSignal->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float, std::milli>(now - startTime).count();
        if (elapsed >= timeoutMs) {
            result.timedOut = true;
            break;
        }
        if (nodes.size() >= cappedMax) {
            result.timedOut = true;
            break;
        }

        uint32_t curId = open.top();
        open.pop();
        ++result.statesExplored;

        const SolitaireBoard::ScriptedSnapshot curSnap = nodes[curId].snapshot;
        const uint32_t curDepth = nodes[curId].depth;
        if (curDepth >= 256u) continue;

        auto tryPushChild = [&](SolverMove mv) -> bool {
            GameStatus st = simBoard.gameStatus();
            if (st == GameStatus::Defeat) {
                return false;
            }
            auto childSnap = simBoard.captureSnapshot();
            uint64_t h = hashScriptedSnapshot(childSnap);
            if (!visited.insert(h).second && st != GameStatus::Victory) {
                return false;
            }

            ScriptNode child{};
            child.id = static_cast<uint32_t>(nodes.size());
            child.parentId = curId;
            child.depth = curDepth + 1;
            child.move = std::move(mv);
            child.snapshot = std::move(childSnap);
            child.score = simBoard.evaluateScriptHeuristic() - static_cast<int32_t>(child.depth) * 2;
            uint32_t childId = child.id;
            const int32_t childScore = child.score;
            const uint32_t childDepth = child.depth;
            nodes.push_back(std::move(child));
            if (childDepth >= 2u && childScore > bestPartialScore) {
                bestPartialScore = childScore;
                bestPartialId = childId;
            }

            if (st == GameStatus::Victory) {
                std::vector<SolverMove> path;
                uint32_t trace = childId;
                while (trace != 0) {
                    path.push_back(nodes[trace].move);
                    trace = nodes[trace].parentId;
                }
                std::reverse(path.begin(), path.end());
                result.moves = std::move(path);
                result.solved = true;
                return true;
            }
            open.push(childId);
            return false;
        };

        const size_t pCount = curSnap.piles.size();

        // 1. Enumerate card clicks (RPG / activation / Golf / Pyramid Kings)
        if (simBoard.hasScriptCardClick()) {
            for (size_t p = 0; p < pCount; ++p) {
                const size_t cCount = curSnap.piles[p].cards.size();
                if (cCount == 0) continue;
                size_t cIdx = cCount - 1;
                simBoard.restoreSnapshot(curSnap);
                SolverMove mv{};
                mv.action = SolverMove::Action::ClickCard;
                mv.fromPile = static_cast<uint8_t>(p);
                mv.cardIndex = static_cast<uint8_t>(cIdx);
                mv.toPile = static_cast<uint8_t>(p);
                mv.notation = buildMoveNotation(simBoard, mv);
                if (simBoard.smartMove(static_cast<uint8_t>(p), cIdx)) {
                    if (tryPushChild(std::move(mv))) {
                        auto end = std::chrono::high_resolution_clock::now();
                        result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                        return result;
                    }
                }
            }
        }

        // 2. Enumerate pile-to-pile moves (can_pick + can_drop)
        if (simBoard.hasScriptCanDrop()) {
            for (size_t p = 0; p < pCount; ++p) {
                const size_t cCount = curSnap.piles[p].cards.size();
                for (size_t cIdx = 0; cIdx < cCount; ++cIdx) {
                    if (simBoard.hasScriptCanPick()) {
                        simBoard.restoreSnapshot(curSnap);
                        if (!simBoard.canPickScriptCard(static_cast<uint8_t>(p), cIdx)) {
                            continue;
                        }
                    }
                    for (size_t dst = 0; dst < pCount; ++dst) {
                        if (dst == p) continue;
                        simBoard.restoreSnapshot(curSnap);
                        SolverMove mv{};
                        mv.action = SolverMove::Action::MoveCards;
                        mv.fromPile = static_cast<uint8_t>(p);
                        mv.cardIndex = static_cast<uint8_t>(cIdx);
                        mv.toPile = static_cast<uint8_t>(dst);
                        std::string note = buildMoveNotation(simBoard, mv);
                        if (simBoard.executeMove(static_cast<uint8_t>(p), cIdx, static_cast<uint8_t>(dst), false)) {
                            mv.notation = std::move(note);
                            if (tryPushChild(std::move(mv))) {
                                auto end = std::chrono::high_resolution_clock::now();
                                result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                                return result;
                            }
                        }
                    }
                }
            }
        }

        // 3. Enumerate card-on-card targeting (Pyramid / Giza pairing)
        if (simBoard.hasScriptCanTarget()) {
            for (size_t p = 0; p < pCount; ++p) {
                const size_t cCount = curSnap.piles[p].cards.size();
                if (cCount == 0) continue;
                const size_t cIdx = cCount - 1;
                for (size_t dst = 0; dst < pCount; ++dst) {
                    if (dst == p || curSnap.piles[dst].cards.empty()) continue;
                    simBoard.restoreSnapshot(curSnap);
                    SolverMove mv{};
                    mv.action = SolverMove::Action::MoveCards;
                    mv.fromPile = static_cast<uint8_t>(p);
                    mv.cardIndex = static_cast<uint8_t>(cIdx);
                    mv.toPile = static_cast<uint8_t>(dst);
                    std::string note = buildMoveNotation(simBoard, mv);
                    if (simBoard.executeMove(static_cast<uint8_t>(p), cIdx, static_cast<uint8_t>(dst), false)) {
                        mv.notation = std::move(note);
                        if (tryPushChild(std::move(mv))) {
                            auto end = std::chrono::high_resolution_clock::now();
                            result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                            return result;
                        }
                    }
                }
            }
        }

        // 4. Enumerate stock click (draw / room refill)
        if (simBoard.hasScriptStockClick()) {
            simBoard.restoreSnapshot(curSnap);
            SolverMove mv{};
            mv.action = SolverMove::Action::DrawStock;
            mv.notation = buildMoveNotation(simBoard, mv);
            if (simBoard.drawFromStock()) {
                if (tryPushChild(std::move(mv))) {
                    auto end = std::chrono::high_resolution_clock::now();
                    result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                    return result;
                }
            }
        }
    }

    if (!result.solved && !result.cancelled && bestPartialId != 0) {
        std::vector<SolverMove> path;
        uint32_t trace = bestPartialId;
        while (trace != 0) {
            path.push_back(nodes[trace].move);
            trace = nodes[trace].parentId;
        }
        std::reverse(path.begin(), path.end());
        result.moves = std::move(path);
        result.isPartialDemo = !result.moves.empty();
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
    return result;
}

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

static constexpr size_t kMaxCardsPerPile = 104;
static constexpr size_t kMaxPiles = 44;

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
    bool isTriPeaks{false};
    bool isRadialClock{false};
    bool isPyramid{false};
    bool isYukon{false};
    bool isRussian{false};
    bool isFortyThieves{false};
    bool isBakersGame{false};
    bool isFreeCell{false};
    bool isBakersDozen{false};
    bool isFan{false};
    bool isNoEmptyRefill{false};
    bool isSameSuitTableau{false};
    bool isAnyEmpty{false};
    bool isEasthaven{false};
    bool isScorpion{false};
    bool isCanfield{false};
    bool isRainbow{false};
    uint8_t canfieldBaseRank{1};
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
    const auto& def = board.activeGameDef();
    const std::string& id = def.id;
    rules.variant = board.variant();
    rules.decks = def.decks > 0 ? def.decks : (board.isSpider() ? 2 : 1);
    rules.targetFoundationCards = static_cast<uint32_t>(rules.decks) * 52u;
    rules.isScorpion = (id == "scorpion" || id == "wasp");
    rules.isSpider = !rules.isScorpion && (board.isSpider() || id == "spiderette");
    rules.isTriPeaks = (id == "tri_peaks" || id == "tripeaks" || def.layout == "tripeaks");
    rules.isRadialClock = (id == "clockwork_dial" || def.layout == "radial_clock");
    rules.isGolf = board.isGolf() || rules.isTriPeaks || rules.isRadialClock || id == "golf";
    rules.isPyramid = board.isPyramidGame();
    rules.isYukon = (def.family == "Yukon");
    rules.isRussian = (id == "russian_solitaire" || def.name.find("Russian") != std::string::npos);
    rules.isFortyThieves = (def.family == "FortyThieves");
    rules.isBakersDozen = (id == "bakers_dozen");
    rules.isBakersGame = (id == "bakers_game");
    rules.isFreeCell = (rules.variant == GameVariant::FreeCell || (def.freecells > 0 && def.layout != "canfield"));
    rules.isFan = (def.layout == "fan_grid" || def.family == "Fan");
    rules.isNoEmptyRefill = (id == "bakers_dozen" || id == "la_belle_lucie");
    rules.isSameSuitTableau = (rules.isBakersGame || id == "eight_off" || id == "seahaven_towers" ||
                               id == "la_belle_lucie" || id == "the_fan" || rules.isRussian ||
                               rules.isFortyThieves || id == "scorpion" || id == "wasp");
    rules.isAnyEmpty = (id == "freecell" || id == "bakers_game" || rules.isFortyThieves ||
                        id == "wasp" || rules.isSpider);
    rules.isEasthaven = (id == "easthaven");
    rules.isCanfield = (def.layout == "canfield" || def.family == "Canfield");
    rules.isRainbow = (id == "rainbow");
    if (rules.isCanfield) {
        const auto& piles = board.piles();
        if (piles.size() > 2 && !piles[2].cards.empty()) {
            rules.canfieldBaseRank = static_cast<uint8_t>(piles[2].cards[0].rank);
        } else {
            rules.canfieldBaseRank = static_cast<uint8_t>(std::clamp(board.blackboard().getInt("BASE_RANK", 1), 1, 13));
        }
    }
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

    if (rules.isSpider || rules.isScorpion) {
        int completedFoundations = 0;
        int inSuitSequences = 0;
        int faceDownCount = 0;
        int stockCount = 0;
        int emptyCols = 0;

        for (size_t p = 0; p < board.numPiles; ++p) {
            const auto& pile = board.piles[p];
            if (pile.type == PileType::Stock) {
                stockCount += static_cast<int>(pile.count);
            } else if (pile.type == PileType::Foundation) {
                completedFoundations += static_cast<int>(pile.count / 13);
            } else if (pile.type == PileType::Tableau) {
                if (pile.empty()) {
                    ++emptyCols;
                } else {
                    for (size_t i = 0; i < pile.count; ++i) {
                        if (!pile.cards[i].faceUp()) {
                            ++faceDownCount;
                        } else if (i + 1 < pile.count && pile.cards[i + 1].faceUp()) {
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
        const int stockBonus = (rules.decks >= 2) ? (stockCount * 15) : 0;
        return completedFoundations * 3000 + inSuitSequences * 60 + emptyCols * 140 + stockBonus - faceDownCount * 120;
    }

    // Classic Foundation Games (Klondike, FreeCell, Yukon, Forty Thieves, Baker's, etc.)
    int foundationCards = 0;
    int faceDownCards = 0;
    int wasteCards = 0;
    int emptyColumns = 0;
    int emptyFreeCells = 0;
    int tableauSequences = 0;

    for (size_t p = 0; p < board.numPiles; ++p) {
        const auto& pile = board.piles[p];
        if (pile.type == PileType::Foundation) {
            foundationCards += static_cast<int>(pile.count);
        } else if (pile.type == PileType::Waste) {
            wasteCards += static_cast<int>(pile.count);
        } else if (pile.type == PileType::FreeCell) {
            if (pile.empty()) ++emptyFreeCells;
        } else if (pile.type == PileType::Tableau) {
            if (pile.empty()) {
                ++emptyColumns;
            } else {
                for (size_t i = 0; i < pile.count; ++i) {
                    if (!pile.cards[i].faceUp()) {
                        ++faceDownCards;
                    } else if (i + 1 < pile.count && pile.cards[i + 1].faceUp()) {
                        const auto& cA = pile.cards[i];
                        const auto& cB = pile.cards[i + 1];
                        if (static_cast<int>(cA.rank()) == static_cast<int>(cB.rank()) + 1) {
                            if (cA.suit() == cB.suit()) {
                                tableauSequences += rules.isSameSuitTableau ? 3 : 1;
                            } else if (!rules.isSameSuitTableau && isRedSuit(cA.suit()) != isRedSuit(cB.suit())) {
                                tableauSequences += 2;
                            }
                        }
                    }
                }
            }
        }
    }

    return (foundationCards * 400) + (emptyColumns * 120) + (emptyFreeCells * 80) +
           (tableauSequences * 30) - (faceDownCards * 80) - (wasteCards * 12);
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
        if (startCardIdx != src.count - 1 || !lead.faceUp()) {
            return false;
        }
        if (rules.isTriPeaks && fromPileIdx >= 2 && fromPileIdx <= 29) {
            const size_t idx = static_cast<size_t>(fromPileIdx - 2);
            if (idx < 18 && b.numPiles >= 30) {
                static constexpr uint8_t kChildren[18][2] = {
                    {3, 4}, {5, 6}, {7, 8},
                    {9, 10}, {10, 11}, {12, 13}, {13, 14}, {15, 16}, {16, 17},
                    {18, 19}, {19, 20}, {20, 21}, {21, 22}, {22, 23}, {23, 24}, {24, 25}, {25, 26}, {26, 27}
                };
                if (!b.piles[2 + kChildren[idx][0]].empty() || !b.piles[2 + kChildren[idx][1]].empty()) {
                    return false;
                }
            }
        }
        if (dst.empty()) {
            return true;
        }
        const CompactCard& top = dst.back();
        const int movingRank = static_cast<int>(lead.rank());
        const int topRank = static_cast<int>(top.rank());
        if (rules.isTriPeaks || rules.isRadialClock) {
            return (movingRank == topRank + 1) || (movingRank == topRank - 1) ||
                   (topRank == 13 && movingRank == 1) || (topRank == 1 && movingRank == 13);
        }
        if (top.rank() == Rank::King) {
            return false;
        }
        return (movingRank == topRank + 1) || (movingRank == topRank - 1);
    }

    if (rules.isScorpion) {
        if (dst.type != PileType::Tableau) return false;
        if (dst.empty()) {
            return rules.isAnyEmpty ? true : (lead.rank() == Rank::King);
        }
        const CompactCard& top = dst.back();
        if (!top.faceUp()) return false;
        return (lead.suit() == top.suit()) && (static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1);
    }

    if (rules.isCanfield) {
        if (dst.type == PileType::Foundation) {
            if (moveCount != 1) return false;
            if (dst.empty()) {
                return static_cast<uint8_t>(lead.rank()) == rules.canfieldBaseRank;
            }
            const CompactCard& top = dst.back();
            uint8_t nextR = static_cast<uint8_t>((static_cast<int>(top.rank()) % 13) + 1);
            return (lead.suit() == top.suit()) && (static_cast<uint8_t>(lead.rank()) == nextR);
        }
        if (dst.type == PileType::Tableau) {
            if (src.type == PileType::Tableau && startCardIdx != 0) return false;
            const bool reserveEmpty = (b.numPiles <= 6 || b.piles[6].empty());
            if (dst.empty()) {
                if (fromPileIdx == 6) return true;
                return reserveEmpty && (src.type == PileType::Waste || src.type == PileType::Tableau);
            }
            const CompactCard& top = dst.back();
            if (!top.faceUp()) return false;
            uint8_t nextDown = static_cast<uint8_t>((static_cast<int>(lead.rank()) % 13) + 1);
            if (static_cast<uint8_t>(top.rank()) != nextDown) return false;
            return rules.isRainbow ? true : (isRedSuit(lead.suit()) != isRedSuit(top.suit()));
        }
        return false;
    }

    if (rules.isBakersDozen || rules.isFan) {
        if (moveCount != 1) return false;
        if (dst.type == PileType::Foundation) {
            if (dst.empty()) return lead.rank() == Rank::Ace;
            const CompactCard& top = dst.back();
            return (lead.suit() == top.suit()) && (static_cast<int>(lead.rank()) == static_cast<int>(top.rank()) + 1);
        }
        if (dst.type == PileType::Tableau) {
            if (dst.empty()) {
                if (rules.isNoEmptyRefill) return false;
                return lead.rank() == Rank::King;
            }
            const CompactCard& top = dst.back();
            if (!top.faceUp()) return false;
            if (rules.isSameSuitTableau && lead.suit() != top.suit()) return false;
            return static_cast<int>(top.rank()) == static_cast<int>(lead.rank()) + 1;
        }
        return false;
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

    if (rules.isSpider || rules.isScorpion) {
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
            if (rules.isSameSuitTableau) {
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
        if (rules.isFreeCell && dst.type == PileType::Tableau) {
            size_t emptyFC = 0;
            size_t emptyOtherTab = 0;
            for (size_t p = 0; p < b.numPiles; ++p) {
                if (b.piles[p].type == PileType::FreeCell && b.piles[p].empty()) {
                    ++emptyFC;
                } else if (b.piles[p].type == PileType::Tableau && p != toPileIdx && b.piles[p].empty()) {
                    ++emptyOtherTab;
                }
            }
            const size_t shift = std::min<size_t>(emptyOtherTab, 10);
            const size_t maxMovable = (emptyFC + 1) << shift;
            if (moveCount > maxMovable) {
                return false;
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
            if (rules.isAnyEmpty) {
                return true;
            }
            return lead.rank() == Rank::King;
        }
        const CompactCard& top = dst.back();
        if (!top.faceUp()) return false;
        if (rules.isSameSuitTableau) {
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

    if (rules.isTriPeaks && b.numPiles >= 30) {
        static constexpr uint8_t kChildren[18][2] = {
            {3, 4}, {5, 6}, {7, 8},
            {9, 10}, {10, 11}, {12, 13}, {13, 14}, {15, 16}, {16, 17},
            {18, 19}, {19, 20}, {20, 21}, {21, 22}, {22, 23}, {23, 24}, {24, 25}, {25, 26}, {26, 27}
        };
        for (size_t idx = 0; idx < 18; ++idx) {
            CompactPile& parent = b.piles[2 + idx];
            if (!parent.empty() && !parent.back().faceUp() &&
                b.piles[2 + kChildren[idx][0]].empty() &&
                b.piles[2 + kChildren[idx][1]].empty()) {
                parent.back().setFaceUp(true);
            }
        }
    }

    if (rules.isCanfield && b.numPiles > 10 && !b.piles[6].empty()) {
        for (size_t t = 7; t <= 10; ++t) {
            if (b.piles[t].empty() && !b.piles[6].empty()) {
                CompactCard rc = b.piles[6].back();
                b.piles[6].pop_back();
                rc.setFaceUp(true);
                b.piles[t].push_back(rc);
            }
        }
        if (!b.piles[6].empty() && !b.piles[6].back().faceUp()) {
            b.piles[6].back().setFaceUp(true);
        }
    }

    if (rules.isSpider || rules.isScorpion) {
        collectCompletedSpiderRuns(b);
    }

    return true;
}

bool drawFromStock(CompactBoard& b, const GameRules& rules) {
    size_t stockIdx = b.numPiles;
    for (size_t i = 0; i < b.numPiles; ++i) {
        if (b.piles[i].type == PileType::Stock) {
            stockIdx = i;
            break;
        }
    }
    if (stockIdx >= b.numPiles) {
        return false;
    }
    CompactPile& stock = b.piles[stockIdx];

    if (rules.isScorpion) {
        if (stock.empty()) return false;
        size_t dealt = 0;
        for (size_t i = 0; i < b.numPiles && dealt < 3 && !stock.empty(); ++i) {
            if (b.piles[i].type == PileType::Tableau) {
                CompactCard c = stock.back();
                stock.pop_back();
                c.setFaceUp(true);
                b.piles[i].push_back(c);
                ++dealt;
            }
        }
        collectCompletedSpiderRuns(b);
        return dealt > 0;
    }

    if (rules.isEasthaven) {
        if (stock.empty()) return false;
        for (size_t i = 0; i < b.numPiles && !stock.empty(); ++i) {
            if (b.piles[i].type == PileType::Tableau) {
                CompactCard c = stock.back();
                stock.pop_back();
                c.setFaceUp(true);
                b.piles[i].push_back(c);
            }
        }
        return true;
    }

    if (rules.isSpider || rules.isScorpion) {
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
        for (size_t i = 0; i < b.numPiles; ++i) {
            if (b.piles[i].type == PileType::Foundation) {
                b.piles[i].push_back(c);
                return true;
            }
        }
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
        const uint8_t drawCount = (rules.variant == GameVariant::KlondikeDraw3 || (rules.isCanfield && !rules.isRainbow))
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
        if (rules.isFortyThieves || rules.isRainbow) {
            return false;
        }
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

// ============================================================================
// Reusable Fast Native Room-Crawl RPG Solver (Scoundrel / Card Crawl Archetype)
// ============================================================================

struct RpgCrawlConfig {
    uint8_t stockZone{0};
    uint8_t graveyardZone{1};
    uint8_t weaponZone{2};
    uint8_t roomStartZone{3};
    uint8_t roomEndZone{6};
    int16_t maxHp{20};
    int16_t acePower{14};
    uint8_t maxPotionsPerRoom{1}; // 1 for Scoundrel, 99 for unlimited
    bool durabilityLessOrEqual{true};
    bool allowBarehanded{true};
    bool allowFlee{true};
    bool allowConsecutiveFlee{false};
};

struct RpgCrawlState {
    int16_t hp{20};
    int16_t weapon{0};
    int16_t cap{99};
    uint8_t potionsUsed{0};
    uint8_t fleeUsed{0};
};

bool isRpgCrawlGame(const SolitaireBoard& board) noexcept {
    const auto& def = board.activeGameDef();
    if (def.family == "Scoundrel") return true;
    return def.category == "RPG" &&
           def.tableaus >= 2 &&
           board.piles().size() >= static_cast<size_t>(3 + def.tableaus) &&
           board.blackboard().intStats.count("HP") > 0;
}

RpgCrawlConfig extractRpgCrawlConfig(const SolitaireBoard& board) noexcept {
    RpgCrawlConfig cfg{};
    const auto& def = board.activeGameDef();
    const auto& bb = board.blackboard();
    cfg.roomStartZone = 3;
    cfg.roomEndZone = static_cast<uint8_t>(std::min<size_t>(
        board.piles().size() - 1,
        static_cast<size_t>(3 + std::max<uint8_t>(1, def.tableaus) - 1)
    ));
    cfg.maxHp = static_cast<int16_t>(std::max(1, bb.getInt("MAX_HP", bb.getInt("HP", 20))));
    cfg.acePower = 14;
    cfg.maxPotionsPerRoom = (bb.intStats.count("POTION_USED") > 0) ? 1 : 99;
    cfg.durabilityLessOrEqual = true;
    cfg.allowBarehanded = true;
    cfg.allowFlee = true;
    cfg.allowConsecutiveFlee = false;
    return cfg;
}

int16_t rpgCardPower(CompactCard c, const RpgCrawlConfig& cfg) noexcept {
    uint8_t r = static_cast<uint8_t>(c.rank());
    return (r == 1) ? cfg.acePower : static_cast<int16_t>(r);
}

uint8_t rpgRoomCount(const CompactBoard& b, const RpgCrawlConfig& cfg) noexcept {
    uint8_t cnt = 0;
    for (uint8_t z = cfg.roomStartZone; z <= cfg.roomEndZone; ++z) {
        if (!b.piles[z].empty()) ++cnt;
    }
    return cnt;
}

void rpgDealEmptyRoomSlots(CompactBoard& b, const RpgCrawlConfig& cfg) noexcept {
    auto& stock = b.piles[cfg.stockZone];
    for (uint8_t z = cfg.roomStartZone; z <= cfg.roomEndZone; ++z) {
        if (b.piles[z].empty() && !stock.empty()) {
            CompactCard c = stock.back();
            stock.pop_back();
            c.setFaceUp(true);
            b.piles[z].push_back(c);
        }
    }
}

void rpgRefillRoom(CompactBoard& b, RpgCrawlState& st, const RpgCrawlConfig& cfg) noexcept {
    rpgDealEmptyRoomSlots(b, cfg);
    st.potionsUsed = 0;
    st.fleeUsed = 0;
}

bool rpgApplyAction(
    CompactBoard& b,
    RpgCrawlState& st,
    const RpgCrawlConfig& cfg,
    SolverMove::Action action,
    uint8_t zoneIdx,
    bool useWeapon
) noexcept {
    if (action == SolverMove::Action::DrawStock) {
        if (!cfg.allowFlee || b.piles[cfg.stockZone].empty()) return false;
        const uint8_t numSlots = static_cast<uint8_t>(cfg.roomEndZone - cfg.roomStartZone + 1);
        if (rpgRoomCount(b, cfg) != numSlots) return false;
        if (!cfg.allowConsecutiveFlee && st.fleeUsed != 0) return false;

        auto& stock = b.piles[cfg.stockZone];
        for (uint8_t z = cfg.roomStartZone; z <= cfg.roomEndZone; ++z) {
            if (!b.piles[z].empty()) {
                CompactCard c = b.piles[z].back();
                b.piles[z].pop_back();
                c.setFaceUp(false);
                if (stock.count < kMaxCardsPerPile) {
                    std::memmove(&stock.cards[1], &stock.cards[0], stock.count * sizeof(CompactCard));
                    stock.cards[0] = c;
                    ++stock.count;
                }
            }
        }
        rpgDealEmptyRoomSlots(b, cfg);
        st.potionsUsed = 0;
        st.fleeUsed = 1;
        return true;
    }

    if (zoneIdx < cfg.roomStartZone || zoneIdx > cfg.roomEndZone || b.piles[zoneIdx].empty()) {
        return false;
    }

    CompactCard c = b.piles[zoneIdx].back();
    const Suit s = c.suit();
    const int16_t p = rpgCardPower(c, cfg);

    if (s == Suit::Hearts) {
        if (st.potionsUsed < cfg.maxPotionsPerRoom) {
            st.hp = static_cast<int16_t>(std::min<int>(cfg.maxHp, st.hp + p));
            if (cfg.maxPotionsPerRoom < 99) ++st.potionsUsed;
        }
        b.piles[zoneIdx].pop_back();
        b.piles[cfg.graveyardZone].push_back(c);
    } else if (s == Suit::Diamonds) {
        if (!b.piles[cfg.weaponZone].empty()) {
            CompactCard oldWpn = b.piles[cfg.weaponZone].back();
            b.piles[cfg.weaponZone].pop_back();
            b.piles[cfg.graveyardZone].push_back(oldWpn);
        }
        b.piles[zoneIdx].pop_back();
        b.piles[cfg.weaponZone].push_back(c);
        st.weapon = p;
        st.cap = 99;
    } else {
        int16_t dmg = p;
        const bool canBlock = useWeapon && st.weapon > 0 &&
            (cfg.durabilityLessOrEqual ? (p <= st.cap) : (p < st.cap));
        if (canBlock) {
            dmg = static_cast<int16_t>(std::max(0, p - st.weapon));
            st.cap = p;
        }
        st.hp = static_cast<int16_t>(std::max(0, st.hp - dmg));
        b.piles[zoneIdx].pop_back();
        b.piles[cfg.graveyardZone].push_back(c);
    }

    if (st.hp <= 0) {
        return false; // Defeat branch: prune immediately
    }

    if (rpgRoomCount(b, cfg) <= 1 && !b.piles[cfg.stockZone].empty()) {
        rpgRefillRoom(b, st, cfg);
    }
    return true;
}

uint64_t hashRpgCrawlState(const CompactBoard& b, const RpgCrawlState& st, const RpgCrawlConfig& cfg) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint8_t p = 0; p < b.numPiles; ++p) {
        if (p == cfg.graveyardZone) continue; // Graveyard order is inert
        h ^= static_cast<uint64_t>(b.piles[p].count) + 0x9e3779b97f4a7c15ULL;
        h *= 0x100000001b3ULL;
        for (uint8_t c = 0; c < b.piles[p].count; ++c) {
            h ^= b.piles[p].cards[c].raw;
            h *= 0x100000001b3ULL;
        }
    }
    auto mix = [&](uint64_t v) noexcept {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    mix(static_cast<uint64_t>(st.hp));
    mix(static_cast<uint64_t>(st.weapon));
    mix(static_cast<uint64_t>(st.cap));
    mix(static_cast<uint64_t>(st.potionsUsed));
    mix(static_cast<uint64_t>(st.fleeUsed));
    return h;
}

int32_t evalRpgCrawlHeuristic(const CompactBoard& b, const RpgCrawlState& st, const RpgCrawlConfig& cfg, uint32_t depth) noexcept {
    const int32_t remaining = static_cast<int32_t>(b.piles[cfg.stockZone].count) +
                              static_cast<int32_t>(rpgRoomCount(b, cfg));
    const int32_t effCap = (st.cap >= 99) ? 14 : static_cast<int32_t>(st.cap);
    return static_cast<int32_t>(st.hp) * 70 +
           static_cast<int32_t>(st.weapon) * 35 +
           effCap * 30 -
           remaining * 180 -
           static_cast<int32_t>(st.fleeUsed) * 40 -
           static_cast<int32_t>(depth) * 2;
}

SolveResult solveRpgCrawlBoard(
    const SolitaireBoard& initialBoard,
    const RpgCrawlConfig& cfg,
    uint32_t maxStates,
    float timeoutMs,
    std::atomic<bool>* cancelSignal
) {
    auto startTime = std::chrono::high_resolution_clock::now();
    SolveResult result{};

    if (initialBoard.isVictory()) {
        result.solved = true;
        return result;
    }

    const auto& bb = initialBoard.blackboard();
    RpgCrawlState rootState{};
    rootState.hp = static_cast<int16_t>(bb.getInt("HP", cfg.maxHp));
    rootState.weapon = static_cast<int16_t>(bb.getInt("WEAPON", 0));
    rootState.cap = static_cast<int16_t>(bb.getInt("CAP", 99));
    rootState.potionsUsed = static_cast<uint8_t>(bb.getInt("POTION_USED", 0));
    rootState.fleeUsed = static_cast<uint8_t>(bb.getInt("FLEE_USED", 0));

    CompactBoard rootBoard = fromSolitaireBoard(initialBoard);
    if (rootState.hp > 0 && rootBoard.piles[cfg.stockZone].empty() && rpgRoomCount(rootBoard, cfg) == 0) {
        result.solved = true;
        return result;
    }

    struct RpgNode {
        uint32_t id{0};
        uint32_t parentId{0};
        uint32_t depth{0};
        int32_t score{0};
        SolverMove move{};
        CompactBoard board{};
        RpgCrawlState state{};
    };

    const uint32_t cappedMax = std::max<uint32_t>(1000u, maxStates);
    std::vector<RpgNode> nodes;
    nodes.reserve(std::min<size_t>(cappedMax + 256u, 65536u));

    auto cmp = [&nodes](uint32_t a, uint32_t b) noexcept {
        if (nodes[a].score != nodes[b].score) return nodes[a].score < nodes[b].score;
        return nodes[a].depth > nodes[b].depth;
    };
    std::priority_queue<uint32_t, std::vector<uint32_t>, decltype(cmp)> open(cmp);

    std::unordered_set<uint64_t> visited;
    visited.reserve(std::min<size_t>(cappedMax * 2u, 131072u));

    RpgNode root{};
    root.id = 0;
    root.board = rootBoard;
    root.state = rootState;
    root.score = evalRpgCrawlHeuristic(rootBoard, rootState, cfg, 0);
    nodes.push_back(root);
    visited.insert(hashRpgCrawlState(rootBoard, rootState, cfg));
    open.push(0);

    uint32_t bestPartialId = 0;
    int32_t bestPartialScore = std::numeric_limits<int32_t>::min();

    while (!open.empty()) {
        if (cancelSignal && cancelSignal->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }
        if ((result.statesExplored & 255u) == 0u) {
            auto now = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float, std::milli>(now - startTime).count();
            if (elapsed >= timeoutMs) {
                result.timedOut = true;
                break;
            }
        }
        if (nodes.size() >= cappedMax) {
            result.timedOut = true;
            break;
        }

        const uint32_t curId = open.top();
        open.pop();
        ++result.statesExplored;

        const uint32_t curDepth = nodes[curId].depth;
        if (curDepth >= 256u) continue;

        auto tryPush = [&](const CompactBoard& nextBoard, const RpgCrawlState& nextState, SolverMove mv) -> bool {
            const bool won = (nextState.hp > 0 &&
                              nextBoard.piles[cfg.stockZone].empty() &&
                              rpgRoomCount(nextBoard, cfg) == 0);
            const uint64_t h = hashRpgCrawlState(nextBoard, nextState, cfg);
            if (!won && !visited.insert(h).second) {
                return false;
            }

            RpgNode child{};
            child.id = static_cast<uint32_t>(nodes.size());
            child.parentId = curId;
            child.depth = curDepth + 1;
            child.move = std::move(mv);
            child.board = nextBoard;
            child.state = nextState;
            child.score = evalRpgCrawlHeuristic(nextBoard, nextState, cfg, child.depth);
            const uint32_t childId = child.id;
            const int32_t childScore = child.score;
            nodes.push_back(child);

            if (child.depth >= 2u && childScore > bestPartialScore) {
                bestPartialScore = childScore;
                bestPartialId = childId;
            }

            if (won) {
                std::vector<SolverMove> path;
                uint32_t trace = childId;
                while (trace != 0) {
                    path.push_back(nodes[trace].move);
                    trace = nodes[trace].parentId;
                }
                std::reverse(path.begin(), path.end());
                result.moves = std::move(path);
                result.solved = true;
                return true;
            }
            open.push(childId);
            return false;
        };

        const CompactBoard curBoard = nodes[curId].board;
        const RpgCrawlState curState = nodes[curId].state;

        // 1. Room card actions (ClickCard, plus barehanded MoveCards to graveyard when armed)
        for (uint8_t z = cfg.roomStartZone; z <= cfg.roomEndZone; ++z) {
            if (curBoard.piles[z].empty()) continue;
            const CompactCard c = curBoard.piles[z].back();
            const Suit s = c.suit();
            const int16_t p = rpgCardPower(c, cfg);

            {
                CompactBoard nb = curBoard;
                RpgCrawlState ns = curState;
                if (rpgApplyAction(nb, ns, cfg, SolverMove::Action::ClickCard, z, true)) {
                    SolverMove mv{};
                    mv.action = SolverMove::Action::ClickCard;
                    mv.fromPile = z;
                    mv.cardIndex = 0;
                    mv.toPile = z;
                    if (tryPush(nb, ns, std::move(mv))) {
                        auto end = std::chrono::high_resolution_clock::now();
                        result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                        return result;
                    }
                }
            }

            const bool isMonster = (s == Suit::Spades || s == Suit::Clubs);
            const bool weaponEligible = (curState.weapon > 0) &&
                (cfg.durabilityLessOrEqual ? (p <= curState.cap) : (p < curState.cap));
            if (cfg.allowBarehanded && isMonster && weaponEligible) {
                CompactBoard nb = curBoard;
                RpgCrawlState ns = curState;
                if (rpgApplyAction(nb, ns, cfg, SolverMove::Action::MoveCards, z, false)) {
                    SolverMove mv{};
                    mv.action = SolverMove::Action::MoveCards;
                    mv.fromPile = z;
                    mv.cardIndex = 0;
                    mv.toPile = cfg.graveyardZone;
                    if (tryPush(nb, ns, std::move(mv))) {
                        auto end = std::chrono::high_resolution_clock::now();
                        result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                        return result;
                    }
                }
            }
        }

        // 2. Flee Room (DrawStock when all room slots are occupied and not fleeing consecutively)
        if (cfg.allowFlee) {
            CompactBoard nb = curBoard;
            RpgCrawlState ns = curState;
            if (rpgApplyAction(nb, ns, cfg, SolverMove::Action::DrawStock, cfg.stockZone, false)) {
                SolverMove mv{};
                mv.action = SolverMove::Action::DrawStock;
                mv.fromPile = cfg.stockZone;
                mv.toPile = cfg.stockZone;
                if (tryPush(nb, ns, std::move(mv))) {
                    auto end = std::chrono::high_resolution_clock::now();
                    result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
                    return result;
                }
            }
        }
    }

    if (!result.solved && !result.cancelled && bestPartialId != 0) {
        std::vector<SolverMove> path;
        uint32_t trace = bestPartialId;
        while (trace != 0) {
            path.push_back(nodes[trace].move);
            trace = nodes[trace].parentId;
        }
        std::reverse(path.begin(), path.end());
        result.moves = std::move(path);
        result.isPartialDemo = !result.moves.empty();
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.solveTimeMs = std::chrono::duration<float, std::milli>(end - startTime).count();
    return result;
}

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

    if (isRpgCrawlGame(initialBoard)) {
        const RpgCrawlConfig cfg = extractRpgCrawlConfig(initialBoard);
        return solveRpgCrawlBoard(initialBoard, cfg, maxStates, timeoutMs, cancelSignal);
    }

    const bool useScriptSolver = initialBoard.hasScriptHooks() &&
        (initialBoard.activeGameDef().category == "RPG" ||
         (initialBoard.hasScriptCardClick() && !initialBoard.isGolf() && !initialBoard.isPyramidGame()));
    if (useScriptSolver) {
        return solveScriptedBoard(initialBoard, maxStates, timeoutMs, cancelSignal);
    }

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
    const uint32_t maxDepth = 500;
    uint32_t bestPartialId = 0;
    int32_t bestPartialScore = -2147483647;

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
            for (size_t i = 0; i < currentBoard.numPiles; ++i) {
                if (currentBoard.piles[i].type == PileType::Stock && !currentBoard.piles[i].empty()) {
                    CompactMove cm{SolverMove::Action::DrawStock, static_cast<uint8_t>(i), 0, foundIdx};
                    candidates.push_back(cm);
                    break;
                }
            }
        } else {
            bool foundDominant = false;

            auto findBestFoundation = [&](size_t srcPile, size_t topCardIdx) -> int {
                const Suit cardSuit = currentBoard.piles[srcPile].cards[topCardIdx].suit();
                int fallback = -1;
                size_t foundOrd = 0;
                for (size_t d = 0; d < currentBoard.numPiles; ++d) {
                    if (currentBoard.piles[d].type != PileType::Foundation) continue;
                    if (canMoveCards(currentBoard, static_cast<uint8_t>(srcPile), topCardIdx, static_cast<uint8_t>(d), rules)) {
                        if ((foundOrd % 4) == static_cast<size_t>(cardSuit)) {
                            return static_cast<int>(d);
                        }
                        if (fallback < 0) fallback = static_cast<int>(d);
                    }
                    ++foundOrd;
                }
                return fallback;
            };

            // 1. Dominant Safe Moves (Ace or 2 to Foundation - not in Spider/Scorpion/Canfield)
            if (!rules.isSpider && !rules.isScorpion && !rules.isCanfield) {
                for (size_t s = 0; s < currentBoard.numPiles; ++s) {
                    const auto& src = currentBoard.piles[s];
                    if (src.type == PileType::Foundation || src.type == PileType::Stock || src.empty()) continue;
                    size_t topIdx = src.count - 1;
                    const auto& card = src.cards[topIdx];
                    if (!card.faceUp()) continue;

                    if (card.rank() == Rank::Ace || card.rank() == Rank::Two || ((rules.isFan || rules.isFortyThieves) && rules.isSameSuitTableau)) {
                        int bestF = findBestFoundation(s, topIdx);
                        if (bestF >= 0) {
                            CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(bestF)};
                            candidates.clear();
                            candidates.push_back(cm);
                            foundDominant = true;
                            break;
                        }
                    }
                }
            }

            if (!foundDominant) {
                // 2. Foundation Moves
                if (!rules.isSpider && !rules.isScorpion) {
                    for (size_t s = 0; s < currentBoard.numPiles; ++s) {
                        const auto& src = currentBoard.piles[s];
                        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.empty()) continue;
                        size_t topIdx = src.count - 1;
                        if (!src.cards[topIdx].faceUp()) continue;

                        int bestF = findBestFoundation(s, topIdx);
                        if (bestF >= 0) {
                            CompactMove cm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(bestF)};
                            candidates.push_back(cm);
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
                                    if (!rules.isSpider && !rules.isSameSuitTableau && isRedSuit(below.suit()) == isRedSuit(target.suit())) {
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
                            } else if (dst.type == PileType::FreeCell && !rules.isCanfield) {
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
                    if (!canDraw && !rules.isFortyThieves && !rules.isRainbow && !rules.isEasthaven && !rules.isScorpion &&
                        currentBoard.numPiles > 1 && currentBoard.piles[1].type == PileType::Waste &&
                        !currentBoard.piles[1].empty() && currentStockRecycles < 3) {
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
            if (child.depth >= 2u && child.score > bestPartialScore) {
                bestPartialScore = child.score;
                bestPartialId = nextId;
            }
            openQueue.push(nextId);
        }
    }

    if (!result.solved && !result.cancelled && bestPartialId != 0) {
        std::vector<CompactMove> compactPath;
        uint32_t trace = bestPartialId;
        while (trace != 0) {
            compactPath.push_back(allNodes[trace].move);
            trace = allNodes[trace].parentId;
        }
        std::reverse(compactPath.begin(), compactPath.end());

        SolitaireBoard replayBoard = initialBoard;
        std::vector<SolverMove> path;
        path.reserve(compactPath.size());
        for (const auto& cm : compactPath) {
            SolverMove sm{};
            sm.action = cm.action;
            sm.fromPile = cm.fromPile;
            sm.cardIndex = cm.cardIndex;
            sm.toPile = cm.toPile;
            sm.secondFromPile = cm.secondFromPile;
            sm.notation = buildMoveNotation(replayBoard, sm);
            path.push_back(std::move(sm));

            if (sm.action == SolverMove::Action::DrawStock) {
                replayBoard.drawFromStock();
            } else if (sm.action == SolverMove::Action::PairPyramid) {
                replayBoard.executePyramidPair(sm.fromPile, sm.secondFromPile, sm.toPile);
            } else {
                replayBoard.executeMove(sm.fromPile, sm.cardIndex, sm.toPile, true);
            }
        }
        result.moves = std::move(path);
        result.isPartialDemo = !result.moves.empty();
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
    if (!def.supportsSolver) return true;
    SolitaireBoard testBoard;
    testBoard.newDeal(seed, def);
    SolveResult res = solve(testBoard, 15000, timeoutMs, cancelSignal);
    return res.solved;
}

} // namespace solengine
