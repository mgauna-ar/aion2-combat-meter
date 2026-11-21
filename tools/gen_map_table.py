#!/usr/bin/env python3
"""Map names: src/game/data/maps.tsv to src/game/src/map_table.inc.

The TSV is the source of truth: one `id<TAB>name[<TAB>dungeon]` per line, `#` for
comments. `dungeon` marks an instance game::is_dungeon counts though its id is under
600000 (an arena, Ascension, a seal dungeon); every row but Chaotic Reshanta's and the
field events' has it. The .inc is generated from it and committed, so the build never
runs Python.

Usage:
  python3 tools/gen_map_table.py            write map_table.inc from the TSV
  python3 tools/gen_map_table.py --check    fail if the .inc is out of date (make test)
  python3 tools/gen_map_table.py --import-pages DIR
                                          rebuild the TSV from aion2.app's dungeon
                                          pages saved in DIR (one <name>.html each)
                                          and questlog.gg's dungeon records (one
                                          <id>.json each, database.getDungeon)

The map load carries questlog.gg's `mapId`, which is not always the instance's id
that aion2.app's pages give (Citadel of the Fallen Daeva's tiers are 600151 to
600153 there and maps 600144 to 600146; the matched dungeons' 690xxx are maps
156xxx). The import keys aion2.app's tiers by questlog.gg's map id, keeps aion2.app's
tier names (only its pages say "Stage 2"), and names from questlog.gg every map
aion2.app's pages do not give, its tier from the record's difficulty.

The import rewrites the rows above the TSV's "# By hand" line and keeps that line and
everything after it. It reads pages you downloaded yourself (see
.claude/skills/after-game-patch): this script never touches the network.
"""
import argparse
import datetime
import glob
import html
import json
import os
import re
import sys

sys.dont_write_bytecode = True  # no __pycache__ in tools/ from the import below
from gen_skill_table import c_string  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "src", "game", "data", "maps.tsv")
INC = os.path.join(ROOT, "src", "game", "src", "map_table.inc")

HAND_MARKER = "# By hand"
HAND_SECTION = HAND_MARKER + ": ids seen in captures. --import-pages keeps this section as it is.\n"

# questlog.gg's categories that are not a place to name: the open world's phasing
# layers ("World L A Layer 01") and story instances ("G Daeva D1 02").
QUESTLOG_SKIP = {"instancelayer", "quest"}

# questlog.gg's categories that are no dungeon to game::is_dungeon, the user's call
# (2026-11-21): Chaotic Reshanta, the Abyss's open PvP zones, and the events that
# come and go in the open world.
QUESTLOG_OPEN = {"abyss", "fieldevent"}

DUNGEON = "dungeon"

# questlog.gg's difficulty, as aion2.app names the tier, by category: read off the
# maps both sites give. A party dungeon's "hard" is aion2.app's "Stage N", which
# questlog.gg does not number, so it has no entry here.
QUESTLOG_TIERS = {
    "party": {"easy": "Exploration", "normal": "Conquest", "advanced": "Conquest [Hard]"},
    "raid": {"easy": "Easy", "normal": "Conquest", "advanced": "Conquest [Hard]"},
    "awaken": {"easy": "Easy", "normal": "Normal", "hard": "Hard", "extreme": "Challenge"},
    "matching": {"easy": "Exploration", "normal": "Conquest", "hard": "Hard",
                 "extreme": "Extreme", "despair": "Despair", "hell": "Hell"},
}

# A tier row of a dungeon page's difficulty table, in the page's Next.js payload:
# ["$","tr","600001",{... "children":"Conquest" ...}]. The row's React key is the
# tier's id; its first text child is the tier's name.
TIER_ROW = re.compile(r'"tr","(\d+)",\{')
FIRST_TEXT = re.compile(r'"children":"([^"$][^"]*)"')


def hand_section(path):
    if not os.path.exists(path):
        return HAND_SECTION
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    for i, line in enumerate(lines):
        if line.startswith(HAND_MARKER):
            return "".join(lines[i:])
    return HAND_SECTION


