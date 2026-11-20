#ifndef SOLENGINE_SETTINGS_MANAGER_HPP
#define SOLENGINE_SETTINGS_MANAGER_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/card_texture_cache.hpp"
#include "solengine/table_renderer.hpp"
#include "solengine/dynamic_light.hpp"
#include "solengine/audio_foley.hpp"
#include <string>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace solengine {

class SolitaireBoard;

enum class RenderResolutionMode : uint8_t {
    Auto = 0,     // Native on capable hardware, 720p on low-capability CPU/GPU
    Native = 1,   // Full swapchain resolution
    Res1080p = 2, // Max 1920x1080 (aspect-preserving)
    Res720p = 3,  // Max 1280x720 (aspect-preserving)
    Res540p = 4,  // Max 960x540 (aspect-preserving)
    Count
};

struct GameSceneProfile {
    std::optional<TableThemeId> tableTheme;
    std::optional<std::string> tableThemeSlug;
    std::optional<std::string> tableThemeOption;
    std::optional<TableTintPreset> tableTint;
    std::optional<std::string> postFxId;
    std::optional<std::string> postFxOption;
    std::optional<float> postFxIntensity;
    std::optional<bool> showTableMarkings;

    std::optional<DeckThemeId> deckTheme;
    std::optional<std::string> deckSlug;
    std::optional<uint32_t> lowPolySeed;
    std::optional<std::string> cardBackId;
    std::optional<CardWearLevel> cardWear;
    std::optional<CardStock> cardStock;
    std::optional<CardCornerStyle> cardCorners;
    std::optional<float> cardDeviation;

    std::optional<LightingPreset> lightingPreset;
    std::optional<LightAngle> lightAngle;
    std::optional<ShadowGobo> shadowGobo;
    std::optional<float> lightWaver;
    std::optional<float> lightFlicker;
    std::optional<float> goboSoftness;
    std::optional<float> godRaysIntensity;
    std::optional<float> dustMotesIntensity;
    std::optional<float> sceneDarkness;
    std::optional<float> lightStrength;

    std::optional<std::string> soundPackId;
    std::optional<std::string> ambienceId;
    std::optional<float> ambienceVolume;
    std::optional<StockPosition> stockPosition;
    std::optional<bool> autoFinish;
    std::optional<VictoryAnimOverride> victoryAnimOverride;

    [[nodiscard]] bool empty() const noexcept;
};

struct AppSettings {
    GameVariant variant{GameVariant::KlondikeDraw1};
    TableThemeId tableTheme{TableThemeId::PastelTan};
    std::string tableThemeSlug{"pastel"};
    std::string tableThemeOption{"stone"}; // Active option id for current theme (e.g. "navy_blue", "cognac")
    std::unordered_map<std::string, std::string> tableThemeOptions; // Per-theme option preference cache
    std::string postFxId{"none"};
    std::string postFxOption;
    std::unordered_map<std::string, std::string> postFxOptions;
    float postFxIntensity{1.0f};
    bool postFxAffectUi{false};
    RenderResolutionMode renderResolution{RenderResolutionMode::Auto};
    TableTintPreset tableTint{TableTintPreset::Auto};
    DeckThemeId deckTheme{DeckThemeId::SolPolyClassic};
    std::string deckSlug{"solpoly_classic"};
    uint32_t lowPolySeed{42};
    std::string cardBackId{"classic_sapphire"};
    GuiThemeStyle guiTheme{GuiThemeStyle::ClockworkEngine};
    CardWearLevel cardWear{CardWearLevel::Natural};
    CardStock cardStock{CardStock::SmoothParchment};
    CardCornerStyle cardCorners{CardCornerStyle::Auto};
    float cardDeviation{0.50f}; // 0.0 = perfectly neat (0px, 0 deg), 0.25 = Subtle (+-2.5px, +-1.25 deg), 1.0 = maximum organic skew (+-10px, +-5 deg)
    ResponsiveLayoutMode layoutMode{ResponsiveLayoutMode::Auto};
    StockPosition stockPosition{StockPosition::Auto};
    MenuAnchor menuAnchor{MenuAnchor::BottomLeft};
    bool menuFlowVertical{false};
    MenuDisplayMode menuDisplayMode{MenuDisplayMode::Auto};
    ToolbarScale toolbarScale{ToolbarScale::Regular};
    uint32_t toolbarItems{ToolbarItem_Default};
    int aaMode{4}; // Permanently 4x MSAA (supersampled anti-aliasing)
    LightingPreset lightingPreset{LightingPreset::GoldenHour}; // Light mood palette
    LightAngle lightAngle{LightAngle::TopLeft};
    ShadowGobo shadowGobo{ShadowGobo::WindowFrame};
    float lightWaver{0.50f};          // 0.0 = steady, 1.0 = full organic sway
    float lightFlicker{0.0f};        // 0.0 = steady, 1.0 = dynamic candle/draft flicker
    float goboSoftness{0.45f};       // 0.05 = crisp mullions, 1.0 = soft diffused penumbra
    float godRaysIntensity{0.0f};     // 0.0 = off, up to 1.0 = dramatic light shafts
    float dustMotesIntensity{0.0f};   // 0.0 = off, up to 1.0 = sunlit floating dust
    float sceneDarkness{0.22f}; // 0.0 = 0% darkening, 0.22 = 22% subtle atmospheric darkening, up to 0.85
    float lightStrength{1.20f}; // 0.0 = off, 1.20 = 120% warm overhead light pool, up to 2.00
    float masterVolume{0.85f};
    bool muted{false};
    ShuffleStyle shuffleStyle{ShuffleStyle::Soft};
    std::string soundPackId{"default"};
    std::string ambienceId{"off"};
    float ambienceVolume{0.55f};
    bool randomizeOnNewDeal{true};
    bool guaranteeSolvable{false};
    uint32_t lastSeed{2026u};
    bool autoFinish{true};
    bool showTableMarkings{true}; // true = guides/runes/wells, false = pure bare tabletop (cards only)
    VictoryAnimOverride victoryAnimOverride{VictoryAnimOverride::GameDefault};
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
    std::unordered_map<std::string, GameSceneProfile> gameProfiles;
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

    [[nodiscard]] bool hasGameProfile(const std::string& gameId) const;
    [[nodiscard]] const GameSceneProfile* getGameProfile(const std::string& gameId) const;
    void setGameProfile(const std::string& gameId, const GameSceneProfile& profile);
    void clearGameProfile(const std::string& gameId);
    [[nodiscard]] AppSettings resolveForGame(const std::string& gameId) const;
    [[nodiscard]] static GameSceneProfile diffFromGlobal(const AppSettings& global, const AppSettings& effective);

    void applyToGame(SolitaireBoard& board, AudioFoleyEngine& foley, bool applyTextures = true);
    void syncFromGame(const SolitaireBoard& board, const AudioFoleyEngine& foley);

    [[nodiscard]] std::string getSettingsFilePath() const;

private:
    SettingsManager() = default;
    AppSettings settings_{};
};

} // namespace solengine

#endif // SOLENGINE_SETTINGS_MANAGER_HPP

