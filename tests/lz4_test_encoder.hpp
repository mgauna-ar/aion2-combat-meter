#pragma once

// A small greedy LZ4 block compressor, so the tests can build the game's bundles
// without a vendored LZ4. Output follows the block format's end rules (the last
// five bytes are literals, no match starts in the last twelve), so any LZ4 decoder
// accepts it, not only ours.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace aion2::test::lz4 {

namespace detail {

inline void put_length(std::vector<uint8_t>& out, size_t extra) {
    while (extra >= 255) {
        out.push_back(255);
        extra -= 255;
    }
    out.push_back(static_cast<uint8_t>(extra));
}

inline void put_sequence(std::vector<uint8_t>& out, std::span<const uint8_t> literals, size_t offset,
                         size_t match_len) {
    const size_t lit = literals.size();
    const size_t ml = match_len == 0 ? 0 : match_len - 4;
    const auto token = static_cast<uint8_t>(((lit >= 15 ? 15 : lit) << 4) | (match_len == 0 ? 0 : (ml >= 15 ? 15 : ml)));
    out.push_back(token);
    if (lit >= 15) put_length(out, lit - 15);
    out.insert(out.end(), literals.begin(), literals.end());
    if (match_len == 0) return;
    out.push_back(static_cast<uint8_t>(offset));
    out.push_back(static_cast<uint8_t>(offset >> 8));
    if (ml >= 15) put_length(out, ml - 15);
}

inline uint32_t read32(std::span<const uint8_t> b, size_t at) {
    return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8) |
           (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

} // namespace detail

inline std::vector<uint8_t> compress(std::span<const uint8_t> src) {
    constexpr size_t kMfLimit = 12;
    constexpr size_t kLastLiterals = 5;
    constexpr size_t kHashBits = 12;
    std::vector<uint8_t> out;
    std::vector<size_t> table(size_t{1} << kHashBits, SIZE_MAX);
    size_t anchor = 0;
    size_t i = 0;
    while (src.size() >= kMfLimit && i + kMfLimit <= src.size()) {
        const uint32_t seq = detail::read32(src, i);
        const size_t h = (seq * 2654435761u) >> (32 - kHashBits);
        const size_t cand = table[h];
        table[h] = i;
        if (cand != SIZE_MAX && i - cand <= 65535 && detail::read32(src, cand) == seq) {
            size_t len = 4;
            while (i + len < src.size() - kLastLiterals && src[cand + len] == src[i + len]) ++len;
            detail::put_sequence(out, src.subspan(anchor, i - anchor), i - cand, len);
            i += len;
            anchor = i;
        } else {
            ++i;
        }
    }
    detail::put_sequence(out, src.subspan(anchor), 0, 0);
    return out;
}

} // namespace aion2::test::lz4
