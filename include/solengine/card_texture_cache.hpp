#ifndef SOLENGINE_CARD_TEXTURE_CACHE_HPP
#define SOLENGINE_CARD_TEXTURE_CACHE_HPP

#include "solengine/solitaire_types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace eatsbits::ui {
    class BatchRenderer2D;
}

namespace solengine {

struct CardAtlasSlot {
    float u0{0.0f};
    float v0{0.0f};
    float u1{1.0f};
    float v1{1.0f};
    int width{0};
    int height{0};
};

struct CardTexture {
    int width{0};
    int height{0};
    std::vector<uint8_t> rgba;    // 32-bit RGBA (byte 0=R, 1=G, 2=B, 3=A)
    std::vector<uint32_t> argb32; // Mip 0: Pre-packed 0xAARRGGBB for 1-cycle direct blitting
    std::vector<uint32_t> mip1;   // Mip 1: (W/2 x H/2) 2x2 box-supersampled
    std::vector<uint32_t> mip2;   // Mip 2: (W/4 x H/4) 4x4 box-supersampled

    // Select the optimal pre-filtered mipmap level based on target screen width
    [[nodiscard]] const uint32_t* getMipData(float screenW, int& outW, int& outH) const noexcept {
        if (!mip2.empty() && screenW <= 115.0f && width >= 360) {
            outW = width / 4;
            outH = height / 4;
            return mip2.data();
        }
        if (!mip1.empty() && screenW <= 230.0f && width >= 180) {
            outW = width / 2;
            outH = height / 2;
            return mip1.data();
        }
        outW = width;
        outH = height;
        return argb32.empty() ? nullptr : argb32.data();
    }
};

enum class DeckThemeId : uint8_t {
    SolEngineClassic  = 0,
    ClassicSvg        = 1,
    SteampunkAi       = 2,
    Anime             = 3,
    Cyberpunk         = 4,
    BaroqueNoir       = 5,
    BaroqueLight      = 6,
    BaroqueAlabaster  = BaroqueLight,
    BaroqueDark       = 7,
    BaroqueCellShaded = 8,
    StainedGlass      = 9,
    PolychromeCrt     = 10,
    WatercolorSketch  = 11,
    Custom            = 12,
    SolPolyWhite      = 13,
    SolPolyDark       = 14,
    ScoundrelLowPoly  = 15,
    SolPolyClassic    = 16,
    SolPolyMobile     = 17,
    MemoryLowPoly     = 18,
    LowPolyWar        = 19,
    Count
};

enum class CardWearLevel : uint8_t {
    Pristine = 0,    // Pure original crisp vector/bitmap art (digital clean)
    Natural  = 1,    // Authentic playing card: warm ivory stock, softened press inks, micro-linen air-cushion finish
    Played   = 2,    // Gently handled: finger oils/vignette along edges, softened borders, slight corner scuffs
    Vintage  = 3,    // Aged deck: sepia oxidation, deckled fibrous edges, foxing spots, pulp core wear
    Count    = 4
};

enum class CardStock : uint8_t {
    None            = 0, // Pure unconditioned card artwork with zero paper stock effect
    CasinoLinen     = 1, // Embossed casino cambric/air-cushion linen finish
    AntiqueLaid     = 2, // Traditional 18th/19th century laid paper: wire ribbing & chain lines
    CottonRag       = 3, // Heavy fibrous cotton rag pasteboard: multi-directional tooth & raw pulp
    SmoothParchment = 4, // Calendered smooth satin playing card stock with micro-grain
    Count           = 5
};

enum class CardCornerStyle : uint8_t {
    Auto    = 0, // Follows deck designated corner radius (defaults to 24px if none)
    Sharp   = 1, // Forced 0px sharp square corners
    Vintage = 2, // Forced 8px subtle vintage corners
    Round   = 3, // Forced 24px modern casino round corners
    Count   = 4
};

class CardTextureCache {
public:
    static CardTextureCache& instance();

    static constexpr int kDefaultCardW = 360;
    static constexpr int kDefaultCardH = 504;
    static constexpr size_t kJokerBlackIndex = 52;
    static constexpr size_t kJokerRedIndex   = 53;
    static constexpr size_t kCardBackIndex   = 54;
    static constexpr size_t kCardBackVariants = 6;
    static constexpr size_t kTotalTextures = kCardBackIndex + kCardBackVariants;

