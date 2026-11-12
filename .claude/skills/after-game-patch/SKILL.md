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
   Stop at the first page that lists no skills. aion2.app lists some skills with no
   English name (the Brawler's, since 2026-11); questlog.gg's list names them, so
   take its pages into the same folder (`pageCount` in a page says how many):
   ```bash
   for p in $(seq 1 10); do curl -sSL -G -o /tmp/skills/questlog$p.json --data-urlencode "input={\"language\":\"en\",\"page\":$p}" "https://questlog.gg/aion-2/api/trpc/database.getSkills"; sleep 1; done
   ```
2. **Rebuild the table.** Run `python3 tools/gen_skill_table.py --import-pages /tmp/skills`.
   It rewrites `src/game/data/skills.tsv` (names, and each skill's icon) and
   `skill_table.inc`. A skill aion2.app lists with no English name takes
   questlog.gg's, or failing that keeps the name the TSV had.
3. **Check the diff.** Renamed rows are expected. A class whose codes stopped starting
   with its two digits (`class_of_skill`) is not, and means the codes were renumbered.
4. **Rebuild the icons**, when an icon column changed, here or in `effects.tsv` (3b) (`make test`'s `check-icons`
   fails until they are). Download every image the sheet needs, then pack them; it
   needs Pillow with WebP (`pip install pillow`):
   ```bash
   mkdir -p /tmp/icons/skills /tmp/icons/classes /tmp/icons/items
   python3 tools/gen_icon_sheet.py --urls > /tmp/icons/urls.txt
   while IFS=$'\t' read -r dest url; do [ -s "/tmp/icons/$dest" ] || curl -sSfL -o "/tmp/icons/$dest" "$url"; sleep 0.5; done < /tmp/icons/urls.txt
   python3 tools/gen_icon_sheet.py --icons /tmp/icons --preview /tmp/icons/sheet.png
   ```
   Look over `sheet.png`: the emblems first, then every skill icon. The class colours
   it prints are the emblems' own; `class_color` (`src/app/src/display_format.cpp`)
   takes them, so a changed one goes there too. Then re-run `make screenshots`.

## 3b. Refresh the effect names

An effect the meter cannot name reads "Effect 12345678" (`aion2meter-replay --unnamed`
counts a capture's). Their names come from questlog.gg's status-effect database, whose
list pages are drawn by script from one JSON list.

1. **Download the list.** It stops at 1,000 results, so take the debuffs, then the
   buffs by name in both directions (25 pages of 40 each way cover up to 2,000):
   ```bash
   mkdir -p /tmp/effects && cd /tmp/effects
   get() { curl -sSL -G -o "$1" --data-urlencode "input=$2" "https://questlog.gg/aion-2/api/trpc/database.getStatusEffects"; sleep 1; }
   for p in $(seq 1 25); do get debuff$p.json "{\"language\":\"en\",\"page\":$p,\"mainCategory\":\"debuff\"}"; done
   for o in asc desc; do for p in $(seq 1 25); do get buff-$o$p.json "{\"language\":\"en\",\"page\":$p,\"sortBy\":\"name\",\"sortType\":\"$o\",\"mainCategory\":\"buff\"}"; done; done
   ```
   A page's `pageCount` says how many pages its list has: more than 25 of debuffs, or
   more than 50 of buffs, and the list has outgrown this.
2. **Rebuild the table.** Run `python3 tools/gen_effect_table.py --import-pages /tmp/effects`.
   It rewrites `src/game/data/effects.tsv` and `effect_table.inc`, and prints how many
   effects it found of each kind (skill, item, other).
3. **Check it against a capture.** `aion2meter-replay --unnamed <capture>` should list
   fewer effects than before. The table takes no rows by hand.

## 4. Refresh the map names

A patch can add dungeons or tiers. One the meter cannot name reads "Dungeon 600001".
It counts as a dungeon all the same (`game::is_dungeon` goes by the id, 600000 and
up), so the overlays and the history work in it before this step: only the name is
missing.

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
   It rewrites `src/game/data/maps.tsv` above its `# By hand` line, and `map_table.inc`.
   It names each page that has no tier ids. Some do (the Lv.99 Awakening tiers,
   Ascension, Legion and more, at the time of writing), so the list alone is no failure.
   A dungeon that had ids and is now on it means the page's layout moved.
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
   It rewrites `src/game/data/npcs.tsv` and `npc_table.inc`, and warns when a list came
   out shorter than the site says it is (a page missing or cut off).
3. **Check the diff.** New ids are expected. A known id under another name is not, and
   means the ids were renumbered.

## 6. Refresh the cooldowns

A patch can change a cooldown or a specialty. The Auras plugin's cooldowns start from
`plugins/auras/data/cooldowns.tsv`; a stale row makes them run long or short until the
plugin learns the skill's quickest reuse again.

1. **Download the pages.** Each class's list gives its skills' pages (Elementalist and
   Gladiator have a second list page at the time of writing; add one for any class
   whose list shows 60):
   ```bash
   mkdir -p /tmp/cooldowns/list /tmp/cooldowns/pages
   for c in Gladiator Templar Assassin Ranger Sorcerer Elementalist Cleric Chanter Fighter; do for p in 1 2; do curl -sSL -o "/tmp/cooldowns/list/$c-$p.html" "https://aion2.app/db/skills?class=$c&page=$p"; sleep 1; done; done
   grep -oh 'href="/db/skills/[0-9]*"' /tmp/cooldowns/list/*.html | grep -oE '[0-9]{8}' | sort -u > /tmp/cooldowns/ids.txt
   while read -r id; do curl -sSL -o "/tmp/cooldowns/pages/$id.html" "https://aion2.app/db/skills/$id"; sleep 1; done < /tmp/cooldowns/ids.txt
   ```
2. **Rebuild the table.** Run `python3 tools/gen_cooldown_table.py --import-pages /tmp/cooldowns/pages`.
   It rewrites `plugins/auras/data/cooldowns.tsv` and `cooldown_table.inc`.
3. **Check the diff.** Changed cooldowns and cuts are expected. Every row turning
   A or S, or every cut vanishing, means the page's layout moved: look at one page's
   specialty rows and the importer's patterns.

## 7. Check and ship

1. Run `make fixtures`. Re-pin any `.expected.txt` whose change you have checked against
   the ground truth.
2. Run `make test`. If layouts moved, update `tests/wire_builder.hpp` and the protocol
   section of `AGENTS.md`.
3. Bump the patch version in `include/aion2/version.hpp`.
4. Tag `vX.Y.Z` to release (skill `release`). Installed copies pick the fix up through
   their update check.
