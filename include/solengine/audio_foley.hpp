#ifndef SOLENGINE_AUDIO_FOLEY_HPP
#define SOLENGINE_AUDIO_FOLEY_HPP

#include <atomic>
#include <cstdint>
#include <array>
#include <algorithm>

namespace solengine {

namespace FoleySound {
    // Defined core sounds and sound sequences:
    constexpr uint16_t Deal    = 0;  // Pure high-frequency soft sweeping sound (card pickup, card move, new game deal)
    constexpr uint16_t Place   = 1;  // Bassier soft thump when placing a card manually on the table
    constexpr uint16_t Shuffle = 2;  // Sweep followed by multiple Flips (Deal + Place) with interspersed Sweeps
    constexpr uint16_t Flip    = 3;  // Deal + Place in quick succession (menu actions & flipping card from stack)
    constexpr uint16_t Sweep   = 4;  // Lengthy sweeping sound of card slowly dragged on table (Settings OK & Shuffle)

    // Semantic aliases for compatibility across call sites:
    constexpr uint16_t CardSlide       = Deal;
    constexpr uint16_t CardSnap        = Place;
    constexpr uint16_t DeckShuffle     = Shuffle;
    constexpr uint16_t WoodClick       = Flip;
    constexpr uint16_t FoundationChime = Sweep;
    constexpr uint16_t SpectreWhisper  = 5;  // Ethereal ZAngband Spectre Nether-Sight shimmer

    constexpr uint16_t CardDealSingle  = Deal;
    constexpr uint16_t CardDealAlt     = Deal;
    constexpr uint16_t CardPlace       = Place;
    constexpr uint16_t ShuffleSoft     = Shuffle;
    constexpr uint16_t ShuffleLoud     = Shuffle;
    constexpr uint16_t DealSeries      = Deal;
} // namespace FoleySound

enum class ShuffleStyle : uint8_t {
    Soft = 0, // Gentle cascading brush / gather (matches shuffle-soft.wav)
    Loud = 1  // Crisp energetic riffle snaps (matches shuffle-loud.wav)
};

/**
 * Real-Time Multi-Modal Procedural Card & Solitaire Foley Synthesizer.
 * Built on miniaudio with zero sample dependencies: mathematically synthesizes
 * parchment friction, edge snaps, cardboard flex resonances, and table landings.
 */
class AudioFoleyEngine {
public:
    AudioFoleyEngine();
    ~AudioFoleyEngine();

    bool init(bool enableHardwareDevice = true);
    void shutdown();

    void triggerSound(uint16_t soundId, float intensity = 1.0f);
    void mixSamples(float* outInterleavedStereo, uint32_t frameCount, uint32_t sampleRate = 48000) noexcept;

    void setShufflePreference(ShuffleStyle style) noexcept { shuffleStyle_.store(style, std::memory_order_relaxed); }
    ShuffleStyle getShufflePreference() const noexcept { return shuffleStyle_.load(std::memory_order_relaxed); }

    void setMasterVolume(float vol) noexcept { masterVolume_.store(std::clamp(vol, 0.0f, 1.0f), std::memory_order_relaxed); }
    [[nodiscard]] float getMasterVolume() const noexcept { return masterVolume_.load(std::memory_order_relaxed); }
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

private:

    struct Voice {
        bool active{false};
        uint16_t soundId{0};
        float delaySec{0.0f};
        float ageSec{0.0f};
        float durationSec{0.045f};
        float intensity{1.0f};
        float jitter{1.0f};

        // Synthesis parameters
        float fBody{320.0f};
        float phaseBody{0.0f};
        float phaseThud{0.0f};
        float phaseHarmonic{0.0f};
        float phaseLfo{0.0f};
        float thudDelaySec{0.0f};

        // Multi-modal filters
        BiquadFilter snapFilter;
        BiquadFilter slideFilter1;
        BiquadFilter slideFilter2;
        BiquadFilter bodyFilter;

        void reset() noexcept {
            active = false;
            delaySec = 0.0f;
            ageSec = 0.0f;
            phaseBody = phaseThud = phaseHarmonic = phaseLfo = 0.0f;
            snapFilter.reset();
            slideFilter1.reset();
            slideFilter2.reset();
            bodyFilter.reset();
        }
    };

    struct TriggerCmd {
        uint16_t soundId{0};
        float intensity{1.0f};
    };

    void allocateVoice(const Voice& v) noexcept;
    void handleTrigger(uint16_t soundId, float intensity, float sampleRate) noexcept;
    float nextNoise() noexcept;

    static constexpr int kMaxVoices = 48;
    std::array<Voice, kMaxVoices> voices_{};

    // Lock-free single-producer single-consumer ring buffer for triggers
    static constexpr size_t kQueueCapacity = 64;
    std::array<TriggerCmd, kQueueCapacity> cmdQueue_{};
    std::atomic<uint32_t> writeIdx_{0};
    std::atomic<uint32_t> readIdx_{0};

    std::atomic<ShuffleStyle> shuffleStyle_{ShuffleStyle::Soft};
    std::atomic<float> masterVolume_{0.85f};
    std::atomic<bool> muted_{false};
    uint32_t rngState_{0x9E3779B9u};
    uint32_t dealAlternator_{0};
    bool initialized_{false};
    void* maDevicePtr_{nullptr};
};

} // namespace solengine

#endif // SOLENGINE_AUDIO_FOLEY_HPP