def ids_by_hand(path):
    """The ids in the TSV's "# By hand" section, whose names the import leaves alone."""
    return {int(line.split("\t")[0]) for line in hand_section(path).splitlines() if line[:1].isdigit()}


def page_tiers(text):
    """The page's dungeon name and its (id, tier) rows, in page order."""
    m = re.search(r"<h1[^>]*>(.*?)</h1>", text, re.S)
    if not m:
        return None, []
    name = html.unescape(" ".join(re.sub(r"<[^>]+>", " ", m.group(1)).split()))
    payload = re.sub(r'\\+"', '"', text)  # the payload is a JS string: undo its quoting
    starts = list(TIER_ROW.finditer(payload))
    tiers, seen = [], set()
    for i, row in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(payload)
        label = FIRST_TEXT.search(payload, row.end(), min(end, row.end() + 2000))
        tier_id = int(row.group(1))
        if label and tier_id not in seen:
            seen.add(tier_id)
            tiers.append((tier_id, html.unescape(label.group(1)).strip()))
    return name, tiers


def read_maps(path):
    """{id: (name, dungeon)} from the TSV."""
    rows = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if (len(parts) not in (2, 3) or not parts[0].isdigit() or not parts[1]
                    or (len(parts) == 3 and parts[2] != DUNGEON)):
                sys.exit(f"{path}:{n}: expected id<TAB>name[<TAB>{DUNGEON}]")
            map_id = int(parts[0])
            if map_id in rows:
                sys.exit(f"{path}:{n}: id {map_id} listed twice")
            rows[map_id] = (parts[1], len(parts) == 3)
    return rows


def read_questlog(folder):
    """{instance id: record} from questlog.gg's dungeon records (*.json), each a
    database.getDungeon answer."""
    records = {}
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        with open(path, encoding="utf-8") as f:
            try:
                d = json.load(f)["result"]["data"]
            except (ValueError, KeyError, TypeError):
                sys.exit(f"{path}: not a questlog.gg dungeon record")
        if not d or "mapId" not in d:
            continue  # a list page, or an id the site no longer has
        records[int(d["id"])] = d
    return records


def questlog_name(d):
    name = " ".join(str(d.get("name", "")).split())
    difficulty = d.get("difficulty")
    if not difficulty or name.endswith(")"):  # "Nightmare Altar (Insane)" says its tier
        return name
    tier = QUESTLOG_TIERS.get(d.get("mainCategory"), {}).get(difficulty)
    if tier is None:
        print(f"questlog.gg map {d['mapId']}: no tier name for {d.get('mainCategory')}/"
              f"{difficulty}; named {name!r}", file=sys.stderr)
        return name
    return f"{name} ({tier})"


