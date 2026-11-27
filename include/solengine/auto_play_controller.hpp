#ifndef SOLENGINE_AUTO_PLAY_CONTROLLER_HPP
#define SOLENGINE_AUTO_PLAY_CONTROLLER_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/solitaire_solver.hpp"
#include "solengine/replay_track.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>

namespace solengine {

class AudioFoleyEngine;

enum class AutoPlayState : uint8_t {
    Inactive = 0,
    Solving,
    Playing,
    Paused,
    WaitingForMove, // Guided tutorial step: board unlocked, waiting for player move
    Branched,       // Guided tutorial soft-branch: player exploring off-script with 1-click rewind
    Completed,
    Failed
};

enum class AutoPlaySpeed : uint8_t {
    Normal = 0, // ~1.50s per move (relaxed learner pace)
    Fast   = 1, // ~0.75s per move
    Hyper  = 2  // ~0.35s per move
};

/**
 * Hardwood Solitaire 3-Style Auto-Play Showcase & Bot Playback Engine.
 * Features asynchronous background solving, smooth card flight animations,
 * bidirectional step navigation (Rewind / Fast Forward), and instant interactive Take-Over.
 */
class AutoPlayController {
public:
    AutoPlayController();
    ~AutoPlayController();

    // Start auto-play demonstration on the current board
    void start(SolitaireBoard& board);
    void startDemo(SolitaireBoard& board);
    void startPreview(SolitaireBoard& board, uint32_t maxStates = 12000u, float maxTimeMs = 450.0f);
    void startCachedMoves(const std::vector<SolverMove>& moves, uint32_t dealSeed);

    // Start playback of an annotated replay or interactive guided tutorial track
    void startReplay(SolitaireBoard& board, const ReplayTrack& track, bool guidedOverride = false);

    // Stop auto-play and let the player immediately take over manual control
    void stop();

    // Guided Tutorial / Soft Branching hooks
    bool onPlayerMove(const SolverMove& performedMove, SolitaireBoard& board, AudioFoleyEngine* foley = nullptr);
    bool rewindToBranch(SolitaireBoard& board, AudioFoleyEngine* foley = nullptr);
    void toggleGuidedMode() noexcept;

    // Transport playback controls
    void togglePlayPause();
    void play();
    void pause();
    bool stepForward(SolitaireBoard& board, AudioFoleyEngine* foley = nullptr);
    bool stepBackward(SolitaireBoard& board, AudioFoleyEngine* foley = nullptr);
    void restart(SolitaireBoard& board, AudioFoleyEngine* foley = nullptr);
    void cycleSpeed();
    void setSpeed(AutoPlaySpeed s) noexcept { speed_ = s; }

    // Per-frame update called by the main render/input loop
    void update(float dt, SolitaireBoard& board, AudioFoleyEngine* foley = nullptr);

    [[nodiscard]] AutoPlayState state() const noexcept { return state_; }
    [[nodiscard]] bool isActive() const noexcept { return state_ != AutoPlayState::Inactive; }
    [[nodiscard]] bool isPlaying() const noexcept { return state_ == AutoPlayState::Playing; }
    [[nodiscard]] bool isPaused() const noexcept { return state_ == AutoPlayState::Paused; }
    [[nodiscard]] bool isSolving() const noexcept { return state_ == AutoPlayState::Solving; }
    [[nodiscard]] bool isWaitingForMove() const noexcept { return state_ == AutoPlayState::WaitingForMove; }
    [[nodiscard]] bool isBranched() const noexcept { return state_ == AutoPlayState::Branched; }
    [[nodiscard]] bool allowsPlayerBoardInput() const noexcept {
        return state_ == AutoPlayState::WaitingForMove || state_ == AutoPlayState::Branched;
    }
    [[nodiscard]] bool isReplayMode() const noexcept { return isReplayMode_; }
    [[nodiscard]] bool isGuidedMode() const noexcept { return guidedMode_; }
    [[nodiscard]] size_t branchMoveIndex() const noexcept { return branchMoveIdx_; }
    [[nodiscard]] size_t branchExtraMoves() const noexcept { return branchExtraMoves_; }
    [[nodiscard]] const ReplayNote* activeNote() const noexcept;
    [[nodiscard]] const std::string& nudgeText() const noexcept { return nudgeText_; }
    [[nodiscard]] const std::string& replayTitle() const noexcept { return replayTitle_; }
    [[nodiscard]] AutoPlaySpeed speed() const noexcept { return speed_; }

