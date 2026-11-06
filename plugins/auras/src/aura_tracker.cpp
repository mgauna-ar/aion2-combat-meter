#include "auras/aura_tracker.hpp"

#include "game/skill_names.hpp"

#include <algorithm>
#include <tuple>

namespace aion2::auras {

void AuraTracker::on_heartbeat(uint64_t server_ms, Micros ts) noexcept { m_book.on_heartbeat(server_ms, ts); }

void AuraTracker::on_applied(const proto::BuffApplied& b, Micros ts) { (void)m_book.on_applied(b, ts); }

void AuraTracker::on_removed(const proto::BuffRemoved& b) { (void)m_book.on_removed(b); }

void AuraTracker::on_death(EntityId id) { (void)m_book.on_death(id); }

void AuraTracker::on_map_load(Micros ts) { (void)m_book.on_map_load(ts); }

void AuraTracker::prune(Micros now, const Entities& entities) {
    m_book.prune(now);
    const EntityId local = entities.local();
    if (local == 0) return;
    const game::EffectBook::Instances* on = m_book.on(local);
    if (on == nullptr) return;
    for (const auto& [instance, i] : *on) {
        if (m_seen.size() >= kMaxSeen) break;
        if (entities.is_enemy(i.caster)) continue;
        m_seen.try_emplace(game::effect_key(i.buff_id, i.source_skill), game::effect_label(i.buff_id, i.source_skill));
    }
}

size_t AuraTracker::instances_on(EntityId id, Micros now) const { return m_book.instances_on(id, now); }

std::vector<AuraEntry> AuraTracker::entries(EntityId id, Micros now, const Entities& entities,
                                            bool only_mine) const {
    std::vector<AuraEntry> out;
    const game::EffectBook::Instances* on = m_book.on(id);
    if (on == nullptr) return out;
    for (const auto& [instance, i] : *on) {
        if (!i.runs_at(now)) continue;
        const bool mine = entities.mine(i.caster);
        if (only_mine && !mine) continue;
        const auto same = std::find_if(out.begin(), out.end(), [&](const AuraEntry& e) {
            return e.buff_id == i.buff_id && e.caster == i.caster;
        });
        if (same != out.end()) {
            ++same->stacks;
            if (!i.permanent && (same->permanent || i.expires_at > same->expires_at)) {
                same->permanent = false;
                same->expires_at = i.expires_at;
                same->duration_ms = i.duration_ms;
            }
            continue;
        }
        AuraEntry e;
        e.key = game::effect_key(i.buff_id, i.source_skill);
        e.buff_id = i.buff_id;
        e.source_skill = i.source_skill;
        e.caster = i.caster;
        e.label = game::effect_label(i.buff_id, i.source_skill);
        e.permanent = i.permanent;
        e.expires_at = i.expires_at;
        e.duration_ms = i.duration_ms;
        e.mine = mine;
        e.hostile = entities.is_enemy(i.caster);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const AuraEntry& a, const AuraEntry& b) {
        return std::tuple(a.permanent, a.expires_at, a.label) < std::tuple(b.permanent, b.expires_at, b.label);
    });
    return out;
}

AuraSnapshot AuraTracker::snapshot(Micros now, const Entities& entities, EntityId target,
                                   std::string target_label) const {
    AuraSnapshot s;
    if (const EntityId local = entities.local(); local != 0) s.on_self = entries(local, now, entities, false);
    if (target != 0) {
        s.target = target;
        s.target_label = std::move(target_label);
        s.on_target = entries(target, now, entities, true);
    }
    s.seen.reserve(m_seen.size());
    for (const auto& [key, label] : m_seen) s.seen.push_back(SeenEffect{key, label});
    std::sort(s.seen.begin(), s.seen.end(),
              [](const SeenEffect& a, const SeenEffect& b) { return std::tie(a.label, a.key) < std::tie(b.label, b.key); });
    return s;
}

} // namespace aion2::auras
