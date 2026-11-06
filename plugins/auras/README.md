# ✨ Auras

**Your buffs and debuffs, yours on the enemy you fight, and your cooldowns, as bars in
an overlay of their own.** One of the meter's two plugins (the other is the
[Combat Meter](../combat_meter/README.md)). Off until you switch it on, at the top right
of the app window's **Auras** page; switching it on shows its overlay.

![The aura overlay mid-fight: a missing watched buff, three buffs with their time left, three cooldowns, and the player's effect on the boss](../../docs/images/overlay-auras.png)

## The overlay

A second window you place like the meter's (unlock with Ctrl+Alt+L, drag, resize):
- **Your effects:** each is a bar, led by the icon of the skill behind it, showing what
  share of its time is left, with the seconds on the right. A monster's effect has no
  icon. Green bars are an ally's buffs, blue ones yours, red ones an
  enemy's debuffs. Stacks show as "x2".
- **Your cooldowns:** amber bars under "Cooldowns", each with its skill's icon, and "~"
  on an estimate. A skill
  with a second use ("+1 consecutive use") shows only once both are spent.
- **Yours on your target:** under "On <enemy>": the biggest enemy you (or your summons)
  hit in the last 10 seconds, so in a boss fight with adds, the boss.
- **The watch list:** the page's *Watch list* lists every buff seen on you this session.
  A buff you switch on there shows as a red "Missing: <name>" line while it is not on
  you: a Chanter's mantra, say.
- **What it leaves out:** effects shorter than a second (a dodge's mark), and passives'
  effects, which never end. Both are settings.
- **Hiding it:** locked, it can hide while the game is not in front, and outside
  dungeons, by its own settings. It has no "only in combat": buffs matter between
  fights too.

![The Auras page: the plugin's switch, what the overlay lists, the cooldowns, the watch list, and the overlay's own settings](../../docs/images/app-auras.png)

## How exact it is

- **Effects** come with the server's own durations and end times, so their bars are
  exact. About four in five have a name from the skill table; monster, item and system
  effects show as "Effect <number>".
- **Cooldowns** are estimates, marked "~": the server sends none when you use a skill.
  The plugin starts from aion2.app's figure less the specialties you picked, and
  shortens it to your quickest reuse as you play (that is where your cooldown
  reduction shows). Over 274 uses on a dungeon capture, no estimate ran more than
  0.65 s long. When the server does say what is left (after a cut or a reset), the bar
  is exact.
- **Switched off**, it forgets what it knew. Switched back on, it fills in as effects
  come: on the next loading screen, every passive's effect is sent again.

## Configuration

Its keys live in `%APPDATA%\aion2-combat-meter\config.json`, beside the app's
([README, Configuration](../../README.md#%EF%B8%8F-configuration)). The page edits them
and saves as you go.

**auras**

| Key | Default | Meaning |
|---|---|---|
| `plugin_enabled` | false | The plugin's switch, which shows and hides its overlay. A file from 0.2.0, which has none, takes its `visible` instead. |
| `locked` | false | Click-through and fixed in place. |
| `opacity` | 0.55 | Background opacity, 0 to 1. Bars and text stay opaque. |
| `scale` | 1.0 | Size, 0.6 to 2.5, on top of the monitor's DPI scaling. |
| `x`, `y`, `width`, `height` | -1, -1, 280, 300 | Position and size in screen pixels. -1, -1 places it under the meter overlay's default spot. |
| `show_debuffs` | true | Enemies' effects on you. |
| `show_target` | true | Your effects on the enemy you fight. |
| `show_permanent` | false | Effects that never end: passive skills' and the ones a login puts on. |
| `min_duration_s` | 1.0 | Effects shorter than this are left out, 0 to 60. |
| `show_cooldowns` | true | Your cooldowns, estimated. |
| `min_cooldown_s` | 3.0 | Cooldowns shorter than this are left out (Dodge's, a combo's), 0 to 120. |
| `watch` | `[]` | Effects to flag while they are not on you: the skill codes the *Watch list* stores, at most 64. |
| `hide_when_game_unfocused` | false | Hide it while neither the game nor the meter's window is in front. Applies only while locked. |
| `show_in_dungeons_only` | false | Show it only in a dungeon (as the meter overlay's setting of that name). Applies only while locked. |

## For developers

How it decides who is who, which enemy is the target, and how the cooldowns are
estimated is in this plugin's [`AGENTS.md`](AGENTS.md). `aion2meter-replay --auras`
prints every effect put on and taken off, and your skill uses and cooldown changes.
