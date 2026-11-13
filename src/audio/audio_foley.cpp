#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "solengine/audio_foley.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace solengine {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.5707963267948966f;

inline AudioFoleyEngine::BiquadCoeffs makeBandpass(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float a0 = 1.0f + alpha;
    return {
        alpha / a0,
        0.0f,
        -alpha / a0,
        (-2.0f * std::cos(w0)) / a0,
        (1.0f - alpha) / a0
    };
}

inline AudioFoleyEngine::BiquadCoeffs makeHighpass(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float cosw = std::cos(w0);
    float a0 = 1.0f + alpha;
    float b0 = (1.0f + cosw) * 0.5f;
    return {
        b0 / a0,
        -(1.0f + cosw) / a0,
        b0 / a0,
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    };
}

inline AudioFoleyEngine::BiquadCoeffs makeLowpass(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float cosw = std::cos(w0);
    float a0 = 1.0f + alpha;
    float b1 = 1.0f - cosw;
    float b0 = b1 * 0.5f;
    return {
        b0 / a0,
        b1 / a0,
        b0 / a0,
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    };
}

inline AudioFoleyEngine::BiquadCoeffs makeHighshelf(float f0, float gainDb, float sr) noexcept {
    float A = std::pow(10.0f, gainDb / 40.0f);
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float cosw = std::cos(w0);
    float sinw = std::sin(w0);
    float alpha = sinw * 0.70710678f; // S = 1
    float sqrtA2Alpha = 2.0f * std::sqrt(A) * alpha;

    float a0 = (A + 1.0f) - (A - 1.0f) * cosw + sqrtA2Alpha;
    float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw + sqrtA2Alpha);
    float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw);
    float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw - sqrtA2Alpha);
    float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw);
    float a2 = (A + 1.0f) - (A - 1.0f) * cosw - sqrtA2Alpha;

    return {
        b0 / a0,
        b1 / a0,
        b2 / a0,
        a1 / a0,
        a2 / a0
    };
}

inline AudioFoleyEngine::BiquadCoeffs makeNotch(float f0, float Q, float sr) noexcept {
    float w0 = kTwoPi * std::clamp(f0, 20.0f, sr * 0.45f) / sr;
    float alpha = std::sin(w0) / (2.0f * std::max(0.1f, Q));
    float cosw = std::cos(w0);
    float a0 = 1.0f + alpha;
    return {
        1.0f / a0,
        (-2.0f * cosw) / a0,
        1.0f / a0,
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    };
}

    // 16th-order Linear Predictive Coding coefficients trained on cardsounds/deal-series.wav
    // Captures cellulose resonance formants (3.0kHz, 6.1kHz, 11.0kHz, 13.7kHz) with LSD 0.96 dB
    static constexpr std::array<float, 16> kLpcDeal = {
        -1.534673f, +1.803655f, -1.885687f, +1.771341f, -1.613252f, +1.467351f, -1.373514f, +1.182798f,
        -1.019356f, +0.838832f, -0.643898f, +0.485627f, -0.384877f, +0.203584f, -0.091463f, -0.013349f
    };
    static constexpr float kLpcDealGain = 0.0820f;

void miniaudioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioFoleyEngine*>(pDevice->pUserData);
    if (engine && pOutput) {
        engine->mixSamples(static_cast<float*>(pOutput), frameCount, pDevice->sampleRate);
    }
}
} // namespace

AudioFoleyEngine::AudioFoleyEngine() {
    velvetNoiseBuffer_.resize(kNoiseBufferSize, 0.0f);
    continuousNoiseBuffer_.resize(kNoiseBufferSize, 0.0f);
    regenerateVelvetBuffer(4200.0f, 48000.0f);
    generateContinuousNoise(48000.0f);

    populateDefaultBank(soundBanks_[0]);
    soundBanks_[1] = soundBanks_[0];
    activeBankIndex_.store(0, std::memory_order_relaxed);
}

AudioFoleyEngine::~AudioFoleyEngine() {
    shutdown();
}

void AudioFoleyEngine::regenerateVelvetBuffer(float density, float sampleRate) {
    if (velvetNoiseBuffer_.size() < kNoiseBufferSize) {
        velvetNoiseBuffer_.resize(kNoiseBufferSize, 0.0f);
    }
    std::fill(velvetNoiseBuffer_.begin(), velvetNoiseBuffer_.end(), 0.0f);
    const float avgInterval = sampleRate / std::clamp(density, 500.0f, 20000.0f);
    size_t cursor = 0;
    while (cursor < velvetNoiseBuffer_.size()) {
        float randVal = static_cast<float>(nextNoise() * 0.5f + 0.5f);
        size_t step = static_cast<size_t>(avgInterval * (0.55f + randVal * 0.90f));
        cursor += std::max<size_t>(1, step);
        if (cursor < velvetNoiseBuffer_.size()) {
            float sign = (nextNoise() > 0.0f) ? 1.0f : -1.0f;
            float amp = 0.5f + std::abs(nextNoise()) * 0.5f;
            velvetNoiseBuffer_[cursor] = sign * amp;
        }
    }
}

void AudioFoleyEngine::generateContinuousNoise(float /*sampleRate*/) {
    if (continuousNoiseBuffer_.size() < kNoiseBufferSize) {
        continuousNoiseBuffer_.resize(kNoiseBufferSize, 0.0f);
    }
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    for (size_t i = 0; i < continuousNoiseBuffer_.size(); ++i) {
        float white = nextNoise();
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.1600000f;
        b2 = 0.57000f * b2 + white * 0.5692600f;
        continuousNoiseBuffer_[i] = (b0 + b1 + b2 + white * 0.5362f) * 0.15f;
    }
}

void AudioFoleyEngine::populateDefaultBank(FoleySoundBank& bank) {
    bank.masterVariance = 1.0f;
    bank.physicalParams = {4200.0f, 0.85f, 5.0f, 0.00018f, 7500.0f, 3200.0f, 0.0f, 1.5f, 0.30f};
    bank.customSounds.clear();
    bank.customSequences.clear();

    // Snap baseline (Dirac Edge Snap)
    {
        auto& sn = bank.coreSounds[FoleySound::Snap];
        sn.name = "snap";
        sn.soundId = FoleySound::Snap;
        sn.isCustom = false;
        sn.hasModalResonators = false;
        sn.durationBase = 0.045f;
        sn.durationDev = 0.08f;
        sn.gainBase = 1.0f;
        sn.gainDev = 0.05f;
        sn.panBase = 0.0f;
        sn.panDev = 0.04f;
    }

    // Deal baseline (Quick deal: Snap + trailing slide)
    {
        auto& d = bank.coreSounds[FoleySound::Deal];
        d.name = "deal";
        d.soundId = FoleySound::Deal;
        d.isCustom = false;
        d.hasModalResonators = false;
        d.durationBase = 0.22f;
        d.durationDev = 0.12f;
        d.gainBase = 1.0f;
        d.gainDev = 0.05f;
        d.panBase = 0.0f;
        d.panDev = 0.05f;
    }

    // Place baseline (Table Drop / Thud, modeled from cardsounds/place.wav)
    {
        auto& p = bank.coreSounds[FoleySound::Place];
        p.name = "place";
        p.soundId = FoleySound::Place;
        p.isCustom = false;
        p.hasModalResonators = false;
        p.durationBase = 0.055f;
        p.durationDev = 0.08f;
        p.gainBase = 1.0f;
        p.gainDev = 0.05f;
        p.panBase = 0.0f;
        p.panDev = 0.04f;
    }

    // Shuffle baseline (Riffle cascade)
    {
        auto& sh = bank.coreSounds[FoleySound::Shuffle];
        sh.name = "shuffle";
        sh.soundId = FoleySound::Shuffle;
        sh.isCustom = false;
        sh.durationBase = 0.52f;
        sh.durationDev = 0.08f;
        sh.gainBase = 1.0f;
        sh.gainDev = 0.05f;
        sh.panBase = 0.0f;
        sh.panDev = 0.10f;
    }

    // Texas Pitch baseline (Dealing flurry)
    {
        auto& tp = bank.coreSounds[FoleySound::TexasPitch];
        tp.name = "texas_pitch";
        tp.soundId = FoleySound::TexasPitch;
        tp.isCustom = false;
        tp.durationBase = 0.70f;
        tp.durationDev = 0.06f;
        tp.gainBase = 1.0f;
        tp.gainDev = 0.05f;
        tp.panBase = 0.0f;
        tp.panDev = 0.08f;
    }

    // Flip baseline (Turnover flip)
    {
        auto& fl = bank.coreSounds[FoleySound::Flip];
        fl.name = "flip";
        fl.soundId = FoleySound::Flip;
        fl.isCustom = false;
        fl.durationBase = 0.18f;
        fl.durationDev = 0.08f;
        fl.gainBase = 1.0f;
        fl.gainDev = 0.05f;
        fl.panBase = 0.0f;
        fl.panDev = 0.05f;
    }

    // Slide baseline (Velvet baize slide)
    {
        auto& sl = bank.coreSounds[FoleySound::Slide];
        sl.name = "slide";
        sl.soundId = FoleySound::Slide;
        sl.isCustom = false;
        sl.durationBase = 0.22f;
        sl.durationDev = 0.12f;
        sl.gainBase = 1.0f;
        sl.gainDev = 0.05f;
        sl.panBase = 0.0f;
        sl.panDev = 0.05f;
    }

    // Spectre Whisper baseline
    {
        auto& sp = bank.coreSounds[FoleySound::SpectreWhisper];
        sp.name = "spectre";
        sp.soundId = FoleySound::SpectreWhisper;
        sp.isCustom = false;
        sp.durationBase = 0.350f;
        sp.durationDev = 0.05f;
        sp.gainBase = 1.0f;
        sp.gainDev = 0.05f;
        sp.panBase = 0.0f;
        sp.panDev = 0.08f;
    }

    // Classic sequence structures preserved for custom script backward compatibility
    {
        auto& shSeq = bank.coreSequences[FoleySound::Shuffle];
        shSeq.name = "shuffle";
        shSeq.sequenceId = FoleySound::Shuffle;
        shSeq.isCustom = false;
        shSeq.minFlips = 6;
        shSeq.maxFlips = 8;
        shSeq.baseGapSec = 0.056f;
        shSeq.gapDev = 0.15f;
        shSeq.dealToPlaceDelaySec = 0.036f;
        shSeq.dealToPlaceDelayDev = 0.10f;
        shSeq.doubleDropChance = 0.25f;
        shSeq.doubleDropMinSec = 0.008f;
        shSeq.doubleDropMaxSec = 0.014f;
        shSeq.stereoAlternation = 0.12f;
        shSeq.introSoundId = FoleySound::Slide;
        shSeq.introGain = 0.90f;
        shSeq.interspersedSoundId = FoleySound::Slide;
        shSeq.interspersedChance = 0.40f;
        shSeq.interspersedMaxCount = 3;
    }

    {
        auto& flSeq = bank.coreSequences[FoleySound::Flip];
        flSeq.name = "flip";
        flSeq.sequenceId = FoleySound::Flip;
        flSeq.isCustom = false;
        flSeq.dealToPlaceDelaySec = 0.048f;
        flSeq.dealToPlaceDelayDev = 0.12f;
    }
}

void AudioFoleyEngine::resetToDefaults() {
    uint32_t inactiveIdx = 1 - activeBankIndex_.load(std::memory_order_relaxed);
    populateDefaultBank(soundBanks_[inactiveIdx]);
    regenerateVelvetBuffer(soundBanks_[inactiveIdx].physicalParams.velvetDensity);
    masterVariance_.store(1.0f, std::memory_order_relaxed);
    activeBankIndex_.store(inactiveIdx, std::memory_order_release);
}

void AudioFoleyEngine::setPhysicalParams(const PhysicalAcousticParams& params) {
    uint32_t inactiveIdx = 1 - activeBankIndex_.load(std::memory_order_relaxed);
    FoleySoundBank& targetBank = soundBanks_[inactiveIdx];
    targetBank = soundBanks_[activeBankIndex_.load(std::memory_order_relaxed)];
    targetBank.physicalParams = params;
    regenerateVelvetBuffer(params.velvetDensity);
    activeBankIndex_.store(inactiveIdx, std::memory_order_release);
}

PhysicalAcousticParams AudioFoleyEngine::getPhysicalParams() const noexcept {
    const auto& bank = soundBanks_[activeBankIndex_.load(std::memory_order_relaxed)];
    return bank.physicalParams;
}

void AudioFoleyEngine::applyPreset(const std::string& presetName) {
    std::string lower;
    for (char c : presetName) lower += static_cast<char>(std::tolower(c));
    PhysicalAcousticParams p = getPhysicalParams();

    if (lower == "casino" || lower == "casino_baize" || lower == "default") {
        p = {4200.0f, 0.85f, 5.0f, 0.00018f, 7500.0f, 3200.0f};
    } else if (lower == "glass" || lower == "crystal") {
        loadFromEatsFile("sounds/glass_crystal.eats");
        return;
    } else if (lower == "place" || lower == "authentic_place") {
        loadFromEatsFile("sounds/authentic_place.eats");
        return;
    } else if (lower == "plastic" || lower == "copag" || lower == "copag_plastic") {
        p = {5800.0f, 1.25f, 7.5f, 0.00011f, 11500.0f, 4400.0f};
    } else if (lower == "bicycle" || lower == "vintage_bicycle" || lower == "paper") {
        p = {2600.0f, 0.95f, 6.0f, 0.00022f, 6800.0f, 2900.0f};
    } else if (lower == "linen" || lower == "vegas_linen" || lower == "bee") {
        p = {4800.0f, 1.10f, 4.0f, 0.00015f, 8200.0f, 3600.0f, 0.0f, 1.5f, 0.30f};
    } else if (lower == "futuristic" || lower == "cyberdeck" || lower == "scifi") {
        p = {4800.0f, 1.15f, 8.5f, 0.00035f, 12500.0f, 4200.0f, 0.00055f, 1.8f, 0.38f};
    }
    setPhysicalParams(p);
}

