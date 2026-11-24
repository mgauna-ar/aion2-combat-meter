#!/usr/bin/env python3
"""Monster and NPC names: src/game/data/npcs.tsv to src/game/src/npc_table.inc.

The TSV is the source of truth: one `id<TAB>name` per line, `#` for comments.
The .inc is generated from it and committed, so the build never runs Python.

Usage:
  python3 tools/gen_npc_table.py            write npc_table.inc from the TSV
  python3 tools/gen_npc_table.py --check    fail if the .inc is out of date (make test)
  python3 tools/gen_npc_table.py --import-pages DIR
                                          rebuild the TSV from questlog.gg's NPC list
                                          pages saved in DIR (*.json), and aion2.app's
                                          monster and NPC list pages (*.html) for the
                                          ids questlog.gg does not list

questlog.gg's list is the source, the user's call (2026-11-24): it holds every
difficulty's id of a boss, where aion2.app's lists give one, mostly the Conquest
tier's (Ludra's Easy tier showed as "NPC #2301038"), and the boss mechanics and
summons (2,920,xxx) aion2.app leaves out. A query returns at most 1,000 of its
13,557, so the skill's download splits it by name; the import says when the pages
hold fewer than the whole. Its developers' identifiers (M_Summon_Kashapa_01_V01_018)
are no name to show, and are skipped.

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
from gen_skill_table import c_string, read_tsv  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "src", "game", "data", "npcs.tsv")
INC = os.path.join(ROOT, "src", "game", "src", "npc_table.inc")

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


# A developer's identifier, not a name: one word of letters, digits and underscores,
# with an underscore in it (Test_D1_DQ_Seal4_Buff, N_Invisible_Summon_DredgionB_01).
IDENTIFIER = re.compile(r"[A-Za-z0-9]*_[A-Za-z0-9_]*")


def read_questlog(folder):
    """({id: name}, the whole list's count) from questlog.gg's NPC list pages (*.json).
    The count is the largest a page gives: the unfiltered query's."""
    rows = {}
    total = 0
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        with open(path, encoding="utf-8") as f:
            try:
                data = json.load(f)["result"]["data"]
                entries = data["pageData"]
            except (ValueError, KeyError, TypeError):
                sys.exit(f"{path}: not a questlog.gg NPC list page")
        total = max(total, sum((data.get("facetDistribution") or {}).get("createdAt", {}).values()))
        for e in entries:
            if e.get("dbType") != "npc":
                sys.exit(f"{path}: not a questlog.gg NPC list page")
            npc_id = str(e.get("id", ""))
            name = " ".join(str(e.get("name", "")).split())  # no tab or newline
            if npc_id.isdigit() and name:
                rows[int(npc_id)] = name
    return rows, total


def import_pages(folder):
    questlog, total = read_questlog(folder)
    if not questlog:
        sys.exit(f"no questlog.gg NPC list pages (*.json) in {folder}")
    if len(questlog) < total:
        print(f"questlog.gg: {len(questlog)} of the site's {total}; a query or a page "
              "is missing?", file=sys.stderr)
    pages = sorted(glob.glob(os.path.join(folder, "*.html")))
    if not pages:
        sys.exit(f"no aion2.app list pages (*.html) in {folder}")
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
    from_aion2app = len(rows)
    named_apart = []
    identifiers = 0
    for npc_id, name in questlog.items():
        if IDENTIFIER.fullmatch(name):
            identifiers += 1
            continue
        if npc_id in rows and rows[npc_id] != name:
            named_apart.append((npc_id, rows[npc_id], name))
        rows[npc_id] = name
    for npc_id, theirs, ours in sorted(named_apart):
        print(f"{npc_id}: {theirs!r} on aion2.app, {ours!r} on questlog.gg; took questlog.gg's",
              file=sys.stderr)
    only_aion2app = sum(1 for npc_id in rows if npc_id not in questlog)
    print(f"{len(questlog) - identifiers} names from questlog.gg ({identifiers} identifiers "
          f"left out), {only_aion2app} more from aion2.app's {from_aion2app}; "
          f"{len(named_apart)} named apart")
    today = datetime.date.today().isoformat()
    with open(TSV, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            "# AION 2 monster and NPC names by npc code, the u32 a spawn (41 36) carries\n"
            "# after its mask. Every mob spawn in the first capture carried one of these.\n"
            "#\n"
            f"# Sources, retrieved {today}: questlog.gg's NPC database\n"
            "# (questlog.gg/aion-2/en/db/npcs), every difficulty's id of a boss and the\n"
            "# mechanics and summons among them; and aion2.app's monster and NPC lists\n"
            "# (aion2.app/db/monsters, aion2.app/db/npcs) for the ids questlog.gg does not\n"
            "# list. metabot.gg, read from the client on its own, gives the same ids for the\n"
            "# bosses compared (2310601 Atiel, 2301014 Eternal Ludra). A name can have\n"
            "# several ids, one per level or difficulty. A developer's identifier\n"
            "# (Test_D1_DQ_Seal4_Buff) is no name and is left out. Regenerate with\n"
            "# tools/gen_npc_table.py.\n"
        )
        for npc_id, name in sorted(rows.items()):
            f.write(f"{npc_id}\t{name}\n")
    print(f"wrote {len(rows)} names to {os.path.relpath(TSV, ROOT)}")


def render_inc(rows):
    lines = [
        "// Generated by tools/gen_npc_table.py from src/game/data/npcs.tsv.",
        "// Do not edit: change the TSV and run the script.",
        "// {npc code, name}, sorted by code for a binary search.",
    ]
    lines += [f"{{{npc_id}u, {c_string(name)}}}," for npc_id, name in sorted(rows.items())]
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
                sys.exit("npc_table.inc is out of date: run python3 tools/gen_npc_table.py")
        return
    with open(INC, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(INC, ROOT)}")


if __name__ == "__main__":
    main()
