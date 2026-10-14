#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace mafia {

// splitmix64: из сида партии получаем независимые сиды для ведущего и каждого
// игрока. Каждая нить пользуется только своим генератором, поэтому партия
// с одним сидом воспроизводится независимо от планирования нитей.
constexpr std::uint64_t mixSeed(std::uint64_t seed, std::uint64_t salt) noexcept {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (salt + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

class Rng {
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}

    double uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(engine_); }

    int uniformInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(engine_); }

    bool chance(double probability) { return uniform(0.0, 1.0) < probability; }

    template <class T>
    const T& pick(const std::vector<T>& items) {
        return items[static_cast<std::size_t>(uniformInt(0, static_cast<int>(items.size()) - 1))];
    }

    template <class T>
    void shuffle(std::vector<T>& items) {
        std::shuffle(items.begin(), items.end(), engine_);
    }

private:
    std::mt19937_64 engine_;
};

}  // namespace mafia
