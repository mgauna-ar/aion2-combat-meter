# Agent Guidelines & Architecture Reference: AION 2 Combat Meter

The rules, architecture and invariants for anyone, agent or human, working on
`aion2-combat-meter`. It follows the conventions of `ffxiv-hub`, which several parts
were ported from.

Occasional procedures are skills under `.claude/skills/`:
- `decode-capture`: check and fix the decoder against a real capture. The decoder
  has not been through this yet.
- `after-game-patch`: the game updated, and the opcodes or layouts moved.

---

## 🔒 Passive only (the rule above all others)

The meter **reads** the game's network traffic and **draws its own window**. It never:

- opens a handle to the game process (`OpenProcess` and friends), reads or writes its
  memory, injects code, or hooks anything (`SetWindowsHookEx`, DirectX, the game's
  functions);
- sends anything: no packets (`pcap_sendpacket`, `pcap_inject`, WinDivert), no input
  (`SendInput`, `keybd_event`, `mouse_event`, `PostThreadMessage` to the game);
- talks to any server: no update checks, uploads or telemetry.

`tools/check_forbidden_apis.sh` greps our code (not `src/third_party`) for those APIs.
It runs in `make test` and in CI, so a violation fails the build. Finding the game's
connection uses a Toolhelp process snapshot and `GetExtendedTcpTable`. Neither opens a
handle to another process; keep it that way. "Is the game in front" (the overlay's
hide condition) compares `GetWindowThreadProcessId` of the foreground window with
those snapshot ids: no handle either. AION 2 has anti-cheat, and the user cares
about account safety: when in doubt, don't.

**Third-party code:** do not copy code from the community AION 2 meters. They are
GPL-3.0 and this repository is MIT. Facts about the protocol from their notes are fine;
the code is ours.

---

## 🏛️ Engineering principles

1. **Portable core.**
   - Everything but `src/app/src/win32/` builds and is tested with clang on macOS and
     Linux (`make test`).
   - Windows calls live behind `os/` helpers (`src/common/src/os/`,
     `src/capture/src/os/`), each with an `#ifdef _WIN32` mock.
   - The Windows-only window code (`src/app/src/win32/`) is built only by CMake on
     Windows, so CI is the first to compile it. Keep it small.
2. **One clock.**
   - Everything after capture runs on the capture timestamp (`Micros`, Unix µs,
     `common/micros.hpp`).
   - The pipeline, the parser and the engine never read the system clock.
   - That is why `aion2meter-replay` reproduces the live meter's fights exactly.
     `AppRuntime` is the only place that turns wall time into ticks. While packets are
     queued, it uses their time, never the source's clock, which runs ahead of a
     backlog.
3. **Nothing throws on wire data.**
   - `ByteReader` fails sticky and returns zeros.
   - Every length is bounded: frames 1 MiB, LZ4 output 4 MiB, pcap blocks 64 MiB.
   - A body that does not parse counts as a failure for its opcode and yields nothing.
4. **Zero-dependency runtime.**
   - Static `/MT`, so no Visual C++ redistributable.
   - Npcap's `wpcap.dll` is loaded at run time from `System32\Npcap`, never linked
     from the SDK and never bundled.
   - Dear ImGui is vendored, unchanged, in `src/third_party/imgui`.
   - LZ4 *decoding* is our own (`lz4_block.cpp`). The tests' encoder is in
     `tests/lz4_test_encoder.hpp`.
5. **Privacy.**
   - Captures and fight records hold character names.
   - `*.pcap*` and `tests/fixtures/local/` are gitignored.
   - Committed tests build their bytes in `tests/wire_builder.hpp` and
     `tests/synthetic_session.hpp`, with invented names.
   - The log records entity ids, never names.
6. **Documentation stays in sync.**
   - A change to behaviour, a config key, the layering or a protocol fact updates
     `README.md` and this file in the same change.
   - Each config key is documented once, in README's Configuration.
   - Anything a README picture shows means re-running `make screenshots`.

---

## 📂 Layout and layers

