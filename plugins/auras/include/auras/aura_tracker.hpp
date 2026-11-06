#pragma once

#include "auras/cooldowns.hpp"
#include "auras/entities.hpp"
#include "common/micros.hpp"
#include "game/effect_book.hpp"
#include "protocol/events.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace aion2::auras {

/// One effect on someone, as an overlay shows it. The instances of one effect from
/// one caster are one entry: a stack.
struct AuraEntry {
    /// effect_key: what a watch list names.
    uint32_t key{0};
    uint32_t buff_id{0};
    uint32_t source_skill{0};
    EntityId caster{0};
    std::string label;
    bool permanent{false};
    /// When the stack's last instance runs out, on the capture clock; 0 when permanent.
    Micros expires_at{0};
    /// That instance's whole duration, for the length of a bar.
    uint64_t duration_ms{0};
    uint32_t stacks{1};
    /// Put there by the local player, or by their summons.
    bool mine{false};
    /// Put there by an enemy (Entities::is_enemy): a debuff.
    bool hostile{false};

    bool operator==(const AuraEntry&) const = default;
};

/// A helpful effect seen on the local player this session: the watch list's choices.
struct SeenEffect {
    uint32_t key{0};
    std::string label;

    bool operator==(const SeenEffect&) const = default;
};

/// What the aura overlay draws.
struct AuraSnapshot {
    /// On the local player: buffs and debuffs, the soonest to end first and the
    /// permanent ones last.
    std::vector<AuraEntry> on_self;
    /// The enemy the local player is fighting (Entities::target), and the effects
    /// they have on it.
    EntityId target{0};
    std::string target_label;
    std::vector<AuraEntry> on_target;
    /// Every helpful effect seen on the local player since the plugin started, by label.
    std::vector<SeenEffect> seen;
    /// The local player's skills cooling down, the soonest ready first.
    std::vector<CooldownEntry> cooldowns;

    bool operator==(const AuraSnapshot&) const = default;
};

/**
 * @brief The effects on every entity in view, from 2a 38, 2b 38 and 1a 92, as the
 * aura overlay shows them.
 *
 * The protocol's rules for effects (instances are stacks, the stated end, 1a 92
 * taking the soonest, deaths and map loads) are game::EffectBook's, which the
 * combat meter shares; this adds the overlay's view of them and the watch list.
 */
class AuraTracker {
public:
    /// Most effects kept on one entity; a misread flood stops here.
    static constexpr size_t kMaxInstancesPerEntity = game::EffectBook::kMaxInstancesPerEntity;
    /// Most effects remembered as seen on the local player.
    static constexpr size_t kMaxSeen = 256;
    /// How far ahead of its 21 36 a map's first effects can come: 0.4 s in the second
    /// capture. A map load keeps what was put on or renewed this close before it.
    static constexpr Micros kMapLoadLead = game::EffectBook::kMapLoadLead;

    void on_heartbeat(uint64_t server_ms, Micros ts) noexcept;
    void on_applied(const proto::BuffApplied& b, Micros ts);
    void on_removed(const proto::BuffRemoved& b);
    void on_death(EntityId id);
    void on_map_load(Micros ts);
    /// Drops what ran out by `now`, and notes the local player's helpful effects as
    /// seen (their id is named only after their first effects arrive). An enemy's
    /// debuff is no choice for a watch list.
    void prune(Micros now, const Entities& entities);

    /// What the overlay draws at `now`: the local player's effects, and the local
    /// player's on `target` (0 for none). Cooldowns are the plugin's to add.
    [[nodiscard]] AuraSnapshot snapshot(Micros now, const Entities& entities, EntityId target,
                                        std::string target_label) const;

    /// Instances running on `id` at `now`, for tests.
    [[nodiscard]] size_t instances_on(EntityId id, Micros now) const;

private:
    [[nodiscard]] std::vector<AuraEntry> entries(EntityId id, Micros now, const Entities& entities,
                                                 bool only_mine) const;

    game::EffectBook m_book;
    std::map<uint32_t, std::string> m_seen;
};

} // namespace aion2::auras
