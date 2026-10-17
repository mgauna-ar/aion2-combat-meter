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

## 4. Refresh the map names

A patch can add dungeons or tiers. Maps the meter cannot name read "Map 600001".

1. **Download the pages.** The list pages give each dungeon's page, and each dungeon's
   page gives its tier ids:
   ```bash
   mkdir -p /tmp/dungeons/list /tmp/dungeons/pages
   for p in 1 2 3; do curl -sSL -o /tmp/dungeons/list/page$p.html "https://aion2.app/db/dungeons?page=$p"; sleep 1; done
   grep -oh 'href="/db/dungeons/[^"?#]*"' /tmp/dungeons/list/*.html | sed 's|href="/db/dungeons/||; s|"$||' | sort -u > /tmp/dungeons/slugs.txt
   while read -r s; do curl -sSL -o "/tmp/dungeons/pages/$s.html" "https://aion2.app/db/dungeons/$s"; sleep 1; done < /tmp/dungeons/slugs.txt
   ```
   Add list pages if the site shows more than three.
2. **Rebuild the table.** Run `python3 tools/gen_map_table.py --import-pages /tmp/dungeons/pages`.
   It rewrites `src/meter/data/maps.tsv` above its `# By hand` line, and `map_table.inc`.
3. **Check the diff.** New tiers are expected. An id that moved to another dungeon is
   not, and means the ids were renumbered.

## 5. Refresh the NPC names

A patch can add monsters and NPCs. Ones the meter cannot name read "NPC #2345678".

1. **Download the lists.** The monster list has about 126 pages and the NPC list about
   62, at the time of writing. The site's pager shows the count:
   ```bash
   mkdir -p /tmp/npcs
   for p in $(seq 1 126); do curl -sSL -o /tmp/npcs/monsters-$p.html "https://aion2.app/db/monsters?page=$p"; sleep 1; done
   for p in $(seq 1 62); do curl -sSL -o /tmp/npcs/npcs-$p.html "https://aion2.app/db/npcs?page=$p"; sleep 1; done
   ```
2. **Rebuild the table.** Run `python3 tools/gen_npc_table.py --import-pages /tmp/npcs`.
   It rewrites `src/meter/data/npcs.tsv` and `npc_table.inc`, and warns when a list came
   out shorter than the site says it is (a page missing or cut off).
3. **Check the diff.** New ids are expected. A known id under another name is not, and
   means the ids were renumbered.

## 6. Check and ship

1. Run `make fixtures`. Re-pin any `.expected.txt` whose change you have checked against
   the ground truth.
2. Run `make test`. If layouts moved, update `tests/wire_builder.hpp` and the protocol
   section of `AGENTS.md`.
3. Bump the patch version in `include/aion2/version.hpp`.
4. Tag `vX.Y.Z` to release (skill `release`). Installed copies pick the fix up through
   their update check.
