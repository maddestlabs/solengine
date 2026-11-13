#ifndef SOLENGINE_PROCEDURAL_BACKGROUND_HPP
#define SOLENGINE_PROCEDURAL_BACKGROUND_HPP

#include "solengine/solitaire_types.hpp"
#include "solengine/proctex_api.hpp"
#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/geometry.hpp"
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace solengine {

struct ThemeOptionChoice {
    std::string id;         // e.g. "navy_blue", "light_blue", "cognac", "camel"
    std::string name;       // e.g. "Navy Blue", "Light Blue", "Cognac", "Camel"
    float u[4]{0.0f, 0.0f, 0.0f, 0.0f}; // Uniform registers passed to bytecode shader (e.g. RGB dye tint)
};

struct TableThemeDef {
    std::string id;         // e.g. "ambientcg_paintedwood008c", "paintedwood_redcedar", "pastel_tan"
    std::string name;       // e.g. "PAINTED WOOD 008C", "PAINTED RED CEDAR", "PASTEL TAN"
    std::string filePath;   // Relative or absolute path to .eats file (empty for PastelTan)
    TableThemeId legacyId{TableThemeId::PastelTan};
    bool isProcedural{true};
    float lacquer{0.0f};    // 0.0 = completely matte (felt/fabric), 1.0 = high-gloss piano finish
    float reliefBump{2.4f}; // micro-relief normal bump intensity
    std::string optionLabel; // e.g. "Color", "Wash", "Leather", "Species" (defaults to "Option" if empty)
    std::vector<ThemeOptionChoice> options; // Author-defined theme sub-options
};

/**
 * High-performance procedural and AmbientCG PBR table surface generator with
 * physically informed 3D chandelier lighting, normal-mapped surface relief,
 * Blinn-Phong specular sheen, and radial distance falloff.
 * 
 * Supports seamless normal-mapped PBR materials:
 * - Emerald Lawn (AmbientCG Carpet012: rich woven baize felt)
 * - Hopsack Tweed (AmbientCG Fabric063: tactile 2x2 multi-ply wool yarn weave)
 * - Denim Chambray (AmbientCG Fabric061: indigo twill weft relief)
 * - Striped Jersey (AmbientCG Fabric079: ribbed knit jersey texture)
 * - Sunken Sanctum (AmbientCG Onyx013: polished dark obsidian veins)
 * - Perilous Shores (AmbientCG Moss002: weathered coastal earth)
 * - Pebbled Cowhide (Embossed pebble grain cowhide leather)
 * - Distressed Leather (AmbientCG Leather006: aniline nubuck)
 * - Painted Wood (AmbientCG PaintedWood008C: weathered barnwood)
 * - Pixel Dungeon Brick (Retro 16x16 dungeon brick wall)
 * - Pixel Emerald Water (Retro 32x32 aquatic shimmering water)
 */
class ProceduralBackgroundSystem {
public:
    static ProceduralBackgroundSystem& instance();

    ProceduralBackgroundSystem();
    ~ProceduralBackgroundSystem();

    ProceduralBackgroundSystem(const ProceduralBackgroundSystem&) = delete;
    ProceduralBackgroundSystem& operator=(const ProceduralBackgroundSystem&) = delete;

    /**
     * Render the procedural table background across the specified rectangle by theme slug / ID.
     */
    void renderBackground(
        eatsbits::ui::BatchRenderer2D& r,
        float x, float y, float w, float h,
        const std::string& themeSlug,
        const eatsbits::ui::LightSource2D& light,
        uint32_t seed = 2026u
    );

    /**
     * Render the procedural table background across the specified rectangle by legacy theme ID.
     */
    void renderBackground(
        eatsbits::ui::BatchRenderer2D& r,
        float x, float y, float w, float h,
        TableThemeId theme,
        const eatsbits::ui::LightSource2D& light,
        uint32_t seed = 2026u
    );

    [[nodiscard]] float activeLacquer() const noexcept;
    [[nodiscard]] float activeReliefBump() const noexcept;
    void setActiveLacquer(float lacquer, float reliefBump = 2.4f) noexcept;

    /**
     * Invalidate cached procedural textures (e.g. on window resize or theme switch).
     */
    void invalidateCache() noexcept;

    /**
     * Pre-bake and synthesize procedural table surface textures for a given theme offline.
     * Invokes progress callback and supports early cooperative cancellation.
     */
    bool prebakeTheme(
        TableThemeId theme,
        float w, float h,
        const eatsbits::ui::LightSource2D& light,
        const std::function<bool(float progress, const std::string& status)>& onProgress = nullptr
    );

    /**
     * Retrieve all dynamically discovered table background themes found in /themes/.
     */
    [[nodiscard]] const std::vector<TableThemeDef>& availableThemes() const noexcept;

    /**
     * Find theme index by ID, slug, name, or legacy TableThemeId.
     */
    [[nodiscard]] size_t findThemeIndex(const std::string& idOrName, TableThemeId fallback = TableThemeId::Count) const noexcept;

    /**
     * Get theme definition by index (bounds checked, returns nullptr if out of range).
     */
    [[nodiscard]] const TableThemeDef* getThemeDef(size_t index) const noexcept;

    /**
     * Scan themes directory and populate availableThemes.
     */
    void scanThemesFolder(const std::string& themesDir = "themes");

    /**
     * Pre-bake and synthesize surface for a TableThemeDef.
     */
    bool prebakeThemeDef(
        const TableThemeDef& def,
        float w, float h,
        const eatsbits::ui::LightSource2D& light,
        const std::function<bool(float progress, const std::string& status)>& onProgress = nullptr
    );

    /**
     * Enable or disable seamless AmbientCG PBR texture baking.
     */
    void setPbrTexturesEnabled(bool enable) noexcept;
    [[nodiscard]] bool isPbrTexturesEnabled() const noexcept;

    /**
     * Set the active theme option / variant for the current theme (e.g. "navy_blue", "cognac").
     */
    void setThemeOption(const std::string& optionId) noexcept;
    [[nodiscard]] const std::string& themeOption() const noexcept;

    /**
     * Helper to resolve the active ThemeOptionChoice for a given theme definition.
     * Returns nullptr if the theme defines no options.
     */
    [[nodiscard]] const ThemeOptionChoice* getActiveOption(const TableThemeDef& def) const noexcept;

    /**
     * Set the global table surface tint preset (recolors procedural & PBR backgrounds).
     */
    void setTableTint(TableTintPreset tint) noexcept;
    [[nodiscard]] TableTintPreset tableTint() const noexcept;

    /**
     * Check if a PBR material is available for the given theme.
     */
    [[nodiscard]] bool hasPbrMaterial(TableThemeId theme) const noexcept;

    /**
     * Check if a procedural .eat material script is available for the given theme.
     */
    [[nodiscard]] bool hasProceduralMaterial(TableThemeId theme) const noexcept;

    /**
     * Load a procedural background theme directly from an .eat script or script source.
     * Bakes the procedural PBR material in host memory and overrides the table surface.
     */
    bool loadProceduralTheme(
        const std::string& scriptPathOrSource,
        const ProceduralBakeOptions& options = {}
    );

    /**
     * Load a custom background theme directly from a single-file .eats package.
     * When loaded, overrides the table surface with this custom PBR material.
     */
    bool loadCustomEatsTheme(const std::string& eatsFilePath);

    /**
     * Load a custom background theme directly from memory buffer (EBKG container or image).
     */
    bool loadCustomThemeFromMemory(
        const uint8_t* blobData,
        size_t blobSize,
        float uvScale = 2.0f,
        float normalStrength = 1.35f,
        float specularIntensity = 0.65f,
        float ambientLevel = 0.22f,
        float diffuseLevel = 0.88f,
        float tintR = 1.0f,
        float tintG = 1.0f,
        float tintB = 1.0f
    );

    /**
     * Clear any loaded custom .eats theme and return to the active TableThemeId theme.
     */
    void clearCustomEatsTheme() noexcept;

    /**
     * Check if a custom theme is currently loaded and active.
     */
    [[nodiscard]] bool hasCustomEatsTheme() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace solengine

#endif // SOLENGINE_PROCEDURAL_BACKGROUND_HPP
