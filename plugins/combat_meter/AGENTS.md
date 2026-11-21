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
(`EndReason::Manual`), which is saved like any other. Switched back on, it is handed
the last map load, own 33 36 and party roster it missed, in the order they came
(`Pipeline::keep_for_meter`), so a map loaded meanwhile is a map change to it, as it
would have been; the rest it learns as a meter started mid-visit does. Quitting the
app, or restarting it into an update, ends the live fight the same way and saves it
(`AppRuntime::run`, once stopped).

---

## ⚔️ Meter rules


- **Sides** (`CombatantRegistry`):
  - Identity packets make players. Spawns make NPCs, or player objects for summon and
    effect kinds.
  - A summon's side is the player side, whoever owns it, and its spawn makes the owner
    a player before any name arrives.
    - **Unless the owner's HP is no player's** (`kPlayerHpCeiling`, 100,000): a boss
      owns kind 1F adds too. In the Ludra capture the meter never saw the boss spawn
      (it started mid-run), the boss's first adds made it a player, and every hit on
      it after that counted for nothing: each pull ended on idle at its first add
      phase, 82% left, while the party fought on to the wipe. An id never named or
      seen spawning whose HP reaches 100,000 is never made a player, and one made a
      player before its HP came goes back to the enemy side when it does. Players
      had at most 40,703 there.
    - **A summon an NPC owns is an enemy of its own** (`owned_by_npc`): its owner
      spawned as an NPC, or has HP no player has, or was put on the enemy side.
      `resolve` stops at it, so a hit on one of Ludra's adds is the add's damage
      taken, not the boss's, its death is not the boss's kill, and the party's
      effects on it are effects on an enemy.
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
    - **nor a row's id** (`CombatantTotals::id`, which the page's selection, the
      timeline's rows and the deaths go by). A row's id is the lowest of its ids,
      unless a row that came into the fight first has it: a reload within a run
      keeps the fight going, and the server can hand one player's id to another
      there. The later row takes its next id, or one no row has, counted down from
      the top of the range;
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
  - With none, it depends on the map. In a dungeon (`game::is_dungeon`: the map's id,
    whether or not the meter can name it) every player counts: only the group gets in. In the open world, or on a map the meter
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
- **Max hit:** a skill's biggest direct hit, and a player's their biggest own direct
  hit: a DoT tick, or their summon's hit, is not theirs (a summon's skill row keeps
  its own max).
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
  (`wipe_detection`, off: see below) or by hand (the hotkey, switching the plugin off,
  quitting the app).
  - A wipe is every party member in the fight dead at once. A player is dead from
    00 8D HP 0 until HP above 0, or their own direct hit more than `kInFlight` (1 s)
    after (one in flight as they died proves nothing), and stays dead while their DoTs
    and summons keep hitting.
  - So it stays off: in the second capture's last boss fight the whole party was at HP
    0 at once for 1.2 s (four died together, the fifth already down), got up 1 to 9 s
    later and killed the boss. By this rule that was a wipe, cutting the fight in two
    at 2:41; in game it was not.
  - Duration runs from the first hit to the last, floored at 1 s for DPS.
