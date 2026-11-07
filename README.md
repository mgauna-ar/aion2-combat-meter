# AION 2 Combat Meter

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D6)
![Game](https://img.shields.io/badge/AION%202-Global%20NA%2FEU-8A2BE2)
![C++20](https://img.shields.io/badge/standard-C%2B%2B20-crimson)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

**A party DPS meter for AION 2 that draws on top of the game, without touching it.**

It reads the game's own network traffic as it reaches your PC and works out who hit
what, with which skill, for how much. What it does with that comes in two plugins,
each with its own page in the app window and its own switch:

- **[Combat Meter](plugins/combat_meter/README.md)** (on): what the game's own Combat
  Analysis window (Ctrl+X) shows, but for every party member. An overlay over the game
  has the live fight: the party ranked by DPS, the boss's HP and the fight timer. The
  app window has the details: damage, hit counts, crit, perfect, double, back and front
  rates for each player and each of their skills; healing; damage taken; each fight
  second by second, its deaths and how long every buff and debuff was up; and every
  saved fight, grouped by visit to a map, one entry per dungeon run.
- **[Auras](plugins/auras/README.md)** (off until you switch it on): your buffs and
  debuffs and yours on the enemy you fight, each as a bar with the time it has left,
  your cooldowns, and a warning when a buff you watch for (a Chanter's mantra, say) is
  not on you, in an overlay of their own.

![The overlay during a boss fight: three players ranked by DPS, the boss's HP bar and the fight timer](docs/images/overlay-live.png)

[Status](#-status) · [Account safety](#%EF%B8%8F-account-safety) · [Quick start](#-quick-start) ·
[Plugins](#-plugins) · [Controls](#-controls) · [Configuration](#%EF%B8%8F-configuration) ·
[Troubleshooting](#-troubleshooting) · [Recording a capture](#-recording-a-capture) ·
[Building](#-building-from-source)

---

## 🚧 Status

Version 0.4. The decoder started from the community's notes on the Global client's
protocol and has been checked against two real captures: 30 seconds of open-world
mobs, then a full dungeon run with three boss kills.

**Checked against the game's own Combat Analysis (Ctrl+X):** for the last boss fight of
that run, your damage, hits and every per-skill CRI, PERF, DOUB, BACK, FRNT, MULT and
BLOC share matched the game's window exactly. Healing came to 97% of the game's Total
Heal. Dungeon and boss names were right, and so were the classes from skill codes.

**Auras:** effects' bars are exact, from the server's own end times; cooldowns are
estimates, marked "~", which ran at most 0.65 s long over 274 uses on the dungeon
capture ([how exact](plugins/auras/README.md#how-exact-it-is)).

**Still open** (more captures help, `.claude/skills/decode-capture`):
- **Misses and defences:** the game also counts missed hits (ACC) and the target's
  endurance, regeneration and invincibility. The capture had none of them, so where
  they live is unknown.
- **Map names:** dungeons are named from a public database's dungeon ids
  (`src/game/data/maps.tsv`), which the game turned out to send. Open-world zones have
  no public ids, so they show theirs ("Map 1010") until named by hand. So do the newer
  dungeons the database gives no id for yet, such as the Lv.99 Awakening tiers and
  Ascension.
- **Boss threshold:** an enemy with 1,000,000 max HP or more counts as a boss
  (`meter.boss_min_max_hp`). One dungeon's bosses had 5.2M–9.9M, its trash under
  450,000 and open-world mobs about 120,000; other dungeons are unchecked.
- **PvP:** the meter cannot yet tell a player of the other faction from your party, so
  it counts no PvP damage. A capture of a PvP fight would show how to.

## ⚠️ Account safety

**Third-party tools may break AION 2's terms of service. Use this at your own risk.**
NCSOFT has not approved any third-party meter, and its operation policy forbids
"unauthorized programs" in broad terms.

The meter is built to stay passive, and a check in the build fails if any code breaks
these rules:

- **It never opens the game.** No process handle is opened, no memory is read or
  written, nothing is injected and nothing is hooked. The game's connection is found
  through Windows' TCP table and a list of running processes, and neither of those opens
  a handle to the game.
- **It never sends anything.** No packets, no keystrokes, no mouse input. Npcap is used
  read-only, in non-promiscuous mode, filtered to the game server's side of the
  connection.
- **The overlay is an ordinary window.** It is a separate topmost window, like Discord's
  or OBS's. Nothing is drawn inside the game.

- **Its one connection is the update check, to GitHub.** Once, soon after it starts, the
  meter asks GitHub whether a newer release is out, sending nothing but its version.
  *Check for updates at startup* in Settings turns that off. It downloads only when you
  click **Update & restart**. There are no uploads and no telemetry, and nothing goes
  to the game or its servers.

## 🚀 Quick start

1. **Install [Npcap](https://npcap.com/).** Tick **"Support loopback traffic"** so ping
   reducers and VPNs work too. Leave **"Restrict Npcap driver's access to
   Administrators only"** unticked, or the meter has to run as administrator. The
   licence does not allow bundling Npcap, so it is a separate install.
2. **Get the release.** Download `aion2-combat-meter-windows-x64.zip` from
   [Releases](https://github.com/mgauna-ar/aion2-combat-meter/releases/latest), check it
   against `SHA256SUMS.txt`, and unzip it into a folder of its own, anywhere you can
   write to: the meter updates itself there. The zip holds `aion2-combat-meter.exe`
   alone. The replay tool, `aion2meter-replay.exe`, is a separate download on the same
   page, for troubleshooting.
3. **Set the game to Borderless Windowed.** An exclusive fullscreen game hides every
   other window, this one included.
4. **Run `aion2-combat-meter.exe`.** The app window opens, the meter overlay shows up
   in the top-left corner, and the meter sits in the tray. Hit something. For the aura
   overlay, switch the Auras plugin on, on its page.

The exe is not code-signed, so SmartScreen or antivirus software may warn about it.
Capture tools often get flagged. CI builds every release from source, and the zip's
SHA-256 is published with it.

## 🧩 Plugins

| Plugin | Shows | At first |
|---|---|---|
| [⚔️ Combat Meter](plugins/combat_meter/README.md) | The party's damage, healing and damage taken, fight by fight; the meter overlay and the saved fights. | On |
| [✨ Auras](plugins/auras/README.md) | Your buffs, debuffs and cooldowns, and yours on the enemy you fight; the aura overlay. | Off |

Each has a page in the app window's sidebar, with its switch at the top right. A plugin
switched off reads nothing and draws no overlay; its page offers only the button that
turns it back on. Each plugin's README covers its overlay, its page and its settings.

## 🎮 Controls

| Hotkey | Does |
|---|---|
| Ctrl+Alt+L | Lock or unlock every overlay. Locked, clicks pass through to the game, and the overlays cannot be moved. |
| Ctrl+Alt+H | Hide or show the meter overlay. |
| Ctrl+Alt+R | End the current fight now. |

**The app window** opens at start, and from the tray icon. Its sidebar has a page for
each plugin between two of the app's own:
- **Dashboard:** what the capture sees, which plugins are on, recording, and the
  meter's files.
- **Combat Meter** and **Auras:** each plugin's page.
- **Settings:** the window, Start with Windows, notifications, the capture and updates.

Changes are saved as you make them. Closing the window hides it to the tray (or quits,
with *Close to tray* off); starting the meter again while it runs opens it.

The tray icon's menu opens the window, does all of the above, and also opens the
history and recordings folders, `config.json` and the log.

## ⚙️ Configuration

The settings live in `%APPDATA%\aion2-combat-meter\config.json`. The app window's
Settings pages edit them and save as you go; the file is written with defaults on
first run, and is also saved when you move, resize, lock or hide an overlay. Edit it
by hand only while the meter is closed. Out-of-range values are clamped.

**app**

| Key | Default | Meaning |
|---|---|---|
| `open_window_at_start` | true | Open the app window when the meter starts; off, it starts in the tray. |
| `close_to_tray` | true | Closing the app window hides it to the tray. Off, closing it quits. |
| `notifications` | true | Tray notifications: a capture that failed to start, a recording started, a new version. |
| `check_for_updates` | true | Ask GitHub for a newer release once, 10 seconds after the meter starts. **Check now** in Settings works either way. |
| `window_x`, `window_y`, `window_width`, `window_height` | -1, -1, 1280, 760 | The app window's position and size in screen pixels; -1, -1 centres it, at no more than 90% of the screen. |

*Start with Windows* is not in the file: Windows' Run key for your user is its only record.

**capture**

| Key | Default | Meaning |
|---|---|---|
| `server_port` | 13328 | The game server's TCP port. |
| `game_process` | `Aion2.exe` | The game's executable, used to find its connection. Third-party guides report this name; nothing official confirms it. If it is wrong, the meter still captures port 13328 on every adapter, but it cannot follow a ping reducer's relay. |
| `detect_relays` | true | Also watch other TCP flows for the game's heartbeat, for ping reducers and VPNs that relay the game on another port. |

**Each plugin's own keys** are in its section of the same file, and in its README:
[Combat Meter](plugins/combat_meter/README.md#configuration) (`meter`, `overlay`,
`history`) and [Auras](plugins/auras/README.md#configuration) (`auras`). Each section's
`plugin_enabled` is that plugin's switch.

**Files the meter writes:**
- Saved fights: `%APPDATA%\aion2-combat-meter\history\`, one JSON file per fight plus
  `index.jsonl`.
- Recordings: `captures\`, in the same folder.
- The log: `meter.log`, in the same folder. The previous run's log is kept as
  `meter.prev.log`.

## 🩺 Troubleshooting

**The overlay says WAIT and "Npcap is not installed"**
Install Npcap (see Quick start), then restart the meter.

**It says "cannot open any adapter"**
Npcap was installed for administrators only. Reinstall it with that option unticked, or
run the meter as administrator.

**It says WAIT although I'm in game**
- **Ping reducer or VPN (ExitLag, GearUP and the like):** Npcap needs its loopback
  support (reinstall with it ticked). Also check `capture.game_process`.
- **Another port:** if the game moved off port 13328, record a capture in Wireshark
  and run `aion2meter-replay` on it (a separate download on the release page). It lists
  every flow it decoded as the game.

**I can't see the overlay in game**
Use Borderless Windowed, and check its plugin is switched on. If the meter overlay is
hidden, press Ctrl+Alt+H. Shown only in combat or only in dungeons, or hidden while the
game is not in front (its visibility settings), a locked overlay stays out of sight
until those allow it; unlock it (Ctrl+Alt+L) to see it at once. A dungeon the meter
cannot name ("Map <number>") counts as not one.

**My party shows as #12345 instead of names**
The game sends names mostly on loading screens. Start the meter before you log in or
teleport, and the names will appear after the next loading screen. Until then your own
row shows as **You**.

**Only my own row shows**
Out of a party, the open world lists only you. In a party, the meter learns who is in
it from the party list the game sends; if the meter started after the party formed, it
may have missed it. Start the meter before you form or join a party.

**The boss fight says "partial data"**
On crowded world bosses the server stops sending most players' hits, so the meter
cannot see the whole fight. Only the hits it did see are counted.

**How do I update?**
When a new version is out, a tray notification says so and the sidebar shows **Update
available**. Open **Settings → Updates** and click **Update & restart**. The meter
downloads the release, checks it against the SHA-256 GitHub publishes for it, replaces
its executable and restarts; a fight still going is not saved. **Check now** checks
at any time.
- The old executable stays next to the new one as `*.old` until the next start.
- The replay tool is not part of an update: download it again from the release page
  when you need a newer one.
- If the update can't write to the meter's folder (under `Program Files`, say), run the
  meter as administrator once, or download the zip and unzip it over the old one.
- A release with no published checksum is announced but not installed: download it from
  its release page instead.

## 🎥 Recording a capture

A capture of the game's traffic is how the decoder gets fixed and checked: replay it on
any PC with `aion2meter-replay`. **Captures hold character names. Share them only with
people you trust, and never commit them** (`.gitignore` blocks `*.pcap*`).

**From the meter:** click **Record** on the app window's Dashboard, or choose **Record
capture** in the tray menu. The file goes to `%APPDATA%\aion2-combat-meter\captures\`.

**With Wireshark (most complete):**
1. Capture on both your network adapter and "Adapter for loopback traffic capture",
   with the capture filter `tcp port 13328`.
2. Start before you log in or teleport, because names arrive on loading screens.
3. Save as pcapng.
4. Write down what you know: your character's name, a few hit numbers you saw, and when
   the boss died.

## 🛠 Building from source

**macOS or Linux (the portable core, tests and tools):**

```bash
make test
```

That runs:
- the unit tests;
- a compile of every header on its own;
- an ImGui syntax check of the overlays and the app window;
- the forbidden-API check;
- a replay of a made-up boss fight against `tests/golden/synthetic_fights.txt`.

Other targets:

| Target | Does |
|---|---|
| `make tsan` | The tests under ThreadSanitizer. |
| `make asan` | The tests under AddressSanitizer and UBSan. |
| `make replay` | Builds `./aion2meter-replay`. |
| `make fixtures` | Replays your own captures in `tests/fixtures/local/` and diffs each against its `.expected.txt`. |
| `make screenshots` | Re-renders `docs/images` from the overlay's and the app window's own drawing code. Needs zlib; set `SHOTS_FONT` and `SHOTS_BOLD_FONT` off macOS. |
| `make golden` | Rewrites the replay smoke test's reference after an intended change. |

**Windows (the app):** Visual Studio 2022 and CMake 3.22 or newer.

```bash
cmake -B build -A x64
```

```bash
cmake --build build --config Release
```

`build\bin\Release\` then holds `aion2-combat-meter.exe` and `aion2meter-replay.exe`.

You can also run the meter on a capture instead of live traffic, at recorded speed. It
works without the game or Npcap:

```bash
aion2-combat-meter.exe --replay fight.pcapng --speed 1
```

**The replay tool** runs a capture through the same pipeline as the live meter:

```bash
./aion2meter-replay capture.pcapng --fights --events --opcodes
```

| Option | Prints |
|---|---|
| `--fights` | A table per fight. |
| `--skills` | The fight tables, and each player's skills, healing and damage taken. |
| `--deaths` | The fight tables, and each fight's deaths: killing blow, time until up, the hits and heals before. |
| `--active` | The fight tables, and each player's time spent hitting. |
| `--effects` | The fight tables, and how long each buff and debuff was up. |
| `--events` | Every decoded event but effects, skill uses and cooldowns. |
| `--auras` | Effects put on and taken off, and your skill uses and cooldown changes. |
| `--opcodes` | Counts and sizes per opcode. |
| `--unknown` | Hex dumps of anything the parser does not read or failed on. |
| `--dump 0438,4136` | Hex dumps of those opcodes. |
| `--anonymize` | Every name as `#id`, for output you will share. |

`build/synth/aion2meter-synth out.pcapng` writes the made-up boss fight the tests use,
with invented names, so you can try everything without the game.

## 📡 Protocol

The meter decodes the server's side of the game connection: TCP port 13328.
- **Encryption:** none on this side. The client's side is encrypted, and the meter
  ignores it.
- **Framing:** each frame is a varint length, an optional flag byte, a 2-byte opcode and
  a body. Some frames are LZ4 bundles of more frames.
- **Heartbeat:** about twenty a second. It carries the server clock, and the decoder
  uses it to find frame boundaries again after lost packets.
- **Where the details live:** `src/protocol/include/protocol/opcodes.hpp` holds the
  opcodes. `AGENTS.md` describes the layouts and lists what is still unknown; each
  plugin's own `AGENTS.md` what it makes of them.

These facts come from the AION 2 community's published notes on the Global client. The
decoder and everything else here are written from scratch.

## License

MIT, see [LICENSE](LICENSE). Dear ImGui is MIT (`src/third_party/imgui/LICENSE.txt`). The
app window's icons are a subset of [Lucide](https://lucide.dev) (ISC), embedded in
`src/app/include/app/ui/icons_font.inl` by `tools/embed_icon_font.py`. Skill, dungeon,
monster and NPC names come from aion2.app's database (see `src/game/data/skills.tsv`,
`maps.tsv` and `npcs.tsv`), and so do skill cooldowns (`plugins/auras/data/cooldowns.tsv`).
The class emblems and skill icons are NCSOFT's own art, as aion2.app serves it, packed
into `src/app/src/game_icon_sheet.inc` by `tools/gen_icon_sheet.py`. The MIT licence
does not cover them. AION 2 is a trademark of NCSOFT. This project is not affiliated
with NCSOFT.
