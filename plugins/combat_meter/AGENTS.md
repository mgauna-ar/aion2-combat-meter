# Combat Meter: Agent Guidelines

The rules of the Combat Meter plugin (`plugins/combat_meter`, namespace `aion2::meter`).
The repository's [`AGENTS.md`](../../AGENTS.md) holds what every plugin shares: Passive
only, the engineering principles, the layering, the threads, the protocol and the
self-update. What a user sees and sets is in this plugin's [`README.md`](README.md).

**What it is:** `EncounterEngine` (a `game::Plugin`) turns parsed events into fights:
who is who (`CombatantRegistry`), each fight's damage, healing and damage taken per
player and skill, its boss, and the saved history (`HistoryStore`, `fight_json`,
`pull_grouping`). The app draws it: the meter overlay (`src/app/src/overlay_view.cpp`)
and the Combat Meter page (`src/app/src/ui/view_meter.cpp`).

**What it may use:** the protocol layer and the game data (`src/game`: skill, npc and
map names, classes). Never the Auras plugin: the two do not know of each other. When it
is switched off the pipeline feeds it nothing; switching it off ends its live fight
(`EndReason::Manual`), which is saved like any other.

---

## ⚔️ Meter rules


- **Sides** (`CombatantRegistry`):
  - Identity packets make players. Spawns make NPCs, or player objects for summon and
    effect kinds.
  - A summon's side is the player side, whoever owns it, and its spawn makes the owner
    a player before any name arrives.
  - Ids the meter was never told about get their side from the hits they trade: PvE
    damage only crosses sides.
  - Hits between two unknown ids are held (up to 4096) and settled when either end
    becomes known.
  - A hit with a spawned NPC settles the other end as a player, over any earlier
    inference: mobs do not fight mobs, and 04 38 also carries heals between players.
  - 00 8D's own values make the local player, shown as "You" until 33 36 names them.
  - With no anchor at all, an id in the folded range is guessed to be an enemy.
  - **Ids belong to the map, and the server hands them out again.** A player's id on
    one map can be another player's, or an NPC's, on the next. So rows belong to
    characters (name@server), not ids:
    - an id is one character at a time: the last identity for it says which;
    - a hit goes to its id's row for the character the id was then, and the summary
      joins a character's rows. An id named after it hit joins its character. Ids are
      never joined to each other, so two characters never share a row;
    - a map load, or a reload within a run, leaves players stale rather than
      dropping them, since their names may have come before it. A spawn on a stale id makes it the spawn, and a character named on a
      new id leaves its stale one. On the map they were named on, two ids can share a
      name;
    - the local player's name follows 00 8D's own values to their new id.
  - A scanned identity names no one (Protocol, Identity). One that took a stale
    party member's name, after a load, made the meter forget the member's id, and
    the member fell out of the party.
  - A map load drops NPCs, summons and inferred sides; a reload within a run keeps
    them.
- **Classes:** from the skill codes a player uses (their summons' count too), unless an
  identity packet's class code names one.
- **Party:** with a roster, only its names count, and the local player.
  - With none, it depends on the map. In an instance (`MapLoad::is_instance`) every
    player counts: only the group gets in. In the open world, or on a map the meter
    did not see load, only the local player does, so a crowd at a world boss stays
    out.
  - Non-party hits still lower the boss's HP estimate, but never start or extend a
    fight.
- **Healing and damage taken** count only during a fight, and never start or extend
  one.
  - Healing: heal ticks to anyone on the player side (effect 30's included), and 04 38
    records from a player (not a summon) to a party member. A record to anyone else
    may be a PvP hit. It is the total, like the game's: no overheal split.
  - Damage taken: an enemy's hits and ticks on a player. A summon's HP is its own.
  - Both are booked for party members by the roster and the local player; with no
    roster, for players already in the fight, so strangers nearby stay out.
- **Skills:** each player's damage, healing and damage taken are kept per skill, keyed
  by `skill_key()`: variants of one skill share a row.
- **Hits** (`HitCounts`, the game's columns): direct hits, and of them crits (CRI),
  perfect (PERF), double (DOUB), back (BACK), front (FRNT), multi-hits (MULT: a
  record with the 0x20 tail, one hit however many strikes) and blocked (BLOC).
  History files before the block was known saved it as `"parry"`, which still loads.
- **Aimed mechanics:** a spawn aimed at a player (Spawn::aimed_at), such as a trap the
  party breaks, takes no damage that counts: hits on it are neither damage nor hits,
  and never start or extend a fight, as in the game's window. Its hits on players are
  damage taken.
- **Fight start:** a direct hit that lands, from the player side on an enemy. A DoT tick
  never starts a fight.
- **Fight end:** the boss's death (00 8D HP 0 or 42 36, after `kill_grace_s`, unless a
  bigger boss was hit during the grace), a map load (the same map's included when it
  comes over a new connection: running a dungeon again, straight back in; not over the
  same one, a teleport or a revive within the run), idle time with no party hit (`idle_timeout_s`, or
  `boss_idle_timeout_s` while a boss stands), no traffic (`capture_lost_s`), a wipe
  (when enabled) or by hand.
  - A dead player stays dead for the wipe check while their DoTs and summons keep
    hitting; their own direct hit, or 00 8D HP above 0, revives them.
  - Duration runs from the first hit to the last, floored at 1 s for DPS.
- **Boss:** the damaged enemy with the most max HP. It is a boss if that max HP is at
  least `boss_min_max_hp`.
  - Calibrated on one dungeon: its bosses had 5.22M–9.86M max HP, its trash
    203k–435k. Undamaged adds and mechanics can share the boss's max HP; they never
    win, having taken no damage.
  - Its name comes from its spawn's npc code (`npc_label`): "NPC #code" when the table
    does not list it, "#id" when it was never seen spawning. The label is saved with
    the fight, so a fight keeps the name it had.
  - Its HP is the server's figure (00 8D) once one arrives. Before that, it is the spawn
    HP minus all damage seen since the spawn, shown with `~`.
  - A mob that spawned before the meter started has no max HP; the most 00 8D HP seen
    stands in for it.
  - **Partial:** a killed boss that lost less than `partial_damage_ratio` of its HP (by
    00 8D) with a hit seen since the HP figure before marks the fight partial. It is
    HP against HP: damage and HP are not in the same units. A boss that leaves view
    and spawns again keeps the loss counted so far; its spawn HP is one more figure.
- **History:**
  - What is saved: boss fights, and fights of at least `min_saved_duration_s`. With
    `save_only_dungeons`, only in a dungeon (`is_dungeon`: a row above maps.tsv's
    `# By hand` line, so not map 0, an open-world zone, or a map added by hand).
  - Each fight is one JSON file (`"v":2`; version 1 files still load, without the
    breakdowns), written atomically, plus a line appended to `index.jsonl`.
  - Fights are grouped by map visit (`visit_ts`): every map load starts one but a
    reload within a run (the same map over the same connection), so each run of a
    dungeon is its own. So does the first fight after the meter starts.
  - A cut-off index line is skipped. A missing index is rebuilt from the files.
  - Pruning keeps `history.max_fights`, in batches of 50.
  - Clearing (the app window's trash button) deletes the fight records and the index,
    on the engine thread; other files in the folder stay.
