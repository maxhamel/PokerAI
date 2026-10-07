#pragma once

#include <cstdint>
#include <limits>

// Small, fast random number generator (xoshiro128++). Much cheaper than
// mt19937 to create and to call, with good statistical quality for games and
// simulations (not for cryptography). Works with the standard distributions.
class FastRng {
    private:
        uint32_t s[4];

        static uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

    public:
        using result_type = uint32_t;
        static constexpr result_type min() { return 0; }
        static constexpr result_type max() { return std::numeric_limits<uint32_t>::max(); }

        explicit FastRng(uint64_t seed) {
            // spread the seed over the state with splitmix64
            for (int i = 0; i < 4; i += 2) {
                uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
                z ^= z >> 31;
                s[i] = (uint32_t)z;
                s[i + 1] = (uint32_t)(z >> 32);
            }
        }

        result_type operator()() {
            uint32_t result = rotl(s[0] + s[3], 7) + s[0];
            uint32_t t = s[1] << 9;
            s[2] ^= s[0];
            s[3] ^= s[1];
            s[1] ^= s[2];
            s[0] ^= s[3];
            s[2] ^= t;
            s[3] = rotl(s[3], 11);
            return result;
        }

        // Random integer in [0, n), unbiased (Lemire's method).
        uint32_t below(uint32_t n) {
            uint64_t m = (uint64_t)(*this)() * n;
            if ((uint32_t)m < n) {
                uint32_t threshold = -n % n;
                while ((uint32_t)m < threshold) m = (uint64_t)(*this)() * n;
            }
            return m >> 32;
        }
};
