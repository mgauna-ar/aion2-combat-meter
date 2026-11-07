#pragma once

#include "common/micros.hpp"
#include "protocol/events.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace aion2::game {

using EntityId = proto::EntityId;

/// One application of an effect on someone (2a 38), as the server last stated it.
struct EffectInstance {
    EntityId target{0};
    /// One per application, counting up per target: instances are stacks.
    uint32_t instance{0};
    uint32_t buff_id{0};
    uint32_t source_skill{0};
    EntityId caster{0};
    bool permanent{false};
    /// When it runs out, on the capture clock; 0 when permanent.
    Micros expires_at{0};
    /// Its whole duration as last stated, for the length of a bar.
    uint64_t duration_ms{0};
    /// When it was put on: a renewal keeps it.
    Micros applied_at{0};
    /// When it was put on or last renewed.
    Micros updated_at{0};

    [[nodiscard]] bool runs_at(Micros now) const noexcept { return permanent || expires_at > now; }
    bool operator==(const EffectInstance&) const = default;
};

/// What EffectBook::on_applied changed.
struct EffectApplied {
    /// The instance as it now stands; nullptr when nothing is kept: a marker that
    /// ends as it starts, or an entity past the cap.
    const EffectInstance* now{nullptr};
    /// The instance of that number before the record, when there was one.
    std::optional<EffectInstance> before;

    /// The record renewed an instance still running, rather than starting one.
    [[nodiscard]] bool continued(Micros ts) const noexcept {
        return now != nullptr && before.has_value() && before->buff_id == now->buff_id && before->runs_at(ts);
    }
};

/**
 * @brief The effects on every entity in view, from 2a 38, 2b 38 and 1a 92: the
 * protocol's rules for them, which every plugin that reads effects shares.
 *
 * Each application is an instance, so an effect applied again while it runs
 * stacks; 1a 92 names the effect but not the instance, and takes the one that ends
 * soonest. An instance ends at the time its record states, in the server's clock,
 * brought onto the capture clock through the last heartbeat (an end more than
 * kExpirySlack from the stated duration is not believed), so a replay sees what the
 * live meter did. A death drops the entity's effects. Ids belong to a map, so a
 * map load drops the effects of the map before; the new map's own come up to a
 * second ahead of its load (21 36), and stay.
 *
 * Each plugin keeps its own book: plugins never read each other. The mutators
 * return what they changed, so a caller can keep intervals of its own.
 */
class EffectBook {
public:
    /// Most effects kept on one entity; a misread flood stops here.
    static constexpr size_t kMaxInstancesPerEntity = 128;
    /// How far ahead of its 21 36 a map's first effects can come: 0.4 s in the second
    /// capture. A map load keeps what was put on or renewed this close before it.
    static constexpr Micros kMapLoadLead = 2 * kMicrosPerSecond;
    /// A stated end further than this from the stated duration is not believed: the
    /// record's duration, from its arrival, stands in. Renewals ran up to 1.4 s apart.
    static constexpr Micros kExpirySlack = 5 * kMicrosPerSecond;

    using Instances = std::map<uint32_t, EffectInstance>;

    void on_heartbeat(uint64_t server_ms, Micros ts) noexcept;
    /// Puts on or renews an instance. A marker that ends as it starts (a duration of
    /// 0) keeps nothing and takes away the instance of its number.
    EffectApplied on_applied(const proto::BuffApplied& b, Micros ts);
    /// Takes the instance of that effect that ends soonest, a permanent one only when
    /// nothing else is left, and returns it.
    std::optional<EffectInstance> on_removed(const proto::BuffRemoved& b);
    /// Drops every effect on `id` (HP 0 or 42 36), and returns them.
    std::vector<EffectInstance> on_death(EntityId id);
    /// Drops every instance not put on or renewed in the kMapLoadLead before `ts`
    /// (a reload within a run too, since ids may change), and returns them.
    std::vector<EffectInstance> on_map_load(Micros ts);
    /// Drops what ran out by `now`.
    void prune(Micros now);
    void clear() noexcept;

    /// The instances on `id` by number, nullptr for none. Those that ran out stay
    /// until the next prune: ask runs_at().
    [[nodiscard]] const Instances* on(EntityId id) const;
    [[nodiscard]] const std::unordered_map<EntityId, Instances>& all() const noexcept { return m_on; }
    /// Instances running on `id` at `now`.
    [[nodiscard]] size_t instances_on(EntityId id, Micros now) const;
    /// When an effect stated to end at `b.expires_server_ms` ends on the capture clock.
    [[nodiscard]] Micros expiry(const proto::BuffApplied& b, Micros ts) const noexcept;

private:
    std::unordered_map<EntityId, Instances> m_on;
    /// The server clock less the capture clock, in µs, from the last heartbeat.
    int64_t m_server_offset{0};
    bool m_have_offset{false};
};

} // namespace aion2::game
