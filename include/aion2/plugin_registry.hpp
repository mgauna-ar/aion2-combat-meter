#pragma once

#include <array>
#include <cstdint>

// What the pipeline, the app and the plugins themselves agree a plugin is. Every
// plugin's name and config section is written here and nowhere else.

namespace aion2::plugins {

enum class PluginId : uint8_t {
    CombatMeter,
    Auras,
};

struct PluginDescriptor {
    PluginId id;
    const char* name;
    const char* description;
    /// The settings file's section holding the plugin's master switch
    /// (MASTER_SWITCH_KEY) and its own settings.
    const char* config_section;
    /// The master switch on a fresh install.
    bool enabled_by_default;
};

inline constexpr PluginDescriptor COMBAT_METER{
    PluginId::CombatMeter,
    "Combat Meter",
    "Damage, healing and damage taken per player, per fight, with a saved history",
    "meter",
    true,
};

inline constexpr PluginDescriptor AURAS{
    PluginId::Auras,
    "Auras",
    "Your buffs and debuffs, yours on the boss, and your cooldowns, in an overlay of their own",
    "auras",
    false,
};

/// Key of every plugin's master switch in its config section.
inline constexpr const char* MASTER_SWITCH_KEY = "plugin_enabled";

/// Every plugin, in the order the app lists them.
inline constexpr std::array ALL{COMBAT_METER, AURAS};

/// Which plugins are switched on: the settings' master switches, and what the
/// pipeline feeds.
struct PluginSwitches {
    bool combat_meter{COMBAT_METER.enabled_by_default};
    bool auras{AURAS.enabled_by_default};

    [[nodiscard]] constexpr bool enabled(PluginId id) const noexcept {
        return id == PluginId::CombatMeter ? combat_meter : auras;
    }
    constexpr void set(PluginId id, bool on) noexcept { (id == PluginId::CombatMeter ? combat_meter : auras) = on; }

    bool operator==(const PluginSwitches&) const = default;
};

/// Every plugin on: the replay tool's and the tests' pipeline.
inline constexpr PluginSwitches ALL_ON{true, true};

/// The descriptor for `id`.
[[nodiscard]] constexpr const PluginDescriptor& find(PluginId id) noexcept {
    for (const auto& d : ALL) {
        if (d.id == id) return d;
    }
    return ALL[0];
}

} // namespace aion2::plugins
