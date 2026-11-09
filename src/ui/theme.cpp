#include "eatsbits/ui/theme.hpp"
#include <array>
#include <mutex>

namespace eatsbits::ui {

namespace {

float hue2rgb(float p, float q, float t) noexcept {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

} // anonymous namespace

Color::Hsl Color::toHsl() const noexcept {
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    Hsl hsl;
    hsl.l = (maxVal + minVal) * 0.5f;

    if (delta <= 1e-5f) {
        hsl.h = 0.0f;
        hsl.s = 0.0f;
    } else {
        hsl.s = (hsl.l > 0.5f) ? (delta / (2.0f - maxVal - minVal)) : (delta / (maxVal + minVal));

        if (maxVal == r) {
            hsl.h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
        } else if (maxVal == g) {
            hsl.h = (b - r) / delta + 2.0f;
        } else {
            hsl.h = (r - g) / delta + 4.0f;
        }
        hsl.h *= 60.0f;
        if (hsl.h < 0.0f) hsl.h += 360.0f;
    }

    return hsl;
}

Color Color::fromHsl(float h, float s, float l, float a) noexcept {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    l = std::clamp(l, 0.0f, 1.0f);

    if (s <= 1e-5f) {
        return Color(l, l, l, a);
    }

    float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    float p = 2.0f * l - q;
    float hNorm = h / 360.0f;

    float r_ = hue2rgb(p, q, hNorm + 1.0f / 3.0f);
    float g_ = hue2rgb(p, q, hNorm);
    float b_ = hue2rgb(p, q, hNorm - 1.0f / 3.0f);

    return Color(r_, g_, b_, a);
}

Color Color::lighten(float amount) const noexcept {
    Hsl hsl = toHsl();
    hsl.l = std::clamp(hsl.l + amount, 0.0f, 1.0f);
    return fromHsl(hsl.h, hsl.s, hsl.l, a);
}

Color Color::darken(float amount) const noexcept {
    Hsl hsl = toHsl();
    hsl.l = std::clamp(hsl.l - amount, 0.0f, 1.0f);
    return fromHsl(hsl.h, hsl.s, hsl.l, a);
}

Color Color::saturate(float amount) const noexcept {
    Hsl hsl = toHsl();
    hsl.s = std::clamp(hsl.s + amount, 0.0f, 1.0f);
    return fromHsl(hsl.h, hsl.s, hsl.l, a);
}

Color Color::desaturate(float amount) const noexcept {
    Hsl hsl = toHsl();
    hsl.s = std::clamp(hsl.s - amount, 0.0f, 1.0f);
    return fromHsl(hsl.h, hsl.s, hsl.l, a);
}

Color Color::complementary() const noexcept {
    Hsl hsl = toHsl();
    hsl.h = std::fmod(hsl.h + 180.0f, 360.0f);
    return fromHsl(hsl.h, hsl.s, hsl.l, a);
}

Color Color::analogous(float angleDegrees) const noexcept {
    Hsl hsl = toHsl();
    hsl.h = std::fmod(hsl.h + angleDegrees, 360.0f);
    if (hsl.h < 0.0f) hsl.h += 360.0f;
    return fromHsl(hsl.h, hsl.s, hsl.l, a);
}

Color Color::blend(const Color& bg, const Color& fg, float weight) noexcept {
    weight = std::clamp(weight, 0.0f, 1.0f);
    float inv = 1.0f - weight;
    return Color(
        bg.r * inv + fg.r * weight,
        bg.g * inv + fg.g * weight,
        bg.b * inv + fg.b * weight,
        bg.a * inv + fg.a * weight
    );
}

Color Color::ensureContrast(const Color& bg, float minRatio) const noexcept {
    float bgLum = bg.luminance();
    Color candidate = *this;

    for (int step = 0; step < 10; ++step) {
        float cLum = candidate.luminance();
        float l1 = std::max(bgLum, cLum);
        float l2 = std::min(bgLum, cLum);
        float ratio = (l1 + 0.05f) / (l2 + 0.05f);
        if (ratio >= minRatio) {
            return candidate;
        }
        if (bg.isDark()) {
            candidate = candidate.lighten(0.1f);
        } else {
            candidate = candidate.darken(0.1f);
        }
    }
    return candidate;
}

// ----------------------------------------------------------------------------
// Theme Derivation Engine
// ----------------------------------------------------------------------------

ThemeTokens Theme::buildTokens(const ThemeSeed& seed) {
    ThemeTokens t;
    t.name = seed.name;
    t.description = seed.description;
    t.isLight = seed.isLight;

    // 1. Chassis & Panel Backgrounds
    if (!seed.isLight) {
        t.backgroundDark            = seed.overrideBackgroundDark.value_or(seed.chassisBase.darken(0.35f));
        t.panelBackground           = seed.chassisBase;
        t.panelHeader               = seed.overridePanelHeader.value_or(seed.chassisBase.lighten(0.12f));
        t.panelHeaderGradientTop    = t.panelHeader.lighten(0.06f);
        t.panelHeaderGradientBottom = t.panelHeader.darken(0.06f);
        t.controlBackground         = seed.chassisBase.lighten(0.05f);
        t.controlWell               = seed.overrideControlWell.value_or(seed.chassisBase.darken(0.20f));
        t.borderSubtle              = seed.chassisBase.lighten(0.18f);
    } else {
        t.backgroundDark            = seed.overrideBackgroundDark.value_or(seed.chassisBase.darken(0.04f));
        t.panelBackground           = seed.chassisBase;
        t.panelHeader               = seed.overridePanelHeader.value_or(seed.chassisBase.darken(0.08f));
        t.panelHeaderGradientTop    = t.panelHeader.lighten(0.03f);
        t.panelHeaderGradientBottom = t.panelHeader.darken(0.04f);
        t.controlBackground         = seed.chassisBase.darken(0.05f);
        t.controlWell               = seed.overrideControlWell.value_or(seed.chassisBase.darken(0.14f));
        t.borderSubtle              = seed.chassisBase.darken(0.18f);
    }

    // 2. Accents & Interactions
    t.primaryAccent    = seed.primaryAccent;
    t.secondaryAccent  = seed.secondaryAccent.value_or(seed.primaryAccent.analogous(40.0f));
    t.highlight        = seed.primaryAccent.lighten(0.12f);
    t.borderFocus      = seed.primaryAccent.withAlpha(0.85f);
    t.playActive       = Color::fromHex(0x00D95A); // Pro audio green
    t.stopActive       = Color::fromHex(0x718096); // Slate
    t.recordActive     = Color::fromHex(0xFF334B); // Red
    t.muteActive       = Color::fromHex(0xFF3B30); // Alert Red
    t.soloActive       = Color::fromHex(0xFFCC00); // Amber Yellow

    // 3. Displays & Nixie Tubes
    t.lcdBackground = seed.overrideLcdBackground.value_or(t.backgroundDark);
    t.lcdBorder     = seed.overrideLcdBorder.value_or(t.borderSubtle.darken(0.05f));
    t.lcdText       = seed.overrideLcdText.value_or(seed.primaryAccent.saturate(0.1f));
    t.lcdDotGrid    = t.lcdBackground.lighten(seed.isLight ? -0.05f : 0.08f);
    t.lcdGlow       = seed.primaryAccent.withAlpha(0.40f);

    t.tempoText     = seed.overrideTempoText.value_or(seed.isLight ? seed.primaryAccent.darken(0.2f) : seed.primaryAccent.lighten(0.35f));
    t.tempoGlow     = seed.overrideTempoGlow.value_or(seed.primaryAccent.withAlpha(0.55f));

    // 4. Arranger & Piano Roll
    if (!seed.isLight) {
        t.timelineRuler   = t.panelHeader.darken(0.05f);
        t.playhead        = seed.primaryAccent.lighten(0.1f);
        t.loopRegion      = seed.primaryAccent.withAlpha(0.20f);
        t.gridLineMajor   = Color::blend(t.backgroundDark, Color::fromHex(0xFFFFFF), 0.12f);
        t.gridLineMinor   = Color::blend(t.backgroundDark, Color::fromHex(0xFFFFFF), 0.05f);
        t.pianoWhiteKey   = Color::fromHex(0xEDEAE5);
        t.pianoBlackKey   = Color::fromHex(0x1B1917);
        t.pianoLaneDark   = t.backgroundDark;
        t.pianoLaneLight  = t.backgroundDark.lighten(0.035f);
        t.noteDefault     = seed.primaryAccent;
        t.noteSelected    = Color::fromHex(0xFFFFFF);
    } else {
        t.timelineRuler   = t.panelHeader.darken(0.03f);
        t.playhead        = seed.primaryAccent;
        t.loopRegion      = seed.primaryAccent.withAlpha(0.25f);
        t.gridLineMajor   = Color::blend(t.backgroundDark, Color::fromHex(0x000000), 0.18f);
        t.gridLineMinor   = Color::blend(t.backgroundDark, Color::fromHex(0x000000), 0.07f);
        t.pianoWhiteKey   = Color::fromHex(0xFFFFFF);
        t.pianoBlackKey   = Color::fromHex(0x2D3748);
        t.pianoLaneDark   = t.backgroundDark.darken(0.03f);
        t.pianoLaneLight  = t.backgroundDark;
        t.noteDefault     = seed.primaryAccent;
        t.noteSelected    = Color::fromHex(0x0F172A);
    }

    // 5. Code Editor & Scripting
    if (!seed.isLight) {
        t.codeEditorBg         = seed.overrideCodeEditorBg.value_or(t.backgroundDark.darken(0.05f));
        t.codeEditorGutter     = t.codeEditorBg.lighten(0.035f);
        t.codeEditorText       = seed.primaryAccent.lighten(0.30f);
        t.codeEditorGutterText = Color::fromHex(0x535D6E);
        t.codeEditorBorder     = t.borderSubtle;
        t.syntaxKeyword        = seed.overrideSyntaxKeyword.value_or(seed.primaryAccent.lighten(0.15f));
        t.syntaxString         = seed.overrideSyntaxString.value_or(Color::fromHex(0x85E89D));
        t.syntaxNumber         = seed.overrideSyntaxNumber.value_or(Color::fromHex(0xFFAB70));
        t.syntaxComment        = seed.overrideSyntaxComment.value_or(Color::fromHex(0x6A737D));
        t.syntaxFunction       = seed.overrideSyntaxFunction.value_or(seed.secondaryAccent.value_or(Color::fromHex(0x79B8FF)));
        t.syntaxIdentifier     = seed.overrideSyntaxIdentifier.value_or(t.textPrimary);
        t.syntaxOperator       = seed.overrideSyntaxOperator.value_or(seed.primaryAccent);
        t.syntaxType           = seed.overrideSyntaxType.value_or(Color::fromHex(0xB392F0));
    } else {
        t.codeEditorBg         = seed.overrideCodeEditorBg.value_or(Color::fromHex(0xFFFFFF));
        t.codeEditorGutter     = Color::fromHex(0xF1F5F9);
        t.codeEditorText       = Color::fromHex(0x0F172A);
        t.codeEditorGutterText = Color::fromHex(0x94A3B8);
        t.codeEditorBorder     = Color::fromHex(0xCBD5E1);
        t.syntaxKeyword        = seed.overrideSyntaxKeyword.value_or(Color::fromHex(0xD73A49));
        t.syntaxString         = seed.overrideSyntaxString.value_or(Color::fromHex(0x22863A));
        t.syntaxNumber         = seed.overrideSyntaxNumber.value_or(Color::fromHex(0x005CC5));
        t.syntaxComment        = seed.overrideSyntaxComment.value_or(Color::fromHex(0x6A737D));
        t.syntaxFunction       = seed.overrideSyntaxFunction.value_or(Color::fromHex(0x6F42C1));
        t.syntaxIdentifier     = seed.overrideSyntaxIdentifier.value_or(Color::fromHex(0x24292E));
        t.syntaxOperator       = seed.overrideSyntaxOperator.value_or(Color::fromHex(0xD73A49));
        t.syntaxType           = seed.overrideSyntaxType.value_or(Color::fromHex(0xE36209));
    }

    // 6. Typography
    if (!seed.isLight) {
        t.textPrimary   = seed.overrideTextPrimary.value_or(Color::fromHex(0xF0F4F8));
        t.textSecondary = seed.overrideTextSecondary.value_or(Color::fromHex(0x8E9BAE));
        t.textMuted     = seed.overrideTextMuted.value_or(Color::fromHex(0x535D6E));
        t.textInverse   = Color::fromHex(0x0F172A);
    } else {
        t.textPrimary   = seed.overrideTextPrimary.value_or(Color::fromHex(0x0F172A));
        t.textSecondary = seed.overrideTextSecondary.value_or(Color::fromHex(0x334155));
        t.textMuted     = seed.overrideTextMuted.value_or(Color::fromHex(0x64748B));
        t.textInverse   = Color::fromHex(0xF0F4F8);
    }

    // 7. Contextual 2.5D Lighting & Shadows
    if (seed.overrideGlobalLight) {
        t.globalLight = *seed.overrideGlobalLight;
    } else {
        if (seed.isLight) {
            // Bright daylight studio: higher virtual elevation, softer shadows
            t.globalLight = LightSource2D(640.0f, -220.0f, 600.0f, 0.70f);
        } else {
            // Vintage hardware rack: focused warm studio spot lamp
            t.globalLight = LightSource2D(640.0f, -180.0f, 500.0f, 0.88f);
        }
    }

    return t;
}

// ----------------------------------------------------------------------------
// Curated Eatsbeats Presets
// ----------------------------------------------------------------------------

namespace {

struct PresetRegistry {
    std::vector<ThemeTokens> presets;
    std::vector<Theme::PresetMetadata> catalog;
    Theme::Preset currentPreset{Theme::Preset::AteTrack};
    ControlAestheticMode controlMode{ControlAestheticMode::AutoThemeDefault};
    std::mutex mutex;

