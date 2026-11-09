#include "solengine/auto_play_controller.hpp"
#include "solengine/audio_foley.hpp"
#include <chrono>

namespace solengine {

AutoPlayController::AutoPlayController() = default;

AutoPlayController::~AutoPlayController() {
    stop();
}

void AutoPlayController::start(SolitaireBoard& board) {
    stop();

    currentDealSeed_ = board.seed();
    cancelSignal_.store(false, std::memory_order_release);
    state_ = AutoPlayState::Solving;
    moves_.clear();
    currentMoveIdx_ = 0;
    moveTimer_ = 0.0f;
    hasResult_ = false;

    SolitaireBoard boardCopy = board;
    solveThread_ = std::thread([this, boardCopy]() {
        SolitaireSolver solver;
        SolveResult res = solver.solve(boardCopy, 25000, 2500.0f, &cancelSignal_);
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            asyncResult_ = std::move(res);
            hasResult_ = true;
        }
    });
}

void AutoPlayController::stop() {
    cancelSignal_.store(true, std::memory_order_release);
    if (solveThread_.joinable()) {
        solveThread_.join();
    }
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
    } else {
        ok = board.executeMove(m.fromPile, m.cardIndex, m.toPile, true);
    }

    if (ok) {
        ++currentMoveIdx_;
        if (foley) {
            foley->triggerSound(FoleySound::Deal, 0.78f);
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
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            if (hasResult_) {
                ready = true;
                res = std::move(asyncResult_);
                hasResult_ = false;
            }
        }
        if (ready) {
            if (solveThread_.joinable()) {
                solveThread_.join();
            }
            if (res.solved && !res.moves.empty()) {
                moves_ = std::move(res.moves);
                state_ = AutoPlayState::Playing;
                currentMoveIdx_ = 0;
                moveTimer_ = 0.0f;
                if (foley) {
                    foley->triggerSound(FoleySound::SpectreWhisper, 0.85f);
                }
            } else {
                state_ = AutoPlayState::Failed;
            }
        }
        return;
    }

    if (state_ == AutoPlayState::Playing) {
        moveTimer_ += dt;
        if (moveTimer_ >= moveInterval()) {
            moveTimer_ = 0.0f;
            stepForward(board, foley);
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
            return "Playing (" + std::string(speedLabel()) + ") - Move " +
                   std::to_string(currentMoveIdx_) + " / " + std::to_string(moves_.size());
        case AutoPlayState::Paused:
            return "Paused - Move " + std::to_string(currentMoveIdx_) + " / " + std::to_string(moves_.size());
        case AutoPlayState::Completed:
            return "Demo Complete (" + std::to_string(moves_.size()) + " moves)";
        case AutoPlayState::Failed:
            return "No solution found (Game may be unwinnable)";
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
