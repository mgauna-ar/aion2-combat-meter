#pragma once

#include "protocol/byte_reader.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace aion2::proto {

/// Longest character name, in code points.
inline constexpr size_t kMaxNameChars = 16;
/// Shortest name believed when one is found by scanning rather than read where a
/// known layout puts it: one letter is too easily two bytes of something else.
inline constexpr size_t kMinFoundNameChars = 2;

/// Whether `utf8` is a character name: 1 to 16 letters or digits with at least one
/// letter. Any non-ASCII code point counts as a letter (accented Latin, Hangul,
/// Han). A name starting with '$' is a tutorial placeholder, not a player's.
[[nodiscard]] bool is_character_name(std::string_view utf8) noexcept;

/// The code points in `utf8` if it is a character name, else 0.
[[nodiscard]] size_t character_name_chars(std::string_view utf8) noexcept;

/// The identity record at `r`'s position: id varint, mask u32, flags u8 (bit 0:
/// has a name), name length u8, the name, server u16, class u32. With no name only
/// the id is read. The local player's own record (33 36, `own`) has a u16 of
/// unknown meaning between the name and the server: the same 1406 in all three of
/// the second capture's, whose server was 9 where the name turned up elsewhere.
/// nullopt when the record does not parse or the name is not one.
[[nodiscard]] std::optional<Identity> read_identity_record(ByteReader& r, bool own = false);

/**
 * @brief Identity records embedded in a packet the parser does not otherwise read.
 *
 * Names arrive mostly on loading screens, inside packets whose layout is not
 * known. Bytes that merely look like a record are common in a large body, so a
 * match must also carry a server id from `known_servers`, the servers already
 * seen in real identity packets, and a name of at least two characters.
 */
[[nodiscard]] std::vector<Identity> scan_identities(std::span<const uint8_t> bytes,
                                                    std::span<const uint16_t> known_servers);

} // namespace aion2::proto
