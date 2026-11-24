# Agent Guidelines & Architecture Reference: AION 2 Combat Meter

The rules, architecture and invariants for anyone, agent or human, working on
`aion2-combat-meter`. It follows the conventions of `ffxiv-hub`, which several parts
were ported from, its plugin layout among them.

Each feature is a plugin with rules of its own, next to it. Read them before touching
a plugin:
- [`plugins/combat_meter/AGENTS.md`](plugins/combat_meter/AGENTS.md): fights, sides,
  the boss, the history;
- [`plugins/auras/AGENTS.md`](plugins/auras/AGENTS.md): effects, the target, the
  cooldown estimates;
- [`plugins/ping/AGENTS.md`](plugins/ping/AGENTS.md): the ping off the game's own
  pong, the jitter, the counter offset;
- [`plugins/combat_power/AGENTS.md`](plugins/combat_power/AGENTS.md): other players'
  combat power as the game sends it, the target, the list.

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
those snapshot ids: no handle either. No game process found means the game is not
in front (`foreground_is_game`). The Ping plugin sends no ping of its own: it
times the game's, against the machine's performance counter
(`QueryPerformanceCounter`), which is system-wide and read in our own process. AION 2
has anti-cheat, and the user cares about account safety: when in doubt, don't.

**Third-party code:** copy none: this repository is MIT, and its code is its own.
Facts about the protocol from the community's published notes are fine. **Game art** is
NCSOFT's: the class emblems, skill icons and item icons are bundled as data
(`game_icon_sheet.inc`), taken from aion2.app, outside the MIT licence (README,
License).

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
   - One figure comes from outside the capture: how far its clock sits from the
     machine's performance counter (`PacketSource::counter_offset`), measured by the
     capture threads and handed to the pipeline as data, for the Ping plugin alone.
     A capture file has none.
   - That is why `aion2meter-replay` reproduces the live meter's fights exactly.
     `AppRuntime` is the only place that turns wall time into ticks. While packets are
     queued, it uses their time, never the source's clock, which runs ahead of a
     backlog.
   - **Quiet time runs on from the latest packet.** With the queue empty, time moves
     on from that packet's stamp by as much as the source's clock has run since the
     engine took it off the queue, never to the source's clock itself: live,
     `NpcapSource::now()` is the wall clock, while Npcap stamps packets on the
     performance counter, and the gap between the two read as quiet time (a stall to
     the Ping plugin, a reassembly hole expiring early). Taken at dequeue, a backlog
     makes it trail, never lead. It never goes back: a step back of the source's
     clock holds it still. Before the first packet there is no capture time; the
     source's clock only names a recording, and the pipeline is not ticked.
   - **What is left of the two clocks:** a forward step of the wall clock (a time
     sync) while the queue is empty still shows live as that much quiet time, and
     time then holds there until the packets' stamps catch up. A Windows-only
     `NpcapSource::now()` on the capture's own clock (the performance counter plus
     the counter offset) would close it.
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
   - The icon sheet is raw DEFLATE, decoded once by that same `archive::inflate`, and
     painted into each window's font atlas: no image library, no second texture. The
     app's own mark (`app_mark.inc`, the sidebar's) is too, kept apart from the sheet,
     since the sheet is NCSOFT's art and the mark is ours.
5. **Privacy.**
   - Captures and fight records hold character names.
   - `*.pcap*` and `tests/fixtures/local/` are gitignored.
   - Committed tests build their bytes in `tests/wire_builder.hpp` and
     `tests/synthetic_session.hpp`, with invented names.
   - The log records entity ids, never names.
6. **Documentation stays in sync.**
   - A change to the app, the layering or a protocol fact updates `README.md` and this
     file in the same change; a change to a plugin's behaviour or keys updates that
     plugin's `AGENTS.md` and `README.md`.
   - Each config key is documented once: the app's in README's Configuration, a
     plugin's in its own README.
   - Anything a README picture shows means re-running `make screenshots`, and
     `make gif` when it is the meter's overlay.

---

## 📂 Layout and layers

```
include/aion2/version.hpp        the one place the version is written (CMake, app.rc, CI tag check)
include/aion2/plugin_registry.hpp  every plugin's id, name, description, config section and
                                 default switch (Plugins), and the switches themselves
src/common/      aion2::config, aion2::os, aion2::common
    config/      json (ported), key_table (ported), json_file (atomic write)
    os/          logger, paths, single_instance, unique_handle, local_time (ported), tray_icon,
                 auto_start (the Run key), install_entry (the installer's entry in
                 Windows' app list), http_client (WinHTTP GET, the only network
                 access), process_launch (the updater's relaunch), performance_clock
                 (the machine's performance counter, for the Ping plugin)
    archive/     inflate (RFC 1951) and zip_reader, for the release zip (ported)
    bounded_queue.hpp, snapshot_slot.hpp, micros.hpp, time_format, sha256 (ported)
src/capture/     aion2::capture
    capture_file (pcap + pcapng reader), pcapng_writer, packet_headers, tcp_reassembler,
    packet_source + file_source, os/npcap_source, os/connection_finder,
    clock_offset (the capture's clock against the performance counter)
src/protocol/    aion2::proto
    byte_reader, lz4_block, frame_decoder, opcodes.hpp, events.hpp, entity_id, identity,
    packet_parser
src/game/        aion2::game   what the game is, for every plugin
    plugin.hpp (game::Plugin: events and ticks in), classes, class_codes,
    skill_names (data/skills.tsv → skill_table.inc; effect names too, from
    data/effects.tsv → effect_table.inc first),
    map_names (data/maps.tsv → map_table.inc), npc_names (data/npcs.tsv → npc_table.inc),
    effect_book (the protocol's rules for effects: each plugin that reads them keeps one)
plugins/combat_meter/  aion2::meter (include prefix meter/; its AGENTS.md and README.md)
    types, combatant_registry, encounter_engine, meter_settings, fight_json,
    history_store, pull_grouping, fight_detail (what a fight keeps beyond its totals),
    timeline, death_log, effect_uptime
plugins/auras/   aion2::auras (include prefix auras/; its AGENTS.md and README.md)
    aura_plugin, entities (who is who, and the local player's HP), aura_tracker (effects
    on everyone in view), cooldowns (data/cooldowns.tsv → cooldown_table.inc, and the
    local player's tracker), chains (their chain skills on offer)
plugins/ping/    aion2::ping (include prefix ping/; its AGENTS.md and README.md)
    ping_plugin, ping_tracker (the pong's round trip, the heartbeats' jitter and
    lateness, the graph's seconds)
plugins/combat_power/  aion2::combat_power (include prefix combat_power/; its AGENTS.md
                 and README.md)
    power_plugin, power_tracker (the map's players, their combat power and how far
    they are, the local player's target)
src/pipeline/    aion2::pipeline   stream_demux (flows -> frames), pipeline (parser, then
                 each plugin switched on)
src/app/         aion2::app
    app_settings, app_runtime (threads, history, recording), display_format,
    overlay_view (the Combat Meter's overlay), aura_view (the Auras'), ping_view
    (the Ping's) and power_view (the Combat Power's; ImGui, behind HAVE_IMGUI),
    overlay_chrome (what the four share, so they stay one look: the fonts, the frame,
    the header's buttons, the grip, the bars),
    overlay_visibility, app_frame (the app window's contents),
    update_check (release parsing, versions, the update balloon) and updater (check,
    download, stage, swap), both ported from ffxiv-hub,
    game_icons (class emblems, skill icons and items' effect icons from
    game_icon_sheet.inc, painted into
    each window's font atlas at the size it draws them, and the app's mark from
    app_mark.inc at the sidebar chip's), timeline_chart (the Timeline tab's
    lines as numbers, no ImGui)
    src/ui/      the app window's pages (a page per plugin, opened by plugin_page's
                 header and master switch), theme, widgets, table (the meter's tables)
                 and Lucide icons (ported from ffxiv-hub; ImGui, behind HAVE_IMGUI);
                 the Combat Meter page's view_timeline, view_deaths, view_effects and
                 view_casts
    src/win32/   main.cpp, app_window, overlay_window: Windows only
packaging/windows/  installer.nsi (the per-user installer, NSIS) and try-installer.ps1
                 (installs and uninstalls it on CI's runner)
tools/replay/    aion2meter-replay (every platform)
tools/synth/     aion2meter-synth: writes the tests' made-up boss fight as a pcapng
tools/screenshots/  renders docs/images from overlay_view and app_frame (software rasterizer from
                 ffxiv-hub), in UTC; --all also draws the pages the README does not show
                 and the overlays unlocked; --frames draws the meter's overlay through
                 the whole fight, which `make gif` joins with ffmpeg
tools/check_forbidden_apis.sh, tools/embed_icon_font.py
tools/gen_app_icon.py            the app's mark (wings around a crystal, our own drawing)
                                 → app.ico and app_mark.inc
tools/gen_skill_table.py         questlog.gg's skill list, and aion2.app's for the skills it
                                 leaves out → skills.tsv → skill_table.inc (`make test`
                                 checks they agree);
                                 the TSV's third column names each skill's icon, and its
                                 `# By hand` section (the Theostones) outlives an import
