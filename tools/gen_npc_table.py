#!/usr/bin/env python3
"""Monster and NPC names: aion2.app's lists to src/meter/data/npcs.tsv.

The TSV is one `id<TAB>name` per line, `#` for comments. The meter does not read it
yet: where the spawn (41 36) carries the npc code is unconfirmed (AGENTS.md), so there
is no .inc until a capture shows it.

Usage:
  python3 tools/gen_npc_table.py            check the TSV and count its names
  python3 tools/gen_npc_table.py --import-pages DIR
                                          rebuild the TSV from aion2.app's monster and
                                          NPC list pages saved in DIR (*.html)

The import reads pages you downloaded yourself (see .claude/skills/after-game-patch):
this script never touches the network.
"""
import argparse
import datetime
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True  # no __pycache__ in tools/ from the import below
from gen_skill_table import read_tsv  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "src", "meter", "data", "npcs.tsv")

# A list page carries its rows as schema.org JSON-LD too: an ItemList whose items are
# {"name": ..., "url": ".../db/monsters/<id>"}. That is steadier than the page's markup.
LD_JSON = re.compile(r'<script type="application/ld\+json">(.*?)</script>', re.S)
ITEM_URL = re.compile(r"/db/(monsters|npcs)/(\d+)$")
# Where both lists give an id, the monster list's name wins: monsters are what the
# meter will name.
KINDS = ("monsters", "npcs")


def item_lists(text):
    """The ItemLists in a page's JSON-LD blocks."""
    for block in LD_JSON.finditer(text):
        try:
            data = json.loads(block.group(1))
        except ValueError:
            continue
        for node in data if isinstance(data, list) else [data]:
            entity = node.get("mainEntity") if isinstance(node, dict) else None
            if isinstance(entity, dict) and entity.get("@type") == "ItemList":
                yield entity


def import_pages(folder):
    pages = sorted(glob.glob(os.path.join(folder, "*.html")))
    if not pages:
        sys.exit(f"no .html pages in {folder}")
    found = {kind: {} for kind in KINDS}
    listed = {}
    for page in pages:
        with open(page, encoding="utf-8") as f:
            text = f.read()
        for entity in item_lists(text):
            for item in entity.get("itemListElement", []):
                m = ITEM_URL.search(str(item.get("url", "")))
                name = " ".join(str(item.get("name", "")).split())  # no tab or newline
                if not m or not name:
                    continue
                kind, npc_id = m.group(1), int(m.group(2))
                listed[kind] = entity.get("numberOfItems", 0)
                rows = found[kind]
                if rows.setdefault(npc_id, name) != name:
                    print(f"{os.path.basename(page)}: {kind} id {npc_id} is {name!r} here and "
                          f"{rows[npc_id]!r} before; kept that", file=sys.stderr)
    rows = {}
    for kind in KINDS:
        for npc_id, name in found[kind].items():
            if rows.setdefault(npc_id, name) != name:
                print(f"{kind} id {npc_id} {name!r} is also {rows[npc_id]!r}; kept that",
                      file=sys.stderr)
        if len(found[kind]) < listed.get(kind, 0):
            print(f"{kind}: {len(found[kind])} of the site's {listed[kind]}; "
                  "a page is missing?", file=sys.stderr)
    if not rows:
        sys.exit(f"no monsters or NPCs found in {folder}")
    today = datetime.date.today().isoformat()
    with open(TSV, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            "# AION 2 monster and NPC names by npc code. Not read by the meter yet: where\n"
            "# the spawn (41 36) carries the npc code is unconfirmed, and the u24 read as\n"
            "# it so far gave 8206 for every open-world mob, which is not an id like these.\n"
            "#\n"
            f"# Source: aion2.app's monster and NPC lists (aion2.app/db/monsters and\n"
            f"# aion2.app/db/npcs), retrieved {today}; the site says it reads them from the\n"
            "# game client. metabot.gg, read from the client on its own, gives the same ids\n"
            "# for the bosses compared (2310601 Atiel, 2301014 Eternal Ludra). A name can\n"
            "# have several ids, one per level or difficulty. Where both lists give an id,\n"
            "# the monster's name is kept. Regenerate with tools/gen_npc_table.py.\n"
        )
        for npc_id, name in sorted(rows.items()):
            f.write(f"{npc_id}\t{name}\n")
    counts = ", ".join(f"{len(found[kind])} {kind}" for kind in KINDS)
    print(f"wrote {len(rows)} names ({counts}) to {os.path.relpath(TSV, ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--import-pages", metavar="DIR")
    args = ap.parse_args()
    if args.import_pages:
        import_pages(args.import_pages)
    rows = read_tsv(TSV)
    print(f"{os.path.relpath(TSV, ROOT)}: {len(rows)} names")


if __name__ == "__main__":
    main()
