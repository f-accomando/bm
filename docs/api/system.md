# API: Time, network, saves, documents

Part of the game API. Index and basics: [API.md](../API.md).

### Time and system

| Function | Description |
|---|---|
| `time()` | seconds since the cartridge started (with decimals) |
| `stat(n)` | 0 KB used by Lua, 1 ms of the last frame (`_update` + `_draw`, with the GPU's 3D), 2 fps, 3 frame number, 4 3D triangles, 5 3D pixels (0 with the GPU), 6 ms spent in 3D drawing (since `zclear`; with the GPU the ARM's part), 7 3D vertices transformed, 8 ms since the start of this frame (to measure the phases), 9 `1` if the GPU draws the 3D, 10 Lua instructions of the last frame (`_update` + `_draw`; the count, rounded to the thousand); the **dev kit** (2026-10-04): 11 tokens of the cartridge's code (`code_tokens`), 12 the most KB of Lua of this run, 13 KB of the cartridge's data in memory (sprite sheet, map, models and skeletons, sound bank, the 3D z-buffer), 14 the Lua instructions of the busiest frame of this run; 15 how many `_update` ran before this `_draw` (1; more with `frameskip`); 16 ms of GPU job time (bin + render) accounted in the last frame, `0` when the ARM draws the 3D (2026-10-12; with `stat(1)` it tells whether the GPU or the ARM sets the pace); the profiling metrics (bm3D 7.x): 17 ms of audio processing in the last frame (the time of the audio interrupt), 18 ms the ARM waited for the GPU in the last frame (time blocked on GPU job completion). Use 1, 16, 17 and 18 together to see if the CPU/ARM or the GPU is the bottleneck. 18 is `0` when the ARM draws the 3D. On the RGB30 and in QEMU, 16 and 18 are `0` (no V3D jobs there), and 17 is `0` where the console has no audio interrupt to time. **A measurement only**: it differs between consoles, so never use it (nor 1, 2, 6, 8, 10, 16, 17, 18) in game logic, or a lockstep match falls out of step |
| `frameskip([n])` | the game's time at 60 `_update` a second whatever `_draw` costs (2026-10-05): when a frame takes longer than 1/60 s, up to `n` `_update` run before the next `_draw` (the frames in between are not drawn), so a game that moves 1/60 s per `_update` does not slow down; past `n` the time is let go (the game slows rather than never drawing). `1` (the default) is one `_update` a frame, as before; at most 8. Returns the old value. A button pressed counts once in `btnp()` (and `mousep()`, the wheel) however many `_update` see it; `btn()` stays held. Overbit uses `frameskip(4)` (its benchmark `1`) |
| `devkit([mode])` | the dev kit's overlay: `0` off, `1` at the top right, `2` at the bottom right (`3`, the old functions page, is the top: old games keep working); with a mode it shows it (a game's own key for it, e.g. Select on a pad: F11 is the keyboard's), in this run only: every game starts as Settings says (off by default). Returns the old mode. To go round it: `devkit((devkit() + 1) % 3)` |
| `profile([on])` | the profiler of functions (R14): the functions that cost the most in the last whole second (60 frames), the costliest first, up to 32: `{name, where, c, self, total, calls}` (`where` `"main.lua:120"` or `"[C]"`; `c` true for the console's functions and Lua's written in C; `self` and `total` ms a frame, its own time and with what it calls; `calls` the calls a frame, C ones only), and the frames measured (0 during the first second). `profile(true)` turns it on for the run (the session report then lists the ten costliest), `profile(false)` turns it off (and forgets). E.g. `for _, f in ipairs(profile()) do log(f.name, f.self) end` |
| `devinfo(line, ...)` | up to 4 lines of the game in the dev kit's session report ("the game says:", as the run ended: its quality, its actors...), 18 characters each; `devinfo()` none. Call it again when they change (Overbit every frame while the overlay is on) |
| `battery()` | the console's battery (2026-10-10): its charge `0`–`100` and `true` on the charger (`local pct, plugged = battery()`); `nil` without a battery (the Pi). On the RGB30 the charge comes from the voltage (an indication: it sags under load and rises on the charger); the charger is seen within a quarter of a second |
| `battery_low()` | `true` while the battery is low: 20% or less off the charger (until 23%). Then the system draws by itself a small red battery top right over the frame (left of the dev kit's overlay when it is at the top), in every game and tool; *Settings > Screen > Low battery icon* (`battery_icon=0` in `bm/config.txt`) turns that icon off, not `battery_low()`. Information only: each console has its own battery, so in lockstep (Overbit, `bmnet`) it never changes what the game simulates; use it to show something or to save |
| `code_tokens(text)` | the **tokens** of a piece of Lua code, counted as `stat(11)` and the SDK's dev kit do (`src/bm/tokens.c`): each name, keyword, number, string and operator is one; comments, spaces, `,` `.` `:` `;` `::`, closing brackets (`)` `]` `}`), `end` and `local` do not count, nor the minus sign in front of a number (`-1` is one token). Information, not a limit: bm puts no ceiling on tokens |
| `log(...)` | writes in the kernel's log (serial line and console), not on the game's screen |
| `report(kind, text)` | a report for the people who develop bm (2026-10-04): saved in `bm/reports` on the SD card with kernel, branch, board and date (and a first `options` line: the 3D options in force, `docs/BENCH3D.md`), then sent to the reports' repository if there are `github_token` and the network (`src/kernel/reports.h`); at most 8 per run, 256 KB each; `true` if saved. The first time a game calls it, the player is asked (the answer stays in `bm/config.txt`, `allow_...`): after a no, `false` |
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
- **Size.** The body is cut off at the cap: 4 MB by default. `web_max_kib` in `bm/config.txt` changes it (at most 32768). A cartridge can ask for less with `max`.
  A body over the cap ends in the state `"error"` and nothing is kept.
- **One request runs at a time.** The result of up to 3 requests stays in memory until `web_cancel` or the end of the cartridge.
- **The cost of one frame.** `web_poll` works for about 4 ms. The TLS handshake of a new connection does not give the CPU back (about 100 ms on the Pi Zero W,
  measured for the reports in `docs/progress/multitasking.md`; not measured inside a cartridge). Show a "connecting" message before the first `web_poll`.
- **One connection is kept.** After a complete answer the kernel keeps the connection and uses it for the next request to the same host
  (keep-alive): no new TLS handshake for a page and its pictures. The pause per host still applies. A server that closes the connection, or a request to
  another host, costs one new connection. Nothing changes in the calls.
- **Resume.** `web_get` with `resume = "NAME.EXT"` keeps what came in `bm/web/name.prt` when the request fails (a lost connection, a timeout) or is cancelled, and about
  every 1 MB. The next `web_get` for the same address with the same `resume` name asks the server for the rest (`Range`). If the server sends the whole file
  (it changed, or it ignores `Range`), the saved start is dropped. The body is always the whole file. The part file is deleted when the request is done. It needs a server
  that sends `Accept-Ranges` and a size; else nothing is kept. The cap and the 128 files count it. Check the whole file (its size, a hash) before you use it.
- **A queued write.** `file_write(name, data, true)` copies the data and returns at once. The file goes to the card in pieces of up to 32 KB while the cartridge calls
  `web_poll` or `file_state` (3 to 4 ms each); the cartridge ends with all of them written. Up to 4 files wait. A game `save()` or a `doc_write()` pauses the queued write,
  and it starts again from its first byte.
- **Models (`.bmm`).** `file_write("house.bmm", bytes_or_id)` saves a model in `/bmm/house.bmm`, where the 3D editors list it and where the tools' `res_write` puts models. These rules hold:
  - The kernel checks the bytes first, with the check that loads a model file (`bm_parse_any`, then `bm_mesh_check`). The magic is `BMRES`, the kind is 1 (model) and the file has a MESH section. A file that fails is refused and nothing is written. It is not written in `bm/web` either.
  - The name is the usual one: 1 to 8 of `a-z 0-9 _ -` (upper case is made lower case), and `.bmm`. `web/`, `bm/web/` and `/bm/web/` before the name are ignored. The file always goes to `/bmm`.
  - A model that is there is **never replaced**. The call returns `nil` and `"a model of that name is there already"`. The same rule as `res_write` without `replace`.
  - The size cap is `file_write_max_kib` (8 MB). The file is written at once, also with `async = true`. `file_state` has nothing for it. The path in the result is `"bmm/house.bmm"`.
  - **No question to the player.** `res_write` asks nothing either: it only runs in the tools (`tool_mode`). The weakest safe rule for a cartridge is this: the file has no code, it must pass the model check, it cannot replace a file, and the download itself already needs the network question (`web_get`). A cartridge can write other bytes in `/bmm` only as a valid model.
  - A cartridge still cannot write a `.bm`, `.bme` or any other file the kernel runs or reads as a setting.
- **bmhost.** `web_get` returns `nil` and a reason. `image_load` and `file_write` work (the host folder is the card, `/bmm` is a folder of it, as for `res_write`).

| Function | Description |
|---|---|
| `id = web_get(url, [opts])` | starts a GET request. `opts` is a table: `max` (a body cap in bytes, lower than the configured cap), `timeout` (milliseconds for one read, 1000 to 60000, default 15000), `resume` (a bare 8.3 name in `bm/web`, see Resume above). Returns the request `id`, or `nil` and the reason (bad address, no network, the player said no, a request already runs, no free slot) |
| `state, size, length = web_poll(id)` | works the request for about 4 ms. `state` is `"wait"` (the pause per host), `"busy"`, `"done"` or `"error"`. `size` is the body bytes received so far. `length` is the Content-Length (`nil` if the server did not give it). `nil` and a reason for an `id` that is not there. Call it once a frame |
| `web_info(id)` | a table: `state`, `status` (the HTTP status), `type` (Content-Type), `length`, `location` (the Location header of the last answer), `url` (the address reached), `size`, `resumed` (the bytes that came from the part file) and `error`. Fields that are not known are left out. Any HTTP status ends in `"done"`, a 404 too: look at `status`. A failed request (no connection, a refused redirect, a body over the cap, a timeout) ends in `"error"` |
| `web_body(id)` | the body of a finished request as a string, or `nil` and the reason. It copies the bytes into Lua memory: to decode a picture or write a file, give `id` to `image_load` or `file_write` instead |
| `web_cancel(id)` | stops the request and frees its result. Returns `true` if it was there |
| `img = image_load(bytes_or_id, sx, sy, [maxw, maxh])` | decodes a picture into the sprite sheet with its top left corner at `(sx, sy)`. The first argument is a string of bytes, or the `id` of a finished `web_get` (the body is not copied). It reads **baseline JPEG, PNG and WebP** (lossy and lossless). Progressive JPEG is refused. A picture over 4 megapixels is refused. A part that does not fit `maxw`×`maxh` or the sheet is cut at the right and the bottom. PNG pixels with alpha below 128 are transparent. Returns `{x =, y =, w =, h =}` (the part of the sheet used), or `nil` and the reason. Draw it with `sspr(img.x, img.y, img.w, img.h, dx, dy)`. The sheet is 256×256 by default: make it bigger with `cart_sheet(w, h)` first. The pixels are RGB565 |
| `file_write(name, bytes_or_id, [async])` | writes a file in `bm/web` on the SD card (the folder is made if needed). `name` is a bare 8.3 name (`"MODEL.GLB"`), or the same after `web/`, `bm/web/` or `/bm/web/`. No `..`, no other folder, no leading dot, never a `.bm` or `.bme` file. **A name that ends in `.bmm` is a model:** the kernel checks the bytes as it loads a model file (BMRES, kind 1, a MESH section it accepts) and writes `/bmm/NAME.bmm` (see the Models rule above the table). The second argument is a string, or the `id` of a finished `web_get`. A file with the same name is replaced. At most 8 MB (`file_write_max_kib` in `bm/config.txt`, at most 32768) and 128 files in the folder. Returns `true` and the path (`"bm/web/MODEL.GLB"`), or `nil` and the reason. Without `async` it waits for the card: a big file stops the frame. With `async = true` the data is copied and queued, the call returns `true` at once and `file_state` says when the file is on the card. It refuses with "busy" if another writer that cannot be paused holds the card: try again next frame |
| `state, info = file_state(name)` | the last queued `file_write` of that name: `"queued"`, `"writing"` (and the bytes on the card), `"done"` (and the size) or `"error"` (and the reason); `nil` and a reason if there is none. It also works the queue for about 3 ms: call it once a frame while a write is queued. The results stay until the cartridge ends or 4 newer writes need the place |

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

### A chat model (ai_ask, ai_reply, ai_info)

A cartridge asks a cloud chat model through the player's own server and key. The kernel sends
`POST <ai_url>/chat/completions` in the OpenAI-compatible format. Gemini (its OpenAI-compatible endpoint),
Groq, OpenRouter, Mistral and `tools/bm_agent_relay.py` accept it. The app that uses it is `carts/agent` (bm Agent).
The design and the providers' terms are in [ai-agent](../design/ai-agent.md).

- **The player sets the server.** The settings are in `bm/ai.txt`, apart from `bm/config.txt` (the system's): `url=` (a base
  such as `https://api.groq.com/openai/v1`, or the full endpoint), `model=` and `key=`. Each value has at most 127 characters.
  bm Agent's `/setup` writes them through `ai_setup` or `ai_pair` (below). The user can also write the file on the SD card from a PC.
- **The file is protected.** No call reads or writes `bm/ai.txt` or `bm/ca-user.pem`. The kernel writes them only after the
  player says yes to the kernel's own question. The network transfer from a PC refuses both names (`BF`). Keys `ai_url`,
  `ai_model` and `ai_key` found in `bm/config.txt` (the first version) move to `bm/ai.txt` at the first read.
- **The key stays in the kernel.** The kernel adds `Authorization: Bearer <key>` to the request. No call gives the key
  to a cartridge. A cartridge cannot choose the server: only `url` is used. Without `key` no `Authorization` is sent.
- **The rules of `web_get` apply.** https only, the certificate chains to `bm/ca.pem`, the player allows the network, a pause of
  1 s per host, one request at a time, 3 results kept. The answer's cap is 1 MB.
- **No redirect.** A POST follows no redirect: a 3xx answer ends in `"error"`.
- **The request is a web request.** Advance it with `web_poll(id)` once a frame. Free it with `web_cancel(id)`. `web_info(id)` and
  `web_body(id)` work on it too.
- **bmhost.** `ai_ask` returns `nil` and a reason, as `web_get`.

| Function | Description |
|---|---|
| `info = ai_info()` | the setup in `bm/ai.txt`: `{ready = true, host =, model =, key = bool}`, or `{ready = false, why =}`. `key` says only whether a key is set |
| `id = ai_ask(messages, [opts])` | starts a request. `messages` is a string (one user message) or a list `{ {role =, content =}, ... }` of 1 to 64 messages, `role` `"system"`, `"user"` or `"assistant"`, at most 192 KB of text. `opts`: `system` (a system message put first), `model` (instead of `ai_model`: 1 to 96 of `a-z A-Z 0-9 - _ . : / @`), `max_tokens` (1 to 32768, default 1024), `temperature` (0 to 2; the server's default if missing), `timeout` (milliseconds for one read, 1000 to 180000, default 60000). Returns the request `id`, or `nil` and the reason (no server set, a bad message, no network, the player said no, a request already runs) |
| `ok = ai_setup({url =, model =, key =})` | saves the settings in `bm/ai.txt` after the kernel's question (server, model, the last 4 characters of the key). A field left out stays. A new `url` needs its `key` in the same call. Returns `true`, or `nil` and the reason (a bad field, the player said no) |
| `id = ai_pair(address, code)` | starts the pairing with `tools/bm_agent_relay.py`: `address` is the PC's LAN address (`"192.168.1.20"`, or with `:port` if not 8443), `code` the 20 characters the relay prints. The kernel fetches the relay's CA over plain http at the next port, on a private LAN address only. Poll the `id` with `web_poll`. Returns the `id`, or `nil` and the reason |
| `ok = ai_pair_finish(id)` | after `web_poll` says `"done"`: checks the CA against the code (the first 12 characters are its fingerprint, SHA-256 of the DER), asks the player, then saves the CA in `bm/ca-user.pem` and `url`, `model=default` and `key` (the last 8 characters of the code) in `bm/ai.txt`. Returns `true`, or `nil` and the reason. Free the request with `web_cancel(id)` |
| `text, info = ai_reply(id)` | the answer of a finished request: the text of the first choice, and `{finish =, prompt_tokens =, completion_tokens =, status =}` (the tokens if the server gave them). `nil` and the reason if the request is not finished, failed, or the server answered an error (`"HTTP 429: ..."` with the server's own message) |

```lua
local req, answer
function _update()
  if btnp("ok") and not req then
    req = ai_ask({ { role = "user", content = "Name three 8-bit consoles." } }, { max_tokens = 200 })
  end
  if req then
    local state = web_poll(req)
    if state == "done" or state == "error" then
      local text, info = ai_reply(req)
      answer = text or ("no answer: " .. info)
      web_cancel(req)
      req = nil
    end
  end
end

function _draw()
  cls(0)
  print(answer or (req and "thinking..." or "press ok"), 8, 8)
end
```

### Saves

| Function | Description |
|---|---|
| `save(t, [slot])` | saves table `t` on the SD card, in slot 1–8 (default 1); `true`, or `false` and the reason (no SD card, card full...) |
| `saved([slot])` | the table saved last time in that slot (default 1), or `nil` |
| `saves()` | the slots in use, `{[slot] = bytes}` (e.g. `{[1] = 40, [3] = 212}`), and how many there are (8): for a "load game" page |
| `delsave([slot])` | empties the slot (default 1); `true`, or `false` and the reason (`"nothing saved in slot 3"`) |

Each cartridge has **8 save slots**, each a table of up to 32 KB, in `/bm/save/` on the SD
card: slot 1 is `xxxxxxxx.sav` (the one of always: `save(t)` and `saved()` without a slot),
the others `xxxxxxxx.s02` ... `.S08`. The name depends on title and author: changing them
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
(`"letter.bmd"`; the file is always written in lower case, and a read finds it in any case); the folder is made
by the first write. The first time an app uses them the console asks the player, as for the
network; the answer stays in `bm/config.txt` (`allow_...=docs=yes`), and after a no every
call returns `nil` and the reason.

| Function | Description |
|---|---|
| `doc_list()` | the files in `/docs`: `{ {name = "letter.bmd", size = 1234}, ... }` (empty if there are none); `nil` and the reason if the player did not allow it |
| `doc_read(name)` | the bytes of a document (a string), or `nil` and the reason |
| `doc_write(name, bytes)` | writes a document, new or replaced (at most 4 MB); `true`, or `nil` and the reason |
| `doc_delete(name)` | deletes a document; `true`, or `nil` and the reason |

```lua
local ok, why = doc_write("notes.txt", "shopping:\nbread\nmilk\n")
if not ok then msg = why end
for _, d in ipairs(doc_list() or {}) do print(d.name .. " " .. d.size) end
```
