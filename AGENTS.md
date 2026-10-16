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
handle to another process; keep it that way. AION 2 has anti-cheat, and the user cares
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
    os/          logger, paths, single_instance, unique_handle, local_time (ported), tray_icon
    bounded_queue.hpp, snapshot_slot.hpp, micros.hpp, time_format
src/capture/     aion2::capture
    capture_file (pcap + pcapng reader), pcapng_writer, packet_headers, tcp_reassembler,
    packet_source + file_source, os/npcap_source, os/connection_finder
src/protocol/    aion2::proto
    byte_reader, lz4_block, frame_decoder, opcodes.hpp, events.hpp, entity_id, identity,
    packet_parser
src/meter/       aion2::meter
    types, class_codes, combatant_registry, encounter_engine, meter_settings,
    fight_json, history_store
src/pipeline/    aion2::pipeline   stream_demux (flows -> frames), pipeline (all of it)
src/app/         aion2::app
    app_settings, app_runtime (threads, history, recording), display_format,
    overlay_view (ImGui, behind HAVE_IMGUI)
    src/win32/   main.cpp, overlay_window: Windows only
tools/replay/    aion2meter-replay (every platform)
tools/synth/     aion2meter-synth: writes the tests' made-up boss fight as a pcapng
tools/screenshots/  renders docs/images from overlay_view (software rasterizer from ffxiv-hub)
tools/check_forbidden_apis.sh, tools/gen_app_icon.py
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
| UI (`wWinMain`) | the window, ImGui, the tray, `AppSettings` | reads snapshots and the history index (copied under a mutex). Requests go to the engine as atomics: end fight, recording on/off. |

`make tsan` runs the suite under ThreadSanitizer. The `AppRuntime` tests drive real
threads with a FileSource.

---

## 📡 Protocol (Global client; from the community's notes, unverified here)

All of it lives in `src/protocol`. `opcodes.hpp` is the one place opcodes are spelled.

**Transport**
- TCP, server port 13328.
- Only server→client is decoded; it is not encrypted. Client→server is encrypted and
  ignored.
- Through a ping reducer or VPN the game appears on loopback, on another port. The demux
  takes any flow once 3 heartbeats prove it is the game (`detect_relays`).

**Framing**
- A frame is `varint len, [flag 0xF0..0xFE], opcode (2 bytes, first byte high), body`.
- Its size on the wire is `len + varint_bytes − 4`.
- 0x00 bytes between frames are padding.
- An opcode of `FF FF` means `u32 raw_size` and an LZ4 block follow, decompressing into
  more frames. Nesting is capped at 2.

**Heartbeat**
- The frame is `0E 00 36` plus a u64 LE of server Unix ms, about every 50 ms.
- The decoder resyncs on one whose time is within a day of the capture's.
- 3 s with no heartbeat while bytes keep flowing means the decoder is misaligned, and it
  resyncs.

**Body layouts** (`packet_parser.cpp`)
- **Damage `04 38`:** target varint, switch varint, flag varint, actor varint, skill
  u32, hit uid u8, damage type varint, then the block.
  - **Switch:** the low 4 bits are the layout (4–7; 0 marks a cast, not damage). Bit
    0x20 means an extra-hit list follows.
  - **Damage type:** 3 is a crit.
  - **Block:** 8 bytes for layout 4, 11 bytes for layouts 5–7. Byte 0 is the modifiers
    (0x02 parry, 0x04 perfect, 0x08 double, 0x20 heavy). Byte 2 is the direction
    (1 back, 2 front).
  - **Damage:** an optional 00 pad, then a scalar varint, then the damage varint.
  - **Tail:** for layout 4, one varint. With 0x20, a count and then that many extra-hit
    varints.
  - More records may follow, each behind `01 00`.
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
    triple, `01`, cur_hp varint and max_hp varint.
  - **Owner:** the u32 after `07 02 06`.
- **Other opcodes:**
  - Death `42 36`: id varint.
  - Map load `21 36`: load count u32, map id u32. Ids 600000–700000 are instances.
  - Party `02 97`: names only, found by scanning, since each member's tail varies in
    length.
  - Remaining HP `00 8D`: layout unknown; kept raw for `--unknown`.

**Unknown or guessed. Each needs a real capture (skill `decode-capture`):**
- whether the "optional 00 pad" can be confused with a scalar of 0;
- whether `npc_code` is the u24 right after the spawn mask (the notes only say "within 60
  bytes before the marker");
- the class codes (`meter/class_codes.cpp` is empty);
- the layout of `00 8D`;
- the right `boss_min_max_hp`;
- whether the folded id range really holds every NPC (the registry guesses enemies from it
  when nothing else is known);
- the game's executable name (`Aion2.exe` is reported by third-party guides).

---

## ⚔️ Meter rules (`src/meter`)

- **Sides** (`CombatantRegistry`):
  - Identity packets make players. Spawns make NPCs, or player objects for summon and
    effect kinds.
  - A summon's side is the player side, whoever owns it.
  - Ids the meter was never told about get their side from the hits they trade: PvE
    damage only crosses sides.
  - Hits between two unknown ids are held (up to 4096) and settled when either end
    becomes known.
  - With no anchor at all, an id in the folded range is guessed to be an enemy.
  - Two ids with the same name@server are one player (aliases).
  - A map change drops NPCs, summons and inferred sides. Players and names stay.
- **Party:** with no roster, every player counts. With one, only roster names do.
  Non-party hits still lower the boss's HP estimate.
- **Fight start:** a direct hit that lands, from the player side on an enemy. A DoT tick
  never starts a fight.
- **Fight end:** the boss's death (after `kill_grace_s`), a map change (not a reload of
  the same map, which is a teleport), idle time with no party hit
  (`idle_timeout_s`, or `boss_idle_timeout_s` while a boss stands), no traffic
  (`capture_lost_s`), a wipe (when enabled) or by hand.
  - Duration runs from the first hit to the last, floored at 1 s for DPS.
- **Boss:** the damaged enemy with the most max HP. It is a boss if that max HP is at
  least `boss_min_max_hp`.
  - Its HP is the spawn HP minus all damage seen since the spawn.
  - **Partial:** a killed boss that took less than `partial_damage_ratio` of its spawn HP
    in seen damage marks the fight partial.
- **History:**
  - What is saved: boss fights, and fights of at least `min_saved_duration_s`.
  - Each fight is one JSON file (`"v":1`), written atomically, plus a line appended to
    `index.jsonl`.
  - A cut-off index line is skipped. A missing index is rebuilt from the files.
  - Pruning keeps `history.max_fights`, in batches of 50.

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
