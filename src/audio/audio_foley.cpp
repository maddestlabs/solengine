#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "solengine/audio_foley.hpp"
#include <algorithm>
#include <cmath>

namespace solengine {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

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

void miniaudioCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    auto* engine = static_cast<AudioFoleyEngine*>(pDevice->pUserData);
    if (engine && pOutput) {
        engine->mixSamples(static_cast<float*>(pOutput), frameCount, pDevice->sampleRate);
    }
}
} // namespace

AudioFoleyEngine::AudioFoleyEngine() = default;

AudioFoleyEngine::~AudioFoleyEngine() {
    shutdown();
}

bool AudioFoleyEngine::init(bool enableHardwareDevice) {
    if (initialized_) return true;
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

float AudioFoleyEngine::nextNoise() noexcept {
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return static_cast<float>(static_cast<int32_t>(rngState_)) * (1.0f / 2147483648.0f);
}

void AudioFoleyEngine::triggerSound(uint16_t soundId, float intensity) {
    uint32_t w = writeIdx_.load(std::memory_order_relaxed);
    uint32_t r = readIdx_.load(std::memory_order_acquire);
    if (w - r < kQueueCapacity) {
        cmdQueue_[w % kQueueCapacity] = {soundId, intensity};
        writeIdx_.store(w + 1, std::memory_order_release);
    }
}

void AudioFoleyEngine::allocateVoice(const Voice& v) noexcept {
    for (auto& slot : voices_) {
        if (!slot.active) {
            slot = v;
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
    voices_[oldestIdx] = v;
}

void AudioFoleyEngine::handleTrigger(uint16_t soundId, float intensity, float sampleRate) noexcept {
    const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;

    auto makeDealVoice = [&](float delay, float amp, float durScale = 1.0f) noexcept -> Voice {
        Voice v{};
        v.active = true;
        v.soundId = FoleySound::Deal;
        v.delaySec = std::max(0.0f, delay);
        v.intensity = amp * (0.92f + nextNoise() * 0.08f);
        v.jitter = 1.0f + (nextNoise() * 0.085f);
        float durJitter = 1.0f + (nextNoise() * 0.12f);
        v.durationSec = std::clamp(0.056f * durScale * durJitter, 0.036f, 0.085f);
        v.fBody = (2600.0f + nextNoise() * 320.0f) * v.jitter;
        // Pure high-frequency soft sweeping filters (no bass thud)
        v.snapFilter.c = makeHighpass(3600.0f * v.jitter, 0.75f, sr);
        v.slideFilter1.c = makeBandpass(2450.0f * v.jitter, 1.05f, sr);
        v.slideFilter2.c = makeBandpass(4650.0f * v.jitter, 1.20f, sr);
        return v;
    };

    auto makePlaceVoice = [&](float delay, float amp) noexcept -> Voice {
        Voice v{};
        v.active = true;
        v.soundId = FoleySound::Place;
        v.delaySec = std::max(0.0f, delay);
        v.intensity = amp * (0.92f + nextNoise() * 0.08f);
        v.jitter = 1.0f + (nextNoise() * 0.075f);
        float durJitter = 1.0f + (nextNoise() * 0.10f);
        v.durationSec = std::clamp(0.072f * durJitter, 0.052f, 0.095f);
        v.fBody = (132.0f + nextNoise() * 14.0f) * v.jitter;
        v.thudDelaySec = 0.0f;
        // Warm low-mid felt thump + muted parchment contact
        v.bodyFilter.c = makeLowpass(340.0f * v.jitter, 0.85f, sr);
        v.slideFilter1.c = makeBandpass(680.0f * v.jitter, 1.10f, sr);
        v.snapFilter.c = makeBandpass(1850.0f * v.jitter, 1.15f, sr);
        return v;
    };

    auto makeSweepVoice = [&](float delay, float amp, float durScale = 1.0f) noexcept -> Voice {
        Voice v{};
        v.active = true;
        v.soundId = FoleySound::Sweep;
        v.delaySec = std::max(0.0f, delay);
        v.intensity = amp * (0.92f + nextNoise() * 0.08f);
        v.jitter = 1.0f + (nextNoise() * 0.09f);
        float durJitter = 1.0f + (nextNoise() * 0.14f);
        v.durationSec = std::clamp(0.245f * durScale * durJitter, 0.120f, 0.340f);
        v.fBody = (1450.0f + nextNoise() * 180.0f) * v.jitter;
        // Smooth multi-band baize drag friction filters
        v.bodyFilter.c = makeBandpass(920.0f * v.jitter, 0.90f, sr);
        v.slideFilter1.c = makeBandpass(1680.0f * v.jitter, 0.95f, sr);
        v.slideFilter2.c = makeBandpass(2950.0f * v.jitter, 1.15f, sr);
        v.snapFilter.c = makeHighpass(3800.0f * v.jitter, 0.80f, sr);
        return v;
    };

    if (soundId == FoleySound::Flip) {
        // Flip: Deal + Place sound in relatively quick succession after each other
        allocateVoice(makeDealVoice(0.0f, intensity * 0.92f, 0.92f));
        float placeDelay = 0.048f + (nextNoise() * 0.009f);
        allocateVoice(makePlaceVoice(placeDelay, intensity * 0.82f));
        return;
    }

    if (soundId == FoleySound::Shuffle) {
        // Shuffle:
        // 1. Starts with a Sweep sound.
        // 2. Followed by multiple Flip (Deal + Place) sounds in quick succession.
        // 3. Sweep sounds interspersed at arbitrary intervals between the Place sounds.
        const float styleBoost = (shuffleStyle_.load(std::memory_order_relaxed) == ShuffleStyle::Loud) ? 1.08f : 0.92f;
        const float baseAmp = intensity * styleBoost;

        // Opening Sweep sound
        allocateVoice(makeSweepVoice(0.0f, baseAmp * 0.90f, 0.90f));

        float cursorSec = 0.165f + (nextNoise() * 0.020f);
        const int flipCount = 6 + static_cast<int>(std::abs(nextNoise()) * 2.9f); // 6 to 8 flips
        int sweepsInterspersed = 0;

        for (int i = 0; i < flipCount; ++i) {
            float dealDelay = cursorSec;
            float flipAmp = baseAmp * (0.74f + 0.18f * (static_cast<float>(i + 1) / static_cast<float>(flipCount)));
            allocateVoice(makeDealVoice(dealDelay, flipAmp * 0.92f, 0.85f));

            float placeOffset = 0.036f + (nextNoise() * 0.008f);
            float placeDelay = dealDelay + std::max(0.026f, placeOffset);
            allocateVoice(makePlaceVoice(placeDelay, flipAmp * 0.78f));

            // Advance cursor past this Flip (Deal + Place)
            float stepGap = 0.056f + (nextNoise() * 0.015f);
            cursorSec = placeDelay + std::max(0.038f, stepGap);

            // Arbitrarily intersperse Sweep sounds between Place sounds and the next Flip
            bool shouldSweep = (i < flipCount - 1) &&
                               ((nextNoise() > 0.05f) || (i == flipCount - 2 && sweepsInterspersed == 0));
            if (shouldSweep && sweepsInterspersed < 3) {
                ++sweepsInterspersed;
                float sweepStart = placeDelay + 0.014f + std::abs(nextNoise()) * 0.014f;
                float sweepScale = 0.58f + std::abs(nextNoise()) * 0.25f;
                allocateVoice(makeSweepVoice(sweepStart, baseAmp * 0.72f, sweepScale));
                cursorSec += 0.048f + std::abs(nextNoise()) * 0.028f;
            }
        }
        return;
    }

    if (soundId == FoleySound::Deal) {
        allocateVoice(makeDealVoice(0.0f, intensity));
        return;
    }

    if (soundId == FoleySound::Place) {
        allocateVoice(makePlaceVoice(0.0f, intensity));
        return;
    }

    if (soundId == FoleySound::Sweep) {
        allocateVoice(makeSweepVoice(0.0f, intensity));
        return;
    }

    // Ethereal ZAngband Nether-Sight shimmer (FoleySound::SpectreWhisper)
    float jitter = 1.0f + (nextNoise() * 0.035f);
    Voice v{};
    v.active = true;
    v.soundId = FoleySound::SpectreWhisper;
    v.intensity = intensity;
    v.jitter = jitter;
    v.durationSec = 0.350f;
    v.fBody = 396.0f * jitter;
    v.slideFilter1.c = makeBandpass(880.0f * jitter, 2.5f, sr);
    allocateVoice(v);
}

void AudioFoleyEngine::mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate) noexcept {
    const float sr = (sampleRate > 8000.0f) ? static_cast<float>(sampleRate) : 48000.0f;
    const float dt = 1.0f / sr;

    // Process queued triggers from the main thread
    uint32_t r = readIdx_.load(std::memory_order_relaxed);
    uint32_t w = writeIdx_.load(std::memory_order_acquire);
    while (r != w) {
        const auto& cmd = cmdQueue_[r % kQueueCapacity];
        handleTrigger(cmd.soundId, cmd.intensity, sr);
        ++r;
    }
    readIdx_.store(r, std::memory_order_release);

    if (muted_.load(std::memory_order_relaxed)) {
        std::fill(outInterleavedStereo, outInterleavedStereo + frameCount * 2, 0.0f);
        return;
    }
    const float masterVol = masterVolume_.load(std::memory_order_relaxed);

    for (uint32_t f = 0; f < frameCount; ++f) {
        float mix = 0.0f;

        for (auto& v : voices_) {
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

            const float normAge = v.ageSec / v.durationSec;
            float sample = 0.0f;
            const float noise = nextNoise();

            switch (v.soundId) {
                case FoleySound::Deal: {
                    // Pure high-frequency soft sweeping sound ("hit me" card slide, zero bass thud)
                    float env = std::sin(normAge * 3.14159265f) * std::exp(-1.55f * normAge) * 2.2f;
                    float midSweep = v.slideFilter1.process(noise);
                    float hiSweep = v.slideFilter2.process(noise);
                    float airSheen = v.snapFilter.process(noise);

                    // Progressive upward spectral sweep as the card glides free
                    float sweepMix = (1.0f - normAge) * midSweep * 0.55f
                                   + normAge * hiSweep * 0.65f
                                   + airSheen * (0.25f + 0.35f * normAge);

                    sample = sweepMix * env * v.intensity * 0.42f;
                    break;
                }

                case FoleySound::Sweep: {
                    // Lengthy sweeping sound of a card being slowly dragged across felt table
                    float attack = std::clamp(normAge * 5.0f, 0.0f, 1.0f);
                    float release = std::sin(normAge * 3.14159265f);
                    float grainMod = 0.88f + 0.12f * std::sin(normAge * kTwoPi * (2.4f * v.jitter));
                    float env = attack * release * grainMod * 1.45f;

                    float warmFelt = v.bodyFilter.process(noise);
                    float midBrush = v.slideFilter1.process(noise);
                    float upperBrush = v.slideFilter2.process(noise);
                    float airBrush = v.snapFilter.process(noise);

                    // Smooth spectral trajectory across the slow table drag
                    float wWarm = std::max(0.0f, 1.0f - normAge * 1.3f) * 0.35f;
                    float wMid = std::sin(normAge * 3.14159265f) * 0.55f;
                    float wUpper = normAge * (1.0f - 0.3f * normAge) * 0.50f;
                    float wAir = std::sin(normAge * 3.14159265f) * 0.22f;

                    float dragSignal = warmFelt * wWarm + midBrush * wMid + upperBrush * wUpper + airBrush * wAir;
                    sample = dragSignal * env * v.intensity * 0.46f;
                    break;
                }

                case FoleySound::Place: {
                    // Bassier soft thump for manual card placement on table
                    float thudEnv = (normAge * 12.0f) * std::exp(-10.5f * normAge);
                    float thudFreq = (v.fBody * std::exp(-7.5f * normAge) + 52.0f * v.jitter);
                    v.phaseThud += thudFreq * dt;
                    if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;
                    float subThump = std::sin(v.phaseThud * kTwoPi) * thudEnv * 0.62f;

                    // Soft cushioned felt resonance + gentle card settle
                    float feltEnv = (normAge * 14.0f) * std::exp(-16.0f * normAge);
                    float feltBody = v.bodyFilter.process(noise) * feltEnv * 0.42f;
                    float cardSettle = v.slideFilter1.process(noise) * feltEnv * 0.26f;

                    float edgeEnv = std::exp(-48.0f * normAge) * (normAge * 18.0f);
                    float softEdge = v.snapFilter.process(noise) * edgeEnv * 0.16f;

                    sample = (subThump + feltBody + cardSettle + softEdge) * v.intensity * 0.48f;
                    break;
                }

                case FoleySound::SpectreWhisper:
                default: {
                    v.phaseBody += v.fBody * dt;
                    if (v.phaseBody >= 1.0f) v.phaseBody -= 1.0f;

                    v.phaseThud += (v.fBody * 1.503f) * dt; // Detuned Nether interval
                    if (v.phaseThud >= 1.0f) v.phaseThud -= 1.0f;

                    float whisper = v.slideFilter1.process(noise) * 0.25f;
                    float env = std::sin(normAge * 3.14159265f) * std::exp(-2.2f * normAge);
                    float tone = 0.55f * std::sin(v.phaseBody * kTwoPi) + 0.45f * std::sin(v.phaseThud * kTwoPi);
                    sample = (tone * 0.75f + whisper * 0.25f) * env * v.intensity * 0.30f;
                    break;
                }
            }

            mix += sample;
        }

        // Soft saturation limiter to preserve dynamics without harsh digital clipping
        mix = std::clamp(mix * masterVol, -0.92f, 0.92f);
        outInterleavedStereo[f * 2 + 0] = mix;
        outInterleavedStereo[f * 2 + 1] = mix;
    }
}

} // namespace solengine
