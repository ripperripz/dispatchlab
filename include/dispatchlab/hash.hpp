#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace dispatchlab {

// FNV-1a 64-bit. Chosen because it is fully specified, platform independent and
// trivially re-implementable, unlike std::hash. Integers are fed as fixed-width
// little-endian bytes so the digest does not depend on host endianness.
class Fnv1a64 {
public:
    static constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    static constexpr std::uint64_t kPrime = 0x100000001b3ULL;

    void update(const void* data, std::size_t len) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < len; ++i) {
            state_ ^= p[i];
            state_ *= kPrime;
        }
    }
    void add_u8(std::uint8_t v) { update(&v, 1); }
    void add_u32(std::uint32_t v) {
        unsigned char b[4];
        for (int i = 0; i < 4; ++i) b[i] = static_cast<unsigned char>(v >> (8 * i));
        update(b, 4);
    }
    void add_u64(std::uint64_t v) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>(v >> (8 * i));
        update(b, 8);
    }
    void add_i64(std::int64_t v) { add_u64(static_cast<std::uint64_t>(v)); }
    void add_str(std::string_view s) {
        add_u64(s.size());
        update(s.data(), s.size());
    }
    std::uint64_t digest() const { return state_; }

private:
    std::uint64_t state_ = kOffsetBasis;
};

inline std::uint64_t fnv1a64(std::string_view s) {
    Fnv1a64 h;
    h.update(s.data(), s.size());
    return h.digest();
}

inline std::string hex64(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

}  // namespace dispatchlab