    PresetRegistry() {
        presets.resize(Theme::kPresetCount);

        // 1. Preset 0: Ate Track (Default Vintage Hardware Console)
        {
            ThemeSeed seed;
            seed.name = "Ate Track";
            seed.description = "80s 8-Track Vintage Hardware Console (Aged chassis, warm amber nixie glow)";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x25211C); // Aged warm metal chassis surface
            seed.primaryAccent = Color::fromHex(0xFFFF8C00); // Warm amber / vintage nixie glow
            seed.secondaryAccent = Color::fromHex(0xFFFFB700); // Warm antique gold

            // Artisanal overrides for authentic 80s hardware look (sepia tan, brown, gold, zero blue)
            seed.overrideBackgroundDark = Color::fromHex(0x141210); // Weathered vintage rack dark background
            seed.overridePanelHeader    = Color::fromHex(0x363028); // Dark warm brushed metallic header
            seed.overrideControlWell    = Color::fromHex(0x181614); // Recessed control well background
            seed.overrideTempoText      = Color::fromHex(0xFFFFF2D6); // Warm glowing amber-cream
            seed.overrideTempoGlow      = Color::fromHex(0xFFFF8C00); // Vintage amber glow bloom
            seed.overrideLcdBackground  = Color::fromHex(0x14110E); // Smoked warm amber glass
            seed.overrideLcdBorder      = Color::fromHex(0x423627); // Aged bronze/brass bezel
            seed.overrideLcdText        = Color::fromHex(0xFFFFB347); // Vintage amber LCD pixels
            seed.overrideCodeEditorBg   = Color::fromHex(0x141210);
            seed.overrideTextPrimary    = Color::fromHex(0xF6F0E6); // Warm parchment cream
            seed.overrideTextSecondary  = Color::fromHex(0xC4B49E); // Warm sepia tan (replaces cold blue-grey)
            seed.overrideTextMuted      = Color::fromHex(0x7A6E5E); // Warm deep sepia

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFFFF8C00); // Warm amber
            seed.overrideSyntaxString     = Color::fromHex(0xFF85E89D); // Phosphor green
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFFFAB70); // Warm tube orange
            seed.overrideSyntaxComment    = Color::fromHex(0xFF7A6E5E); // Warm sepia
            seed.overrideSyntaxFunction   = Color::fromHex(0xFFFFB700); // Antique gold
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFF6F0E6); // Cream
            seed.overrideSyntaxOperator   = Color::fromHex(0xFFFF8C00);
            seed.overrideSyntaxType       = Color::fromHex(0xFFFFC66D);

