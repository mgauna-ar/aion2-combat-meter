#pragma once

#include "protocol/byte_reader.hpp"
#include "protocol/events.hpp"
#include "protocol/frame_decoder.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace aion2::proto {

struct ParseStats {
    uint64_t frames{0};
    /// Frames of an opcode the parser reads that parsed.
    uint64_t decoded{0};
    /// Frames of an opcode the parser reads that did not: the layout changed, or
    /// the notes it follows were wrong for this case.
    uint64_t failed{0};
    /// Frames of an opcode the parser does not read.
    uint64_t unknown{0};
    uint64_t scanned_identities{0};
    std::map<uint16_t, uint64_t> failures_by_opcode;

    /// decoded / (decoded + failed), as a percentage; 100 with nothing to read.
    [[nodiscard]] double coverage_percent() const noexcept {
        const uint64_t tried = decoded + failed;
        return tried == 0 ? 100.0 : 100.0 * static_cast<double>(decoded) / static_cast<double>(tried);
    }
};

/**
 * @brief Game frames to events.
 *
 * Layouts follow the community's notes on the Global client (README, "Protocol").
 * Nothing here throws or trusts a length: a body that does not parse counts as a
 * failure for its opcode and yields nothing, and --dump shows its bytes. The parser
 * remembers which servers real identity packets named, so the identity scan of
 * unknown packets can reject look-alikes.
 */
class PacketParser {
public:
    /// Longest extra-hit list believed; more means the record was misread.
    static constexpr uint64_t kMaxExtraHits = 64;
    /// Most records one damage packet is believed to hold.
    static constexpr int kMaxDamageRecords = 32;
    /// Most party members believed (an alliance included).
    static constexpr uint64_t kMaxPartyMembers = 24;

    /// Appends the events in `frame` to `out`.
    void parse(const Frame& frame, std::vector<ParsedEvent>& out);

    [[nodiscard]] const ParseStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] std::span<const uint16_t> known_servers() const noexcept { return m_known_servers; }

    /// The damage records of a 04 38 body; false when the first does not parse.
    /// Exposed for tests.
    [[nodiscard]] static bool parse_damage(std::span<const uint8_t> body, std::vector<Damage>& out);

private:
    bool parse_known(const Frame& frame, std::vector<ParsedEvent>& out);
    void remember_server(uint16_t server);

    ParseStats m_stats;
    std::vector<uint16_t> m_known_servers;
};

} // namespace aion2::proto
