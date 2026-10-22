# API: Time, network, saves, documents

Part of the game API. Index and basics: [API.md](../API.md).

### Time and system

| Function | Description |
|---|---|
| `time()` | seconds since the cartridge started (with decimals) |
| `stat(n)` | 0 KiB used by Lua, 1 ms of the last frame (`_update` + `_draw`, with the GPU's 3D), 2 fps, 3 frame number, 4 3D triangles, 5 3D pixels (0 with the GPU), 6 ms spent in 3D drawing (since `zclear`; with the GPU the ARM's part), 7 3D vertices transformed, 8 ms since the start of this frame (to measure the phases), 9 `1` if the GPU draws the 3D, 10 Lua instructions of the last frame (`_update` + `_draw`; the count, rounded to the thousand); the **dev kit** (2026-10-04): 11 tokens of the cartridge's code (`code_tokens`), 12 the most KiB of Lua of this run, 13 KiB of the cartridge's data in memory (sprite sheet, map, models and skeletons, sound bank, the 3D z-buffer), 14 the Lua instructions of the busiest frame of this run; 15 how many `_update` ran before this `_draw` (1; more with `frameskip`); 16 ms of GPU job time (bin + render) accounted in the last frame, `0` when the ARM draws the 3D (2026-10-12; with `stat(1)` it tells whether the GPU or the ARM sets the pace); the profiling metrics (bm3D 7.x): 17 ms of audio processing in the last frame (the time of the audio interrupt), 18 ms the ARM waited for the GPU in the last frame (time blocked on GPU job completion). Use 1, 16, 17 and 18 together to see if the CPU/ARM or the GPU is the bottleneck. 18 is `0` when the ARM draws the 3D. On the RGB30 and in QEMU, 16 and 18 are `0` (no V3D jobs there), and 17 is `0` where the console has no audio interrupt to time. **A measurement only**: it differs between consoles, so never use it (nor 1, 2, 6, 8, 10, 16, 17, 18) in game logic, or a lockstep match falls out of step |
| `frameskip([n])` | the game's time at 60 `_update` a second whatever `_draw` costs (2026-10-05): when a frame takes longer than 1/60 s, up to `n` `_update` run before the next `_draw` (the frames in between are not drawn), so a game that moves 1/60 s per `_update` does not slow down; past `n` the time is let go (the game slows rather than never drawing). `1` (the default) is one `_update` a frame, as before; at most 8. Returns the old value. A button pressed counts once in `btnp()` (and `mousep()`, the wheel) however many `_update` see it; `btn()` stays held. Overbit uses `frameskip(4)` (its benchmark `1`) |
| `devkit([mode])` | the dev kit's overlay: `0` off, `1` at the top right, `2` at the bottom right (`3`, the old functions page, is the top: old games keep working); with a mode it shows it (a game's own key for it, e.g. Select on a pad: F11 is the keyboard's), in this run only: every game starts as Settings says (off by default). Returns the old mode. To go round it: `devkit((devkit() + 1) % 3)` |
| `profile([on])` | the profiler of functions (R14): the functions that cost the most in the last whole second (60 frames), the costliest first, up to 32: `{name, where, c, self, total, calls}` (`where` `"main.lua:120"` or `"[C]"`; `c` true for the console's functions and Lua's written in C; `self` and `total` ms a frame, its own time and with what it calls; `calls` the calls a frame, C ones only), and the frames measured (0 during the first second). `profile(true)` turns it on for the run (the session report then lists the ten costliest), `profile(false)` turns it off (and forgets). E.g. `for _, f in ipairs(profile()) do log(f.name, f.self) end` |
| `devinfo(line, ...)` | up to 4 lines of the game in the dev kit's session report ("the game says:", as the run ended: its quality, its actors...), 18 characters each; `devinfo()` none. Call it again when they change (Overbit every frame while the overlay is on) |
| `battery()` | the console's battery (2026-10-10): its charge `0`–`100` and `true` on the charger (`local pct, plugged = battery()`); `nil` without a battery (the Pi). On the RGB30 the charge comes from the voltage (an indication: it sags under load and rises on the charger); the charger is seen within a quarter of a second |
| `battery_low()` | `true` while the battery is low: 20% or less off the charger (until 23%). Then the system draws by itself a small red battery top right over the frame (left of the dev kit's overlay when it is at the top), in every game and tool; *Settings > Screen and sound > Low battery icon* (`battery_icon=0` in `bm/config.txt`) turns that icon off, not `battery_low()`. Information only: each console has its own battery, so in lockstep (Overbit, `bmnet`) it never changes what the game simulates; use it to show something or to save |
| `code_tokens(text)` | the **tokens** of a piece of Lua code, counted as `stat(11)` and the SDK's dev kit do (`src/bm/tokens.c`): each name, keyword, number, string and operator is one; comments, spaces, `,` `.` `:` `;` `::`, closing brackets (`)` `]` `}`), `end` and `local` do not count, nor the minus sign in front of a number (`-1` is one token). Information, not a limit: bm puts no ceiling on tokens (nor does the `.b16`, [B16.md](../B16.md) §2.4) |
| `log(...)` | writes in the kernel's log (serial line and console), not on the game's screen |
| `report(kind, text)` | a report for the people who develop bm (2026-10-04): saved in `bm/reports` on the SD card with kernel, branch, board and date (and a first `options` line: the 3D options in force, `docs/BENCH3D.md`), then sent to the reports' repository if there are `github_token` and the network (`src/kernel/reports.h`); at most 8 per run, 256 KiB each; `true` if saved. The first time a game calls it, the player is asked (the answer stays in `bm/config.txt`, `allow_...`): after a no, `false` |
| `shot([prefix])` | saves the frame being drawn as a PNG in `bm/shots/` when `_draw` ends and returns its name (`"bm/shots/PONG0007.png"`); the name starts with the first letters of the game's title, or of `prefix` (up to 4 letters or digits), then a number. `nil` and a message if the numbers are over. The player has a key for it too (Print Screen; Select held for 3 seconds). [SCREENSHOTS.md](../SCREENSHOTS.md) |
| `quit()` | closes the cartridge at the end of the frame |
| `timeslice(co, [k])` | coroutine `co` stops by itself after about `k` thousand Lua instructions in a frame (400 if missing) and `coroutine.resume` returns `true` without values: a long computation goes on in the next frames instead of stopping the cartridge at the instruction limit. `timeslice(nil)` takes it away (nano8 uses it for its cartridges) |

Random numbers: `math.random`. For different games at every start, seed the generator when
the player presses a key: `math.randomseed(stat(3))`.

### Network (UDP)

For network games (M38.5: Overbit). A game is better off with the
[bmnet](bmnet.md#bmnet-games-over-the-network) library (lobby, sure messages, lockstep), which uses
these functions. UDP packets up to 1024 bytes; each cartridge has 2
sockets, closed when it ends. Addresses are text (`"192.168.1.23"`); `"*"` is the LAN's
broadcast. The console's network is needed (WiFi or cable): without it `udp_open` returns
`nil`. In bmhost the same packets go through the PC's sockets (`BMHOST_NET_ID=k` for more
consoles on the same PC, `--realtime` to play at 60 frames a second).

| Function | What it does |
|---|---|
| `s, port = udp_open([port])` | a socket on the port (0 or nothing: any); `nil` and the reason if there is no network or free socket. The first time a game opens one, the player is asked whether it may use the network (the answer stays in `bm/config.txt`); after a no, `nil` and the reason |
| `udp_send(s, address, port, data)` | sends a string (at most 1024 bytes); `true` if it left (UDP: it can be lost) |
| `data, address, port = udp_recv(s)` | the next packet that arrived, or `nil`; up to 48 wait in the queue |
| `udp_close(s)` | closes the socket |
| `net_ip()` | the console's address, or `nil` without a network |
| `net_resolve(name)` | the address of a name: `nil` while it looks for it (call it again the frame after), `false` if it does not exist |

### The web (web_get, image_load, file_write)

For a cartridge that shows web pages (a browser cart). A request runs in the background: the game keeps
drawing while the page arrives. The kernel enforces the rules below. A cartridge cannot change them.

- **Network.** The player must allow the network, as for `udp_open`. Without a network `web_get` returns `nil, "no network"`.
- **Only `https://`.** The address is `https://host[:port]/path?query`, at most 1024 characters. The host has letters, digits, `.` and `-`.
  No user name, no space and no `#fragment` (remove them, or write `%XX`). The certificate must chain to `boot/ca.pem`.
- **Redirects.** The kernel follows at most 3. A redirect to `http://` is refused. The error text says so, and `web_info` shows the `location`.
- **A pause per host.** The kernel waits until 1 second has passed since the last request to the same host. A redirect to that host waits too.
  While it waits, the state is `"wait"`. The cartridge sets its own pace on top of this minimum (a site can ask for more).
- **User agent.** Fixed: `bm/<kernel version>`. A cartridge cannot change it or add headers.
- **Size.** The body is cut off at the cap: 4 MiB by default. `web_max_kib` in `bm/config.txt` changes it (at most 32768). A cartridge can ask for less with `max`.
  A body over the cap ends in the state `"error"` and nothing is kept.
- **One request runs at a time.** The result of up to 3 requests stays in memory until `web_cancel` or the end of the cartridge.
- **The cost of one frame.** `web_poll` works for about 4 ms. The TLS handshake of a new connection does not give the CPU back (about 100 ms on the Pi Zero W,
  measured for the reports in `docs/progress/multitasking.md`; not measured inside a cartridge). Show a "connecting" message before the first `web_poll`.
- **bmhost.** `web_get` returns `nil` and a reason. `image_load` and `file_write` work (the host folder is the card).

| Function | Description |
|---|---|
| `id = web_get(url, [opts])` | starts a GET request. `opts` is a table: `max` (a body cap in bytes, lower than the configured cap), `timeout` (milliseconds for one read, 1000 to 60000, default 15000). Returns the request `id`, or `nil` and the reason (bad address, no network, the player said no, a request already runs, no free slot) |
| `state, size, length = web_poll(id)` | works the request for about 4 ms. `state` is `"wait"` (the pause per host), `"busy"`, `"done"` or `"error"`. `size` is the body bytes received so far. `length` is the Content-Length (`nil` if the server did not give it). `nil` and a reason for an `id` that is not there. Call it once a frame |
| `web_info(id)` | a table: `state`, `status` (the HTTP status), `type` (Content-Type), `length`, `location` (the Location header of the last answer), `url` (the address reached), `size` and `error`. Fields that are not known are left out. Any HTTP status ends in `"done"`, a 404 too: look at `status`. A failed request (no connection, a refused redirect, a body over the cap, a timeout) ends in `"error"` |
| `web_body(id)` | the body of a finished request as a string, or `nil` and the reason. It copies the bytes into Lua memory: to decode a picture or write a file, give `id` to `image_load` or `file_write` instead |
| `web_cancel(id)` | stops the request and frees its result. Returns `true` if it was there |
| `img = image_load(bytes_or_id, sx, sy, [maxw, maxh])` | decodes a picture into the sprite sheet with its top left corner at `(sx, sy)`. The first argument is a string of bytes, or the `id` of a finished `web_get` (the body is not copied). It reads **baseline JPEG, PNG and WebP** (lossy and lossless). Progressive JPEG is refused. A picture over 4 megapixels is refused. A part that does not fit `maxw`×`maxh` or the sheet is cut at the right and the bottom. PNG pixels with alpha below 128 are transparent. Returns `{x =, y =, w =, h =}` (the part of the sheet used), or `nil` and the reason. Draw it with `sspr(img.x, img.y, img.w, img.h, dx, dy)`. The sheet is 256×256 by default: make it bigger with `cart_sheet(w, h)` first. The pixels are RGB565 |
| `file_write(name, bytes_or_id)` | writes a file in `bm/web` on the SD card (the folder is made if needed). `name` is a bare 8.3 name (`"MODEL.GLB"`), or the same after `web/`, `bm/web/` or `/bm/web/`. No `..`, no other folder, no leading dot, never a `.bm`, `.bme` or `.b16` file. The second argument is a string, or the `id` of a finished `web_get`. A file with the same name is replaced. At most 8 MiB (`file_write_max_kib` in `bm/config.txt`, at most 32768) and 128 files in the folder. Returns `true` and the path (`"bm/web/MODEL.GLB"`), or `nil` and the reason. It waits for the card: a big file stops the frame. It refuses while another write is under way (`fat_writing`): try again next frame |

```lua
local req
function _update()
  if btnp(4) and not req then req = web_get("https://example.org/pic.webp") end
  if req then
    local state = web_poll(req)
    if state == "done" then
      local img = image_load(req, 0, 64, 160, 90)   -- into the sheet, at most 160x90
      web_cancel(req)                               -- free the body
      req = nil
      shown = img
    elseif state == "error" then
      msg = web_info(req).error
      web_cancel(req)
      req = nil
    end
  end
end

function _draw()
  cls(0)
  if shown then sspr(shown.x, shown.y, shown.w, shown.h, 10, 10) end
end
```

### Saves

| Function | Description |
|---|---|
| `save(t, [slot])` | saves table `t` on the SD card, in slot 1–8 (default 1); `true`, or `false` and the reason (no SD card, card full...) |
| `saved([slot])` | the table saved last time in that slot (default 1), or `nil` |
| `saves()` | the slots in use, `{[slot] = bytes}` (e.g. `{[1] = 40, [3] = 212}`), and how many there are (8): for a "load game" page |
| `delsave([slot])` | empties the slot (default 1); `true`, or `false` and the reason (`"nothing saved in slot 3"`) |

Each cartridge has **8 save slots**, each a table of up to 32 KiB, in `/bm/save/` on the SD
card: slot 1 is `XXXXXXXX.SAV` (the one of always: `save(t)` and `saved()` without a slot),
the others `XXXXXXXX.S02` ... `.S08`. The name depends on title and author: changing them
starts again from zero. A common use: slot 1 for settings and records (`lib.store` uses it
too), the others for the games in progress. A table can hold numbers, strings, booleans and
other tables (no functions). The game's options in the menu (X on the cover) show the bytes
of all the slots and *Delete the save data* deletes them all. Writing on the SD card takes a
few milliseconds: call `save()` at moments like the end of a match, not in every frame
(`lib.store` and `lib.best` of [bmlib](bmlib.md#bmlib-the-games-shared-library) write only when a
value changes). Example (Snake's record):

```lua
function _init()
  local data = saved()
  if data then best = data.best end
end
-- at the end of the match
if score > best then best = score; save({ best = best }) end
```

Three games in progress, with the page to choose one (slots 2, 3 and 4):

```lua
local function slots()                  -- the rows of the "load game" page
  local used, rows = saves(), {}
  for i = 1, 3 do
    local p = used[i + 1] and saved(i + 1)
    rows[i] = p and ("Game " .. i .. ": level " .. p.level) or ("Game " .. i .. ": empty")
  end
  return rows
end
function save_game(i) save({ level = level, hp = hp }, i + 1) end
function load_game(i) local p = saved(i + 1) if p then level, hp = p.level, p.hp end end
```

### Documents

The player's files, shared by the apps: `/docs` on the SD card (bm Write keeps its `.BMD`
there, with the exports `.TXT`, `.MD`, `.HTM` and `.PDF`). Names are 8.3 without folders
(`"LETTER.BMD"`; lower case is fine, the file is written in upper case); the folder is made
by the first write. The first time an app uses them the console asks the player, as for the
network; the answer stays in `bm/config.txt` (`allow_...=docs=yes`), and after a no every
call returns `nil` and the reason.

| Function | Description |
|---|---|
| `doc_list()` | the files in `/docs`: `{ {name = "LETTER.BMD", size = 1234}, ... }` (empty if there are none); `nil` and the reason if the player did not allow it |
| `doc_read(name)` | the bytes of a document (a string), or `nil` and the reason |
| `doc_write(name, bytes)` | writes a document, new or replaced (at most 4 MiB); `true`, or `nil` and the reason |
| `doc_delete(name)` | deletes a document; `true`, or `nil` and the reason |

```lua
local ok, why = doc_write("NOTES.TXT", "shopping:\nbread\nmilk\n")
if not ok then msg = why end
for _, d in ipairs(doc_list() or {}) do print(d.name .. " " .. d.size) end
```