            presets[static_cast<size_t>(Theme::Preset::AteTrack)] = Theme::buildTokens(seed);
        }

        // 2. Preset 1: Midnight Bites (Obsidian Cyber Neon / Midnight Munchies)
        {
            ThemeSeed seed;
            seed.name = "Midnight Bites";
            seed.description = "Obsidian dark cyber theme with electric neon cyan and glowing magenta";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x101010);
            seed.primaryAccent = Color::fromHex(0x21F4E8); // EatsBeats signature neon cyan
            seed.secondaryAccent = Color::fromHex(0xFFFF007A); // Glowing magenta

            seed.overrideBackgroundDark = Color::fromHex(0x000000); // Pure black
            seed.overridePanelHeader    = Color::fromHex(0x181818);
            seed.overrideControlWell    = Color::fromHex(0x222222);
            seed.overrideTempoText      = Color::fromHex(0xE0FFFF); // Electric neon cyan-white
            seed.overrideTempoGlow      = Color::fromHex(0x21F4E8);
            seed.overrideLcdBackground  = Color::fromHex(0x080D11);
            seed.overrideLcdBorder      = Color::fromHex(0x132A32);
            seed.overrideLcdText        = Color::fromHex(0x38EEDD); // Neon cyan LCD pixels
            seed.overrideCodeEditorBg   = Color::fromHex(0x08090C);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFFFF007A); // Neon magenta
            seed.overrideSyntaxString     = Color::fromHex(0xFF21F4E8); // Neon cyan
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFFFE600); // Electric yellow
            seed.overrideSyntaxComment    = Color::fromHex(0xFF4E566A); // Dark slate
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF00F0FF); // Bright laser cyan
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFE2E8F0);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFFFF007A);
            seed.overrideSyntaxType       = Color::fromHex(0xFFA855F7);

            presets[static_cast<size_t>(Theme::Preset::MidnightBites)] = Theme::buildTokens(seed);
        }

        // 3. Preset 2: Light Snack (Bright Studio Light)
        {
            ThemeSeed seed;
            seed.name = "Light Snack";
            seed.description = "Bright high-contrast daylight studio theme for maximum visibility";
            seed.isLight = true;
            seed.chassisBase   = Color::fromHex(0xFFFFFF); // Pure white panel
            seed.primaryAccent = Color::fromHex(0x007799); // Deep contrast teal
            seed.secondaryAccent = Color::fromHex(0x0284C7);

            seed.overrideBackgroundDark = Color::fromHex(0xF4F6F9); // Crisp light background
            seed.overridePanelHeader    = Color::fromHex(0xE2E8F0); // Light slate header
            seed.overrideControlWell    = Color::fromHex(0xCBD5E1); // Soft control fill
            seed.overrideTempoText      = Color::fromHex(0x0F172A);
            seed.overrideTempoGlow      = Color::fromHex(0x00B4D8);
            seed.overrideLcdBackground  = Color::fromHex(0xE2E8F0);
            seed.overrideLcdBorder      = Color::fromHex(0x94A3B8);
            seed.overrideLcdText        = Color::fromHex(0x0F172A); // High-contrast studio slate
            seed.overrideTextPrimary    = Color::fromHex(0x0F172A);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFF0284C7); // Sky blue
            seed.overrideSyntaxString     = Color::fromHex(0xFF16A34A); // Studio green
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFD97706); // Amber
            seed.overrideSyntaxComment    = Color::fromHex(0xFF64748B); // Slate
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF007799); // Deep teal
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFF0F172A);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFF475569);
            seed.overrideSyntaxType       = Color::fromHex(0xFF9333EA);

            presets[static_cast<size_t>(Theme::Preset::LightSnack)] = Theme::buildTokens(seed);
        }

        // 4. Preset 3: Breakfast (Solarized Light)
        {
            ThemeSeed seed;
            seed.name = "Breakfast";
            seed.description = "Solarized light theme with warm parchment and gold nixie glow";
            seed.isLight = true;
            seed.chassisBase   = Color::fromHex(0xEEE8D5); // Solarized base2
            seed.primaryAccent = Color::fromHex(0xB58900); // Solarized yellow/gold
            seed.secondaryAccent = Color::fromHex(0x2AA198); // Solarized cyan

            seed.overrideBackgroundDark = Color::fromHex(0xFDF6E3); // Solarized base3 light
            seed.overridePanelHeader    = Color::fromHex(0xE0D8C3);
            seed.overrideControlWell    = Color::fromHex(0xE4DCC8);
            seed.overrideTempoText      = Color::fromHex(0x586E75); // Solarized base01
            seed.overrideTempoGlow      = Color::fromHex(0xB58900);
            seed.overrideLcdBackground  = Color::fromHex(0xE8E2CF);
            seed.overrideLcdBorder      = Color::fromHex(0xB58900);
            seed.overrideLcdText        = Color::fromHex(0x586E75);
            seed.overrideTextPrimary    = Color::fromHex(0x073642); // Solarized base02
            seed.overrideCodeEditorBg   = Color::fromHex(0xFDF6E3); // Solarized base3

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFF859900); // Solarized green
            seed.overrideSyntaxString     = Color::fromHex(0xFF2AA198); // Solarized cyan
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFCB4B16); // Solarized orange
            seed.overrideSyntaxComment    = Color::fromHex(0xFF93A1A1); // Solarized base1
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF268BD2); // Solarized blue
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFF657B83); // Solarized base00
            seed.overrideSyntaxOperator   = Color::fromHex(0xFFB58900); // Solarized yellow
            seed.overrideSyntaxType       = Color::fromHex(0xFF6C71C4); // Solarized violet

            presets[static_cast<size_t>(Theme::Preset::Breakfast)] = Theme::buildTokens(seed);
        }

        // 5. Preset 4: Dinner (Solarized Dark)
        {
            ThemeSeed seed;
            seed.name = "Dinner";
            seed.description = "Solarized dark theme with deep oceanic teal and cyan accents";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x073642); // Solarized base02
            seed.primaryAccent = Color::fromHex(0x2AA198); // Solarized cyan
            seed.secondaryAccent = Color::fromHex(0x268BD2); // Solarized blue

            seed.overrideBackgroundDark = Color::fromHex(0x002B36); // Solarized base03
            seed.overridePanelHeader    = Color::fromHex(0x0A4858);
            seed.overrideControlWell    = Color::fromHex(0x001F27);
            seed.overrideTempoText      = Color::fromHex(0xEEE8D5); // Solarized base2
            seed.overrideTempoGlow      = Color::fromHex(0x2AA198);
            seed.overrideLcdBackground  = Color::fromHex(0x00212B);
            seed.overrideLcdBorder      = Color::fromHex(0x0A4858);
            seed.overrideLcdText        = Color::fromHex(0x2AA198);
            seed.overrideTextPrimary    = Color::fromHex(0xEEE8D5);
            seed.overrideCodeEditorBg   = Color::fromHex(0x002B36); // Solarized base03

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFF859900); // Solarized green
            seed.overrideSyntaxString     = Color::fromHex(0xFF2AA198); // Solarized cyan
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFCB4B16); // Solarized orange
            seed.overrideSyntaxComment    = Color::fromHex(0xFF586E75); // Solarized base01
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF268BD2); // Solarized blue
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFF839496); // Solarized base0
            seed.overrideSyntaxOperator   = Color::fromHex(0xFFB58900); // Solarized yellow
            seed.overrideSyntaxType       = Color::fromHex(0xFF6C71C4); // Solarized violet

            presets[static_cast<size_t>(Theme::Preset::Dinner)] = Theme::buildTokens(seed);
        }

        // 6. Preset 5: Count's Bite (Dracula)
        {
            ThemeSeed seed;
            seed.name = "Count's Bite";
            seed.description = "Dracula-inspired dark gothic slate with vibrant purple, pink, and cyan accents";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x282A36); // Dracula background
            seed.primaryAccent = Color::fromHex(0xFFBD93F9); // Dracula purple
            seed.secondaryAccent = Color::fromHex(0xFFFF79C6); // Dracula pink

            seed.overrideBackgroundDark = Color::fromHex(0x1E1F29);
            seed.overridePanelHeader    = Color::fromHex(0x343746);
            seed.overrideControlWell    = Color::fromHex(0x21222C);
            seed.overrideTempoText      = Color::fromHex(0xFFF8F8F2);
            seed.overrideTempoGlow      = Color::fromHex(0xFFBD93F9);
            seed.overrideLcdBackground  = Color::fromHex(0x191A21);
            seed.overrideLcdBorder      = Color::fromHex(0x44475A);
            seed.overrideLcdText        = Color::fromHex(0xFF8BE9FD); // Dracula cyan
            seed.overrideCodeEditorBg   = Color::fromHex(0x282A36);
            seed.overrideTextPrimary    = Color::fromHex(0xFFF8F8F2);
            seed.overrideTextSecondary  = Color::fromHex(0xFF6272A4); // Dracula comment/slate
            seed.overrideTextMuted      = Color::fromHex(0xFF44475A);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFFFF79C6); // Pink
            seed.overrideSyntaxString     = Color::fromHex(0xFFF1FA8C); // Yellow
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFBD93F9); // Purple
            seed.overrideSyntaxComment    = Color::fromHex(0xFF6272A4); // Muted slate-blue
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF50FA7B); // Green
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFF8F8F2);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFFFF79C6);
            seed.overrideSyntaxType       = Color::fromHex(0xFF8BE9FD); // Cyan

            presets[static_cast<size_t>(Theme::Preset::CountsBite)] = Theme::buildTokens(seed);
        }

        // 7. Preset 6: Nordic Frost (Nord)
        {
            ThemeSeed seed;
            seed.name = "Nordic Frost";
            seed.description = "Arctic polar night with soothing frost blues and aurora green highlights";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x2E3440); // Polar Night
            seed.primaryAccent = Color::fromHex(0xFF88C0D0); // Frost cyan
            seed.secondaryAccent = Color::fromHex(0xFFA3BE8C); // Aurora green

            seed.overrideBackgroundDark = Color::fromHex(0x242933);
            seed.overridePanelHeader    = Color::fromHex(0x3B4252);
            seed.overrideControlWell    = Color::fromHex(0x2E3440);
            seed.overrideTempoText      = Color::fromHex(0xFFECEFF4); // Snow storm
            seed.overrideTempoGlow      = Color::fromHex(0xFF88C0D0);
            seed.overrideLcdBackground  = Color::fromHex(0x20242C);
            seed.overrideLcdBorder      = Color::fromHex(0x434C5E);
            seed.overrideLcdText        = Color::fromHex(0xFF8FBCBB);
            seed.overrideCodeEditorBg   = Color::fromHex(0x2E3440);
            seed.overrideTextPrimary    = Color::fromHex(0xFFECEFF4);
            seed.overrideTextSecondary  = Color::fromHex(0xFFD8DEE9);
            seed.overrideTextMuted      = Color::fromHex(0xFF616E88);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFF81A1C1); // Nord blue
            seed.overrideSyntaxString     = Color::fromHex(0xFFA3BE8C); // Aurora green
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFB48EAD); // Aurora magenta
            seed.overrideSyntaxComment    = Color::fromHex(0xFF616E88);
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF88C0D0); // Frost cyan
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFECEFF4);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFF81A1C1);
            seed.overrideSyntaxType       = Color::fromHex(0xFF8FBCBB);

            presets[static_cast<size_t>(Theme::Preset::NordicFrost)] = Theme::buildTokens(seed);
        }

        // 8. Preset 7: Catppuccino (Catppuccin Mocha)
        {
            ThemeSeed seed;
            seed.name = "Catppuccino";
            seed.description = "Cozy cafe cocoa aesthetic with soothing pastel lavender, peach, and mauve";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x1E1E2E); // Mocha Base
            seed.primaryAccent = Color::fromHex(0xFFB4BEFE); // Lavender
            seed.secondaryAccent = Color::fromHex(0xFFFAB387); // Peach

            seed.overrideBackgroundDark = Color::fromHex(0x11111B); // Crust
            seed.overridePanelHeader    = Color::fromHex(0x181825); // Mantle
            seed.overrideControlWell    = Color::fromHex(0x313244); // Surface0
            seed.overrideTempoText      = Color::fromHex(0xFFCDD6F4);
            seed.overrideTempoGlow      = Color::fromHex(0xFFB4BEFE);
            seed.overrideLcdBackground  = Color::fromHex(0x181825);
            seed.overrideLcdBorder      = Color::fromHex(0x45475A);
            seed.overrideLcdText        = Color::fromHex(0xFF94E2D5); // Teal
            seed.overrideCodeEditorBg   = Color::fromHex(0x1E1E2E);
            seed.overrideTextPrimary    = Color::fromHex(0xFFCDD6F4);
            seed.overrideTextSecondary  = Color::fromHex(0xFFA6ADC8);
            seed.overrideTextMuted      = Color::fromHex(0xFF6C7086);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFFCBA6F7); // Mauve
            seed.overrideSyntaxString     = Color::fromHex(0xFFA6E3A1); // Green
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFFAB387); // Peach
            seed.overrideSyntaxComment    = Color::fromHex(0xFF6C7086);
            seed.overrideSyntaxFunction   = Color::fromHex(0xFF89B4FA); // Blue
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFCDD6F4);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFF94E2D5); // Teal
            seed.overrideSyntaxType       = Color::fromHex(0xFFF9E2AF); // Yellow

            presets[static_cast<size_t>(Theme::Preset::Catppuccino)] = Theme::buildTokens(seed);
        }

        // 9. Preset 8: Dark Roast (Monochrome Dark)
        {
            ThemeSeed seed;
            seed.name = "Dark Roast";
            seed.description = "High-contrast distraction-free obsidian monochrome with pure chalk accents";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x181818);
            seed.primaryAccent = Color::fromHex(0xFFE0E0E0); // Chalk white
            seed.secondaryAccent = Color::fromHex(0xFFB0B0B0); // Silver

            seed.overrideBackgroundDark = Color::fromHex(0x101010);
            seed.overridePanelHeader    = Color::fromHex(0x222222);
            seed.overrideControlWell    = Color::fromHex(0x141414);
            seed.overrideTempoText      = Color::fromHex(0xFFFFFFFF);
            seed.overrideTempoGlow      = Color::fromHex(0xFFE0E0E0);
            seed.overrideLcdBackground  = Color::fromHex(0x0C0C0C);
            seed.overrideLcdBorder      = Color::fromHex(0x333333);
            seed.overrideLcdText        = Color::fromHex(0xFFEDEDED);
            seed.overrideCodeEditorBg   = Color::fromHex(0x121212);
            seed.overrideTextPrimary    = Color::fromHex(0xFFEDEDED);
            seed.overrideTextSecondary  = Color::fromHex(0xFFB0B0B0);
            seed.overrideTextMuted      = Color::fromHex(0xFF666666);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFFFFFFFF); // Bold white
            seed.overrideSyntaxString     = Color::fromHex(0xFFCCCCCC);
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFE0E0E0);
            seed.overrideSyntaxComment    = Color::fromHex(0xFF555555);
            seed.overrideSyntaxFunction   = Color::fromHex(0xFFFFFFFF);
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFD6D6D6);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFF888888);
            seed.overrideSyntaxType       = Color::fromHex(0xFFEEEEEE);

            presets[static_cast<size_t>(Theme::Preset::DarkRoast)] = Theme::buildTokens(seed);
        }

        // 10. Preset 9: Sea & Salt (DuoTone Dark)
        {
            ThemeSeed seed;
            seed.name = "Sea & Salt";
            seed.description = "Harmonious two-tone deep sea navy with amber gold syntax structure";
            seed.isLight = false;
            seed.chassisBase   = Color::fromHex(0x1D262F);
            seed.primaryAccent = Color::fromHex(0xFFFFB852); // Amber gold
            seed.secondaryAccent = Color::fromHex(0xFF64B5F6); // Sea sky blue

            seed.overrideBackgroundDark = Color::fromHex(0x141B22);
            seed.overridePanelHeader    = Color::fromHex(0x24303C);
            seed.overrideControlWell    = Color::fromHex(0x19212A);
            seed.overrideTempoText      = Color::fromHex(0xFFFFD599);
            seed.overrideTempoGlow      = Color::fromHex(0xFFFFB852);
            seed.overrideLcdBackground  = Color::fromHex(0x10161C);
            seed.overrideLcdBorder      = Color::fromHex(0x2C3B49);
            seed.overrideLcdText        = Color::fromHex(0xFFFFB852);
            seed.overrideCodeEditorBg   = Color::fromHex(0x182028);
            seed.overrideTextPrimary    = Color::fromHex(0xFFE2EAF2);
            seed.overrideTextSecondary  = Color::fromHex(0xFF8CA3BA);
            seed.overrideTextMuted      = Color::fromHex(0xFF4E657D);

            seed.overrideSyntaxKeyword    = Color::fromHex(0xFFFFB852); // Amber gold
            seed.overrideSyntaxString     = Color::fromHex(0xFF81D4FA); // Cool sea cyan
            seed.overrideSyntaxNumber     = Color::fromHex(0xFFFFCC80);
            seed.overrideSyntaxComment    = Color::fromHex(0xFF4E657D); // Deep slate
            seed.overrideSyntaxFunction   = Color::fromHex(0xFFFFD599);
            seed.overrideSyntaxIdentifier = Color::fromHex(0xFFCFDCE8);
            seed.overrideSyntaxOperator   = Color::fromHex(0xFF64B5F6);
            seed.overrideSyntaxType       = Color::fromHex(0xFFFFB852);

            presets[static_cast<size_t>(Theme::Preset::SeaAndSalt)] = Theme::buildTokens(seed);
        }

        // Initialize Rich Metadata Catalog
        catalog = {
            { Theme::Preset::AteTrack, "Ate Track", "1980s Vintage Hardware Console", "Hardware",
              "Aged metal chassis, smoked amber displays, and warm glowing nixie tubes.", "Eatsbits Original" },
            { Theme::Preset::MidnightBites, "Midnight Bites", "Cyberpunk Neon / Midnight Munchies", "Dark",
              "Obsidian OLED black chassis with electric neon cyan and glowing magenta highlights.", "Eatsbits Original" },
            { Theme::Preset::LightSnack, "Light Snack", "Daylight Studio Environment", "Light",
              "High-contrast crisp white and slate studio console for bright environments.", "Eatsbits Original" },
            { Theme::Preset::Breakfast, "Breakfast", "Solarized Light Heritage", "Syntax Port",
              "Ethan Schoonover's mathematical 16-color daylight palette on warm parchment.", "Ethan Schoonover" },
            { Theme::Preset::Dinner, "Dinner", "Solarized Dark Heritage", "Syntax Port",
              "Deep oceanic teal and cyan syntax palette engineered for late-night mixing.", "Ethan Schoonover" },
            { Theme::Preset::CountsBite, "Count's Bite", "Dracula IDE Heritage", "Syntax Port",
              "Gothic dark slate chassis with vibrant Dracula purple, neon pink, and cyan accents.", "Zeno Rocha" },
            { Theme::Preset::NordicFrost, "Nordic Frost", "Nord Arctic Heritage", "Syntax Port",
              "Soothing polar night with arctic frost blues and aurora green highlights for zero eye strain.", "Arctic Ice Studio" },
            { Theme::Preset::Catppuccino, "Catppuccin Mocha", "Cozy Cocoa Cafe Heritage", "Syntax Port",
              "Warm, pastel-tuned mocha base with lavender, peach, and soft teal highlights.", "Catppuccin Org" },
            { Theme::Preset::DarkRoast, "Dark Roast", "Pure Obsidian Monochrome", "Minimal / DuoTone",
              "Distraction-free black, graphite, and pure chalk white with zero chromatic clutter.", "anotherglitchinthematrix" },
            { Theme::Preset::SeaAndSalt, "Sea & Salt", "DuoTone Dark Harmony", "Minimal / DuoTone",
              "Calm two-tone palette pairing deep sea navy chassis with amber gold script syntax.", "Simurai / Sallar" }
        };
    }
};

PresetRegistry& getRegistry() {
    static PresetRegistry registry;
    return registry;
}

} // anonymous namespace

const ThemeTokens& Theme::get(Preset preset) {
    auto& reg = getRegistry();
    size_t idx = static_cast<size_t>(preset);
    if (idx >= reg.presets.size()) {
        idx = 0;
    }
    return reg.presets[idx];
}

const ThemeTokens& Theme::current() {
    auto& reg = getRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return get(reg.currentPreset);
}

void Theme::setPreset(Preset preset) {
    auto& reg = getRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.currentPreset = preset;
}

Theme::Preset Theme::getCurrentPreset() {
    auto& reg = getRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.currentPreset;
}

const std::vector<ThemeTokens>& Theme::getAllPresets() {
    return getRegistry().presets;
}

const std::vector<Theme::PresetMetadata>& Theme::getPresetCatalog() {
    return getRegistry().catalog;
}

void Theme::setControlMode(ControlAestheticMode mode) {
    auto& reg = getRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.controlMode = mode;
    for (auto& p : reg.presets) {
        p.controlMode = mode;
    }
}

ControlAestheticMode Theme::getControlMode() {
    auto& reg = getRegistry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    return reg.controlMode;
}

} // namespace eatsbits::ui