tools/gen_icon_sheet.py          aion2.app's class emblems, skill icons and item icons →
                                 game_icon_sheet.inc (`make test` checks its index against
                                 skills.tsv and effects.tsv)
tools/gen_effect_table.py        questlog.gg's status-effect pages → effects.tsv → effect_table.inc
                                 (the same)
tools/gen_map_table.py           aion2.app's dungeon pages and questlog.gg's dungeon
                                 records → maps.tsv → map_table.inc (the same)
tools/gen_npc_table.py           questlog.gg's NPC list, and aion2.app's monster and NPC
                                 pages for the ids it leaves out → npcs.tsv → npc_table.inc
tools/gen_cooldown_table.py      questlog.gg's skill records, and aion2.app's skill pages for
                                 the skills it has none of → cooldowns.tsv →
                                 cooldown_table.inc (the same)
tests/           TEST_CASE framework (ported), one test_<concern>.cpp per area
```

**Dependencies run one way:** common ← capture; common ← protocol ← game ← each
plugin; capture + the plugins ← pipeline ← app. No plugin depends on another.
- The Makefile's `layer_inc` and CMake's per-target include roots enforce it: an
  `#include` across it, one plugin's header in another included, does not compile.
- `make check-headers` compiles every header alone, against its own layer's roots.

---

## 🧵 Threads

| Thread | Owns | Talks through |
|---|---|---|
| Capture (one per Npcap adapter, or the FileSource thread) | its pcap handle | `BoundedQueue<OwnedPacket>` (8192), `try_push` only. A full queue drops and counts, and the drop shows up as a TCP gap. Each Npcap thread also samples the capture clock against the performance counter (`ClockOffsetEstimator`, under its own mutex), which the engine reads before each packet and each tick. |
| Npcap watcher | the loopback filter text | a mutex plus an atomic generation counter. Each capture thread applies a new filter itself between `pcap_next_ex` calls, because a pcap handle is not shared. |
| Engine (`AppRuntime::run`) | `Pipeline`, `HistoryStore` writes, the recorder | publishes `LiveSnapshot` into a `SnapshotSlot` every 200 ms of capture time, and whenever the queue runs dry. Its clock is the packets' time; with the queue empty, the latest packet's plus the source's clock elapsed since it was dequeued (One clock). A fight's `FightDetail` is shared, never copied: a snapshot holds a pointer to it. Stopped (Exit, Update & restart), it drains the closed queue, ends the live fight (`Manual`, at the last packet's time) and saves it before it returns. |
| UI (`wWinMain`) | the five windows (the app window and the meter, aura, ping and combat power overlays, each with its own ImGui context), the tray, `AppSettings`, the `Updater` | reads snapshots and the history index (copied under a mutex). Requests go to the engine as atomics: end fight, recording on/off, clear history. New meter and history settings and the plugin switches go through `AppRuntime::update_settings`, under the mutex; the engine applies them on its next pass. |
| Updater worker (one at a time) | the check or the download | `Updater::status()`, copied under its mutex, which the UI reads each frame. Staged files are moved into place on the UI thread (`apply_staged`). `stop()` cancels a download and joins, before the engine stops. |

`make tsan` runs the suite under ThreadSanitizer. The `AppRuntime` tests drive real
threads with a FileSource.

---

## 📡 Protocol (Global client; from the community's notes, checked against four captures)

All of it lives in `src/protocol`. `opcodes.hpp` is the one place opcodes are spelled.

The two captures: 30 s of open-world mobs, then a full run of a dungeon (Vakron Sky
Island), from login through three boss kills and back to town, whose final boss fight
was checked against the game's own Combat Analysis window (Ctrl+X). For the local
player there, damage (2,265,310), hits (862) and every per-skill CRI, PERF, DOUB,
BACK, FRNT, MULT and BLOC share matched it exactly.

A third, the arena capture: three minutes of a town, a one-on-one arena match (map
61, Impetusium Arena) and the town again, 79 player records in all. It gave the combat power (Identity,
below).

A fourth, the PvP capture: five minutes on map 20 (Chaotic Lower Reshanta, in the
Abyss) with both factions in view, 71
player records, and a fight between the local player and one of the other faction.
It gave the faction (Identity, below).

**Transport**
- TCP, server port 13328.
- Only server→client is decoded; it is not encrypted. Client→server is encrypted and
  ignored.
- Through a ping reducer or VPN the game appears on loopback, on another port. The demux
  takes any flow once 3 heartbeats prove it is the game (`detect_relays`).
- Entering a dungeon opens a new connection, to another server; leaving it, another
  back to the town's. Each is its own flow, with its own framing state. The demux
  numbers them (`Frame::connection`, carried into `ParsedEvent`), and the plugins tell
  a new run of a dungeon from a reload within one by it
  (plugins/combat_meter/AGENTS.md, Fight end).
  - **A number is a connection, not an address pair.** Back in town, Windows can
    hand the town's client port out again: a new SYN on a 4-tuple already seen gets
    the next number, as the reassembler starts a new stream there. Kept by address
    pair, it came back under the town's old number, lower than the dungeon's, and the
    plugins took it for that old connection's last frames.
  - **A zone change, on the wire** (all five in the four captures): the old
    connection's last heartbeat; 0.10 to 0.18 s later the new connection's SYN and
    its first frame, 276 bytes the parser does not read (`11 36` or `5b 42`); then
    nothing for 2.4 to 2.7 s, the load, until a `15 36` and its first heartbeat. The old server
    sent its FIN within milliseconds of the new SYN in four of them, and not at all
    in the arena's return to town.
  - **A game connection's end** (FIN or RST, once its bytes are through) goes to the
    pipeline as `FrameSink::on_connection_closed`, with its number. The Ping plugin
    takes it, and a newer connection's first frame, for the end of the old one's
    heartbeats (plugins/ping/AGENTS.md, Stalls).
- **The map and the local player's name are the pipeline's**, from 21 36 and 33 36,
  whatever plugins are on: every overlay's `show_in_dungeons_only` and the dashboard
  read them, and they once came from the meter's engine, which hears nothing while
  the meter is off. The meter's registry still names the local player while it runs
  (it knows them from 00 8D when 33 36 was missed).
- Reassembly (`TcpReassembler`): a hole waits 300 ms, or until 1 MiB queues behind it,
  then is a gap. A segment more than 4 MiB ahead is ignored as a stray, unless nothing
  else arrives for 300 ms: then the capture lost that much, and the stream skips there.

**Framing**
- A frame is `varint len, [flag 0xF0..0xFE], opcode (2 bytes, first byte high), body`.
- Its size on the wire is `len + varint_bytes − 4`.
- 0x00 bytes between frames are padding.
- An opcode of `FF FF` means `u32 raw_size` and an LZ4 block follow, decompressing into
  more frames. Nesting is capped at 2.

**Heartbeat and pong**
- The heartbeat frame is `0E 00 36` plus a u64 LE of server Unix ms, about every 50 ms.
- The decoder resyncs on one whose time is within a day of the capture's. Only such a
  frame (no flag, an 8-byte body, a time that close) counts as a heartbeat, for the
  decoder's watchdog and the parser's `Heartbeat` event alike
  (`FrameDecoder::heartbeat_server_ms`, the one rule). Any other `00 36` is a parse
  failure; none of the four captures had one.
- 3 s with no heartbeat while bytes keep flowing means the decoder is misaligned, and it
  resyncs.
- **Pong `03 36`:** the server's answer to the client's ping. 18 bytes: two bytes
  (00 00), a u64 LE the client sent, a u64 LE of server Unix ms. All 95 in the two
  captures parse.
  - One every 10 s on each connection, the first as it opens. The echoed u64 steps
    by exactly 10,000 a pong: the client's own clock, in ms.
  - **That clock reads as Unreal's** `FPlatformTime::Seconds()`: the performance
    counter since boot plus 16,777,216 s. The two captures' first pongs (16,784,277,100
    and 16,803,843,895) are 1.96 h and 7.40 h of uptime that way, on two different
    days. Inferred, not confirmed (below).
  - The pong's arrival less that clock sat within 0 to 3 ms of its least, with spikes
    of 20 to 45 ms: it moves as a round trip does. The Ping plugin makes the ping of
    it (plugins/ping/AGENTS.md).
  - The server's ms is the heartbeat's clock: a pong's fell between the two
    heartbeats around it. A heartbeat's delay ran 7 to 10 ms longer than a pong's, so
    the two are not stamped at the same point of the server's tick.

