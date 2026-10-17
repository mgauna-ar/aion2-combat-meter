#!/usr/bin/env python3
"""Map names: src/meter/data/maps.tsv to src/meter/src/map_table.inc.

The TSV is the source of truth: one `id<TAB>name` per line, `#` for comments.
The .inc is generated from it and committed, so the build never runs Python.

Usage:
  python3 tools/gen_map_table.py            write map_table.inc from the TSV
  python3 tools/gen_map_table.py --check    fail if the .inc is out of date (make test)
  python3 tools/gen_map_table.py --import-pages DIR
                                          rebuild the TSV from aion2.app's dungeon
                                          pages saved in DIR (one <name>.html each)

The import rewrites the rows above the TSV's "# By hand" line and keeps that line and
everything after it. It reads pages you downloaded yourself (see
.claude/skills/after-game-patch): this script never touches the network.
"""
import argparse
import datetime
import glob
import html
import os
import re
import sys

sys.dont_write_bytecode = True  # no __pycache__ in tools/ from the import below
from gen_skill_table import c_string, read_tsv  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "src", "meter", "data", "maps.tsv")
INC = os.path.join(ROOT, "src", "meter", "src", "map_table.inc")

HAND_MARKER = "# By hand"
HAND_SECTION = HAND_MARKER + ": ids seen in captures. --import-pages keeps this section as it is.\n"

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


def import_pages(folder):
    pages = sorted(glob.glob(os.path.join(folder, "*.html")))
    if not pages:
        sys.exit(f"no .html pages in {folder}")
    hand = hand_section(TSV)
    hand_ids = {int(line.split("\t")[0]) for line in hand.splitlines() if line[:1].isdigit()}
    rows, where = {}, {}
    for page in pages:
        with open(page, encoding="utf-8") as f:
            name, tiers = page_tiers(f.read())
        if not name or not tiers:
            continue
        for tier_id, tier in tiers:
            # A one-tier page's name already says which (Chamber of the Dead (Challenge)).
            full = name if len(tiers) == 1 else f"{name} ({tier})"
            if tier_id in hand_ids:
                continue
            if tier_id in rows:
                print(f"{os.path.basename(page)}: id {tier_id} already {rows[tier_id]!r} "
                      f"from {where[tier_id]}; kept that", file=sys.stderr)
                continue
            rows[tier_id] = full
            where[tier_id] = os.path.basename(page)
    if not rows:
        sys.exit(f"no dungeon tiers found in {folder}")
    today = datetime.date.today().isoformat()
    with open(TSV, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            "# AION 2 map names by map id, as the map load (21 36) carries it. By the\n"
            "# community's notes ids 600000 to 700000 are instances; the Awakening tiers\n"
            "# here sit at 910000 and up.\n"
            "#\n"
            f"# Source: aion2.app's dungeon pages (aion2.app/db/dungeons/<name>), retrieved\n"
            f"# {today}: one row per difficulty tier, keyed by the tier's id. Unchecked: no\n"
            "# capture has shown a map load yet, so these are assumed, not known, to be the\n"
            "# ids 21 36 carries, and no second source gives tier ids to compare with.\n"
            "#\n"
            "# Open-world zones have no public ids. The game's own files cannot be read for\n"
            "# them (they are encrypted), and no site lists numeric ids for open-world maps.\n"
            "# metabot.gg gives zone ids (Reshanta's Root is 622), but they look like zones\n"
            "# within a map, not maps. Add rows by hand below as captures show them.\n"
            "# Regenerate with tools/gen_map_table.py.\n"
        )
        for map_id, name in sorted(rows.items()):
            f.write(f"{map_id}\t{name}\n")
        f.write("\n" + hand)
    print(f"wrote {len(rows)} map names to {os.path.relpath(TSV, ROOT)}")


def render_inc(rows):
    lines = [
        "// Generated by tools/gen_map_table.py from src/meter/data/maps.tsv.",
        "// Do not edit: change the TSV and run the script.",
        "// {map id, name}, sorted by id for a binary search.",
    ]
    lines += [f"{{{map_id}u, {c_string(name)}}}," for map_id, name in sorted(rows.items())]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--import-pages", metavar="DIR")
    args = ap.parse_args()
    if args.import_pages:
        import_pages(args.import_pages)
    text = render_inc(read_tsv(TSV))
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
