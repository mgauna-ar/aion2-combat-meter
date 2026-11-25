# Auras: Agent Guidelines

The rules of the Auras plugin (`plugins/auras`, namespace `aion2::auras`). The
repository's [`AGENTS.md`](../../AGENTS.md) holds what every plugin shares: Passive
only, the engineering principles, the layering, the threads, the protocol (the effect,
skill-use and cooldown opcodes are under "Effects, skill uses and cooldowns") and the
self-update. What a user sees and sets is in this plugin's [`README.md`](README.md).

**What it is:** `AuraPlugin` (a `game::Plugin`) owns four parts, fed the same events
as the combat meter on the same thread and the same capture clock:
- `Entities`: who is who, as far as the auras need it, and the local player's HP;
- `AuraTracker`: the effects on every entity in view;
- `CooldownTracker`: the local player's cooldowns, from `data/cooldowns.tsv`
  (`tools/gen_cooldown_table.py` makes `src/cooldown_table.inc` from it);
- `ChainTracker`: the local player's chain skills on offer.

The app draws it: the aura overlay (`src/app/src/aura_view.cpp`) and the Auras page
(`src/app/src/ui/view_auras.cpp`).

**What it may use:** the protocol layer and the game data (`src/game`: effect and skill
names, npc names). Never the combat meter: it reads nothing of `CombatantRegistry` or
the meter's fights, so it keeps its own record of who is who (`Entities`) and picks its
own target. Switched off, the pipeline feeds it nothing and clears it: it would miss
what happens meanwhile, so it starts afresh when switched back on.

---

## 🧭 Who is who (`Entities`)

The registry's rules, cut to what the auras need: no names, no sides inferred from
hits, no party.
- **The local player** is the id of the last own identity (33 36), or of the last own
  values (00 8D kind 1 or 3), unless that id is a spawn. Another player's identity on
  that id, or a spawn on it after a new map, means it is no longer theirs.
- **Their character** is the own identity's `name@server`. A new one clears the
  cooldowns as its identity comes, not at its first skill: until then the old
  character's showed, and as icons their ready skills stayed lit. What was learned
  was the other character's.
- **Owners:** a spawn of a player-object kind with an owner (`proto::spawn_kind`)
  belongs to its owner, a summon's summon too. `mine()` is the local player or what
  they own.
- **Enemies:** a spawn that is not a player object. A spawn on an id an identity named
  on this map is that player coming into view, not an NPC. A mob already in view when
  the meter started never spawned for it, so its effects show as no one's until it
  comes into view again.
