#ifndef SOLENGINE_AUDIO_FOLEY_HPP
#define SOLENGINE_AUDIO_FOLEY_HPP

#include <atomic>
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <algorithm>

namespace solengine {

namespace FoleySound {
    // Defined core sounds and sound sequences:
    constexpr uint16_t Snap           = 0;  // Dirac Edge Snap (needle transient, card tap/touch)
    constexpr uint16_t Deal           = 1;  // Quick Deal (Snap + 26ms trailing baize slide)
    constexpr uint16_t Place          = 2;  // Card drop / table thud (surface landing with air slap)
    constexpr uint16_t Shuffle        = 3;  // Riffle cascade: 26 micro-interleaved snaps + baize slide
    constexpr uint16_t TexasPitch     = 4;  // Quick deal flurry: 4 dealer tosses with trailing friction
    constexpr uint16_t Flip           = 5;  // Turnover flip: edge peel friction into crisp snap & drop
    constexpr uint16_t Slide          = 6;  // Velvet baize slide across felt
    constexpr uint16_t SpectreWhisper = 7;  // Ethereal ZAngband Spectre Nether-Sight shimmer
    constexpr uint16_t MaxCoreSounds  = 8;

    // Semantic aliases for compatibility across call sites:
    constexpr uint16_t Tap             = Snap;
    constexpr uint16_t CardSnap        = Snap;
    constexpr uint16_t CardTap         = Snap;
    constexpr uint16_t Drop            = Place;
    constexpr uint16_t CardPlace       = Place;
    constexpr uint16_t DeckShuffle     = Shuffle;
    constexpr uint16_t Riffle          = Shuffle;
    constexpr uint16_t Flurry          = TexasPitch;
    constexpr uint16_t CardSlide       = Slide;
    constexpr uint16_t Sweep           = Slide;
    constexpr uint16_t WoodClick       = Snap;  // UI clicks become tactile Dirac needle snaps
    constexpr uint16_t FoundationChime = Snap;
    constexpr uint16_t CardDealSingle  = Deal;
    constexpr uint16_t CardDealAlt     = Deal;
    constexpr uint16_t DealSeries      = Deal;
} // namespace FoleySound

enum class ShuffleStyle : uint8_t {
    Soft = 0, // Gentle cascading brush / gather
    Loud = 1  // Crisp energetic riffle snaps
};

struct PhysicalAcousticParams {
    float velvetDensity{4200.0f};       // Micro-impulses per second (3500 - 6000)
    float edgeSnapCrispness{0.85f};     // Sub-ms Dirac snap mix
    float airBoostDb{5.0f};             // 8kHz - 14kHz shelf boost (dB)
    float combDelayTime{0.00018f};      // ~5.5kHz notch dispersion (0.18 ms)
    float feltDampingCutoff{7500.0f};   // Open frequency ceiling (Hz)
    float cardRigidityFreq{3200.0f};    // Card flexion resonance (Hz)
    float flangerDepth{0.0f};           // Flanger LFO modulation depth in sec (0.0 = disabled)
    float flangerRate{1.5f};            // Flanger LFO sweep rate in Hz
    float flangerFeedback{0.30f};       // Flanger resonance feedback (0.0 - 0.7)
};

namespace FoleyGenerator {
    constexpr uint8_t Noise    = 0;
    constexpr uint8_t Sine     = 1;
    constexpr uint8_t Triangle = 2;
    constexpr uint8_t Square   = 3;
    constexpr uint8_t Saw      = 4;
}

namespace FoleyFilter {
    constexpr uint8_t None     = 0;
    constexpr uint8_t Lowpass  = 1;
    constexpr uint8_t Highpass = 2;
    constexpr uint8_t Bandpass = 3;
}

namespace FoleyEnvelope {
    constexpr uint8_t Exponential   = 0;
    constexpr uint8_t HalfSine      = 1;
    constexpr uint8_t AttackRelease = 2;
    constexpr uint8_t PunchSub      = 3;
    constexpr uint8_t LinearDecay   = 4;
}

struct FoleyLayerDef {
    uint8_t generator{FoleyGenerator::Noise};
    uint8_t filter{FoleyFilter::None};
    uint8_t envelope{FoleyEnvelope::Exponential};

    float freqStartBase{440.0f};
    float freqStartDev{0.0f};
    float freqEndBase{440.0f};
    float freqEndDev{0.0f};

    float filterCutoffBase{1000.0f};
    float filterCutoffDev{0.0f};
    float filterQBase{1.0f};
    float filterQDev{0.0f};

    float attackSec{0.005f};
    float decayRate{2.0f};
    float dutyCycle{0.5f};

    float weightBase{1.0f};
    float weightDev{0.0f};
};

static constexpr size_t kMaxLayersPerSound = 4;

struct FoleySoundDef {
    std::string name;
    uint16_t soundId{0};
    bool isCustom{false};

    float durationBase{0.060f};
    float durationDev{0.10f};

    float gainBase{1.0f};
    float gainDev{0.05f};

    float panBase{0.0f};
    float panDev{0.05f};

