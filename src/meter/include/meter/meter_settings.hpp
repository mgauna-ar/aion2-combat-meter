#pragma once

#include "common/config/key_table.hpp"
#include <array>
#include <cstdint>

namespace aion2::meter {

/// How fights are cut and kept. Every key is documented once, in README.md
/// ("Configuration", section "meter").
struct MeterSettings {
    /// No party hit on an enemy for this long ends a fight.
    double idle_timeout_s{10.0};
    /// The same while a boss is still standing: phases where it cannot be hit.
    double boss_idle_timeout_s{30.0};
    /// No game traffic at all for this long ends a fight.
    double capture_lost_s{15.0};
    /// After the boss dies, ticks this much later still count.
    double kill_grace_s{2.0};
    /// An enemy with this much max HP or more makes the fight a boss fight.
    /// A guess until captures calibrate it.
    uint64_t boss_min_max_hp{1'000'000};
    /// Fights without a boss are saved when at least this long.
    double min_saved_duration_s{20.0};
    /// A killed boss that lost less than this share of its HP (by the server's HP
    /// figures) with hits we saw makes the fight's data partial.
    double partial_damage_ratio{0.7};
    /// End the fight when every party member has died. Off until captures show
    /// how revives look on the wire.
    bool wipe_detection{false};
    /// Finished fights kept in memory for the overlay's history.
    int memory_history{10};
};

inline constexpr std::array<config::ConfigKey<MeterSettings>, 9> kMeterSettingKeys{{
    config::field<MeterSettings, &MeterSettings::idle_timeout_s>("idle_timeout_s"),
    config::field<MeterSettings, &MeterSettings::boss_idle_timeout_s>("boss_idle_timeout_s"),
    config::field<MeterSettings, &MeterSettings::capture_lost_s>("capture_lost_s"),
    config::field<MeterSettings, &MeterSettings::kill_grace_s>("kill_grace_s"),
    config::field<MeterSettings, &MeterSettings::boss_min_max_hp>("boss_min_max_hp"),
    config::field<MeterSettings, &MeterSettings::min_saved_duration_s>("min_saved_duration_s"),
    config::field<MeterSettings, &MeterSettings::partial_damage_ratio>("partial_damage_ratio"),
    config::field<MeterSettings, &MeterSettings::wipe_detection>("wipe_detection"),
    config::field<MeterSettings, &MeterSettings::memory_history>("memory_history"),
}};

} // namespace aion2::meter