bool AudioFoleyEngine::init(bool enableHardwareDevice) {
    if (initialized_) return true;

    // Check for optional default soundpack script on disk
    loadFromEatsFile("sounds/default.eats");

    if (!enableHardwareDevice) {
        initialized_ = true;
        return true;
    }

    auto* device = new ma_device{};
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = miniaudioCallback;
    config.pUserData         = this;

    if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
        delete device;
        initialized_ = true;
        return false;
    }

    ma_device_start(device);
    maDevicePtr_ = device;
    initialized_ = true;
    return true;
}

void AudioFoleyEngine::shutdown() {
    if (maDevicePtr_) {
        auto* device = static_cast<ma_device*>(maDevicePtr_);
        ma_device_uninit(device);
        delete device;
        maDevicePtr_ = nullptr;
    }
    initialized_ = false;
}

void AudioFoleyEngine::triggerSound(uint16_t soundId, float intensity, float pan) {
    uint32_t w = writeIdx_.load(std::memory_order_relaxed);
    uint32_t r = readIdx_.load(std::memory_order_acquire);
    if (w - r < kQueueCapacity) {
        cmdQueue_[w % kQueueCapacity] = {soundId, intensity, pan};
        writeIdx_.store(w + 1, std::memory_order_release);
    }
}

void AudioFoleyEngine::allocateVoice(const Voice& v) noexcept {
    Voice assigned = v;
    assigned.gainL = std::clamp(0.5f * (1.0f - assigned.pan), 0.0f, 1.0f);
    assigned.gainR = std::clamp(0.5f * (1.0f + assigned.pan), 0.0f, 1.0f);

    for (auto& slot : voices_) {
        if (!slot.active) {
            slot = assigned;
            return;
        }
    }
    // All voices busy: steal the voice closest to finishing its duration
    size_t oldestIdx = 0;
    float maxNormAge = -1.0f;
    for (size_t i = 0; i < voices_.size(); ++i) {
        float normAge = voices_[i].ageSec / std::max(0.001f, voices_[i].durationSec);
        if (normAge > maxNormAge) {
            maxNormAge = normAge;
            oldestIdx = i;
        }
    }
    voices_[oldestIdx] = assigned;
}

