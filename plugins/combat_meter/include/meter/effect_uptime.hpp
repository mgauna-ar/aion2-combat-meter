#pragma once

#include "common/micros.hpp"
#include "game/effect_book.hpp"
#include "meter/combatant_registry.hpp"
#include "meter/fight_detail.hpp"
#include "protocol/events.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aion2::meter {

/// Who a span is on and from, as the engine saw them when it opened.
struct EffectTag {
    EffectSide side{EffectSide::PartyBuff};
    EntityId target{0};
    /// The character the target id was then; 0 for an enemy or an unnamed id.
    CharacterId target_who{0};
    /// The caster, resolved: a summon's owner. 0 when the record names no one.
    EntityId caster{0};
    CharacterId caster_who{0};
};

/// One instance of an effect on a tracked target during a fight.
struct EffectSpan {
    EffectTag tag;
    uint32_t instance{0};
    uint32_t buff_id{0};
    uint32_t source_skill{0};
    Micros begin{0};
    /// The instance's end as last stated, or when it was taken off.
    Micros end{0};
};

/**
 * @brief How long each effect was on each party member and enemy during a fight.
 *
 * It reads every effect record, fight or not, through its own game::EffectBook
 * (plugins never read each other: the Auras plugin keeps one too), so a fight
 * that starts knows what is already running. During a fight each instance on a
 * target the engine tracks is a span: put on, renewed while it runs (the span
 * goes on), and ended by its stated end, a 1a 92, a death or a map load. Permanent
 * effects (passives) are left out.
 */
class EffectUptime {
public:
    /// Most spans one fight keeps; the rest are counted, not kept.
    static constexpr size_t kMaxSpans = 16384;
    /// Says whether an effect on `target` from `caster` is tracked, and as what.
    using Classify = std::function<std::optional<EffectTag>(EntityId target, EntityId caster)>;

    void on_heartbeat(uint64_t server_ms, Micros ts) noexcept { m_book.on_heartbeat(server_ms, ts); }
    /// `tag` empty: not tracked (no fight, or a target the meter does not keep).
    void on_applied(const proto::BuffApplied& b, Micros ts, const std::optional<EffectTag>& tag);
    void on_removed(const proto::BuffRemoved& b, Micros ts);
    void on_death(EntityId id, Micros ts);
    void on_map_load(Micros ts);
    void prune(Micros now);

    /// A fight starts at `ts`: each timed instance running then that `classify`
    /// keeps opens a span there.
    void begin_fight(Micros ts, const Classify& classify);
    /// Forgets the fight's spans; the book goes on.
    void end_fight() noexcept;
    /// Forgets everything, the book too: the meter was switched off and will miss
    /// what happens meanwhile.
    void clear() noexcept;

    [[nodiscard]] const std::vector<EffectSpan>& spans() const noexcept { return m_spans; }
    [[nodiscard]] size_t spans_dropped() const noexcept { return m_dropped; }
    [[nodiscard]] const game::EffectBook& book() const noexcept { return m_book; }

private:
    [[nodiscard]] static uint64_t span_key(EntityId target, uint32_t instance) noexcept {
        return (static_cast<uint64_t>(target) << 32) | instance;
    }
    void open_span(const EffectTag& tag, const game::EffectInstance& i, Micros begin);
    void end_span(EntityId target, uint32_t instance, Micros at);

    game::EffectBook m_book;
    bool m_in_fight{false};
    std::vector<EffectSpan> m_spans;
    /// The open span of each (target, instance), by span_key: an index into m_spans.
    std::unordered_map<uint64_t, size_t> m_open;
    size_t m_dropped{0};
};

using TimeSpan = std::pair<Micros, Micros>;

/// The spans clamped to [from, to], merged where they overlap or lie less than
/// `bridge` apart, by start.
[[nodiscard]] std::vector<TimeSpan> merged_spans(std::vector<TimeSpan> spans, Micros from, Micros to,
                                                 Micros bridge = 0);
/// Seconds the spans cover within [from, to], overlaps once.
[[nodiscard]] double covered_seconds(std::vector<TimeSpan> spans, Micros from, Micros to);

} // namespace aion2::meter
