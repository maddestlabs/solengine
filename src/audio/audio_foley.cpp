#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "solengine/audio_foley.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/ui/base64.hpp"
#include "stb_image.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace solengine {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.5707963267948966f;

static bool decodeEmbeddedAudio(const std::string& b64String, std::vector<float>& outPcm) {
    if (b64String.empty()) return false;

    // 1. Base64 decode
    std::vector<uint8_t> rawBytes = eatsbits::ui::base64Decode(b64String);
    if (rawBytes.empty()) return false;

    // 2. Check if zlib-compressed (WAV files start with "RIFF" 0x52 0x49 0x46 0x46)
    const uint8_t* pAudioData = rawBytes.data();
    size_t audioDataSize = rawBytes.size();
    char* decomp = nullptr;

    bool isRiff = (rawBytes.size() >= 4 && rawBytes[0] == 'R' && rawBytes[1] == 'I' && rawBytes[2] == 'F' && rawBytes[3] == 'F');
    if (!isRiff) {
        int outLen = 0;
        decomp = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(rawBytes.data()), static_cast<int>(rawBytes.size()), &outLen);
        if (!decomp) {
            decomp = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(rawBytes.data()), static_cast<int>(rawBytes.size()), &outLen);
        }
        if (decomp && outLen > 0) {
            pAudioData = reinterpret_cast<const uint8_t*>(decomp);
            audioDataSize = static_cast<size_t>(outLen);
        }
    }

    // 3. Decode via miniaudio memory decoder (automatically resamples to 48kHz mono float)
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    ma_result result = ma_decoder_init_memory(pAudioData, audioDataSize, &config, &decoder);
    if (result != MA_SUCCESS) {
        if (decomp) free(decomp);
        return false;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (totalFrames == 0) {
        totalFrames = 48000 * 2;
    }

    outPcm.resize(static_cast<size_t>(totalFrames));
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, outPcm.data(), totalFrames, &framesRead);
    outPcm.resize(static_cast<size_t>(framesRead));

    ma_decoder_uninit(&decoder);
    if (decomp) free(decomp);

    return !outPcm.empty();
}

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
    // Captures cellulose resonance formants (3.5kHz, 6.3kHz, 11.0kHz, 13.7kHz) with LSD 1.08 dB
    static constexpr std::array<float, 16> kLpcDeal = {
        -1.1232f, 1.0078f, -0.8596f, 0.7548f, -0.6460f, 0.5738f, -0.6435f, 0.5256f,
        -0.5334f, 0.5139f, -0.5716f, 0.3876f, -0.4906f, 0.3628f, -0.3130f, 0.1184f
    };
    static constexpr float kLpcDealGain = 0.1317f;

    struct ShuffleSnapEvent {
        float timeSec;
        float amplitude;
    };

    // Empirical card release schedules extracted from physical deck recordings:
    static constexpr size_t kSoftSnapCount = 8;
    static constexpr std::array<ShuffleSnapEvent, kSoftSnapCount> kSoftSnaps = {{
        { 0.048f, 0.30f },
        { 0.096f, 0.22f },
        { 0.138f, 0.70f },
        { 0.174f, 0.40f },
        { 0.207f, 0.36f },
        { 0.237f, 0.45f },
        { 0.288f, 0.65f },
        { 0.342f, 0.32f }
    }};

    static constexpr size_t kLoudSnapCount = 13;
    static constexpr std::array<ShuffleSnapEvent, kLoudSnapCount> kLoudSnaps = {{
        { 0.057f, 0.35f },
        { 0.090f, 0.45f },
        { 0.111f, 0.25f },
        { 0.132f, 0.50f },
        { 0.150f, 0.32f },
        { 0.177f, 0.20f },
        { 0.192f, 0.46f },
        { 0.204f, 0.88f },
        { 0.222f, 0.75f },
        { 0.243f, 0.80f },
        { 0.276f, 1.00f },
        { 0.300f, 0.85f },
        { 0.327f, 0.55f }
    }};

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
    auto makeSlideVoice = [&](float delay, float amp, float duration = 0.11f, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::Slide;
        v.soundId = FoleySound::Slide;
        v.delaySec = std::max(0.0f, delay);
        v.durationSec = std::clamp(duration * (1.0f + nextNoise() * 0.08f * varScale), 0.05f, 0.35f);
        v.intensity = amp;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 0.95f + std::abs(nextNoise()) * 0.15f * varScale;
        v.continuousIdx = static_cast<float>(nextRng() % kNoiseBufferSize);

        float feltCutoff = pp.feltDampingCutoff * (1.0f + nextNoise() * 0.06f * varScale);
        float airBoost = (pp.airBoostDb * 0.4f) + nextNoise() * 0.4f * varScale;

        // Biquad 1: Felt acoustic damping lowpass
        v.biquad1.c = makeLowpass(feltCutoff, 0.707f, sr);
        // Biquad 2: High-frequency air presence shelf
        v.biquad2.c = makeHighshelf(8500.0f, airBoost, sr);

        v.combDelaySamples = 0.0f;
        v.lfoPhase = 0.0f;
        return v;
    };

    // Helper to spawn an authentic single-voice Procedural Shuffle Riffle
    // Physical phenomenon: Interleaving card flutter friction with discrete micro-snap releases & Baize gather
    auto makeShuffleRiffleVoice = [&](ShuffleStyle style, float delay, float amp, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::ShuffleRiffle;
        v.soundId = FoleySound::Shuffle;
        v.delaySec = std::max(0.0f, delay);
        v.shuffleMode = (style == ShuffleStyle::Loud) ? 1 : 0;
        v.durationSec = (style == ShuffleStyle::Loud) ? (0.46f * (1.0f + nextNoise() * 0.05f * varScale))
                                                      : (0.42f * (1.0f + nextNoise() * 0.05f * varScale));
        v.intensity = amp;
        v.pan = std::clamp(voicePan + nextNoise() * 0.035f * varScale, -1.0f, 1.0f);
        v.velvetIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.velvetPlaybackRate = 1.0f + nextNoise() * 0.08f * varScale;
        v.continuousIdx = static_cast<float>(nextRng() % kNoiseBufferSize);
        v.snapEnv = 0.0f;
        v.snapIdx = 0;

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

        // Filter 1: Main card face air slap bandpass (3800 Hz)
        float slapFreq = 3800.0f * (1.0f + nextNoise() * 0.06f * varScale);
        float slapQ = 1.4f * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad1.c = makeBandpass(slapFreq, slapQ, sr);

        // Filter 2: Leading edge touch tick bandpass (5800 Hz)
        float edgeFreq = 5800.0f * (1.0f + nextNoise() * 0.05f * varScale);
        float edgeQ = 1.8f * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad2.c = makeBandpass(edgeFreq, edgeQ, sr);

        // Filter 3: Card body acoustic knock bandpass (920 Hz)
        float knockFreq = 920.0f * (1.0f + nextNoise() * 0.07f * varScale);
        float knockQ = 1.6f * (1.0f + nextNoise() * 0.08f * varScale);
        v.biquad3.c = makeBandpass(knockFreq, knockQ, sr);

        // Low table coupling thump: starts around 95Hz and rapidly settles to 48Hz
        float fStart = 95.0f * (1.0f + nextNoise() * 0.05f * varScale);
        float fEnd = 48.0f * (1.0f + nextNoise() * 0.05f * varScale);
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

    // Helper to spawn an embedded authentic sample playback voice
    auto makeSampleVoice = [&](const std::vector<float>& pcm, float delay, float amp, float pitchMod = 1.0f, float voicePan = 0.0f) noexcept -> Voice {
        Voice v{};
        v.reset();
        v.active = true;
        v.type = VoiceType::SamplePlayback;
        v.delaySec = std::max(0.0f, delay);
        v.intensity = amp;
        v.pan = std::clamp(voicePan, -1.0f, 1.0f);
        v.sampleData = pcm.data();
        v.sampleLength = pcm.size();
        v.sampleCursor = 0.0f;
        v.sampleRateRatio = std::clamp(pitchMod, 0.5f, 2.0f);
        v.durationSec = (static_cast<float>(pcm.size()) / 48000.0f) / v.sampleRateRatio + v.delaySec;
        return v;
    };

    // 1. Custom Procedural DSP & Modal Resonator Layer Fast-Path (audio.define_sound):
    // If the active Eatscript soundpack defined custom procedural synth layers or modal physics,
    // they take highest priority over both embedded samples and default routines:
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

    // 2. Embedded Authentic Audio Sample Playback (audio.embed_sound):
    // If the active Eatscript soundpack embedded authentic physical recordings, play them:
    if (soundId == FoleySound::Place && !bank.samplePlace.empty()) {
        float pitch = 1.0f + (nextNoise() * 0.035f * varScale);
        allocateVoice(makeSampleVoice(bank.samplePlace, 0.0f, intensity * 1.05f, pitch, pan));
        return;
    }
    if (soundId == FoleySound::Deal && !bank.sampleDeal.empty()) {
        float pitch = 1.0f + (nextNoise() * 0.035f * varScale);
        allocateVoice(makeSampleVoice(bank.sampleDeal, 0.0f, intensity * 1.10f, pitch, pan));
        return;
    }
    if (soundId == FoleySound::Slide && !bank.sampleDeal.empty()) {
        float pitch = 0.95f + (nextNoise() * 0.035f * varScale);
        allocateVoice(makeSampleVoice(bank.sampleDeal, 0.0f, intensity * 0.95f, pitch, pan));
        return;
    }
    if (soundId == FoleySound::Shuffle) {
        const auto style = shuffleStyle_.load(std::memory_order_relaxed);
        const auto& pcm = (style == ShuffleStyle::Loud && !bank.sampleShuffleLoud.empty())
            ? bank.sampleShuffleLoud
            : bank.sampleShuffleSoft;
        if (!pcm.empty()) {
            float pitch = 1.0f + (nextNoise() * 0.025f * varScale);
            allocateVoice(makeSampleVoice(pcm, 0.0f, intensity * 1.15f, pitch, pan));
            return;
        }
    }
    if (soundId == FoleySound::Flip && !bank.sampleDeal.empty() && !bank.samplePlace.empty()) {
        float pitchSlide = 1.05f + (nextNoise() * 0.04f * varScale);
        float pitchDrop = 1.0f + (nextNoise() * 0.04f * varScale);
        allocateVoice(makeSampleVoice(bank.sampleDeal, 0.0f, intensity * 0.90f, pitchSlide, pan));
        allocateVoice(makeSampleVoice(bank.samplePlace, 0.11f, intensity * 1.0f, pitchDrop, pan));
        return;
    }
    if (soundId == FoleySound::TexasPitch && !bank.sampleDeal.empty()) {
        for (int i = 0; i < 4; ++i) {
            float tossDelay = static_cast<float>(i) * 0.145f + (nextNoise() * 0.006f * varScale);
            float pitch = 0.98f + static_cast<float>(i) * 0.035f + nextNoise() * 0.03f * varScale;
            float tossPan = std::clamp(pan + (static_cast<float>(i) - 1.5f) * 0.08f * varScale, -1.0f, 1.0f);
            allocateVoice(makeSampleVoice(bank.sampleDeal, tossDelay, intensity * 0.90f, pitch, tossPan));
        }
        return;
    }
    if (soundId == FoleySound::Snap && !bank.sampleSnap.empty()) {
        float pitch = 1.0f + (nextNoise() * 0.05f * varScale);
        allocateVoice(makeSampleVoice(bank.sampleSnap, 0.0f, intensity * 1.0f, pitch, pan));
        return;
    }

    // 3. HD Physical Procedural Synthesis Routines (Built-in LPC-16, Dirac snaps, 2-stage touchdown, discrete riffle):
    switch (soundId) {
        case FoleySound::Snap: {
            // Core Dirac Edge Snap (Card click / tap / selection interaction)
            float pitchJitter = 1.0f + (nextNoise() * 0.075f * varScale);
            float snapAmp = intensity * (1.0f + nextNoise() * 0.06f * varScale);
            allocateVoice(makeSnapVoice(0.0f, snapAmp, pitchJitter, pan));
            break;
        }

        case FoleySound::Deal: {
            // Authentic Deal swipe (pre-peel micro-tick into velvet Baize slide from deal-series.wav)
            float pitchJitter = 1.0f + (nextNoise() * 0.05f * varScale);
            float snapAmp = 0.55f * intensity * (1.0f + nextNoise() * 0.06f * varScale);
            float slideDelay = 0.008f + std::abs(nextNoise()) * 0.004f * varScale;
            float slideDur = 0.11f * (1.0f + nextNoise() * 0.08f * varScale);
            float slideAmp = 0.95f * intensity * (1.0f + nextNoise() * 0.05f * varScale);

            allocateVoice(makeSnapVoice(0.0f, snapAmp, pitchJitter * 1.15f, pan));
            allocateVoice(makeSlideVoice(slideDelay, slideAmp, slideDur, pan));
            break;
        }

        case FoleySound::Place: {
            // Table Drop / Thud (card landing on surface)
            float dropAmp = intensity * (1.0f + nextNoise() * 0.06f * varScale);
            allocateVoice(makeDropVoice(0.0f, dropAmp, pan));
            break;
        }

        case FoleySound::Shuffle: {
            // Authentic Single-Voice Procedural Riffle Cascade
            // Physical model: Interleaving card flutter friction with accelerating micro-snap releases & Baize gather
            const auto style = shuffleStyle_.load(std::memory_order_relaxed);
            allocateVoice(makeShuffleRiffleVoice(style, 0.0f, intensity, pan));
            break;
        }

        case FoleySound::TexasPitch: {
            // Texas Pitch (4 crisp dealer tosses with trailing friction)
            for (int i = 0; i < 4; ++i) {
                float tossDelay = static_cast<float>(i) * 0.145f + (nextNoise() * 0.006f * varScale);
                float slideDelay = tossDelay + 0.010f + std::abs(nextNoise()) * 0.004f * varScale;
                float pitch = 0.98f + static_cast<float>(i) * 0.035f + nextNoise() * 0.03f * varScale;
                float tossPan = std::clamp(pan + (static_cast<float>(i) - 1.5f) * 0.08f * varScale, -1.0f, 1.0f);
                float tossAmp = intensity * 0.90f * (1.0f + nextNoise() * 0.05f * varScale);

                allocateVoice(makeSnapVoice(tossDelay, tossAmp * 0.60f, pitch, tossPan));
                allocateVoice(makeSlideVoice(slideDelay, tossAmp * 0.95f, 0.10f, tossPan));
            }
            break;
        }

        case FoleySound::Flip: {
            // Turnover Flip (Edge peel friction into crisp snap & drop)
            float slideDur = 0.12f * (1.0f + nextNoise() * 0.10f * varScale);
            float contactDelay = 0.105f + std::abs(nextNoise()) * 0.015f * varScale;
            float snapPitch = 1.15f * (1.0f + nextNoise() * 0.06f * varScale);

            allocateVoice(makeSlideVoice(0.0f, intensity * 1.40f, slideDur, pan));
            allocateVoice(makeSnapVoice(contactDelay, intensity * 0.75f, snapPitch, pan));
            allocateVoice(makeDropVoice(contactDelay, intensity * 0.85f, pan));
            break;
        }

        case FoleySound::Slide: {
            // Standalone card drag / glide sweep across felt
            float slideDur = 0.11f * (1.0f + nextNoise() * 0.08f * varScale);
            float slideAmp = intensity * 0.88f * (1.0f + nextNoise() * 0.05f * varScale);
            allocateVoice(makeSlideVoice(0.0f, slideAmp, slideDur, pan));
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

                        // Authentic physical deal glide envelope:
                        // 14ms attack (card acceleration), body glide, exponential decay (friction deceleration)
                        float fricGain = 0.0f;
                        const float attT = 0.014f;
                        if (v.ageSec < attT) {
                            fricGain = 0.95f * (v.ageSec / attT);
                        } else {
                            float decayFraction = (v.ageSec - attT) / std::max(0.010f, v.durationSec - attT);
                            fricGain = 0.95f * std::exp(-5.2f * decayFraction);
                        }

                        sample = airFiltered * fricGain * v.intensity * 2.2f;

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
                        // Stage 1 (t=0 to ~8ms): Leading corner/edge touch (crisp 5.8kHz micro-tick)
                        // Stage 2 (t >= ~9.5ms): Card face air slap (3.8kHz) + body knock (920Hz) + table thump (95->48Hz)
                        const float splitSec = 0.0095f;
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
                        if (v.ageSec < 0.009f) {
                            float edgeFilt = v.biquad2.process(noiseSample);
                            float edgeGain = 0.0f;
                            if (v.ageSec < 0.0008f) {
                                edgeGain = 0.95f * (v.ageSec / 0.0008f);
                            } else {
                                edgeGain = 0.95f * std::exp(-(v.ageSec - 0.0008f) / 0.0025f);
                            }
                            edgeOut = edgeFilt * edgeGain;
                        }

                        // Stage 2: Main touchdown
                        if (v.ageSec >= splitSec) {
                            const float bodyAge = v.ageSec - splitSec;

                            // 1. Air Slap (3.8kHz)
                            float slapFilt = v.biquad1.process(noiseSample);
                            float slapGain = 0.0f;
                            if (bodyAge < 0.0010f) {
                                slapGain = 1.10f * (bodyAge / 0.0010f);
                            } else {
                                slapGain = 1.10f * std::exp(-(bodyAge - 0.0010f) / 0.0055f);
                            }
                            slapOut = slapFilt * slapGain;

                            // 2. Card Body Knock (920Hz)
                            float knockFilt = v.biquad3.process(noiseSample);
                            float knockGain = 0.0f;
                            if (bodyAge < 0.0012f) {
                                knockGain = 0.45f * (bodyAge / 0.0012f);
                            } else {
                                knockGain = 0.45f * std::exp(-(bodyAge - 0.0012f) / 0.0065f);
                            }
                            knockOut = knockFilt * knockGain;

                            // 3. Deep Table Thump (95Hz -> 48Hz in 20ms)
                            v.dropFreq = std::max(48.0f, v.dropFreq * v.dropFreqStep);
                            v.phaseSine += v.dropFreq * dt;
                            if (v.phaseSine >= 1.0f) v.phaseSine -= 1.0f;
                            float rawSine = std::sin(v.phaseSine * kTwoPi);
                            float thudGain = 0.16f * std::exp(-bodyAge / 0.014f);
                            thudOut = rawSine * thudGain;
                        }

                        sample = (edgeOut + slapOut + knockOut + thudOut) * v.intensity * 0.95f;
                        break;
                    }

                    case VoiceType::ShuffleRiffle: {
                        // 100% Procedural Single-Voice Discrete Transient Riffle Cascade
                        // Models empirical discrete physical card corner releases (8 snaps for Soft, 13 snaps for Loud)
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
                        const size_t snapCount = isLoud ? kLoudSnapCount : kSoftSnapCount;

                        // Check empirical card snap schedule:
                        if (v.snapIdx < snapCount) {
                            float eventTime = isLoud ? kLoudSnaps[v.snapIdx].timeSec : kSoftSnaps[v.snapIdx].timeSec;
                            if (v.ageSec >= eventTime) {
                                float eventAmp = isLoud ? kLoudSnaps[v.snapIdx].amplitude : kSoftSnaps[v.snapIdx].amplitude;
                                v.snapEnv = eventAmp;
                                v.snapIdx++;
                            }
                        }

                        // Exponential decay of discrete card corner snap (~2.8 ms)
                        v.snapEnv *= 0.9926f;

                        // 1. Discrete card corner snap impulse (formant bandpass + crisp air presence)
                        float snapSignal = inNoise * v.snapEnv;
                        float snapBp = v.biquad1.process(snapSignal);
                        float snapAir = v.biquad2.process(snapSignal) * 0.35f;
                        float snapOut = (snapBp + snapAir) * (isLoud ? 1.5f : 1.1f);

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

                    case VoiceType::SamplePlayback: {
                        if (!v.sampleData || v.sampleLength == 0) {
                            v.active = false;
                            break;
                        }
                        size_t idx0 = static_cast<size_t>(v.sampleCursor);
                        if (idx0 + 1 >= v.sampleLength) {
                            v.active = false;
                            break;
                        }
                        float frac = v.sampleCursor - static_cast<float>(idx0);
                        float s0 = v.sampleData[idx0];
                        float s1 = v.sampleData[idx0 + 1];
                        sample = (s0 + frac * (s1 - s0)) * v.intensity;
                        v.sampleCursor += v.sampleRateRatio;
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
}

bool AudioFoleyEngine::loadFromEatsScript(const std::string& script) {
    if (script.empty()) return false;

    uint32_t inactiveIdx = 1 - activeBankIndex_.load(std::memory_order_relaxed);
    FoleySoundBank& targetBank = soundBanks_[inactiveIdx];
    // Copy current configuration as baseline
    targetBank = soundBanks_[activeBankIndex_.load(std::memory_order_relaxed)];

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

    auto embedSoundFn = [&](const std::vector<eatsbits::eatscript::Value>& args,
                            const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (args.size() < 2 || !args[0].isString() || !args[1].isString()) {
            return eatsbits::eatscript::Value(false);
        }
        std::string soundName = args[0].asString();
        std::string b64Data = args[1].asString();
        for (char& c : soundName) c = static_cast<char>(std::tolower(c));

        std::vector<float> decodedPcm;
        if (!decodeEmbeddedAudio(b64Data, decodedPcm)) {
            std::cerr << "[SolEngine Foley] Failed to decode embedded audio for: " << soundName << "\n";
            return eatsbits::eatscript::Value(false);
        }

        if (soundName == "place" || soundName == "drop" || soundName == "card_place" || soundName == "card_drop") {
            targetBank.samplePlace = std::move(decodedPcm);
        } else if (soundName == "deal" || soundName == "slide" || soundName == "card_deal" || soundName == "card_slide" || soundName == "deal_series") {
            targetBank.sampleDeal = std::move(decodedPcm);
        } else if (soundName == "shuffle" || soundName == "shuffle_soft" || soundName == "deck_shuffle") {
            targetBank.sampleShuffleSoft = std::move(decodedPcm);
        } else if (soundName == "shuffle_loud") {
            targetBank.sampleShuffleLoud = std::move(decodedPcm);
        } else if (soundName == "snap" || soundName == "tap" || soundName == "card_snap") {
            targetBank.sampleSnap = std::move(decodedPcm);
        } else {
            return eatsbits::eatscript::Value(false);
        }
        return eatsbits::eatscript::Value(true);
    };

    auto clearEmbedsFn = [&](const std::vector<eatsbits::eatscript::Value>&,
                             const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        targetBank.samplePlace.clear();
        targetBank.sampleDeal.clear();
        targetBank.sampleShuffleSoft.clear();
        targetBank.sampleShuffleLoud.clear();
        targetBank.sampleSnap.clear();
        return eatsbits::eatscript::Value(true);
    };

    evaluator.setVariable("audio", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
    evaluator.registerMemberFunction("audio", "embed_sound", embedSoundFn);
    evaluator.registerMemberFunction("audio", "embed_sample", embedSoundFn);
    evaluator.registerMemberFunction("audio", "clear_embedded_sounds", clearEmbedsFn);
    evaluator.registerMemberFunction("audio", "clear_embeds", clearEmbedsFn);
    evaluator.registerMemberFunction("audio", "define_sound", defineSoundFn);
    evaluator.registerMemberFunction("audio", "define_sequence", defineSeqFn);
    evaluator.registerMemberFunction("audio", "set_master_variance", setVarianceFn);
    evaluator.registerMemberFunction("audio", "set_master_volume", setVolumeFn);
    evaluator.registerMemberFunction("audio", "set_physical_params", setPhysicalParamsFn);
    evaluator.registerMemberFunction("audio", "apply_preset", applyPresetFn);

    evaluator.registerFunction("embed_sound", embedSoundFn);
    evaluator.registerFunction("embed_sample", embedSoundFn);
    evaluator.registerFunction("clear_embedded_sounds", clearEmbedsFn);
    evaluator.registerFunction("clear_embeds", clearEmbedsFn);
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

} // namespace solengine
