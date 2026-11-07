# Auras: Agent Guidelines

The rules of the Auras plugin (`plugins/auras`, namespace `aion2::auras`). The
repository's [`AGENTS.md`](../../AGENTS.md) holds what every plugin shares: Passive
only, the engineering principles, the layering, the threads, the protocol (the effect,
skill-use and cooldown opcodes are under "Effects, skill uses and cooldowns") and the
self-update. What a user sees and sets is in this plugin's [`README.md`](README.md).

**What it is:** `AuraPlugin` (a `game::Plugin`) owns three parts, fed the same events
as the combat meter on the same thread and the same capture clock:
- `Entities`: who is who, as far as the auras need it;
- `AuraTracker`: the effects on every entity in view;
- `CooldownTracker`: the local player's cooldowns, from `data/cooldowns.tsv`
  (`tools/gen_cooldown_table.py` makes `src/cooldown_table.inc` from it).

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
  cooldowns: what was learned was the other character's.
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

## 🎯 The target

The overlay's "On <enemy>" section names the enemy the local player is fighting: of
those they or their summons hit directly (04 38, an amount above 0) in the last 10 s
(`Entities::kTargetMemory`) and still standing, the one with the most max HP, the
latest hit on a tie. In a boss fight with adds, the boss. A mechanic aimed at a player
(`Spawn::aimed_at`) never is.
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
- An instance ends at its record's stated end, brought onto the capture clock by the
  last heartbeat (One clock): a replay shows what the live overlay did. An end more than
  5 s from the stated duration is not believed, and the duration stands in.
- A death (HP 0 or 42 36) drops the entity's effects. A map load (a reload within a run
  too, since ids may change) drops every instance not put on or renewed in the 2 s
  before it.
- The snapshot carries the local player's effects (an enemy's marked hostile, their own
  and their summons' marked mine) and their own on the target.
- It also carries every helpful effect seen on the local player this session, which is
  what the watch list offers. Enemies' debuffs are left out.
- Stacks: instances of one effect from one caster are one entry, ending with the last
  of them. Two Chanters' mantras are two entries.
- The overlay counts down between snapshots on the wall clock, at most 250 ms past the
  snapshot's own time (`aura_display_time`): drawing only, never the plugin.

## ⏳ Cooldowns (`CooldownTracker`, the local player's only; estimates, shown with `~`)

- A use (01 38) starts `listed_cooldown_ms`: `cooldowns.tsv`'s figure (aion2.app's,
  the same at every level) less the "−Ns cooldown" specialties in play. An active
  skill's are the picks its cast code's variant digits name; a stigma's, the first n
  it unlocked, n its tens digit.
- The estimate then shortens to the quickest reuse seen this session, which is where
  the game's cooldown reduction shows. A gap with a 47 38 in it (a cut or a reset)
  teaches nothing, nor one under half the figure (a triggered Judgment).
- A 47 38 sets what is left, exactly.
- A "+1 consecutive use" specialty gives a second use: each recharges on its own, the
  skill shows only once both are cooling, and it learns nothing from its gaps.
- Another local character starts from nothing learned.
- aion2.app's Brawler pages list no specialties (2026-11): Brawler cooldowns start
  from the listed figure, and every Brawler row reads as an active skill.
- Checked on the second capture: over 274 uses of 12 skills, 13 came while the
  estimate still ran, by at most 0.65 s (0.2 s on average). Each was a reuse
  quicker than any before it, which the estimate then took.

## 🖥️ The overlay and its settings

- The plugin's master switch (`auras.plugin_enabled`, off on a fresh install) shows and
  hides the overlay: the overlay is all the plugin shows. A settings file without it
  takes 0.2.0's `auras.visible`, the overlay's own switch then, which is no longer
  written.
- Locked, the overlay hides by its own `hide_when_game_unfocused` and
  `show_in_dungeons_only` (`auras_shown`), never by the meter overlay's. It has no
  "only in combat": buffs matter between fights too.
- Ctrl+Alt+L locks every overlay at once; that is the app's, not a plugin's.
