#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <cmath>
#include <algorithm>
#include "eatsbits/ui/geometry.hpp"

namespace eatsbits::ui {

/**
 * Lightweight perceptual color structure supporting RGBA float components [0.0, 1.0],
 * packed RGBA8 integers, HSL color space conversions, relative luminance calculations,
 * lightening/darkening, saturation scaling, and alpha tinting.
 */
struct Color {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{1.0f};

    constexpr Color() noexcept = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) noexcept
        : r(r_), g(g_), b(b_), a(a_) {}

    // Construct from packed 0xRRGGBB or 0xAARRGGBB
    static constexpr Color fromHex(uint32_t hex, float alpha = 1.0f) noexcept {
        float r_ = ((hex >> 16) & 0xFF) / 255.0f;
        float g_ = ((hex >> 8) & 0xFF) / 255.0f;
        float b_ = (hex & 0xFF) / 255.0f;
        return Color(r_, g_, b_, alpha);
    }

    [[nodiscard]] constexpr uint32_t toRgba8() const noexcept {
        auto ur = static_cast<uint32_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
        auto ug = static_cast<uint32_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
        auto ub = static_cast<uint32_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        auto ua = static_cast<uint32_t>(std::clamp(a * 255.0f, 0.0f, 255.0f));
        return (ur << 24) | (ug << 16) | (ub << 8) | ua;
    }

    [[nodiscard]] constexpr bool operator==(const Color& o) const noexcept {
        return toRgba8() == o.toRgba8();
    }
    [[nodiscard]] constexpr bool operator!=(const Color& o) const noexcept {
        return toRgba8() != o.toRgba8();
    }

    [[nodiscard]] constexpr Color withAlpha(float newAlpha) const noexcept {
        return Color(r, g, b, std::clamp(newAlpha, 0.0f, 1.0f));
    }

    [[nodiscard]] constexpr Color operator*(float scalar) const noexcept {
        return Color(std::clamp(r * scalar, 0.0f, 1.0f),
                     std::clamp(g * scalar, 0.0f, 1.0f),
                     std::clamp(b * scalar, 0.0f, 1.0f),
                     a);
    }

    static constexpr Color lerp(const Color& c1, const Color& c2, float t) noexcept {
        float f = std::clamp(t, 0.0f, 1.0f);
        return Color(
            c1.r + (c2.r - c1.r) * f,
            c1.g + (c2.g - c1.g) * f,
            c1.b + (c2.b - c1.b) * f,
            c1.a + (c2.a - c1.a) * f
        );
    }

    // ITU-R BT.709 relative perceptual luminance
    [[nodiscard]] float luminance() const noexcept {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    [[nodiscard]] bool isDark() const noexcept {
        return luminance() < 0.45f;
    }

    // Struct for HSL color space
    struct Hsl {
        float h{0.0f}; // [0.0, 360.0)
        float s{0.0f}; // [0.0, 1.0]
        float l{0.0f}; // [0.0, 1.0]
    };

    [[nodiscard]] Hsl toHsl() const noexcept;
    static Color fromHsl(float h, float s, float l, float a = 1.0f) noexcept;

    // Lightness adjustments
    [[nodiscard]] Color lighten(float amount) const noexcept;
    [[nodiscard]] Color darken(float amount) const noexcept;

    // Saturation adjustments
    [[nodiscard]] Color saturate(float amount) const noexcept;
    [[nodiscard]] Color desaturate(float amount) const noexcept;

    // Color Harmonics
    [[nodiscard]] Color complementary() const noexcept;
    [[nodiscard]] Color analogous(float angleDegrees = 30.0f) const noexcept;

    // Linear interpolation / alpha blending between background and foreground
    static Color blend(const Color& bg, const Color& fg, float weight) noexcept;

    // Ensure contrast against a background (adjusts lightness until WCAG contrast ratio is met)
    [[nodiscard]] Color ensureContrast(const Color& bg, float minRatio = 4.5f) const noexcept;
};

/**
 * Minimal seed colors needed to algorithmically derive an entire working DAW theme.
 * Allows optional artisanal overrides for signature hardware nuances.
 */
struct ThemeSeed {
    std::string name;
    std::string description;
    bool isLight{false};

    // Core Anchors
    Color chassisBase;      // Main hardware chassis surface
    Color primaryAccent;    // Primary UI highlight / brand glow
    std::optional<Color> secondaryAccent; // Secondary accent (e.g. solo/record/function)

    // Optional Artisanal Overrides (if unset, auto-derived mathematically)
    std::optional<Color> overrideBackgroundDark;
    std::optional<Color> overridePanelHeader;
    std::optional<Color> overrideControlWell;
    std::optional<Color> overrideTempoText;
    std::optional<Color> overrideTempoGlow;
    std::optional<Color> overrideLcdBackground;
    std::optional<Color> overrideLcdBorder;
    std::optional<Color> overrideLcdText;
    std::optional<Color> overrideCodeEditorBg;
    std::optional<Color> overrideTextPrimary;
    std::optional<Color> overrideTextSecondary;
    std::optional<Color> overrideTextMuted;
    std::optional<LightSource2D> overrideGlobalLight;

