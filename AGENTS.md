# Agent Guidelines & Architecture Reference: AION 2 Combat Meter

The rules, architecture and invariants for anyone, agent or human, working on
`aion2-combat-meter`. It follows the conventions of `ffxiv-hub`, which several parts
were ported from.

Occasional procedures are skills under `.claude/skills/`:
- `decode-capture`: check and fix the decoder against a real capture. The decoder
  has not been through this yet.
- `after-game-patch`: the game updated, and the opcodes or layouts moved.
- `release`: cut a release, and what every installed copy's updater expects of it.

---

## 🔒 Passive only (the rule above all others)

The meter **reads** the game's network traffic and **draws its own window**. It never:

- opens a handle to the game process (`OpenProcess` and friends), reads or writes its
  memory, injects code, or hooks anything (`SetWindowsHookEx`, DirectX, the game's
  functions);
- sends anything: no packets (`pcap_sendpacket`, `pcap_inject`, WinDivert), no input
  (`SendInput`, `keybd_event`, `mouse_event`, `PostThreadMessage` to the game);
- talks to any server but GitHub's, and to GitHub only for the update check (Self-update):
  one HTTPS request at start, which `check_for_updates` turns off, and a download
  when the user clicks **Update & restart**. Nothing reaches the game or its servers;
  there are no uploads and no telemetry.