    [[nodiscard]] size_t currentMoveIndex() const noexcept { return currentMoveIdx_; }
    [[nodiscard]] size_t historyMoveCount() const noexcept { return historyCount_; }
    [[nodiscard]] size_t totalMoves() const noexcept { return moves_.size(); }
    [[nodiscard]] const std::vector<SolverMove>& moves() const noexcept { return moves_; }
    [[nodiscard]] float moveInterval() const noexcept;
    [[nodiscard]] float moveProgress() const noexcept {
        float iv = moveInterval();
        return (iv > 0.001f) ? (moveTimer_ / iv) : 0.0f;
    }
    [[nodiscard]] const SolverMove* peekNextMove() const noexcept {
        if ((state_ == AutoPlayState::Playing || state_ == AutoPlayState::WaitingForMove) &&
            currentMoveIdx_ < moves_.size()) {
            return &moves_[currentMoveIdx_];
        }
        return nullptr;
    }
    [[nodiscard]] const SolverMove* peekCurrentOrNextMove() const noexcept {
        if ((state_ == AutoPlayState::Playing || state_ == AutoPlayState::Paused ||
             state_ == AutoPlayState::WaitingForMove) &&
            currentMoveIdx_ < moves_.size()) {
            return &moves_[currentMoveIdx_];
        }
        return nullptr;
    }
    [[nodiscard]] bool showMoveTrails() const noexcept { return showMoveTrails_; }
    void setShowMoveTrails(bool show) noexcept { showMoveTrails_ = show; }
    [[nodiscard]] const char* speedLabel() const noexcept;

    // Rich status descriptions for HUD rendering
    [[nodiscard]] std::string getStatusText() const;
    [[nodiscard]] std::string getCurrentMoveDescription() const;

private:
    AutoPlayState state_{AutoPlayState::Inactive};
    AutoPlaySpeed speed_{AutoPlaySpeed::Normal};

    std::vector<SolverMove> moves_;
    size_t currentMoveIdx_{0};
    size_t historyCount_{0};
    std::unordered_map<size_t, TurnTransaction> historyTransactions_;
    float moveTimer_{0.0f};
    std::string failReason_;
    bool showMoveTrails_{true};
    bool allowPartialDemo_{true};
    bool demoFallbackToSeed_{false};
    bool isPartialDemo_{false};
    bool isLiveDemoMode_{false};

    // Annotated Replay & Guided Tutorial state
    bool isReplayMode_{false};
    bool guidedMode_{false};
    std::string replayTitle_;
    std::unordered_map<size_t, ReplayNote> replayNotes_;
    size_t pausedForNoteIdx_{static_cast<size_t>(-1)};
    size_t branchMoveIdx_{0};
    size_t branchExtraMoves_{0};
    std::string nudgeText_;
    float nudgeTimer_{0.0f};

    // Asynchronous background solve worker thread
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    std::thread solveThread_;
#endif
    std::atomic<bool> cancelSignal_{false};
    std::mutex resultMutex_;
    bool hasResult_{false};
    SolveResult asyncResult_;
    std::vector<SolverMove> pendingHistoryMoves_;
    std::unordered_map<size_t, TurnTransaction> pendingHistoryTxs_;
    uint32_t currentDealSeed_{0};
};

} // namespace solengine

#endif // SOLENGINE_AUTO_PLAY_CONTROLLER_HPP