void AudioFoleyEngine::handleTrigger(uint16_t soundId, float intensity, float pan, float sampleRate) noexcept {
    const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
    const auto& bank = soundBanks_[activeBankIndex_.load(std::memory_order_acquire)];
    const float varScale = masterVariance_.load(std::memory_order_relaxed);
    const auto& pp = bank.physicalParams;

    // Helper to spawn a procedural Snap voice
    auto makeSnapVoice = [&](float delay, float amp, float pitchMod = 1.0f, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::Snap;
        v.soundId = FoleySound::Snap;
        v.delaySec = std::max(0.0f, delay);
        v.durationSec = std::clamp(0.042f + std::abs(nextNoise()) * 0.008f * varScale, 0.035f, 0.060f);
        v.intensity = amp;
        v.pitchMod = pitchMod;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 1.0f + (nextNoise() * 0.12f * varScale);

        float finalFreq = pp.cardRigidityFreq * v.pitchMod;
        float feltCutoff = pp.feltDampingCutoff * (1.0f + nextNoise() * 0.08f * varScale);
        float airBoost = pp.airBoostDb + nextNoise() * 0.8f * varScale;
        float combDelay = pp.combDelayTime * (1.0f + nextNoise() * 0.06f * varScale);

        v.biquad1.c = makeLowpass(feltCutoff, 0.707f, sr); // Felt cutoff
        v.biquad2.c = makeBandpass(finalFreq, 4.2f, sr);   // Snap bandpass
        v.biquad3.c = makeHighshelf(8000.0f, airBoost, sr); // Air presence shelf
        v.biquad4.c = makeHighpass(6500.0f, 0.707f, sr);   // Dirac HP
        v.biquad5.c = makeLowpass(feltCutoff, 0.707f, sr);
        v.combDelaySamples = std::clamp(combDelay * sr, 1.0f, static_cast<float>(Voice::kCombBufferSize - 4));
        v.lfoPhase = static_cast<float>(nextRng() % 1000) * 0.006283f;

        v.triFreq = finalFreq * 0.55f;
        float triSamples = std::max(1.0f, 0.022f * sr);
        v.triFreqStep = std::pow(130.0f / std::max(1.0f, v.triFreq), 1.0f / triSamples);

        v.diracFreq = 9400.0f * v.pitchMod;
        float diracSamples = std::max(1.0f, 0.0025f * sr);
        v.diracFreqStep = std::pow(1800.0f / std::max(1.0f, v.diracFreq), 1.0f / diracSamples);
        return v;
    };

    // Helper to spawn a procedural Slide voice modeled on authentic paper baize friction via LPC-16 formants
    auto makeSlideVoice = [&](float delay, float amp, float duration = 0.11f, float voicePan = 0.0f, bool isDealSwell = false) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::Slide;
        v.soundId = FoleySound::Slide;
        v.delaySec = std::max(0.0f, delay);
        v.durationSec = std::clamp(duration * (1.0f + nextNoise() * 0.08f * varScale), 0.045f, 0.35f);
        v.intensity = amp;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 0.94f + std::abs(nextNoise()) * 0.14f * varScale;
        v.continuousIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.isDealSwell = isDealSwell;

        float feltCutoff = pp.feltDampingCutoff * (1.0f + nextNoise() * 0.06f * varScale);
        float airBoost = (pp.airBoostDb * 0.45f) + nextNoise() * 0.4f * varScale;

        // Biquad 1: Felt acoustic damping lowpass
        v.biquad1.c = makeLowpass(feltCutoff, 0.707f, sr);
        // Biquad 2: High-frequency air presence shelf
        v.biquad2.c = makeHighshelf(8500.0f, airBoost, sr);
        // Biquad 3: Cardboard flexion resonance
        v.biquad3.c = makeBandpass(pp.cardRigidityFreq * (1.0f + nextNoise() * 0.06f * varScale), 2.2f, sr);

        v.combDelaySamples = 0.0f;
        v.lfoPhase = 0.0f;
        return v;
    };

    // Helper to spawn an authentic single-voice Procedural Shuffle Riffle
    // Physical phenomenon: Interleaving card flutter friction with dynamic PhISEM micro-snap releases & Baize gather
    auto makeShuffleRiffleVoice = [&](ShuffleStyle style, float delay, float amp, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::ShuffleRiffle;
        v.soundId = FoleySound::Shuffle;
        v.delaySec = std::max(0.0f, delay);
        v.shuffleMode = (style == ShuffleStyle::Loud) ? 1 : 0;
        v.intensity = amp;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 1.0f + nextNoise() * 0.08f * varScale;
        v.continuousIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.snapEnv = 0.0f;
        v.snapIdx = 0;

        // Dynamic PhISEM stochastic particle cascade generation:
        int nSnaps = (style == ShuffleStyle::Loud) ? (13 + static_cast<int>(nextRng() % 5))
                                                   : (8 + static_cast<int>(nextRng() % 5));
        nSnaps = std::min<int>(nSnaps, static_cast<int>(Voice::kMaxRiffleSnaps));
        v.numRiffleSnaps = static_cast<uint8_t>(nSnaps);

        float curTime = (style == ShuffleStyle::Loud) ? 0.045f : 0.040f;
        curTime += std::abs(nextNoise()) * 0.010f * varScale;
        float baseGap = (style == ShuffleStyle::Loud) ? 0.024f : 0.032f;
        for (int i = 0; i < nSnaps; ++i) {
            float progress = (nSnaps > 1) ? (static_cast<float>(i) / static_cast<float>(nSnaps - 1)) : 0.5f;
            // Physical U-gap interleave curve: slower thumb peel at start and end, rapid flurry zipper in middle
            float gapMult = 1.35f - 0.70f * std::sin(progress * kPi);
            float step = baseGap * gapMult + (nextNoise() * 0.0035f * varScale);
            curTime += std::max(0.010f, step);
            v.riffleSnaps[i].timeSec = curTime;

            float ampBase = (style == ShuffleStyle::Loud) ? 0.45f : 0.28f;
            float ampPeak = (style == ShuffleStyle::Loud) ? 0.55f : 0.42f;
            float snapAmp = (ampBase + ampPeak * std::sin(progress * kPi)) * (1.0f + nextNoise() * 0.15f * varScale);
            v.riffleSnaps[i].amplitude = std::clamp(snapAmp, 0.15f, 1.10f);
            v.riffleSnaps[i].pitchMod = 1.0f + (nextNoise() * 0.07f * varScale);
        }
        v.durationSec = curTime + 0.12f;

        // Filter 1: Discrete card snap edge release bandpass
        // Soft shuffle has primary resonance around 2800 Hz; Loud has crisp resonance around 4170 Hz
        float fSnap = (style == ShuffleStyle::Loud) ? 4170.0f : 2800.0f;
        fSnap *= (1.0f + nextNoise() * 0.04f * varScale);
        v.biquad1.c = makeBandpass(fSnap, (style == ShuffleStyle::Loud) ? 2.4f : 2.0f, sr);

        // Filter 2: High frequency air / paper edge presence
        v.biquad2.c = makeHighpass((style == ShuffleStyle::Loud) ? 6500.0f : 5500.0f, 0.707f, sr);

        // Filter 3: Background card-on-card friction flutter bandpass
        float fFlutter = (style == ShuffleStyle::Loud) ? 2400.0f : 1800.0f;
        v.biquad3.c = makeBandpass(fFlutter, 1.4f, sr);

        // Filter 4: Final Baize gather slide
        v.biquad4.c = makeBandpass(2200.0f, 1.2f, sr);

        return v;
    };

    // Helper to spawn a procedural Drop / Thud voice modeled from cardsounds/place.wav
    // Physical phenomenon: Two-stage contact (leading corner edge touch at t=0, card face air slap & table thump at t=~9.5ms)
    auto makeDropVoice = [&](float delay, float amp, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::Drop;
        v.soundId = FoleySound::Place;
        v.delaySec = std::max(0.0f, delay);
        v.durationSec = std::clamp(0.055f * (1.0f + nextNoise() * 0.08f * varScale), 0.040f, 0.075f);
        v.intensity = amp;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.continuousIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 1.0f + (nextNoise() * 0.10f * varScale);

        // Randomize contact tilt angle: flat landing ~7ms split, tilted landing ~13ms split
        float split = 0.0075f + std::abs(nextNoise()) * 0.0065f * varScale;
        v.dropSplitSec = std::clamp(split, 0.006f, 0.015f);

        // Tilt-dependent balance between leading corner impact and air squeeze slap
        float tilt = nextNoise() * 0.5f + 0.5f;
        v.dropEdgeGain = 0.65f + 0.60f * tilt;
        v.dropSlapGain = 1.30f - 0.40f * tilt;
        v.dropKnockGain = 0.45f * (1.0f + nextNoise() * 0.10f * varScale);
        v.dropThudGain = 0.16f * (1.0f + nextNoise() * 0.10f * varScale);

        // Filter 1: Main card face air slap bandpass (4150 Hz, modeled from deal-series.wav & place.wav)
        float slapFreq = 4150.0f * (1.0f + nextNoise() * 0.05f * varScale);
        float slapQ = 1.45f * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad1.c = makeBandpass(slapFreq, slapQ, sr);

        // Filter 2: Leading edge touch tick bandpass (6100 Hz)
        float edgeFreq = 6100.0f * (1.0f + nextNoise() * 0.05f * varScale);
        float edgeQ = 1.9f * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad2.c = makeBandpass(edgeFreq, edgeQ, sr);

        // Filter 3: Card body acoustic knock bandpass (2450 Hz)
        float knockFreq = 2450.0f * (1.0f + nextNoise() * 0.06f * varScale);
        float knockQ = 1.8f * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad3.c = makeBandpass(knockFreq, knockQ, sr);

        // Low table coupling thump: starts around 88Hz and rapidly settles to 46Hz
        float fStart = (88.0f + nextNoise() * 10.0f * varScale);
        float fEnd = (46.0f + nextNoise() * 5.0f * varScale);
        v.dropFreq = fStart;
        float dropSamples = std::max(1.0f, 0.020f * sr);
        v.dropFreqStep = std::pow(fEnd / fStart, 1.0f / dropSamples);
        return v;
    };

    // Custom DSP voice builder for .eats defined sounds
    auto makeCustomVoice = [&](const FoleySoundDef& def, float delay, float amp, float voicePan) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.soundId = def.soundId;
        v.type = VoiceType::CustomDSP;
        v.isCustomDSP = true;
        v.delaySec = std::max(0.0f, delay);
        v.pan = std::clamp(voicePan + def.panBase + nextNoise() * def.panDev * varScale, -1.0f, 1.0f);
        v.intensity = amp * def.gainBase * (1.0f + nextNoise() * def.gainDev * varScale);

        float durJitter = 1.0f + (nextNoise() * def.durationDev * varScale);
        v.durationSec = std::max(0.010f, def.durationBase * durJitter);
        v.numLayers = static_cast<uint8_t>(def.numLayers);

        for (size_t i = 0; i < def.numLayers; ++i) {
            const auto& lDef = def.layers[i];
            auto& l = v.layers[i];
            l.active = true;
            l.generator = lDef.generator;
            l.filterType = lDef.filter;
            l.envelope = lDef.envelope;
            l.phase = 0.0f;
            l.dutyCycle = lDef.dutyCycle;
            l.attackSec = lDef.attackSec;
            l.decayRate = lDef.decayRate;

            float jFreq = 1.0f + (nextNoise() * lDef.freqStartDev * varScale);
            l.fStart = std::clamp(lDef.freqStartBase * jFreq, 20.0f, 20000.0f);
            l.fEnd   = std::clamp(lDef.freqEndBase * (1.0f + nextNoise() * lDef.freqEndDev * varScale), 20.0f, 20000.0f);

            float jWeight = 1.0f + (nextNoise() * lDef.weightDev * varScale);
            l.weight = std::max(0.0f, lDef.weightBase * jWeight);

            if (l.filterType != FoleyFilter::None) {
                float jCutoff = 1.0f + (nextNoise() * lDef.filterCutoffDev * varScale);
                float fCut = std::clamp(lDef.filterCutoffBase * jCutoff, 20.0f, sr * 0.45f);
                float jQ = 1.0f + (nextNoise() * lDef.filterQDev * varScale);
                float qVal = std::clamp(lDef.filterQBase * jQ, 0.1f, 30.0f);

                switch (l.filterType) {
                    case FoleyFilter::Lowpass:
                        l.filter.c = makeLowpass(fCut, qVal, sr);
                        break;
                    case FoleyFilter::Highpass:
                        l.filter.c = makeHighpass(fCut, qVal, sr);
                        break;
                    case FoleyFilter::Bandpass:
                        l.filter.c = makeBandpass(fCut, qVal, sr);
                        break;
                    default:
                        break;
                }
            }
        }
        return v;
    };

    // Helper to spawn a physics-based 4-pole Modal Resonator Card Voice
    auto makeModalSnapVoice = [&](float delay, float amp, const std::array<FoleyModalModeDef, 4>& modes, size_t numModes, float pitchMod = 1.0f, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::ModalCard;
        v.soundId = FoleySound::Snap;
        v.delaySec = std::max(0.0f, delay);
        v.intensity = amp;
        v.pitchMod = pitchMod;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);

        float maxDecay = 0.035f;
        for (size_t i = 0; i < std::min<size_t>(numModes, 4); ++i) {
            float f = modes[i].freqBase * (1.0f + nextNoise() * modes[i].freqDev * varScale) * pitchMod;
            float d = modes[i].decayBase * (1.0f + nextNoise() * modes[i].decayDev * varScale);
            float q = (modes[i].q > 0.5f) ? modes[i].q : (kPi * f * d);
            maxDecay = std::max(maxDecay, d * 2.5f);
            v.modeGains[i] = modes[i].gain;

            if (i == 0) v.biquad1.c = makeBandpass(f, q, sr);
            else if (i == 1) v.biquad2.c = makeBandpass(f, q, sr);
            else if (i == 2) v.biquad3.c = makeBandpass(f, q, sr);
            else if (i == 3) v.biquad4.c = makeBandpass(f, q, sr);
        }
        float feltCutoff = pp.feltDampingCutoff * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad5.c = makeLowpass(feltCutoff, 0.707f, sr);
        v.durationSec = std::clamp(maxDecay, 0.025f, 0.080f);
        return v;
    };

    // Helper to spawn a pre-snap thumb peel friction voice
    auto makePeelVoice = [&](float delay, float amp, float duration = 0.018f, float cutoff = 6800.0f, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::PeelFriction;
        v.soundId = FoleySound::Slide;
        v.delaySec = std::max(0.0f, delay);
        v.durationSec = std::clamp(duration * (1.0f + nextNoise() * 0.15f * varScale), 0.006f, 0.045f);
        v.intensity = amp;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 1.0f + (nextNoise() * 0.15f * varScale);

        float fCenter = cutoff * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad1.c = makeBandpass(fCenter, 1.4f, sr);
        return v;
    };

    auto dispatchModalSound = [&](const FoleySoundDef& def, float trigAmp, float trigPan) noexcept {
        float pitchJitter = 1.0f + (nextNoise() * 0.045f * varScale);
        float peelDur = def.peelFriction.durationBase * (1.0f + nextNoise() * def.peelFriction.durationDev * varScale);
        float snapDelay = (def.peelFriction.gainBase > 0.01f) ? peelDur : 0.0f;
        float landingDelay = snapDelay + def.landing.delayBase * (1.0f + nextNoise() * def.landing.delayDev * varScale);

        // 1. Peel friction
        if (def.peelFriction.gainBase > 0.01f) {
            allocateVoice(makePeelVoice(0.0f, trigAmp * def.peelFriction.gainBase, peelDur, def.peelFriction.filterCutoff, trigPan));
        }

        // 2. Modal card snap
        allocateVoice(makeModalSnapVoice(snapDelay, trigAmp * def.gainBase, def.modes, def.numModes, pitchJitter, trigPan));

        // 3. Landing thud
        if (def.landing.gainBase > 0.01f) {
            allocateVoice(makeDropVoice(landingDelay, trigAmp * def.landing.gainBase, trigPan));
            // Optional double tap
            if (def.landing.doubleTapChance > 0.01f && (static_cast<float>(nextRng() % 1000) / 1000.0f) < def.landing.doubleTapChance * varScale) {
                float split = def.landing.doubleTapSplit * (1.0f + nextNoise() * 0.15f * varScale);
                allocateVoice(makeDropVoice(landingDelay + split, trigAmp * def.landing.gainBase * 0.55f, trigPan));
            }
        }
    };

    // 1. Custom Procedural DSP & Modal Resonator Layer Fast-Path (audio.define_sound):
    // If the active Eatscript soundpack defined custom procedural synth layers or modal physics,
    // they take highest priority over default routines:
    if (soundId < FoleySound::MaxCoreSounds && bank.coreSounds[soundId].hasModalResonators) {
        dispatchModalSound(bank.coreSounds[soundId], intensity, pan + bank.coreSounds[soundId].panBase);
        return;
    }
    for (const auto& cDef : bank.customSounds) {
        if (cDef.soundId == soundId) {
            if (cDef.hasModalResonators) {
                dispatchModalSound(cDef, intensity, pan + cDef.panBase);
                return;
            }
            allocateVoice(makeCustomVoice(cDef, 0.0f, intensity, pan + cDef.panBase));
            return;
        }
    }
    if (soundId < FoleySound::MaxCoreSounds && bank.coreSounds[soundId].isCustom) {
        allocateVoice(makeCustomVoice(bank.coreSounds[soundId], 0.0f, intensity, pan + bank.coreSounds[soundId].panBase));
        return;
    }

    // 2. HD Physical Procedural Synthesis Routines (Built-in LPC-16, Dirac snaps, 2-stage touchdown, discrete riffle):
    switch (soundId) {
        case FoleySound::Snap: {
            // Core Dirac Edge Snap (Card click / tap / selection interaction)
            float pitchJitter = 1.0f + (nextNoise() * 0.075f * varScale);
            float snapAmp = intensity * (1.0f + nextNoise() * 0.06f * varScale);
            allocateVoice(makeSnapVoice(0.0f, snapAmp, pitchJitter, pan));
            break;
        }

        case FoleySound::Deal: {
            // Authentic Deal swipe modeled directly from cardsounds/deal-series.wav
            // Structure: Pre-lift whisper (t=0) + accelerating baize swell glide (t=2ms) + arrival touchdown tap (t=slideDur)
            dealAlternator_++;
            float dealPan = std::clamp(pan + ((static_cast<float>(dealAlternator_ % 5) - 2.0f) * 0.035f * varScale), -1.0f, 1.0f);
            float pitchOffset = 1.0f + ((static_cast<float>(dealAlternator_ % 7) - 3.0f) * 0.018f) + (nextNoise() * 0.035f * varScale);

            // Variable swipe duration matching empirical deals in deal-series.wav (68ms to 102ms)
            static constexpr std::array<float, 5> kDealDurations = { 0.088f, 0.068f, 0.098f, 0.076f, 0.092f };
            float baseDur = kDealDurations[dealAlternator_ % kDealDurations.size()];
            float slideDur = std::clamp(baseDur * (1.0f + nextNoise() * 0.06f * varScale), 0.055f, 0.115f);
            float slideAmp = 1.08f * intensity * (1.0f + nextNoise() * 0.06f * varScale);

            // 1. Soft deck-lift whisper (subtle, 18% volume, 2.4kHz)
            float liftAmp = 0.20f * intensity * (1.0f + nextNoise() * 0.08f * varScale);
            allocateVoice(makeSnapVoice(0.0f, liftAmp, pitchOffset * 0.85f, dealPan));

            // 2. Swell card glide across baize (accelerates from t=0 up to slideDur)
            allocateVoice(makeSlideVoice(0.002f, slideAmp, slideDur, dealPan, true));

            // 3. Arrival touchdown tap right at the end of the slide
            float hitDelay = slideDur - 0.006f;
            float hitAmp = 0.92f * intensity * (1.0f + nextNoise() * 0.06f * varScale);
            allocateVoice(makeDropVoice(hitDelay, hitAmp, dealPan));
            break;
        }

        case FoleySound::Place: {
            // Table Drop / Thud (card landing on surface with 3-stage physical contact)
            dealAlternator_++;
            float dropPan = std::clamp(pan + ((static_cast<float>(dealAlternator_ % 5) - 2.0f) * 0.03f * varScale), -1.0f, 1.0f);
            float dropAmp = intensity * (1.0f + nextNoise() * 0.08f * varScale);
            allocateVoice(makeDropVoice(0.0f, dropAmp, dropPan));
            break;
        }

        case FoleySound::Shuffle: {
            // Authentic Single-Voice Procedural Riffle Cascade
            // Physical model: Interleaving card flutter friction with dynamic PhISEM micro-snap releases & Baize gather
            const auto style = shuffleStyle_.load(std::memory_order_relaxed);
            allocateVoice(makeShuffleRiffleVoice(style, 0.0f, intensity, pan));
            break;
        }

        case FoleySound::TexasPitch: {
            // Texas Pitch (4 crisp dealer tosses with trailing friction)
            static constexpr std::array<float, 4> kTossDurs = { 0.078f, 0.068f, 0.084f, 0.072f };
            for (int i = 0; i < 4; ++i) {
                float tossDelay = static_cast<float>(i) * 0.138f + (nextNoise() * 0.006f * varScale);
                float slideDur = kTossDurs[i % 4] * (1.0f + nextNoise() * 0.06f * varScale);
                float pitch = 0.97f + static_cast<float>(i) * 0.028f + nextNoise() * 0.025f * varScale;
                float tossPan = std::clamp(pan + (static_cast<float>(i) - 1.5f) * 0.09f * varScale, -1.0f, 1.0f);
                float tossAmp = intensity * 0.92f * (1.0f + nextNoise() * 0.05f * varScale);

                // Lift
                allocateVoice(makeSnapVoice(tossDelay, tossAmp * 0.18f, pitch * 0.90f, tossPan));
                // Swell slide
                allocateVoice(makeSlideVoice(tossDelay + 0.002f, tossAmp * 1.05f, slideDur, tossPan, true));
                // Arrival tap
                float hitDelay = tossDelay + slideDur - 0.006f;
                allocateVoice(makeDropVoice(hitDelay, tossAmp * 0.88f, tossPan));
            }
            break;
        }

        case FoleySound::Flip: {
            // Turnover Flip (Edge peel friction into crisp snap & drop)
            dealAlternator_++;
            float flipPan = std::clamp(pan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
            float slideDur = 0.085f * (1.0f + nextNoise() * 0.08f * varScale);
            float contactDelay = slideDur + 0.015f + std::abs(nextNoise()) * 0.010f * varScale;
            float snapPitch = 1.10f * (1.0f + nextNoise() * 0.04f * varScale);

            allocateVoice(makeSlideVoice(0.0f, intensity * 1.15f, slideDur, flipPan, false));
            allocateVoice(makeSnapVoice(contactDelay, intensity * 0.50f, snapPitch, flipPan));
            allocateVoice(makeDropVoice(contactDelay, intensity * 0.85f, flipPan));
            break;
        }

        case FoleySound::Slide: {
            // Standalone card drag / glide sweep across felt
            float slideDur = std::clamp(0.12f * (1.0f + nextNoise() * 0.12f * varScale), 0.07f, 0.25f);
            float slideAmp = intensity * 0.90f * (1.0f + nextNoise() * 0.08f * varScale);
            float slidePan = std::clamp(pan + nextNoise() * 0.04f * varScale, -1.0f, 1.0f);
            allocateVoice(makeSlideVoice(0.0f, slideAmp, slideDur, slidePan));
            break;
        }

        case FoleySound::SpectreWhisper:
        default: {
            // Ethereal ZAngband Nether-Sight shimmer
            float jitter = 1.0f + (nextNoise() * 0.035f * varScale);
            Voice v{};
            v.reset();
            v.active = true;
            v.type = VoiceType::SpectreWhisper;
            v.soundId = FoleySound::SpectreWhisper;
            v.intensity = intensity;
            v.pitchMod = jitter;
            v.pan = std::clamp(pan + nextNoise() * 0.06f * varScale, -1.0f, 1.0f);
            v.durationSec = 0.350f;
            v.phaseBody = 396.0f * jitter;
            v.biquad1.c = makeBandpass(880.0f * jitter, 2.5f, sr);
            allocateVoice(v);
            break;
        }
    }
}

