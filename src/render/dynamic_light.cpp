#include "solengine/dynamic_light.hpp"
#include <algorithm>
#include <cmath>

namespace solengine {

DynamicLightController::DynamicLightController(LightingPreset initialPreset) {
    setPreset(initialPreset);
}

void DynamicLightController::setPreset(LightingPreset preset) {
    preset_ = preset;
    switch (preset) {
        case LightingPreset::StudioSteady:
            sway_.enabled = false;
            sway_.amplitudeX = 0.0f;
            sway_.amplitudeY = 0.0f;
            sway_.speed = 0.0f;
            flicker_.mode = FlickerMode::None;
            flicker_.intensity = 0.0f;
            flicker_.dropoutChance = 0.0f;
            baseIntensity_ = 1.0f;
            colorR_ = 1.00f; colorG_ = 1.00f; colorB_ = 1.00f;
            ambientR_ = 0.38f; ambientG_ = 0.38f; ambientB_ = 0.38f;
            break;

        case LightingPreset::GentleChandelier:
            sway_.enabled = true;
            sway_.amplitudeX = 52.0f;
            sway_.amplitudeY = 20.0f;
            sway_.speed = 0.52f;
            sway_.phaseOffset = 0.38f;
            flicker_.mode = FlickerMode::GentleDraft;
            flicker_.intensity = 0.035f;
            flicker_.dropoutChance = 0.0f;
            baseIntensity_ = 1.05f;
            colorR_ = 1.06f; colorG_ = 1.00f; colorB_ = 0.92f;
            ambientR_ = 0.35f; ambientG_ = 0.35f; ambientB_ = 0.35f;
            break;

        case LightingPreset::Candlelight:
            sway_.enabled = true;
            sway_.amplitudeX = 36.0f;
            sway_.amplitudeY = 16.0f;
            sway_.speed = 0.72f;
            sway_.phaseOffset = 0.65f;
            flicker_.mode = FlickerMode::CandleGaslight;
            flicker_.intensity = 0.085f;
            flicker_.dropoutChance = 0.0f;
            baseIntensity_ = 0.98f;
            colorR_ = 1.18f; colorG_ = 0.94f; colorB_ = 0.74f;
            ambientR_ = 0.30f; ambientG_ = 0.27f; ambientB_ = 0.23f;
            break;

        case LightingPreset::HauntedManor:
            sway_.enabled = true;
            sway_.amplitudeX = 66.0f;
            sway_.amplitudeY = 26.0f;
            sway_.speed = 0.36f; // Heavy, ominous, slow pendulum
            sway_.phaseOffset = 0.45f;
            flicker_.mode = FlickerMode::EerieHorror;
            flicker_.intensity = 0.15f;
            flicker_.dropoutChance = 0.010f;
            flicker_.dropoutDip = 0.55f;
            flicker_.dropoutDuration = 0.18f;
            baseIntensity_ = 0.88f;
            colorR_ = 0.95f; colorG_ = 0.88f; colorB_ = 0.75f;
            ambientR_ = 0.24f; ambientG_ = 0.23f; ambientB_ = 0.25f;
            break;

        case LightingPreset::CyberpunkNeon:
            sway_.enabled = true;
            sway_.amplitudeX = 24.0f;
            sway_.amplitudeY = 12.0f;
            sway_.speed = 0.92f;
            sway_.phaseOffset = 0.20f;
            flicker_.mode = FlickerMode::CyberpunkGlitch;
            flicker_.intensity = 0.12f;
            flicker_.dropoutChance = 0.012f;
            flicker_.dropoutDip = 0.38f;
            flicker_.dropoutDuration = 0.09f;
            baseIntensity_ = 1.02f;
            colorR_ = 0.84f; colorG_ = 1.08f; colorB_ = 1.16f;
            ambientR_ = 0.26f; ambientG_ = 0.20f; ambientB_ = 0.32f;
            break;

        case LightingPreset::MorningWindow:
            sway_.enabled = false;
            sway_.amplitudeX = 0.0f;
            sway_.amplitudeY = 0.0f;
            sway_.speed = 0.0f;
            flicker_.mode = FlickerMode::None;
            flicker_.intensity = 0.0f;
            flicker_.dropoutChance = 0.0f;
            baseIntensity_ = 1.08f;
            colorR_ = 1.08f; colorG_ = 1.00f; colorB_ = 0.88f;
            ambientR_ = 0.42f; ambientG_ = 0.48f; ambientB_ = 0.58f;
            break;

        case LightingPreset::GoldenHour:
            baseIntensity_ = 1.12f;
            colorR_ = 1.18f; colorG_ = 0.93f; colorB_ = 0.68f;
            ambientR_ = 0.34f; ambientG_ = 0.36f; ambientB_ = 0.46f;
            break;

        case LightingPreset::Evening:
            baseIntensity_ = 1.02f;
            colorR_ = 1.16f; colorG_ = 0.90f; colorB_ = 0.66f;
            ambientR_ = 0.28f; ambientG_ = 0.26f; ambientB_ = 0.30f;
            break;

        case LightingPreset::DarkNight:
            baseIntensity_ = 0.86f;
            colorR_ = 0.78f; colorG_ = 0.88f; colorB_ = 1.12f;
            ambientR_ = 0.16f; ambientG_ = 0.18f; ambientB_ = 0.28f;
            break;
    }
    setWaver(waverLevel_);
    setFlickerLevel(flickerLevel_);
}

void DynamicLightController::setWaver(float waver) noexcept {
    waverLevel_ = std::clamp(waver, 0.0f, 1.0f);
    if (waverLevel_ <= 0.005f) {
        sway_.enabled = false;
        sway_.amplitudeX = 0.0f;
        sway_.amplitudeY = 0.0f;
        sway_.speed = 0.0f;
    } else {
        sway_.enabled = true;
        sway_.amplitudeX = 68.0f * waverLevel_;
        sway_.amplitudeY = 28.0f * waverLevel_;
        sway_.speed = 0.28f + 0.45f * waverLevel_;
        sway_.phaseOffset = 0.42f;
    }
}

void DynamicLightController::setFlickerLevel(float flicker) noexcept {
    flickerLevel_ = std::clamp(flicker, 0.0f, 1.0f);
    if (flickerLevel_ <= 0.005f) {
        flicker_.mode = FlickerMode::None;
        flicker_.intensity = 0.0f;
        flicker_.dropoutChance = 0.0f;
    } else if (flickerLevel_ <= 0.38f) {
        flicker_.mode = FlickerMode::GentleDraft;
        flicker_.intensity = 0.12f * flickerLevel_;
        flicker_.dropoutChance = 0.0f;
    } else {
        flicker_.mode = FlickerMode::CandleGaslight;
        flicker_.intensity = 0.04f + 0.16f * flickerLevel_;
        flicker_.dropoutChance = (flickerLevel_ > 0.75f) ? ((flickerLevel_ - 0.75f) * 0.025f) : 0.0f;
    }
}

void DynamicLightController::setAngle(LightAngle angle) noexcept {
    angle_ = angle;
    switch (angle) {
        case LightAngle::Overhead:
            baseXRatio_ = 0.50f;
            baseYRatio_ = 0.28f;
            baseZ_ = 550.0f;
            break;
        case LightAngle::TopLeft:
            baseXRatio_ = -0.10f;
            baseYRatio_ = -0.15f;
            baseZ_ = 360.0f;
            break;
        case LightAngle::TopRight:
            baseXRatio_ = 1.10f;
            baseYRatio_ = -0.15f;
            baseZ_ = 360.0f;
            break;
        case LightAngle::LowLeft:
            baseXRatio_ = -0.25f;
            baseYRatio_ = 0.16f;
            baseZ_ = 280.0f;
            break;
    }
}

void DynamicLightController::cyclePreset() {
    uint8_t next = (static_cast<uint8_t>(preset_) + 1) % 9;
    setPreset(static_cast<LightingPreset>(next));
}

const char* DynamicLightController::getPresetName() const noexcept {
    switch (preset_) {
        case LightingPreset::StudioSteady:     return "LIGHT: STUDIO STEADY";
        case LightingPreset::GentleChandelier: return "LIGHT: GENTLE CHANDELIER";
        case LightingPreset::Candlelight:      return "LIGHT: CANDLELIGHT";
        case LightingPreset::HauntedManor:     return "LIGHT: HAUNTED MANOR";
        case LightingPreset::CyberpunkNeon:    return "LIGHT: CYBERPUNK NEON";
        case LightingPreset::MorningWindow:    return "LIGHT: MORNING WINDOW";
        case LightingPreset::GoldenHour:       return "LIGHT: GOLDEN HOUR";
        case LightingPreset::Evening:          return "LIGHT: EVENING";
        case LightingPreset::DarkNight:        return "LIGHT: DARK NIGHT";
        default: return "LIGHT";
    }
}

void DynamicLightController::applyThemeDefault(TableThemeId theme) {
    if (preset_ == LightingPreset::StudioSteady) {
        return; // Respect user preference: keep animated lighting disabled
    }
    switch (theme) {
        case TableThemeId::SunkenSanctum:
        case TableThemeId::PixelDungeon:
            setPreset(LightingPreset::HauntedManor);
            break;
        case TableThemeId::PerilousShores:
        case TableThemeId::PebbledCowhide:
        case TableThemeId::LeatherNubuck:
        case TableThemeId::ShaderArcaneParchment:
        case TableThemeId::ShaderVelvetNoir:
            setPreset(LightingPreset::Candlelight);
            break;
        case TableThemeId::PixelWater:
        case TableThemeId::ShaderCosmicNebula:
        case TableThemeId::ShaderSynthwaveSunset:
            setPreset(LightingPreset::CyberpunkNeon);
            break;
        case TableThemeId::PastelTan:
        case TableThemeId::EmeraldLawn:
        case TableThemeId::HopsackTweed:
        case TableThemeId::DenimChambray:
        case TableThemeId::StripedJersey:
        case TableThemeId::PaintedWood:
        case TableThemeId::ShaderCarraraMarble:
        case TableThemeId::ShaderWateredSilk:
        case TableThemeId::ShaderPoolCaustics:
        case TableThemeId::ShaderTopographic:
        default:
            setPreset(LightingPreset::GentleChandelier);
            break;
    }
}

void DynamicLightController::update(float dt) {
    if (dt <= 0.0f) return;
    dt = std::min(dt, 0.1f); // Protect against large lag spikes
    elapsedTime_ += dt;

    // 1. Evaluate Pendular Sway
    if (sway_.enabled && sway_.speed > 0.0001f) {
        const float t = elapsedTime_ * sway_.speed * 6.2831853f;
        // Dual-harmonic Lissajous curve with irrational ratio (1.618 golden ratio) prevents mechanical loops
        currentSwayOffsetX_ = sway_.amplitudeX * std::sin(t);
        currentSwayOffsetY_ = sway_.amplitudeY * std::sin(t * 1.6180339f + sway_.phaseOffset);
    } else {
        currentSwayOffsetX_ = 0.0f;
        currentSwayOffsetY_ = 0.0f;
    }

    // 2. Evaluate Atmospheric Flicker & Brown-Out Dropouts
    float fMod = 1.0f;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    if (flicker_.mode == FlickerMode::GentleDraft) {
        fMod = 1.0f + std::sin(elapsedTime_ * 1.8f) * (flicker_.intensity * 0.55f)
                    + std::cos(elapsedTime_ * 3.4f) * (flicker_.intensity * 0.35f);
    } else if (flicker_.mode == FlickerMode::CandleGaslight) {
        float microJitter = dist(rng_) * 0.45f;
        float flamePulse = std::sin(elapsedTime_ * 8.5f) * 0.35f + std::sin(elapsedTime_ * 19.3f) * 0.25f;
        fMod = 1.0f + (flamePulse + microJitter) * flicker_.intensity;
    } else if (flicker_.mode == FlickerMode::EerieHorror) {
        if (dropoutActive_ > 0.0f) {
            dropoutActive_ -= dt;
            float dipPhase = std::sin((dropoutActive_ / flicker_.dropoutDuration) * 3.14159265f);
            fMod = 1.0f - (flicker_.dropoutDip * dipPhase);
            // Sudden startled twitch in light position during brownout
            currentSwayOffsetX_ += (dist(rng_) * 16.0f) * dipPhase;
            currentSwayOffsetY_ += (dist(rng_) * 10.0f) * dipPhase;
        } else {
            dropoutTimer_ += dt;
            if (dropoutTimer_ >= 0.08f) {
                dropoutTimer_ = 0.0f;
                std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                if (roll(rng_) < flicker_.dropoutChance) {
                    dropoutActive_ = flicker_.dropoutDuration;
                }
            }
            float jitter = dist(rng_) * 0.55f + std::sin(elapsedTime_ * 6.2f) * 0.45f;
            fMod = 1.0f + jitter * flicker_.intensity;
        }
    } else if (flicker_.mode == FlickerMode::CyberpunkGlitch) {
        if (dropoutActive_ > 0.0f) {
            dropoutActive_ -= dt;
            fMod = 1.0f - flicker_.dropoutDip;
        } else {
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            if (roll(rng_) < flicker_.dropoutChance) {
                dropoutActive_ = flicker_.dropoutDuration;
            }
            float fastNoise = std::sin(elapsedTime_ * 48.0f) * 0.5f + std::cos(elapsedTime_ * 115.0f) * 0.5f;
            fMod = 1.0f + fastNoise * flicker_.intensity;
        }
    }

    currentFlickerMod_ = std::clamp(fMod, 0.20f, 1.80f);
}

eatsbits::ui::LightSource2D DynamicLightController::getEffectiveLight(float viewportW, float viewportH) const noexcept {
    float cx = viewportW * baseXRatio_ + currentSwayOffsetX_;
    float cy = viewportH * baseYRatio_ + currentSwayOffsetY_;
    float effIntensity = baseIntensity_ * currentFlickerMod_;
    return eatsbits::ui::LightSource2D{cx, cy, baseZ_, effIntensity};
}

eatsbits::ui::LightSource2D DynamicLightController::getSteadyLight(float viewportW, float viewportH) const noexcept {
    return eatsbits::ui::LightSource2D{viewportW * baseXRatio_, viewportH * baseYRatio_, baseZ_, baseIntensity_};
}

std::array<float, 4> DynamicLightController::getLightColor() const noexcept {
    return {colorR_, colorG_, colorB_, baseIntensity_ * currentFlickerMod_};
}

std::array<float, 4> DynamicLightController::getAmbientColor() const noexcept {
    return {ambientR_, ambientG_, ambientB_, 1.0f};
}

} // namespace solengine

