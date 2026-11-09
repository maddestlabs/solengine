#ifndef SOLENGINE_SOLITAIRE_TYPES_HPP
#define SOLENGINE_SOLITAIRE_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace solengine {

enum class Suit : uint8_t {
    Spades   = 0, // Iron & Grave-Blades (Black / Obsidian)
    Hearts   = 1, // Ichors & Soul-Vessels (Red / Crimson)
    Diamonds = 2, // Aether & Waystones (Red / Amber Crimson)
    Clubs    = 3  // Wraithwood & Elder Boughs (Black / Deep Moss-Charcoal)
};

[[nodiscard]] constexpr bool isRedSuit(Suit s) noexcept {
    return s == Suit::Hearts || s == Suit::Diamonds;
}

enum class Rank : uint8_t {
    Ace   = 1,
    Two   = 2,
    Three = 3,
    Four  = 4,
    Five  = 5,
    Six   = 6,
    Seven = 7,
    Eight = 8,
    Nine  = 9,
    Ten   = 10,
    Jack  = 11,
    Queen = 12,
    King  = 13
};

enum class PileType : uint8_t {
    Stock      = 0,
    Waste      = 1,
    Foundation = 2,
    Tableau    = 3,
    FreeCell   = 4
};

enum class GameVariant : uint8_t {
    KlondikeDraw1 = 0,
    KlondikeDraw3 = 1,
    FreeCell      = 2,
    Spider1Suit   = 3,
    Spider2Suits  = 4,
    Spider3Suits  = 5,
    Spider4Suits  = 6,
    Golf          = 7,
    CustomScript  = 8
};

[[nodiscard]] constexpr bool isGolfVariant(GameVariant v) noexcept {
    return v == GameVariant::Golf;
}

[[nodiscard]] constexpr bool isSpiderVariant(GameVariant v) noexcept {
    return v == GameVariant::Spider1Suit ||
           v == GameVariant::Spider2Suits ||
           v == GameVariant::Spider3Suits ||
           v == GameVariant::Spider4Suits;
}

[[nodiscard]] constexpr uint8_t spiderSuitCount(GameVariant v) noexcept {
    switch (v) {
        case GameVariant::Spider1Suit:  return 1;
        case GameVariant::Spider2Suits: return 2;
        case GameVariant::Spider3Suits: return 3;
        case GameVariant::Spider4Suits: return 4;
        default: return 4;
    }
}

enum class ResponsiveLayoutMode : uint8_t {
    Auto            = 0, // Dynamic aspect-ratio & viewport width adaptation
    DesktopStandard = 1, // Traditional fixed desktop layout (generous spacing & wide top row)
    MobileCompact   = 2  // Forced compact mobile layout (lean margins, tight gaps)
};

enum class StockPosition : uint8_t {
    Auto        = 0, // Auto: Top-Left in Landscape, Bottom-Right (Thumb Zone) in Portrait
    TopLeft     = 1, // Classic Top-Left
    BottomRight = 2, // Thumb Zone: Bottom-Right (One-handed ergonomic reach)
    BottomLeft  = 3  // Thumb Zone: Bottom-Left (Left-handed ergonomic reach)
};

enum class MenuDisplayMode : uint8_t {
    Auto        = 0, // Auto: Icons only on mobile/narrow; Icon+Text on desktop
    IconAndText = 1, // Always show Icon and Text label
    IconOnly    = 2  // Minimalist Icon-only toolbar
};