void AudioFoleyEngine::mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate) noexcept {
    const float sr = (sampleRate > 8000.0f) ? static_cast<float>(sampleRate) : 48000.0f;
    const float dt = 1.0f / sr;

    // Process queued triggers from the main thread
    uint32_t r = readIdx_.load(std::memory_order_relaxed);
    uint32_t w = writeIdx_.load(std::memory_order_acquire);
    while (r != w) {
        const auto& cmd = cmdQueue_[r % kQueueCapacity];
        handleTrigger(cmd.soundId, cmd.intensity, cmd.pan, sr);
        ++r;
    }
    readIdx_.store(r, std::memory_order_release);

    if (muted_.load(std::memory_order_relaxed)) {
        std::fill(outInterleavedStereo, outInterleavedStereo + frameCount * 2, 0.0f);
        return;
    }
    const float masterVol = masterVolume_.load(std::memory_order_relaxed);
    const float ambVol = masterVol * ambienceVolume_.load(std::memory_order_relaxed);
    const auto& bank = soundBanks_[activeBankIndex_.load(std::memory_order_acquire)];
    const auto& pp = bank.physicalParams;

    const bool hasVelvet = !velvetNoiseBuffer_.empty();
    const bool hasContinuous = !continuousNoiseBuffer_.empty();

    // Fast-path: detect active voices once per mix buffer to eliminate idle overhead
    uint8_t activeVoiceIndices[kMaxVoices];
    uint8_t activeCount = 0;
    for (uint8_t vi = 0; vi < static_cast<uint8_t>(voices_.size()); ++vi) {
        if (voices_[vi].active) {
            activeVoiceIndices[activeCount++] = vi;
        }
    }
    if (activeCount == 0) {
        std::fill_n(outInterleavedStereo, frameCount * 2, 0.0f);
        mixAmbience(outInterleavedStereo, frameCount, sr, ambVol);
        return;
    }

    for (uint32_t f = 0; f < frameCount; ++f) {
        float mixL = 0.0f;
        float mixR = 0.0f;

        for (uint8_t vi = 0; vi < activeCount; ++vi) {
            auto& v = voices_[activeVoiceIndices[vi]];
            if (!v.active) continue;

            if (v.delaySec > 0.0f) {
                v.delaySec -= dt;
                continue;
            }

            v.ageSec += dt;
            if (v.ageSec >= v.durationSec) {
                v.active = false;
                continue;
            }

            v.subCounter++;
            const float normAge = v.ageSec / std::max(0.0001f, v.durationSec);
            float sample = 0.0f;

            if (v.isCustomDSP) {
                const float noise = nextNoise();
                for (size_t li = 0; li < v.numLayers; ++li) {
                    auto& l = v.layers[li];
                    if (!l.active) continue;

                    float raw = 0.0f;
                    float curFreq = l.fStart + (l.fEnd - l.fStart) * normAge;
                    l.phase += curFreq * dt;
                    if (l.phase >= 1.0f) l.phase -= std::floor(l.phase);

                    switch (l.generator) {
                        case FoleyGenerator::Sine:
                            raw = std::sin(l.phase * kTwoPi);
                            break;
                        case FoleyGenerator::Triangle:
                            raw = std::abs(l.phase * 4.0f - 2.0f) - 1.0f;
                            break;
                        case FoleyGenerator::Square:
                            raw = (l.phase < l.dutyCycle) ? 0.85f : -0.85f;
                            break;
                        case FoleyGenerator::Saw:
                            raw = l.phase * 2.0f - 1.0f;
                            break;
                        case FoleyGenerator::Noise:
                        default:
                            raw = noise;
                            break;
                    }

                    float filtered = (l.filterType != FoleyFilter::None) ? l.filter.process(raw) : raw;

                    float env = 0.0f;
                    switch (l.envelope) {
                        case FoleyEnvelope::Exponential:
                            env = std::exp(-l.decayRate * normAge);
                            break;
                        case FoleyEnvelope::HalfSine:
                            env = std::sin(normAge * kPi);
                            break;
                        case FoleyEnvelope::AttackRelease: {
                            float att = (l.attackSec > 0.0001f) ? std::clamp(v.ageSec / l.attackSec, 0.0f, 1.0f) : 1.0f;
                            float dec = std::exp(-l.decayRate * normAge);
                            env = att * dec;
                            break;
                        }
                        case FoleyEnvelope::PunchSub:
                            env = (normAge * 12.0f) * std::exp(-l.decayRate * normAge);
                            break;
                        case FoleyEnvelope::LinearDecay:
                            env = std::max(0.0f, 1.0f - normAge);
                            break;
                        default:
                            env = std::exp(-l.decayRate * normAge);
                            break;
                    }

                    sample += filtered * env * l.weight;
                }
                sample *= v.intensity;
            } else {
                switch (v.type) {
                    case VoiceType::ModalCard: {
                        float impulse = 0.0f;
                        if (!v.impulseInjected) {
                            impulse = 1.0f;
                            v.impulseInjected = true;
                        }
                        float m1 = v.biquad1.process(impulse) * v.modeGains[0];
                        float m2 = v.biquad2.process(impulse) * v.modeGains[1];
                        float m3 = v.biquad3.process(impulse) * v.modeGains[2];
                        float m4 = v.biquad4.process(impulse) * v.modeGains[3];
                        float modalSum = m1 + m2 + m3 + m4;
                        float feltSig = v.biquad5.process(modalSum);
                        float env = std::exp(-v.ageSec * 45.0f);
                        sample = feltSig * env * v.intensity * 95.0f;
                        break;
                    }

                    case VoiceType::PeelFriction: {
                        if (hasVelvet) {
                            size_t vIdx = static_cast<size_t>(v.velvetIdx) % kNoiseBufferSize;
                            float velvet = velvetNoiseBuffer_[vIdx];
                            v.velvetIdx += v.velvetPlaybackRate;
                            if (v.velvetIdx >= static_cast<float>(kNoiseBufferSize)) v.velvetIdx -= static_cast<float>(kNoiseBufferSize);

                            float filtered = v.biquad1.process(velvet);
                            float att = std::min(1.0f, v.ageSec / std::max(0.002f, v.durationSec * 0.35f));
                            float dec = std::max(0.0f, 1.0f - (v.ageSec / v.durationSec));
                            float env = std::sin(att * kHalfPi) * dec;
                            sample = filtered * env * v.intensity * 1.5f;
                        }
                        break;
                    }

                    case VoiceType::Snap: {
                        const float finalFreq = pp.cardRigidityFreq * v.pitchMod;

                        // 1. Triangle Card Deflection Transient (sweeping down to 130Hz in 22ms)
                        float triOut = 0.0f;
                        if (v.ageSec < 0.022f) {
                            float norm22 = v.ageSec / 0.022f;
                            v.triFreq = std::max(130.0f, v.triFreq * v.triFreqStep);
                            v.phaseTri += v.triFreq * dt;
                            if (v.phaseTri >= 1.0f) v.phaseTri -= 1.0f;
                            float rawTri = std::abs(v.phaseTri * 4.0f - 2.0f) - 1.0f;
                            float triGain = 0.42f * v.intensity * std::exp(-8.34f * norm22);
                            triOut = v.biquad1.process(rawTri * triGain);
                        }

                        // 2. Core Velvet Noise Burst (sweeping down in 28ms, through snap BP -> air shelf -> felt cutoff)
                        float noiseOut = 0.0f;
                        if (v.ageSec < 0.038f && hasVelvet) {
                            size_t vIdx = static_cast<size_t>(v.velvetIdx) % kNoiseBufferSize;
                            float velvet = velvetNoiseBuffer_[vIdx];
                            v.velvetIdx += v.velvetPlaybackRate;
                            if (v.velvetIdx >= static_cast<float>(kNoiseBufferSize)) v.velvetIdx -= static_cast<float>(kNoiseBufferSize);

                            // Control-rate filter update every 16 samples for performance
                            if ((v.subCounter & 15) == 0) {
                                float norm28 = std::min(1.0f, v.ageSec / 0.028f);
                                float fSnap = finalFreq * std::pow(0.65f, norm28);
                                v.biquad2.c = makeBandpass(fSnap, 4.2f, sr);
                            }

                            float bp = v.biquad2.process(velvet);
                            float air = v.biquad3.process(bp);
                            float felt = v.biquad5.process(air);

                            float noiseGain = 0.0f;
                            if (v.ageSec < 0.0008f) {
                                noiseGain = 0.0001f + (0.85f * v.intensity - 0.0001f) * (v.ageSec / 0.0008f);
                            } else {
                                noiseGain = 0.85f * v.intensity * std::exp(-8.9f * ((v.ageSec - 0.0008f) / 0.0372f));
                            }
                            noiseOut = felt * noiseGain;
                        }

                        // 3. Dirac Edge-Snap Needle Pulse (9.4kHz square down to 1.8kHz in 2.5ms + Comb Dispersion)
                        float diracOut = 0.0f;
                        if (v.ageSec < 0.0025f && pp.edgeSnapCrispness > 0.05f) {
                            v.diracFreq = std::max(1800.0f, v.diracFreq * v.diracFreqStep);
                            v.phaseDirac += v.diracFreq * dt;
                            if (v.phaseDirac >= 1.0f) v.phaseDirac -= 1.0f;
                            float rawSquare = (v.phaseDirac < 0.5f) ? 0.90f : -0.90f;
                            float hp = v.biquad4.process(rawSquare);

                            float diracGain = 0.90f * pp.edgeSnapCrispness * v.intensity * std::exp(-8.9f * (v.ageSec / 0.0022f));
                            float sig = hp * diracGain;

                            // Comb dispersion / sweeping flanger delay
                            float curDelay = v.combDelaySamples;
                            if (pp.flangerDepth > 0.00001f) {
                                v.lfoPhase += pp.flangerRate * kTwoPi * dt;
                                if (v.lfoPhase >= kTwoPi) v.lfoPhase -= kTwoPi;
                                curDelay += (pp.flangerDepth * sr) * (0.5f + 0.5f * std::sin(v.lfoPhase));
                            }
                            curDelay = std::clamp(curDelay, 1.0f, static_cast<float>(Voice::kCombBufferSize - 2));

                            float readPos = static_cast<float>(v.combWriteIdx) - curDelay;
                            while (readPos < 0.0f) readPos += static_cast<float>(Voice::kCombBufferSize);
                            size_t i0 = static_cast<size_t>(readPos) % Voice::kCombBufferSize;
                            size_t i1 = (i0 + 1) % Voice::kCombBufferSize;
                            float frac = readPos - std::floor(readPos);
                            float delayed = v.combBuffer[i0] * (1.0f - frac) + v.combBuffer[i1] * frac;

                            float feedback = (pp.flangerDepth > 0.00001f) ? pp.flangerFeedback : 0.0f;
                            v.combBuffer[v.combWriteIdx] = sig + feedback * delayed;
                            diracOut = sig + 0.55f * delayed;
                            v.combWriteIdx = (v.combWriteIdx + 1) % Voice::kCombBufferSize;
                        }

                        sample = (triOut + noiseOut + diracOut) * 0.95f;
                        break;
                    }

                    case VoiceType::Slide: {
                        // 100% Procedural LPC-16 Cellulose Multi-Formant Synthesis
                        // Derived from cardsounds/deal-series.wav (LSD = 1.08 dB)
                        // Captures authentic paper cellulose resonance (3.5kHz, 6.3kHz, 11.0kHz, 13.7kHz)
                        float inNoise = 0.0f;
                        if (hasVelvet) {
                            size_t vIdx = static_cast<size_t>(v.velvetIdx) % kNoiseBufferSize;
                            inNoise = velvetNoiseBuffer_[vIdx] * 0.70f;
                            v.velvetIdx += v.velvetPlaybackRate;
                            if (v.velvetIdx >= static_cast<float>(kNoiseBufferSize)) v.velvetIdx -= static_cast<float>(kNoiseBufferSize);
                        }
                        if (hasContinuous) {
                            size_t cIdx = static_cast<size_t>(v.continuousIdx) % kNoiseBufferSize;
                            inNoise += continuousNoiseBuffer_[cIdx] * 0.30f;
                            v.continuousIdx += 1.0f;
                        }
                        if (!hasVelvet && !hasContinuous) {
                            inNoise = nextNoise();
                        }

                        // All-Pole LPC-16 Multi-Formant Recursion:
                        float lpcOut = kLpcDealGain * inNoise;
                        size_t curIdx = v.lpcIdx;
                        for (size_t k = 0; k < 16; ++k) {
                            size_t prevPos = (curIdx - 1 - k) & 15;
                            lpcOut -= kLpcDeal[k] * v.lpcHistory[prevPos];
                        }
                        v.lpcHistory[curIdx] = lpcOut;
                        v.lpcIdx = static_cast<uint8_t>((curIdx + 1) & 15);

                        // Felt acoustic damping lowpass + Air presence shelf
                        float feltFiltered = v.biquad1.process(lpcOut);
                        float airFiltered = v.biquad2.process(feltFiltered);
                        float flexFiltered = v.biquad3.process(inNoise) * 0.25f;

                        // Envelope selection: accelerating swell for deal swipes vs smooth ease for drags
                        float fricGain = 0.0f;
                        if (v.isDealSwell) {
                            // Authentic accelerating deal swipe modeled from cardsounds/deal-series.wav
                            // Friction swells as card is flicked across baize, peaking at arrival (t ~ durationSec - 10ms)
                            const float swellPeak = std::max(0.020f, v.durationSec - 0.010f);
                            if (v.ageSec < swellPeak) {
                                float progress = v.ageSec / swellPeak;
                                fricGain = 0.16f + 0.84f * std::pow(progress, 1.35f);
                            } else {
                                float decayFraction = (v.ageSec - swellPeak) / 0.010f;
                                fricGain = 1.0f * std::exp(-6.5f * decayFraction);
                            }
                        } else {
                            // Standard ease-in card drag across baize
                            const float attT = 0.014f;
                            if (v.ageSec < attT) {
                                fricGain = 0.95f * (v.ageSec / attT);
                            } else {
                                float decayFraction = (v.ageSec - attT) / std::max(0.010f, v.durationSec - attT);
                                fricGain = 0.95f * std::exp(-5.2f * decayFraction);
                            }
                        }

                        sample = (airFiltered + flexFiltered) * fricGain * v.intensity * 2.2f;

                        // Sweeping flanger comb modulation for futuristic flare on card slides & deals
                        if (pp.flangerDepth > 0.00001f) {
                            v.lfoPhase += pp.flangerRate * kTwoPi * dt;
                            if (v.lfoPhase >= kTwoPi) v.lfoPhase -= kTwoPi;
                            float curDelay = v.combDelaySamples + (pp.flangerDepth * sr) * (0.5f + 0.5f * std::sin(v.lfoPhase));
                            curDelay = std::clamp(curDelay, 1.0f, static_cast<float>(Voice::kCombBufferSize - 2));

                            float readPos = static_cast<float>(v.combWriteIdx) - curDelay;
                            while (readPos < 0.0f) readPos += static_cast<float>(Voice::kCombBufferSize);
                            size_t i0 = static_cast<size_t>(readPos) % Voice::kCombBufferSize;
                            size_t i1 = (i0 + 1) % Voice::kCombBufferSize;
                            float frac = readPos - std::floor(readPos);
                            float delayed = v.combBuffer[i0] * (1.0f - frac) + v.combBuffer[i1] * frac;

                            v.combBuffer[v.combWriteIdx] = sample + pp.flangerFeedback * delayed;
                            sample = sample * 0.65f + delayed * 0.55f;
                            v.combWriteIdx = (v.combWriteIdx + 1) % Voice::kCombBufferSize;
                        }
                        break;
                    }

                    case VoiceType::Drop: {
                        // Two-stage physical touchdown modeled from cardsounds/place.wav:
                        // Stage 1 (t=0 to split): Leading corner/edge touch (crisp 5.8kHz micro-tick)
                        // Stage 2 (t >= split): Card face air slap (3.8kHz) + body knock (920Hz) + table thump (95->48Hz)
                        float edgeOut = 0.0f;
                        float slapOut = 0.0f;
                        float knockOut = 0.0f;
                        float thudOut = 0.0f;

                        float noiseSample = nextNoise();
                        if (hasContinuous) {
                            size_t cIdx = static_cast<size_t>(v.continuousIdx) % kNoiseBufferSize;
                            noiseSample = continuousNoiseBuffer_[cIdx] * 3.5f;
                            v.continuousIdx += 1.0f;
                        }

                        // Stage 1: Leading edge touch tick
                        if (v.ageSec < v.dropSplitSec) {
                            float edgeFilt = v.biquad2.process(noiseSample);
                            float edgeGain = 0.0f;
                            if (v.ageSec < 0.0008f) {
                                edgeGain = v.dropEdgeGain * (v.ageSec / 0.0008f);
                            } else {
                                edgeGain = v.dropEdgeGain * std::exp(-(v.ageSec - 0.0008f) / 0.0022f);
                            }
                            edgeOut = edgeFilt * edgeGain;
                        }

                        // Stage 2: Main touchdown
                        if (v.ageSec >= v.dropSplitSec) {
                            const float bodyAge = v.ageSec - v.dropSplitSec;

                            // 1. Air Slap (3.8kHz)
                            float slapFilt = v.biquad1.process(noiseSample);
                            float slapGain = 0.0f;
                            if (bodyAge < 0.0010f) {
                                slapGain = v.dropSlapGain * (bodyAge / 0.0010f);
                            } else {
                                slapGain = v.dropSlapGain * std::exp(-(bodyAge - 0.0010f) / 0.0050f);
                            }
                            slapOut = slapFilt * slapGain;

                            // 2. Card Body Knock (920Hz)
                            float knockFilt = v.biquad3.process(noiseSample);
                            float knockGain = 0.0f;
                            if (bodyAge < 0.0012f) {
                                knockGain = v.dropKnockGain * (bodyAge / 0.0012f);
                            } else {
                                knockGain = v.dropKnockGain * std::exp(-(bodyAge - 0.0012f) / 0.0065f);
                            }
                            knockOut = knockFilt * knockGain;

                            // 3. Deep Table Thump (95Hz -> 48Hz in 20ms)
                            v.dropFreq = std::max(48.0f, v.dropFreq * v.dropFreqStep);
                            v.phaseSine += v.dropFreq * dt;
                            if (v.phaseSine >= 1.0f) v.phaseSine -= 1.0f;
                            float rawSine = std::sin(v.phaseSine * kTwoPi);
                            float thudGain = v.dropThudGain * std::exp(-bodyAge / 0.014f);
                            thudOut = rawSine * thudGain;
                        }

                        sample = (edgeOut + slapOut + knockOut + thudOut) * v.intensity * 0.95f;
                        break;
                    }

                    case VoiceType::ShuffleRiffle: {
                        // 100% Procedural Single-Voice Discrete Transient Riffle Cascade
                        // Models dynamic PhISEM card corner releases with non-repeating stochastic intervals
                        float inNoise = 0.0f;
                        if (hasVelvet) {
                            size_t vIdx = static_cast<size_t>(v.velvetIdx) % kNoiseBufferSize;
                            inNoise = velvetNoiseBuffer_[vIdx];
                            v.velvetIdx += v.velvetPlaybackRate;
                            if (v.velvetIdx >= static_cast<float>(kNoiseBufferSize)) v.velvetIdx -= static_cast<float>(kNoiseBufferSize);
                        } else {
                            inNoise = nextNoise();
                        }

                        const bool isLoud = (v.shuffleMode == 1);

                        // Check dynamic PhISEM card snap schedule:
                        if (v.snapIdx < v.numRiffleSnaps) {
                            if (v.ageSec >= v.riffleSnaps[v.snapIdx].timeSec) {
                                v.snapEnv = v.riffleSnaps[v.snapIdx].amplitude;
                                v.snapIdx++;
                            }
                        }

                        // Exponential decay of discrete card corner snap (~2.6 ms)
                        v.snapEnv *= 0.9926f;

                        // 1. Discrete card corner snap impulse (formant bandpass + crisp air presence)
                        float snapSignal = inNoise * v.snapEnv;
                        float snapBp = v.biquad1.process(snapSignal);
                        float snapAir = v.biquad2.process(snapSignal) * 0.35f;
                        float snapOut = (snapBp + snapAir) * (isLoud ? 1.45f : 1.10f);

                        // 2. Continuous background card-on-card friction flutter
                        float flutterFilt = v.biquad3.process(inNoise);
                        // Riffle bow envelope: rises smoothly, peaks during main interleave, settles before gather
                        float flutterEnv = std::sin(std::clamp(normAge / 0.85f, 0.0f, 1.0f) * kPi);
                        float flutterOut = flutterFilt * flutterEnv * (isLoud ? 0.32f : 0.25f);

                        // 3. Trailing Baize gather slide as packets square up (after final card drop)
                        float gatherOut = 0.0f;
                        if (normAge > 0.72f) {
                            float normTail = (normAge - 0.72f) / 0.28f;
                            float gatherFilt = v.biquad4.process(inNoise);
                            gatherOut = gatherFilt * std::sin(normTail * kPi) * 0.50f;
                        }

                        sample = (snapOut + flutterOut + gatherOut) * v.intensity * 1.15f;
                        break;
                    }

                    case VoiceType::SpectreWhisper:
                    default: {
                        v.phaseBody += 396.0f * v.pitchMod * dt;
                        if (v.phaseBody >= 1.0f) v.phaseBody -= 1.0f;

                        v.phaseThud += (396.0f * v.pitchMod * 1.503f) * dt;
                        if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;

                        float whisper = v.biquad1.process(nextNoise()) * 0.25f;
                        float env = std::sin(normAge * kPi) * std::exp(-2.2f * normAge);
                        float tone = 0.55f * std::sin(v.phaseBody * kTwoPi) + 0.45f * std::sin(v.phaseThud * kTwoPi);
                        sample = (tone * 0.75f + whisper * 0.25f) * env * v.intensity * 0.35f;
                        break;
                    }
                }
            }

            mixL += sample * v.gainL;
            mixR += sample * v.gainR;
        }

        // Soft saturation limiter to protect against digital clipping
        mixL = std::clamp(mixL * masterVol, -0.92f, 0.92f);
        mixR = std::clamp(mixR * masterVol, -0.92f, 0.92f);
        outInterleavedStereo[f * 2 + 0] = mixL;
        outInterleavedStereo[f * 2 + 1] = mixR;
    }

    mixAmbience(outInterleavedStereo, frameCount, sr, ambVol);
}

