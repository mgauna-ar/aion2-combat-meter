#pragma once

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
#include "auras/aura_tracker.hpp"
#include "common/micros.hpp"
#include "pipeline/pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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
    /// Seconds on any steady clock: what the alerts pulse by.
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

/// How long a cooldown that came back shows as ready: a "Ready" bar, a glowing icon.
inline constexpr Micros kReadyFlash = 1500 * kMicrosPerMilli;

/// A cooldown that came back within kReadyFlash of `now`.
[[nodiscard]] bool just_ready(const auras::CooldownEntry& cooldown, Micros now) noexcept;

/// The cooldowns the overlay lists at `now`: at least min_cooldown_s long, none with
/// show_cooldowns off.
/// - Bars: those cooling, and with show_ready those just_ready, the soonest ready first.
/// - Icons: those cooling, and with show_ready every one ready again too, by skill
///   code, so each keeps its place as on an action bar.
[[nodiscard]] std::vector<const auras::CooldownEntry*> shown_cooldowns(const std::vector<auras::CooldownEntry>& entries,
                                                                       const AuraSettings& settings, Micros now);

/// The local player's HP, in whole percent of their max, while low_hp_alert is on
/// and it is under low_hp_percent; nothing when their max is not known yet, or at HP
/// 0 (dead: nothing left to warn of).
[[nodiscard]] std::optional<int> low_hp_percent(const auras::AuraSnapshot& auras, const AuraSettings& settings);

/// Square tiles in rows across a width: how many a row holds (at least one), and
/// how many rows `count` of them take.
struct TileLayout {
    int columns{1};
    int rows{0};

    bool operator==(const TileLayout&) const = default;
};

[[nodiscard]] TileLayout tile_layout(float width, float size, float gap, size_t count) noexcept;

/// What a tile shows for an effect with no icon (a monster's): its label's first two
/// letters or digits, "?" for none.
[[nodiscard]] std::string tile_initials(const std::string& label);

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

/// "2m", "12", "4.5": the time left on an icon, which has room for less.
[[nodiscard]] std::string format_tile_time(Micros left);

/// Draws the aura overlay as one ImGui window filling the display, into the frame
/// the caller has begun, as bars or icons: the alerts (low HP, missing watched
/// effects, chain skills on offer), the effects on the local player, their
/// cooldowns, and theirs on their target. A no-op where ImGui is not built
/// (HAVE_IMGUI unset).
OverlayActions draw_auras(const AuraFrameInput& input);

} // namespace aion2::app
