#include "protocol/lz4_block.hpp"

namespace aion2::proto {

namespace {
constexpr size_t kMinMatch = 4;
constexpr uint8_t kRunMask = 15;
} // namespace

std::optional<std::vector<uint8_t>> lz4_decompress_block(std::span<const uint8_t> src, size_t raw_size) {
    if (raw_size > kMaxLz4RawSize) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(raw_size);
    size_t in = 0;

    // A length nibble of 15 continues in the following bytes, each added, until one
    // is not 255.
    const auto extend = [&src, &in](size_t& length) -> bool {
        for (;;) {
            if (in >= src.size()) return false;
            const uint8_t b = src[in++];
            length += b;
            if (b != 255) return true;
        }
    };

    while (in < src.size()) {
        const uint8_t token = src[in++];

        size_t literals = token >> 4;
        if (literals == kRunMask && !extend(literals)) return std::nullopt;
        if (literals > src.size() - in || literals > raw_size - out.size()) return std::nullopt;
        out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(in),
                   src.begin() + static_cast<std::ptrdiff_t>(in + literals));
        in += literals;

        // The last sequence is literals only.
        if (in == src.size()) break;

        if (src.size() - in < 2) return std::nullopt;
        const size_t offset = static_cast<size_t>(src[in]) | (static_cast<size_t>(src[in + 1]) << 8);
        in += 2;
        if (offset == 0 || offset > out.size()) return std::nullopt;

        size_t match = token & kRunMask;
        if (match == kRunMask && !extend(match)) return std::nullopt;
        match += kMinMatch;
        if (match > raw_size - out.size()) return std::nullopt;

        // Byte by byte: a match may overlap the bytes it is producing (offset < length),
        // which is how LZ4 encodes runs.
        size_t from = out.size() - offset;
        for (size_t i = 0; i < match; ++i) {
            const uint8_t b = out[from++];
            out.push_back(b);
        }
    }

    if (out.size() != raw_size) return std::nullopt;
    return out;
}

} // namespace aion2::proto