    size_t numLayers{0};
    std::array<FoleyLayerDef, kMaxLayersPerSound> layers{};
};

struct FoleySequenceDef {
    std::string name;
    uint16_t sequenceId{0};
    bool isCustom{false};

    int minFlips{6};
    int maxFlips{8};

    float baseGapSec{0.056f};
    float gapDev{0.15f};

    float dealToPlaceDelaySec{0.036f};
    float dealToPlaceDelayDev{0.10f};

    float doubleDropChance{0.25f};
    float doubleDropMinSec{0.008f};
    float doubleDropMaxSec{0.014f};

    float stereoAlternation{0.12f};

    uint16_t introSoundId{FoleySound::Sweep};
    float introGain{0.90f};

    uint16_t interspersedSoundId{FoleySound::Sweep};
    float interspersedChance{0.40f};
    int interspersedMaxCount{3};
};

struct FoleySoundBank {
    PhysicalAcousticParams physicalParams{};
    std::array<FoleySoundDef, FoleySound::MaxCoreSounds> coreSounds{};
    std::vector<FoleySoundDef> customSounds{};
    std::array<FoleySequenceDef, FoleySound::MaxCoreSounds> coreSequences{};
    std::vector<FoleySequenceDef> customSequences{};
    float masterVariance{1.0f};
};

/**
 * Real-Time Multi-Modal Procedural Card & Solitaire Foley Synthesizer.
 * Built on miniaudio with zero sample dependencies: physically synthesizes
 * parchment micro-friction via sparse Velvet Noise micro-impulses, sub-ms Dirac
 * edge transients with comb aperture dispersion, cardboard deflection, and table landings.
 * Fully configurable via declarative Eatscript (.eats) files with bounded organic variance.
 */
class AudioFoleyEngine {
public:
    AudioFoleyEngine();
    ~AudioFoleyEngine();

    bool init(bool enableHardwareDevice = true);
    void shutdown();

    void triggerSound(uint16_t soundId, float intensity = 1.0f, float pan = 0.0f);
    void mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate = 48000) noexcept;

    // Eatscript (.eats) sound pack configuration API
    bool loadFromEatsFile(const std::string& filePath);
    bool loadFromEatsScript(const std::string& script);
    void resetToDefaults();

    void setPhysicalParams(const PhysicalAcousticParams& params);
    [[nodiscard]] PhysicalAcousticParams getPhysicalParams() const noexcept;
    void applyPreset(const std::string& presetName);

    void setShufflePreference(ShuffleStyle style) noexcept { shuffleStyle_.store(style, std::memory_order_relaxed); }
    ShuffleStyle getShufflePreference() const noexcept { return shuffleStyle_.load(std::memory_order_relaxed); }

    void setMasterVolume(float vol) noexcept { masterVolume_.store(std::clamp(vol, 0.0f, 1.0f), std::memory_order_relaxed); }
    [[nodiscard]] float getMasterVolume() const noexcept { return masterVolume_.load(std::memory_order_relaxed); }

    void setMasterVariance(float v) noexcept { masterVariance_.store(std::clamp(v, 0.0f, 3.0f), std::memory_order_relaxed); }
    [[nodiscard]] float getMasterVariance() const noexcept { return masterVariance_.load(std::memory_order_relaxed); }

    void setMuted(bool m) noexcept { muted_.store(m, std::memory_order_relaxed); }
    [[nodiscard]] bool isMuted() const noexcept { return muted_.load(std::memory_order_relaxed); }

    struct BiquadCoeffs {
        float b0{1.0f}, b1{0.0f}, b2{0.0f};
        float a1{0.0f}, a2{0.0f};
    };

    struct BiquadFilter {
        BiquadCoeffs c;
        float x1{0.0f}, x2{0.0f};
        float y1{0.0f}, y2{0.0f};

        inline float process(float in) noexcept {
            float out = c.b0 * in + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }

        void reset() noexcept {
            x1 = x2 = y1 = y2 = 0.0f;
        }
    };

    enum class VoiceType : uint8_t {
        Snap,
        Slide,
        Drop,
        SpectreWhisper,
        CustomDSP
    };

private:
    struct VoiceLayer {
        bool active{false};
        uint8_t generator{FoleyGenerator::Noise};
        uint8_t filterType{FoleyFilter::None};
        uint8_t envelope{FoleyEnvelope::Exponential};

        float phase{0.0f};
        float fStart{440.0f};
        float fEnd{440.0f};
        float dutyCycle{0.5f};

        float attackSec{0.005f};
        float decayRate{2.0f};
        float weight{1.0f};

        BiquadFilter filter;
    };

    struct Voice {
        bool active{false};
        VoiceType type{VoiceType::Snap};
        uint16_t soundId{0};
        bool isCustomDSP{false};

        float delaySec{0.0f};
        float ageSec{0.0f};
        float durationSec{0.045f};
        float intensity{1.0f};
        float pitchMod{1.0f};
        float pan{0.0f}; // -1.0 left to +1.0 right
        float gainL{0.5f};
        float gainR{0.5f};

