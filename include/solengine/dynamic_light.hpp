#ifndef SOLENGINE_DYNAMIC_LIGHT_HPP
#define SOLENGINE_DYNAMIC_LIGHT_HPP

#include "eatsbits/ui/geometry.hpp"
#include "solengine/solitaire_board.hpp"
#include <array>
#include <cstdint>
#include <random>

namespace solengine {

/**
 * Pendular sway configuration for physical illumination sources.
 * Employs a dual-frequency Lissajous harmonic to create an organic,
 * non-mechanical 2D orbital sway.
 */
struct LightingSwayConfig {
    bool enabled{true};
    float amplitudeX{48.0f};     // Horizontal sway amplitude in pixels
    float amplitudeY{18.0f};     // Vertical / depth sway amplitude in pixels
    float speed{0.65f};          // Oscillation frequency in Hz
    float phaseOffset{0.42f};    // Lissajous phase difference for organic orbit
};

/**
 * Atmospheric flicker modes for dramatic table moods.
 */
enum class FlickerMode : uint8_t {
    None = 0,         // Steady, unvarying studio light
    GentleDraft,      // Subtle ambient breathing (1-3% fluctuation)
    CandleGaslight,   // Organic candle/lamp flame jitter with soft draft flutter
    EerieHorror,      // Ominous flickering gaslamp with sudden nervous brown-outs / dropouts
    CyberpunkGlitch   // Sharp electric voltage dips and rapid micro-jitters
};

/**
 * Parameterization for light flicker, drafts, and unsettling voltage drops.
 */
struct LightingFlickerConfig {
    FlickerMode mode{FlickerMode::None};
    float intensity{0.08f};       // Flutter depth (0.0 = none, 1.0 = heavy)
    float dropoutChance{0.005f};  // Probability per second of an ominous sudden dip
    float dropoutDip{0.42f};      // Depth of sudden light drop (0.42 = drops by 42%)
    float dropoutDuration{0.14f}; // Duration of dropouts in seconds
};

/**
 * Curated lighting presets for one-touch atmospheric control.
 */
enum class LightingPreset : uint8_t {
    GentleChandelier = 0, // Warm, elegant ballroom chandelier with slow pendular sway
    Candlelight,          // Cozy warm gold flame with delicate wind-draft flicker
    HauntedManor,         // Dark eerie amber/green gaslamp with unsettling dropouts & creep
    CyberpunkNeon,        // Electric cyan/magenta pulse with high-frequency neon buzzing
    StudioSteady          // Perfectly crisp, motionless photographic studio lighting
};

/**
 * Real-time dynamic light controller for SolEngine.
 * Synthesizes physical pendular sway, organic flame micro-flutter, and unsettling
 * horror-themed light dropouts to inform GPU fragment shaders, card edge bevel glints,
 * and dynamic table drop shadows.
 */
class DynamicLightController {
public:
    explicit DynamicLightController(LightingPreset initialPreset = LightingPreset::StudioSteady);

    void setPreset(LightingPreset preset);
    void cyclePreset();
    [[nodiscard]] LightingPreset getPreset() const noexcept { return preset_; }
    [[nodiscard]] const char* getPresetName() const noexcept;

    void setSway(const LightingSwayConfig& sway) noexcept { sway_ = sway; }
    [[nodiscard]] const LightingSwayConfig& getSway() const noexcept { return sway_; }

    void setFlicker(const LightingFlickerConfig& flicker) noexcept { flicker_ = flicker; }
    [[nodiscard]] const LightingFlickerConfig& getFlicker() const noexcept { return flicker_; }

    void setBasePosition(float xRatio, float yRatio, float elevation) noexcept {
        baseXRatio_ = xRatio;
        baseYRatio_ = yRatio;
        baseZ_ = elevation;
    }

    void setBaseColor(float r, float g, float b) noexcept {
        colorR_ = r;
        colorG_ = g;
        colorB_ = b;
    }

    void setAmbientColor(float r, float g, float b) noexcept {
        ambientR_ = r;
        ambientG_ = g;
        ambientB_ = b;
    }

    void setBaseIntensity(float intensity) noexcept {
        baseIntensity_ = intensity;
    }

    /**
     * Advance animation clock and evaluate physical sway and stochastic flicker.
     */
    void update(float dt);

    /**
     * Compute current effective 3D light source for the given viewport.
     */
    [[nodiscard]] eatsbits::ui::LightSource2D getEffectiveLight(float viewportW, float viewportH) const noexcept;

    /**
     * RGBA light color with dynamic flicker intensity applied to alpha.
     */
    [[nodiscard]] std::array<float, 4> getLightColor() const noexcept;

    /**
     * RGBA ambient baseline color.
     */
    [[nodiscard]] std::array<float, 4> getAmbientColor() const noexcept;

    /**
     * Automatically tune lighting atmosphere to match deck or table theme.
     */
    void applyThemeDefault(TableThemeId theme);

private:
    LightingPreset preset_{LightingPreset::StudioSteady};
    LightingSwayConfig sway_{};
    LightingFlickerConfig flicker_{};

    float baseXRatio_{0.50f};
    float baseYRatio_{0.28f};
    float baseZ_{550.0f};
    float baseIntensity_{1.0f};

    float colorR_{1.04f};
    float colorG_{1.00f};
    float colorB_{0.94f};

    float ambientR_{0.35f};
    float ambientG_{0.35f};
    float ambientB_{0.35f};

    float elapsedTime_{0.0f};
    float currentSwayOffsetX_{0.0f};
    float currentSwayOffsetY_{0.0f};
    float currentFlickerMod_{1.0f};

    float dropoutTimer_{0.0f};
    float dropoutActive_{0.0f};

    std::mt19937 rng_{42u};
};

} // namespace solengine

#endif // SOLENGINE_DYNAMIC_LIGHT_HPP

