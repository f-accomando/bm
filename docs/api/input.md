# API: Input

Part of the game API. Index and basics: [API.md](../API.md).

### Input

| Function | Description |
|---|---|
| `btn(i, [p])` | `true` while the button is held; without `p` from **any** controller, with `p` = 1–4 only from player `p`'s. Instead of `i` also a **name** (2026-10-04): a button (`"a"`, `"b"`, `"x"`, `"y"`, `"left"`…`"down"`, `"start"`, `"select"`, `"l1"`, `"r1"`, `"l2"`, `"r2"`, `"l3"`, `"r3"`), `"ok"` and `"back"` (the system's yes and back: cross and circle on the DS4, A and B on an Xbox pad, Space and X on the keyboard; on the RGB30 as `confirm=` and `game_buttons=` say) or an action of `keymap()` |
| `btnp(i, [p])` | `true` only in the frame it is pressed (the same `p`, also with a name) |
| `keymap(t)` | the game's **actions** on the buttons: `keymap({ jump = "a", fire = {"x", "r1"}, pause = "start", confirm = "ok" })` (up to 32 actions, 4 buttons each); then `btn("jump", p)`, `btnp("fire")` and `prompt("jump", x, y)`. The game changes the keys by calling it again (its options menu; it keeps them with `save()`); `keymap()` returns the table, `keymap(nil)` takes it away. An unknown button or an action with a button's name is an error |
| `controller([p])` | what player `p` (1–4, the first if missing) plays with: `{kind = "keyboard" / "ds4" / "xbox" / "pad" / "builtin" / "none", layout = "keyboard" / "ds4" / "xbox" / "nintendo" / "none", bluetooth = bool, ok = "a" / "b", back = "b" / "a"}`: `layout` says what the buttons are called (the DS4's symbols; letters with A at the bottom as Xbox; A on the right as the RGB30), `ok` and `back` which of the game's buttons are the yes and the back, `color` the player's colour (that of their pad's light: 1 blue, 2 red, 3 green, 4 pink) |
| `online([on, note])` | the match is played over the **network** (2026-10-04): with `online(true)` PS, Ctrl+Esc and Start+Select do not suspend the game (the others go on playing) but ask the player who leaves, only on their console, "Leave the match?" (they will leave the game and disconnect from the server), over the game that goes on. While the question is open the game sees neither buttons nor sticks nor keys; ok (cross on the DS4, Enter or Space on the keyboard) or PS again leave: `_leave()` and the cartridge closes; back (circle, Esc) stays, and the buttons still held go back to the game only after being released. `note`: a line under the question (e.g. `"You are the host: the match ends for all."`). `online(false)` at the end of the match; `online()` returns whether it was online and whether the question is open (the player is away) |
| `players()` | how many players have a controller (at least 1) and, as the second value, which: bit `n` = player `n+1` (e.g. `3, 7` = players 1, 2 and 3) |
| `stick([p, n])` | player `p`'s left stick: `x, y` between −1 and 1 (x to the right, y down), with a dead zone; with the keyboard or a pad without a stick it is the cross (8 directions). With `n = 1` the **right** stick (to aim in shooters; `0, 0` without a stick). Without `p`: the one pushed most |

**Mouse and pointer (M32).** A cartridge has the pointer only if it asks for it: without
`mouse(true)` there is none (in bm's menu there always is). Only a USB or Bluetooth mouse
moves it: a controller never shows it (its right stick is `stick(p, 1)`); the console may
have it off for the whole system (`mouse=off` in `bm/config.txt`).

| Function | Description |
|---|---|
| `mouse(on, [arrow])` | `mouse(true)`: the cartridge wants the pointer, and the console draws its arrow over the frame (`mouse(true, false)`: no arrow, the cartridge draws its cursor); `mouse(false)` takes it away. Returns `false` if the console has the mouse off |
| `mouse()` | `x, y, buttons, wheel, visible`: the position in pixels of the cartridge's screen (without `camera`), the buttons held as bits (1 left, 2 right, 4 middle), the wheel's clicks in this frame (up positive) and `true` if the pointer shows (as soon as something moves it). `nil` if the cartridge has not asked for it or if nothing moves it (neither mouse nor right stick) |
| `mousep([i])` | `true` in the frame button `i` is pressed (0 left, the default; 1 right; 2 middle) |

```lua
function _init() mouse(true) end
function _update()
  local x, y = mouse()
  if x and mousep() then sfx(0) end   -- a click
end
```

**bmui, the mouse of bm's tools** (`require "bmui"`, `src/script/bmui.lua`, 2026-10-06): the
editors (SDK, bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel, Sound, the assistant's
panel) use it, and a game or a tool of your own can too. Its rule: the mouse does what the
keys do. Things are made clickable as they are drawn, and a click presses the key behind
them or chooses what they show; the right button opens a context menu whose entries are keys.

| Function | Description |
|---|---|
| `U.update()` / `U.begin()` | in `_update` first (reads `mouse()` once a frame), in `_draw` first (forgets the zones of the frame before) |
| `U.zone(x, y, w, h, kind, a, b)` | something clickable, as it is drawn (the last one drawn wins: a dialog over the page); `U.at()` the zone under the pointer, `U.click([b])` the zone pressed this frame (0 left, 1 right, 2 middle), `U.clicked(b)` a button let go without a drag (the zone where it went down, or `true`) |
| `U.key_zone(x, y, w, h, keys)` / `U.keys()` | a chip (`"ctrl s"`, `{"shift", "l"}`, `"f5"`) that presses its key when clicked; `U.keys()` does it for the click of the frame |
| `U.press(key)` | a key as if typed: the next `keyp()` gives it (bmui puts its own `keyp` first) |
| `U.menu(items, [x, y])` / `U.menu_update()` / `U.menu_draw()` | the context menu at the pointer: `{ {"Copy", "^c"}, "-", {"Run", function() ... end} }` (the key shown on the right, "Ctrl+C"); `U.menu_update()` in `_update` (true: it took the input), `U.menu_draw()` last in `_draw`; rows on the 8x16 grid |
| `U.x`, `U.y`, `U.on`, `U.moved`, `U.wheel`, `U.double`, `U.drag` | the pointer (`U.on`: a mouse moved it and it shows), whether it moved, the wheel's clicks of the frame, a double click, the drag `{b, x0, y0, dx, dy}` (past 3 pixels; `dx, dy` the frame's move); `U.pressed(b)`, `U.released(b)`, `U.down(b)`, `U.shift()`, `U.drag_arrows(step)` (a drag as arrow keys) |

