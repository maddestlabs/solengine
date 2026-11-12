#ifndef SOLENGINE_CARD_BACK_HPP
#define SOLENGINE_CARD_BACK_HPP

#include "solengine/card_texture_cache.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace solengine {

class CardCanvas;

enum class CardBackType : uint8_t {
    ProceduralDraw = 0, // Generated via CardCanvas (vector primitives, procedural shaders, finishes)
    Eatscript      = 1, // Generated via Eatscript script function (on_draw_card_back)
    Bitmap         = 2, // Raster image (PNG/JPEG)
    Svg            = 3, // Scalable Vector Graphics
};

struct CardBackDescriptor {
    std::string id;            // Unique identifier (e.g. "solenshade", "classic_sapphire", "steampunk_brass")
    std::string displayName;   // User-facing name (e.g. "SolEnshade Charcoal", "Classic Sapphire")
    std::string deckOrigin;    // "" for universal (available across all decks), or deck theme slug (e.g. "steampunk", "anime")
    CardBackType type{CardBackType::ProceduralDraw};
    
    // Cached unconditioned master card back texture
    CardTexture baseTexture;

    // Optional generator function to regenerate texture at arbitrary resolution (w, h)
    std::function<bool(CardTexture& outTex, int w, int h)> generator;
};

class CardBackRegistry {
public:
    static CardBackRegistry& instance();

    // Initialize universal card backs (called once on engine startup)
    void initialize(int cardW = CardTextureCache::kDefaultCardW, int cardH = CardTextureCache::kDefaultCardH);

    // Registration
    void registerUniversalBack(const CardBackDescriptor& desc);
    void registerDeckBack(const CardBackDescriptor& desc);
    void clearDeckBacks(const std::string& deckOrigin);
    void clearAllDeckBacks();

    // Querying
    [[nodiscard]] std::vector<CardBackDescriptor> getAvailableBacks(const std::string& deckOrigin) const;
    [[nodiscard]] const CardBackDescriptor* findBack(const std::string& id, const std::string& deckOrigin) const;
    [[nodiscard]] const CardBackDescriptor* getDefaultBackForDeck(const std::string& deckOrigin) const;

    // CardCanvas Baking Helper: gives authors the exact same API as card faces!
    static bool bakeCanvasBack(
        CardTexture& outTex,
        int cardW,
        int cardH,
        const std::function<void(CardCanvas& canvas, int w, int h)>& drawFn
    );

    // Raster & Vector loaders
    static bool loadBitmapBack(CardTexture& outTex, const std::string& filePath, int cardW, int cardH);
    static bool loadMemoryBack(CardTexture& outTex, const uint8_t* data, size_t size, const std::string& format, int cardW, int cardH);
    static bool loadSvgBack(CardTexture& outTex, const std::string& svgSource, int cardW, int cardH);

    // Companion file scanning in decks/ directory
    void scanCompanionBacks(const std::string& decksDir, int cardW, int cardH);

private:
    CardBackRegistry() = default;
    bool initialized_{false};
    int currentCardW_{CardTextureCache::kDefaultCardW};
    int currentCardH_{CardTextureCache::kDefaultCardH};
    std::vector<CardBackDescriptor> universalBacks_;
    std::vector<CardBackDescriptor> deckBacks_;
};

} // namespace solengine

#endif // SOLENGINE_CARD_BACK_HPP