enum class TableThemeId : uint8_t {
    PastelTan             = 0, // Solid warm pastel tan / craft paper table
    EmeraldLawn           = 1, // Green linen canvas weave (ambientCG Fabric035 / classic felt)
    HopsackTweed          = 2, // Basketweave hopsack tweed with multicolor neps (ambientCG Fabric063)
    DenimChambray         = 3, // Indigo chambray denim with horizontal weft slubs (ambientCG Fabric061)
    StripedJersey         = 4, // Horizontal striped ribbed knit jersey (ambientCG Fabric079)
    SunkenSanctum         = 5, // ZAngband Spectre vault: polished obsidian + cyan aether runes
    PerilousShores        = 6, // Cartographer's cabin + weathered coastal moss (ambientCG Moss002)
    PebbledCowhide        = 7, // Embossed pebble grain cowhide leather
    LeatherNubuck         = 8, // Distressed aniline nubuck cowhide (ambientCG Leather006)
    PaintedWood           = 9, // Weathered painted barnwood planks (ambientCG PaintedWood008C)
    PixelDungeon          = 10,// Retro 16x16 dungeon brick wall quantized to Endesga32
    PixelWater            = 11,// Retro 32x32 aquatic shimmering water tile
    ShaderVelvetNoir      = 12,// Obsidian velvet nap shimmer + photographic vignette
    ShaderCarraraMarble   = 13,// Italian white/gray Carrara marble with serpentine veins
    ShaderWateredSilk     = 14,// Royal sapphire watered silk moire wave interference
    ShaderPoolCaustics    = 15,// Tropical sunlit pool with refractive caustic light web
    ShaderArcaneParchment = 16,// Aged manuscript vellum with foxing spots & scorched edge
    ShaderCosmicNebula    = 17,// Deep space stellar nursery with violet/teal gas & stars
    ShaderSynthwaveSunset = 18,// Retro 80s Outrun perspective laser grid & glowing sunset
    ShaderTopographic     = 19,// Cartographic survey elevation contour map on explorer paper
    Count                 = 20
};

struct Card {
    uint8_t id{0};
    Suit suit{Suit::Spades};
    Rank rank{Rank::Ace};
    bool faceUp{false};
    bool enshadedGlow{false};

    // Spring-damper visual state (Hardwood 3.0 smooth card flight & 2.5D flip)
    float x{0.0f};
    float y{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float targetX{0.0f};
    float targetY{0.0f};
    float elevation{0.0f};       // 0 = resting on pile, >0 = airborne/dragged drop-shadow lift
    float targetElevation{0.0f};
    float flipAngle{0.0f};       // 0.0 = faceUp state, pi = flipped
    float angle{0.0f};           // Current visual rotation angle in radians (Hardwood 3.0 tactile realism)
    float targetAngle{0.0f};     // Target resting angle in radians
};

/**
 * Fast stateless Thomas Wang integer hash yielding deterministic uniform float in [-1.0f, +1.0f].
 * Zero memory overhead, zero state mutation, zero heap allocation.
 */
[[nodiscard]] inline float cardHashNoise(uint32_t seed, uint8_t cardId, uint32_t channel) noexcept {
    uint32_t key = seed ^ (static_cast<uint32_t>(cardId) << 8) ^ (channel * 0x9E3779B9u);
    key = (key ^ 61u) ^ (key >> 16);
    key += (key << 3);
    key ^= (key >> 4);
    key *= 0x27D4EB2Du;
    key ^= (key >> 15);
    return (static_cast<float>(key & 0xFFFFu) / 32767.5f) - 1.0f;
}

struct Pile {
    PileType type{PileType::Tableau};
    uint8_t index{0};
    float x{0.0f};
    float y{0.0f};
    float fanOffsetDown{18.0f};
    float fanOffsetUp{28.0f};
    std::vector<Card> cards;
};

struct MoveCommand {
    uint8_t fromPile{0};
    uint8_t toPile{0};
    uint8_t cardCount{1};
    bool flippedSourceTop{false};
    bool recycledWaste{false};
    bool spiderStockDeal{false};
    bool spiderCompletedRun{false};
    uint8_t spiderRunFoundation{0};
    bool isPairMove{false};
    uint8_t secondFromPile{255};
    int32_t scoreDelta{0};
};

struct HintMove {
    bool valid{false};
    uint8_t fromPile{0};
    uint8_t cardIndex{0};
    uint8_t toPile{0};
};

} // namespace solengine

#endif // SOLENGINE_SOLITAIRE_TYPES_HPP
