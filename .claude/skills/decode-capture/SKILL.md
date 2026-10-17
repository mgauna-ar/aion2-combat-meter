---
name: decode-capture
description: Check and fix the AION 2 decoder against a real capture. Use when a .pcap/.pcapng recorded from the game is available, when replay shows parse failures, resyncs or wrong numbers, or when filling in unknowns (class codes, the damage scale, npc codes, boss_min_max_hp) - including any work on src/protocol/src/packet_parser.cpp, src/protocol/include/protocol/opcodes.hpp or src/meter/src/class_codes.cpp.
---

# Decode a real capture

The decoder was written from the community's protocol notes and checked against one real
capture (AGENTS.md, "Protocol", lists what that settled and what is still open). A real
capture is the ground truth. Captures hold character names: keep
them in `tests/fixtures/local/` (gitignored) and never paste names into commits,
issues or logs. `--anonymize` prints `#id` instead.

## 1. Get a capture and the ground truth

The user records it on the Windows PC. See README, "Recording a capture": Wireshark on
the adapter and on loopback, filter `tcp port 13328`, started before login or a
teleport. Ask for the facts that make it checkable:
- the character's name and class;
- a few hit numbers seen on screen;
- the boss's name and roughly when it died;
- the game's own Combat Analysis window (Ctrl+X) for that fight, ideally a screenshot of
  its Detailed Mode: time, DPS, total damage, Total Heal, and per skill HIT, ACC, CRI,
  PERF, DOUB, FRNT, BACK, MULT, BLOC, IMMU, ENDR and RSTO.

Put it in `tests/fixtures/local/<name>.pcapng`.

## 2. Framing first

```bash
make replay
```

```bash
./aion2meter-replay tests/fixtures/local/<name>.pcapng --opcodes --anonymize
```

**What healthy looks like:**
- one game flow;
- `heartbeats ≈ 20/s`;
- `resyncs 0`;
- `bundles (0 bad)`;
- `server clock` within a second of the capture.

**If it doesn't:**
- **No game flow:** check the port (`--port`), or whether a relay hid it (the flow list
  shows what was decoded).
- **Many resyncs or bad bundles:** the framing rule is wrong (`frame_decoder.cpp`).
  Dump with `--dump all --limit 5` and check the bytes around a failure against the rule
  that a frame takes `len + varint bytes − 4` bytes on the wire.

## 3. Then the bodies

```bash
./aion2meter-replay tests/fixtures/local/<name>.pcapng --events --anonymize
```

**Check each layout against the ground truth:**
- **Own identity:** the own identity event carries the character's id. Its `class` code
  goes into `src/meter/src/class_codes.cpp` (an entry per class seen).
- **Damage:** the damage events for that id should show the hit numbers noted in step 1.
  - If the amounts are off by a field, look at `read_damage_record`: the "optional 00
    pad" and the scalar varint are the likeliest culprits.
  - `--dump 0438 --limit 10` shows raw bodies next to `--events` output by time.
- **Ids across maps:** around each `map` event, note the party's ids before and after,
  and whether their identities come after it. Does a player keep their id? Does an old
  id come back as someone else, or as an NPC? AGENTS.md's Sides rules assume ids can
  move and be reused; confirm that, or simplify them.
- **Map ids:** a `map` event prints the name `src/meter/data/maps.tsv` gives its id.
  - Every map event starts a new visit. Ask the user to run one dungeon twice,
    straight back in, and to use a teleport inside it: the re-entry should show a
    `map` event with the same id (note its load count), and the teleport none. A
    teleport that sends one splits a run (AGENTS.md, Unknowns).
  - Entering a dungeon should print that dungeon and the tier you picked. If it prints
    the wrong tier, or no name, then aion2.app's tier ids are not the wire's ids. Note
    it in AGENTS.md, and find what the ids do follow before trusting the table.
  - Open-world zones have no public ids. Add a row under the TSV's `# By hand` line for
    each zone the capture shows (the user knows where they were), then run
    `python3 tools/gen_map_table.py`. metabot.gg's zone pages give a zone id
    (`?focus=zone:622`). If an open-world id matches the zone the user stood in, those
    are map ids after all: note it in AGENTS.md.
- **Spawn:** a boss spawn's `hp` should be its max HP. Its `npc` field prints the
  name `src/meter/data/npcs.tsv` gives it.
  - The name should be the boss the user fought. Mobs were checked in the first
    capture; no boss has been yet.
  - If the name is wrong, or missing for a mob the site lists, search the spawn body
    for the right id from the TSV (as a u32 LE, or as a varint) and move `read_spawn`
    there.
- **Failures:** `parsed ...: N failed` names each opcode that failed. Fix those before
  trusting any total.

**Check damage against the server's HP.** `--events` prints an `hp` line (00 8D) after
each hit on a mob: the target's exact HP. Each drop should match the damage records
since the last line. In the first capture it was a steady multiple of the damage instead
(4.5 for one player), and extra-hit values never counted. Recheck this on every capture.
Comparing whole fights works too: a mob that dies took its max HP.

**Check the breakdowns against the game's window.** `aion2meter-replay --skills` prints
each player's skills, healing and damage taken. For the local player, its skill rows
should match the Ctrl+X window's: names, hits, and the CRI, PERF, DOUB, BACK and FRNT
shares. Then look for what the meter cannot show yet, since the game's window counts it:
- **ACC (misses):** a damage record with amount 0, or a damage type other than 3 (crit).
- **MULT (multi-hit):** the switch-0x20 tail. Do its counts match MULT?
- **BLOC, IMMU, ENDR, RSTO:** the target's defensive outcomes, likely in the damage
  type or the block's unknown bytes.
Whatever is found goes into `parse_damage`, `HitCounts`, the tables and AGENTS.md.

`--unknown` dumps every opcode the parser does not read.

## 4. Pin the result

When `--fights` matches the ground truth:

```bash
./aion2meter-replay tests/fixtures/local/<name>.pcapng --fights --quiet > tests/fixtures/local/<name>.expected.txt
```

```bash
make fixtures
```

**For every layout fix:**
- add a committed test built in `tests/wire_builder.hpp`, with invented names and the
  real byte shape;
- update the protocol section of `AGENTS.md`;
- if the made-up session's numbers move, run `make golden` and look at the diff before
  committing it.

Calibrate `meter.boss_min_max_hp` from the max HP of real bosses and trash, and
document the new default in README's Configuration.