`tools/check_forbidden_apis.sh` greps our code (not `src/third_party`) for those APIs,
and keeps every network API in `src/common/src/os/http_client.cpp`.
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
   - So are the updater's SHA-256 and zip reader (`sha256`, `archive/`, ported from
     ffxiv-hub). HTTPS is Windows' own WinHTTP.
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
                 auto_start (the Run key), http_client (WinHTTP GET, the only network
                 access), process_launch (the updater's relaunch)
    archive/     inflate (RFC 1951) and zip_reader, for the release zip (ported)
    bounded_queue.hpp, snapshot_slot.hpp, micros.hpp, time_format, sha256 (ported)
src/capture/     aion2::capture
    capture_file (pcap + pcapng reader), pcapng_writer, packet_headers, tcp_reassembler,
    packet_source + file_source, os/npcap_source, os/connection_finder
src/protocol/    aion2::proto
    byte_reader, lz4_block, frame_decoder, opcodes.hpp, events.hpp, entity_id, identity,
    packet_parser
src/meter/       aion2::meter
    types, class_codes, skill_names (data/skills.tsv → skill_table.inc),
    map_names (data/maps.tsv → map_table.inc), npc_names (data/npcs.tsv → npc_table.inc),
    combatant_registry, encounter_engine, meter_settings, fight_json, history_store
src/pipeline/    aion2::pipeline   stream_demux (flows -> frames), pipeline (all of it)
src/app/         aion2::app
    app_settings, app_runtime (threads, history, recording), display_format,
    overlay_view (ImGui, behind HAVE_IMGUI), app_frame (the app window's contents),
    update_check (release parsing, versions, the update balloon) and updater (check,
    download, stage, swap), both ported from ffxiv-hub
    src/ui/      the app window's pages, theme, widgets and Lucide icons (ported from
                 ffxiv-hub; ImGui, behind HAVE_IMGUI)
    src/win32/   main.cpp, app_window, overlay_window: Windows only
tools/replay/    aion2meter-replay (every platform)
tools/synth/     aion2meter-synth: writes the tests' made-up boss fight as a pcapng
tools/screenshots/  renders docs/images from overlay_view and app_frame (software rasterizer from
                 ffxiv-hub), in UTC; --all also draws the pages the README does not show
tools/check_forbidden_apis.sh, tools/gen_app_icon.py, tools/embed_icon_font.py
tools/gen_skill_table.py         skills.tsv → skill_table.inc (`make test` checks they agree)
tools/gen_map_table.py           maps.tsv → map_table.inc (the same)
tools/gen_npc_table.py           aion2.app's monster and NPC pages → npcs.tsv → npc_table.inc
                                 (the same)
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
| UI (`wWinMain`) | both windows (the app window and the overlay, each with its own ImGui context), the tray, `AppSettings`, the `Updater` | reads snapshots and the history index (copied under a mutex). Requests go to the engine as atomics: end fight, recording on/off, clear history. New meter and history settings go through `AppRuntime::update_settings`, under the mutex; the engine applies them on its next pass. |
| Updater worker (one at a time) | the check or the download | `Updater::status()`, copied under its mutex, which the UI reads each frame. Staged files are moved into place on the UI thread (`apply_staged`). `stop()` cancels a download and joins, before the engine stops. |

`make tsan` runs the suite under ThreadSanitizer. The `AppRuntime` tests drive real
threads with a FileSource.

---

## 📡 Protocol (Global client; from the community's notes, checked against two captures)

All of it lives in `src/protocol`. `opcodes.hpp` is the one place opcodes are spelled.

The two captures: 30 s of open-world mobs, then a full run of a dungeon (Vakron Sky
Island), from login through three boss kills and back to town, whose final boss fight
was checked against the game's own Combat Analysis window (Ctrl+X). For the local
player there, damage (2,265,310), hits (862) and every per-skill CRI, PERF, DOUB,
BACK, FRNT, MULT and BLOC share matched it exactly.

**Transport**
- TCP, server port 13328.
- Only server→client is decoded; it is not encrypted. Client→server is encrypted and
  ignored.
- Through a ping reducer or VPN the game appears on loopback, on another port. The demux
  takes any flow once 3 heartbeats prove it is the game (`detect_relays`).
- Entering a dungeon opens a new connection, to another server; leaving it, another
  back to the town's. Each is its own flow, with its own framing state. The demux
  numbers them (`Frame::connection`, carried into `ParsedEvent`), and the engine tells
  a new run of a dungeon from a reload within one by it (Meter rules, Fight end).
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
  - **Damage type:** 3 is a crit; 2 otherwise in the second capture.
  - **Block:** layout 4 has 8 bytes: a hit id (u32, counting up) and a count (u32).
    Layouts 5, 6 and 7 have 12, 10 and 14 bytes (6 seen in a capture; 5 and 7 as
    NOIA2 reads them), starting with the modifiers (0x02 block, 0x04 perfect, 0x08
    double, 0x20 heavy; 0x80 seen, meaning unknown), a byte (00 in every record of
    the second capture), and the direction (1 back, 2 front, 0 none). Layout 4
    carries no modifiers or direction.
    - 0x02 was the notes' "parry". Its count was the game's BLOC (11 of 862), skill
      by skill: the target blocked the hit.
    waffle_meter reads the flag byte as rotated by a 2026-07 patch, and says a
    lifesteal varint of 1–3 bytes can come before the direction; neither is
    confirmed here.
  - **Damage:** an optional 00 pad, then a scalar varint (about 10,000: 10,000 for a
    mob, 10,250, 10,910 or 12,910 for players), then the damage varint. Layouts 5–7
    had the pad in every record of the second capture, layout 4 never.
  - **Tail:** with 0x20, a count (1–4) and that many varints: a multi-hit's further
    strikes. A record with them is one the game's MULT counts (228 of 862, matching
    per skill). Neither the game's damage nor the target's HP adds them. Then the
    count again and a 00; the notes read that `01 00` as "another record follows",
    and no capture has shown one yet.
  - **It carries heals too:** a support skill on an ally looks like any hit, and the
    ally's HP rises. Side inference must not trust a record between two players,
    and neither can healing: a PvP hit on the other faction looks the same.
    In the first capture these were layout 4 records, to the caster itself (Siphon,
    Warding Strike) or to an ally or spirit (Spirit Communion). A spirit's record to
    itself as it spawned carried 100,000, and is no heal.
  - **Not decoded yet:** whether a hit missed, or set off the target's endurance,
    regeneration or invincibility, all of which the game's own window counts. The
    second capture had none of them (ACC 100%, ENDR, RSTO and INVC 0%), so it could
    not show where they live.
  - **Scale: settled.** The damage varint is the game's own damage figure: summed
    over the second capture's boss fight it matched the Combat Analysis to the
    point. HP is in other units: a mob's HP (00 8D) falls by a steady multiple of
    the damage (4.5 for one player in the first capture, about 10.6 for two others),
    and the last boss took 18.59M damage against 9.86M max HP. Never compare damage
    with HP: compare HP with HP.
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
  - Effect 00 has no amount: after the varint, a u32 (100,000,010 or so) and the end.
    It came on a boss, actor and target both, about once a second of its fight.
  - Effect 30 is a heal, always on the player themself: the skill u32 is not scaled
    (2001 for every class; once 100031), and after the amount a u32 names the enemy
    skill whose hit set it off. It may be a shield taking the hit. The game's Total
    Heal counts it: without it the local player's healing came to half the game's
    figure, with it to 97%.
  - Effects 02 to self (100,000, a spirit spawning), 08 and 38 (a boss skill on a
    trap, naming a player) are rare, and 08 and 38 fail to parse until understood.
