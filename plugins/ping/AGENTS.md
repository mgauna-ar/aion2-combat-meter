# Ping: Agent Guidelines

The rules of the Ping plugin (`plugins/ping`, namespace `aion2::ping`). The
repository's [`AGENTS.md`](../../AGENTS.md) holds what every plugin shares: Passive
only, the engineering principles, the layering, the threads, the protocol (the pong is
under "Heartbeat and pong") and the self-update. What a user sees and sets is in this
plugin's [`README.md`](README.md).

**What it is:** `PingPlugin` (a `game::Plugin`) owns one part, `PingTracker`, fed two
events on the engine thread and the capture clock: the pong (`03 36`) and the
heartbeat (`00 36`). It reads no entity, no map and no fight.

The app draws it: the ping overlay (`src/app/src/ping_view.cpp`) and the Ping page
(`src/app/src/ui/view_ping.cpp`).

**What it may use:** the protocol layer. Never another plugin. Switched off, the
pipeline feeds it nothing and clears it.

---

## 🔒 Passive, here too

The plugin measures the game's own ping. It must never send one of its own: no ICMP,
no TCP probe, no packet to the game's server or anyone else's (the repository's
Passive only). A figure that cannot be had by reading is not shown.

---

## 📶 Ping (`PingTracker::on_pong`)

- **The sum:** `ping = (pong's capture time − counter offset) − (client_ms − base)`.
  - `client_ms` is the clock the client stamped its ping with, echoed by the server.
  - `base` is `kClientClockBaseMs`, 16,777,216,000: Unreal's
    `FPlatformTime::Seconds()` is the performance counter plus 2^24 s.
  - The counter offset is the capture's clock less the machine's performance
    counter, measured by the capture threads (below).
- **The one figure from outside the capture** is that offset. It reaches the plugin as
  data, through `Pipeline::set_counter_offset`, as the dropped count does: the plugin
  and the pipeline still read no clock (One clock).
- **No offset, no ping:** a capture file has none (`PacketSource::counter_offset` is
  empty), so a replay's state is `NoCounter`. Tests set one by hand.
- **Rejected, never guessed:** a round trip under 0 or over `kMaxPing` (5 s), a clock
  under the base, or one more than ten years past it, is counted, kept as
  `last_rejected_ms` for the page, and makes the state `ClockRejected`. Nothing is
  shown for it. That is what a wrong base would look like.
- **The state is the latest pong's.** An earlier ping stays in the snapshot, but
  `shown_ping` (the view) shows it only while the state is `Measured` and it is under
  `kPingStale` (25 s: two pongs missed) old.
- **The range** (least, average, most) is over the pongs of the last `kStatsWindow`
  (5 min). It is not reset by a new connection, so for five minutes after entering a
  dungeon it spans two servers.

**The inference, and how to check it.** That the echoed clock is Unreal's rests on two
captures: their pongs read as 1.96 h and 7.40 h of uptime, and step by exactly 10,000
a pong. It has not been checked against the game's own ping display. On the gaming PC,
a steady ping in the tens or hundreds confirms it; `ClockRejected` with a
`last_rejected_ms` of some constant size says the base is off by that much.

---

## ⏱️ The counter offset (`capture::ClockOffsetEstimator`, in `src/capture`)

- **Per packet, not from the wall clock.** Npcap stamps packets from the performance
  counter, set against the wall clock once; the wall clock then drifts from it by tens
  of milliseconds an hour. `wall − counter` read now would be off by that drift.
- **How:** a capture thread reads the counter (`os::performance_counter_now`) as soon
  as `pcap_next_ex` returns. `stamp − counter` is the true offset less the delivery
  delay, never more: the largest of the last 15 to 30 s is the offset.
- **So the ping reads high, never low,** by the quickest delivery of that window: well
  under a millisecond with 20 heartbeats a second.
- The counter is system-wide and read in our own process. No handle to the game.

---

## 〰️ Jitter and lateness (`PingTracker::on_heartbeat`)

Both come from the heartbeat alone, so they need no counter and work in a replay.
- **Jitter** is RFC 3550's: `D` is the gap between two arrivals less the gap between
  their server times; `J += (|D| − J) / 16`. The server's time is in whole
  milliseconds, which alone gives a calm line 1 to 3 ms.
- **Lateness** is a heartbeat's `arrival − server_ms` above the least it was over the
  last `kBaselineWindow` (30 s, in two halves). The delay itself means nothing: the
  two clocks are not set alike, and it may be negative.
  - A delay that lasts becomes the usual one within 30 s. Until the next pong the
    ping line and the bars can then show the same added delay twice; they are drawn
    apart for that reason, never summed.
- **A heartbeat further than a day from the capture's clock** is not one, as the
  decoder has it.
- **One second, one entry** (`PingSecond`): that second's worst lateness, and the ping
  as the second closed (none once it is stale). A second with no heartbeat has no
  entry. `kGraphWindow` (120 s) of them are kept.

---

## 🔌 Connections

- Entering a dungeon opens a connection to another server (the repository's
  Transport): another clock, another route.
- The tracker follows the newest (`ParsedEvent::connection` counts up). A higher one
  resets the jitter and the lateness baseline; a lower one is the old connection's
  last frames, and is ignored.
- The ping and the graph are kept across it: the last ping shows until the new server
  answers, within 10 s.

---

## 🖥️ The overlay and its settings

- The plugin's master switch (`ping.plugin_enabled`, off on a fresh install) shows and
  hides the overlay.
- Locked, the overlay hides by its own `hide_when_game_unfocused` and
  `show_in_dungeons_only` (`ping_shown`), never by another overlay's. It has no "only
  in combat": lag matters between fights too.
- Ctrl+Alt+L locks every overlay at once; that is the app's, not a plugin's.
- **The pure parts are in `ping_view` and tested without ImGui:** `ping_level` (the two
  thresholds; `bad_ms` is never under `warn_ms`, which `sanitize` and the page's
  sliders both hold), `shown_ping`, `ping_status`, `format_ms`, and the graph as
  numbers (`ping_graph`: 120 columns, the newest last, and `ping_graph_top`, a round
  figure so the scale does not twitch).
- **The graph draws two things apart:** the ping as a line, each second's lateness as
  bars from the bottom, on one scale. Lateness under 1 ms is not drawn.
- **Text is ASCII:** the overlay's font has no dash beyond "-".
- The README's pictures come from a `PingSnapshot` made up in `tools/screenshots`: the
  synthetic session has no pongs, and a replay has no counter.
