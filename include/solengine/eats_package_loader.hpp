#ifndef SOLENGINE_EATS_PACKAGE_LOADER_HPP
#define SOLENGINE_EATS_PACKAGE_LOADER_HPP

#include <string>
#include <vector>

namespace solengine {

class AudioFoleyEngine;

struct EatsLoadResult {
    bool ok{false};
    std::string error;
    std::vector<std::string> gameIds;
    std::vector<std::string> seriesIds;
    std::vector<std::string> themeIds;
    std::vector<std::string> deckIds;
    std::vector<std::string> ambienceIds;
    std::vector<std::string> soundPackIds;
    std::vector<std::string> postFxIds;
    std::vector<std::string> presetIds;

    [[nodiscard]] bool hasAnyAsset() const noexcept {
        return !gameIds.empty() || !seriesIds.empty() || !themeIds.empty() ||
               !deckIds.empty() || !ambienceIds.empty() || !soundPackIds.empty() ||
               !postFxIds.empty() || !presetIds.empty();
    }
};

/**
 * Unified .eats v2 package loader and router.
 * Evaluates any .eats file (single-asset, multi-game series, or full themed bundle)
 * and registers all defined games, themes, decks, ambience, sound packs, post-FX,
 * presets, and series into their respective runtime registries.
 */
class EatsPackageLoader {
public:
    static bool loadFile(const std::string& filePath,
                         EatsLoadResult& outResult,
                         AudioFoleyEngine* foley = nullptr);

    static bool loadSource(const std::string& source,
                           const std::string& virtualPath,
                           EatsLoadResult& outResult,
                           AudioFoleyEngine* foley = nullptr);
};

} // namespace solengine

#endif // SOLENGINE_EATS_PACKAGE_LOADER_HPP