```
include/aion2/version.hpp        the one place the version is written (CMake, app.rc, CI tag check)
src/common/      aion2::config, aion2::os, aion2::common
    config/      json (ported), key_table (ported), json_file (atomic write)
    os/          logger, paths, single_instance, unique_handle, local_time (ported), tray_icon,
                 auto_start (the Run key)
    bounded_queue.hpp, snapshot_slot.hpp, micros.hpp, time_format
src/capture/     aion2::capture
    capture_file (pcap + pcapng reader), pcapng_writer, packet_headers, tcp_reassembler,
    packet_source + file_source, os/npcap_source, os/connection_finder
src/protocol/    aion2::proto
    byte_reader, lz4_block, frame_decoder, opcodes.hpp, events.hpp, entity_id, identity,
    packet_parser
src/meter/       aion2::meter
    types, class_codes, skill_names (data/skills.tsv → skill_table.inc), combatant_registry,
    encounter_engine, meter_settings, fight_json, history_store
src/pipeline/    aion2::pipeline   stream_demux (flows -> frames), pipeline (all of it)
src/app/         aion2::app
    app_settings, app_runtime (threads, history, recording), display_format,
    overlay_view (ImGui, behind HAVE_IMGUI), app_frame (the app window's contents)
    src/ui/      the app window's pages, theme, widgets and Lucide icons (ported from
                 ffxiv-hub; ImGui, behind HAVE_IMGUI)
    src/win32/   main.cpp, app_window, overlay_window: Windows only
tools/replay/    aion2meter-replay (every platform)
tools/synth/     aion2meter-synth: writes the tests' made-up boss fight as a pcapng
tools/screenshots/  renders docs/images from overlay_view and app_frame (software rasterizer from
                 ffxiv-hub), in UTC; --all also draws the pages the README does not show
tools/check_forbidden_apis.sh, tools/gen_app_icon.py, tools/embed_icon_font.py
tools/gen_skill_table.py         skills.tsv → skill_table.inc (`make test` checks they agree)
tests/           TEST_CASE framework (ported), one test_<concern>.cpp per area
```

**Dependencies run one way:** common ← capture; common ← protocol ← meter;
capture + meter ← pipeline ← app.
- The Makefile's `layer_inc` and CMake's per-target include roots enforce it: an
  `#include` across it does not compile.
- `make check-headers` compiles every header alone, against its own layer's roots.

---

## 🧵 Threads

| Thread | Owns | Talks through |
|---|---|---|
| Capture (one per Npcap adapter, or the FileSource thread) | its pcap handle | `BoundedQueue<OwnedPacket>` (8192), `try_push` only. A full queue drops and counts, and the drop shows up as a TCP gap. |
| Npcap watcher | the loopback filter text | a mutex plus an atomic generation counter. Each capture thread applies a new filter itself between `pcap_next_ex` calls, because a pcap handle is not shared. |
| Engine (`AppRuntime::run`) | `Pipeline`, `HistoryStore` writes, the recorder | publishes `LiveSnapshot` into a `SnapshotSlot` every 200 ms of capture time, and whenever the queue runs dry. |
| UI (`wWinMain`) | both windows (the app window and the overlay, each with its own ImGui context), the tray, `AppSettings` | reads snapshots and the history index (copied under a mutex). Requests go to the engine as atomics: end fight, recording on/off, clear history. New meter and history settings go through `AppRuntime::update_settings`, under the mutex; the engine applies them on its next pass. |

`make tsan` runs the suite under ThreadSanitizer. The `AppRuntime` tests drive real
threads with a FileSource.

---

## 📡 Protocol (Global client; from the community's notes, checked against one capture)

All of it lives in `src/protocol`. `opcodes.hpp` is the one place opcodes are spelled.

**Transport**
- TCP, server port 13328.
- Only server→client is decoded; it is not encrypted. Client→server is encrypted and
  ignored.
- Through a ping reducer or VPN the game appears on loopback, on another port. The demux
  takes any flow once 3 heartbeats prove it is the game (`detect_relays`).
- Reassembly (`TcpReassembler`): a hole waits 300 ms, or until 1 MiB queues behind it,
  then is a gap. A segment more than 4 MiB ahead is ignored as a stray, unless nothing
  else arrives for 300 ms: then the capture lost that much, and the stream skips there.

