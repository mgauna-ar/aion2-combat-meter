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
- the game's own personal DPS meter reading for that fight, if possible.

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
- **Spawn:** a boss spawn's `hp` should be its max HP. The `npc` field should be stable
  across sightings of the same boss. If it is not, the u24 after the mask is the wrong
  place for the npc code.
- **Failures:** `parsed ...: N failed` names each opcode that failed. Fix those before
  trusting any total.

**Check damage against the server's HP.** `--events` prints an `hp` line (00 8D) after
each hit on a mob: the target's exact HP. Each drop should match the damage records
since the last line. In the first capture it was a steady multiple of the damage instead
(4.5 for one player), and extra-hit values never counted. Recheck this on every capture.
Comparing whole fights works too: a mob that dies took its max HP.

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
