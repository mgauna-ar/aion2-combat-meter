# Combat Power: Agent Guidelines

The rules of the Combat Power plugin (`plugins/combat_power`, namespace
`aion2::combat_power`). The repository's [`AGENTS.md`](../../AGENTS.md) holds what
every plugin shares: Passive only, the engineering principles, the layering, the
threads, the protocol (the figure is under "Identity", Combat power) and the
self-update. What a user sees and sets is in this plugin's [`README.md`](README.md).

**What it is:** `PowerPlugin` (a `game::Plugin`) owns one part, `PowerTracker`, fed
five events on the engine thread and the capture clock: identities (`33 36`,
`45 36`), casts (`02 38`), damage (`04 38`), own values (`00 8D`) and map loads
(`21 36`). It keeps its own record of who is on the map: no fight, no side, no NPC.

The app draws it: the combat power overlay (`src/app/src/power_view.cpp`) and the
Combat Power page (`src/app/src/ui/view_power.cpp`).

**What it may use:** the protocol and game layers. Never another plugin: the auras'
`Entities::target` picks monsters, and is out of reach anyway. Switched off, the
pipeline feeds it nothing and clears it.

---

## 🔒 Passive, here too

The figure is the one the server sent on its own (`Identity::combat_power`). The
plugin must never fetch one: no character page, no armory, no API, whatever a name
would find there (the repository's Passive only, and its one network exception is
the update check). It works nothing out from gear either. A player whose record
carried no figure shows "-".

---

## 👥 Who is on the map (`PowerTracker`)

- **A player** is an identity read from `45 36`, with a name. A scan's look-alike
  (`Identity::scanned`) names no one, as in the other plugins.
- **The figure is kept as sent.** A record sent again whose end did not read (0)
  leaves the figure it gave before.
- **One row a character.** The same name and server under a new id is that player
  come back: the old row goes, and what was known of them (class, figure, being the
  target) moves to the new id. An id that passes to another character starts clean.
- **The local player** is the id of the last own identity or own values, unless that
  id is a listed player's. They are never listed, and their figure is not known:
  `33 36` does not end with it.
- **The class** comes from the skills they are seen using (`game::class_of_skill`),
  as everywhere: the record's "class" u32 is not the class.
- **Ids belong to a map** (`follow`, `on_map_load`):
  - A newer connection forgets everyone and the local player's id: entering or
    leaving a dungeon or an arena opens one. An event from an older connection is
    ignored.
  - Another map over the same connection forgets everyone but those named in the
    `kEarlyArrival` (2 s) before its `21 36`, since a map's records can come ahead of
    it.
  - The same map over the same connection is a reload, and keeps them.
- **No one leaves.** No packet read says a player went out of view, so a map's list
  only grows until the next map; `kMaxPlayers` (2,048) bounds it.

## 🎯 The target

- **A press or a hit names it:** the last other player a skill the local player
  pressed named (`SkillCast`, mode 2), or one they hit (`Damage`), for
  `kTargetMemory` (30 s) after the last of either.
- **A press on anything else lets go:** a monster, an object. A hit on a monster
  does not, since an area skill hits whatever stands beside a player.
- **Not a press:** someone else's skill, a self-cast, a passive going off (mode 0).
- **An ally is a target too:** a heal pressed on a party member names them. Other
  factions are not told apart yet (the repository's Unknowns), so neither are these.

## 📋 The list

- **A snapshot lists** the map's players strongest first (then by name), the
  `kMaxListed` (12) strongest of a crowd; `in_view` counts them all.
- **The view narrows it** (`power_layout`): `show_list` and `list_max_players` are
  the user's, and what the list leaves out is counted under it ("+N more"). A
  target in the list is marked in its row; one outside it, or with the list off,
  has a line of its own above.
- **Never empty in a crowd.** The list once went away above `list_max_players`, and
  in a town that read as the overlay losing its players a few seconds after it
  showed them. A crowd shows its strongest instead.
- **Why not the arena's map:** an arena's map id is known for one arena (61). A rule
  by id would miss every other, and the strongest few need none.

---

## 🧪 Tests

`tests/test_combat_power.cpp` (the tracker, the layout) and `tests/test_parser.cpp`
(the record's end, built by `wire::player_body` in each shape the capture showed). To check a
real capture: `aion2meter-replay <capture> --events` prints `cp N` per player.
