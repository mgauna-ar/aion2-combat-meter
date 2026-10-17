---
name: after-game-patch
description: Repair the AION 2 meter after a game patch. Use when the game has updated and the meter shows nothing, wrong numbers, many parse failures or resyncs, or when opcodes or packet layouts may have moved - including any change to src/protocol/include/protocol/opcodes.hpp.
---

# After a game patch

The meter knows the game only through its network protocol. A patch can renumber
opcodes, change a body's layout, or change the port. Nothing else in the meter depends
on the game version.

## 1. Re-record

Ask the user for a fresh capture of the patched client: login or a teleport, then a
fight with known hit numbers (README, "Recording a capture"). Put it in
`tests/fixtures/local/`.

## 2. Find what moved

```bash
./aion2meter-replay tests/fixtures/local/<new>.pcapng --opcodes --anonymize
```

**Read the result:**
- **No game flow, or zero heartbeats:**
  - The port changed if a flow on another port shows heartbeats: check with `--port`.
  - The heartbeat itself changed if no flow does. That is the framing's anchor, so start
    in `frame_decoder.cpp`.
- **Healthy framing but a known opcode is missing:**
  - Compare the counts and body sizes with an older capture's `--opcodes`.
  - A renumbered opcode keeps its count and size profile. Damage is the most frequent
    after the heartbeat.
  - Fix it in `opcodes.hpp`, the only place opcodes are spelled.
- **An opcode is present but `failed`:** its layout changed. Dump it (`--dump`) and
  follow `.claude/skills/decode-capture` step 3.

## 3. Refresh the skill names

A patch can add or rename skills. Rows the meter cannot name read "Skill 12345678".

1. **Download the list.** Fetch aion2.app's skill list pages into an empty folder:
   ```bash
   mkdir -p /tmp/skills && for p in $(seq 1 12); do curl -sSL -o /tmp/skills/page$p.html "https://aion2.app/db/skills?page=$p"; sleep 1; done
   ```
   Stop at the first page that lists no skills.
2. **Rebuild the table.** Run `python3 tools/gen_skill_table.py --import-pages /tmp/skills`.
   It rewrites `src/meter/data/skills.tsv` and `skill_table.inc`.
3. **Check the diff.** Renamed rows are expected. A class whose codes stopped starting
   with its two digits (`class_of_skill`) is not, and means the codes were renumbered.

## 4. Check and ship

1. Run `make fixtures`. Re-pin any `.expected.txt` whose change you have checked against
   the ground truth.
2. Run `make test`. If layouts moved, update `tests/wire_builder.hpp` and the protocol
   section of `AGENTS.md`.
3. Bump the patch version in `include/aion2/version.hpp`.
4. Tag `vX.Y.Z` to release.
