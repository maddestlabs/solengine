#ifndef SOLENGINE_GAME_DEFINITION_HPP
#define SOLENGINE_GAME_DEFINITION_HPP

#include "solengine/solitaire_types.hpp"
#include <string>
#include <vector>
#include <memory>

namespace solengine {

/**
 * Metadata and structural rule specification for a Solitaire game variant.
 * Can be loaded dynamically from an Eatscript (.eats) definition file or statically compiled.
 */
struct SolitaireGameDef {
    std::string id;              // Unique identifier (e.g. "klondike_draw_1", "golf", "spider_4", "yukon")
    std::string name;            // Display title (e.g. "Klondike (Draw 1)", "Golf", "Yukon")
    std::string family;          // Classification family (e.g. "Klondike", "FreeCell", "Spider", "Golf", "Yukon", "FortyThieves")
    std::string category;        // Filter category for browser (e.g. "Popular", "Spider", "Golf", "FreeCell", "Adding")
    std::string mechanic;        // Core mechanic: "building", "clearing", "pairing", "matching", "rpg"
    std::string visibility;      // Information profile: "open", "half_open", "closed"
    std::vector<std::string> tags; // Discovery tags (e.g. "radial", "pure_skill", "wrapping", "no_stock")
    std::string layout{"columns"}; // Layout archetype: "columns", "pyramid", "canfield", "fan_grid", "radial_clock", "tripeaks"
    // Declarative rule traits (orthogonal solver/engine rules shared across variants)
    bool wrapRanks{false};       // King <-> Ace rank wrapping (Black Hole, TriPeaks, Clockwork Dial, Canfield, etc.)
    bool kingDeadEnd{false};     // Clearing games where Kings stop chains (Classic Golf)
    std::string tableauBuild;    // "alt_color", "same_suit", "any_suit", "none"
    std::string emptyTableau;    // "king", "any", "none"
    std::string moveGroup;       // "single", "ordered", "any_face_up", "whole_column"
    std::string foundationMode;  // "suit_up", "completed_run", "canfield_wrap"
    std::string ribbonRule;      // Ribbon solver archetype: "compress_1_3" (Accordion), "sandwich_1_2" (Betrothal)
    std::string description;     // Rules summary / how to play
    uint8_t decks{1};            // Deck count (1, 2, etc.)
    uint16_t deckCards{0};       // Optional explicit card count (0 = default decks * 52)
    bool excludeRedRoyals{false}; // Exclude red Aces & red J/Q/K (44-card Scoundrel deck)
    std::vector<uint8_t> excludeRanks; // Optional rank numbers (1-13) to exclude (e.g. {8,9} for 40-card Spanish)
    uint8_t suitCount{4};        // Number of suits to include (1-4)
    uint8_t jokers{0};           // Number of jokers per deck (0-2)
    uint8_t gridCols{0};         // Optional column count for "grid" layout (0 = auto-fit)
    bool supportsSolver{true};   // False for hidden-information or non-deterministic games (e.g. Memory)
    bool preferCuratedSolvable{false}; // True for ultra-low-win-rate games (e.g. Accordion): instant curated demo_seeds in Solvable mode
    bool dynamicLayout{false};   // True to re-run on_layout() after every move/undo/redo as tableau expands/shrinks
    bool allowUndo{true};        // False to disable Undo/Redo for strict modes
    uint8_t tableaus{7};         // Tableau column/slot count
    uint8_t foundations{4};      // Foundation pile count
    uint8_t freecells{0};        // FreeCell / reserve count
    std::vector<uint8_t> tableauDealCounts; // Initial card count per tableau column
    bool tableauAllFaceUp{false};
    bool foundationStarterCard{false};
    std::string stockDealMode{"waste_draw_1"}; // "waste_draw_1", "waste_draw_3", "deal_to_all_columns", "deal_to_foundation", "none"
    int32_t stockDealCount{1};
    int32_t maxRedeals{0};       // -1 = infinite, 0 = none, N = N redeals
    std::string filePath;        // Path to source .eats file (if loaded from file)
    bool isScripted{false};
    GameVariant nativeVariant{GameVariant::KlondikeDraw1};
    std::vector<std::string> alternateNames;
    uint32_t demoSeed{2026u};
    std::vector<uint32_t> demoSeeds;
    std::string defaultDeckSlug;
    std::string defaultTableThemeSlug;
    std::string defaultPostFxId;
    std::string defaultAmbienceId;
    std::string defaultSoundPackId;
    std::string defaultPresetId;
    std::string scriptSource;    // Optional in-memory Eatscript source (for bundled/Gist games)
    std::string seriesId;        // Optional parent series identifier
    std::string seriesName;      // Optional parent series display name
    int32_t seriesOrder{0};      // Ordering index within series
};

struct GameSeriesDef {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string presetId;
    std::string filePath;
    std::vector<std::string> gameIds;
};

/**
 * Registry and discovery manager for SolEngine game variants.
 * Discovers and parses .eats game definitions in the games/ folder.
 */
class GameRegistry {
public:
    static GameRegistry& instance();

    void initialize(const std::string& gamesDir = "games");
    void scanDirectory(const std::string& gamesDir);
    bool loadGameFile(const std::string& filePath, SolitaireGameDef& outDef);
    void registerGame(const SolitaireGameDef& def);
    void registerSeries(const GameSeriesDef& series);

    [[nodiscard]] const std::vector<SolitaireGameDef>& allGames() const noexcept { return games_; }
    [[nodiscard]] const std::vector<GameSeriesDef>& allSeries() const noexcept { return series_; }
    [[nodiscard]] const SolitaireGameDef* findById(const std::string& id) const noexcept;
    [[nodiscard]] const SolitaireGameDef* findByName(const std::string& name) const noexcept;
    [[nodiscard]] const SolitaireGameDef* findByVariant(GameVariant v) const noexcept;
    [[nodiscard]] const GameSeriesDef* findSeriesById(const std::string& id) const noexcept;

    [[nodiscard]] std::vector<SolitaireGameDef> search(
        const std::string& query,
        const std::string& category = "ALL",
        const std::string& mechanic = "ALL",
        const std::string& visibility = "ALL"
    ) const;
    [[nodiscard]] std::vector<std::string> categories() const;

private:
    GameRegistry();
    void registerBuiltins();

    std::vector<SolitaireGameDef> games_;
    std::vector<GameSeriesDef> series_;
    bool initialized_{false};
};

} // namespace solengine

#endif // SOLENGINE_GAME_DEFINITION_HPP

