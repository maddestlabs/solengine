#include "solengine/solitaire_solver.hpp"
#include <chrono>
#include <unordered_set>
#include <queue>
#include <memory>
#include <algorithm>

namespace solengine {

std::string formatCardNotation(Suit suit, Rank rank) {
    const char* suits[] = { "♠", "♡", "♢", "♣" };
    const char* ranks[] = { "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };
    uint8_t sIdx = static_cast<uint8_t>(suit) % 4;
    uint8_t rIdx = static_cast<uint8_t>(rank);
    if (rIdx < 1 || rIdx > 13) rIdx = 1;
    return std::string(suits[sIdx]) + ranks[rIdx];
}

namespace {

uint64_t computeBoardHash(const SolitaireBoard& board) {
    uint64_t h = 14695981039346656037ULL;
    auto hashByte = [&h](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };

    const auto& piles = board.piles();
    std::vector<uint8_t> freeCellCards;

    for (size_t pIdx = 0; pIdx < piles.size(); ++pIdx) {
        const auto& p = piles[pIdx];

        if (p.type == PileType::FreeCell) {
            if (p.cards.empty()) {
                freeCellCards.push_back(0);
            } else {
                const auto& c = p.cards.back();
                uint8_t val = (static_cast<uint8_t>(c.rank) & 0x0F) |
                              ((static_cast<uint8_t>(c.suit) & 0x03) << 4);
                freeCellCards.push_back(val);
            }
            continue;
        }

        hashByte(static_cast<uint8_t>(p.type));
        hashByte(static_cast<uint8_t>(p.cards.size()));

        if (p.type == PileType::Foundation) {
            if (!p.cards.empty()) {
                const auto& c = p.cards.back();
                hashByte(static_cast<uint8_t>(c.suit));
                hashByte(static_cast<uint8_t>(c.rank));
            }
            continue;
        }

        for (const auto& c : p.cards) {
            uint8_t val = (static_cast<uint8_t>(c.rank) & 0x0F) |
                          ((static_cast<uint8_t>(c.suit) & 0x03) << 4) |
                          (c.faceUp ? 0x80 : 0x00);
            hashByte(val);
        }
    }

    // Canonicalize FreeCells by sorting so slot permutations map to identical hash
    if (!freeCellCards.empty()) {
        std::sort(freeCellCards.begin(), freeCellCards.end());
        for (uint8_t b : freeCellCards) {
            hashByte(b);
        }
    }

    return h;
}

int evaluateHeuristic(const SolitaireBoard& board) {
    if (board.isVictory()) {
        return 1000000;
    }

    const auto& piles = board.piles();

    if (board.isPyramidGame()) {
        int pyrRemaining = 0;
        for (const auto& p : piles) {
            if (p.type == PileType::Tableau) {
                pyrRemaining += static_cast<int>(p.cards.size());
            }
        }
        return (28 - pyrRemaining) * 200;
    }

    if (board.isGolf()) {
        int tabRemaining = 0;
        for (const auto& p : piles) {
            if (p.type == PileType::Tableau) {
                tabRemaining += static_cast<int>(p.cards.size());
            }
        }
        return (52 - tabRemaining) * 200;
    }

    if (board.isSpider()) {
        int completedFoundations = 0;
        int inSuitSequences = 0;
        int faceUpCount = 0;
        int emptyCols = 0;

        for (const auto& p : piles) {
            if (p.type == PileType::Foundation) {
                completedFoundations += static_cast<int>(p.cards.size() / 13);
            } else if (p.type == PileType::Tableau) {
                if (p.cards.empty()) {
                    ++emptyCols;
                } else {
                    for (size_t i = 0; i < p.cards.size(); ++i) {
                        if (p.cards[i].faceUp) {
                            ++faceUpCount;
                            if (i + 1 < p.cards.size() && p.cards[i + 1].faceUp) {
                                if (static_cast<int>(p.cards[i].rank) == static_cast<int>(p.cards[i + 1].rank) + 1) {
                                    if (p.cards[i].suit == p.cards[i + 1].suit) {
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

    for (const auto& p : piles) {
        if (p.type == PileType::Foundation) {
            foundationCards += static_cast<int>(p.cards.size());
        } else if (p.type == PileType::FreeCell) {
            if (p.cards.empty()) ++emptyFreeCells;
        } else if (p.type == PileType::Tableau) {
            if (p.cards.empty()) {
                ++emptyColumns;
            } else {
                for (size_t i = 0; i < p.cards.size(); ++i) {
                    if (p.cards[i].faceUp) {
                        ++faceUpCards;
                        if (i + 1 < p.cards.size() && p.cards[i + 1].faceUp) {
                            const auto& cA = p.cards[i];
                            const auto& cB = p.cards[i + 1];
                            if (static_cast<int>(cA.rank) == static_cast<int>(cB.rank) + 1) {
                                if (isRedSuit(cA.suit) != isRedSuit(cB.suit)) {
                                    tableauSequences += 2;
                                    if (cA.suit == cB.suit) tableauSequences += 1;
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

struct SearchNode {
    uint32_t id{0};
    uint32_t parentId{0};
    SolverMove move{};
    int score{0};
    uint32_t depth{0};
    uint8_t stockRecycles{0};
    SolitaireBoard board;
};

struct NodeComparator {
    bool operator()(const std::shared_ptr<SearchNode>& a, const std::shared_ptr<SearchNode>& b) const {
        if (a->score != b->score) {
            return a->score < b->score; // Max-heap: highest heuristic score first
        }
        return a->depth > b->depth; // Tie breaker: prefer shallower paths
    }
};

} // namespace

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

    std::priority_queue<std::shared_ptr<SearchNode>, std::vector<std::shared_ptr<SearchNode>>, NodeComparator> openQueue;
    std::vector<std::shared_ptr<SearchNode>> allNodes;
    allNodes.reserve(std::min<size_t>(maxStates, 32768u));

    std::unordered_set<uint64_t> visited;
    visited.reserve(std::min<size_t>(maxStates * 2, 65536u));

    auto root = std::make_shared<SearchNode>();
    root->id = 0;
    root->parentId = 0;
    root->depth = 0;
    root->stockRecycles = 0;
    root->board = initialBoard;
    root->score = evaluateHeuristic(root->board);

    uint64_t rootHash = computeBoardHash(root->board);
    visited.insert(rootHash);
    allNodes.push_back(root);
    openQueue.push(root);

    uint32_t statesCount = 0;
    const uint32_t maxDepth = 220;
    const bool isGolf = initialBoard.isGolf();
    const bool isPyramid = initialBoard.isPyramidGame();

    while (!openQueue.empty()) {
        auto current = openQueue.top();
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

        if (current->board.isVictory()) {
            // Reconstruct path from current node back to root
            std::vector<SolverMove> path;
            auto curr = current;
            while (curr && curr->id != 0) {
                path.push_back(curr->move);
                curr = allNodes[curr->parentId];
            }
            std::reverse(path.begin(), path.end());
            result.solved = true;
            result.moves = std::move(path);
            break;
        }

        if (current->depth >= maxDepth) {
            continue;
        }

        const auto& piles = current->board.piles();
        std::vector<SolverMove> candidates;

        if (isPyramid) {
            // Pyramid Kings
            for (size_t i = 0; i < piles.size(); ++i) {
                if (!current->board.isCardPlayableInPyramid(i) || piles[i].cards.empty()) continue;
                const auto& c = piles[i].cards.back();
                if (c.rank == Rank::King) {
                    SolverMove sm{SolverMove::Action::MoveCards, static_cast<uint8_t>(i), static_cast<uint8_t>(piles[i].cards.size() - 1), 2};
                    sm.notation = buildMoveNotation(current->board, sm);
                    candidates.push_back(sm);
                }
            }

            // Pyramid Pairs totaling 13
            for (size_t a = 0; a < piles.size(); ++a) {
                if (!current->board.isCardPlayableInPyramid(a) || piles[a].cards.empty()) continue;
                int rankA = static_cast<int>(piles[a].cards.back().rank);
                for (size_t b = a + 1; b < piles.size(); ++b) {
                    if (!current->board.isCardPlayableInPyramid(b) || piles[b].cards.empty()) continue;
                    int rankB = static_cast<int>(piles[b].cards.back().rank);
                    if (rankA + rankB == 13) {
                        SolverMove sm{SolverMove::Action::PairPyramid, static_cast<uint8_t>(a), static_cast<uint8_t>(piles[a].cards.size() - 1), 2, static_cast<uint8_t>(b)};
                        sm.notation = buildMoveNotation(current->board, sm);
                        candidates.push_back(sm);
                    }
                }
            }

            // Draw Stock
            if (!piles.empty() && (!piles[0].cards.empty() || (!piles[1].cards.empty() && current->stockRecycles < 3))) {
                SolverMove sm{SolverMove::Action::DrawStock, 0, 0, 1};
                sm.notation = buildMoveNotation(current->board, sm);
                candidates.push_back(sm);
            }
        } else if (isGolf) {
            // Golf: dynamically find Foundation pile
            uint8_t foundIdx = 1;
            for (size_t i = 0; i < piles.size(); ++i) {
                if (piles[i].type == PileType::Foundation) {
                    foundIdx = static_cast<uint8_t>(i);
                    break;
                }
            }
            for (size_t s = 0; s < piles.size(); ++s) {
                if (piles[s].type != PileType::Tableau || piles[s].cards.empty()) continue;
                size_t topIdx = piles[s].cards.size() - 1;
                if (current->board.canMoveCards(static_cast<uint8_t>(s), topIdx, foundIdx)) {
                    SolverMove sm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), foundIdx};
                    sm.notation = buildMoveNotation(current->board, sm);
                    candidates.push_back(sm);
                }
            }
            if (!piles.empty() && piles[0].type == PileType::Stock && !piles[0].cards.empty()) {
                SolverMove sm{SolverMove::Action::DrawStock, 0, 0, foundIdx};
                sm.notation = buildMoveNotation(current->board, sm);
                candidates.push_back(sm);
            }
        } else {
            // Standard Solitaire Games (Klondike, FreeCell, Spider, Yukon, Forty Thieves, Baker's)
            bool foundDominant = false;

            // 1. Dominant Safe Moves (Ace or 2 to Foundation - not in Spider)
            if (!current->board.isSpider()) {
                for (size_t s = 0; s < piles.size(); ++s) {
                    const auto& src = piles[s];
                    if (src.type == PileType::Foundation || src.type == PileType::Stock || src.cards.empty()) continue;
                    size_t topIdx = src.cards.size() - 1;
                    const auto& card = src.cards[topIdx];
                    if (!card.faceUp) continue;

                    if (card.rank == Rank::Ace || card.rank == Rank::Two) {
                        for (size_t d = 0; d < piles.size(); ++d) {
                            if (piles[d].type == PileType::Foundation && current->board.canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                                SolverMove sm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                                sm.notation = buildMoveNotation(current->board, sm);
                                candidates.clear();
                                candidates.push_back(sm);
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
                if (!current->board.isSpider()) {
                    for (size_t s = 0; s < piles.size(); ++s) {
                        const auto& src = piles[s];
                        if (src.type == PileType::Foundation || src.type == PileType::Stock || src.cards.empty()) continue;
                        size_t topIdx = src.cards.size() - 1;
                        if (!src.cards[topIdx].faceUp) continue;

                        for (size_t d = 0; d < piles.size(); ++d) {
                            if (piles[d].type == PileType::Foundation && current->board.canMoveCards(static_cast<uint8_t>(s), topIdx, static_cast<uint8_t>(d))) {
                                SolverMove sm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(topIdx), static_cast<uint8_t>(d)};
                                sm.notation = buildMoveNotation(current->board, sm);
                                candidates.push_back(sm);
                                break;
                            }
                        }
                    }
                }

                // 3. Tableau-to-Tableau, Tableau-to-FreeCell, and FreeCell-to-Tableau Moves
                for (size_t s = 0; s < piles.size(); ++s) {
                    const auto& src = piles[s];
                    if (src.type != PileType::Tableau && src.type != PileType::Waste && src.type != PileType::FreeCell) continue;
                    if (src.cards.empty()) continue;

                    bool pushedFreeCellForThisPile = false;

                    for (size_t cIdx = 0; cIdx < src.cards.size(); ++cIdx) {
                        if (!src.cards[cIdx].faceUp) continue;

                        // Only top card can move from Waste or FreeCell
                        if ((src.type == PileType::Waste || src.type == PileType::FreeCell) && cIdx + 1 != src.cards.size()) {
                            continue;
                        }

                        bool pushedEmptyTableau = false;

                        for (size_t d = 0; d < piles.size(); ++d) {
                            if (d == s) continue;
                            const auto& dst = piles[d];

                            // FreeCell to FreeCell is redundant
                            if (src.type == PileType::FreeCell && dst.type == PileType::FreeCell) {
                                continue;
                            }

                            // Moving base card of column to an empty column is redundant
                            if (src.type == PileType::Tableau && cIdx == 0 && dst.type == PileType::Tableau && dst.cards.empty()) {
                                continue;
                            }

                            // Prune duplicate empty tableau columns
                            if (dst.type == PileType::Tableau && dst.cards.empty()) {
                                if (pushedEmptyTableau) continue;
                            }

                            // Prune unproductive lateral swaps: moving from one parent to an identical parent
                            if (src.type == PileType::Tableau && dst.type == PileType::Tableau && cIdx > 0 && !dst.cards.empty()) {
                                const auto& below = src.cards[cIdx - 1];
                                const auto& target = dst.cards.back();
                                if (below.faceUp && below.rank == target.rank) {
                                    if (below.suit == target.suit) {
                                        continue;
                                    }
                                    if (!current->board.isSpider() && isRedSuit(below.suit) == isRedSuit(target.suit)) {
                                        continue;
                                    }
                                }
                            }

                            if (dst.type == PileType::Tableau) {
                                if (current->board.canMoveCards(static_cast<uint8_t>(s), cIdx, static_cast<uint8_t>(d))) {
                                    SolverMove sm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(cIdx), static_cast<uint8_t>(d)};
                                    sm.notation = buildMoveNotation(current->board, sm);
                                    candidates.push_back(sm);
                                    if (dst.cards.empty()) pushedEmptyTableau = true;
                                }
                            } else if (dst.type == PileType::FreeCell) {
                                if (cIdx + 1 == src.cards.size() && dst.cards.empty() && !pushedFreeCellForThisPile) {
                                    if (current->board.canMoveCards(static_cast<uint8_t>(s), cIdx, static_cast<uint8_t>(d))) {
                                        SolverMove sm{SolverMove::Action::MoveCards, static_cast<uint8_t>(s), static_cast<uint8_t>(cIdx), static_cast<uint8_t>(d)};
                                        sm.notation = buildMoveNotation(current->board, sm);
                                        candidates.push_back(sm);
                                        pushedFreeCellForThisPile = true;
                                    }
                                }
                            }
                        }
                    }
                }

                // 4. Stock Draw Move
                if (!piles.empty() && piles[0].type == PileType::Stock) {
                    const auto& stock = piles[0];
                    bool canDraw = !stock.cards.empty();
                    // Spider: cannot draw if any tableau column is empty
                    if (canDraw && current->board.isSpider()) {
                        for (const auto& p : piles) {
                            if (p.type == PileType::Tableau && p.cards.empty()) {
                                canDraw = false;
                                break;
                            }
                        }
                    }
                    if (!canDraw && piles.size() > 1 && piles[1].type == PileType::Waste && !piles[1].cards.empty() && current->stockRecycles < 3) {
                        canDraw = true;
                    }
                    if (canDraw) {
                        SolverMove sm{SolverMove::Action::DrawStock, 0, 0, 1};
                        sm.notation = buildMoveNotation(current->board, sm);
                        candidates.push_back(sm);
                    }
                }
            }
        }

        // Apply candidate moves and push valid, unvisited children
        for (const auto& sm : candidates) {
            SolitaireBoard nextBoard = current->board;
            bool ok = false;
            uint8_t nextRecycle = current->stockRecycles;

            if (sm.action == SolverMove::Action::DrawStock) {
                if (nextBoard.piles()[0].cards.empty()) {
                    ++nextRecycle;
                }
                ok = nextBoard.drawFromStock();
            } else if (sm.action == SolverMove::Action::PairPyramid) {
                ok = nextBoard.executePyramidPair(sm.fromPile, sm.secondFromPile, sm.toPile);
            } else {
                ok = nextBoard.executeMove(sm.fromPile, sm.cardIndex, sm.toPile, true);
            }

            if (!ok) continue;

            uint64_t nextHash = computeBoardHash(nextBoard);
            if (!visited.insert(nextHash).second) {
                continue; // Already visited this state topology
            }

            auto child = std::make_shared<SearchNode>();
            child->id = static_cast<uint32_t>(allNodes.size());
            child->parentId = current->id;
            child->move = sm;
            child->depth = current->depth + 1;
            child->stockRecycles = nextRecycle;
            child->board = std::move(nextBoard);
            child->score = evaluateHeuristic(child->board);

            allNodes.push_back(child);
            openQueue.push(child);
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
