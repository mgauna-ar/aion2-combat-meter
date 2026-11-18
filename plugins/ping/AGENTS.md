# Ping: Agent Guidelines

The rules of the Ping plugin (`plugins/ping`, namespace `aion2::ping`). The
repository's [`AGENTS.md`](../../AGENTS.md) holds what every plugin shares: Passive
only, the engineering principles, the layering, the threads, the protocol (the pong is
under "Heartbeat and pong") and the self-update. What a user sees and sets is in this
plugin's [`README.md`](README.md).

**What it is:** `PingPlugin` (a `game::Plugin`) owns one part, `PingTracker`, fed two
events on the engine thread and the capture clock: the pong (`03 36`) and the
heartbeat (`00 36`). It reads no entity, no map and no fight. From the pipeline it
also takes three facts that are no event: the counter offset (below), each frame's
connection number, and a connection closing (Stalls, Connections).

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
  and the pipeline still read no clock (One clock). `AppRuntime` hands it over before
  each packet as well as at each tick: at the tick alone, a pong in the first packets
  of a live capture came before any offset, and read as a replay's (`NoCounter`).
- **No offset, no ping:** a capture file has none (`PacketSource::counter_offset` is
  empty), so a replay's state is `NoCounter`. Tests set one by hand.
- **Rejected, never guessed:** a round trip under 0 or over `kMaxPing` (5 s), a clock
  under the base, or one more than ten years past it, is counted, kept as
  `last_rejected_ms` for the page, and makes the state `ClockRejected`. Nothing is
  shown for it, on the graph either: a pong that gave no ping (rejected, or with no
  counter) is kept as such, and the seconds after it carry none. That is what a wrong
  base would look like.
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
- **Threads interleave.** Each reads its counter before taking the estimator's lock,
  so a sample can come in after one read later. One read before the current half
  began counts for the half before; only a counter more than half a window back
  starts the estimate over. It once did at any step back, and one slow sample then
  stood for the whole window.
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
- **A heartbeat is the decoder's:** no flag, an 8-byte body, a server time within a
  day of the capture's (`FrameDecoder::heartbeat_server_ms`, in the parser too). A
  flagged or longer `00 36` once reached the plugin, and its first u64 read as some
  seconds late.
  - **The window's halves run on the server's time,** not on arrival. A stall does
    not age them: the heartbeats held up behind it were sent in the half they were
    sent in, and are measured against the delay before it. On arrival time, a stall
    that ended 30 s or more into a half threw the old baseline away, and the burst
    that ended it measured itself: 0 ms late.
- **One second, one entry** (`PingSecond`): that second's worst lateness, and the ping
  as the second closed: the latest pong before its end, none if that one gave none
  or is stale. A pong after the second (the next server's, after a load) is not its
  ping. A second with no heartbeat has no entry, unless a stall passed through it.
  `kGraphWindow` (120 s) of them are kept.

---

## ⏸️ Stalls (`PingTracker::stall_late`)

Lateness is measured as a heartbeat arrives, so a stall, when none arrives, would
show nothing until it ends. Instead:
- **A heartbeat overdue is already late.** The next one's server time is the latest's
  plus `kHeartbeatEveryMs` (50); were it to come now, its delay less the baseline
  would be its lateness, and it comes later still. From `kStallAfter` (250 ms) on,
  that lower bound is shown: in the second `now` is in, and in each second the stall
  has passed through, as it stood at that second's end.
- **Once the heartbeats come again,** the seconds it passed through are kept as they
  were drawn, and the heartbeats that were held up show their own lateness.
- **The jitter moves with it:** the next heartbeat's arrival gap is already that much
  over the server's 50 ms, so the snapshot shows at least the jitter it will make.
- **Under `kStallAfter`** a heartbeat a little late shows as it arrives, as before:
  20 a second, they would otherwise flicker.
- **What is no stall,** since the server is not holding anything back:
  - the connection closed (`FrameSink::on_connection_closed`, a FIN or RST);
  - a newer connection's first frame came, read or not (`Pipeline::on_frame`): a
    zone change opens the new connection 0.10 to 0.18 s after the old one's last
    heartbeat, and the load then sends nothing for 2.4 to 2.7 s. One of the five
    zone changes in the captures had no FIN;
  - silence beyond `kHeartbeatsGone` (30 s): the game crashed or the capture stopped.
  The jitter goes too (`has_jitter` false): there are no heartbeats to be uneven.
- **It needs `now` on the packets' clock.** A stall is measured from the latest
  heartbeat's capture time to the snapshot's `now`. With the queue empty,
  `AppRuntime` runs that on from the latest packet by the source's elapsed time, so
  the wall clock sitting apart from Npcap's stamps reads as nothing. A forward step
  of the wall clock while the queue is empty still reads as stall (the repository's
  One clock).

---

## 🔌 Connections

- Entering a dungeon opens a connection to another server (the repository's
  Transport): another clock, another route.
- The tracker follows the newest (`ParsedEvent::connection` counts up), from its
  first pong or heartbeat. A higher one resets the jitter and the lateness baseline; a
  lower one is the old connection's last frames, and is ignored.
- A newer connection's frame of any kind, or the followed one closing, ends the
  followed one's heartbeats at once (Stalls), before the new one is followed.
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
- **Locking moves and resizes nothing.** The figures' panel starts
  `chrome::header_height` down the window whether the header is drawn or not.
  Unlocked, one ImGui window fills the display, header first. Locked, only the panel
  is drawn (`chrome::begin` over part of the display), and the header's strip stays
  see-through: an overlay window is transparent where nothing is drawn, and
  click-through all over while locked. So `ping.height` includes the header.
  - The graph's height must not depend on `interactive`: the resize grip is drawn
    over the corner, with no room left for it.
- **Without the graph the panel fits its line** (`ping_row_layout`): the ping, and the
  jitter beside it, or under it when the window is too narrow for both. The window
  keeps its size, unseen while locked. Unlocked, the window shows around the panel
  with the panel marked out, since that is what the user moves and what bounds the
  line's width.
- **The pure parts are in `ping_view` and tested without ImGui:** `ping_level` (the two
  thresholds; `bad_ms` is never under `warn_ms`, which `sanitize` and the page's
  sliders both hold), `shown_ping`, `ping_status`, `format_ms`, `ping_row_layout`, and the graph as
  numbers (`ping_graph`: 120 columns, the newest last, and `ping_graph_top`, a round
  figure so the scale does not twitch).
- **The graph draws two things apart:** the ping as a line, each second's lateness as
  bars from the bottom, on one scale. Lateness under 1 ms is not drawn.
- **Text is ASCII:** the overlay's font has no dash beyond "-".
- The README's pictures come from a `PingSnapshot` made up in `tools/screenshots`: the
  synthetic session has no pongs, and a replay has no counter. The tool crops each
  to what was drawn, so a locked overlay shows as its panel; `--all` adds the unlocked
  and narrow states.