- **Identity `33 36` (own) / `45 36` (other):** id varint, mask u32, flags u8 (bit 0:
  has a name), name length u8, name, server u16, a u32 read as the class.
  - 33 36 has a u16 of unknown meaning between the name and the server: 1406 in
    all three of the second capture's, whose server was 9 where the same name turned
    up in other packets. Read as the server, it made the local player a character of
    server 1406. Its flags were 0x37, where 45 36's are 0x07.
  - The "class" u32 is not the class. In 45 36 it was 0x01010000, 0x02010000 or
    0x02020000: a Gladiator and a Ranger shared one value, a Sorcerer and a Chanter
    another. Its two high bytes may be faction and gender; unconfirmed.
    `class_codes.cpp` stays empty, and skill codes give the class.
  - `scan_identities` finds record look-alikes in packets the parser does not read.
    The meter names no one by them: in the second capture 16 of its 38 carried a real
    character's name, none with that character's id. They came in bursts with the
    party packets, as a dungeon was entered and finished. Replay still prints them.
  - A name is at most 16 letters or digits, with at least one letter.
  - A name starting with `$` is a tutorial placeholder.
- **Spawn `41 36`:**
  - **Id:** a varint. An id over 1,000,000 is folded to `(raw & 0x3FFF) | 0x4000`, in
    `entity_id.cpp` only.
  - **Mask:** 3 bytes, the first the kind (then `20 00` for mobs, `10 00` for the one
    summon seen). 1F/5F are summons (with an owner); 1D/5D are spirits and effects;
    1C is a boss mechanic.
  - **Aimed at a player:** bit 0 of the mask's third byte (`1C 00 01`, also on kinds
    14 and 54) puts a name before the npc code: length u8, then the name of the
    party member the mechanic is aimed at. All 57 such spawns in the second capture
    carried one, then a valid code (2,920,360, 364, 371 or 474, none listed).
    - The 10-HP and 3-HP ones are traps around that player, which the others break
      1 damage a hit. The game's window counts neither those hits nor their damage.
    - Read as the code, the name's first bytes made "NPC #1818840325" and the like.
  - **Npc code:** the u32 after the mask, or after the name. Names come from `src/meter/data/npcs.tsv`
    (aion2.app's 7,545 monsters and 3,719 NPCs; metabot.gg agrees on the bosses
    compared): seven digits, 2,000,002 to 2,990,928, one id per level or difficulty.
    - Checked against the first capture: all 10 mob spawns carried a listed code
      there (Highland Odyle Sparkie, Docile Highland Brax). The summon's code
      (2,920,107) is not listed.
    - It was read as the u24 a byte later until then: the code's upper bytes, 8206 for
      every 2,100,7xx mob.
    - Checked against the second capture: its bosses (Tiere, Thamon, Vakron) and its
      trash named themselves from their spawn's code. The boss adds and mechanics
      (2,920,xxx) are not listed.
    - A code not listed shows as "NPC #code".
  - **Position and HP:** the first `00 (00|40) 02` marker followed by a sane xyz f32
    triple, a heading f32, a u16, a byte (01; 00 for a corpse coming into view, with
    cur_hp 0), cur_hp varint and max_hp varint. Open-world mobs: 119,700. In the first
    capture the marker came straight after the npc code, its 00 being the code's top
    byte, so the search starts at the code.
  - **Owner:** the u32 after `07 02 06`, which comes after the HP.
