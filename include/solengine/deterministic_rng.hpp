#ifndef SOLENGINE_DETERMINISTIC_RNG_HPP
#define SOLENGINE_DETERMINISTIC_RNG_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>

namespace solengine {

/// Default fallback seed across SolEngine.
inline constexpr uint32_t kDefaultSeed = 2026u;

/// Single-step Mulberry32 deterministic PRNG.
/// Uses strictly unsigned 32-bit wraparound arithmetic for bit-identical results
/// across MSVC (Windows), libstdc++ (Linux), and libc++ (macOS, iOS, Android, WASM).
[[nodiscard]] inline uint32_t mulberry32Next(uint32_t& state) noexcept {
    uint32_t z = (state += 0x6D2B79F5u);
    z = (z ^ (z >> 15)) * (z | 1u);
    z ^= z + (z ^ (z >> 7)) * (z | 61u);
    return z ^ (z >> 14);
}

/// Cross-platform deterministic PRNG used across SolEngine for card deals,
/// scripted gameplay rules, EatScript math.random(), and procedural deck art.
/// Each domain (deal seed, lowPolySeed, table texture seed) keeps its own
/// independent seed value and DeterministicRng instance.
struct DeterministicRng {
    uint32_t state{kDefaultSeed};

    explicit DeterministicRng(uint32_t seed = kDefaultSeed) noexcept : state(seed) {}

    [[nodiscard]] uint32_t nextU32() noexcept {
        return mulberry32Next(state);
    }

    /// 24-bit uniform float in [0.0f, 1.0f), independent of STL uniform_real_distribution.
    [[nodiscard]] float nextFloat01() noexcept {
        return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
    }

    /// 32-bit uniform double in [0.0, 1.0), independent of STL uniform_real_distribution.
    [[nodiscard]] double nextDouble01() noexcept {
        return static_cast<double>(nextU32()) / 4294967296.0;
    }

    [[nodiscard]] float rangeFloat(float lo, float hi) noexcept {
        return lo + (hi - lo) * nextFloat01();
    }

    /// Inclusive integer range [lo, hi], independent of STL uniform_int_distribution.
    [[nodiscard]] int rangeInt(int lo, int hi) noexcept {
        if (hi <= lo) return lo;
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(nextU32() % span);
    }

    /// Derive an independent sub-stream for a procedural sub-domain (e.g. card back, joker).
    [[nodiscard]] DeterministicRng derive(uint32_t domainSalt) const noexcept {
        return DeterministicRng{state ^ domainSalt};
    }

    /// Deterministic Fisher-Yates shuffle operating on an external uint32_t state.
    template <typename Container>
    static void shuffle(Container& items, uint32_t& externalState) noexcept {
        if (items.size() <= 1) return;
        for (size_t i = items.size() - 1; i > 0; --i) {
            const size_t j = static_cast<size_t>(mulberry32Next(externalState) % static_cast<uint32_t>(i + 1));
            std::swap(items[i], items[j]);
        }
    }

    template <typename Container>
    void shuffle(Container& items) noexcept {
        shuffle(items, state);
    }
};

/// Generate a fresh non-zero 31-bit random seed for starting new random deals.
[[nodiscard]] inline uint32_t generateRandomSeed() noexcept {
    uint32_t entropy = 0u;
    try {
        std::random_device rd;
        entropy = static_cast<uint32_t>(rd());
    } catch (...) {
        entropy = 0x9E3779B9u;
    }
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    uint32_t seed = (entropy ^ static_cast<uint32_t>(now)) & 0x7FFFFFFFu;
    return (seed == 0u) ? kDefaultSeed : seed;
}

} // namespace solengine

#endif // SOLENGINE_DETERMINISTIC_RNG_HPP
