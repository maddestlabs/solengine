#ifndef SOLENGINE_REPLAY_TRACK_HPP
#define SOLENGINE_REPLAY_TRACK_HPP

#include "solengine/solitaire_solver.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace solengine {

class SolitaireBoard;

enum ReplayNoteFlags : uint8_t {
    ReplayNote_None            = 0,
    ReplayNote_PauseBeforeMove = 1 << 0, // Pause playback right before executing moveIndex
    ReplayNote_HighlightMove   = 1 << 1, // Spotlight & draw move trail for this step
    ReplayNote_GuidedStep      = 1 << 2, // Wait for the user to perform this move
    ReplayNote_StrictStep      = 1 << 3, // Snap back on off-script move instead of soft-branching
    ReplayNote_HasHighlights   = 1 << 4  // Note carries explicit ReplayHighlight regions
};

enum ReplayTrackFlags : uint8_t {
    ReplayTrack_None   = 0,
    ReplayTrack_Guided = 1 << 0 // Launch playback in interactive Guided Tutorial mode
};

enum class HighlightKind : uint8_t {
    Pile      = 0, // Single pile (pileIdx)
    ZoneGroup = 1, // All piles sharing pileIdx's PileType (e.g., all Foundations / Tableau)
    CardSpan  = 2, // Card or contiguous range inside pileIdx (startCard, count; 0 = through end)
    PileSpan  = 3  // Contiguous range of piles [pileIdx .. pileIdx + count - 1]
};

enum class HighlightStyle : uint8_t {
    Primary = 0, // Theme gold / amber
    Accent  = 1, // Luminous teal / cyan (secondary contrast, e.g., sandwiched cards)
    Caution = 2  // Warm crimson / coral (blocked or warning highlight)
};

struct ReplayHighlight {
    HighlightKind kind{HighlightKind::CardSpan};
    uint8_t pileIdx{0};
    uint8_t startCard{0};
    uint8_t count{1};
    HighlightStyle style{HighlightStyle::Primary};
};

struct ReplayNote {
    uint16_t moveIndex{0};
    uint8_t flags{ReplayNote_HighlightMove};
    std::string text;
    std::vector<ReplayHighlight> highlights;
};

/**
 * Compact annotated gameplay & tutorial track.
 * Encodes (gameId, seed, bit-packed moves, sparse UTF-8 commentary notes + highlights)
 * into a short URL-safe Base64 string ("sol1:...") that replays identically across all platforms.
 */
struct ReplayTrack {
    static constexpr uint8_t kFormatVersion = 1;
    static constexpr size_t kMaxMoves = 2048;
    static constexpr size_t kMaxNotes = 128;
    static constexpr size_t kMaxNoteTextLen = 280;
    static constexpr size_t kMaxHighlightsPerNote = 16;
    static constexpr size_t kMaxTitleLen = 64;
    static constexpr size_t kMaxGameIdLen = 64;

    uint8_t version{kFormatVersion};
    uint8_t flags{ReplayTrack_None};
    std::string gameId;
    uint32_t seed{2026u};
    std::string title;
    std::vector<SolverMove> moves;
    std::vector<ReplayNote> notes;

    [[nodiscard]] const ReplayNote* findNote(size_t moveIndex) const noexcept;
    void setNote(size_t moveIndex, std::string text,
                 uint8_t noteFlags = ReplayNote_PauseBeforeMove | ReplayNote_HighlightMove,
                 std::vector<ReplayHighlight> highlights = {});
    void addHighlight(size_t moveIndex, ReplayHighlight highlight);
    [[nodiscard]] bool isGuided() const noexcept { return (flags & ReplayTrack_Guided) != 0; }
};

class ReplayCodec {
public:
    /// Extract the clean forward move sequence from a board's active undo history (undone moves excluded).
    static ReplayTrack extractFromBoard(
        const SolitaireBoard& board,
        const std::string& gameId,
        const std::string& title = ""
    );

    /// Encode a ReplayTrack into a compact URL-safe "sol1:..." string.
    static std::string encode(const ReplayTrack& track);

    /// Decode a "sol1:..." string (or a URL containing "#r=sol1:...") back into a ReplayTrack.
    static bool decode(std::string_view code, ReplayTrack& outTrack, std::string* outError = nullptr);

    /// Returns true if the clipboard/input text appears to be a SolEngine replay code.
    [[nodiscard]] static bool looksLikeReplayCode(std::string_view text) noexcept;
};

} // namespace solengine

#endif // SOLENGINE_REPLAY_TRACK_HPP