- **Other opcodes:**
  - Death `42 36`: id varint. For a mob it comes about 4 s after 00 8D says HP 0: the
    corpse going. HP 0 is the death.
  - Map load `21 36`: load count u32, map id u32. Ids 600000–700000 are instances.
    - Names come from `src/meter/data/maps.tsv`. It holds aion2.app's dungeon tier
      ids: one row per difficulty, such as Krao Cave 600002 (Exploration) and 600001
      (Conquest), with Awakening tiers at 910000 and up, outside the instance range.
      The second capture's dungeon came as 600072, which the table names Vakron Sky
      Island (Conquest), the dungeon that was run: the tier ids are the map ids.
    - Open-world zones have no public numeric ids. Their rows go by hand under the
      TSV's `# By hand` line, which `--import-pages` keeps. metabot.gg gives 333 zone
      ids (Reshanta's Root is 622), but they look like zones within a map, not maps,
      so none are in the TSV. The second capture's town came as 1010, which matches
      none of them.
  - Party `02 97`: names only, found by scanning, since each member's tail varies in
    length. A found name has at least 2 characters: `01` and a letter turn up in the
    tails. A list sent while the party forms or breaks up names fewer members than
    its count, and is dropped: a partial list would leave out members who are there.
  - Entity values `00 8D`: id varint, kind u8, count u8, then that many (stat u8,
    value). Kind 1 is the local player's own values (u32; stats 1, 3, 6 seen, meaning
    unknown): no one else gets them, so they name the local player when 33 36 was
    missed. Kind 2 is anyone else's (u64); stat 0 is current HP, exact. Kind 3 is
    the local player's again, both lists in one: kind 1's, then a count and kind 2's,
    which carry their own HP.

**Unknown or guessed. Each needs a real capture (skill `decode-capture`):**
- whether the "optional 00 pad" can be confused with a scalar of 0;
- multi-record `04 38` packets;
- misses, and the target's endurance, regeneration and invincibility (above);
- where the class is in 33 36 / 45 36, and what their "class" u32 and 33 36's extra
  u16 hold (`meter/class_codes.cpp` is empty; skill codes give the class instead);
- tick effects 08 and 38;
- whether the game's Total Heal counts heals before a fight's first hit: the meter's
  healing came to 97% of it;
- what the 5-HP object (2,920,366, kind 14, no name) the party broke in the second
  capture's first boss fight is: its 1-damage hits still count, while the game's
  window for that fight was not checked;
- `boss_min_max_hp` (1,000,000) against other dungeons: the second capture's bosses
  had 5.22M–9.86M max HP, its trash 203k–435k, open-world mobs about 120k;
- whether the folded id range means anything: in the first capture NPC and player ids
  were all small numbers, side by side, none folded;
- the game's process name: the Steam install's executable is
  `Aion2/Binaries/Win64/AION2.exe`, which the default `Aion2.exe` matches (the match
  ignores case), but no one has checked the name of the running process;
- the open-world zones' map ids (the second capture's town was 1010);
- whether the server hands a player's old id to someone else. Ids belong to a map
  instance and are handed out on entering it: in the second capture the local
  player's id was new on each of its three maps, while players who stayed in town
  kept theirs across the 14 minutes the local player spent in the dungeon (22 of 23; one
  came back with a new id). A party whose Ranger and Elementalist shared one row,
  the name switching from pull to pull, points to reuse. The meter assumes both
  (Meter rules, Sides).
- whether a teleport or a revive within a dungeon sends a 21 36, and over which
  connection. Re-entering the same dungeon straight away did, as the runs that shared
  one visit showed, and runs then split once every load was taken for a new one. The
  meter takes the same map over the same connection for a reload within a run, and
  over a new connection for a new run, since entering a dungeon opened one in the
  second capture. A run still split in two, or two runs in one, would prove it
  wrong;
- how a player of the other faction is told apart, for PvP: perhaps 45 36's mask.
  Until then they are players like the party, so a PvP fight starts nothing and a
  hit on them counts for nothing (Meter rules, Party and Healing).

