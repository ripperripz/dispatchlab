#pragma once

#include <cstdint>

namespace dispatchlab {

// Deterministic PRNG with fully specified output. std::uniform_*_distribution
// is implementation-defined, so all sampling goes through this class instead.
class Rng {
public:
    explicit Rng(std::uint64_t seed) {
        std::uint64_t x = seed;
        for (auto& w : s_) w = splitmix64(x);
    }

    static std::uint64_t splitmix64(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // xoshiro256**
    std::uint64_t next() {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform integer in [0, bound) without modulo bias (rejection sampling).
    std::uint64_t below(std::uint64_t bound) {
        if (bound == 0) return 0;
        const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % bound);
        std::uint64_t x;
        do {
            x = next();
        } while (x >= limit);
        return x % bound;
    }

    // Uniform integer in [lo, hi] (inclusive).
    std::int64_t range(std::int64_t lo, std::int64_t hi) {
        return lo + static_cast<std::int64_t>(below(static_cast<std::uint64_t>(hi - lo) + 1));
    }

    // Uniform double in [0, 1) using the top 53 bits.
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    std::uint64_t s_[4];
};

}  // namespace dispatchlab