- **Boss:** the damaged enemy with the most max HP. It is a boss if that max HP is at
  least `boss_min_max_hp`.
  - Calibrated on one dungeon: its bosses had 5.22M–9.86M max HP, its trash
    203k–435k. Undamaged adds and mechanics can share the boss's max HP; they never
    win, having taken no damage. A damaged add its boss owns (Ludra's carry the
    boss's max HP) loses to its owner at the same max HP, whatever damage either took.
  - Its name comes from its spawn's npc code (`npc_label`): "NPC #code" when the table
    does not list it, "#id" when it was never seen spawning. The label is saved with
    the fight, so a fight keeps the name it had.
  - Its HP is the server's figure (00 8D) once one arrives. Before that, it is the spawn
    HP minus all damage seen since the spawn, shown with `~`.
  - **A finished fight keeps the HP the party left it at:** its HP as the last hit
    landed, and the server's figures up to `kHpSettle` (0.5 s) after a hit. A boss
    left standing by a wipe resets to full HP once no one is left, before the fight
    times out; the history's "Wipe 23%" and "23% left" would otherwise always read
    100%. The live fight shows the HP now.
    - **Full HP is never what a hit left** (`back_to_full`): the Ludra capture's
      second wipe sent the reset (a 00 8D at max HP) in the same 100 ms as the party's
      last hits, in flight as the last of them died. So a figure at max HP, or a hit
      landing on a boss at it, leaves the kept HP as it was. That wipe's fight kept
      21.8%, the server's last figure before the reset; the first wipe's reset came
      30 s later, and kept 65.2%.
    - The boss's timeline line skips a reset in the same second as a lower figure,
      so it ends where the party left it.
    - A new spawn at full HP after the last hit is caught the same way, by
      `kHpSettle`; no capture has shown one.
  - **A kill comes down to 0** (`fell_to_it`): a boss-sized enemy's HP 0 or 42 36 is
    its death only when the server's last HP figure for it before (`exact_hp`: its
    00 8D, else its spawn's) was at most `kFellPercent` (25%) of its max. Else it
    *vanished* (`Actor::vanished`): dead to the registry, never killed, so the fight
    goes on to its idle end and keeps the HP the party left it at. The HP 0 takes no
    place on the boss's line or in that HP either. HP above 0 later brings it back.
    - Every boss kill in the captures came down to 10% or less first (the second
      capture's three), then HP 0, then the 42 36 about 4 s on.
    - Things with the boss's own max HP go without that, by the hundred: the second
      capture's mechanics (9.86M, kind 1C, 74 from full straight to HP 0, 72 with only
      a 42 36), and Ludra's adds, traps and mechanics (85.6M, kinds 14, 17, 1C, 1F).
      One the party hits harder than the boss is the fight's boss, and its going
      was its kill.
    - Ludra with two parties on the boss showed every wipe as a kill, the user said,
      with no capture of it: a boss despawning at its reset (42 36), or an HP of 0 for
      it, reads that way, and so does one of those things being the boss. Each is
      caught here. A wipe at 25% or less that resets the boss that way still reads as
      a kill.
    - Small enemies (under `boss_min_max_hp`) die by a 42 36 alone, no figure ever
      sent: 135 of the second capture's trash. Their deaths stay deaths.
  - A mob that spawned before the meter started has no max HP; the most 00 8D HP seen
    stands in for it.
  - **Partial:** a killed boss that lost less than `partial_damage_ratio` of its HP (by
    00 8D) with a hit seen since the HP figure before marks the fight partial. It is
    HP against HP: damage and HP are not in the same units. A boss that leaves view
    and spawns again keeps the loss counted so far; its spawn HP is one more figure.
- **Timeline** (`FightDetail`, beside the totals: `FightSummary::detail` is a
  `shared_ptr`, so the snapshot and the recent fights copy a pointer):
  - each row's damage (direct, ticks and its summons'), healing (to the healer) and
    damage taken (to the victim) per second of the fight, kept per row like the
    totals and joined by character with them. Up to an hour (`kTimelineMaxSeconds`);
    the totals count past it;
  - `seconds` can run past the fight's duration, which ends at the party's last hit:
    hits taken, deaths and the boss's HP 0 can come after it;
  - the boss's line is the server's own HP figures (00 8D, and its spawn's) for the
    summary's boss, in permille of its max HP, carried forward. The figure the meter
    had before the fight's first stands at second 0. Never damage: other units.
- **Active time:** the gaps of `kActiveGap` (3 s) or less between a player's own direct
  hits, by id, so an id named late still bridges its gap. Summons and ticks never
  count. In the second capture's last boss fight 99% of every player's gaps were 2.7 s
  or less, and it came to 55–85%.
- **Casts** (02 38, Protocol in the root AGENTS.md): a player's presses, per skill
  (`skill_key`, so variants share a row, as on the Damage tab).
  - Only mode 2 (a press) of a class skill: no passive going off, chain follow-up or
    mantra pulse, and no NPC's skill.
  - Only the player's own: a summon's skills are its AI's, not a press of its owner's.
  - Booked like healing (`counts_in_fight`): a roster member's and the local player's
    at once; with no roster, a player's once they hit, their presses since the fight
    began going in with that first hit (`Fight::pending_casts`). Strangers' never.
  - A press adds no one to a fight: presses are kept apart from the rows
    (`Fight::casts`) and join only those a hit, a heal or damage taken made. A member
    who only pressed a buff during a short pull has no row, as before casts.
  - A press never starts or extends a fight. The opening cast comes 0.1–0.5 s before
    the hit that starts one, so presses of the last `kCastLead` (2 s) are held while
    no fight runs, and those still within it when one starts count in it; its first
    cast can read below 0 s.
  - Each skill keeps its count and its first and last press: casts a minute run over
    the fight's duration, and the average gap between presses over first to last.
  - Checked on the second capture: over its five fights, the local player's casts
    matched their own 01 38 skill by skill for 97.2%. The rest the server sends in
    another mode (Protocol).
- **Deaths:**
  - a party member's HP 0 (00 8D) during a fight: players get no 42 36. Not a summon's,
    and not a stranger's (`counts_in_fight`). Repeated zeros while down are one death;
  - up again at HP above 0, or at their own direct hit more than `kInFlight` (1 s)
    after: one landed 55 ms after, already in flight, 3.9 s before their HP came back;
  - a death is the character's (`same_player`): on an id the server handed on within
    the fight, someone else's death, or their being up, is not this one's. By id only
    while either is unnamed;
  - the recap: the newest 10 of the last 16 hits taken and heals received, within 12 s,
    booked under the same filters as Damage Taken and Healing, on the character who
    died: what landed on the id's last owner is left out. The killing blow is the
    last damage in it;
  - a death adds no row: it keeps its own name and class;
  - checked on the second capture: 19 deaths in the last boss fight, every one up again
    by its HP, 3 to 17 s later. The local player's killing record carried 0 damage, which
    books nothing, so that death shows no killing blow.
- **Effect uptime** (`EffectUptime`, with its own `game::EffectBook`, fed every effect
  record, fight or not, so a fight knows what is already running as it starts):
  - a span per instance on a party member (by the roster or the map, not a summon) or,
    from a party member or their summon, on an enemy. A renewal while it runs goes on
    with the span; a 1a 92, a death or a map load (a reload too) ends it, but for
    the items a player put on themself (`EffectBook::kept_through`), which a death and
    a reload within a run leave on. Permanent effects (passives) are left out;
  - rows per side (on the party from anyone else, on the party from an enemy, on an
    enemy), target (character, or enemy id), `effect_key` and caster; stacks count
    once, clamped to the fight. Only enemies the party damaged keep rows;
  - a party-wide buff is one caster's effect on two or more party members: merged over
    all of them, bridging gaps under 1 s, it is a band on the timeline;
  - at most 16,384 spans a fight. Switching the meter off forgets every effect;
  - checked on the second capture's last boss fight: 151 rows and 42 bands, the party's
    best-kept debuff on the boss up 87.5%.
  - **Consumables** (the tab's fourth view, `view_effects.cpp`, from those same rows:
    nothing more is saved): a row per fight row, and per `game::item_effect` (by the
    id's first two digits: 20 potion, 21, 27, 28 food, 22 scroll). Food, scrolls and
    potions are the target's, on the party side. Theostones (30) are gear, not
    consumables (the user), and are left out: their effects stay in the other three
    views, and a damage Theostone's hit is a skill of its wearer's (AGENTS.md, Skill
    codes). Food or a scroll up less than 95% of the fight shows its share.
  - checked on the Ludra capture: each of the five players' scrolls and food were
    there. A Sorcerer who died three times used their 600 s scrolls again exactly as
    the first ran out, through a wipe and its reload. Before items were kept through a
    death and a reload, they read 65% and 9% in its two pulls.
  - **What ran before the map came** is known from the players' records
    (`BuffApplied::listed`, root AGENTS.md, Identity): no 2a 38 comes for a scroll
    used in town, and before they were read, one used in town and again mid-fight
    read about half the fight. With them, every scroll of the Ludra capture's two
    pulls kept up all fight reads 99.5% or more (before, 9% to 100%); those still lower
    ran out in it.
- **History:**
  - What is saved: boss fights, and fights of at least `min_saved_duration_s`. With
    `save_only_dungeons`, only in a dungeon (`game::is_dungeon`: a map id of 600000
    or more, named in maps.tsv or not; so not map 0 or an open-world zone).
  - Each fight is one JSON file (`"v":2`; version 1 files still load, without the
    breakdowns), written atomically, plus a line appended to `index.jsonl`.
  - The detail (`timeline`, `deaths`, `effects`), `active_s` and each row's `casts`
    (`[code, casts, first ms, last ms]`, one line each) are optional fields of
    version 2: an older build ignores them, where a new version would make it skip the
    record. A record without the detail reads with none, and one without `casts` with
    `has_casts` false, so its Casts tab says it was saved before. A series is one line
    (`[1,0,2]`, the JSON printer's rule for an array of numbers), its trailing zeros
    cut; reading bounds every list.
  - Fights are grouped by map visit (`visit_ts`): every map load starts one but a
    reload within a run (the same map over the same connection), so each run of a
    dungeon is its own. So does the first fight after the meter starts.
  - A cut-off index line is skipped. A missing index is rebuilt from the files.
  - Pruning keeps `history.max_fights`, in batches of 50.
  - Clearing (the app window's Clear button) deletes the fight records and the index,
    on the engine thread; other files in the folder stay.
