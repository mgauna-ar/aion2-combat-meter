#pragma once

#include "common/micros.hpp"
#include "meter/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aion2::meter {

/// What landed on a player in the seconds before they died.
enum class RecapKind : uint8_t {
    Hit,
    Dot,
    Heal,
    HealTick,
};

[[nodiscard]] std::string_view recap_kind_name(RecapKind kind) noexcept;
[[nodiscard]] RecapKind recap_kind_from_name(std::string_view name) noexcept;

/// One line of a death's recap.
struct RecapEvent {
    /// How long before the death it landed.
    double before_s{0.0};
    RecapKind kind{RecapKind::Hit};
    /// game::skill_key.
    uint32_t skill{0};
    /// Damage: the attacker's NPC code, 0 when its spawn was not seen.
    uint32_t source_npc{0};
    /// Healing: who healed.
    std::string healer;
    uint64_t amount{0};
    bool crit{false};

    [[nodiscard]] bool damage() const noexcept { return kind == RecapKind::Hit || kind == RecapKind::Dot; }
    bool operator==(const RecapEvent&) const = default;
};

/// A party member's death in a fight: 00 8D's HP 0, the only way the server says
/// it of a player.
struct DeathRecord {
    /// The row's CombatantTotals::id when the player has one, else the id that died.
    EntityId player{0};
    std::string name;
    AionClass cls{AionClass::Unknown};
    /// Since the fight's start.
    double time_s{0.0};
    /// Until HP above 0 or their own hit said they were up; below 0 while they were
    /// still down when the fight ended.
    double up_after_s{-1.0};
    /// Oldest first.
    std::vector<RecapEvent> recap;

    [[nodiscard]] bool up() const noexcept { return up_after_s >= 0.0; }
    /// The last damage in the recap; nullptr for none.
    [[nodiscard]] const RecapEvent* killing_blow() const noexcept;
    bool operator==(const DeathRecord&) const = default;
};

/// One event as it lands, before the death it may come to recap.
struct RecapSample {
    Micros ts{0};
    RecapKind kind{RecapKind::Hit};
    uint32_t skill{0};
    uint32_t source_npc{0};
    /// The healer's resolved id, for their name once a death needs it.
    EntityId source{0};
    uint64_t amount{0};
    bool crit{false};
};

/// The last events that landed on one party member, kept for death recaps.
class RecapRing {
public:
    static constexpr size_t kCapacity = 16;

    void push(const RecapSample& sample) noexcept {
        m_samples[m_next] = sample;
        m_next = (m_next + 1) % kCapacity;
        if (m_size < kCapacity) ++m_size;
    }
    [[nodiscard]] size_t size() const noexcept { return m_size; }
    /// i = 0 is the oldest sample kept.
    [[nodiscard]] const RecapSample& at(size_t i) const noexcept {
        return m_samples[(m_next + kCapacity - m_size + i) % kCapacity];
    }

private:
    std::array<RecapSample, kCapacity> m_samples{};
    size_t m_next{0};
    size_t m_size{0};
};

/// How far back a recap reaches.
inline constexpr Micros kRecapWindow = 12 * kMicrosPerSecond;
/// Most lines a recap shows.
inline constexpr size_t kRecapShown = 10;
/// Most deaths one fight keeps.
inline constexpr size_t kMaxDeathsPerFight = 256;

/// The newest kRecapShown samples within kRecapWindow of `death`, oldest first.
[[nodiscard]] std::vector<RecapSample> recap_before(const RecapRing& ring, Micros death);

} // namespace aion2::meter
