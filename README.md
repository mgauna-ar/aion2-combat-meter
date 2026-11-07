# AION 2 Combat Meter

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D6)
![Game](https://img.shields.io/badge/AION%202-Global%20NA%2FEU-8A2BE2)
![C++20](https://img.shields.io/badge/standard-C%2B%2B20-crimson)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

**A party DPS meter for AION 2 that draws on top of the game, without touching it.**

It reads the game's network traffic as it reaches your PC (who hit what, with which
skill, for how much), and two plugins turn that into overlays and pages of the app window:

| Plugin | Shows | At first |
|---|---|---|
| [⚔️ Combat Meter](plugins/combat_meter/README.md) | The game's Combat Analysis (Ctrl+X) for the whole party: damage, healing and damage taken, per skill; each fight's timeline, deaths, buff uptime and casts; every saved fight. | On |
| [✨ Auras](plugins/auras/README.md) | Your buffs, debuffs and cooldowns, and yours on your target, as bars; a warning when a buff you watch for is missing. | Off |

![The overlay during a boss fight: three players ranked by DPS, the boss's HP bar and the fight timer](docs/images/overlay-live.png)

[Status](#-status) · [Account safety](#%EF%B8%8F-account-safety) · [Quick start](#-quick-start) ·
[Controls](#-controls) · [Configuration](#%EF%B8%8F-configuration) ·
[Troubleshooting](#-troubleshooting) · [Recording a capture](#-recording-a-capture) ·
[Building](#-building-from-source)

---

## 🚧 Status

Version 0.5, checked against two real captures: open-world mobs, and a dungeon run with
three bosses. On the last boss, your damage, hits and every per-skill CRI, PERF, DOUB,
BACK, FRNT, MULT and BLOC share matched the game's Combat Analysis exactly; healing came
to 97% of its Total Heal. Aura bars are exact; cooldowns are estimates, marked "~"
([how exact](plugins/auras/README.md#how-exact-it-is)).

Not there yet (a [capture](#-recording-a-capture) helps):
- **Misses and defences** (ACC, endurance, regeneration, invincibility) are not decoded.
- **Open-world zones**, and dungeons with no public id yet (the Lv.99 Awakening tiers,
  Ascension), show by number: `Map 1010`.
- **Bosses** are enemies with 1,000,000 max HP or more (`meter.boss_min_max_hp`),
  checked on one dungeon only.
- **PvP:** the other faction looks like your party, so PvP damage is not counted.

## ⚠️ Account safety

**Third-party tools may break AION 2's terms of service. Use this at your own risk.**
NCSOFT has approved no third-party meter, and its policy forbids "unauthorized programs"
in broad terms.

The meter stays passive, and the build fails if any code breaks these rules:
- **It never opens the game:** no process handle, no memory read or written, nothing
  injected or hooked. It finds the game's connection in Windows' TCP table.
- **It never sends anything:** no packets, no keystrokes, no mouse input. Npcap only
  reads, non-promiscuous, filtered to the game server's side.
- **The overlays are ordinary windows,** topmost like Discord's or OBS's. Nothing is
  drawn inside the game.
- **Its one connection is to GitHub:** an update check at start (sending only its
  version), and a download when you click **Update & restart**. No uploads, no
  telemetry.

## 🚀 Quick start

1. **Install [Npcap](https://npcap.com/)** (its licence forbids bundling it). Tick
   **Support loopback traffic**, for ping reducers and VPNs. Leave **Restrict Npcap
   driver's access to Administrators only** unticked.
2. **Download** `aion2-combat-meter-windows-x64.zip` from
   [Releases](https://github.com/mgauna-ar/aion2-combat-meter/releases/latest), check
   it against `SHA256SUMS.txt`, and unzip it into a folder of its own that you can
   write to: the meter updates itself there.
3. **Set the game to Borderless Windowed.** Exclusive fullscreen hides every other window.
4. **Run `aion2-combat-meter.exe`** and hit something. For the aura overlay, switch the
   Auras plugin on, on its page.

The exe is not code-signed, so SmartScreen or antivirus software may warn about it, as
they often do with capture tools. CI builds every release from source.

## 🎮 Controls

| Hotkey | Does |
|---|---|
| Ctrl+Alt+L | Lock or unlock every overlay. Locked, clicks pass through to the game. |
| Ctrl+Alt+H | Hide or show the meter overlay. |
| Ctrl+Alt+R | End the current fight now. |

The app window opens at start and from the tray icon. Its sidebar has the
**Dashboard** (capture, plugins, recording, files), a page per plugin, and **Settings**
(window, Start with Windows, notifications, capture, updates). Changes save as you make
them. Closing the window hides it to the tray, whose menu also opens the history and
recordings folders, `config.json` and the log.

## ⚙️ Configuration

The settings live in `%APPDATA%\aion2-combat-meter\config.json`, written with defaults
on first run. The app window saves them as you go; edit the file by hand only while the
meter is closed. Out-of-range values are clamped.

**app**

| Key | Default | Meaning |
|---|---|---|
| `open_window_at_start` | true | Open the app window at start; off, the meter starts in the tray. |
| `close_to_tray` | true | Closing the app window hides it to the tray; off, it quits. |
| `notifications` | true | Tray notifications: a capture that failed to start, a recording started, a new version. |
| `check_for_updates` | true | Ask GitHub for a newer release 10 seconds after start. **Check now** in Settings works either way. |
| `window_x`, `window_y`, `window_width`, `window_height` | -1, -1, 1280, 760 | The app window's position and size in pixels; -1, -1 centres it, at no more than 90% of the screen. |

*Start with Windows* is not in the file: Windows' Run key for your user holds it.

**capture**

| Key | Default | Meaning |
|---|---|---|
| `server_port` | 13328 | The game server's TCP port. |
| `game_process` | `Aion2.exe` | The game's executable, to find its connection (unconfirmed). If wrong, port 13328 is still captured on every adapter, but a ping reducer's relay is not followed. |
| `detect_relays` | true | Also take other TCP flows that carry the game's heartbeat: ping reducers and VPNs. |

**Each plugin's keys** are in its own section, documented in its README:
[Combat Meter](plugins/combat_meter/README.md#configuration) (`meter`, `overlay`,
`history`) and [Auras](plugins/auras/README.md#configuration) (`auras`). Each
section's `plugin_enabled` is that plugin's switch.

**Files**, all in `%APPDATA%\aion2-combat-meter\`: saved fights in `history\` (a JSON
file per fight, and `index.jsonl`), recordings in `captures\`, and the log in
`meter.log` (the previous run's in `meter.prev.log`).

## 🩺 Troubleshooting

**"Npcap is not installed":** install it (Quick start), then restart the meter.

**"cannot open any adapter":** Npcap is restricted to administrators. Reinstall it with
that option unticked, or run the meter as administrator.

**It says WAIT while I'm in game**
- **Ping reducer or VPN** (ExitLag, GearUP and the like): Npcap needs loopback support
  (reinstall with it ticked). Also check `capture.game_process`.
- **Another port:** record a capture in Wireshark and run `aion2meter-replay` on it (a
  separate download on the release page). It lists every flow it decoded as the game.

**No overlay in game:** use Borderless Windowed, check the plugin is on, and press
Ctrl+Alt+H if the meter overlay is hidden. An overlay set to show only in combat, only
in dungeons or only with the game in front stays out of sight until then; unlock it
(Ctrl+Alt+L) to see it at once. A dungeon shown by number (`Map 600123`) counts as the
open world.

**Players show as #12345:** the game sends names on loading screens. Start the meter
before you log in or teleport; until then your own row is **You**.

**Only my row shows:** out of a party, the open world lists only you. In a party, the
meter learns its members from the list the game sends as it forms, so start the meter
before you form or join one.

**A boss fight says "partial data":** on crowded world bosses the server stops sending
most players' hits. Only the hits the meter saw are counted.

**Updating:** a tray notification and an **Update available** pill in the sidebar say
when a new version is out. **Settings → Updates → Update & restart** downloads it,
checks it against GitHub's SHA-256, swaps the exe and restarts; a fight still going is
not saved.
- The old exe stays as `*.old` until the next start.
- If the folder is not writable (under `Program Files`, say), run the meter as
  administrator once, or unzip the new release over the old one.
- A release with no published checksum is announced but not installed: download it by
  hand.
- The replay tool is not updated: download it again when you need a newer one.

## 🎥 Recording a capture

A capture of the game's traffic is how the decoder gets fixed and checked.
**Captures hold character names: share them only with people you trust, and never
commit them** (`.gitignore` blocks `*.pcap*`).

- **From the meter:** **Record** on the Dashboard, or **Record capture** in the tray
  menu. It goes to `%APPDATA%\aion2-combat-meter\captures\`.
- **With Wireshark** (most complete): capture on your network adapter and on "Adapter
  for loopback traffic capture", with the filter `tcp port 13328`. Start before you log
  in or teleport, since names arrive on loading screens, and save as pcapng. Note your
  character's name, a few hit numbers you saw and when the boss died.

## 🛠 Building from source

**macOS or Linux** (the portable core, tests and tools):

```bash
make test
```

That runs the unit tests, a compile of every header on its own, an ImGui syntax check,
the forbidden-API check, and a replay of a made-up boss fight against
`tests/golden/synthetic_fights.txt`.

| Target | Does |
|---|---|
| `make tsan`, `make asan` | The tests under ThreadSanitizer, or AddressSanitizer and UBSan. |
| `make replay` | Builds `./aion2meter-replay`. |
| `make fixtures` | Replays your captures in `tests/fixtures/local/` against their `.expected.txt`. |
| `make screenshots` | Re-renders `docs/images`. Needs zlib; off macOS, set `SHOTS_FONT` and `SHOTS_BOLD_FONT`. |
| `make golden` | Rewrites the replay smoke test's reference after an intended change. |

**Windows** (the app): Visual Studio 2022 and CMake 3.22 or newer. `build\bin\Release\`
then holds `aion2-combat-meter.exe` and `aion2meter-replay.exe`.

```bash
cmake -B build -A x64
```

```bash
cmake --build build --config Release
```

**Replaying a capture** runs it through the live meter's pipeline, with no game or
Npcap. The app plays it at recorded speed:

```bash
aion2-combat-meter.exe --replay fight.pcapng --speed 1
```

The replay tool prints it; `--help` lists its reports (skills, deaths, effects, casts,
events, hex dumps) and `--anonymize`, for output you will share:

```bash
./aion2meter-replay capture.pcapng --fights
```

`build/synth/aion2meter-synth out.pcapng` writes the tests' made-up boss fight, with
invented names, to try it all without the game.

## 📡 Protocol

The meter decodes the server's side of TCP port 13328, which is not encrypted: framed
opcodes, some bundled with LZ4, and a heartbeat carrying the server's clock. The opcodes
are in `src/protocol/include/protocol/opcodes.hpp`; the layouts, and what is still
unknown, in [AGENTS.md](AGENTS.md). These facts come from the AION 2 community's
published notes on the Global client; the code is written from scratch.

## License

MIT, see [LICENSE](LICENSE). Beyond this project's own code:
- **Dear ImGui** is MIT (`src/third_party/imgui/LICENSE.txt`). The app window's icons
  are a subset of [Lucide](https://lucide.dev) (ISC), embedded by
  `tools/embed_icon_font.py`.
- **Names and cooldowns** of skills, dungeons, monsters and NPCs come from aion2.app's
  database (`src/game/data/*.tsv`, `plugins/auras/data/cooldowns.tsv`).
- **The class emblems and skill icons are NCSOFT's art,** as aion2.app serves it,
  packed into `src/app/src/game_icon_sheet.inc`. The MIT licence does not cover them.
  The app's own icon is this project's drawing (`tools/gen_app_icon.py`), under MIT.

AION 2 is a trademark of NCSOFT. This project is not affiliated with NCSOFT.
