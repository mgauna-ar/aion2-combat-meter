#pragma once

#include "auras/chains.hpp"
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

/// One effect on someone, as an overlay shows it. Whatever one caster's skill put
/// there is one entry (one effect_key): its instances, a stack, and its other
/// effects, which share its name (Doom Shield's two).
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
    /// When the entry's first running instance was put on: its place in the list.
    Micros applied_at{0};
    /// Kept up by renewals (AuraTracker::kSustainedSpans): a Chanter's mantra, which
    /// lasts 2.4 s and is renewed each second. No countdown says anything of it.
    bool sustained{false};
    /// Instances of the entry's own effect id that end; a passive sent twice is one.
    uint32_t stacks{1};
    /// Put there by the local player, or by their summons.
    bool mine{false};
    /// Put there by an enemy (Entities::is_enemy): a debuff.
    bool hostile{false};

    bool operator==(const AuraEntry&) const = default;
};

/// An effect seen on the local player this session: what a watch list or a hide
/// list can name.
struct SeenEffect {
    uint32_t key{0};
    std::string label;
    /// An enemy's: one to hide, never one to watch for.
    bool hostile{false};

    bool operator==(const SeenEffect&) const = default;
};

/// What the aura overlay draws.
struct AuraSnapshot {
    /// On the local player: buffs and debuffs, in the order they were put on, the
    /// permanent ones last. A renewal moves nothing.
    std::vector<AuraEntry> on_self;
    /// The enemy the local player is fighting (Entities::target), and the effects
    /// they have on it.
    EntityId target{0};
    std::string target_label;
    std::vector<AuraEntry> on_target;
    /// Every effect seen on the local player since the plugin started, by label.
    std::vector<SeenEffect> seen;
    /// The local player's skills used this session that have a cooldown, cooling or
    /// ready again, the soonest ready first.
    std::vector<CooldownEntry> cooldowns;
    /// The local player's chain skills on offer, the oldest first.
    std::vector<ChainEntry> chains;
    /// The local player's HP and max HP (Entities::local_health); 0 for unknown.
    uint64_t hp{0};
    uint64_t max_hp{0};

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
    /// An effect this short or shorter, renewed, and now running for more than
    /// kSustainedSpans of its own length, is kept up rather than counted down.
    /// Longer ones keep their countdown however often renewed: Fury's 20 s says
    /// when to use it again.
    static constexpr uint64_t kSustainedMaxMs = 5000;
    static constexpr uint64_t kSustainedSpans = 2;

    void on_heartbeat(uint64_t server_ms, Micros ts) noexcept;
    void on_applied(const proto::BuffApplied& b, Micros ts);
    void on_removed(const proto::BuffRemoved& b);
    void on_death(EntityId id);
    void on_map_load(Micros ts);
    /// Drops what ran out by `now`, and notes the local player's effects as seen
    /// (their id is named only after their first effects arrive), an enemy's marked
    /// hostile.
    void prune(Micros now, const Entities& entities);

    /// What the overlay draws at `now`: the local player's effects, and the local
    /// player's on `target` (0 for none). Cooldowns, chains and HP are the plugin's
    /// to add.
    [[nodiscard]] AuraSnapshot snapshot(Micros now, const Entities& entities, EntityId target,
                                        std::string target_label) const;

    /// Instances running on `id` at `now`, for tests.
    [[nodiscard]] size_t instances_on(EntityId id, Micros now) const;

private:
    [[nodiscard]] std::vector<AuraEntry> entries(EntityId id, Micros now, const Entities& entities,
                                                 bool only_mine) const;

    /// An effect's name; one with none that an enemy put there takes the enemy's.
    [[nodiscard]] static std::string label(const game::EffectInstance& i, const Entities& entities);

    game::EffectBook m_book;
    std::map<uint32_t, SeenEffect> m_seen;
};

} // namespace aion2::auras
