#pragma once

#include "combat_power/power_tracker.hpp"
#include "common/micros.hpp"
#include "game/plugin.hpp"
#include "protocol/events.hpp"

namespace aion2::combat_power {

/**
 * @brief The Combat Power plugin: the combat power of the player the local player is
 * fighting, and of the strongest players on the map with them, for its overlay.
 *
 * Fed by the pipeline like the others, on the same thread and the same capture
 * clock. Everything it shows the server sent on its own as each player came into
 * view (PowerTracker): it asks no one, the game least of all.
 */
class PowerPlugin final : public game::Plugin {
public:
    void on_event(const proto::ParsedEvent& event) override;
    /// Nothing runs on time alone: a target ages out as its snapshot is taken.
    void tick(Micros /*now*/) override {}
    /// Forgets everything: the plugin was switched off.
    void clear() { m_tracker.clear(); }

    /// What the overlay draws at `now`.
    [[nodiscard]] PowerSnapshot snapshot(Micros now) const { return m_tracker.snapshot(now); }

    [[nodiscard]] const PowerTracker& tracker() const noexcept { return m_tracker; }

private:
    PowerTracker m_tracker;
};

} // namespace aion2::combat_power
