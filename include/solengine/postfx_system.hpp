#ifndef SOLENGINE_POSTFX_SYSTEM_HPP
#define SOLENGINE_POSTFX_SYSTEM_HPP

#include <string>
#include <vector>

namespace solengine {

struct PostFxOptionChoice {
    std::string id;
    std::string name;
    float u[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float param1{0.0f};
    float param2{0.0f};
};

struct PostFxDef {
    std::string id;           // e.g. "none", "filmic_grade", "noir_red_pop", "tilt_shift"
    std::string name;         // Display name in Settings UI
    std::string author;
    std::string description;
    std::string filePath;     // Path to .eats file (empty for "none")
    std::string shaderFunc;   // Eatscript shader function name (optional)
    std::string builtinFilter;// Built-in filter name if using ctx.apply_filter / filter: "..."
    std::string wgslSource;   // Compiled/generated WGSL source for GPU post-FX pass
    std::string optionLabel;  // e.g. "Stock", "Palette", "Focus"
    std::vector<PostFxOptionChoice> options;
    float defaultParam1{0.0f};
    float defaultParam2{0.0f};
    std::string gpuCost{"Low"}; // "Low", "Med", "High"
};

/**
 * Discovers, loads, and transpiles `/postfx/*.eats` post-processing presets
 * into WGSL shaders for real-time full-screen WebGPU post-processing.
 */
class PostFxSystem {
public:
    static PostFxSystem& instance();

    PostFxSystem() = default;
    ~PostFxSystem() = default;

    PostFxSystem(const PostFxSystem&) = delete;
    PostFxSystem& operator=(const PostFxSystem&) = delete;

    /// Scans `postfx/` directory for `.eats` files (prepends "None (Off)").
    void scanPostFx(const std::string& dir = "postfx");

    [[nodiscard]] const std::vector<PostFxDef>& availableEffects();
    [[nodiscard]] size_t findEffectIndex(const std::string& id) const;
    [[nodiscard]] const PostFxDef* getEffectDef(size_t index) const;
    [[nodiscard]] const PostFxDef* getEffectById(const std::string& id) const;

    /// Resolves the active uniform values (u0..u3, param1, param2) for an effect + option id.
    void resolveParams(const std::string& effectId,
                       const std::string& optionId,
                       float outU[4],
                       float& outParam1,
                       float& outParam2) const;

    /// Maps screen-space mouse coordinates (x, y) into pre-Post-FX scene coordinates
    /// when a geometric warp shader (e.g. curved CRT) is active.
    void warpMouseCoords(const std::string& effectId,
                         const std::string& optionId,
                         float intensity,
                         float width,
                         float height,
                         float& ioX,
                         float& ioY) const;

    /// Returns a built-in GPU WGSL shader for any of the 9 CardCanvas::applyFilter effects.
    static std::string buildBuiltinFilterWgsl(const std::string& filterName);

private:
    bool loadEatsFile(const std::string& filePath, PostFxDef& outDef, std::string* errOut = nullptr);

    std::vector<PostFxDef> effects_;
    bool scanned_{false};
};

} // namespace solengine

#endif // SOLENGINE_POSTFX_SYSTEM_HPP
