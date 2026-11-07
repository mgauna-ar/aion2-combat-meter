#include "auras/aura_plugin.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace aion2::auras {

void AuraPlugin::on_event(const proto::ParsedEvent& event) {
    const Micros ts = event.ts;
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, proto::Heartbeat>) {
                m_tracker.on_heartbeat(e.server_ms, ts);
            } else if constexpr (std::is_same_v<T, proto::BuffApplied>) {
                m_tracker.on_applied(e, ts);
            } else if constexpr (std::is_same_v<T, proto::BuffRemoved>) {
                m_tracker.on_removed(e);
            } else if constexpr (std::is_same_v<T, proto::SkillUsed>) {
                on_own_cooldown();
                m_cooldowns.on_used(e.skill, ts);
            } else if constexpr (std::is_same_v<T, proto::CooldownSet>) {
                on_own_cooldown();
                m_cooldowns.on_set(e.skill, e.remaining_ms, ts);
            } else if constexpr (std::is_same_v<T, proto::ChainOffered>) {
                m_chains.on_offered(e, ts);
            } else if constexpr (std::is_same_v<T, proto::ChainWithdrawn>) {
                m_chains.on_withdrawn(e);
            } else if constexpr (std::is_same_v<T, proto::Identity>) {
                m_entities.on_identity(e);
            } else if constexpr (std::is_same_v<T, proto::Spawn>) {
                m_entities.on_spawn(e);
            } else if constexpr (std::is_same_v<T, proto::RemainingHp>) {
                m_entities.on_values(e);
                // HP reaching 0 is the death; 42 36 follows seconds later, with the corpse.
                if (e.has_hp && e.hp == 0) {
                    m_entities.on_dead(e.id);
                    m_tracker.on_death(e.id);
                }
            } else if constexpr (std::is_same_v<T, proto::Death>) {
                m_entities.on_gone(e.id);
                m_tracker.on_death(e.id);
            } else if constexpr (std::is_same_v<T, proto::MapLoad>) {
                on_map(e, event.connection, ts);
            } else if constexpr (std::is_same_v<T, proto::Damage>) {
                m_entities.on_damage(e, ts);
            }
        },
        event.event);
}

void AuraPlugin::on_map(const proto::MapLoad& m, uint32_t connection, Micros ts) {
    // The combat meter's test: the same map over the same connection is a teleport
    // or a revive within a run; the same map over a new one is the dungeon again.
    const bool reload = m.map_id == m_map_id && connection != 0 && connection == m_map_connection;
    m_map_id = m.map_id;
    m_map_connection = connection;
    // Ids may change on a reload too; the server sends each effect again.
    m_tracker.on_map_load(ts);
    m_entities.on_map_load(reload);
    m_chains.clear();
}

void AuraPlugin::tick(Micros now) {
    m_tracker.prune(now, m_entities);
    m_chains.prune(now);
}

void AuraPlugin::clear() { *this = AuraPlugin{}; }

void AuraPlugin::on_own_cooldown() {
    const std::string& who = m_entities.local_character();
    if (who.empty() || who == m_cooldown_character) return;
    if (!m_cooldown_character.empty()) m_cooldowns.clear();
    m_cooldown_character = who;
}

AuraSnapshot AuraPlugin::snapshot(Micros now) const {
    const EntityId target = m_entities.target(now);
    AuraSnapshot s = m_tracker.snapshot(now, m_entities, target, target != 0 ? m_entities.label(target) : std::string{});
    s.cooldowns = m_cooldowns.snapshot(now);
    s.chains = m_chains.snapshot(now);
    const Health health = m_entities.local_health();
    s.hp = health.hp;
    s.max_hp = health.max_hp;
    return s;
}

} // namespace aion2::auras