**The client's own files cannot fill in names.** They were checked on the Steam install
in 2026-10.
- **Format:** UE5. Every `.pak` (v11) has an AES-encrypted index, and 245 of the 282
  IoStore `.utoc` containers are flagged encrypted, all under the default key id, which
  Unreal builds into the executable.
- **NC's tables:** NC's own tables (`StartUpData/Table/*.dat`, `L10N/<lang>/L10NString.dat`)
  sit next to a `key_manifest.dat`, so a second layer of encryption is likely.
- **Instead:** names come from public databases (aion2.app) and from captures.

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
    - a map load, or a reload within a run, leaves players stale rather than
      dropping them, since their names may have come before it. A spawn on a stale id makes it the spawn, and a character named on a
      new id leaves its stale one. On the map they were named on, two ids can share a
      name;
    - the local player's name follows 00 8D's own values to their new id.
  - A scanned identity names no one (Protocol, Identity). One that took a stale
    party member's name, after a load, made the meter forget the member's id, and
    the member fell out of the party.
  - A map load drops NPCs, summons and inferred sides; a reload within a run keeps
    them.
- **Classes:** from the skill codes a player uses (their summons' count too), unless an
  identity packet's class code names one.
- **Party:** with a roster, only its names count, and the local player.
  - With none, it depends on the map. In an instance (`MapLoad::is_instance`) every
    player counts: only the group gets in. In the open world, or on a map the meter
    did not see load, only the local player does, so a crowd at a world boss stays
    out.
  - Non-party hits still lower the boss's HP estimate, but never start or extend a
    fight.
- **Healing and damage taken** count only during a fight, and never start or extend
  one.
  - Healing: heal ticks to anyone on the player side (effect 30's included), and 04 38
    records from a player (not a summon) to a party member. A record to anyone else
    may be a PvP hit. It is the total, like the game's: no overheal split.
  - Damage taken: an enemy's hits and ticks on a player. A summon's HP is its own.
  - Both are booked for party members by the roster and the local player; with no
    roster, for players already in the fight, so strangers nearby stay out.
- **Skills:** each player's damage, healing and damage taken are kept per skill, keyed
  by `skill_key()`: variants of one skill share a row.
- **Hits** (`HitCounts`, the game's columns): direct hits, and of them crits (CRI),
  perfect (PERF), double (DOUB), back (BACK), front (FRNT), multi-hits (MULT: a
  record with the 0x20 tail, one hit however many strikes) and blocked (BLOC).
  History files before the block was known saved it as `"parry"`, which still loads.
- **Aimed mechanics:** a spawn aimed at a player (Spawn::aimed_at), such as a trap the
  party breaks, takes no damage that counts: hits on it are neither damage nor hits,
  and never start or extend a fight, as in the game's window. Its hits on players are
  damage taken.
- **Fight start:** a direct hit that lands, from the player side on an enemy. A DoT tick
  never starts a fight.
- **Fight end:** the boss's death (00 8D HP 0 or 42 36, after `kill_grace_s`, unless a
  bigger boss was hit during the grace), a map load (the same map's included when it
  comes over a new connection: running a dungeon again, straight back in; not over the
  same one, a teleport or a revive within the run), idle time with no party hit (`idle_timeout_s`, or
  `boss_idle_timeout_s` while a boss stands), no traffic (`capture_lost_s`), a wipe
  (when enabled) or by hand.
  - A dead player stays dead for the wipe check while their DoTs and summons keep
    hitting; their own direct hit, or 00 8D HP above 0, revives them.
  - Duration runs from the first hit to the last, floored at 1 s for DPS.
- **Boss:** the damaged enemy with the most max HP. It is a boss if that max HP is at
  least `boss_min_max_hp`.
  - Calibrated on one dungeon: its bosses had 5.22M–9.86M max HP, its trash
    203k–435k. Undamaged adds and mechanics can share the boss's max HP; they never
    win, having taken no damage.
  - Its name comes from its spawn's npc code (`npc_label`): "NPC #code" when the table
    does not list it, "#id" when it was never seen spawning. The label is saved with
    the fight, so a fight keeps the name it had.
  - Its HP is the server's figure (00 8D) once one arrives. Before that, it is the spawn
    HP minus all damage seen since the spawn, shown with `~`.
  - A mob that spawned before the meter started has no max HP; the most 00 8D HP seen
    stands in for it.
  - **Partial:** a killed boss that lost less than `partial_damage_ratio` of its HP (by
    00 8D) with a hit seen since the HP figure before marks the fight partial. It is
    HP against HP: damage and HP are not in the same units. A boss that leaves view
    and spawns again keeps the loss counted so far; its spawn HP is one more figure.
- **History:**
  - What is saved: boss fights, and fights of at least `min_saved_duration_s`. With
    `save_only_dungeons`, only in a dungeon (`is_dungeon`: a row above maps.tsv's
    `# By hand` line, so not map 0, an open-world zone, or a map added by hand).
  - Each fight is one JSON file (`"v":2`; version 1 files still load, without the
    breakdowns), written atomically, plus a line appended to `index.jsonl`.
  - Fights are grouped by map visit (`visit_ts`): every map load starts one but a
    reload within a run (the same map over the same connection), so each run of a
    dungeon is its own. So does the first fight after the meter starts.
  - A cut-off index line is skipped. A missing index is rebuilt from the files.
  - Pruning keeps `history.max_fights`, in batches of 50.
  - Clearing (the app window's trash button) deletes the fight records and the index,
    on the engine thread; other files in the folder stay.

---

## 🔄 Self-update (`src/app`: `update_check`, `updater`; ported from ffxiv-hub)

The meter updates itself from this repository's GitHub releases. The repository is
public: GitHub's API shows a private repository's releases to no anonymous caller.

- **One automatic check per start.** `Updater::tick()` checks once,
  `kStartupCheckDelay` (10 s) after its first tick, when `check_for_updates` is on and
  `os::http_supported()` holds, and never again in that run. `http_supported()` is false
  on the mock, so no test, screenshot or non-Windows build reaches the network.
  **Check now** ignores the switch.
- **Only what can be verified is installed.**
  - A release is announced when its `tag_name` is a strictly newer `vX.Y.Z`; never a
    downgrade, a draft or a prerelease.
  - It is installable only when its `aion2-combat-meter-windows-x64.zip` asset lies
    under this repository's `releases/download/` and GitHub published a `sha256:`
    digest for it. The download must match that digest before anything is unpacked.
  - A URL from the API is followed or opened only under this repository's prefixes
    (`update_check.hpp`), and `os::open_url()` opens nothing but `https://`.
  - Only `UPDATE_FILES` (the meter's exe) come out of the zip, by exact name, each
    checked against its size and CRC. The zip holds nothing else; the replay tool is
    a release asset of its own, which the updater never touches.
- **Rename aside, never overwrite.**
  - A running exe can be renamed but not overwritten or deleted. The download is
    staged in `.aion2-combat-meter-update/` inside the install folder, so moving it in
    is a rename on one volume.
  - `apply_staged_update()` renames each live file to `<name>.<old version>.old`,
    moves the new one in, and undoes every step on any failure. A file the folder
    lacks (renamed or deleted by hand) is just moved in.
  - `remove_update_leftovers()` deletes the staging folder and those backups at every
    start, best effort: a backup still running stays for a later start.
- **The relaunch waits on the lock, not the process.** Once the files are swapped,
  the app starts the new exe with `--updated` and leaves through its normal teardown.
  The new instance retries the single-instance mutex for up to 15 s
  (`SingleInstance::acquire_within`), which frees as the old one exits. Waiting on
  the old process itself would need `OpenProcess`, which Passive only forbids.
- **The tray stays lifecycle-only.** The update notice is a balloon through
  `TrayIcon::notify`, once per version per run (`UpdateNotifier`), gated by
  `notifications`. The tray menu gets no update item; installing lives in Settings,
  behind a confirmation, and a sidebar pill points there.
- **A release is a contract with every installed copy.** The asset's name, the
  exe at the zip's root, the tag scheme and GitHub's digest are what old versions
  look for. Changing any of them strands every copy already installed
  (skill `release`).

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
  agree and publishes the release, which installed copies update to (skill `release`).
