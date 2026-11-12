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

std::string AuraTracker::label(const game::EffectInstance& i, const Entities& entities) {
    if (game::effect_name(i.buff_id, i.source_skill).empty() && entities.is_enemy(i.caster)) {
        return entities.label(entities.resolve(i.caster));
    }
    return game::effect_label(i.buff_id, i.source_skill);
}

void AuraTracker::prune(Micros now, const Entities& entities) {
    m_book.prune(now);
    const EntityId local = entities.local();
    if (local == 0) return;
    const game::EffectBook::Instances* on = m_book.on(local);
    if (on == nullptr) return;
    for (const auto& [instance, i] : *on) {
        if (m_seen.size() >= kMaxSeen) break;
        const uint32_t key = game::effect_key(i.buff_id, i.source_skill);
        m_seen.try_emplace(key, SeenEffect{key, label(i, entities), entities.is_enemy(i.caster)});
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
        const uint64_t age_ms = now > i.applied_at ? (now - i.applied_at) / kMicrosPerMilli : 0;
        const bool sustained = !i.permanent && i.updated_at != i.applied_at && i.duration_ms <= kSustainedMaxMs &&
                               age_ms > kSustainedSpans * i.duration_ms;
        const uint32_t key = game::effect_key(i.buff_id, i.source_skill);
        const auto same = std::find_if(out.begin(), out.end(),
                                       [&](const AuraEntry& e) { return e.key == key && e.caster == i.caster; });
        if (same != out.end()) {
            // A passive sent again under a new number is no stack, nor is the
            // skill's other effect.
            if (!i.permanent && !same->permanent && i.buff_id == same->buff_id) ++same->stacks;
            if (i.permanent) continue;
            if (same->permanent) {
                // The skill's passive gives way to what it set off, which ends.
                same->permanent = false;
                same->buff_id = i.buff_id;
                same->source_skill = i.source_skill;
                same->label = label(i, entities);
                same->expires_at = i.expires_at;
                same->duration_ms = i.duration_ms;
                same->applied_at = i.applied_at;
                same->sustained = sustained;
                continue;
            }
            same->applied_at = std::min(same->applied_at, i.applied_at);
            if (i.expires_at > same->expires_at) {
                same->expires_at = i.expires_at;
                same->duration_ms = i.duration_ms;
                same->sustained = sustained;
            }
            continue;
        }
        AuraEntry e;
        e.key = key;
        e.buff_id = i.buff_id;
        e.source_skill = i.source_skill;
        e.caster = i.caster;
        e.label = label(i, entities);
        e.permanent = i.permanent;
        e.expires_at = i.expires_at;
        e.duration_ms = i.duration_ms;
        e.applied_at = i.applied_at;
        e.sustained = sustained;
        e.mine = mine;
        e.hostile = entities.is_enemy(i.caster);
        out.push_back(std::move(e));
    }
    // In the order they were put on, so a renewal moves nothing and a new effect
    // goes to the end.
    std::sort(out.begin(), out.end(), [](const AuraEntry& a, const AuraEntry& b) {
        return std::tuple(a.permanent, a.applied_at, a.label, a.caster) <
               std::tuple(b.permanent, b.applied_at, b.label, b.caster);
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
    for (const auto& [key, seen] : m_seen) s.seen.push_back(seen);
    std::sort(s.seen.begin(), s.seen.end(),
              [](const SeenEffect& a, const SeenEffect& b) { return std::tie(a.label, a.key) < std::tie(b.label, b.key); });
    return s;
}

} // namespace aion2::auras
