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

void AutoPlayController::startPreview(SolitaireBoard& board, uint32_t maxStates, float maxTimeMs) {
    stop();
    demoFallbackToSeed_ = false;
    allowPartialDemo_ = true;
    isReplayMode_ = false;
    guidedMode_ = false;
    replayTitle_.clear();
    replayNotes_.clear();
    pausedForNoteIdx_ = static_cast<size_t>(-1);
    branchMoveIdx_ = 0;
    branchExtraMoves_ = 0;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;

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
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    // Single-threaded WebAssembly: keep preview solve strictly bounded so UI stays responsive
    const uint32_t wasmMaxStates = std::min<uint32_t>(maxStates, 1800u);
    const float wasmMaxMs = std::min<float>(maxTimeMs, 55.0f);
    try {
        SolitaireSolver solver;
        SolveResult res = solver.solve(boardCopy, wasmMaxStates, wasmMaxMs, &cancelSignal_);
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            asyncResult_ = std::move(res);
            pendingHistoryMoves_.clear();
            pendingHistoryTxs_.clear();
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
        solveThread_ = std::thread([this, boardCopy, maxStates, maxTimeMs]() {
            try {
                SolitaireSolver solver;
                SolveResult res = solver.solve(boardCopy, maxStates, maxTimeMs, &cancelSignal_);
                {
                    std::lock_guard<std::mutex> lock(resultMutex_);
                    asyncResult_ = std::move(res);
                    pendingHistoryMoves_.clear();
                    pendingHistoryTxs_.clear();
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

void AutoPlayController::startCachedMoves(const std::vector<SolverMove>& moves, uint32_t dealSeed) {
    stop();
    demoFallbackToSeed_ = false;
    allowPartialDemo_ = true;
    isPartialDemo_ = false;
    isReplayMode_ = false;
    guidedMode_ = false;
    replayTitle_.clear();
    replayNotes_.clear();
    pausedForNoteIdx_ = static_cast<size_t>(-1);
    branchMoveIdx_ = 0;
    branchExtraMoves_ = 0;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;

    currentDealSeed_ = dealSeed;
    moves_ = moves;
    currentMoveIdx_ = 0;
    historyCount_ = 0;
    historyTransactions_.clear();
    pendingHistoryMoves_.clear();
    pendingHistoryTxs_.clear();
    moveTimer_ = 0.0f;
    failReason_.clear();
    hasResult_ = false;
    state_ = moves_.empty() ? AutoPlayState::Completed : AutoPlayState::Playing;
}

void AutoPlayController::start(SolitaireBoard& board) {
    stop();
    demoFallbackToSeed_ = false;
    allowPartialDemo_ = false;
    isReplayMode_ = false;
    guidedMode_ = false;
    replayTitle_.clear();
    replayNotes_.clear();
    pausedForNoteIdx_ = static_cast<size_t>(-1);
    branchMoveIdx_ = 0;
    branchExtraMoves_ = 0;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;

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

void AutoPlayController::startReplay(SolitaireBoard& board, const ReplayTrack& track, bool guidedOverride) {
    stop();
    demoFallbackToSeed_ = false;
    allowPartialDemo_ = false;
    isPartialDemo_ = false;
    isReplayMode_ = true;
    guidedMode_ = guidedOverride || track.isGuided();
    replayTitle_ = track.title;
    currentDealSeed_ = board.seed();
    moves_ = track.moves;
    currentMoveIdx_ = 0;
    historyCount_ = 0;
    historyTransactions_.clear();
    pendingHistoryMoves_.clear();
    pendingHistoryTxs_.clear();
    replayNotes_.clear();
    for (const auto& n : track.notes) {
        replayNotes_[static_cast<size_t>(n.moveIndex)] = n;
    }
    pausedForNoteIdx_ = static_cast<size_t>(-1);
    branchMoveIdx_ = 0;
    branchExtraMoves_ = 0;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;
    moveTimer_ = 0.0f;
    failReason_.clear();
    hasResult_ = false;

    if (moves_.empty()) {
        state_ = AutoPlayState::Completed;
        return;
    }

    if (moves_[0].notation.empty()) {
        moves_[0].notation = buildMoveNotation(board, moves_[0]);
    }

    const ReplayNote* firstNote = activeNote();
    if (guidedMode_ || (firstNote && (firstNote->flags & ReplayNote_GuidedStep) != 0)) {
        state_ = AutoPlayState::WaitingForMove;
    } else if (firstNote && (firstNote->flags & ReplayNote_PauseBeforeMove) != 0) {
        pausedForNoteIdx_ = 0;
        state_ = AutoPlayState::Paused;
    } else {
        state_ = AutoPlayState::Playing;
    }
}

void AutoPlayController::stop() {
    cancelSignal_.store(true, std::memory_order_release);
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    if (solveThread_.joinable()) {
        solveThread_.join();
    }
#endif
    state_ = AutoPlayState::Inactive;
    isReplayMode_ = false;
    guidedMode_ = false;
    replayTitle_.clear();
    replayNotes_.clear();
    pausedForNoteIdx_ = static_cast<size_t>(-1);
    branchMoveIdx_ = 0;
    branchExtraMoves_ = 0;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;
}

const ReplayNote* AutoPlayController::activeNote() const noexcept {
    if (!isReplayMode_) return nullptr;
    if (state_ != AutoPlayState::Playing && state_ != AutoPlayState::Paused &&
        state_ != AutoPlayState::WaitingForMove && state_ != AutoPlayState::Completed) {
        return nullptr;
    }
    auto it = replayNotes_.find(currentMoveIdx_);
    return (it != replayNotes_.end()) ? &it->second : nullptr;
}

void AutoPlayController::toggleGuidedMode() noexcept {
    if (!isReplayMode_) return;
    guidedMode_ = !guidedMode_;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;
    if (guidedMode_ && (state_ == AutoPlayState::Playing || state_ == AutoPlayState::Paused) &&
        currentMoveIdx_ < moves_.size()) {
        state_ = AutoPlayState::WaitingForMove;
    } else if (!guidedMode_ && state_ == AutoPlayState::WaitingForMove) {
        state_ = AutoPlayState::Paused;
    }
}

bool AutoPlayController::onPlayerMove(
    const SolverMove& performedMove,
    SolitaireBoard& board,
    AudioFoleyEngine* foley
) {
    if (state_ == AutoPlayState::Branched) {
        ++branchExtraMoves_;
        return true;
    }
    if (state_ != AutoPlayState::WaitingForMove || currentMoveIdx_ >= moves_.size()) {
        return false;
    }

    const auto& expected = moves_[currentMoveIdx_];
    bool matched = false;
    if (expected.action == SolverMove::Action::DrawStock) {
        matched = (performedMove.action == SolverMove::Action::DrawStock);
    } else if (expected.action == SolverMove::Action::PairPyramid) {
        matched = (performedMove.action == SolverMove::Action::PairPyramid) &&
                  ((performedMove.fromPile == expected.fromPile && performedMove.secondFromPile == expected.secondFromPile) ||
                   (performedMove.fromPile == expected.secondFromPile && performedMove.secondFromPile == expected.fromPile));
    } else if (expected.action == SolverMove::Action::ClickCard) {
        matched = (performedMove.fromPile == expected.fromPile && performedMove.cardIndex == expected.cardIndex);
    } else {
        matched = (performedMove.action == SolverMove::Action::MoveCards &&
                   performedMove.fromPile == expected.fromPile &&
                   performedMove.toPile == expected.toPile &&
                   performedMove.cardIndex == expected.cardIndex);
    }

    if (matched) {
        ++currentMoveIdx_;
        pausedForNoteIdx_ = static_cast<size_t>(-1);
        nudgeText_.clear();
        nudgeTimer_ = 0.0f;
        moveTimer_ = 0.0f;

        if (currentMoveIdx_ >= moves_.size() || board.isVictory()) {
            state_ = AutoPlayState::Completed;
            if (foley && board.isVictory()) {
                foley->triggerSound(FoleySound::FoundationChime, 0.9f);
            }
            return true;
        }

        if (moves_[currentMoveIdx_].notation.empty()) {
            moves_[currentMoveIdx_].notation = buildMoveNotation(board, moves_[currentMoveIdx_]);
        }

        const ReplayNote* nextNote = activeNote();
        if (guidedMode_ || (nextNote && (nextNote->flags & ReplayNote_GuidedStep) != 0)) {
            state_ = AutoPlayState::WaitingForMove;
        } else if (nextNote && (nextNote->flags & ReplayNote_PauseBeforeMove) != 0) {
            pausedForNoteIdx_ = currentMoveIdx_;
            state_ = AutoPlayState::Paused;
        } else {
            state_ = AutoPlayState::Playing;
        }
        return true;
    }

    // Off-script move during a guided step: either strict snap-back or soft-branch
    const ReplayNote* note = activeNote();
    const bool strict = (note != nullptr) && ((note->flags & ReplayNote_StrictStep) != 0);
    if (strict) {
        board.undo();
        std::string desc = expected.notation.empty() ? buildMoveNotation(board, expected) : expected.notation;
        nudgeText_ = "Try " + desc + " (or click Stop for Free Play)";
        nudgeTimer_ = 3.5f;
        if (foley) {
            foley->triggerSound(FoleySound::WoodClick, 0.75f);
        }
        return false;
    }

    // Soft branch: let the player explore freely with 1-click Rewind to Lesson
    branchMoveIdx_ = currentMoveIdx_;
    branchExtraMoves_ = 1;
    state_ = AutoPlayState::Branched;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;
    return true;
}

bool AutoPlayController::rewindToBranch(SolitaireBoard& board, AudioFoleyEngine* foley) {
    if (state_ != AutoPlayState::Branched) return false;
    while (branchExtraMoves_ > 0) {
        if (!board.undo()) break;
        --branchExtraMoves_;
    }
    currentMoveIdx_ = branchMoveIdx_;
    state_ = AutoPlayState::WaitingForMove;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;
    moveTimer_ = 0.0f;
    if (foley) {
        foley->triggerSound(FoleySound::Flip, 0.8f);
    }
    return true;
}

void AutoPlayController::togglePlayPause() {
    if (state_ == AutoPlayState::Playing) {
        pause();
    } else if (state_ == AutoPlayState::Paused || state_ == AutoPlayState::WaitingForMove) {
        play();
    }
}

void AutoPlayController::play() {
    if (state_ == AutoPlayState::Paused || state_ == AutoPlayState::WaitingForMove ||
        state_ == AutoPlayState::Completed) {
        if (currentMoveIdx_ >= moves_.size()) {
            currentMoveIdx_ = 0;
        }
        pausedForNoteIdx_ = currentMoveIdx_;
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
        nudgeText_.clear();
        nudgeTimer_ = 0.0f;
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
        } else if (isReplayMode_) {
            if (moves_[currentMoveIdx_].notation.empty()) {
                moves_[currentMoveIdx_].notation = buildMoveNotation(board, moves_[currentMoveIdx_]);
            }
            const ReplayNote* nextNote = activeNote();
            if (guidedMode_ || (nextNote && (nextNote->flags & ReplayNote_GuidedStep) != 0)) {
                state_ = AutoPlayState::WaitingForMove;
            } else if (state_ == AutoPlayState::Playing &&
                       nextNote && (nextNote->flags & ReplayNote_PauseBeforeMove) != 0 &&
                       pausedForNoteIdx_ != currentMoveIdx_) {
                pausedForNoteIdx_ = currentMoveIdx_;
                state_ = AutoPlayState::Paused;
            }
        }
        return true;
    }

    return false;
}

bool AutoPlayController::stepBackward(SolitaireBoard& board, AudioFoleyEngine* foley) {
    if (state_ == AutoPlayState::Branched) {
        return rewindToBranch(board, foley);
    }
    if (currentMoveIdx_ == 0) {
        return false;
    }

    if (board.undo()) {
        --currentMoveIdx_;
        pausedForNoteIdx_ = static_cast<size_t>(-1);
        nudgeText_.clear();
        nudgeTimer_ = 0.0f;
        state_ = (isReplayMode_ && guidedMode_) ? AutoPlayState::WaitingForMove : AutoPlayState::Paused;
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
    pausedForNoteIdx_ = static_cast<size_t>(-1);
    branchMoveIdx_ = 0;
    branchExtraMoves_ = 0;
    nudgeText_.clear();
    nudgeTimer_ = 0.0f;
    state_ = (isReplayMode_ && guidedMode_) ? AutoPlayState::WaitingForMove : AutoPlayState::Paused;
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

    if (nudgeTimer_ > 0.0f) {
        nudgeTimer_ -= dt;
        if (nudgeTimer_ <= 0.0f) {
            nudgeTimer_ = 0.0f;
            nudgeText_.clear();
        }
    }

    if (state_ == AutoPlayState::Playing) {
        if (isReplayMode_ && currentMoveIdx_ < moves_.size()) {
            const ReplayNote* note = activeNote();
            if (note && (note->flags & ReplayNote_GuidedStep) != 0) {
                state_ = AutoPlayState::WaitingForMove;
                return;
            }
            if (note && (note->flags & ReplayNote_PauseBeforeMove) != 0 &&
                pausedForNoteIdx_ != currentMoveIdx_) {
                pausedForNoteIdx_ = currentMoveIdx_;
                state_ = AutoPlayState::Paused;
                return;
            }
        }
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
    const std::string prefix = !replayTitle_.empty() ? (replayTitle_ + " | ") : "";
    switch (state_) {
        case AutoPlayState::Inactive:
            return "Inactive";
        case AutoPlayState::Solving:
            return "Oracle Analyzing Game Tree...";
        case AutoPlayState::Playing:
            return prefix + std::string(isReplayMode_ ? "Replay (" : (isPartialDemo_ ? "Demoing Best Moves (" : "Playing (")) +
                   std::string(speedLabel()) + ") - Move " +
                   std::to_string(currentMoveIdx_) + " / " + std::to_string(moves_.size());
        case AutoPlayState::Paused:
            return prefix + std::string(isReplayMode_ ? "Replay Paused - Move " : "Paused - Move ") +
                   std::to_string(currentMoveIdx_) + " / " + std::to_string(moves_.size());
        case AutoPlayState::WaitingForMove:
            return prefix + "Your Turn - Step " +
                   std::to_string(currentMoveIdx_ + 1) + " / " + std::to_string(moves_.size());
        case AutoPlayState::Branched:
            return prefix + "Free Play Branch (+" + std::to_string(branchExtraMoves_) +
                   " moves from Step " + std::to_string(branchMoveIdx_ + 1) + ")";
        case AutoPlayState::Completed:
            return prefix + std::string(isReplayMode_ ? "Replay Complete (" : "Demo Complete (") +
                   std::to_string(moves_.size()) + " moves)";
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
