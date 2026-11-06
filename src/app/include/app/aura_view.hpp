#pragma once

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
#include "auras/aura_tracker.hpp"
#include "common/micros.hpp"
#include "pipeline/pipeline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aion2::app {

/// One frame's inputs for the aura overlay. The snapshot may be null before the first.
struct AuraFrameInput {
    const pipeline::LiveSnapshot* snapshot{nullptr};
    const AuraSettings* settings{nullptr};
    /// The settings' scale times the monitor's DPI scale.
    float scale{1.0f};
    /// The capture time the bars count down to (aura_display_time). For drawing only:
    /// the engine's clock is the snapshot's.
    Micros now{0};
    /// Seconds on any steady clock: what a missing watched effect pulses by.
    double pulse_s{0.0};
};

/// Snapshots come every 200 ms of capture time; between them the countdown runs on
/// the wall clock, this far at most, so a stalled capture never runs bars out.
inline constexpr Micros kAuraDisplayLead = 250 * kMicrosPerMilli;

/// The capture time to draw a snapshot taken at `snapshot_now` with, `since_arrival`
/// after the UI got it.
[[nodiscard]] Micros aura_display_time(Micros snapshot_now, Micros since_arrival) noexcept;

/// The effects one side of the overlay lists at `now`, after the settings' filters:
/// running, long enough, permanent only when asked, and (`self`) an enemy's only
/// with show_debuffs.
[[nodiscard]] std::vector<const auras::AuraEntry*> shown_auras(const std::vector<auras::AuraEntry>& entries,
                                                               const AuraSettings& settings, Micros now, bool self);

/// The cooldowns the overlay lists at `now`: running, at least min_cooldown_s long,
/// and none with show_cooldowns off.
[[nodiscard]] std::vector<const auras::CooldownEntry*> shown_cooldowns(const std::vector<auras::CooldownEntry>& entries,
                                                                       const AuraSettings& settings, Micros now);

/// A watched effect the local player lacks.
struct MissingEffect {
    uint32_t key{0};
    std::string label;

    bool operator==(const MissingEffect&) const = default;
};

/// The watch list's effects not on the local player at `now`, in the list's order.
/// Labels come from the effects seen this session, else the skill table.
[[nodiscard]] std::vector<MissingEffect> missing_effects(const auras::AuraSnapshot& auras,
                                                         const std::vector<uint32_t>& watch, Micros now);

/// "2m05", "12", "4.5": the time left on a bar.
[[nodiscard]] std::string format_time_left(Micros left);

/// Draws the aura overlay as one ImGui window filling the display, into the frame
/// the caller has begun: missing watched effects, the effects on the local player,
/// theirs on their target, and their cooldowns. A no-op where ImGui is not built
/// (HAVE_IMGUI unset).
OverlayActions draw_auras(const AuraFrameInput& input);

} // namespace aion2::app