**Framing**
- A frame is `varint len, [flag 0xF0..0xFE], opcode (2 bytes, first byte high), body`.
- Its size on the wire is `len + varint_bytes − 4`.
- 0x00 bytes between frames are padding.
- An opcode of `FF FF` means `u32 raw_size` and an LZ4 block follow, decompressing into
  more frames. Nesting is capped at 2.

**Heartbeat**
- The frame is `0E 00 36` plus a u64 LE of server Unix ms, about every 50 ms.
- The decoder resyncs on one whose time is within a day of the capture's. Only such a
  frame (no flag, an 8-byte body, a time that close) counts as a heartbeat.
- 3 s with no heartbeat while bytes keep flowing means the decoder is misaligned, and it
  resyncs.

**Body layouts** (`packet_parser.cpp`)
- **Damage `04 38`:** target varint, switch varint, flag varint, actor varint, skill
  u32, hit uid u8, damage type varint, then the block.
  - **Switch:** the low 3 bits are the layout (4–7; 0 marks a cast, not damage). Bit
    0x08 puts a u32 after the damage (so 0x0C is layout 4). Bit 0x20 means an extra
    list follows.
  - **Damage type:** 3 is a crit.
  - **Block:** layout 4 has 8 bytes: a hit id (u32, counting up) and a count (u32).
    Layouts 5, 6 and 7 have 12, 10 and 14 bytes (6 seen in a capture; 5 and 7 as
    NOIA2 reads them), starting with the modifiers (0x02 parry, 0x04 perfect, 0x08
    double, 0x20 heavy; 0x80 seen, meaning unknown), a byte, and the direction (1
    back, 2 front, 0 none). Layout 4 carries no modifiers or direction.
    waffle_meter reads the flag byte as rotated by a 2026-07 patch, and says a
    lifesteal varint of 1–3 bytes can come before the direction; neither is
    confirmed here.
  - **Damage:** an optional 00 pad, then a scalar varint (about 10,000: 10,000 for a
    mob, 10,250 or 10,910 for players), then the damage varint.
  - **Tail:** with 0x20, a count and that many varints. They are not damage: the
    target's HP does not fall by them. Then the count again and a 00; the notes read
    that `01 00` as "another record follows", and no capture has shown one yet.
  - **It carries heals too:** a support skill on an ally looks like any hit, and the
    ally's HP rises. Side inference must not trust a record between two players.
    In the first capture these were layout 4 records, to the caster itself (Siphon,
    Warding Strike) or to an ally or spirit (Spirit Communion). A spirit's record to
    itself as it spawned carried 100,000, and is no heal.
  - **Not decoded yet:** whether a hit missed, was blocked, or set off the target's
    endurance, regeneration or invincibility, all of which the game's own window
    counts; and multi-hit, which may be the 0x20 tail.
  - **Scale:** a mob's HP (00 8D) falls by a steady multiple of the damage varint:
    exactly 4.5 for one player, about 10.6 for two others, 1 for a mob hitting a
    player. The multiple is not anywhere in the record. The public meters (NOIA2,
    waffle_meter) count the varint unscaled and add no extra-hit values, as we do.
    Never compare damage with HP: compare HP with HP.