bool AudioFoleyEngine::loadFromEatsScript(const std::string& script) {
    if (script.empty()) return false;

    uint32_t inactiveIdx = 1 - activeBankIndex_.load(std::memory_order_relaxed);
    FoleySoundBank& targetBank = soundBanks_[inactiveIdx];
    // Start from a clean default baseline so custom DSP overrides or embedded samples
    // from a previously loaded pack do not leak into the new soundpack.
    populateDefaultBank(targetBank);

    auto parseDev = [](const eatsbits::eatscript::Value& val, float defBase, float defDev) -> std::pair<float, float> {
        if (val.isNumber()) {
            return { val.asFloat(defBase), defDev };
        }
        if (val.isDict()) {
            float b = val.hasKey("base") ? val.get("base").asFloat(defBase) : defBase;
            float d = val.hasKey("dev") ? val.get("dev").asFloat(defDev) : defDev;
            return { b, d };
        }
        return { defBase, defDev };
    };

    auto mapSoundName = [](const std::string& name) -> uint16_t {
        std::string lower;
        for (char c : name) lower += static_cast<char>(std::tolower(c));
        if (lower == "snap" || lower == "tap" || lower == "cardsnap" || lower == "card_snap" || lower == "cardtap" || lower == "card_tap" || lower == "woodclick" || lower == "wood_click") return FoleySound::Snap;
        if (lower == "deal" || lower == "cardslide" || lower == "card_deal" || lower == "card_deal_series" || lower == "deal_series" || lower == "single" || lower == "card_single") return FoleySound::Deal;
        if (lower == "place" || lower == "drop" || lower == "cardplace" || lower == "card_place" || lower == "carddrop" || lower == "card_drop" || lower == "authentic_place") return FoleySound::Place;
        if (lower == "shuffle" || lower == "deckshuffle" || lower == "deck_shuffle" || lower == "riffle") return FoleySound::Shuffle;
        if (lower == "texas_pitch" || lower == "texaspitch" || lower == "flurry" || lower == "deal_flurry") return FoleySound::TexasPitch;
        if (lower == "flip" || lower == "turnover") return FoleySound::Flip;
        if (lower == "slide" || lower == "sweep") return FoleySound::Slide;
        if (lower == "spectre" || lower == "spectrewhisper" || lower == "spectre_whisper") return FoleySound::SpectreWhisper;
        return 0xFFFF;
    };

    eatsbits::eatscript::Evaluator evaluator;

    auto defineSoundFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                             const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (args.empty()) return eatsbits::eatscript::Value(false);
        std::string soundName;
        uint16_t sId = 0xFFFF;
        if (args[0].isString()) {
            soundName = args[0].asString();
            sId = mapSoundName(soundName);
        } else if (args[0].isNumber()) {
            sId = static_cast<uint16_t>(args[0].asInt());
        }

        const eatsbits::eatscript::Value* opts = nullptr;
        if (args.size() > 1 && args[1].isDict()) {
            opts = &args[1];
        }
        if (!opts) return eatsbits::eatscript::Value(false);

        FoleySoundDef soundDef{};
        soundDef.name = soundName;
        soundDef.soundId = sId;
        soundDef.isCustom = true;

        if (opts->hasKey("duration")) {
            auto [b, d] = parseDev(opts->get("duration"), 0.060f, 0.10f);
            soundDef.durationBase = b;
            soundDef.durationDev = d;
        }
        if (opts->hasKey("gain") || opts->hasKey("volume")) {
            auto [b, d] = parseDev(opts->hasKey("gain") ? opts->get("gain") : opts->get("volume"), 1.0f, 0.05f);
            soundDef.gainBase = b;
            soundDef.gainDev = d;
        }
        if (opts->hasKey("pan")) {
            auto [b, d] = parseDev(opts->get("pan"), 0.0f, 0.05f);
            soundDef.panBase = b;
            soundDef.panDev = d;
        }

        // Parse modal resonators (modes, peel_friction, landing)
        if (opts->hasKey("modes") && opts->get("modes").isList()) {
            const auto& modeList = opts->get("modes").listVal;
            soundDef.hasModalResonators = true;
            soundDef.numModes = std::min<size_t>(modeList.size(), 4);
            for (size_t i = 0; i < soundDef.numModes; ++i) {
                const auto& mVal = modeList[i];
                if (!mVal.isDict()) continue;
                auto& mDef = soundDef.modes[i];

                if (mVal.hasKey("freq") || mVal.hasKey("freq_base")) {
                    const auto& fKey = mVal.hasKey("freq") ? mVal.get("freq") : mVal.get("freq_base");
                    auto [b, d] = parseDev(fKey, 2600.0f, 0.05f);
                    mDef.freqBase = b;
                    mDef.freqDev = d;
                }
                if (mVal.hasKey("freq_dev")) {
                    mDef.freqDev = mVal.get("freq_dev").asFloat(0.05f);
                }

                if (mVal.hasKey("decay") || mVal.hasKey("decay_base")) {
                    const auto& dKey = mVal.hasKey("decay") ? mVal.get("decay") : mVal.get("decay_base");
                    auto [b, d] = parseDev(dKey, 0.015f, 0.08f);
                    mDef.decayBase = b;
                    mDef.decayDev = d;
                }
                if (mVal.hasKey("decay_dev")) {
                    mDef.decayDev = mVal.get("decay_dev").asFloat(0.08f);
                }

                if (mVal.hasKey("gain") || mVal.hasKey("weight")) {
                    mDef.gain = mVal.hasKey("gain") ? mVal.get("gain").asFloat(1.0f) : mVal.get("weight").asFloat(1.0f);
                }
                if (mVal.hasKey("q") || mVal.hasKey("resonance")) {
                    mDef.q = mVal.hasKey("q") ? mVal.get("q").asFloat(10.0f) : mVal.get("resonance").asFloat(10.0f);
                }
            }

            if (opts->hasKey("peel_friction") && opts->get("peel_friction").isDict()) {
                const auto& pDict = opts->get("peel_friction");
                if (pDict.hasKey("duration") || pDict.hasKey("duration_base")) {
                    auto [b, d] = parseDev(pDict.hasKey("duration") ? pDict.get("duration") : pDict.get("duration_base"), 0.016f, 0.20f);
                    soundDef.peelFriction.durationBase = b;
                    soundDef.peelFriction.durationDev = d;
                }
                if (pDict.hasKey("duration_dev")) soundDef.peelFriction.durationDev = pDict.get("duration_dev").asFloat(0.20f);
                if (pDict.hasKey("filter_cutoff") || pDict.hasKey("cutoff")) {
                    soundDef.peelFriction.filterCutoff = pDict.hasKey("filter_cutoff") ? pDict.get("filter_cutoff").asFloat(7200.0f)
                                                                                      : pDict.get("cutoff").asFloat(7200.0f);
                }
                if (pDict.hasKey("gain") || pDict.hasKey("gain_base")) {
                    soundDef.peelFriction.gainBase = pDict.hasKey("gain") ? pDict.get("gain").asFloat(0.35f)
                                                                          : pDict.get("gain_base").asFloat(0.35f);
                }
            }

            if (opts->hasKey("landing") && opts->get("landing").isDict()) {
                const auto& lDict = opts->get("landing");
                if (lDict.hasKey("delay") || lDict.hasKey("delay_base")) {
                    auto [b, d] = parseDev(lDict.hasKey("delay") ? lDict.get("delay") : lDict.get("delay_base"), 0.024f, 0.20f);
                    soundDef.landing.delayBase = b;
                    soundDef.landing.delayDev = d;
                }
                if (lDict.hasKey("delay_dev")) soundDef.landing.delayDev = lDict.get("delay_dev").asFloat(0.20f);
                if (lDict.hasKey("thud_frequency") || lDict.hasKey("freq")) {
                    soundDef.landing.thudFreq = lDict.hasKey("thud_frequency") ? lDict.get("thud_frequency").asFloat(320.0f)
                                                                               : lDict.get("freq").asFloat(320.0f);
                }
                if (lDict.hasKey("gain") || lDict.hasKey("gain_base")) {
                    soundDef.landing.gainBase = lDict.hasKey("gain") ? lDict.get("gain").asFloat(0.50f)
                                                                     : lDict.get("gain_base").asFloat(0.50f);
                }
                if (lDict.hasKey("double_tap_chance")) soundDef.landing.doubleTapChance = lDict.get("double_tap_chance").asFloat(0.45f);
                if (lDict.hasKey("double_tap_split")) soundDef.landing.doubleTapSplit = lDict.get("double_tap_split").asFloat(0.011f);
            }
        }

        if (opts->hasKey("layers") && opts->get("layers").isList()) {
            const auto& layerList = opts->get("layers").listVal;
            soundDef.numLayers = std::min(layerList.size(), kMaxLayersPerSound);
            for (size_t i = 0; i < soundDef.numLayers; ++i) {
                const auto& lVal = layerList[i];
                if (!lVal.isDict()) continue;
                auto& layer = soundDef.layers[i];

                // Generator
                std::string genStr = lVal.hasKey("generator") ? lVal.get("generator").asString()
                                                              : (lVal.hasKey("type") ? lVal.get("type").asString() : "noise");
                for (char& c : genStr) c = static_cast<char>(std::tolower(c));
                if (genStr == "sine") layer.generator = FoleyGenerator::Sine;
                else if (genStr == "triangle" || genStr == "tri") layer.generator = FoleyGenerator::Triangle;
                else if (genStr == "square" || genStr == "pulse") layer.generator = FoleyGenerator::Square;
                else if (genStr == "saw" || genStr == "sawtooth") layer.generator = FoleyGenerator::Saw;
                else layer.generator = FoleyGenerator::Noise;

                if (lVal.hasKey("duty")) layer.dutyCycle = std::clamp(lVal.get("duty").asFloat(0.5f), 0.05f, 0.95f);

                // Pitch / Frequency
                if (lVal.hasKey("freq")) {
                    auto [b, d] = parseDev(lVal.get("freq"), 440.0f, 0.05f);
                    layer.freqStartBase = layer.freqEndBase = b;
                    layer.freqStartDev = layer.freqEndDev = d;
                }
                if (lVal.hasKey("freq_start")) {
                    auto [b, d] = parseDev(lVal.get("freq_start"), 440.0f, 0.05f);
                    layer.freqStartBase = b;
                    layer.freqStartDev = d;
                }
                if (lVal.hasKey("freq_end")) {
                    auto [b, d] = parseDev(lVal.get("freq_end"), layer.freqStartBase, 0.05f);
                    layer.freqEndBase = b;
                    layer.freqEndDev = d;
                }

                // Filter
                if (lVal.hasKey("filter") && lVal.get("filter").isDict()) {
                    const auto& fDict = lVal.get("filter");
                    std::string fType = fDict.hasKey("type") ? fDict.get("type").asString() : "lowpass";
                    for (char& c : fType) c = static_cast<char>(std::tolower(c));
                    if (fType == "lowpass" || fType == "lp") layer.filter = FoleyFilter::Lowpass;
                    else if (fType == "highpass" || fType == "hp") layer.filter = FoleyFilter::Highpass;
                    else if (fType == "bandpass" || fType == "bp") layer.filter = FoleyFilter::Bandpass;
                    else layer.filter = FoleyFilter::None;

                    if (fDict.hasKey("cutoff") || fDict.hasKey("freq")) {
                        auto [b, d] = parseDev(fDict.hasKey("cutoff") ? fDict.get("cutoff") : fDict.get("freq"), 1000.0f, 0.06f);
                        layer.filterCutoffBase = b;
                        layer.filterCutoffDev = d;
                    }
                    if (fDict.hasKey("q") || fDict.hasKey("resonance")) {
                        auto [b, d] = parseDev(fDict.hasKey("q") ? fDict.get("q") : fDict.get("resonance"), 1.0f, 0.05f);
                        layer.filterQBase = b;
                        layer.filterQDev = d;
                    }
                }

                // Envelope
                if (lVal.hasKey("env")) {
                    if (lVal.get("env").isString()) {
                        std::string eStr = lVal.get("env").asString();
                        for (char& c : eStr) c = static_cast<char>(std::tolower(c));
                        if (eStr == "sine" || eStr == "halfsine") layer.envelope = FoleyEnvelope::HalfSine;
                        else if (eStr == "punch" || eStr == "sub") layer.envelope = FoleyEnvelope::PunchSub;
                        else if (eStr == "linear") layer.envelope = FoleyEnvelope::LinearDecay;
                        else if (eStr == "attack_release" || eStr == "ar") layer.envelope = FoleyEnvelope::AttackRelease;
                        else layer.envelope = FoleyEnvelope::Exponential;
                    } else if (lVal.get("env").isDict()) {
                        const auto& eDict = lVal.get("env");
                        if (eDict.hasKey("attack")) layer.attackSec = eDict.get("attack").asFloat(0.005f);
                        if (eDict.hasKey("decay")) layer.decayRate = eDict.get("decay").asFloat(2.0f);
                        if (eDict.hasKey("curve")) {
                            std::string cStr = eDict.get("curve").asString();
                            for (char& c : cStr) c = static_cast<char>(std::tolower(c));
                            if (cStr == "sine") layer.envelope = FoleyEnvelope::HalfSine;
                            else if (cStr == "punch") layer.envelope = FoleyEnvelope::PunchSub;
                            else if (cStr == "linear") layer.envelope = FoleyEnvelope::LinearDecay;
                            else layer.envelope = FoleyEnvelope::AttackRelease;
                        }
                    }
                }
                if (lVal.hasKey("decay")) {
                    layer.decayRate = lVal.get("decay").asFloat(2.0f);
                }
                if (lVal.hasKey("attack")) {
                    layer.attackSec = lVal.get("attack").asFloat(0.005f);
                    layer.envelope = FoleyEnvelope::AttackRelease;
                }

                // Weight / Gain
                if (lVal.hasKey("gain") || lVal.hasKey("weight") || lVal.hasKey("layer_weight")) {
                    const auto& wVal = lVal.hasKey("gain") ? lVal.get("gain")
                                                           : (lVal.hasKey("weight") ? lVal.get("weight") : lVal.get("layer_weight"));
                    auto [b, d] = parseDev(wVal, 1.0f, 0.08f);
                    layer.weightBase = b;
                    layer.weightDev = d;
                }
            }
        }

        if (sId < FoleySound::MaxCoreSounds) {
            targetBank.coreSounds[sId] = soundDef;
        } else {
            bool found = false;
            for (auto& cs : targetBank.customSounds) {
                if (cs.name == soundName) {
                    cs = soundDef;
                    found = true;
                    break;
                }
            }
            if (!found) targetBank.customSounds.push_back(soundDef);
        }
        return eatsbits::eatscript::Value(true);
    };

    auto defineSeqFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                           const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (args.empty()) return eatsbits::eatscript::Value(false);
        std::string seqName = args[0].asString();
        uint16_t seqId = mapSoundName(seqName);

        const eatsbits::eatscript::Value* opts = nullptr;
        if (args.size() > 1 && args[1].isDict()) {
            opts = &args[1];
        }
        if (!opts) return eatsbits::eatscript::Value(false);

        FoleySequenceDef seq{};
        seq.name = seqName;
        seq.sequenceId = seqId;
        seq.isCustom = true;

        if (opts->hasKey("flips") && opts->get("flips").isDict()) {
            const auto& fDict = opts->get("flips");
            if (fDict.hasKey("count")) {
                if (fDict.get("count").isNumber()) {
                    seq.minFlips = seq.maxFlips = fDict.get("count").asInt();
                } else if (fDict.get("count").isDict()) {
                    seq.minFlips = fDict.get("count").get("min").asInt(6);
                    seq.maxFlips = fDict.get("count").get("max").asInt(8);
                }
            }
            if (fDict.hasKey("min_count")) seq.minFlips = fDict.get("min_count").asInt(6);
            if (fDict.hasKey("max_count")) seq.maxFlips = fDict.get("max_count").asInt(8);

            if (fDict.hasKey("base_gap")) seq.baseGapSec = fDict.get("base_gap").asFloat(0.056f);
            if (fDict.hasKey("gap_jitter")) seq.gapDev = fDict.get("gap_jitter").asFloat(0.15f);

            if (fDict.hasKey("deal_to_place_delay")) {
                auto [b, d] = parseDev(fDict.get("deal_to_place_delay"), 0.036f, 0.10f);
                seq.dealToPlaceDelaySec = b;
                seq.dealToPlaceDelayDev = d;
            }

            if (fDict.hasKey("double_drop_chance")) seq.doubleDropChance = fDict.get("double_drop_chance").asFloat(0.25f);
            if (fDict.hasKey("double_drop_delay") && fDict.get("double_drop_delay").isDict()) {
                seq.doubleDropMinSec = fDict.get("double_drop_delay").get("min").asFloat(0.008f);
                seq.doubleDropMaxSec = fDict.get("double_drop_delay").get("max").asFloat(0.014f);
            }

            if (fDict.hasKey("stereo_alternation")) seq.stereoAlternation = fDict.get("stereo_alternation").asFloat(0.12f);
        }

        if (opts->hasKey("intro_sound")) {
            seq.introSoundId = mapSoundName(opts->get("intro_sound").asString());
        }
        if (opts->hasKey("intro_gain")) seq.introGain = opts->get("intro_gain").asFloat(0.90f);

        if (opts->hasKey("interspersed_sweeps") && opts->get("interspersed_sweeps").isDict()) {
            const auto& swDict = opts->get("interspersed_sweeps");
            if (swDict.hasKey("chance")) seq.interspersedChance = swDict.get("chance").asFloat(0.40f);
            if (swDict.hasKey("max_sweeps")) seq.interspersedMaxCount = swDict.get("max_sweeps").asInt(3);
        }

        if (seqId < FoleySound::MaxCoreSounds) {
            targetBank.coreSequences[seqId] = seq;
        } else {
            targetBank.customSequences.push_back(seq);
        }
        return eatsbits::eatscript::Value(true);
    };

    auto setVarianceFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                             const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (!args.empty() && args[0].isNumber()) {
            targetBank.masterVariance = std::clamp(args[0].asFloat(1.0f), 0.0f, 3.0f);
            return eatsbits::eatscript::Value(true);
        }
        return eatsbits::eatscript::Value(false);
    };

    auto setVolumeFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                           const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (!args.empty() && args[0].isNumber()) {
            setMasterVolume(args[0].asFloat(0.85f));
            return eatsbits::eatscript::Value(true);
        }
        return eatsbits::eatscript::Value(false);
    };

    auto setPhysicalParamsFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                                   const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (!args.empty() && args[0].isDict()) {
            const auto& d = args[0];
            auto& p = targetBank.physicalParams;
            if (d.hasKey("velvet_density")) p.velvetDensity = d.get("velvet_density").asFloat(p.velvetDensity);
            if (d.hasKey("edge_snap")) p.edgeSnapCrispness = d.get("edge_snap").asFloat(p.edgeSnapCrispness);
            if (d.hasKey("air_boost")) p.airBoostDb = d.get("air_boost").asFloat(p.airBoostDb);
            if (d.hasKey("comb_delay")) p.combDelayTime = d.get("comb_delay").asFloat(p.combDelayTime);
            if (d.hasKey("felt_damping")) p.feltDampingCutoff = d.get("felt_damping").asFloat(p.feltDampingCutoff);
            if (d.hasKey("card_rigidity")) p.cardRigidityFreq = d.get("card_rigidity").asFloat(p.cardRigidityFreq);
            if (d.hasKey("flanger_depth")) p.flangerDepth = d.get("flanger_depth").asFloat(p.flangerDepth);
            if (d.hasKey("flanger_rate")) p.flangerRate = d.get("flanger_rate").asFloat(p.flangerRate);
            if (d.hasKey("flanger_feedback")) p.flangerFeedback = d.get("flanger_feedback").asFloat(p.flangerFeedback);
            regenerateVelvetBuffer(p.velvetDensity);
            return eatsbits::eatscript::Value(true);
        }
        return eatsbits::eatscript::Value(false);
    };

    auto applyPresetFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                             const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (!args.empty() && args[0].isString()) {
            std::string name = args[0].asString();
            std::string lower;
            for (char c : name) lower += static_cast<char>(std::tolower(c));
            auto& p = targetBank.physicalParams;
            if (lower == "casino" || lower == "casino_baize" || lower == "default") {
                p = {4200.0f, 0.85f, 5.0f, 0.00018f, 7500.0f, 3200.0f, 0.0f, 1.5f, 0.30f};
            } else if (lower == "glass" || lower == "crystal") {
                loadFromEatsFile("sounds/glass_crystal.eats");
                return eatsbits::eatscript::Value(true);
            } else if (lower == "plastic" || lower == "copag" || lower == "copag_plastic") {
                p = {5800.0f, 1.25f, 7.5f, 0.00011f, 11500.0f, 4400.0f, 0.0f, 1.5f, 0.30f};
            } else if (lower == "bicycle" || lower == "vintage_bicycle" || lower == "paper") {
                p = {2600.0f, 0.95f, 6.0f, 0.00022f, 6800.0f, 2900.0f, 0.0f, 1.5f, 0.30f};
            } else if (lower == "linen" || lower == "vegas_linen" || lower == "bee") {
                p = {4800.0f, 1.10f, 4.0f, 0.00015f, 8200.0f, 3600.0f, 0.0f, 1.5f, 0.30f};
            } else if (lower == "futuristic" || lower == "cyberdeck" || lower == "scifi") {
                p = {4800.0f, 1.15f, 8.5f, 0.00035f, 12500.0f, 4200.0f, 0.00055f, 1.8f, 0.38f};
            }
            regenerateVelvetBuffer(p.velvetDensity);
            return eatsbits::eatscript::Value(true);
        }
        return eatsbits::eatscript::Value(false);
    };

    auto embedNoop = [](const std::vector<eatsbits::eatscript::Value>&,
                        const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        return eatsbits::eatscript::Value(true);
    };

    evaluator.setVariable("audio", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("audio", "embed_sound", embedNoop);
    evaluator.registerMemberFunction("audio", "embed_sample", embedNoop);
    evaluator.registerMemberFunction("audio", "clear_embedded_sounds", embedNoop);
    evaluator.registerMemberFunction("audio", "clear_embeds", embedNoop);
    evaluator.registerMemberFunction("audio", "define_sound", defineSoundFn);
    evaluator.registerMemberFunction("audio", "define_sequence", defineSeqFn);
    evaluator.registerMemberFunction("audio", "set_master_variance", setVarianceFn);
    evaluator.registerMemberFunction("audio", "set_master_volume", setVolumeFn);
    evaluator.registerMemberFunction("audio", "set_physical_params", setPhysicalParamsFn);
    evaluator.registerMemberFunction("audio", "apply_preset", applyPresetFn);

    evaluator.registerFunction("embed_sound", embedNoop);
    evaluator.registerFunction("embed_sample", embedNoop);
    evaluator.registerFunction("clear_embedded_sounds", embedNoop);
    evaluator.registerFunction("clear_embeds", embedNoop);
    evaluator.registerFunction("define_sound", defineSoundFn);
    evaluator.registerFunction("define_sequence", defineSeqFn);
    evaluator.registerFunction("set_master_variance", setVarianceFn);
    evaluator.registerFunction("set_master_volume", setVolumeFn);
    evaluator.registerFunction("set_physical_params", setPhysicalParamsFn);
    evaluator.registerFunction("apply_preset", applyPresetFn);

    try {
        evaluator.evaluateSource(script);
        if (!evaluator.getLastError().empty()) {
            std::cerr << "[SolEngine Audio] Eatscript error: " << evaluator.getLastError() << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[SolEngine Audio] Exception parsing audio script: " << e.what() << std::endl;
        return false;
    }

    std::cout << "[SolEngine Audio] Successfully loaded custom audio soundpack. Master variance: "
              << targetBank.masterVariance << std::endl;

    // Atomically flip active bank
    masterVariance_.store(targetBank.masterVariance, std::memory_order_relaxed);
    activeBankIndex_.store(inactiveIdx, std::memory_order_release);
    return true;
}

bool AudioFoleyEngine::loadFromEatsFile(const std::string& filePath) {
    const std::vector<std::string> searchPaths = {
        filePath,
        "sounds/" + filePath,
        "web/sounds/" + filePath,
        "web/" + filePath,
        "../" + filePath,
        "../../" + filePath,
        "../../sounds/" + filePath,
        "../../web/sounds/" + filePath
    };

    for (const auto& candidate : searchPaths) {
        std::ifstream file(candidate);
        if (file.is_open()) {
            std::string script((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return loadFromEatsScript(script);
        }
    }
    return false;
}

void AudioFoleyEngine::setSoundPackId(const std::string& packId) {
    std::string target = packId.empty() ? "default" : packId;
    if (target == "default") {
        if (!loadFromEatsFile("sounds/default.eats")) {
            resetToDefaults();
        }
        activeSoundPackId_ = "default";
        return;
    }
    std::string filename = target;
    if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".eats") {
        filename += ".eats";
    }
    if (loadFromEatsFile(filename)) {
        activeSoundPackId_ = target;
    } else {
        resetToDefaults();
        activeSoundPackId_ = "default";
    }
}

void AudioFoleyEngine::clearAmbience() {
    activeAmbienceId_ = "off";
    ambienceEnabled_.store(false, std::memory_order_release);
}

bool AudioFoleyEngine::setAmbience(const std::string& envId) {
    std::string lower;
    for (char c : envId) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.empty() || lower == "off" || lower == "none") {
        clearAmbience();
        return true;
    }

    std::string filename = lower;
    if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".eats") {
        filename += ".eats";
    }
    if (loadAmbienceFromEatsFile(filename)) {
        activeAmbienceId_ = lower;
        return true;
    }

    // Fallback to built-in procedural definition if .eats file wasn't found
    uint32_t inactiveIdx = 1 - activeAmbienceBankIdx_.load(std::memory_order_relaxed);
    AmbienceEnvDef def{};
    if (populateBuiltinAmbience(lower, def)) {
        ambienceBanks_[inactiveIdx] = std::move(def);
        activeAmbienceBankIdx_.store(inactiveIdx, std::memory_order_release);
        ambienceGeneration_.fetch_add(1, std::memory_order_release);
        ambienceEnabled_.store(true, std::memory_order_release);
        activeAmbienceId_ = lower;
        return true;
    }
    return false;
}

