#include "auras/chains.hpp"

#include "game/skill_names.hpp"

#include <algorithm>

namespace aion2::auras {

namespace {

/// Offers and withdrawals match by skill (game::skill_key), not by the exact code:
/// a variant's digits are the specialties picked, which a withdrawal may not repeat.
[[nodiscard]] bool same_skill(uint32_t a, uint32_t b) { return game::skill_key(a) == game::skill_key(b); }

} // namespace

void ChainTracker::on_offered(const proto::ChainOffered& offer, Micros ts) {
    if (offer.skill == 0) return;
    // Offered again before it was withdrawn: the new offer stands.
    std::erase_if(m_offers, [&](const ChainEntry& e) { return same_skill(e.skill, offer.skill); });
    if (m_offers.size() >= kMaxOffers) m_offers.erase(m_offers.begin());
    m_offers.push_back(ChainEntry{offer.skill, offer.trigger, game::skill_label(offer.skill), ts});
}

void ChainTracker::on_withdrawn(const proto::ChainWithdrawn& withdrawn) {
    std::erase_if(m_offers, [&](const ChainEntry& e) { return same_skill(e.skill, withdrawn.skill); });
}

void ChainTracker::prune(Micros now) {
    std::erase_if(m_offers, [&](const ChainEntry& e) { return now >= e.offered_at + kMaxOffer; });
}

std::vector<ChainEntry> ChainTracker::snapshot(Micros now) const {
    std::vector<ChainEntry> out;
    for (const ChainEntry& e : m_offers) {
        if (now < e.offered_at + kMaxOffer) out.push_back(e);
    }
    return out;
}

} // namespace aion2::auras