- **Skill codes** (in 04 38 and 05 38):
  - 8 digits. The first two are the class: 11 Gladiator, 12 Templar, 13 Assassin,
    14 Ranger, 15 Sorcerer, 16 Elementalist (the enum's Spiritmaster), 17 Cleric,
    18 Chanter, 19 Brawler.
  - `code / 10000` is the skill; the low four digits are a variant (12240340 and
    12240347 are both Judgment).
  - A spirit's attacks (1699xxxx) carry its summoner's class digits and have no name.
  - Names come from `src/meter/data/skills.tsv`, taken from aion2.app's skill
    database. Checked against the first capture: every Templar and Elementalist code
    resolved to that class.
- **Tick `05 38`:** target varint, effect u8, actor varint, a varint of unknown meaning,
  skill u32 (×100), amount varint.
  - Effects 02 and 0A are damage; 01, 09 and 0B are heals.
- **Identity `33 36` (own) / `45 36` (other):** id varint, mask u32, flags u8 (bit 0:
  has a name), name length u8, name, server u16, class u32.
  - A name is at most 16 letters or digits, with at least one letter.
  - A name starting with `$` is a tutorial placeholder.
- **Spawn `41 36`:**
  - **Id:** a varint. An id over 1,000,000 is folded to `(raw & 0x3FFF) | 0x4000`, in
    `entity_id.cpp` only.
  - **Mask:** a u32 whose first byte is the kind. 1F/5F are summons (with an owner);
    1D/5D are spirits and effects; 1C is a boss mechanic.
  - **Position and HP:** the first `00 (00|40) 02` marker followed by a sane xyz f32
    triple, a heading f32, a u16, a byte (01; 00 for a corpse coming into view, with
    cur_hp 0), cur_hp varint and max_hp varint. Open-world mobs: 119,700.
  - **Owner:** the u32 after `07 02 06`.
- **Other opcodes:**
  - Death `42 36`: id varint. For a mob it comes about 4 s after 00 8D says HP 0: the
    corpse going. HP 0 is the death.
  - Map load `21 36`: load count u32, map id u32. Ids 600000–700000 are instances.
  - Party `02 97`: names only, found by scanning, since each member's tail varies in
    length. A found name has at least 2 characters: `01` and a letter turn up in the
    tails.
  - Entity values `00 8D`: id varint, kind u8, count u8, then that many (stat u8,
    value). Kind 1 is the local player's own values (u32; stats 1, 3, 6 seen, meaning
    unknown): no one else gets them, so they name the local player when 33 36 was
    missed. Kind 2 is anyone else's (u64); stat 0 is current HP, exact.

**Unknown or guessed. Each needs a real capture (skill `decode-capture`):**
- whether the "optional 00 pad" can be confused with a scalar of 0;
- whether `npc_code` is the u24 right after the spawn mask: every open-world mob in the
  first capture read 8206, and they may all have been one kind of mob;
- the damage scale (above): the varint is what the public meters count; the game's own
  meter has not been compared yet;
- the layout of `33 36`: the first capture started after login, so it had none;
- multi-record `04 38` packets;
- misses, blocks, multi-hit, and the target's endurance, regeneration and
  invincibility (above);
- the class codes in 33 36 / 45 36 (`meter/class_codes.cpp` is empty; skill codes
  give the class instead);
- the right `boss_min_max_hp`;
- whether the folded id range means anything: in the first capture NPC and player ids
  were all small numbers, side by side, none folded;
- the game's executable name (`Aion2.exe` is reported by third-party guides);
- whether ids change on a map load, and whether the server hands a player's old id to
  someone else. A party whose Ranger and Elementalist shared one row, the name
  switching from pull to pull, points to both, but no capture has shown a map load
  yet. The meter assumes both can happen (Meter rules, Sides).

---

## ⚔️ Meter rules (`src/meter`)

- **Sides** (`CombatantRegistry`):
  - Identity packets make players. Spawns make NPCs, or player objects for summon and
    effect kinds.
  - A summon's side is the player side, whoever owns it, and its spawn makes the owner
    a player before any name arrives.
  - Ids the meter was never told about get their side from the hits they trade: PvE
    damage only crosses sides.
  - Hits between two unknown ids are held (up to 4096) and settled when either end
    becomes known.
  - A hit with a spawned NPC settles the other end as a player, over any earlier
    inference: mobs do not fight mobs, and 04 38 also carries heals between players.
  - 00 8D's own values make the local player, shown as "You" until 33 36 names them.
  - With no anchor at all, an id in the folded range is guessed to be an enemy.
  - **Ids belong to the map, and the server hands them out again.** A player's id on
    one map can be another player's, or an NPC's, on the next. So rows belong to
    characters (name@server), not ids:
    - an id is one character at a time: the last identity for it says which;
    - a hit goes to its id's row for the character the id was then, and the summary
      joins a character's rows. An id named after it hit joins its character. Ids are
      never joined to each other, so two characters never share a row;
    - a map load, or a reload of the same map (a teleport, or a new instance of it),
      leaves players stale rather than dropping them, since their names may have come
      before it. A spawn on a stale id makes it the spawn, and a character named on a
      new id leaves its stale one. On the map they were named on, two ids can share a
      name;
    - the local player's name follows 00 8D's own values to their new id.
  - A scanned identity may have misread the id before the name. It never turns a
    spawned NPC or summon into a player, renames a player, or takes a name that an
    identity gave another id on this map.
  - A map change drops NPCs, summons and inferred sides.
