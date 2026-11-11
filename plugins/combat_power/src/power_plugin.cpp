#include "combat_power/power_plugin.hpp"

#include <variant>

namespace aion2::combat_power {

void PowerPlugin::on_event(const proto::ParsedEvent& event) {
    if (const auto* identity = std::get_if<proto::Identity>(&event.event)) {
        m_tracker.on_identity(*identity, event.connection, event.ts);
    } else if (const auto* cast = std::get_if<proto::SkillCast>(&event.event)) {
        m_tracker.on_cast(*cast, event.ts);
    } else if (const auto* damage = std::get_if<proto::Damage>(&event.event)) {
        m_tracker.on_damage(*damage, event.ts);
    } else if (const auto* values = std::get_if<proto::RemainingHp>(&event.event)) {
        m_tracker.on_values(*values, event.connection);
    } else if (const auto* map = std::get_if<proto::MapLoad>(&event.event)) {
        m_tracker.on_map_load(*map, event.connection, event.ts);
    }
}

} // namespace aion2::combat_power
