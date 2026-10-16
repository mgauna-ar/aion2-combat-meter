#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aion2::proto {

/// No bundle the game sends comes near this; a larger raw size means the bytes are
/// not a bundle, and honouring it would allocate whatever they say.
inline constexpr size_t kMaxLz4RawSize = 4u << 20;

/// Decompresses one LZ4 block (the raw block format, no frame header) that must
/// expand to exactly `raw_size` bytes. nullopt when it does not: a truncated block,
/// an offset reaching before the output, output past `raw_size`, or `raw_size`
/// over kMaxLz4RawSize. Written from the published block format rather than vendored,
/// as the hub's inflate is: it is short, and decoding is all the meter needs.
[[nodiscard]] std::optional<std::vector<uint8_t>> lz4_decompress_block(std::span<const uint8_t> src,
                                                                       size_t raw_size);

} // namespace aion2::proto
