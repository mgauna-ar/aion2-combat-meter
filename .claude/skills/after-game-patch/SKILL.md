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

1. **Download the lists.** questlog.gg's skill list is the source (`pageCount` in a
   page says how many pages it has), and aion2.app's list pages give the skills it
   leaves out, into the same empty folder. Stop at aion2.app's first page that lists
   no skills:
   ```bash
   mkdir -p /tmp/skills
   for p in $(seq 1 10); do curl -sSL -G -o /tmp/skills/questlog$p.json --data-urlencode "input={\"language\":\"en\",\"page\":$p}" "https://questlog.gg/aion-2/api/trpc/database.getSkills"; sleep 1; done
   for p in $(seq 1 12); do curl -sSL -o /tmp/skills/page$p.html "https://aion2.app/db/skills?page=$p"; sleep 1; done
   ```
2. **Rebuild the table.** Run `python3 tools/gen_skill_table.py --import-pages /tmp/skills`.
   It rewrites `src/game/data/skills.tsv` (names, and each skill's icon) and
   `skill_table.inc`. questlog.gg's name and icon win; aion2.app's list adds the
   skills questlog.gg's does not carry (Dodge, each class's weapon, the
   Elementalist's spirits, skills no class's tree shows), and the icon questlog.gg
   gives none for (the common block's: Glide, Flight). A skill neither site names
   keeps the name the TSV had. The TSV's `# By hand`
   section (the damage Theostones' hits, which neither site lists as skills) stays
   as it is. A patch that adds Theostones adds rows there by its rule
   (AGENTS.md, Skill codes), from questlog.gg's items
   (`database.getItems` with `"searchTerm":"Theostone"`).
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
   Add list pages if the site shows more than three. Then questlog.gg's dungeon
   records, into the same folder: its list (19 pages of 40 in 2026-11; stop at the
   `pageCount` the first gives), then each record but the phasing layers' and the
   story instances' (`instancelayer`, `quest`), which give each instance's map id
   and difficulty:
   ```bash
   mkdir -p /tmp/dungeons/questlog
   for p in $(seq 1 19); do curl -sSL -G -o /tmp/dungeons/questlog/list$p.json --data-urlencode "input={\"language\":\"en\",\"page\":$p}" "https://questlog.gg/aion-2/api/trpc/database.getDungeons"; sleep 0.5; done
   python3 -c 'import json,glob; [print(r["id"]) for f in glob.glob("/tmp/dungeons/questlog/list*.json") for r in json.load(open(f))["result"]["data"]["pageData"] if r.get("mainCategory") not in ("instancelayer","quest")]' > /tmp/dungeons/questlog/ids.txt
   while read -r id; do curl -sSL -G -o "/tmp/dungeons/pages/$id.json" --data-urlencode "input={\"language\":\"en\",\"id\":\"$id\"}" "https://questlog.gg/aion-2/api/trpc/database.getDungeon"; sleep 0.3; done < /tmp/dungeons/questlog/ids.txt
   ```
2. **Rebuild the table.** Run `python3 tools/gen_map_table.py --import-pages /tmp/dungeons/pages`.
   It rewrites `src/game/data/maps.tsv` above its `# By hand` line, and `map_table.inc`.
   It names each aion2.app page that has no tier ids. Some do (the Lv.99 Awakening
   tiers, Ascension, Legion and more, at the time of writing), and questlog.gg's records
   name those maps instead, so the list alone is no failure. A difficulty it has no
   tier name for (`QUESTLOG_TIERS`) is said too, and that map keeps its bare name:
   add the pairing, read off a dungeon both sites give.
   A dungeon that had ids and is now on it means the page's layout moved.
3. **Check the diff.** New tiers are expected. An id that moved to another dungeon is
   not, and means the ids were renumbered.

## 5. Refresh the NPC names

A patch can add monsters and NPCs. Ones the meter cannot name read "NPC #2345678".

1. **Download questlog.gg's list.** It is the source: every difficulty's id of a boss,
   and the boss mechanics and summons. A query returns at most 1,000 of its 13,557
   (2026-11), so each category is split by name prefix (`a`, then `da`, `db`, ...
   where a prefix still holds more): about 1,300 queries, some 20 minutes:
   ```bash
   mkdir -p /tmp/npcs/questlog && cd /tmp/npcs/questlog
   get() { curl -sSL -G -o "$1" --data-urlencode "input=$2" "https://questlog.gg/aion-2/api/trpc/database.getNpcs"; sleep 0.5; }
   hits() { python3 -c 'import json,sys; d=json.load(open(sys.argv[1]))["result"]["data"]; print(sum(d["facetDistribution"].get("createdAt",{}).values()))' "$1"; }
   query() { printf '{"language":"en","page":%s,"mainCategory":"%s","searchTerm":"%s"}' "$3" "$1" "$2"; }
   crawl() {
     local c=$1 t=$2 f="$1-$2-1.json" n p x
     get "$f" "$(query "$c" "$t" 1)"; n=$(hits "$f")
     if [ "$n" -gt 1000 ]; then
       rm "$f"; for x in a b c d e f g h i j k l m n o p q r s t u v w x y z 0 1 2 3 4 5 6 7 8 9; do crawl "$c" "$t$x"; done; return
     fi
     for ((p = 2; p <= (n + 39) / 40; p++)); do get "$c-$t-$p.json" "$(query "$c" "$t" "$p")"; done
   }
   get all.json '{"language":"en","page":1}'
   for c in monster citizen summon bot etc; do crawl "$c" ""; done
   ```
   The page loop is bash's own, not `seq 2 N`: macOS's `seq` counts down when N is
   under 2, which fetches pages 2, 1 and 0 of every small prefix. `all.json`'s count
   is the whole list's; the import checks the pages hold it all.
   Should it say they do not, look for a category this loop does not name (a
   page's `mainCategory`).
2. **Download aion2.app's lists,** into the folder above that one. They give the ids
   questlog.gg does not list. The monster list has about 127 pages and the NPC list
   about 63 (60 a page), at the time of writing. The site's pager shows the count:
   ```bash
   for p in $(seq 1 127); do curl -sSL -o /tmp/npcs/monsters-$p.html "https://aion2.app/db/monsters?page=$p"; sleep 1; done
   for p in $(seq 1 63); do curl -sSL -o /tmp/npcs/npcs-$p.html "https://aion2.app/db/npcs?page=$p"; sleep 1; done
   cp /tmp/npcs/questlog/*.json /tmp/npcs/
   ```
3. **Rebuild the table.** Run `python3 tools/gen_npc_table.py --import-pages /tmp/npcs`.
   It rewrites `src/game/data/npcs.tsv` and `npc_table.inc`. It warns when a list came
   out shorter than its site says it is (a page or a query missing), names each id
   the two sites name apart (questlog.gg's name is taken), and counts the
   developers' identifiers it leaves out (`Test_D1_DQ_Seal4_Buff`).
4. **Check the diff.** New ids are expected. A known id under another name is not, and
   means the ids were renumbered.

## 6. Refresh the cooldowns

A patch can change a cooldown or a specialty. The Auras plugin's cooldowns start from
`plugins/auras/data/cooldowns.tsv`; a stale row makes them run long or short until the
plugin learns the skill's quickest reuse again.

1. **Download the records.** questlog.gg's record of each skill on its list (step 3's
   list pages) gives the cooldown and the specialties. aion2.app's pages fill in the
   skills it has no record of; each class's list gives them (Elementalist and
   Gladiator have a second list page at the time of writing; add one for any class
   whose list shows 60):
   ```bash
   mkdir -p /tmp/cooldowns/list /tmp/cooldowns/pages
   python3 -c 'import json,glob; [print(r["id"]) for f in glob.glob("/tmp/skills/questlog*.json") for r in json.load(open(f))["result"]["data"]["pageData"]]' | sort -u > /tmp/cooldowns/questlog-ids.txt
   while read -r id; do curl -sSL -G -o "/tmp/cooldowns/pages/$id.json" --data-urlencode "input={\"language\":\"en\",\"id\":\"$id\"}" "https://questlog.gg/aion-2/api/trpc/database.getSkill"; sleep 0.3; done < /tmp/cooldowns/questlog-ids.txt
   for c in Gladiator Templar Assassin Ranger Sorcerer Elementalist Cleric Chanter Fighter; do for p in 1 2; do curl -sSL -o "/tmp/cooldowns/list/$c-$p.html" "https://aion2.app/db/skills?class=$c&page=$p"; sleep 1; done; done
   grep -oh 'href="/db/skills/[0-9]*"' /tmp/cooldowns/list/*.html | grep -oE '[0-9]{8}' | sort -u | comm -23 - /tmp/cooldowns/questlog-ids.txt > /tmp/cooldowns/aion2app-ids.txt
   while read -r id; do curl -sSL -o "/tmp/cooldowns/pages/$id.html" "https://aion2.app/db/skills/$id"; sleep 1; done < /tmp/cooldowns/aion2app-ids.txt
   ```
2. **Rebuild the table.** Run `python3 tools/gen_cooldown_table.py --import-pages /tmp/cooldowns/pages`.
   It rewrites `plugins/auras/data/cooldowns.tsv` and `cooldown_table.inc`, and says
   how many rows each site gave.
3. **Check the diff.** Changed cooldowns and cuts are expected. Every row turning
   A or S, or every cut vanishing, means a record's shape moved: look at one
   record's `type` and `specializations` (`slot`, `specialized`), or for aion2.app's
   rows one page's specialty rows, against the importer's patterns.

## 7. Check and ship

1. Run `make fixtures`. Re-pin any `.expected.txt` whose change you have checked against
   the ground truth.
2. Run `make test`. If layouts moved, update `tests/wire_builder.hpp` and the protocol
   section of `AGENTS.md`.
3. Bump the patch version in `include/aion2/version.hpp`.
4. Tag `vX.Y.Z` to release (skill `release`). Installed copies pick the fix up through
   their update check.