    bool initialize(int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadEatsDeck(const std::string& eatsFilePath, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadDeckFromMemory(
        const uint8_t* data,
        size_t size,
        const std::string& format = "svgz",
        int cardW = kDefaultCardW,
        int cardH = kDefaultCardH,
        DeckThemeId theme = DeckThemeId::Custom,
        const std::string& cacheKey = ""
    );
    bool loadSvgDeck(const std::string& svgFilePath, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadBitmapDeck(const std::string& bitmapFilePath, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadDeckTheme(DeckThemeId theme, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    void bakeProceduralDeck(int cardW = kDefaultCardW, int cardH = kDefaultCardH);

    [[nodiscard]] CardWearLevel cardWearLevel() const noexcept { return wearLevel_; }
    void setCardWearLevel(CardWearLevel level);
    static const char* cardWearLevelName(CardWearLevel level) noexcept;

    [[nodiscard]] CardStock cardStock() const noexcept { return cardStock_; }
    void setCardStock(CardStock stock);
    static const char* cardStockName(CardStock stock) noexcept;

    [[nodiscard]] float deckCornerRadius() const noexcept { return deckCornerRadius_; }
    void setDeckCornerRadius(float r) noexcept { deckCornerRadius_ = std::max(0.0f, r); }

    [[nodiscard]] CardCornerStyle cornerStyle() const noexcept { return cornerStyle_; }
    void setCornerStyle(CardCornerStyle style) noexcept { cornerStyle_ = style; }

    [[nodiscard]] float effectiveCornerRadius() const noexcept {
        switch (cornerStyle_) {
            case CardCornerStyle::Sharp:   return 0.0f;
            case CardCornerStyle::Vintage: return 8.0f;
            case CardCornerStyle::Round:   return 24.0f;
            case CardCornerStyle::Auto:
            default:
                return deckCornerRadius_;
        }
    }

    [[nodiscard]] float effectiveRu() const noexcept {
        return effectiveCornerRadius() / static_cast<float>(kDefaultCardW);
    }

    [[nodiscard]] float effectiveRv() const noexcept {
        return effectiveCornerRadius() / static_cast<float>(kDefaultCardH);
    }

    static const char* cornerStyleName(CardCornerStyle style) noexcept;

    struct DeckDef {
        std::string id;
        std::string name;
        std::string filePath;
        DeckThemeId legacyId{DeckThemeId::Custom};
        std::string format;
        std::string blobBase64;
        float cornerRadius{-1.0f};
    };

    struct StagingDeck {
        std::array<CardTexture, kTotalTextures> baseTextures;
        std::array<CardTexture, kTotalTextures> textures;
        DeckThemeId theme{DeckThemeId::SolPolyClassic};
        std::string deckId;
        CardWearLevel wearLevel{CardWearLevel::Pristine};
        CardStock cardStock{CardStock::None};
        int cardW{kDefaultCardW};
        int cardH{kDefaultCardH};
        float cornerRadius{-1.0f};
        bool hasNewBase{false};
    };

    using ProgressCallback = std::function<bool(float progress, const std::string& status)>;

    // Staged offline conditioning / baking - completely isolated from active textures
    bool bakeStagedConditioning(
        StagingDeck& staging,
        CardWearLevel wear,
        CardStock stock,
        const ProgressCallback& onProgress = nullptr
    );

    bool loadBaseDeckTheme(
        DeckThemeId theme,
        int cardW,
        int cardH,
        std::array<CardTexture, kTotalTextures>& outBase,
        const ProgressCallback& onProgress = nullptr
    );

    bool loadStagedDeckTheme(
        StagingDeck& staging,
        DeckThemeId theme,
        CardWearLevel wear,
        CardStock stock,
        int cardW = kDefaultCardW,
        int cardH = kDefaultCardH,
        const ProgressCallback& onProgress = nullptr
    );

    bool loadStagedFileDeck(
        StagingDeck& staging,
        const std::string& filePath,
        CardWearLevel wear,
        CardStock stock,
        int cardW = kDefaultCardW,
        int cardH = kDefaultCardH,
        const ProgressCallback& onProgress = nullptr
    );

    bool loadStagedMemoryDeck(
        StagingDeck& staging,
        const uint8_t* data,
        size_t size,
        const std::string& format,
        DeckThemeId theme,
        const std::string& cacheKey,
        CardWearLevel wear,
        CardStock stock,
        int cardW = kDefaultCardW,
        int cardH = kDefaultCardH,
        const ProgressCallback& onProgress = nullptr
    );

    void commitStagedDeck(StagingDeck&& staging);

    void applyConditioningAndSync(CardWearLevel wear, CardStock stock);
    void applyConditioningAndSync(CardWearLevel wear);

    [[nodiscard]] const CardTexture* getCardTexture(Suit s, Rank r) const noexcept;
    [[nodiscard]] const CardTexture* getJokerTexture(bool red = false) const noexcept;
    [[nodiscard]] const CardTexture* getCardBackTexture(uint8_t cardId = 0) const noexcept;
    [[nodiscard]] const CardTexture* getTextureByIndex(size_t index) const noexcept;

    bool buildGpuAtlas(eatsbits::ui::BatchRenderer2D& renderer);
    [[nodiscard]] const CardAtlasSlot& getAtlasSlot(Suit s, Rank r) const noexcept;
    [[nodiscard]] const CardAtlasSlot& getJokerAtlasSlot(bool red = false) const noexcept;
    [[nodiscard]] const CardAtlasSlot& getBackAtlasSlot(uint8_t cardId = 0) const noexcept;
    [[nodiscard]] const CardAtlasSlot& getAtlasSlotByIndex(size_t index) const noexcept;
    [[nodiscard]] const CardAtlasSlot& getPreviewFaceAtlasSlot() const noexcept;
    [[nodiscard]] const CardTexture* getPreviewFaceTexture() const noexcept;
    void updatePreviewCard(DeckThemeId theme, CardWearLevel wear, CardStock stock);
    void setPreviewDeckTheme(DeckThemeId theme);
    [[nodiscard]] DeckThemeId previewDeckTheme() const noexcept { return previewDeckTheme_; }
    [[nodiscard]] bool hasGpuAtlas() const noexcept { return hasGpuAtlas_; }
    [[nodiscard]] bool isAtlasDirty() const noexcept { return atlasDirty_; }
    void markAtlasClean() noexcept { atlasDirty_ = false; }

    [[nodiscard]] bool isLoaded() const noexcept { return isLoaded_; }
    void setLowPolySeed(uint32_t seed);
    [[nodiscard]] uint32_t lowPolySeed() const noexcept { return lowPolySeed_; }
    [[nodiscard]] int cardWidth() const noexcept { return cardW_; }
    [[nodiscard]] int cardHeight() const noexcept { return cardH_; }
    void scanDecksFolder(const std::string& decksDir = "decks");
    void registerDeck(const DeckDef& def);
    [[nodiscard]] const std::vector<DeckDef>& availableDecks();
    [[nodiscard]] size_t findDeckIndex(const std::string& slug, DeckThemeId legacyFallback) const;
    [[nodiscard]] const DeckDef* getDeckDef(size_t index) const;
    [[nodiscard]] const std::string& currentDeckId() const noexcept { return currentDeckId_; }
    bool loadDeckByDef(const DeckDef& def, int cardW = kDefaultCardW, int cardH = kDefaultCardH);
    bool loadStagedDeckByDef(
        StagingDeck& staging,
        const DeckDef& def,
        CardWearLevel wear,
        CardStock stock,
        int cardW = kDefaultCardW,
        int cardH = kDefaultCardH,
        const ProgressCallback& onProgress = nullptr
    );
    void updatePreviewCardByDef(const DeckDef& def, CardWearLevel wear, CardStock stock);

    [[nodiscard]] DeckThemeId currentDeckTheme() const noexcept { return currentTheme_; }
    static const char* deckThemeName(DeckThemeId theme) noexcept;
    [[nodiscard]] std::string currentDeckSlug() const;

    [[nodiscard]] const std::string& currentCardBackId() const noexcept { return activeCardBackId_; }
    void setActiveCardBack(const std::string& backId);
    [[nodiscard]] std::string getCardBackDisplayName(const std::string& backId) const;
    void cycleCardBack(std::string& inOutId, int stepDelta);

    static inline size_t cardIndex(Suit s, Rank r) noexcept {
        size_t suitIdx = static_cast<size_t>(s);
        size_t rankIdx = (static_cast<size_t>(r) >= 1 && static_cast<size_t>(r) <= 13)
                         ? (static_cast<size_t>(r) - 1) : 0;
        return suitIdx * 13 + rankIdx;
    }

private:
    CardTextureCache() = default;
    int cardW_{200};
    int cardH_{280};
    bool isLoaded_{false};
    bool hasGpuAtlas_{false};
    bool atlasDirty_{false};
    DeckThemeId currentTheme_{DeckThemeId::SolPolyClassic};
    std::string currentDeckId_{"solpoly_classic"};
    uint32_t lowPolySeed_{42};
    uint32_t loadedLowPolySeed_{0};
    CardWearLevel wearLevel_{CardWearLevel::Natural};
    CardStock cardStock_{CardStock::SmoothParchment};
    float deckCornerRadius_{24.0f};
    CardCornerStyle cornerStyle_{CardCornerStyle::Auto};
    std::string activeCardBackId_{"classic_sapphire"};
    DeckThemeId previewDeckTheme_{DeckThemeId::Count};
    std::string previewDeckId_;
    CardTexture previewFaceTexture_{};
    CardAtlasSlot previewFaceAtlasSlot_{};
    std::unordered_map<DeckThemeId, CardTexture> sampleCardCache_{};
    std::unordered_map<std::string, CardTexture> sampleDeckCardCache_{};
    std::vector<DeckDef> scannedDecks_{};
    bool decksScanned_{false};
    std::array<CardTexture, kTotalTextures> baseTextures_{};
    std::array<CardTexture, kTotalTextures> textures_{};
    std::array<CardAtlasSlot, kTotalTextures> atlasSlots_{};
};

} // namespace solengine

#endif // SOLENGINE_CARD_TEXTURE_CACHE_HPP

