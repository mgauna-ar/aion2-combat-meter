#include "game/effect_book.hpp"

#include <algorithm>
#include <iterator>
#include <tuple>

namespace aion2::game {

void EffectBook::on_heartbeat(uint64_t server_ms, Micros ts) noexcept {
    m_server_offset = static_cast<int64_t>(server_ms * kMicrosPerMilli) - static_cast<int64_t>(ts);
    m_have_offset = true;
}

Micros EffectBook::expiry(const proto::BuffApplied& b, Micros ts) const noexcept {
    const Micros stated = ts + b.duration_ms * kMicrosPerMilli;
    if (!m_have_offset || b.expires_server_ms == 0) return stated;
    const int64_t at = static_cast<int64_t>(b.expires_server_ms * kMicrosPerMilli) - m_server_offset;
    if (at <= static_cast<int64_t>(ts)) return stated;
    const auto end = static_cast<Micros>(at);
    const Micros apart = end > stated ? end - stated : stated - end;
    return apart > kExpirySlack ? stated : end;
}

EffectApplied EffectBook::on_applied(const proto::BuffApplied& b, Micros ts) {
    EffectApplied out;
    Instances& on = m_on[b.target];
    const auto had = on.find(b.instance);
    if (had != on.end()) out.before = had->second;
    if (!b.permanent && b.duration_ms == 0) {
        // A marker that ends as it starts.
        if (had != on.end()) on.erase(had);
        if (on.empty()) m_on.erase(b.target);
        return out;
    }
    if (on.size() >= kMaxInstancesPerEntity && had == on.end()) return out;
    EffectInstance& i = on[b.instance];
    const bool continued = out.before.has_value() && out.before->buff_id == b.buff_id && out.before->runs_at(ts);
    i.target = b.target;
    i.instance = b.instance;
    i.buff_id = b.buff_id;
    i.source_skill = b.source_skill;
    i.caster = b.caster;
    i.permanent = b.permanent;
    i.duration_ms = b.duration_ms;
    i.expires_at = b.permanent ? 0 : expiry(b, ts);
    i.applied_at = continued ? out.before->applied_at : ts;
    i.updated_at = ts;
    out.now = &i;
    return out;
}

std::optional<EffectInstance> EffectBook::on_removed(const proto::BuffRemoved& b) {
    const auto on = m_on.find(b.target);
    if (on == m_on.end()) return std::nullopt;
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
    if (pick == on->second.end()) return std::nullopt;
    EffectInstance removed = pick->second;
    on->second.erase(pick);
    if (on->second.empty()) m_on.erase(on);
    return removed;
}

std::vector<EffectInstance> EffectBook::on_death(EntityId id) {
    std::vector<EffectInstance> dropped;
    const auto on = m_on.find(id);
    if (on == m_on.end()) return dropped;
    dropped.reserve(on->second.size());
    for (const auto& [instance, i] : on->second) dropped.push_back(i);
    m_on.erase(on);
    return dropped;
}

std::vector<EffectInstance> EffectBook::on_map_load(Micros ts) {
    std::vector<EffectInstance> dropped;
    const Micros keep_from = ts > kMapLoadLead ? ts - kMapLoadLead : 0;
    for (auto on = m_on.begin(); on != m_on.end();) {
        for (auto it = on->second.begin(); it != on->second.end();) {
            if (it->second.updated_at < keep_from) {
                dropped.push_back(it->second);
                it = on->second.erase(it);
            } else {
                ++it;
            }
        }
        on = on->second.empty() ? m_on.erase(on) : std::next(on);
    }
    return dropped;
}

void EffectBook::prune(Micros now) {
    for (auto on = m_on.begin(); on != m_on.end();) {
        std::erase_if(on->second, [&](const auto& kv) { return !kv.second.runs_at(now); });
        on = on->second.empty() ? m_on.erase(on) : std::next(on);
    }
}

void EffectBook::clear() noexcept {
    m_on.clear();
    m_server_offset = 0;
    m_have_offset = false;
}

const EffectBook::Instances* EffectBook::on(EntityId id) const {
    const auto on = m_on.find(id);
    return on == m_on.end() ? nullptr : &on->second;
}

size_t EffectBook::instances_on(EntityId id, Micros now) const {
    const Instances* on = this->on(id);
    if (on == nullptr) return 0;
    return static_cast<size_t>(
        std::count_if(on->begin(), on->end(), [&](const auto& kv) { return kv.second.runs_at(now); }));
}

} // namespace aion2::game