def import_pages(folder):
    pages = sorted(glob.glob(os.path.join(folder, "*.html")))
    if not pages:
        sys.exit(f"no .html pages in {folder}")
    questlog = read_questlog(folder)
    if not questlog:
        sys.exit(f"no questlog.gg dungeon records (*.json) in {folder}")
    hand_ids = ids_by_hand(TSV)
    rows, where = {}, {}
    for page in pages:
        with open(page, encoding="utf-8") as f:
            name, tiers = page_tiers(f.read())
        if not name or not tiers:
            # aion2.app lists some dungeons whose page gives no tiers (the Lv.99 Awakening
            # tiers, Ascension, Legion and more): say which, for the next refresh to check.
            print(f"{os.path.basename(page)}: no tier ids ({name or 'no name'}); skipped",
                  file=sys.stderr)
            continue
        for tier_id, tier in tiers:
            # A one-tier page's name already says which (Chamber of the Dead (Challenge)).
            full = name if len(tiers) == 1 else f"{name} ({tier})"
            # The page gives the instance's id; the map load carries its map's.
            record = questlog.get(tier_id, {})
            if record:
                tier_id = int(record["mapId"])
            if tier_id in hand_ids:
                continue
            if tier_id in rows:
                print(f"{os.path.basename(page)}: id {tier_id} already {rows[tier_id][0]!r} "
                      f"from {where[tier_id]}; kept that", file=sys.stderr)
                continue
            # A page from aion2.app's dungeon list is a dungeon, unless questlog.gg
            # says it is one of the open world's.
            rows[tier_id] = (full, record.get("mainCategory") not in QUESTLOG_OPEN)
            where[tier_id] = os.path.basename(page)
    if not rows:
        sys.exit(f"no dungeon tiers found in {folder}")
    from_questlog = 0
    for d in questlog.values():
        map_id = int(d["mapId"])
        if d.get("mainCategory") in QUESTLOG_SKIP or map_id in rows or map_id in hand_ids:
            continue
        name = questlog_name(d)
        if name:
            rows[map_id] = (name, d.get("mainCategory") not in QUESTLOG_OPEN)
            from_questlog += 1
    print(f"named {from_questlog} maps from questlog.gg that aion2.app's pages do not give")
    today = datetime.date.today().isoformat()
    hand = hand_section(TSV)  # read before the TSV is truncated below
    with open(TSV, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            "# AION 2 map names by map id, as the map load (21 36) carries it, and which\n"
            "# are dungeons: an id of 600000 and up is one, named or not, and a row marked\n"
            "# `dungeon` is one below it (game::is_dungeon). Every instance is marked but\n"
            "# Chaotic Reshanta's and the open world's events'.\n"
            "#\n"
            f"# Sources, retrieved {today}: aion2.app's dungeon pages\n"
            "# (aion2.app/db/dungeons/<name>), one row per difficulty tier with its tier's\n"
            "# name; and questlog.gg's dungeon records (questlog.gg/aion-2/en/db/dungeon/<id>),\n"
            "# which give each instance's map id and name every map aion2.app's pages do\n"
            "# not give, the tier from the record's difficulty. Keyed by questlog.gg's map\n"
            "# id, not the instance's own id where they differ: the arena capture's map 61\n"
            "# is Impetusium Arena (instance 8) and the PvP capture's map 20 Chaotic Lower\n"
            "# Reshanta (instance 21). Left out: the open world's phasing layers and story\n"
            "# instances, whose names are the game's internal ones.\n"
            "#\n"
            "# Open-world zones have no public ids. The game's own files cannot be read for\n"
            "# them (they are encrypted), and no site lists numeric ids for open-world maps.\n"
            "# metabot.gg gives zone ids (Reshanta's Root is 622), but they look like zones\n"
            "# within a map, not maps. Add rows by hand below as captures show them.\n"
            "# Regenerate with tools/gen_map_table.py.\n"
        )
        for map_id, (name, dungeon) in sorted(rows.items()):
            f.write(f"{map_id}\t{name}\t{DUNGEON}\n" if dungeon else f"{map_id}\t{name}\n")
        f.write("\n" + hand)
    print(f"wrote {len(rows)} map names to {os.path.relpath(TSV, ROOT)}")


def render_inc(rows):
    lines = [
        "// Generated by tools/gen_map_table.py from src/game/data/maps.tsv.",
        "// Do not edit: change the TSV and run the script.",
        "// {map id, name, a dungeon (game::is_dungeon)}, sorted by id for a binary search.",
    ]
    for map_id, (name, dungeon) in sorted(rows.items()):
        lines.append(f"{{{map_id}u, {c_string(name)}, {'true' if dungeon else 'false'}}},")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--import-pages", metavar="DIR")
    args = ap.parse_args()
    if args.import_pages:
        import_pages(args.import_pages)
    text = render_inc(read_maps(TSV))
    if args.check:
        with open(INC, encoding="utf-8") as f:
            if f.read() != text:
                sys.exit("map_table.inc is out of date: run python3 tools/gen_map_table.py")
        return
    with open(INC, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(INC, ROOT)}")


if __name__ == "__main__":
    main()
