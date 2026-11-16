#include "solengine/auto_play_controller.hpp"
#include "solengine/audio_foley.hpp"
#include <algorithm>
#include <chrono>
#include <optional>

namespace solengine {

namespace {

void extractBoardHistory(
    SolitaireBoard boardCopy,
    std::vector<SolverMove>& outMoves,
    std::unordered_map<size_t, TurnTransaction>& outTxs
) {
    outMoves.clear();
    outTxs.clear();

    std::vector<SolverMove> reversedMoves;
    std::vector<std::optional<TurnTransaction>> reversedTxs;

    while (!boardCopy.txUndoStack().empty() || !boardCopy.undoStack().empty()) {
        if (!boardCopy.txUndoStack().empty()) {
            TurnTransaction tx = boardCopy.txUndoStack().back();
            if (!boardCopy.undo()) break;

            SolverMove mv{};
            mv.action = SolverMove::Action::ReplayTransaction;
            mv.notation = "History Move";
            for (const auto& op : tx.ops) {
                if (op.type == BoardOpType::MoveCards) {
                    const auto& piles = boardCopy.piles();
                    if (op.fromPile < piles.size() && op.cardIdx < piles[op.fromPile].cards.size()) {
                        const auto& c = piles[op.fromPile].cards[op.cardIdx];
                        mv.fromPile = op.fromPile;
                        mv.toPile = op.toPile;
                        mv.cardIndex = static_cast<uint8_t>(std::min<uint32_t>(op.cardIdx, 255u));
                        mv.notation = formatCardNotation(c.suit, c.rank);
                    }
                    break;
                }
            }
            reversedMoves.push_back(std::move(mv));
            reversedTxs.push_back(std::move(tx));
        } else {
            MoveCommand cmd = boardCopy.undoStack().back();
            if (!boardCopy.undo()) break;

            SolverMove mv{};
            const auto& piles = boardCopy.piles();
            if (cmd.spiderStockDeal || cmd.recycledWaste ||
                (cmd.fromPile == 0 && cmd.toPile == 1 && !piles.empty() && piles[0].type == PileType::Stock)) {
                mv.action = SolverMove::Action::DrawStock;
                mv.fromPile = cmd.fromPile;
                mv.toPile = cmd.toPile;
            } else if (cmd.isPairMove && cmd.secondFromPile < piles.size()) {
                mv.action = SolverMove::Action::PairPyramid;
                mv.fromPile = cmd.fromPile;
                mv.secondFromPile = cmd.secondFromPile;
                mv.toPile = cmd.toPile;
                if (!piles[cmd.fromPile].cards.empty()) {
                    mv.cardIndex = static_cast<uint8_t>(piles[cmd.fromPile].cards.size() - 1);
                }
            } else {
                mv.action = SolverMove::Action::MoveCards;
                mv.fromPile = cmd.fromPile;
                mv.toPile = cmd.toPile;
                if (cmd.fromPile < piles.size() && piles[cmd.fromPile].cards.size() >= cmd.cardCount) {
                    mv.cardIndex = static_cast<uint8_t>(piles[cmd.fromPile].cards.size() - cmd.cardCount);
                }
            }
            mv.notation = buildMoveNotation(boardCopy, mv);
            reversedMoves.push_back(std::move(mv));
            reversedTxs.push_back(std::nullopt);
        }
    }

    const size_t total = reversedMoves.size();
    outMoves.reserve(total);
    for (size_t i = total; i > 0; --i) {
        const size_t srcIdx = i - 1;
        const size_t dstIdx = outMoves.size();
        outMoves.push_back(std::move(reversedMoves[srcIdx]));
        if (reversedTxs[srcIdx].has_value()) {
            outTxs.emplace(dstIdx, std::move(*reversedTxs[srcIdx]));
        }
    }
}

} // namespace

AutoPlayController::AutoPlayController() = default;

AutoPlayController::~AutoPlayController() {
    stop();
}

void AutoPlayController::startDemo(SolitaireBoard& board) {
    start(board);
    demoFallbackToSeed_ = true;
    allowPartialDemo_ = true;
}

void AutoPlayController::start(SolitaireBoard& board) {
    stop();
    demoFallbackToSeed_ = false;
    allowPartialDemo_ = false;

    currentDealSeed_ = board.seed();
    cancelSignal_.store(false, std::memory_order_release);
    state_ = AutoPlayState::Solving;
    moves_.clear();
    currentMoveIdx_ = 0;
    historyCount_ = 0;
    historyTransactions_.clear();
    pendingHistoryMoves_.clear();
    pendingHistoryTxs_.clear();
    moveTimer_ = 0.0f;
    failReason_.clear();
    hasResult_ = false;
    isPartialDemo_ = false;

    SolitaireBoard boardCopy = board;
    const uint32_t solverMaxStates = (boardCopy.activeGameDef().decks >= 2) ? 65000u : 25000u;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    try {
        SolitaireSolver solver;
        // On single-threaded WebAssembly, solve synchronously with a bounded search budget
        SolveResult res = solver.solve(boardCopy, solverMaxStates, 2500.0f, &cancelSignal_);
        std::vector<SolverMove> histMoves;
        std::unordered_map<size_t, TurnTransaction> histTxs;
        if (!cancelSignal_.load(std::memory_order_acquire)) {
            extractBoardHistory(boardCopy, histMoves, histTxs);
        }
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            asyncResult_ = std::move(res);
            pendingHistoryMoves_ = std::move(histMoves);
            pendingHistoryTxs_ = std::move(histTxs);
            hasResult_ = true;
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(resultMutex_);
        asyncResult_ = SolveResult{};
        pendingHistoryMoves_.clear();
        pendingHistoryTxs_.clear();
        hasResult_ = true;
    }
#else
    try {
        solveThread_ = std::thread([this, boardCopy, solverMaxStates]() {
            try {
                SolitaireSolver solver;
                SolveResult res = solver.solve(boardCopy, solverMaxStates, 2500.0f, &cancelSignal_);
                std::vector<SolverMove> histMoves;
                std::unordered_map<size_t, TurnTransaction> histTxs;
                if (!cancelSignal_.load(std::memory_order_acquire)) {
                    extractBoardHistory(boardCopy, histMoves, histTxs);
                }
                {
                    std::lock_guard<std::mutex> lock(resultMutex_);
                    asyncResult_ = std::move(res);
                    pendingHistoryMoves_ = std::move(histMoves);
                    pendingHistoryTxs_ = std::move(histTxs);
                    hasResult_ = true;
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(resultMutex_);
                asyncResult_ = SolveResult{};
                pendingHistoryMoves_.clear();
                pendingHistoryTxs_.clear();
                hasResult_ = true;
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(resultMutex_);
        asyncResult_ = SolveResult{};
        pendingHistoryMoves_.clear();
        pendingHistoryTxs_.clear();
        hasResult_ = true;
    }
#endif
}

void AutoPlayController::stop() {
    cancelSignal_.store(true, std::memory_order_release);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    if (solveThread_.joinable()) {
        solveThread_.join();
    }
#endif
    state_ = AutoPlayState::Inactive;
}

void AutoPlayController::togglePlayPause() {
    if (state_ == AutoPlayState::Playing) {
        pause();
    } else if (state_ == AutoPlayState::Paused) {
        play();
    }
}

void AutoPlayController::play() {
    if (state_ == AutoPlayState::Paused || state_ == AutoPlayState::Completed) {
        if (currentMoveIdx_ >= moves_.size()) {
            currentMoveIdx_ = 0;
        }
        state_ = AutoPlayState::Playing;
        moveTimer_ = 0.0f;
    }
}

void AutoPlayController::pause() {
    if (state_ == AutoPlayState::Playing) {
        state_ = AutoPlayState::Paused;
    }
}

bool AutoPlayController::stepForward(SolitaireBoard& board, AudioFoleyEngine* foley) {
    if (currentMoveIdx_ >= moves_.size()) {
        state_ = AutoPlayState::Completed;
        return false;
    }

    const auto& m = moves_[currentMoveIdx_];
    bool ok = false;

    if (m.action == SolverMove::Action::DrawStock) {
        ok = board.drawFromStock();
    } else if (m.action == SolverMove::Action::PairPyramid) {
        ok = board.executePyramidPair(m.fromPile, m.secondFromPile, m.toPile);
    } else if (m.action == SolverMove::Action::ClickCard) {
        ok = board.smartMove(m.fromPile, m.cardIndex);
    } else if (m.action == SolverMove::Action::ReplayTransaction) {
        auto it = historyTransactions_.find(currentMoveIdx_);
        if (it != historyTransactions_.end()) {
            ok = board.applyTransaction(it->second);
        }
    } else {
        ok = board.executeMove(m.fromPile, m.cardIndex, m.toPile, true);
    }

    if (ok) {
        ++currentMoveIdx_;
        if (foley) {
            float pan = 0.0f;
            if (static_cast<size_t>(m.toPile) < board.piles().size() && board.cardWidth() > 0.0f) {
                const auto& dstPile = board.piles()[static_cast<size_t>(m.toPile)];
                float boardW = std::max(600.0f, board.cardWidth() * 10.0f);
                pan = std::clamp((dstPile.x + board.cardWidth() * 0.5f) / boardW * 2.0f - 1.0f, -0.75f, 0.75f);
            }
            foley->triggerSound(FoleySound::Deal, 0.78f, pan);
        }
        if (currentMoveIdx_ >= moves_.size() || board.isVictory()) {
            state_ = AutoPlayState::Completed;
            if (foley && board.isVictory()) {
                foley->triggerSound(FoleySound::FoundationChime, 0.9f);
            }
        }
        return true;
    }

    return false;
}

bool AutoPlayController::stepBackward(SolitaireBoard& board, AudioFoleyEngine* foley) {
    if (currentMoveIdx_ == 0) {
        return false;
    }

    if (board.undo()) {
        --currentMoveIdx_;
        state_ = AutoPlayState::Paused;
        if (foley) {
            foley->triggerSound(FoleySound::Flip, 0.72f);
        }
        return true;
    }

    return false;
}

void AutoPlayController::restart(SolitaireBoard& board, AudioFoleyEngine* foley) {
    board.restartDeal();
    currentMoveIdx_ = 0;
    moveTimer_ = 0.0f;
    state_ = AutoPlayState::Paused;
    if (foley) {
        foley->triggerSound(FoleySound::Shuffle, 0.85f);
    }
}

void AutoPlayController::cycleSpeed() {
    switch (speed_) {
        case AutoPlaySpeed::Normal: speed_ = AutoPlaySpeed::Fast; break;
        case AutoPlaySpeed::Fast:   speed_ = AutoPlaySpeed::Hyper; break;
        case AutoPlaySpeed::Hyper:  speed_ = AutoPlaySpeed::Normal; break;
    }
}

float AutoPlayController::moveInterval() const noexcept {
    switch (speed_) {
        case AutoPlaySpeed::Normal: return 1.50f;
        case AutoPlaySpeed::Fast:   return 0.75f;
        case AutoPlaySpeed::Hyper:  return 0.35f;
    }
    return 1.50f;
}

const char* AutoPlayController::speedLabel() const noexcept {
    switch (speed_) {
        case AutoPlaySpeed::Normal: return "1x";
        case AutoPlaySpeed::Fast:   return "2x";
        case AutoPlaySpeed::Hyper:  return "4x";
    }
    return "1x";
}

void AutoPlayController::update(float dt, SolitaireBoard& board, AudioFoleyEngine* foley) {
    if (state_ == AutoPlayState::Solving) {
        bool ready = false;
        SolveResult res;
        std::vector<SolverMove> histMoves;
        std::unordered_map<size_t, TurnTransaction> histTxs;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            if (hasResult_) {
                ready = true;
                res = std::move(asyncResult_);
                histMoves = std::move(pendingHistoryMoves_);
                histTxs = std::move(pendingHistoryTxs_);
                hasResult_ = false;
            }
        }
        if (ready) {
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
            if (solveThread_.joinable()) {
                solveThread_.join();
            }
#endif
            uint32_t fallbackDemoSeed = 0;
            if (demoFallbackToSeed_) {
                const auto& def = board.activeGameDef();
                if (!def.demoSeeds.empty()) {
                    fallbackDemoSeed = def.demoSeeds.front();
                    for (uint32_t cand : def.demoSeeds) {
                        if (cand != 0 && cand != board.seed()) {
                            fallbackDemoSeed = cand;
                            break;
                        }
                    }
                } else if (def.demoSeed != 0) {
                    fallbackDemoSeed = def.demoSeed;
                }
            }

            auto loadCombinedMoves = [&](bool partialDemo, AutoPlayState nextState) {
                historyCount_ = histMoves.size();
                moves_ = std::move(histMoves);
                moves_.insert(moves_.end(),
                              std::make_move_iterator(res.moves.begin()),
                              std::make_move_iterator(res.moves.end()));
                historyTransactions_ = std::move(histTxs);
                isPartialDemo_ = partialDemo;
                state_ = nextState;
                currentMoveIdx_ = historyCount_;
                moveTimer_ = 0.0f;
            };

            if (res.solved && !res.moves.empty()) {
                loadCombinedMoves(false, AutoPlayState::Playing);
                if (foley) {
                    foley->triggerSound(FoleySound::Deal, 0.75f);
                }
            } else if (res.solved && res.moves.empty() && !histMoves.empty() && board.isVictory()) {
                loadCombinedMoves(false, AutoPlayState::Completed);
            } else if (demoFallbackToSeed_ && fallbackDemoSeed != 0 &&
                       board.seed() != fallbackDemoSeed) {
                board.newDeal(fallbackDemoSeed, board.activeGameDef());
                start(board);
                allowPartialDemo_ = true;
                return;
            } else if (allowPartialDemo_ && res.isPartialDemo && !res.moves.empty()) {
                loadCombinedMoves(true, AutoPlayState::Playing);
                if (foley) {
                    foley->triggerSound(FoleySound::Deal, 0.75f);
                }
            } else {
                demoFallbackToSeed_ = false;
                state_ = AutoPlayState::Failed;
            }
        }
        return;
    }

    if (state_ == AutoPlayState::Playing) {
        moveTimer_ += dt;
        if (moveTimer_ >= moveInterval()) {
            moveTimer_ = 0.0f;
            if (!stepForward(board, foley) && state_ == AutoPlayState::Playing) {
                if (currentMoveIdx_ < moves_.size()) {
                    const auto& m = moves_[currentMoveIdx_];
                    failReason_ = "Move " + std::to_string(currentMoveIdx_ + 1) + "/" + std::to_string(moves_.size()) +
                                  " rejected (" + std::to_string(m.fromPile) + "[" + std::to_string(m.cardIndex) + "]->" +
                                  std::to_string(m.toPile) + " " + m.notation + ")";
                }
                state_ = AutoPlayState::Failed;
            }
        }
    }
}

std::string AutoPlayController::getStatusText() const {
    switch (state_) {
        case AutoPlayState::Inactive:
            return "Inactive";
        case AutoPlayState::Solving:
            return "Oracle Analyzing Game Tree...";
        case AutoPlayState::Playing:
            return std::string(isPartialDemo_ ? "Demoing Best Moves (" : "Playing (") +
                   std::string(speedLabel()) + ") - Move " +
                   std::to_string(currentMoveIdx_) + " / " + std::to_string(moves_.size());
        case AutoPlayState::Paused:
            return "Paused - Move " + std::to_string(currentMoveIdx_) + " / " + std::to_string(moves_.size());
        case AutoPlayState::Completed:
            return "Demo Complete (" + std::to_string(moves_.size()) + " moves)";
        case AutoPlayState::Failed:
            return failReason_.empty() ? "No solution found (Game may be unwinnable)" : failReason_;
    }
    return "";
}

std::string AutoPlayController::getCurrentMoveDescription() const {
    if (currentMoveIdx_ < moves_.size()) {
        return moves_[currentMoveIdx_].notation;
    }
    return "";
}

} // namespace solengine
