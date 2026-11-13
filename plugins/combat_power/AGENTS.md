# Combat Power: Agent Guidelines

The rules of the Combat Power plugin (`plugins/combat_power`, namespace
`aion2::combat_power`). The repository's [`AGENTS.md`](../../AGENTS.md) holds what
every plugin shares: Passive only, the engineering principles, the layering, the
threads, the protocol (the figure is under "Identity", Combat power) and the
self-update. What a user sees and sets is in this plugin's [`README.md`](README.md).

**What it is:** `PowerPlugin` (a `game::Plugin`) owns one part, `PowerTracker`, fed
on the engine thread and the capture clock: identities (`33 36`, `45 36`), casts
(`02 38`), damage (`04 38`), own values (`00 8D`), map loads (`21 36`), the party's roster (`02 97`), and for
distance the movement packets and effects (`2a 38`, `2b 38`). It keeps its own record
of who is on the map: no fight, no side, no NPC.

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
- **Leaving view** (`on_left_view`): a player's `47 36` (`LeftView`) takes them off
  the list, and the target with them. They walked out of range, or died and were
  taken away a second or two later, or left the map. Back in view, the game sends
  their record again, and that lists them again.
  - On the current connection only. The local player's own id gets one too (once,
    as they were moved 1.1 km): they are never listed, and stay the local player.
  - **No silence timeout.** A player standing still sends nothing and is still
    there: a town's idle crowd would empty out. A `47 36` lost to a capture gap
    leaves that player listed until the next map, as every player was before.
  - `kMaxPlayers` (2,048) still bounds the map's record.
- **Dead** (`PowerRow::dead`): another player's HP (`00 8D`, not the local
  player's own) at 0 marks them, above 0 clears it. A dead player stays listed
  until their `47 36`, but is no threat (below). A new holder of their id starts
  alive.

## 🎯 The target

- **A press or a hit names it:** the last other player a skill the local player
  pressed named (`SkillCast`, mode 2), or one they hit (`Damage`), for
  `kTargetMemory` (30 s) after the last of either.
- **A press on anything else lets go:** a monster, an object. A hit on a monster
  does not, since an area skill hits whatever stands beside a player.
- **Not a press:** someone else's skill, a self-cast, a passive going off (mode 0).
- **An ally is a target too:** a heal pressed on a party member names them. The
  target is whoever was aimed at, enemy or not.

## 📋 The list

- **A snapshot lists** the map's players strongest first (then by name), the
  `kMaxListed` (12) strongest of a crowd; `in_view` counts them all.
- **The view narrows it** (`power_layout`): `show_list` and `list_max_players` are
  the user's, and what the list leaves out is counted under it ("+N more"). A
  target in the list is marked in its row; one outside it, or with the list off,
  has a line of its own above.
- **Figures in the overlay are short** (`format_power_short`: three figures, 135k,
  98.3k, 1.23M): the row's room goes to the name and the distance. The page's tiles
  keep `format_power`'s whole figure. The bars compare the whole figures.
- **The distance has a column right of the bar,** on the window's dark ground, in
  the accent's blue (lightened) so it stands apart from names and figures, one width for every row so they line up. Drawn in muted grey
  on a class's bar it was hard to read. A window too narrow for it drops it.
- **Never empty in a crowd.** The list once went away above `list_max_players`, and
  in a town that read as the overlay losing its players a few seconds after it
  showed them. A crowd shows its strongest instead.
- **Why not the arena's map:** an arena's map id is known for one arena (61). A rule
  by id would miss every other, and the strongest few need none.

## ⚔️ Enemies (`enemies_first`)

- **Hostile** (`PowerRow::hostile`) is a player whose faction (`Identity::faction`)
  is known and is not the local player's, outside a dungeon (`game::is_dungeon`)
  and outside the local player's party (the last `02 97`, by name).
  - The dungeon rule is not caution: two of the dungeon capture's party were of the
    other faction. The roster rule covers a mixed party anywhere else.
  - The local player's faction is their own record's, kept across maps and ids: it
    is the character's. Unknown until one is seen, and then no one is hostile.
- **Known on sight.** The faction comes with the record that brings a player into
  view, so they are marked before they act. That is the feature: do not make it
  wait on a hit or a cast.
- **A threat** is a hostile player within `kEnemyRange` (200 m), or at no known
  distance. Threats lead both lists, ahead of the cut at `kMaxListed`, so a crowd
  of stronger or nearer allies never hides one; `hostile_in_view` counts them.
  - **A dead enemy is no threat:** marked still, they lead nothing, and the view
    dims their row until they leave.
  - Why a range, now that players leave the list: the local player's position can
    be well out of date (below), and an enemy at the edge of view is not about to
    engage. Before `47 36` was read it was what kept an enemy passed ten minutes
    ago off the top.
  - It leans on the local player's position, stale as that can be (below): an
    enemy may keep or lose the lead a little late. The red name does not depend
    on it.
- **Only the faction.** An arena's opponent of the local player's own faction is
  not hostile, nor a battlefield's team: no rule for them yet (the repository's
  Unknowns).
- **The view** (`power_layout`) keeps the snapshot's order, or with `enemies_first`
  off sorts the rows back into one order; the red name is drawn either way.

## 📏 Distance (`sort_by_distance`)

- **A player's position** is their last: their record's (`Identity::position`), a
  movement packet's (`Position`), or an effect of theirs (`BuffApplied`, which
  carries its caster's). Only listed players and the local player are placed.
- **The local player's is never streamed.** The server does not send them their own
  steps. It is known from their own record on each map, from every effect they
  cast, and from a skill that moves them (`3c 37`). In the arena capture's fight
  that was 202 effects in 133 s, a median 0.1 s apart and 10.9 s at worst; walking
  with no skill, nothing.
- **So a distance is from where they last acted,** and the snapshot says how long
  ago that was (`local_position_age`). The view dims distances older than
  `kDistanceStaleAfter` (5 s). Never extrapolate the local player's position: there
  is nothing to extrapolate from.
- **Metres:** the game's units are centimetres. Height counts.
- **Two lists, one snapshot:** `players` (strongest first) and `nearest` (nearest
  first, those at no known distance last, then by strength), `kMaxListed` each.
  The view picks by the user's setting, so no setting crosses to the engine thread.
- **No local position, no `nearest`:** the view then lists the strongest, as with
  the sort off, rather than an order it cannot stand behind.
- **Forgotten with the map:** the local player's position goes with a new
  connection, a new id of theirs, or another map (but for one said in the
  `kEarlyArrival` before its load); a player's goes with the player, and when their
  id passes to someone else.
- **The pressed cast's position is not used.** A press's tail (`02 38`, mode 2) holds
  a heading and a point that was often the target's last position to the unit, and
  at other times some metres off it: an aim point, perhaps. Not read until it is
  understood.

---

## 🧪 Tests

`tests/test_combat_power.cpp` (the tracker, the layout) and `tests/test_parser.cpp`
(the record's end, built by `wire::player_body` in each shape the capture showed). To check a
real capture: `aion2meter-replay <capture> --events` prints `cp N` per player.