        // Sweep state & control-rate sub-counter for DSP efficiency
        uint8_t subCounter{0};
        float triFreq{1760.0f};
        float triFreqStep{1.0f};
        float diracFreq{9400.0f};
        float diracFreqStep{1.0f};
        float dropFreq{220.0f};
        float dropFreqStep{1.0f};
        float slideFreq{3400.0f};
        float slideFreqStep{1.0f};

        // Custom DSP layers
        uint8_t numLayers{0};
        std::array<VoiceLayer, kMaxLayersPerSound> layers{};

        // Physical Velvet/Dirac modeling state
        float phaseTri{0.0f};
        float phaseDirac{0.0f};
        float phaseSine{0.0f};
        float phaseBody{0.0f};
        float phaseThud{0.0f};

        float velvetIdx{0.0f};
        float velvetPlaybackRate{1.0f};
        float grainIdx{0.0f};
        float grainPlaybackRate{1.35f};
        float continuousIdx{0.0f};

        // Flanger / Comb dispersion delay line (circular buffer)
        static constexpr size_t kCombBufferSize = 256;
        std::array<float, kCombBufferSize> combBuffer{};
        size_t combWriteIdx{0};
        float combDelaySamples{8.64f};
        float lfoPhase{0.0f};

        // Multi-modal biquad filters
        BiquadFilter biquad1; // Snap: felt cutoff / Slide: primary BP / Drop: slap BP / Spectre: bandpass
        BiquadFilter biquad2; // Snap: snap BP / Slide: felt cutoff
        BiquadFilter biquad3; // Snap: air shelf / Slide: grain HP
        BiquadFilter biquad4; // Snap: dirac HP / Slide: grain shelf
        BiquadFilter biquad5; // Extra slot (e.g. Snap: 2nd felt lowpass)

        void reset() noexcept {
            active = false;
            type = VoiceType::Snap;
            soundId = 0;
            isCustomDSP = false;
            delaySec = 0.0f;
            ageSec = 0.0f;
            durationSec = 0.045f;
            intensity = 1.0f;
            pitchMod = 1.0f;
            pan = 0.0f;
            gainL = 0.5f;
            gainR = 0.5f;
            subCounter = 0;
            triFreq = 1760.0f;
            triFreqStep = 1.0f;
            diracFreq = 9400.0f;
            diracFreqStep = 1.0f;
            dropFreq = 220.0f;
            dropFreqStep = 1.0f;
            slideFreq = 3400.0f;
            slideFreqStep = 1.0f;
            phaseTri = phaseDirac = phaseSine = phaseBody = phaseThud = 0.0f;
            velvetIdx = grainIdx = continuousIdx = 0.0f;
            velvetPlaybackRate = 1.0f;
            grainPlaybackRate = 1.35f;
            combBuffer.fill(0.0f);
            combWriteIdx = 0;
            combDelaySamples = 8.64f;
            lfoPhase = 0.0f;
            numLayers = 0;
            biquad1.reset();
            biquad2.reset();
            biquad3.reset();
            biquad4.reset();
            biquad5.reset();
            for (auto& l : layers) {
                l.active = false;
                l.phase = 0.0f;
                l.filter.reset();
            }
        }
    };

    struct TriggerCmd {
        uint16_t soundId{0};
        float intensity{1.0f};
        float pan{0.0f};
    };

    void allocateVoice(const Voice& v) noexcept;
    void handleTrigger(uint16_t soundId, float intensity, float pan, float sampleRate) noexcept;
    uint32_t nextRng() noexcept {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return rngState_;
    }
    float nextNoise() noexcept {
        return static_cast<float>(static_cast<int32_t>(nextRng())) * (1.0f / 2147483648.0f);
    }

    void regenerateVelvetBuffer(float density, float sampleRate = 48000.0f);
    void generateContinuousNoise(float sampleRate = 48000.0f);

    void populateDefaultBank(FoleySoundBank& bank);

    static constexpr int kMaxVoices = 64;
    std::array<Voice, kMaxVoices> voices_{};

    // Lock-free single-producer single-consumer ring buffer for triggers
    static constexpr size_t kQueueCapacity = 64;
    std::array<TriggerCmd, kQueueCapacity> cmdQueue_{};
    std::atomic<uint32_t> writeIdx_{0};
    std::atomic<uint32_t> readIdx_{0};

    // Double-buffered SoundBank for wait-free hot reloading
    std::array<FoleySoundBank, 2> soundBanks_{};
    std::atomic<uint32_t> activeBankIndex_{0};

    static constexpr size_t kNoiseBufferSize = 96000; // 2.0s at 48kHz
    std::vector<float> velvetNoiseBuffer_;
    std::vector<float> continuousNoiseBuffer_;
    size_t continuousNoiseReadIdx_{0};

    std::atomic<ShuffleStyle> shuffleStyle_{ShuffleStyle::Soft};
    std::atomic<float> masterVolume_{0.85f};
    std::atomic<float> masterVariance_{1.0f};
    std::atomic<bool> muted_{false};
    uint32_t rngState_{0x9E3779B9u};
    uint32_t dealAlternator_{0};
    bool initialized_{false};
    void* maDevicePtr_{nullptr};
};

} // namespace solengine

#endif // SOLENGINE_AUDIO_FOLEY_HPP