**Body layouts** (`packet_parser.cpp`)
- **Damage `04 38`:** target varint, switch varint, flag varint, actor varint, skill
  u32, hit uid u8, damage type varint, then the block.
  - **Switch:** the low 3 bits are the layout (4–7; 0 marks a cast, not damage). Bit
    0x08 puts a u32 after the damage (so 0x0C is layout 4). Bit 0x20 means an extra
    list follows.
  - **Damage type:** 3 is a crit; 2 otherwise in the second capture.
  - **Block:** layout 4 has 8 bytes: a hit id (u32, counting up) and a count (u32).
    Layouts 5, 6 and 7 have 12, 10 and 14 bytes (6 seen in a capture; 5 and 7 as
    the community's notes give them), starting with the modifiers (0x02 block, 0x04 perfect, 0x08
    double, 0x20 heavy; 0x80 seen, meaning unknown), a byte (00 in every record of
    the second capture), and the direction (1 back, 2 front, 0 none). Layout 4
    carries no modifiers or direction.
    - 0x02 was the notes' "parry". Its count was the game's BLOC (11 of 862), skill
      by skill: the target blocked the hit.
    Other notes read the flag byte as rotated by a 2026-07 patch, and say a
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
- **Skill codes** (in 04 38, 05 38 and 02 38):
  - 8 digits. The first two are the class: 11 Gladiator, 12 Templar, 13 Assassin,
    14 Ranger, 15 Sorcerer, 16 Elementalist (the enum's Spiritmaster), 17 Cleric,
    18 Chanter, 19 Brawler.
  - `code / 10000` is the skill; the low four digits are a variant (12240340 and
    12240347 are both Judgment).
  - Except in each class's common block, `XX00xxxx`, whose skills are a hundred
    apart: its weapon (`XX0000xx`, "Equip Sorcerer Weapon") and Dodge (`XX0001xx`).
    There `code / 100` is the skill (`game::skill_base`): read by the ten thousands,
    the dungeon capture's Dodge 15000101 named the Sorcerer's weapon.
  - A spirit's attacks (1699xxxx) carry its summoner's class digits and have no name.
  - Names come from `src/game/data/skills.tsv`, taken from questlog.gg's skill
    database, the user's call (2026-11-24): aion2.app's had lost the Brawler's
    names and specialties. The two named all 364 skills both list alike.
    aion2.app's adds what questlog.gg's leaves out: Dodge, each class's weapon, the
    Elementalist's spirits, skills no class's tree shows. Checked against the first
    capture: every Templar and Elementalist code resolved to that class.
  - **7-digit codes are no class's,** and neither site lists them: monsters' skills
    (Ludra's 1801xxx), potions (2011101, whose effect 20,111,011 is the Life
    Potion) and Theostones.
  - **A damage Theostone's hit is `3000TNN`,** a mode-0 cast of its wearer's, then a
    04 38: T the tier (0, 1, 2: the items 51385xxxx, 51384xxxx, 51383xxxx, whose
    Fire stone deals 5,000, 10,000 and 15,000), NN the item's number plus 6 (17 to
    24), as the Theostones that put an effect on run `3001TNN` (effects.tsv). Read
    off the figures, not stated anywhere: in the dungeon capture four players'
    3000017, 3000019, 3000020 and 3000024 went off 13, 42, 71 and 193 times, in the
    order of their items' 1%, 5%, 10% and 20%, at a median 6,020, 859, 379 and 29
    against the items' 5,000, 1,000, 500 and 125; in the Ludra capture a Chanter's
    3000117 and 3000118 came to a median 11,010 and 5,102 against 10,000 and 5,000.
    Their names sit in the TSV's `# By hand` section, which an import keeps. In
    Ludra the Earth and Wind stones hit for a sixth of their figure or less (Ulgorn's
    Will a median 168): the boss resisting them, perhaps.
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
- **Cast `02 38`:** a skill going off, anyone's: caster varint, a byte, skill u32, a
  sequence u8 (counting up per caster), a mode u8, target varint (the caster's own
  for a self-cast), then a tail not read.
  - **Mode 2 is a press,** and its tail a position (20–22 bytes). Mode 0 is a skill
    going off unpressed, with a tail of 8–12 bytes. No other mode was seen in the
    second capture's 28,550 records; the byte before the skill was 0 in all but 33
    (8, 12 or 32).
  - **Checked against the local player's own 01 38:** 1,154 of their 1,176 presses came
    back as a 02 38 of the same skill from them, 11 ms later on average, 1,143 of
    them in mode 2. Of their 1,137 other 02 38 records, 1,129 were mode 0: passives
    going off (Punishing Benediction, 257 times), chain follow-ups (variant xx47 of
    Vicious, Decisive and Desperate Strike), and every Chanter's mantras pulsing,
    once a second (2,244 records, all mode 0).
  - **Not quite every press:** two self-buffs (Defiance, Shield of Protection) came
    back in mode 0, and a follow-up of Punishment's in mode 2 with no press. Over the
    capture's five fights, the meter's casts matched the local player's 01 38, skill
    by skill, for 97.2% of them.
  - **NPCs' too:** 11,868 records carried 7-digit codes, 10,355 in mode 2. Nothing
    reads them yet.
