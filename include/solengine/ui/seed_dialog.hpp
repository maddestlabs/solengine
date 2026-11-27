#ifndef SOLENGINE_SEED_DIALOG_HPP
#define SOLENGINE_SEED_DIALOG_HPP

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "solengine/replay_track.hpp"
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace solengine {

class SolitaireBoard;
class AudioFoleyEngine;
struct ThemePalette;

struct SeedHistoryEntry {
    uint32_t seed{0u};
    uint32_t layoutSeed{0u};
    std::string gameId;
    std::string gameName;
};

class SeedDialog {
public:
    SeedDialog();
    ~SeedDialog() = default;

    void open(uint32_t currentSeed, const std::string& variantName, const std::string& gameId = "",
              uint32_t currentLayoutSeed = 0u, bool supportsLayoutLock = false);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    /// Record a generated or dealt seed into the in-memory session history (most recent first).
    void recordHistory(uint32_t seed, const std::string& gameId, const std::string& gameName, uint32_t layoutSeed = 0u);
    [[nodiscard]] const std::vector<SeedHistoryEntry>& history() const noexcept { return history_; }

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal,
        float timeSec,
        void* nativeWindow = nullptr
    );

    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY,
        SolitaireBoard& board,
        AudioFoleyEngine& foley,
        void* nativeWindow = nullptr
    );

    bool handleMouseMove(float mouseX, float mouseY);
    bool handleScroll(float yoffset);

    bool handleKey(
        int key,
        int action,
        int mods,
        SolitaireBoard& board,
        AudioFoleyEngine& foley,
        void* nativeWindow = nullptr
    );

    bool handleChar(unsigned int codepoint);

    std::function<void(uint32_t newSeed)> onDealSeed;
    std::function<void(uint32_t newSeed, const std::string& gameId)> onReplayHistory;
    std::function<void(const ReplayTrack& track, bool guided)> onLaunchReplay;

private:
    static constexpr size_t kMaxHistory = 64;

    void randomizeSeed();
    [[nodiscard]] std::string formatSeedText() const;
    static bool parseSeedText(const std::string& text, uint32_t& outSeed, uint32_t& outLayoutSeed);
    void copyTextToClipboard(const std::string& text, void* nativeWindow);
    void copyToClipboard(void* nativeWindow);
    void copyReplayToClipboard(const SolitaireBoard& board, void* nativeWindow);
    bool pasteFromClipboard(void* nativeWindow, SolitaireBoard& board, AudioFoleyEngine& foley);
    void commitDeal(SolitaireBoard& board, AudioFoleyEngine& foley);
    void commitHistoryReplay(uint32_t seed, uint32_t layoutSeed, const std::string& gameId, const std::string& gameName,
                             SolitaireBoard& board, AudioFoleyEngine& foley);
    [[nodiscard]] int computeVisibleHistoryRows(float screenH) const noexcept;
    void clampHistoryScroll() noexcept;

    bool isOpen_{false};
    uint32_t seed_{2026u};
    uint32_t layoutSeed_{0u};
    bool supportsLayoutLock_{false};
    std::string seedText_{"2026"};
    std::string variantName_{"Klondike"};
    std::string targetGameId_;
    uint32_t activeBoardSeed_{2026u};
    std::string activeBoardGameId_;
    bool editingSeed_{true};
    float cursorBlink_{0.0f};
    float copyFeedbackTimer_{0.0f};
    float replayCopyFeedbackTimer_{0.0f};

    std::vector<SeedHistoryEntry> history_;
    int historyScrollOffset_{0};
    int visibleHistoryRows_{0};

    // Dialog layout bounds
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{480.0f};
    float dialogH_{290.0f};
};

} // namespace solengine

#endif // SOLENGINE_SEED_DIALOG_HPP