bool AudioFoleyEngine::loadAmbienceFromEatsFile(const std::string& filePath) {
    const std::vector<std::string> searchPaths = {
        filePath,
        "ambience/" + filePath,
        "web/ambience/" + filePath,
        "../ambience/" + filePath,
        "../../ambience/" + filePath,
        "../" + filePath,
        "../../" + filePath
    };

    for (const auto& candidate : searchPaths) {
        std::ifstream file(candidate);
        if (file.is_open()) {
            std::string script((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return loadAmbienceFromEatsScript(script);
        }
    }
    return false;
}

bool AudioFoleyEngine::loadAmbienceFromEatsScript(const std::string& script) {
    if (script.empty()) return false;

    uint32_t inactiveIdx = 1 - activeAmbienceBankIdx_.load(std::memory_order_relaxed);
    AmbienceEnvDef parsedEnv{};
    bool hasEnv = false;

    eatsbits::eatscript::Evaluator evaluator;

    auto defineEnvFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                           const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (args.empty() || !args[0].isDict()) return eatsbits::eatscript::Value(false);
        const auto& d = args[0];

        if (d.hasKey("id") && d.get("id").isString()) parsedEnv.id = d.get("id").asString();
        if (d.hasKey("name") && d.get("name").isString()) parsedEnv.name = d.get("name").asString();
        if (d.hasKey("master_gain")) parsedEnv.masterGain = std::clamp(d.get("master_gain").asFloat(1.0f), 0.0f, 2.0f);

        parsedEnv.layers.clear();
        if (d.hasKey("layers") && d.get("layers").isList()) {
            const auto& list = d.get("layers").listVal;
            for (const auto& item : list) {
                if (!item.isDict() || parsedEnv.layers.size() >= kMaxAmbienceLayers) continue;
                AmbienceLayerDef layer{};

                std::string typeStr = (item.hasKey("type") && item.get("type").isString()) ? item.get("type").asString() : "noise_bed";
                for (char& c : typeStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (typeStr == "stochastic_grains" || typeStr == "grains") {
                    layer.type = AmbienceLayerType::StochasticGrains;
                } else if (typeStr == "chirp_spawner" || typeStr == "chirps") {
                    layer.type = AmbienceLayerType::ChirpSpawner;
                } else {
                    layer.type = AmbienceLayerType::NoiseBed;
                }

                std::string colorStr = (item.hasKey("color") && item.get("color").isString()) ? item.get("color").asString() : "pink";
                for (char& c : colorStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (colorStr == "white") layer.noiseColor = AmbienceNoiseColor::White;
                else if (colorStr == "brown" || colorStr == "red") layer.noiseColor = AmbienceNoiseColor::Brown;
                else layer.noiseColor = AmbienceNoiseColor::Pink;

                std::string filtStr = (item.hasKey("filter") && item.get("filter").isString()) ? item.get("filter").asString() : "lowpass";
                for (char& c : filtStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (filtStr == "none") layer.filterType = FoleyFilter::None;
                else if (filtStr == "highpass") layer.filterType = FoleyFilter::Highpass;
                else if (filtStr == "bandpass") layer.filterType = FoleyFilter::Bandpass;
                else layer.filterType = FoleyFilter::Lowpass;

                if (item.hasKey("gain")) layer.gain = std::clamp(item.get("gain").asFloat(0.35f), 0.0f, 2.0f);
                if (item.hasKey("cutoff")) layer.cutoff = std::clamp(item.get("cutoff").asFloat(1000.0f), 20.0f, 20000.0f);
                if (item.hasKey("q")) layer.q = std::clamp(item.get("q").asFloat(0.707f), 0.1f, 20.0f);
                if (item.hasKey("lfo_rate")) layer.lfoRate = std::clamp(item.get("lfo_rate").asFloat(0.0f), 0.0f, 20.0f);
                if (item.hasKey("lfo_depth")) layer.lfoDepth = std::max(0.0f, item.get("lfo_depth").asFloat(0.0f));
                if (item.hasKey("amp_mod")) layer.ampModDepth = std::clamp(item.get("amp_mod").asFloat(0.0f), 0.0f, 0.95f);

                if (item.hasKey("rate")) layer.rate = std::clamp(item.get("rate").asFloat(8.0f), 0.05f, 200.0f);
                if (item.hasKey("freq_min")) layer.freqMin = std::clamp(item.get("freq_min").asFloat(800.0f), 40.0f, 18000.0f);
                if (item.hasKey("freq_max")) layer.freqMax = std::clamp(item.get("freq_max").asFloat(4000.0f), layer.freqMin, 19000.0f);
                if (item.hasKey("freq_sweep")) layer.freqSweep = std::clamp(item.get("freq_sweep").asFloat(1.0f), 0.1f, 4.0f);
                if (item.hasKey("decay")) layer.decaySec = std::clamp(item.get("decay").asFloat(0.02f), 0.002f, 1.5f);
                if (item.hasKey("stereo_spread")) layer.stereoSpread = std::clamp(item.get("stereo_spread").asFloat(0.5f), 0.0f, 1.0f);
                if (item.hasKey("tonal")) layer.tonal = item.get("tonal").asBoolean(false);

                parsedEnv.layers.push_back(layer);
            }
        }
        hasEnv = !parsedEnv.layers.empty();
        return eatsbits::eatscript::Value(hasEnv);
    };

    evaluator.setVariable("ambience", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("ambience", "define_environment", defineEnvFn);
    evaluator.registerFunction("define_environment", defineEnvFn);

    try {
        evaluator.evaluateSource(script);
        if (!evaluator.getLastError().empty() || !hasEnv) {
            return false;
        }
    } catch (...) {
        return false;
    }

    ambienceBanks_[inactiveIdx] = std::move(parsedEnv);
    activeAmbienceBankIdx_.store(inactiveIdx, std::memory_order_release);
    ambienceGeneration_.fetch_add(1, std::memory_order_release);
    ambienceEnabled_.store(true, std::memory_order_release);
    return true;
}

bool AudioFoleyEngine::populateBuiltinAmbience(const std::string& envId, AmbienceEnvDef& outDef) {
    outDef = AmbienceEnvDef{};
    outDef.id = envId;
    if (envId == "whitenoise") {
        outDef.name = "White Noise";
        AmbienceLayerDef l{};
        l.type = AmbienceLayerType::NoiseBed;
        l.noiseColor = AmbienceNoiseColor::White;
        l.filterType = FoleyFilter::Lowpass;
        l.cutoff = 11000.0f;
        l.q = 0.707f;
        l.gain = 0.22f;
        outDef.layers.push_back(l);
        return true;
    }
    if (envId == "rain") {
        outDef.name = "Rain";
        AmbienceLayerDef sheet{};
        sheet.type = AmbienceLayerType::NoiseBed;
        sheet.noiseColor = AmbienceNoiseColor::Pink;
        sheet.filterType = FoleyFilter::Lowpass;
        sheet.cutoff = 2400.0f;
        sheet.q = 0.7f;
        sheet.lfoRate = 0.08f;
        sheet.lfoDepth = 450.0f;
        sheet.ampModDepth = 0.15f;
        sheet.gain = 0.34f;
        AmbienceLayerDef drops{};
        drops.type = AmbienceLayerType::StochasticGrains;
        drops.rate = 38.0f;
        drops.freqMin = 1400.0f;
        drops.freqMax = 5200.0f;
        drops.decaySec = 0.012f;
        drops.q = 3.2f;
        drops.gain = 0.28f;
        drops.stereoSpread = 0.85f;
        outDef.layers = { sheet, drops };
        return true;
    }
    if (envId == "fireplace") {
        outDef.name = "Fireplace Crackle";
        AmbienceLayerDef rumble{};
        rumble.type = AmbienceLayerType::NoiseBed;
        rumble.noiseColor = AmbienceNoiseColor::Brown;
        rumble.filterType = FoleyFilter::Lowpass;
        rumble.cutoff = 240.0f;
        rumble.gain = 0.42f;
        rumble.lfoRate = 0.14f;
        rumble.lfoDepth = 60.0f;
        AmbienceLayerDef crackle{};
        crackle.type = AmbienceLayerType::StochasticGrains;
        crackle.rate = 14.0f;
        crackle.freqMin = 2000.0f;
        crackle.freqMax = 6500.0f;
        crackle.decaySec = 0.007f;
        crackle.q = 2.4f;
        crackle.gain = 0.48f;
        crackle.stereoSpread = 0.55f;
        outDef.layers = { rumble, crackle };
        return true;
    }
    return false;
}

void AudioFoleyEngine::mixAmbience(float* outInterleavedStereo, uint32_t frameCount, float sampleRate, float ambVol) noexcept {
    const bool enabled = ambienceEnabled_.load(std::memory_order_acquire);
    const uint32_t gen = ambienceGeneration_.load(std::memory_order_acquire);

    if (gen != lastObservedAmbienceGen_) {
        lastObservedAmbienceGen_ = gen;
        for (auto& bed : ambienceBedStates_) bed.reset();
        for (auto& g : ambienceGrains_) g.active = false;
        ambienceFadeGain_ = 0.0f;
    }

    if (!enabled && ambienceFadeGain_ <= 0.0001f) {
        return;
    }

    const auto& env = ambienceBanks_[activeAmbienceBankIdx_.load(std::memory_order_acquire)];
    const float dt = 1.0f / sampleRate;
    const float fadeStep = dt / 0.35f; // 350ms smooth fade in/out
    const float targetFade = enabled ? 1.0f : 0.0f;
    const size_t numLayers = std::min(env.layers.size(), kMaxAmbienceLayers);

    auto rand01 = [&]() noexcept -> float {
        return static_cast<float>(nextRng() & 0x00FFFFFFu) / 16777216.0f;
    };

    for (uint32_t f = 0; f < frameCount; ++f) {
        if (ambienceFadeGain_ < targetFade) {
            ambienceFadeGain_ = std::min(targetFade, ambienceFadeGain_ + fadeStep);
        } else if (ambienceFadeGain_ > targetFade) {
            ambienceFadeGain_ = std::max(targetFade, ambienceFadeGain_ - fadeStep);
        }
        if (ambienceFadeGain_ <= 0.0001f && !enabled) {
            break;
        }

        float ambL = 0.0f;
        float ambR = 0.0f;

        for (size_t li = 0; li < numLayers; ++li) {
            const auto& layer = env.layers[li];
            auto& bed = ambienceBedStates_[li];

            if (layer.type == AmbienceLayerType::NoiseBed) {
                float wL = nextNoise();
                float wR = nextNoise();
                float sL = wL;
                float sR = wR;

                if (layer.noiseColor == AmbienceNoiseColor::Pink) {
                    bed.pinkL0 = 0.99765f * bed.pinkL0 + wL * 0.0990460f;
                    bed.pinkL1 = 0.96300f * bed.pinkL1 + wL * 0.2965164f;
                    bed.pinkL2 = 0.57000f * bed.pinkL2 + wL * 1.0526913f;
                    sL = (bed.pinkL0 + bed.pinkL1 + bed.pinkL2 + wL * 0.1848f) * 0.18f;

                    bed.pinkR0 = 0.99765f * bed.pinkR0 + wR * 0.0990460f;
                    bed.pinkR1 = 0.96300f * bed.pinkR1 + wR * 0.2965164f;
                    bed.pinkR2 = 0.57000f * bed.pinkR2 + wR * 1.0526913f;
                    sR = (bed.pinkR0 + bed.pinkR1 + bed.pinkR2 + wR * 0.1848f) * 0.18f;
                } else if (layer.noiseColor == AmbienceNoiseColor::Brown) {
                    bed.brownL = std::clamp((bed.brownL + wL * 0.045f) * 0.9965f, -1.0f, 1.0f);
                    bed.brownR = std::clamp((bed.brownR + wR * 0.045f) * 0.9965f, -1.0f, 1.0f);
                    sL = bed.brownL * 1.6f;
                    sR = bed.brownR * 1.6f;
                } else {
                    sL = wL * 0.45f;
                    sR = wR * 0.45f;
                }

                // Dual-sine organic LFO for non-repetitive gusts & swells
                float lfoVal = 0.0f;
                if (layer.lfoRate > 0.001f) {
                    bed.lfoPhase1 += kTwoPi * layer.lfoRate * dt;
                    bed.lfoPhase2 += kTwoPi * (layer.lfoRate * 1.37f) * dt;
                    if (bed.lfoPhase1 >= kTwoPi) bed.lfoPhase1 -= kTwoPi;
                    if (bed.lfoPhase2 >= kTwoPi) bed.lfoPhase2 -= kTwoPi;
                    lfoVal = 0.65f * std::sin(bed.lfoPhase1) + 0.35f * std::sin(bed.lfoPhase2);
                }

                if ((bed.subCounter++ & 31) == 0 && layer.filterType != FoleyFilter::None) {
                    float modCutoff = std::clamp(layer.cutoff + lfoVal * layer.lfoDepth, 30.0f, sampleRate * 0.42f);
                    BiquadCoeffs c{};
                    if (layer.filterType == FoleyFilter::Highpass) c = makeHighpass(modCutoff, layer.q, sampleRate);
                    else if (layer.filterType == FoleyFilter::Bandpass) c = makeBandpass(modCutoff, layer.q, sampleRate);
                    else c = makeLowpass(modCutoff, layer.q, sampleRate);
                    bed.filterL.c = c;
                    bed.filterR.c = c;
                }

                if (layer.filterType != FoleyFilter::None) {
                    sL = bed.filterL.process(sL);
                    sR = bed.filterR.process(sR);
                }

                float ampMod = 1.0f + lfoVal * layer.ampModDepth;
                ambL += sL * layer.gain * ampMod;
                ambR += sR * layer.gain * ampMod;
            } else {
                // StochasticGrains / ChirpSpawner: check spawn every 32 samples
                if ((bed.subCounter++ & 31) == 0) {
                    float prob = layer.rate * (32.0f * dt);
                    if (rand01() < prob) {
                        for (auto& g : ambienceGrains_) {
                            if (!g.active) {
                                g.active = true;
                                g.isChirp = (layer.type == AmbienceLayerType::ChirpSpawner);
                                g.tonal = layer.tonal || g.isChirp;
                                g.ageSec = 0.0f;
                                g.durationSec = g.isChirp
                                    ? layer.decaySec * (0.75f + 0.55f * rand01())
                                    : std::max(0.004f, layer.decaySec * (0.65f + 0.75f * rand01()) * 4.5f);
                                g.decayRate = 1.0f / std::max(0.002f, layer.decaySec * (0.75f + 0.5f * rand01()));
                                g.phase = 0.0f;
                                g.freq = layer.freqMin + (layer.freqMax - layer.freqMin) * rand01();
                                float endFreq = std::clamp(g.freq * layer.freqSweep, 40.0f, sampleRate * 0.42f);
                                float totalSamples = std::max(1.0f, g.durationSec * sampleRate);
                                g.freqMult = std::pow(endFreq / std::max(40.0f, g.freq), 1.0f / totalSamples);
                                g.chirpModPhase = 0.0f;
                                g.chirpModRate = 14.0f + 16.0f * rand01();
                                g.chirpModDepth = g.isChirp ? 0.14f : 0.0f;

                                float pan = (rand01() * 2.0f - 1.0f) * layer.stereoSpread;
                                float amp = layer.gain * (0.55f + 0.45f * rand01());
                                g.gainL = amp * std::sqrt(0.5f * (1.0f - pan));
                                g.gainR = amp * std::sqrt(0.5f * (1.0f + pan));
                                g.filter.reset();
                                if (!g.tonal) {
                                    g.filter.c = makeBandpass(g.freq, layer.q, sampleRate);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Render active ambient grains / chirps
        for (auto& g : ambienceGrains_) {
            if (!g.active) continue;
            g.ageSec += dt;
            if (g.ageSec >= g.durationSec) {
                g.active = false;
                continue;
            }

            float sample = 0.0f;
            if (g.tonal) {
                float instFreq = g.freq;
                if (g.isChirp) {
                    g.chirpModPhase += kTwoPi * g.chirpModRate * dt;
                    instFreq *= (1.0f + g.chirpModDepth * std::sin(g.chirpModPhase));
                }
                g.phase += kTwoPi * instFreq * dt;
                if (g.phase >= kTwoPi) g.phase -= kTwoPi;
                g.freq *= g.freqMult;

                float grainEnv = 1.0f;
                if (g.isChirp) {
                    float tNorm = g.ageSec / g.durationSec;
                    // Smooth bell/2-syllable woodland chirp envelope
                    grainEnv = std::sin(kPi * tNorm) * (0.65f + 0.35f * std::sin(kTwoPi * 2.0f * tNorm));
                } else {
                    float att = std::min(1.0f, g.ageSec / 0.0025f);
                    grainEnv = att * std::exp(-g.ageSec * g.decayRate);
                }
                sample = std::sin(g.phase) * grainEnv * 0.35f;
            } else {
                float grainEnv = std::exp(-g.ageSec * g.decayRate);
                sample = g.filter.process(nextNoise()) * grainEnv;
            }

            ambL += sample * g.gainL;
            ambR += sample * g.gainR;
        }

        const float scale = ambVol * env.masterGain * ambienceFadeGain_;
        outInterleavedStereo[f * 2 + 0] = std::clamp(outInterleavedStereo[f * 2 + 0] + ambL * scale, -0.95f, 0.95f);
        outInterleavedStereo[f * 2 + 1] = std::clamp(outInterleavedStereo[f * 2 + 1] + ambR * scale, -0.95f, 0.95f);
    }
}

} // namespace solengine