- **Identity `33 36` (own) / `45 36` (other):** id varint, mask u32, flags u8 (bit 0:
  has a name), name length u8, name, server u16, a u32 read as the class.
  - 33 36 has a u16 of unknown meaning between the name and the server: 1406 in
    all three of the second capture's, whose server was 9 where the same name turned
    up in other packets. Read as the server, it made the local player a character of
    server 1406. Its flags were 0x37, where 45 36's are 0x07.
  - The "class" u32 is not the class. In 45 36 it was 0x01010000, 0x02010000,
    0x01020000 or 0x02020000: a Gladiator and a Ranger shared one value, a Sorcerer
    and a Chanter another. `class_codes.cpp` stays empty, and skill codes give the
    class.
  - **Its third byte is the faction** (`Identity::faction`: 1 or 2), in 33 36 too,
    whose fourth is the level (0x2D010000: faction 1, level 45).
    - In the PvP capture 66 records read 1, as the local player's own did, and 3
      read 2. Every fight between players there was a 1 against a 2: the local
      player's (22 skills pressed on a 2, 42 taken from them) and two others'. No 1
      pressed a skill on a 1 but themself.
    - The arena opponent, of the local player's faction by their public page, read
      1 as they did.
    - **A dungeon's party mixes both:** two of the dungeon capture's party read 2
      (16 records). The faction alone makes no one an enemy there.
    - The fourth byte of 45 36's (1 or 2, either with either faction) is their
      gender, most likely; unconfirmed.
    - Which of 1 and 2 is the Elyos is not settled by one page; nothing needs it.
  - `scan_identities` finds record look-alikes in packets the parser does not read.
    The meter names no one by them: in the second capture 16 of its 38 carried a real
    character's name, none with that character's id. They came in bursts with the
    party packets, as a dungeon was entered and finished. Replay still prints them.
  - A name is at most 16 letters or digits, with at least one letter.
  - A name starting with `$` is a tutorial placeholder.
  - **Combat power:** 45 36 is the player's whole record, 1,162 to 2,151 bytes in the
    arena capture: after the fields above come their HP, their gear slot by slot,
    their looks. Its last fields are a u32 of unknown meaning (the tag: `F4 CB 1F`
    after a byte of 21, 24 or 26), a title and a rank perhaps (u32 each: 1,001 to
    1,299, and 3 to 19), the combat power (u32), and five 00 bytes
    (`read_combat_power`).
    - **Checked against public character pages** (which give the official site's
      figure): the arena opponent's read 134,667 and another player's 188,962, both
      their page's figure to the point. A third read 187,649 against 188,087 hours
      later, and a level-11 character 4,756 against 6,189: characters still gaining.
    - All 77 of the capture's 45 36 records read one, 4,756 to 210,515.
    - **It is found from the tag,** not from the end: 11 records had one byte more
      after the zeros (01 in 6, 00 in 5), and a figure's own high bytes are zeros, so
      from the end those five read 256 times too small.
    - **The other shapes:** 2 records had a 01 between the rank and the figure (6
      of the three captures' records: four 01, two 03); the level-11 character's had
      no title and no rank, the figure straight after the tag. A record with no tag
      is read from the end, past a last byte that is not 0.
    - **That byte and a 00 after the zeros can look alike.** Before a figure under
      39,063, read from the byte, the figure is 256 times itself plus the byte, and
      the record reads as one ending in a 00 more would (4,756 read 1,217,537). Where
      both read, a byte of 1 to 3 is taken for the inserted one: a figure's low byte
      is anything, so 1 to 3 there is that byte far more often than a figure's.
    - **Not read yet:** 4 of the dungeon capture's records had a title and no rank
      (13 bytes after the tag), and 2 of the map-20 capture's four bytes more
      between the rank and the figure. Those show "-".
    - Item level (2,700 on the opponent's page) is not among these fields.
    - **33 36 does not end with it:** the local player's own record ends in a list of
      small u32s, and their combat power has not been found.
    - The arena sent the opponent's record three times in the match, the figure the
      same each time: it is neither hidden nor equalised there.
  - **The effects running on them** (`read_listed_effects`): both records list them,
    and no 2a 38 comes for what already runs as a player comes into view or onto a
    new map. A count u8, then per entry 2a 38's fields from its kind on: the kind
    (0x10 to 0x13: bit 0 a caster varint, bit 1 the applying skill u32), the instance
    (varint, 2a 38's numbering), the effect u32, the whole duration u64, the end u64
    (the server's Unix ms; 2100-01-01 for a permanent one), the caster, the level u8,
    the skill, then a position (three f32; FLT_MAX in the local player's own items').
    - **Some entries carry one byte more before the position** (00, 70, 80, C0 and
      the like, never 0x10 to 0x13), with no field yet seen to say which: the next
      entry's kind says it, and the last entry's is not read.
    - **It is found, not read to:** 104 to 159 bytes into the 45 36 records, 329 into
      the 33 36, after fields not read (HP, a legion's name, zeros). The first count
      whose every entry reads, each where the last ends, is the list. In all 338
      records of the four captures it was found once, its count right.
    - **The end is the truth:** the local player's food ended 2.9 s later in their
      own record on a new map than its 2a 38 had said, the loading screen's length.
    - Entering the arena, the local player's record listed the three scrolls used in
      town, 195 to 379 s left; nothing else said so. Kind 0x10 entries name no
      caster: scrolls and food, but also 74 Chanter and Cleric effects, so no
      caster is not "themself" (`game::effect_caster` takes it so for an item's only).
    - **The Theostones a player wears** are permanent entries, one or two a player
      (30,000,071 to 30,001,241), each a stone's code (`3000TNN`, Skill codes, above)
      times ten plus one. In the dungeon capture each of the four players whose
      damage Theostone went off carried that stone's (3000017, 19, 20 and 24 as
      30,000,171, 191, 201 and 241), and the one who also put Reda's Tenacity
      (30,010,131) on the boss carried 30,000,131 as well. `effects.tsv` lists none.
- **Spawn `41 36`:**
  - **Id:** a varint. An id over 1,000,000 is folded to `(raw & 0x3FFF) | 0x4000`, in
    `entity_id.cpp` only.
  - **Mask:** 3 bytes, the first the kind (then `20 00` for mobs, `10 00` for the one
    summon seen). 1F/5F are summons (with an owner); 1D/5D are spirits and effects;
    1C is a boss mechanic.
    - **An NPC can own a 1F:** in the Ludra capture, 37 1F spawns (npc 2,920,671
      Abyssal Wings Ketu and 2,920,702, unnamed) named the boss as owner, each with the boss's own max
      HP (85,640,625). An Elementalist's spirit (2,920,107, Fire Spirit) is a 1F owned by a player,
      so the kind alone does not tell. Every 5F seen had a player for owner.
      A summon an NPC owns is an enemy of its own to the plugins: the meter books
      no hit on it, nor its death, to the boss, and the Auras' target stays on the
      boss (plugins/combat_meter/AGENTS.md, Sides; plugins/auras/AGENTS.md, The target).
  - **Aimed at a player:** bit 0 of the mask's third byte (`1C 00 01`, also on kinds
    14 and 54) puts a name before the npc code: length u8, then the name of the
    party member the mechanic is aimed at. All 57 such spawns in the second capture
    carried one, then a valid code (2,920,360, 364, 371 or 474: Vakron's Thorn
    Root, Gluttonous Tiere, Thamon, Thamon's Petrification).
    - The 10-HP and 3-HP ones are traps around that player, which the others break
      1 damage a hit. The game's window counts neither those hits nor their damage.
    - Read as the code, the name's first bytes made "NPC #1818840325" and the like.
  - **Npc code:** the u32 after the mask, or after the name. Names come from `src/game/data/npcs.tsv`
    (questlog.gg's 13,557 NPCs, 12,644 of them named, and one more of aion2.app's;
    metabot.gg agrees on the bosses compared): seven digits, 2,000,000 to 2,990,928,
    one id per level or difficulty.
    - **questlog.gg's list, the user's call (2026-11-24).** aion2.app's lists (11,281)
      give one id per boss, mostly its Conquest tier's: Ludra's Easy tier (2301030
      to 2301038) showed as "NPC #2301038". Nor did they list the boss mechanics and
      summons (2,920,xxx) the captures carry. Where both named an id they agreed but
      for a hyphen ("Grudge-Bearing") and one word ("Tundra Lagur").
    - **913 of questlog.gg's are developers' identifiers**
      (`M_Summon_Kashapa_01_V01_018`, `Test_D1_DQ_Seal4_Buff`): no name to show,
      left out, so they read "NPC #code".
    - A query of its list returns at most 1,000; the skill `after-game-patch` splits
      it by name prefix.
    - Checked against the first capture: all 10 mob spawns carried a listed code
      there (Highland Odyle Sparkie, Docile Highland Brax), and the summon's
      (2,920,107) is the Fire Spirit.
    - It was read as the u24 a byte later until then: the code's upper bytes, 8206 for
      every 2,100,7xx mob.
    - Checked against the second capture: its bosses (Tiere, Thamon, Vakron) and its
      trash named themselves from their spawn's code, and so do the boss adds and
      mechanics (2,920,xxx) since questlog.gg's list.
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
    - **Not every one is a death.** A boss's kill came down in figures to 10% or
      less, then HP 0, then 42 36 (the second capture's three). Things carrying the
      boss's own max HP (mechanics, adds, traps: 9.86M there, 85.6M in Ludra) went
      from full to HP 0, or got a 42 36, with no figure between: a mechanic ending,
      not a kill. Trash gets no 00 8D at all, and dies by 42 36 alone. How a boss
      resetting with two parties on it goes is not seen (Unknown, below;
      plugins/combat_meter/AGENTS.md, Boss).
    - A player gets none: their death is 00 8D HP 0 (kind 2, or kind 3 for the local
      player), and HP above 0 that they are up again. In the second capture's last boss
      fight 19 deaths came that way, each back up 3 to 17 s later, at full HP.
    - A killing hit can carry 0: the local player's did there (a heavy hit by the boss),
      though the same skill hit others for 77,635 and more.
  - **Left view `47 36`:** id varint, a byte, a byte (00 in most; 4F or 46 in a
    few). Someone left the local player's view; whoever comes back is sent again.
    - In the PvP capture 62 of its 68 named players (53 of the 58 there). Of the 15
      seen again later, 11 came with a new `45 36`; the rest were a hit landing in
      the same instant, and one player acting again with no new record. In the
      dungeon capture all 31 seen again came with a new `45 36`.
    - **A player's death goes through it:** the two players whose HP reached 0 in
      the PvP capture got theirs 1.4 s and 3.4 s later.
    - The first byte reads like a reason: 1 in most, 10, 2 and 4 in a few (the 4
      once, for the local player's own id, as their position jumped 1.1 km: a
      death and a return, perhaps). Some name NPCs. Not
      needed: any of them means gone.
  - Map load `21 36`: load count u32, map id u32.
    - **An id of 600000 or more is a dungeon**, named or not, so a dungeon a patch
      adds counts before the meter can name it. The party dungeons, raids and
      Awakening tiers sit there (600001 to 620022, 910011 to 910067), and so do the
      daily dungeons (900001 to 900003).
    - **Not every instance does** (questlog.gg's map ids, below): the matched
      dungeons (Orcus's Grave, Fafnite Forge and two more) are 156000 to 156045,
      Ascension 200002 to 220027, Growth 160000 to 160005, the Abyss Corridor 503001
      to 504006, the seal dungeons 310001 to 400002, the arenas 60 to 64, the Legion
      airships 901 to 905, Pantheon 50, Chaotic Reshanta 20 to 23, the open world's
      events 111002 to 510038. The one open-world id seen is the town's, 1010.
    - **Two rules, in `game`'s map_names, the one place that says either:**
      - `is_dungeon`, for the overlays' `show_in_dungeons_only` and
        `save_only_dungeons`: 600000 or more, or a row `maps.tsv` marks `dungeon`.
        The import marks every instance below but Chaotic Reshanta (the Abyss's
        open PvP) and the events, the user's call (2026-11-21): the arenas and
        battlefields count, the airships too.
      - `is_party_instance`, for who is on the local player's side with nothing
        else to go by: 600000 or more alone. Only the group that entered is in
        there, so the meter counts everyone in view as party with no roster, and the
        Combat Power plugin marks no one of the other faction as an enemy. An arena
        or battlefield has the other side inside, and stays out.
    - Names come from `src/game/data/maps.tsv`, one row per difficulty, such as
      Krao Cave 600002 (Exploration) and 600001 (Conquest). Two sources fill it
      (`tools/gen_map_table.py`): aion2.app's dungeon pages give each tier's name,
      and questlog.gg's dungeon records (`database.getDungeon`) each instance's map
      id, a name for every map aion2.app's pages do not give, and a difficulty for
      its tier. 412 rows, 2026-11-21.
    - **A row is keyed by the map's id, not the instance's.** questlog.gg gives
      both: the same for most, not for 32 (Fire Temple Arena is instance 7, map
      60). The captures went by the map's: the arena capture's 61 is Impetusium
      Arena's map (instance 8), the PvP capture's 20 Chaotic Lower Reshanta's
      (instance 21), and the second capture's 600072 Vakron Sky Island (Conquest),
      the run it was, whose two ids agree. aion2.app's tier ids are the instances':
      until 2026-11-21 the table held Citadel of the Fallen Daeva under 600151 to
      600153 (maps 600144 to 600146) and the matched dungeons under 690001 to
      690046 (maps 156000 to 156045), which no map load would carry.
    - A tier added later gets an id of its own, with no pattern to guess it by:
      Krao Cave's Conquest [Hard] is 600004, skipping 600003. A raid's normal tier is
      the site's Conquest (Abyssal Forge: Ludra 600082, its Hard tier 600083).
    - **aion2.app's pages leave tiers out:** Fire Temple's and Ferocious Horn Den's
      Hard tiers (600023, 600093) and Ludra's Easy tier (600081) are on questlog.gg
      alone, and the Lv.99 Awakening tiers (Insane, Hell, Doom), Ascension, Abyss
      Corridor, Growth, Daily, the Legion airships, the battlefields, Chaotic
      Reshanta and the arenas have pages with no tier ids. questlog.gg names them
      all. Its difficulty is named as aion2.app names the tier, by category
      (`QUESTLOG_TIERS`: a party dungeon's "advanced" is Conquest [Hard]), except a
      party dungeon's "hard", aion2.app's "Stage N", which questlog.gg does not
      number: those come from aion2.app alone.
    - Left out: questlog.gg's phasing layers of the open world ("World L A Layer
      01") and story instances ("G Daeva D1 02"), 323 records named as the game
      names them inside.
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
    - **Stat 7 is max HP.** At login and on each map the local player's came four
      times, climbing 11,988, 17,421, 21,320, 25,373 as their gear's figures added up
      (the same four on all three maps of the second capture), and their current HP
      (564 records) never went above it. Others got a stat 7 twice.

**Effects, skill uses and cooldowns** (the Auras plugin's; checked on the second
capture, where all 17,182 records parse):
- **Effect put on `2a 38` / renewed `2b 38`:** target varint, (2a 38 only: a byte, 01
  in all 4,008), kind u8, instance varint, effect u32, duration u64 (ms; all ones for no
  end), end u64 (the server's Unix ms, as the heartbeat's), caster varint, level u8,
  (kind 0x13 only: the applying skill u32), a byte (00, or 02 on most renewals), and a
  position (three f32: the caster's, below). Kind 0x11 has no skill; no other
  kind was seen.
  - **Instances are stacks.** Each application gets the next instance number on its
    target, and an effect applied again while it runs is a second instance (a Templar
    debuff ran four at once). 2b 38 renews an instance by number.
  - **The end is the truth.** It equals arrival + duration (to 50 ms) for every 2a 38,
    but for some renewals (flag 00) it is up to 1.4 s sooner than the stated duration.
  - **The effect id** is mostly the applying skill's code × 10 plus a digit (Undefeated
    Mantra 18190041 → 181,900,411). 78% of effects get a name from `skills.tsv` that way
    (`effect_name`). The rest are monster skills (7-digit codes ×10), items and system
    effects (12011, 80001). A skill's code names the effect better than the id: one
    Gladiator effect's id pointed at Ruinous Blow while its record named Leaping Slam.
  - **An effect's own name** comes from `src/game/data/effects.tsv`, before its
    skill's: questlog.gg's status-effect database lists 2,714 effects by this id, with
    the icon that tells an item's from a skill's. An item effect's row names that icon
    (the fourth column), and the icon sheet packs it: a scroll's effect shows the scroll. Checked on the second capture:
    120,900,001, which Punishment puts on for 20 s, is "Executor"; 22,101,031 and
    22,101,051, which a player put on themself for 300 s, are "Speed Scroll" and
    "Courage Scroll"; 20,111,011 (10 s, 31 times) is "Life Potion"; the bosses' 12,000,161
    and 12,000,173 are "Burn" and "Slow". It named 26 of the capture's 66 unnamed ids.
    - Item effects are 8 digits from 20,xxx,xxx (potions), 21,xxx,xxx (food) and
      22,xxx,xxx (scrolls); 30,010,131 is a Theostone's.
    - The id's own name can differ from the applying skill's: 111,000,071, which a
      record named Leaping Slam for, is "Prepare for Battle".
  - **Still with no name** (`aion2meter-replay --unnamed`; 40 ids in the second
    capture): permanent ones at login (4031 to 4115, 12011, 12031, 80001), short system
    marks (200 at 0.3 s with every dodge, 231, 10002 for 5 s on entering a map), and
    some of the bosses' (1604xxxx, from skills 1604xxx).
  - **The position is the caster's.** In the arena capture the opponent's effects
    (on anyone) sat on the opponent's own last movement position, 0 units off in
    the median; the local player's sat a median 222 units from the opponent, a
    melee's reach. It is the one steady source of the local player's own position.
- **Effect gone `1a 92`:** target varint, effect u32. It names the effect, not the
  instance: a stack of three is taken off by three.
- **Items outlast a death and a reload.** In the Ludra capture a Sorcerer died three
  times and the party wiped and reloaded (the same map over the same connection, the
  ids kept), and the Sorcerer used their 600 s scrolls and food again exactly as the
  first ran out. No 1a 92 came for them, and the reload sent them no 2a 38 again.
  `game::EffectBook` keeps an item's effect its target put on itself through both
  (`kept_through`); skills' effects still go with a death. A new map drops them, its
  ids being others, and the player's record on it lists them again (Identity, above).
- **Skill used `01 38`:** three bytes (00 00 00 in 1,154 of 1,176), the skill u32. Only
  the local player's own: the hit with that skill follows 0.1 to 0.5 s later.
- **Cooldown set `47 38`:** a count u8 (always 1), then per entry the skill u32 (the
  base code) and the time left in ms (varint). It comes when something cuts or resets a
  cooldown, not when one starts: every Vicious Strike hit cut Warding Strike's by 2 s
  (its specialty "−2s [Warding Strike] cooldown on hit"), and an Annihilate reset sent 0.
- **Chain skill offered `18 38` / withdrawn `19 38`:** 18 38 is the skill that set it
  off (u32, as cast), a count u8 (1 in all 437), and the skill on offer (u32); 19 38 a
  count u8 and the skill. Only the local player's: neither names anyone.
  - In the second capture, Vicious Strike offered Decisive Strike (118 times), Decisive
    Strike Desperate Strike (96), and Pummel Punishing Strike (223).
  - Every offer was withdrawn: 379 after the skill on offer was used (0.15 to 1.5 s
    after the offer), 58 when it ran out, 3.0 to 3.1 s after. The window is not sent.
- **The skill list `13 38`** (at login and on each map, not read yet): 01, skill u32,
  01, skill u32 again, level u8, 10, level − 10, four 00, 0a. Its levels are what set the
  specialty slots (8: one, 12: two, 20: three).
- **Specialties are in the cast's code.** The variant digits of the code a skill is
  used with (01 38, 04 38) are the specialties picked: Warding Strike at level 13 is
  cast as 12350130, specialties 1 and 3 (their slots on questlog.gg, aion2.app's
  order on its page). On every skill
  whose level the capture gave, the count of picks matched the slots.
- **Seen, not read:** `1b 92` is a party member's HP and MP (id, cur and max HP varints,
  MP u32 twice; 21,750 / 23,170), `1c 92` their position and legion name; `4a 36` the
  local player's stats (id, count u8, then stat u16 and value u32 pairs); `2a 37`
  and `1d 37` movement of some other shape (`1d 37` is the most frequent packet
  after the heartbeat, 8 to 12 bytes: steps as small differences, perhaps).

**Positions** (the Combat Power plugin's distances; all 12,300 movement records of
the three captures read as a point on their map):
- **Units** are Unreal's: centimetres. The arena's two starting corners were 135 m
  apart, and two players in melee 1 to 2 m.
- **Movement:** an id varint, a few bytes, then x, y, z (f32 each); what follows (a
  heading, a speed, a destination) is not read. The bytes between differ by opcode
  (`position_header`):
  - `1a 37` (they stopped): two, 00 00.
  - `1b 37`, `1c 37` (they set off, they turn or go on) and `29 37` (a path, mostly
    monsters'): a flags byte, and one more when its lowest bit is set. 05 03 for a
    player's, 04 for a monster's, 03 03 or 02 on 29 37.
  - `28 37` and `1e 37`, by the same rule (07 03, 05 03 or 04; 11 03 or 14). `28 37`
    is players flying, most likely: most of the map-20 capture's 107 came from
    players following the Abyss's Flight Circles, ten a second. `1e 37` came from
    the dungeon capture's party (213 of 224); what sets it apart is not known. Of
    the four captures' 172 and 224, 171 and 213 sat within 20 m of where that entity
    was last placed, and every one was a point.
  - `2e 37` (a jump): six. `2f 37` (in the air): one.
  - `3c 37` (a skill moved them: a charge, a pull, with the skill's code after the
    point): one.
- **Everyone's but the local player's.** The server does not send a player their own
  steps: in the arena match the opponent got 240 movement packets and the local
  player none. Of these only `3c 37` names them (9 times in the match), and in the
  other captures a stray `1a 37`, `1e 37` or `28 37`.
  - **Nor their flight.** In the map-20 capture the local player flew 100 to 400 m
    between their effects, and no packet in between carried a point on their path.
    The four that name only them (`44 37`, `41 37`, `42 37`, `22 37`) are 2 to 4
    bytes.
- **A player's record** carries where they stand, after its "class" u32: a byte, a
  flags byte whose low four bits are 2 or 6 (D2, 52, 92, 12, D6), then x, y, z. In
  `45 36` that is straight after the class; in `33 36`, 154 bytes on. So the local
  player's position is known on entering each map.
  - **On map 20 one byte more comes before the flags byte,** in both records. In
    45 36 it is the faction again, straight after the class. In 33 36 it is in a
    list that ends some 20 bytes before the position (`05 01 03 01 7f ...` where
    other maps had `05 03 01 7f ...`), the byte before the flags 80 still. The
    reader finds the flags byte in either place (`read_identity_position`); read two
    bytes after the class as on other maps, that capture's records put their
    players at or near the origin.
  - **The byte before the flags never passed as one:** 00, 80, 88 or C0 in every
    record of the captures. Should one, the point decides: read a byte early, a
    point's floats are cut across, and an axis comes out under a unit (-47,736 read
    as 0.0009), so the reading whose every axis is a unit or more is taken.
  - A position within a unit of the origin on every axis is not believed.
- **A pressed cast's tail** (`02 38`, mode 2) is a heading (f32, degrees) and a point:
  the target's last position to the unit in many records, some metres from it in
  others. An aim point, perhaps; not read.

**Unknown or guessed. Each needs a real capture (skill `decode-capture`):**
- whether the "optional 00 pad" can be confused with a scalar of 0;
- **the clock in the pong.** That `03 36` echoes Unreal's clock (base 16,777,216 s) is
  read off two captures' figures, and has not been held against the game's own ping
  display or a live run. The Ping plugin shows nothing rather than a ping outside 0 to
  5 s, so a wrong base shows as no ping, with the figure it read on the plugin's page.
  What the pong's first two bytes are (00 00 in all 95) is unknown too;
- multi-record `04 38` packets;
- misses, and the target's endurance, regeneration and invincibility (above);
- where the class is in 33 36 / 45 36, and what their "class" u32 and 33 36's extra
  u16 hold (`game/class_codes.cpp` is empty; skill codes give the class instead);
- **around the combat power** (Identity, above): where the local player's own is,
  what the tag, the two u32s before it and the stray bytes around it are (the
  frame's length, perhaps, where a byte follows the zeros), and whether a battlefield
  or another arena sends the record whole as the one-on-one arena did. The Combat
  Power plugin shows "-" for a record whose end does not read;
- **the local player's position between their skills.** Nothing found streams it
  (above), so the Combat Power plugin's distances are from where they last cast an
  effect, and none once that is stale. Whether some packet does (`1d 37`? a
  correction when they are pushed?) needs a capture of walking with no skill used,
  against a player standing still. Flying sends none (above);
- the movement packets not read: `1f 37` (118 in the four captures) holds a point,
  but 46 of them sat 20 to 200 m from where the entity was last placed (a
  destination, perhaps); `23 37`, `24 37`, `2b 37` and `2a 37` do not read as one
  rule;
- what the point in a pressed cast's tail is (above);
- what `47 36`'s two bytes after the id say (Left view, above): the first looks
  like a reason, 1 in most;
- tick effects 08 and 38;
- in `02 38`, the byte before the skill (8, 12 or 32 on a few records), and why two
  self-buffs come back in mode 0;
- what `0e 92` (an id and a byte, as often as 1a 92) means, and the byte before 2a 38's
  kind;
- **the cooldown reduction.** The game shortens cooldowns by the Illusion [Kaisinel]
  stat (0.1% a point, capped at 60%, per metabot.gg). The table's figures, less the
  specialties picked, do not explain the second capture alone: Shield Smite (10 s, no
  cut picked) came back after 8.85 s, which needs 11.5% or more, while Warding Strike's
  exact 47 38 figures (25 s after its cut, done in 23.1 s) allow about 7.6%. Which
  `4a 36` stat is Illusion is unknown. The Auras plugin learns each skill's figure
  instead (plugins/auras/AGENTS.md, Cooldowns);
- whether a skill with a second use ("+1 consecutive use") recharges each use on its
  own, as the Auras plugin assumes, or both at once; and which use a 47 38 for it
  speaks of (the plugin takes the one cooling that is back soonest, never a ready one);
- what a wipe looks like on the wire. The whole party at HP 0 is not one: in the second
  capture's last boss fight they all were, for 1.2 s, and got up again and won
  (plugins/combat_meter/AGENTS.md, Fight end). `wipe_detection` stays off until then.
  **How the boss resets after one** was seen in the Ludra capture (two wipes on Ludra,
  max HP 85,640,625): a 00 8D at exactly its max HP. In one pull it came 30 s after
  the party's last hit; in the other, in the same 100 ms as their last hits, which
  were in flight as the last of them died. Each wipe also sent the same map's 21 36
  over the same connection, as the party went back to the entrance. A finished
  fight keeps the boss's HP from the party's last hits (plugins/combat_meter/AGENTS.md,
  Boss). **With two parties on Ludra** every wipe showed as a kill (the user, no
  capture): what the boss's reset sends then is unknown. The meter now counts a
  boss-sized death as a kill only when its HP came down to it;
- whether the game's Total Heal counts heals before a fight's first hit: the meter's
  healing came to 97% of it;
- **the damage Theostones' codes** (Skill codes, above): read off how often and how
  hard they hit, not stated by any source. Tier 2's (`30002xx`, the 15,000 stones)
  have not been seen. Whether a Poison or Bleed Theostone's tick (05 38) carries
  `3000T15` / `3000T16` or its effect's `3001T15` / `3001T16` is not known either;
- what the 5-HP object (2,920,366, kind 14, "Detoxifying Rafflesia") the party broke in the second
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
- **which instances below 600000 only a group gets into** (Map load, above). The
  matched dungeons (156xxx, one to four players), Ascension, the seal dungeons and
  Growth may well be, but whether players outside the group share them, and of
  which faction, is not known: `is_party_instance` leaves them out, as it does the
  arenas, so the meter counts no stranger as party there and the Combat Power
  plugin marks the other faction. Whether the Abyss Corridor is PvP is not known
  either. None of these map ids has come in a capture but the arena's and
  Chaotic Reshanta's;
- whether the server hands a player's old id to someone else. Ids belong to a map
  instance and are handed out on entering it: in the second capture the local
  player's id was new on each of its three maps, while players who stayed in town
  kept theirs across the 14 minutes the local player spent in the dungeon (22 of 23; one
  came back with a new id). A party whose Ranger and Elementalist shared one row,
  the name switching from pull to pull, points to reuse. The meter assumes both
  (plugins/combat_meter/AGENTS.md, Sides).
- whether a teleport or a revive within a dungeon sends a 21 36, and over which
  connection. A wipe does: the same map, over the same connection (above).
  Re-entering the same dungeon straight away did, as the runs that shared
  one visit showed, and runs then split once every load was taken for a new one. The
  plugins take the same map over the same connection for a reload within a run, and
  over a new connection for a new run, since entering a dungeon opened one in the
  second capture. A run still split in two, or two runs in one, would prove it
  wrong;
- **the shard:** a damage buff of 5% to 10%, by its tier, that a player switches on and
  off (the user). No effect in `effects.tsv` is called one, and nothing timed of it
  shows as a buff. Permanent effects only the local player's own record lists, with
  no name: 40,001,011 and 40,002,011, on every map of the three November captures and
  not in October's; and 100,000,401, in the Ludra capture (17 times). Whether one is
  the shard needs a capture with it switched on and off;
- **the byte some listed effects carry before their position** (Identity, above), and
  what decides it;
- **what the faction does not say.** A player of the other faction is told apart
  (Identity, above), and the Combat Power plugin marks them. Still open: who is an
  enemy where the faction is not the line (an arena's opponent of the local
  player's own, a battlefield's teams), and whether any map but a dungeon mixes
  the factions as allies. The Combat Meter does not use the faction yet: to it
  they are players like the party, so a PvP fight starts nothing and a hit on them
  counts for nothing (plugins/combat_meter/AGENTS.md, Party and Healing);
- what the extra byte in a player's record on map 20 marks: map 20 is Chaotic
  Lower Reshanta, the Abyss, where both factions meet;
- **whether the local player's position always sits 154 bytes on in 33 36.** The
  bytes before it end in a list, a byte longer on map 20. A faction-2 character's,
  or one with more in that list, may run longer still, and the reader looks in two
  places only. Every own record of the captures is one faction-1 character's;

**The client's own files cannot fill in names.** They were checked on the Steam install
in 2026-10.
- **Format:** UE5. Every `.pak` (v11) has an AES-encrypted index, and 245 of the 282
  IoStore `.utoc` containers are flagged encrypted, all under the default key id, which
  Unreal builds into the executable.
- **NC's tables:** NC's own tables (`StartUpData/Table/*.dat`, `L10N/<lang>/L10NString.dat`)
  sit next to a `key_manifest.dat`, so a second layer of encryption is likely.
- **Instead:** names come from public databases (questlog.gg, aion2.app) and from
  captures.

---

## 🧩 Plugins

Each feature the capture feeds is a plugin, laid out as ffxiv-hub's are: one exe, one
capture, one tray and one updater, around them.

| Plugin | Directory | What | Default |
|---|---|---|---|
| Combat Meter | `plugins/combat_meter` | fights: damage, healing, damage taken, the boss, the history; the meter overlay | on |
| Auras | `plugins/auras` | effects on the local player and theirs on their target, their cooldowns; the aura overlay | off |
| Ping | `plugins/ping` | the latency to the game server off the game's own pong, the heartbeats' jitter, a graph; the ping overlay | off |
| Combat Power | `plugins/combat_power` | other players' combat power as the game sends it: the target's, and the strongest in view (all of an arena's or a dungeon's), or the nearest, the other faction's first and in red; the combat power overlay | off |

- **One table names them.** `include/aion2/plugin_registry.hpp` holds each plugin's id,
  name, description, config section and default switch; the sidebar, the dashboard,
  the plugin pages and `config.json` read them there and nowhere else.
- **A plugin is a `game::Plugin`.** It takes parsed events and capture-time ticks on
  the engine thread, and keeps its own record of whatever it needs (the Auras plugin
  its own `Entities`, not the meter's registry). It depends on `src/protocol` and
  `src/game` only, never on another plugin, which the include roots enforce. Shared
  game knowledge (names, classes) goes in `src/game`.
- **Every plugin is switchable.** Its master switch is `plugin_enabled` in its config
  section, on its page's header (`render_plugin_header`); while off, the page shows
  only the panel that turns it back on (`render_plugin_disabled_gate`). Off means the
  pipeline feeds it no events and no ticks, and its overlay hides. Switching it off
  takes effect on the engine thread through `AppRuntime::update_settings`
  (`Pipeline::set_plugins`): the meter ends its live fight, the auras, the ping and
  the combat power forget everything.
  - **The meter switched back on is handed where it is.** While it is off the
    pipeline keeps the last map load, the last own `33 36` and the last party roster
    (`keep_for_meter`), and hands them over, in the order they came, as it comes back
    on. It kept the map it was switched off on: back on in the open world after a
    dungeon, its fights were filed under the dungeon and strangers counted as party.
    The rest it learns as a meter started mid-visit does.
- **A plugin's page** opens with its header and switch, then its own settings, then its
  overlay's, as ffxiv-hub orders them. Overlays it draws are separate windows of the
  app (`win32/overlay_window`), each shown by its plugin's switch and its own hide
  conditions (`overlay_visibility`). Ctrl+Alt+L locks every overlay: that is the app's.
- **The dashboard stays plugin-agnostic:** capture, the plugin list from the table,
  recording, files. A plugin's figures belong on its own page.
- **A new plugin** gets a descriptor in the table and its switch in `PluginSwitches`, a
  directory under `plugins/` with its `AGENTS.md` and `README.md`, its layer in the
  Makefile's `layer_inc` and in CMake, its member in `Pipeline`, its page, and its keys
  in its own config section.

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
- **The installer is beside the contract, not in it.** A release also carries
  `aion2-combat-meter-windows-x64-setup.exe` (`packaging/windows/installer.nsi`, NSIS,
  built by CI's `installer` job around build-windows' exe). It installs for the user
  alone, with no administrator prompt, into `%LOCALAPPDATA%\Programs\AION 2 Combat
  Meter`: a folder of the user's own, so Update & restart swaps the exe there as in
  one the zip was extracted to. The updater never downloads it.
  - It adds a Start menu shortcut and an entry in Windows' app list (HKCU's
    `Uninstall\AION2CombatMeter`). An in-app update leaves that entry's version
    behind, so each start sets it (`os::refresh_install_entry`), when the entry's
    `InstallLocation` is the running exe's folder: a copy elsewhere leaves it alone.
  - It does not bundle Npcap, whose licence forbids it: when `System32\Npcap\wpcap.dll`
    is missing it says so and offers npcap.com.
  - Uninstalling takes the exe, the updater's staging folder and `.old` backups, and
    Start with Windows when it starts this copy. `%APPDATA%\aion2-combat-meter` stays
    unless the user says otherwise (never on a silent uninstall): every copy shares it,
    and its fights hold character names.
  - It refuses while the meter runs, any copy of it (the app and overlay windows'
    classes, `FindWindow`): the exe could not be replaced, and another copy's
    single-instance lock would keep the installed one from starting.

---

## 🧪 Verification

| Command | Where | What |
|---|---|---|
| `make test` | macOS / Linux | Tests, header check, ImGui syntax check, forbidden-API check, replay smoke test. |
| `make tsan`, `make asan` | macOS / Linux | The same tests under the sanitizers. |
| `make screenshots` | macOS (zlib) | Re-renders `docs/images`. |
| `make gif` | macOS (zlib, ffmpeg) | Re-renders the README's animation, `docs/images/overlay-fight.gif`: the meter's overlay through the made-up fight. |
| `make fixtures` | anywhere | Your own captures against their `.expected.txt`. |
| `aion2meter-replay --deaths --active --effects` | anywhere | A capture's fight detail, to check against what happened in game. |
| `aion2meter-replay --ping` | anywhere | A capture's pongs, and per connection how their echo and the heartbeats spread. |
| CI `build-windows` | GitHub | The only MSVC build: the app, ctest, the replay smoke test, the zip. |
| CI `installer` | GitHub (windows-2022, which has NSIS) | The installer around that exe, installed and uninstalled on the runner (`try-installer.ps1`). The release job takes the SHA-256 of every asset. |

**Conventions:**
- Naming: `snake_case` functions, `PascalCase` types, `m_` members, `kName` constants.
- Comments: `///` doc comments that say why, not what.
- Commits: Conventional Commits.
- Releases: bump `include/aion2/version.hpp`, then push a `vX.Y.Z` tag; CI checks the two
  agree and publishes the release, which installed copies update to (skill `release`).
