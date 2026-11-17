#ifndef SOLENGINE_AUTO_PLAY_CONTROLLER_HPP
#define SOLENGINE_AUTO_PLAY_CONTROLLER_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/solitaire_solver.hpp"
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

    // Stop auto-play and let the player immediately take over manual control
    void stop();

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
    [[nodiscard]] AutoPlaySpeed speed() const noexcept { return speed_; }

    [[nodiscard]] size_t currentMoveIndex() const noexcept { return currentMoveIdx_; }
    [[nodiscard]] size_t historyMoveCount() const noexcept { return historyCount_; }
    [[nodiscard]] size_t totalMoves() const noexcept { return moves_.size(); }
    [[nodiscard]] float moveInterval() const noexcept;
    [[nodiscard]] float moveProgress() const noexcept {
        float iv = moveInterval();
        return (iv > 0.001f) ? (moveTimer_ / iv) : 0.0f;
    }
    [[nodiscard]] const SolverMove* peekNextMove() const noexcept {
        if (state_ == AutoPlayState::Playing && currentMoveIdx_ < moves_.size()) {
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
