#pragma once

#include "common/micros.hpp"
#include "protocol/events.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aion2::auras {

/// A chain skill on offer to the local player, as the aura overlay lights it up.
struct ChainEntry {
    /// The skill on offer, as it would be cast.
    uint32_t skill{0};
    /// The skill that set it off.
    uint32_t trigger{0};
    std::string label;
    Micros offered_at{0};

    bool operator==(const ChainEntry&) const = default;
};

/**
 * @brief The local player's chain skills on offer, from 18 38 (offered) and 19 38
 * (withdrawn): Vicious Strike offers Decisive Strike, which offers Desperate Strike.
 *
 * The server withdraws every offer, used or not: in the second capture all 437 had
 * their 19 38, 379 after a use and 58 when they ran out, 3.0 to 3.1 s after the
 * offer. kMaxOffer is a cap for a 19 38 the capture lost, never the window itself,
 * which the server does not send.
 */
class ChainTracker {
public:
    /// An offer the server has not withdrawn by then is taken as gone: the longest
    /// seen ran 3.08 s.
    static constexpr Micros kMaxOffer = 5 * kMicrosPerSecond;
    /// Most offers kept at once; a misread flood stops here. Three is the most seen.
    static constexpr size_t kMaxOffers = 16;

    void on_offered(const proto::ChainOffered& offer, Micros ts);
    void on_withdrawn(const proto::ChainWithdrawn& withdrawn);
    /// Drops the offers past kMaxOffer.
    void prune(Micros now);
    /// A map load or the plugin switched off: no offer outlives either.
    void clear() noexcept { m_offers.clear(); }

    /// The offers standing at `now`, the oldest first.
    [[nodiscard]] std::vector<ChainEntry> snapshot(Micros now) const;

private:
    /// Ordered by when they were offered.
    std::vector<ChainEntry> m_offers;
};

} // namespace aion2::auras
