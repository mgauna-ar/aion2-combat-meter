#include "meter/effect_uptime.hpp"

#include <algorithm>

namespace aion2::meter {

std::string_view effect_side_name(EffectSide side) noexcept {
    switch (side) {
        case EffectSide::PartyBuff: return "buff";
        case EffectSide::PartyDebuff: return "debuff";
        case EffectSide::OnEnemy: return "enemy";
    }
    return "buff";
}

EffectSide effect_side_from_name(std::string_view name) noexcept {
    if (name == "debuff") return EffectSide::PartyDebuff;
    if (name == "enemy") return EffectSide::OnEnemy;
    return EffectSide::PartyBuff;
}

double EffectUptimeRow::uptime(double duration_s) const noexcept {
    if (duration_s <= 0.0) return 0.0;
    return std::clamp(active_s / duration_s, 0.0, 1.0);
}

void EffectUptime::open_span(const EffectTag& tag, const game::EffectInstance& i, Micros begin) {
    if (m_spans.size() >= kMaxSpans) {
        ++m_dropped;
        return;
    }
    EffectSpan s;
    s.tag = tag;
    s.instance = i.instance;
    s.buff_id = i.buff_id;
    s.source_skill = i.source_skill;
    s.begin = begin;
    s.end = std::max(i.expires_at, begin);
    m_open[span_key(i.target, i.instance)] = m_spans.size();
    m_spans.push_back(s);
}

void EffectUptime::end_span(EntityId target, uint32_t instance, Micros at) {
    const auto open = m_open.find(span_key(target, instance));
    if (open == m_open.end()) return;
    EffectSpan& s = m_spans[open->second];
    s.end = std::max(s.begin, std::min(s.end, at));
    m_open.erase(open);
}

void EffectUptime::on_applied(const proto::BuffApplied& b, Micros ts, const std::optional<EffectTag>& tag) {
    const game::EffectApplied a = m_book.on_applied(b, ts);
    if (!m_in_fight) return;
    const auto open = m_open.find(span_key(b.target, b.instance));
    if (open != m_open.end() && a.continued(ts)) {
        // Renewed while it ran: the same span goes on.
        m_spans[open->second].end = std::max(m_spans[open->second].begin, a.now->expires_at);
        return;
    }
    // Whatever ran under this number ends here.
    end_span(b.target, b.instance, ts);
    if (a.now == nullptr || a.now->permanent || !tag) return;
    open_span(*tag, *a.now, ts);
}

void EffectUptime::on_removed(const proto::BuffRemoved& b, Micros ts) {
    const auto removed = m_book.on_removed(b);
    if (removed && m_in_fight) end_span(removed->target, removed->instance, ts);
}

void EffectUptime::on_death(EntityId id, Micros ts) {
    for (const game::EffectInstance& i : m_book.on_death(id)) {
        if (m_in_fight) end_span(i.target, i.instance, ts);
    }
}

void EffectUptime::on_map_load(Micros ts) {
    for (const game::EffectInstance& i : m_book.on_map_load(ts)) {
        if (m_in_fight) end_span(i.target, i.instance, ts);
    }
}

void EffectUptime::prune(Micros now) {
    m_book.prune(now);
    // A span that ran its course is no longer open: its number may come again.
    std::erase_if(m_open, [&](const auto& kv) { return m_spans[kv.second].end <= now; });
}

void EffectUptime::begin_fight(Micros ts, const Classify& classify) {
    m_spans.clear();
    m_open.clear();
    m_dropped = 0;
    m_in_fight = true;
    for (const auto& [id, instances] : m_book.all()) {
        for (const auto& [number, i] : instances) {
            if (i.permanent || !i.runs_at(ts)) continue;
            if (const auto tag = classify(i.target, i.caster)) open_span(*tag, i, ts);
        }
    }
}

void EffectUptime::end_fight() noexcept {
    m_in_fight = false;
    m_spans.clear();
    m_open.clear();
    m_dropped = 0;
}

void EffectUptime::clear() noexcept {
    end_fight();
    m_book.clear();
}

std::vector<TimeSpan> merged_spans(std::vector<TimeSpan> spans, Micros from, Micros to, Micros bridge) {
    std::vector<TimeSpan> clamped;
    clamped.reserve(spans.size());
    for (const auto& [b, e] : spans) {
        const Micros begin = std::max(b, from);
        const Micros end = std::min(e, to);
        if (end > begin) clamped.emplace_back(begin, end);
    }
    std::sort(clamped.begin(), clamped.end());
    std::vector<TimeSpan> merged;
    for (const TimeSpan& s : clamped) {
        if (!merged.empty() && s.first <= merged.back().second + bridge) {
            merged.back().second = std::max(merged.back().second, s.second);
        } else {
            merged.push_back(s);
        }
    }
    return merged;
}

double covered_seconds(std::vector<TimeSpan> spans, Micros from, Micros to) {
    Micros total = 0;
    for (const auto& [b, e] : merged_spans(std::move(spans), from, to)) total += e - b;
    return to_seconds(total);
}

} // namespace aion2::meter
