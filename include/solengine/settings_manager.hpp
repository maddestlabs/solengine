#ifndef SOLENGINE_SETTINGS_MANAGER_HPP
#define SOLENGINE_SETTINGS_MANAGER_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/dynamic_light.hpp"
#include "solengine/audio_foley.hpp"
#include <string>
#include <cstdint>

namespace solengine {

class SolitaireBoard;

struct AppSettings {
    GameVariant variant{GameVariant::KlondikeDraw1};
    TableThemeId tableTheme{TableThemeId::PastelTan};
    DeckThemeId deckTheme{DeckThemeId::SolEnshade};
    CardWearLevel cardWear{CardWearLevel::Pristine};
    CardStock cardStock{CardStock::None};
    CardCornerStyle cardCorners{CardCornerStyle::Auto};
    float cardDeviation{0.25f}; // 0.0 = perfectly neat (0px, 0 deg), 0.25 = Subtle (+-2.5px, +-1.25 deg), 1.0 = maximum organic skew (+-10px, +-5 deg)
    ResponsiveLayoutMode layoutMode{ResponsiveLayoutMode::Auto};
    StockPosition stockPosition{StockPosition::Auto};
    MenuAnchor menuAnchor{MenuAnchor::BottomLeft};
    MenuDisplayMode menuDisplayMode{MenuDisplayMode::Auto};
    int aaMode{4}; // Permanently 4x MSAA (supersampled anti-aliasing)
    LightingPreset lightingPreset{LightingPreset::StudioSteady}; // Animated lighting (default StudioSteady = off)
    float sceneDarkness{0.22f}; // 0.0 = 0% darkening, 0.22 = 22% subtle atmospheric darkening, up to 0.85
    float lightStrength{1.20f}; // 0.0 = off, 1.20 = 120% warm overhead light pool, up to 2.00
    float masterVolume{0.85f};
    bool muted{false};
    ShuffleStyle shuffleStyle{ShuffleStyle::Soft};
    bool randomizeOnNewDeal{true};
    bool guaranteeSolvable{false};
    uint32_t lastSeed{2026u};
    bool autoFinish{true};
    bool showTableMarkings{true}; // true = guides/runes/wells, false = pure bare tabletop (cards only)
    std::vector<std::string> favorites{
        "klondike_draw_1",
        "klondike_draw_3",
        "freecell",
        "spider_1_suit",
        "spider_4_suits",
        "golf",
        "spiderette",
        "yukon"
    };
};

class SettingsManager {
public:
    static SettingsManager& instance();

    bool load();
    bool save();

    [[nodiscard]] const AppSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] AppSettings& settings() noexcept { return settings_; }

    [[nodiscard]] bool isFavorite(const std::string& gameId) const;
    void toggleFavorite(const std::string& gameId);

    void applyToGame(SolitaireBoard& board, AudioFoleyEngine& foley, bool applyTextures = true);
    void syncFromGame(const SolitaireBoard& board, const AudioFoleyEngine& foley);

    [[nodiscard]] std::string getSettingsFilePath() const;

private:
    SettingsManager() = default;
    AppSettings settings_{};
};

} // namespace solengine

#endif // SOLENGINE_SETTINGS_MANAGER_HPP

