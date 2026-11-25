#pragma once

#include "auras/aura_tracker.hpp"
#include "auras/chains.hpp"
#include "auras/cooldowns.hpp"
#include "auras/entities.hpp"
#include "common/micros.hpp"
#include "game/plugin.hpp"
#include "protocol/events.hpp"

#include <string>

namespace aion2::auras {

/**
 * @brief The Auras plugin: the effects on the local player, theirs on the enemy
 * they fight, their cooldowns, their chain skills on offer and their HP, for the
 * aura overlay.
 *
 * Fed by the pipeline like the combat meter, on the same thread and the same
 * capture clock, so a replay shows what the live overlay did. It keeps its own
 * record of who is who (Entities) and reads nothing of the meter's.
 */
class AuraPlugin final : public game::Plugin {
public:
    void on_event(const proto::ParsedEvent& event) override;
    /// Drops the effects that ran out, and the chain offers past their cap.
    void tick(Micros now) override;
    /// Forgets everything: the plugin was switched off, and will miss what happens
    /// until it is back on.
    void clear();

    /// What the overlay draws at `now`.
    [[nodiscard]] AuraSnapshot snapshot(Micros now) const;

    [[nodiscard]] const Entities& entities() const noexcept { return m_entities; }
    [[nodiscard]] const AuraTracker& tracker() const noexcept { return m_tracker; }
    [[nodiscard]] const CooldownTracker& cooldowns() const noexcept { return m_cooldowns; }
    [[nodiscard]] const ChainTracker& chains() const noexcept { return m_chains; }

private:
    void on_map(const proto::MapLoad& m, uint32_t connection, Micros ts);
    /// After an identity: another character than the one the cooldowns are of drops
    /// them and their class, as it logs in, not at its first skill. Cooldowns seen
    /// before any own identity are the first character's.
    void follow_character();

    Entities m_entities;
    AuraTracker m_tracker;
    CooldownTracker m_cooldowns;
    ChainTracker m_chains;
    /// The character m_cooldowns learned from; another one starts afresh.
    std::string m_cooldown_character;
    /// That character's class (AuraSnapshot::local_class), from their own presses and
    /// their own skills' effects on them.
    game::AionClass m_class{game::AionClass::Unknown};
    uint32_t m_map_id{0};
    /// The connection the current map loaded on (Frame::connection); 0 for none.
    uint32_t m_map_connection{0};
};

} // namespace aion2::auras