- **Ids belong to the map.** A new map forgets every spawn and the players named on the
  map before. A reload within a run (the same map over the same connection: the combat
  meter's test, repeated here) keeps the spawns.
- **HP 0** (00 8D) marks an enemy dead; its 42 36 (the corpse going) forgets it.
- **The local player's HP** (`local_health`) is the 00 8D HP and max HP (stat 7) sent
  for their id. Until a max comes (the plugin switched on mid-map), the most HP seen
  stands in. A new local id knows nothing until 00 8D says it again, which it does on
  each map, so nothing carries over from the old id.

## 🎯 The target

The overlay's "On <enemy>" section names the enemy the local player is fighting: of
those they or their summons hit directly (04 38, an amount above 0) in the last 10 s
(`Entities::kTargetMemory`) and still standing, the one with the most max HP, then
one no NPC owns, then the latest hit. In a boss fight with adds, the boss: Ludra's adds
are kind 1F, owned by the boss and at its own max HP, and one hit on one took the
target until owners counted. A mechanic aimed at a player (`Spawn::aimed_at`) never is.
- Its label is `game::npc_label` of its npc code, "#id" without one.
- It replaced "the combat meter's live boss" when Auras became a plugin. Checked on the
  dungeon capture: over the 544 seconds of its three boss fights, it was the meter's
  boss for 530 and no other enemy ever. The other 14 had none: a fight's first second,
  before the local player's own first hit; the boss already at HP 0 while the meter's
  kill grace ran (its effects are gone with it); and one stretch of 7 s in which the
  local player did not hit.

## ✨ Effects (`AuraTracker`)

The rules below that are the protocol's (instances, ends, removals, deaths, map loads)
live in `game::EffectBook` (`src/game`), which the combat meter keeps one of too; the
watch list and what the overlay shows are the tracker's own.
- Every entity's effects are kept, by instance, from 2a 38, 2b 38 and 1a 92. A 1a 92
  takes the instance of that effect that ends soonest.
  - **One that ran out stays 0.5 s for its 1a 92** (`EffectBook::kRemovalLag`). The
    server sends one as an instance runs its course, 0 to 100 ms after the end the
    book computes (median 49 ms). Pruned at the 100 ms tick before it came, its 1a 92
    took a live instance of the same effect: 6 of 1,900 removals in the dungeon
    capture, 25 of 1,816 in Ludra's.
- An instance ends at its record's stated end, brought onto the capture clock by the
  last heartbeat (One clock): a replay shows what the live overlay did. An end more than
  5 s from the stated duration is not believed, and the duration stands in.
  - A duration short of all ones (no end) but past `kForever` is clamped there: times
    1000 it wrapped to an end before the start, and the effect never showed. None
    has come on the wire.
- A death (HP 0 or 42 36) drops the entity's effects. A map load (a reload within a run
  too, since ids may change) drops every instance not put on or renewed in the 2 s
  before it.
  - **Except the items one put on oneself** (`EffectBook::kept_through`: scrolls, food,
    potions, an armour Theostone), which a death and a reload within a run leave on
    until their stated end or their 1a 92. The server sends neither for them: in the
    Ludra capture a Sorcerer died three times and wiped once, and used their 600 s
    scrolls again exactly as the first ran out. A new map still drops them, its ids
    being others.
- **A player's record lists what runs on them** (`BuffApplied::listed`), as they come
  into view or onto a new map, where no 2a 38 comes for it: each entry puts on or
  renews its instance as a 2a 38 would, ending at its stated end, and one already
  over puts on nothing. Without it, scrolls used in town were gone from the overlay
  in the dungeon.
- The snapshot carries the local player's effects (an enemy's marked hostile, their own
  and their summons' marked mine) and their own on the target.
- **Names:** `game::effect_label`: the effect's own from `effects.tsv`, else its
  skill's. One with neither that an enemy put there takes the enemy's name
  (`Entities::label`), so a boss's debuff reads "Vakron", not "Effect 16044202".
- **One entry per skill per caster** (`game::effect_key` and the caster), ending with
  the last of its instances. Doom Shield's two effects (120700271, 120700071) are one
  row; two Chanters' mantras are two.
- **Stacks** count the instances of the entry's own effect id that end. A passive sent
  again under a new number (each map sends them up to three times) is no stack, and a
  passive whose skill goes off (Punishing Benediction) gives its entry to the proc
  until that ends.
- **In the order they were put on,** permanent ones last. By time left, every renewal
  and every proc reshuffled the list: in the second capture's fights something on
  the local player changed about twice a second.
- **Sustained:** an instance of `kSustainedMaxMs` (5 s) or less that was renewed and
  has run for more than `kSustainedSpans` (2) of its own length is kept up, not
  counted down: a Chanter's mantra lasts 2.4 s and is renewed each second (529
  renewals on the local player in the second capture). The overlay draws it as a full
  bar with no time. Longer effects keep their countdown however often renewed: Fury's
  20 s says when to use it again.
