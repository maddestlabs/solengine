#ifndef SOLENGINE_SOLITAIRE_SOLVER_HPP
#define SOLENGINE_SOLITAIRE_SOLVER_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/solitaire_board.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>

namespace solengine {

struct SolverMove {
    enum class Action : uint8_t {
        MoveCards = 0,
        DrawStock = 1,
        PairPyramid = 2
    };
    Action action{Action::MoveCards};
    uint8_t fromPile{0};
    uint8_t cardIndex{0};
    uint8_t toPile{0};
    uint8_t secondFromPile{255}; // For Pyramid pairs
    std::string notation;        // e.g. "♠7 -> ♡8", "Draw Stock", "Pair K"
};

struct SolveResult {
    bool solved{false};
    bool timedOut{false};
    bool cancelled{false};
    uint32_t statesExplored{0};
    float solveTimeMs{0.0f};
    std::vector<SolverMove> moves;
};

/**
 * Universal Solitaire Solver for SolEngine.
 * High-performance heuristic graph search with Zobrist transposition caching,
 * cycle avoidance, Campbell/Horne safe-move pruning, and zero-allocation in-place DFS.
 */
class SolitaireSolver {
public:
    SolitaireSolver() = default;

    /**
     * Solves the given SolitaireBoard configuration.
     * Searches for a winning path of legal moves.
     */
    SolveResult solve(
        const SolitaireBoard& initialBoard,
        uint32_t maxStates = 120000,
        float timeoutMs = 2500.0f,
        std::atomic<bool>* cancelSignal = nullptr
    );

    /**
     * Fast deal solvability check for guaranteed-solvable deal generation.
     */
    bool isDealSolvable(
        uint32_t seed,
        GameVariant variant,
        float timeoutMs = 350.0f,
        std::atomic<bool>* cancelSignal = nullptr
    );

    bool isDealSolvable(
        uint32_t seed,
        const SolitaireGameDef& def,
        float timeoutMs = 350.0f,
        std::atomic<bool>* cancelSignal = nullptr
    );
};

// Formats card rank and suit into human-friendly unicode notation (e.g. "♠A", "♡10", "♢K")
std::string formatCardNotation(Suit suit, Rank rank);

} // namespace solengine

#endif // SOLENGINE_SOLITAIRE_SOLVER_HPP