    // Syntax Highlighting Overrides
    std::optional<Color> overrideSyntaxKeyword;
    std::optional<Color> overrideSyntaxString;
    std::optional<Color> overrideSyntaxNumber;
    std::optional<Color> overrideSyntaxComment;
    std::optional<Color> overrideSyntaxFunction;
    std::optional<Color> overrideSyntaxIdentifier;
    std::optional<Color> overrideSyntaxOperator;
    std::optional<Color> overrideSyntaxType;
};

enum class ControlAestheticMode : uint8_t {
    AutoThemeDefault = 0, // Respects plugin/preset skeuomorphic styling
    ForceMinimal     = 1, // Global override: clean minimalist vector aesthetics DAW-wide
    HighContrastFlat = 2  // Pure flat lines & high contrast
};

/**
 * Comprehensive table of semantic UI tokens consumed across all DAW panels and widgets.
 */
struct ThemeTokens {
    std::string name;
    std::string description;
    bool isLight{false};

    // 1. Chassis & Panel Backgrounds
    Color backgroundDark;
    Color panelBackground;
    Color panelHeader;
    Color panelHeaderGradientTop;
    Color panelHeaderGradientBottom;
    Color controlBackground;
    Color controlWell;
    Color borderSubtle;
    Color borderFocus;

    // 2. Accents & Indicators
    Color primaryAccent;
    Color secondaryAccent;
    Color highlight;
    Color playActive;
    Color stopActive;
    Color recordActive;
    Color muteActive;
    Color soloActive;

    // 3. Displays & Nixie Tubes
    Color tempoText;
    Color tempoGlow;
    Color lcdBackground;
    Color lcdBorder;
    Color lcdText;
    Color lcdDotGrid;
    Color lcdGlow;

    // 4. Arranger, Timeline & Piano Roll
    Color timelineRuler;
    Color playhead;
    Color loopRegion;
    Color gridLineMajor;
    Color gridLineMinor;
    Color pianoWhiteKey;
    Color pianoBlackKey;
    Color pianoLaneDark;
    Color pianoLaneLight;
    Color noteDefault;
    Color noteSelected;

    // 5. Code Editor & Scripting (Eatscript)
    Color codeEditorBg;
    Color codeEditorGutter;
    Color codeEditorText;
    Color codeEditorGutterText;
    Color codeEditorBorder;
    Color syntaxKeyword;
    Color syntaxString;
    Color syntaxNumber;
    Color syntaxComment;
    Color syntaxFunction;
    Color syntaxIdentifier;
    Color syntaxOperator;
    Color syntaxType;

    // 6. Typography
    Color textPrimary;
    Color textSecondary;
    Color textMuted;
    Color textInverse;

    // 7. Contextual 2.5D Lighting & Shadows
    LightSource2D globalLight{640.0f, -180.0f, 500.0f, 0.85f};

    // 8. Global Aesthetic & Performance Flags
    ControlAestheticMode controlMode{ControlAestheticMode::AutoThemeDefault};
    bool showControlGradients{true};
    bool showControlDropShadows{true};
};

/**
 * Central Theme Registry and Derivation Engine.
 */
class Theme {
public:
    enum class Preset : int {
        AteTrack = 0,       // Default: 80s 8-Track Vintage Hardware Console
        MidnightBites = 1,  // Obsidian Dark / Cyber Neon (Midnight Munchies)
        LightSnack = 2,     // Bright Daylight Studio
        Breakfast = 3,      // Solarized Light
        Dinner = 4,         // Solarized Dark
        CountsBite = 5,     // Dracula (Gothic slate / purple / pink)
        NordicFrost = 6,    // Nord (Polar night / arctic frost blues)
        Catppuccino = 7,    // Catppuccin Mocha (Cocoa / lavender / peach)
        DarkRoast = 8,      // Monochrome Dark (Obsidian / neutral graphite / chalk white)
        SeaAndSalt = 9      // DuoTone Dark (Deep sea navy / amber gold syntax)
    };

    static constexpr size_t kPresetCount = 10;

    struct PresetMetadata {
        Preset preset;
        std::string name;
        std::string subtitle;
        std::string category; // "Dark", "Light", "Syntax Port", "Minimal / DuoTone", "Hardware"
        std::string description;
        std::string author;
    };

    /// Build a complete ThemeTokens table algorithmically from a ThemeSeed
    static ThemeTokens buildTokens(const ThemeSeed& seed);

    /// Get tokens for a specific preset
    static const ThemeTokens& get(Preset preset);

    /// Get tokens for active preset
    static const ThemeTokens& current();

    /// Set active preset
    static void setPreset(Preset preset);

    /// Get current preset enum
    static Preset getCurrentPreset();

    /// Get descriptive metadata for all presets
    static const std::vector<ThemeTokens>& getAllPresets();

    /// Get rich catalog metadata for theme browsing
    static const std::vector<PresetMetadata>& getPresetCatalog();

    /// Global Aesthetic & Performance Override (e.g. Minimalist GUI Mode)
    static void setControlMode(ControlAestheticMode mode);
    static ControlAestheticMode getControlMode();
};

} // namespace eatsbits::ui
