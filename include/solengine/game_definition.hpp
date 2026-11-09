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
    std::string layout{"columns"}; // Layout archetype: "columns", "pyramid", "canfield", "fan_grid", "radial_clock", "tripeaks"
    std::string description;     // Rules summary / how to play
    uint8_t decks{1};            // Deck count (1, 2, etc.)
    uint8_t tableaus{7};         // Tableau column count
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

    [[nodiscard]] const std::vector<SolitaireGameDef>& allGames() const noexcept { return games_; }
    [[nodiscard]] const SolitaireGameDef* findById(const std::string& id) const noexcept;
    [[nodiscard]] const SolitaireGameDef* findByName(const std::string& name) const noexcept;
    [[nodiscard]] const SolitaireGameDef* findByVariant(GameVariant v) const noexcept;

    [[nodiscard]] std::vector<SolitaireGameDef> search(const std::string& query, const std::string& category = "ALL") const;
    [[nodiscard]] std::vector<std::string> categories() const;

private:
    GameRegistry();
    void registerBuiltins();

    std::vector<SolitaireGameDef> games_;
    bool initialized_{false};
};

} // namespace solengine

#endif // SOLENGINE_GAME_DEFINITION_HPP

