#pragma once

#include "app/app_settings.hpp"
#include "app/game_icons.hpp"
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
/// with show_debuffs. `pins` are the class's (pins_for): once there are any, `self`
/// lists enemies' effects alone, the rest being in the pins' slots or left out.
[[nodiscard]] std::vector<const auras::AuraEntry*> shown_auras(const std::vector<auras::AuraEntry>& entries,
                                                               const AuraSettings& settings, Micros now, bool self,
                                                               const std::vector<uint32_t>& pins = {});

/// How long a cooldown that came back shows as ready: a "Ready" bar, a glowing icon.
inline constexpr Micros kReadyFlash = 1500 * kMicrosPerMilli;

/// A cooldown that came back within kReadyFlash of `now`.
[[nodiscard]] bool just_ready(const auras::CooldownEntry& cooldown, Micros now) noexcept;

/// The cooldowns the overlay lists at `now`: at least min_cooldown_s long, none with
/// show_cooldowns off, and none of a skill one of `pins` names (pinned_cooldown):
/// its slot shows it.
/// - Bars: those cooling, and with show_ready those just_ready, the soonest ready first.
/// - Icons: those cooling, and with show_ready every one ready again too, by skill
///   code, so each keeps its place as on an action bar.
[[nodiscard]] std::vector<const auras::CooldownEntry*> shown_cooldowns(const std::vector<auras::CooldownEntry>& entries,
                                                                       const AuraSettings& settings, Micros now,
                                                                       const std::vector<uint32_t>& pins = {});

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
    /// Its icon, for a tile (game_icons): the skill's, or the item's; kNoIcon for none.
    int icon{kNoIcon};

    bool operator==(const MissingEffect&) const = default;
};

/// The watch list's effects not on the local player at `now`, in the list's order,
/// but for `pins` (pins_for): a pinned one's slot shows it missing. Labels come from
/// the effects seen this session, else the skill table.
[[nodiscard]] std::vector<MissingEffect> missing_effects(const auras::AuraSnapshot& auras,
                                                         const std::vector<uint32_t>& watch, Micros now,
                                                         const std::vector<uint32_t>& pins = {});

/// What a pinned effect's slot shows (pinned_slots).
enum class PinState : uint8_t {
    /// On the local player: its time left, as an effect's bar or tile shows it.
    Active,
    /// Off, and the skill behind it cooling: the time until it is ready.
    Cooling,
    /// Off, and that skill ready again (show_ready).
    Ready,
    /// Off, with no cooldown to say: a passive between its procs (Fury between
    /// blocks), an ally's buff, an item's.
    Off,
};

/// One pinned effect: a slot of its own, drawn whatever its state, in the order pinned.
struct PinnedSlot {
    uint32_t key{0};
    std::string label;
    /// effect_key_icon: the same in every state.
    int icon{kNoIcon};
    PinState state{PinState::Off};
    /// Active: the entry it shows (time left, stacks, colour), in the snapshot.
    const auras::AuraEntry* effect{nullptr};
    /// Cooling or Ready: the skill's cooldown, in the snapshot.
    const auras::CooldownEntry* cooldown{nullptr};
    /// Watched, and not on: its slot is the missing alert.
    bool missing{false};
};

/// The cooldown a pin's slot shows while its effect is off: the cooldown of the
/// skill the key is (game::skill_base of a skill key), when it was used this session;
/// null for an item's or a monster's key.
[[nodiscard]] const auras::CooldownEntry* pinned_cooldown(const std::vector<auras::CooldownEntry>& cooldowns,
                                                          uint32_t key);

/// The slots of the effects pinned for the local player's class
/// (AuraSnapshot::local_class), in the order pinned; none while it is not known.
/// - A running entry of the key that no enemy put there lights its slot, unless it
///   is a passive's always-on half (permanent, naming no skill): Fury's lights only
///   while a block's proc runs, a Chanter's mantra (a toggle: permanent, naming its
///   skill) while it is on. The local player's own entry first, then the one ending
///   last. Pins ignore min_duration_s and show_permanent: picked, they show.
/// - Off, the skill's cooldown (pinned_cooldown) while it cools, then Ready with
///   show_ready.
/// - Labels: the effects seen this session, else the entry's, else the key's.
[[nodiscard]] std::vector<PinnedSlot> pinned_slots(const auras::AuraSnapshot& auras, const AuraSettings& settings,
                                                   Micros now);

/// "2m05", "12", "4.5": the time left on a bar.
[[nodiscard]] std::string format_time_left(Micros left);

/// "2m", "12", "4.5": the time left on an icon, which has room for less.
[[nodiscard]] std::string format_tile_time(Micros left);

/// The side of a bar's icon, in pixels before the scale: what the window's atlas
/// holds the game's icons at while the overlay draws bars (OverlayWindow::set_icon_px).
/// As icons, it holds them at the settings' icon_size.
inline constexpr float kAuraBarIconPx = 20.0f;

/// Draws the aura overlay as one ImGui window filling the display, into the frame
/// the caller has begun, as bars or icons: the alerts (low HP, missing watched
/// effects, chain skills on offer), the pinned effects' slots, the effects on the
/// local player, their cooldowns, and theirs on their target. A no-op where ImGui is
/// not built (HAVE_IMGUI unset).
OverlayActions draw_auras(const AuraFrameInput& input);

} // namespace aion2::app
