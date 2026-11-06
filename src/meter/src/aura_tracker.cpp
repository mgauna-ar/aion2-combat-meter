#include "meter/aura_tracker.hpp"

#include "meter/skill_names.hpp"

#include <algorithm>
#include <tuple>

namespace aion2::meter {

namespace {

/// A stated end further than this from the stated duration is not believed: the
/// record's duration, from its arrival, stands in. Renewals ran up to 1.4 s apart.
constexpr Micros kExpirySlack = 5 * kMicrosPerSecond;

[[nodiscard]] bool runs_at(const auto& instance, Micros now) noexcept {
    return instance.permanent || instance.expires_at > now;
}

} // namespace

void AuraTracker::on_heartbeat(uint64_t server_ms, Micros ts) noexcept {
    m_server_offset = static_cast<int64_t>(server_ms * kMicrosPerMilli) - static_cast<int64_t>(ts);
    m_have_offset = true;
}

Micros AuraTracker::expiry(const proto::BuffApplied& b, Micros ts) const noexcept {
    const Micros stated = ts + b.duration_ms * kMicrosPerMilli;
    if (!m_have_offset || b.expires_server_ms == 0) return stated;
    const int64_t at = static_cast<int64_t>(b.expires_server_ms * kMicrosPerMilli) - m_server_offset;
    if (at <= static_cast<int64_t>(ts)) return stated;
    const auto end = static_cast<Micros>(at);
    const Micros apart = end > stated ? end - stated : stated - end;
    return apart > kExpirySlack ? stated : end;
}

void AuraTracker::on_applied(const proto::BuffApplied& b, Micros ts) {
    auto& on = m_on[b.target];
    if (!b.permanent && b.duration_ms == 0) {
        // A marker that ends as it starts.
        on.erase(b.instance);
        return;
    }
    if (on.size() >= kMaxInstancesPerEntity && !on.contains(b.instance)) return;
    Instance& i = on[b.instance];
    i.buff_id = b.buff_id;
    i.source_skill = b.source_skill;
    i.caster = b.caster;
    i.permanent = b.permanent;
    i.duration_ms = b.duration_ms;
    i.expires_at = b.permanent ? 0 : expiry(b, ts);
    i.updated_at = ts;
}

void AuraTracker::on_removed(const proto::BuffRemoved& b) {
    const auto on = m_on.find(b.target);
    if (on == m_on.end()) return;
    // The one that ends soonest: a permanent one only when nothing else is left.
    auto pick = on->second.end();
    for (auto it = on->second.begin(); it != on->second.end(); ++it) {
        if (it->second.buff_id != b.buff_id) continue;
        if (pick == on->second.end() ||
            std::tuple(it->second.permanent, it->second.expires_at) <
                std::tuple(pick->second.permanent, pick->second.expires_at)) {
            pick = it;
        }
    }
    if (pick != on->second.end()) on->second.erase(pick);
    if (on->second.empty()) m_on.erase(on);
}

void AuraTracker::on_death(EntityId id) { m_on.erase(id); }

void AuraTracker::on_map_load(Micros ts) {
    const Micros keep_from = ts > kMapLoadLead ? ts - kMapLoadLead : 0;
    for (auto on = m_on.begin(); on != m_on.end();) {
        std::erase_if(on->second, [&](const auto& kv) { return kv.second.updated_at < keep_from; });
        on = on->second.empty() ? m_on.erase(on) : std::next(on);
    }
}

void AuraTracker::prune(Micros now, const CombatantRegistry& registry) {
    const EntityId local = registry.local_player();
    for (auto on = m_on.begin(); on != m_on.end();) {
        std::erase_if(on->second, [&](const auto& kv) { return !runs_at(kv.second, now); });
        if (on->first == local && local != 0) {
            for (const auto& [instance, i] : on->second) {
                if (m_seen.size() >= kMaxSeen) break;
                if (registry.side(registry.resolve(i.caster)) == Side::Enemy) continue;
                m_seen.try_emplace(effect_key(i.buff_id, i.source_skill), effect_label(i.buff_id, i.source_skill));
            }
        }
        on = on->second.empty() ? m_on.erase(on) : std::next(on);
    }
}

size_t AuraTracker::instances_on(EntityId id, Micros now) const {
    const auto on = m_on.find(id);
    if (on == m_on.end()) return 0;
    return static_cast<size_t>(
        std::count_if(on->second.begin(), on->second.end(), [&](const auto& kv) { return runs_at(kv.second, now); }));
}

std::vector<AuraEntry> AuraTracker::entries(EntityId id, Micros now, const CombatantRegistry& registry,
                                            bool only_mine) const {
    std::vector<AuraEntry> out;
    const auto on = m_on.find(id);
    if (on == m_on.end()) return out;
    const EntityId local = registry.local_player();
    for (const auto& [instance, i] : on->second) {
        if (!runs_at(i, now)) continue;
        const EntityId caster = registry.resolve(i.caster);
        const bool mine = local != 0 && caster == local;
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
        e.key = effect_key(i.buff_id, i.source_skill);
        e.buff_id = i.buff_id;
        e.source_skill = i.source_skill;
        e.caster = i.caster;
        e.label = effect_label(i.buff_id, i.source_skill);
        e.permanent = i.permanent;
        e.expires_at = i.expires_at;
        e.duration_ms = i.duration_ms;
        e.mine = mine;
        e.hostile = registry.side(caster) == Side::Enemy;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const AuraEntry& a, const AuraEntry& b) {
        return std::tuple(a.permanent, a.expires_at, a.label) < std::tuple(b.permanent, b.expires_at, b.label);
    });
    return out;
}

AuraSnapshot AuraTracker::snapshot(Micros now, const CombatantRegistry& registry, EntityId target,
                                   std::string target_label) const {
    AuraSnapshot s;
    if (const EntityId local = registry.local_player(); local != 0) s.on_self = entries(local, now, registry, false);
    if (target != 0) {
        s.target = target;
        s.target_label = std::move(target_label);
        s.on_target = entries(target, now, registry, true);
    }
    s.seen.reserve(m_seen.size());
    for (const auto& [key, label] : m_seen) s.seen.push_back(SeenEffect{key, label});
    std::sort(s.seen.begin(), s.seen.end(),
              [](const SeenEffect& a, const SeenEffect& b) { return std::tie(a.label, a.key) < std::tie(b.label, b.key); });
    return s;
}

} // namespace aion2::meter