- **Classes:** from the skill codes a player uses (their summons' count too), unless an
  identity packet's class code names one.
- **Party:** with no roster, every player counts. With one, only roster names do.
  Non-party hits still lower the boss's HP estimate.
- **Healing and damage taken** count only during a fight, and never start or extend
  one.
  - Healing: heal ticks, and 04 38 records from a player (not a summon) to anyone on
    the player side. It is the total, like the game's: no overheal split.
  - Damage taken: an enemy's hits and ticks on a player. A summon's HP is its own.
  - Both are booked for party members by the roster and the local player; with no
    roster, for players already in the fight, so strangers nearby stay out.
- **Skills:** each player's damage, healing and damage taken are kept per skill, keyed
  by `skill_key()`: variants of one skill share a row.
- **Fight start:** a direct hit that lands, from the player side on an enemy. A DoT tick
  never starts a fight.
- **Fight end:** the boss's death (00 8D HP 0 or 42 36, after `kill_grace_s`, unless a
  bigger boss was hit during the grace), a map change (not a reload of the same map:
  a teleport, or a new instance of it), idle time with no party hit (`idle_timeout_s`, or
  `boss_idle_timeout_s` while a boss stands), no traffic (`capture_lost_s`), a wipe
  (when enabled) or by hand.
  - A dead player stays dead for the wipe check while their DoTs and summons keep
    hitting; their own direct hit, or 00 8D HP above 0, revives them.
  - Duration runs from the first hit to the last, floored at 1 s for DPS.
- **Boss:** the damaged enemy with the most max HP. It is a boss if that max HP is at
  least `boss_min_max_hp`.
  - Its HP is the server's figure (00 8D) once one arrives. Before that, it is the spawn
    HP minus all damage seen since the spawn, shown with `~`.
  - A mob that spawned before the meter started has no max HP; the most 00 8D HP seen
    stands in for it.
  - **Partial:** a killed boss that lost less than `partial_damage_ratio` of its HP (by
    00 8D) with a hit seen since the HP figure before marks the fight partial. It is
    HP against HP: damage and HP are not in the same units. A boss that leaves view
    and spawns again keeps the loss counted so far; its spawn HP is one more figure.
- **History:**
  - What is saved: boss fights, and fights of at least `min_saved_duration_s`.
  - Each fight is one JSON file (`"v":2`; version 1 files still load, without the
    breakdowns), written atomically, plus a line appended to `index.jsonl`.
  - Fights are grouped by map visit (`visit_ts`): a map load starts one, and so does
    the first fight after the meter starts. A reload of the same map does not.
  - A cut-off index line is skipped. A missing index is rebuilt from the files.
  - Pruning keeps `history.max_fights`, in batches of 50.
  - Clearing (the app window's trash button) deletes the fight records and the index,
    on the engine thread; other files in the folder stay.

---

## 🧪 Verification

| Command | Where | What |
|---|---|---|
| `make test` | macOS / Linux | Tests, header check, ImGui syntax check, forbidden-API check, replay smoke test. |
| `make tsan`, `make asan` | macOS / Linux | The same tests under the sanitizers. |
| `make screenshots` | macOS (zlib) | Re-renders `docs/images`. |
| `make fixtures` | anywhere | Your own captures against their `.expected.txt`. |
| CI `build-windows` | GitHub | The only MSVC build: the app, ctest, the replay smoke test, the zip and its SHA-256. |

**Conventions:**
- Naming: `snake_case` functions, `PascalCase` types, `m_` members, `kName` constants.
- Comments: `///` doc comments that say why, not what.
- Commits: Conventional Commits.
- Releases: bump `include/aion2/version.hpp`, then push a `vX.Y.Z` tag; CI checks the two
  agree.