In the editors: a click chooses a row, a double click (or a click on the chosen one in the
dialogs) takes it, the wheel moves or changes a value; in the 3D views the right button
dragged turns, the middle one moves, the wheel zooms; on canvases the left button draws and
the middle one picks the colour; in bm Code a click places the cursor, a drag selects, a double
click takes the word, a click in the numbers is a breakpoint.

**More players (M16).** Bluetooth controller *n* is player *n* (paired from the monitor
with `T`, one at a time: each takes the first free place and its light the player's colour:
1 blue, 2 red, 3 green, 4 pink). The USB keyboard or gamepad and the serial line are the
first player without a pad (without Bluetooth pads: player 1); the Bluetooth keyboard (M28)
is a player of its own, the next one without a pad (the first, if there is nothing on USB).
One-player games use `btn(i)` without `p` and work with any controller; a game for more
players asks `btn(i, p)` for each (example: `carts/pong`, two-player mode). bmlib has the
screen where the players join (`lib.party`), the split screen (`lib.split`) and the
colours (`lib.PLAYER_COLORS`): [Players on one console](bmlib.md#players-on-one-console); the SDK's
*Versus 2D* template uses them.

| `i` | Keyboard | Gamepad | Serial |
|---|---|---|---|
| 0 left | ← or A | cross / stick | `a` or ← |
| 1 right | → or D | cross / stick | `d` or → |
| 2 up | ↑ or W | cross / stick | `w` or ↑ |
| 3 down | ↓ or S | cross / stick | `s` or ↓ |
| 4 **A** | space, Z, J | A / cross (DS4) | space, `j` |
| 5 **B** | X, K | B / circle | `x`, `k` |
| 6 **X** | C, L | X / square | `c`, `l` |
| 7 **Y** | V, I | Y / triangle | `v`, `i` |
| 8 **Start** | Enter | Start / Options | Enter |
| 9 **Select** | Tab | Select / Share | — |

Start+Select together (or the PS button) always close the cartridge: Start alone is free for
the game's pause.

**Standard for games.** Which logical action (confirm, cancel, pause, act1, act2, shoulders) goes on which button, on the RGB30 and the Pi: [games-input-standard.md](../design/games-input-standard.md).

A cartridge that never asks `btn(6)`/`btn(7)` (or `btnp`) gets X as A and Y as B: games
with two buttons work with all four.

**Keys chosen by the player.** A cartridge that lets the player choose the keys (as nano8)
reads the keyboard key by key and the controllers button by button:

| Function | Description |
|---|---|
| `rawkeys(on)` | with `true` the keyboards stop being controllers for `btn()` and `pad()`: they are read with `keydown()`. Ctrl+Esc still closes the cartridge |
| `keydown(u)` | `true` while the key with USB HID usage `u` is held (USB or Bluetooth): `0x04`…`0x1D` the letters A–Z, `0x1E`…`0x27` the digits, `0x28` Enter, `0x2C` space, `0x4F`…`0x52` the arrows (right, left, down, up), `0xE0`…`0xE7` Ctrl, Shift, Alt, GUI on the left and then on the right |
| `keys()` | the usages of the keys held now (`{0x1D, 0xE1}`): for "press a key" |
| `pad([p])` | the buttons player `p` (1–4) holds, as bits: 1 left, 2 right, 4 up, 8 down, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y, 1024 L1, 2048 R1, 4096 L2, 8192 R2, 16384 L3, 32768 R3 (the triggers and the pressed sticks: DS4 and Xbox 360; on generic pads buttons 7–8 and 11–12); without `p` everybody's. The serial keys count as the first player's controller (L1 and R1: `u` and `o` from the serial line, Q and E from the USB keyboard; Select: Tab from both) |
| `lastinput()` | what was pressed last: `"keyboard"`, `"ds4"` or `"pad"` (another controller); `nil` before any key. To show the right keys with `prompt()` (for example `"enter"` or `"A"`) |