- It also carries every effect seen on the local player this session (`seen`, an
  enemy's marked hostile), which is what the page's Pin, Watch and Hide switches
  offer.
  - One seen before its caster spawned (the plugin switched on mid-fight) is an ally's
    "Effect <id>" at first; once the caster spawns, it is marked hostile and takes
    their name, as the overlay's entry does. Never the other way: a caster a map load
    forgot says nothing new.
- The overlay counts down between snapshots on the wall clock, at most 250 ms past the
  snapshot's own time (`aura_display_time`): drawing only, never the plugin.

## 📌 Pins (per class)

The answer to an overlay that lists everything: about 40 different timed effects landed
on the local Templar in the dungeon capture (Judgment 86 times, Insulting Roar 73, Dodge
69), and Fury, the one they wanted to see, 6 times. Each class keeps a list of effect
keys (`auras.pinned`, the user's call, 2026-11-25): a slot each, in the order pinned,
and once the class being played has any, nothing else on the local player but enemies'
effects (`shown_auras`).
- **The class** (`AuraSnapshot::local_class`) is the local player's own: a press (01 38),
  or an effect of a class skill they put on themselves (`AuraTracker::own_class`; an
  ally's buff on them is the ally's class). Their passives come on every map load, so
  it is known before any press: 8 s into the dungeon capture, whose first press came at
  79 s. Another character drops it as they log in. Until it is known no pins apply, and
  the overlay is as it was.
- **What lights a slot** (`pinned_slots`): a running entry of the key that no enemy put
  there, but for a passive's always-on half, which names no skill (Fury's 127800001). A
  toggle names its own (a Chanter's mantra, permanent on the Chanter: 181900201, skill
  18190020, in all three captures) and lights.
  - Replayed with Fury pinned, the dungeon capture lit its slot for its four procs
    (146 to 178 s, 378 to 401, 428 to 448, 490 to 510), blocks renewing each, and at
    no other time.
  - The local player's own entry first, then the one ending last.
  - Pins ignore `min_duration_s` and `show_permanent`: picked, they show.
- **Off,** a slot shows the cooldown of the skill its key is (`pinned_cooldown`: a skill
  key's `skill_base`, as `CooldownEntry::skill` is), then Ready with `show_ready`; dim
  otherwise (an item's, an ally's, Fury between blocks). A pressed self-buff's key is
  its skill's: Warding Strike's Warding, Defiance's Tenacity, Doom Shield, Shield of
  Protection. That cooldown leaves the Cooldowns section (`shown_cooldowns`).
- **A watched pin** that is off is the alert, in its slot, and not among the alerts
  too (`missing_effects`).
- **Pin and Hide exclude each other** on the page. A class skill's key carries its
  class's digits, so one class's pins never light on another; an item's is shared, and
  the page pins it for the class being played alone.
- **One name in every state:** the seen list's, else the entry's, else the key's
  (`game::effect_key_label`). An effect seen later this session can rename a pin once,
  from its skill's name to the effect's own (Warding Strike to Warding).

## ⏳ Cooldowns (`CooldownTracker`, the local player's only; estimates, shown with `~`)

- A use (01 38) starts `listed_cooldown_ms`: `cooldowns.tsv`'s figure (questlog.gg's,
  aion2.app's for the skills it has no record of; the same at every level) less the "−Ns cooldown" specialties in play. An active
  skill's are the picks its cast code's variant digits name; a stigma's, the first n
  it unlocked, n its tens digit.
- The estimate then shortens to the quickest reuse seen this session, which is where
  the game's cooldown reduction shows. A gap with a 47 38 in it (a cut or a reset)
  teaches nothing, nor one under half the figure (a triggered Judgment).
- A 47 38 sets what is left, exactly.
- A "+1 consecutive use" specialty gives a second use: each recharges on its own, the
  skill shows only once both are cooling, and it learns nothing from its gaps.
  - A 47 38 sets the use back soonest of those cooling, never a ready one: put on the
    use left, it showed the skill cooling with a use still there. Whether the game
    cuts that use or both is unknown (AGENTS.md, Unknown or guessed).
- Another local character starts from nothing learned, as they log in.
- The figures came from aion2.app's pages until 2026-11-24, whose Brawler pages had
  lost their specialties: every Brawler stigma read as an active skill, and Phantom's,
  Full Force's and Shoulder Tackle's cuts and three skills' second use were missing.
  questlog.gg's records give them.
- Checked on the second capture: over 274 uses of 12 skills, 13 came while the
  estimate still ran, by at most 0.65 s (0.2 s on average). Each was a reuse
  quicker than any before it, which the estimate then took.
- The snapshot lists every skill used this session, ready ones too (`ready_at` past),
  at most 64: the overlay decides what to show, a "Ready" bar or a lit icon.

## 🔗 Chain skills (`ChainTracker`, the local player's only)

- 18 38 puts an offer up, 19 38 takes it down. They match by `game::skill_key`, so a
  withdrawal's variant digits need not repeat the offer's. The same skill offered again
  before its end replaces the offer.
- The server withdraws every offer, used or run out (all 437 in the second capture).
  `kMaxOffer` (5 s, the longest seen 3.08 s) drops one whose 19 38 the capture lost;
  it is no window: the server sends none, so the overlay shows no timer.
- A map load drops every offer, as does switching the plugin off.

## 🖥️ The overlay and its settings

- The plugin's master switch (`auras.plugin_enabled`, off on a fresh install) shows and
  hides the overlay: the overlay is all the plugin shows. A settings file without it
  takes 0.2.0's `auras.visible`, the overlay's own switch then, which is no longer
  written.
- Locked, the overlay hides by its own `hide_when_game_unfocused` and
  `show_in_dungeons_only` (`auras_shown`), never by the meter overlay's. It has no
  "only in combat": buffs matter between fights too.
- Ctrl+Alt+L locks every overlay at once; that is the app's, not a plugin's.
- **The hide list** (`auras.hidden`, effect keys) leaves an effect out of the overlay,
  on the local player and on the target: `shown_auras`. It is the answer to procs that
  come and go all fight (Judgment 81 times in the second capture), which no shortest
  duration tells from a buff worth seeing. A hidden effect can still be watched, not
  pinned (Pins, above).
- **Icons** (`effect_icon`): an item's effect has the item's own (a scroll, food, a
  potion: `effects.tsv`'s fourth column, packed into the sheet), any other its
  skill's. With neither, an item's effect whose icon the site does not serve shows a
  flask (`game::EffectKind::Item`), and the rest their label's initials, "?" for a
  label that is only a number (`tile_initials`).
- **Alerts first,** in both styles: low HP (`low_hp_percent`: under the threshold of a
  known max, never at HP 0, which is a death), the watch list's missing effects, the
  chain skills on offer. Then the pins' slots, the effects on the local player, their
  cooldowns, and theirs on the target.
- **A watched effect not seen this session** (after a restart, a scroll not on yet)
  has only its key to go by: `game::effect_key_label` and `effect_key_icon`, which the
  page uses too. A key is a skill's when the skill table lists that very code, as
  every skill `game::effect_key` gives is; else an effect id, named and drawn as its
  own (Speed Scroll, with the scroll). The overlay read every key as a skill: a
  scroll was "Effect 22101031" with no tile, and the monsters' Burn (12000121), by
  its ten thousands, "Dodge".
- **Bars or icons** (`auras.icons`). The pure parts are in `aura_view` and tested
  without ImGui: `shown_cooldowns` (bars: cooling, and `just_ready` for `kReadyFlash`,
  soonest first; icons: ready ones too, by skill code, so a tile keeps its place),
  `pinned_slots`, `tile_layout`, `tile_initials` for an effect with no icon,
  `format_tile_time`.
  - A slot is drawn as what it shows: an effect's bar or tile while on, a cooldown's
    while cooling or ready (a ready one glows only as it comes back: a slot stays,
    and would pulse for good), an empty bar or a dimmed icon while off, and the
    missing alert's pulsing red frame for a watched one that is off.
- **The atlas holds the icons at the size they are drawn:** a text line for bars,
  `icon_size` for icons (`OverlayWindow::set_icon_px`, rebuilt on a change). The sheet
  is 40 px; a larger tile stretches it.
