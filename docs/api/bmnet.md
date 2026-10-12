# API: bmnet

Part of the game API. Index and basics: [API.md](../API.md).

## bmnet: games over the network

`local net = require "bmnet"` (2026-10-04): what Overbit's network code does, for every
game. The consoles find each other on the **LAN** (UDP broadcasts) or over the
**internet** through a relay (`tools/overbit_relay.py`: a room passes every packet to the
other consoles, as a broadcast would); one **hosts** the match and the others **join**;
they send **messages** (that may be lost, or sure and in order) and, for action games,
the match runs in **lockstep**: every console runs the whole game with the same seed and
only the players' inputs travel; a frame runs when everybody's inputs for it are there
(the host gathers them and sends them to all). The SDK's *Online 2D* template is a whole
example; the packets are described at the top of `src/script/bmnet.lua`.

| Function | Description |
|---|---|
| `net.open([opt])` | opens the network: `game` (4 characters, the game and its version: the packets of other games are not seen), `port` (47320, the same on every console), `relay` (`"name"` or `"name:port"`, 47310: over the internet; without, the LAN), `room` (4 characters, `"PLAY"`: the consoles of a room see each other on the relay), `name` (the player's name). `true`, or `false` and why (no network) |
| `net.close()` | leaves: the others know (in a match the seat's input becomes `false`; when the host leaves the match ends for all), the socket closes. Call it from `_leave()` too |
| `net.host([opt])` | this console hosts a match: `max` players (2–8, 4), `info` (a line the others see in the list). It is seat 1 |
| `net.hosts()` | the matches found in the last 3 seconds: `{id, name, players, max, info}`, by id |
| `net.join(id)` | asks console `id` for a seat: the event `"joined"` when it says yes, `"refused"` if it is full or has begun |
| `net.peers()` | the consoles in the room or the match: `{id, name, seat, me}`, by seat |
| `net.start([opt])` | (the host) starts the match with those in: `seed` (the same random numbers on every console; one by chance if missing), `delay` (frames between a press and its frame: 4 on the LAN, 7 through the relay), `data` (a string for everybody: the options chosen). Everybody gets the event `"start"` (the host too, at the next `net.update()`) |
| `net.update()` | once in each `_update`: the packets that came, the lobby's announcements, the sure messages to send again. Returns the frame's **events**, a list of `{type = ...}`: `"join"` (`id, name, seat`), `"leave"` (`seat, id`), `"joined"` (`seat`), `"refused"`, `"start"` (`seed, seat, seats, host, data`), `"msg"` (`from, data, sure`), `"lost"` (`why`: the host has gone), `"desync"` (`frame`), `"error"` (`text`) |
| `net.send(data, [to])` | a message (a string up to 900 bytes) to everybody or to console `to` (an id): it may be lost or come after a newer one (positions) |
| `net.post(data, [to])` | a **sure** message: repeated until it arrives, and each console gets another's in the order they left (chat, a turn, a choice) |
| `net.input(v)` | (lockstep) my input for frame `net.frame + delay`: a 32-bit whole number (`net.pad(1)`). Once in each `_update` of the match, before `net.frames()` |
| `net.frames()` | the frames of the match that can run now: `for f, inputs in net.frames() do ... end`, `inputs[seat]` that seat's input (`false` once its player has left). One, two when the console is behind; none while an input is missing (`net.stall`: seconds since the last) |
| `net.check(hash)` | a number that sums up the game after the frame just run (positions, scores): the consoles compare them and a difference is the event `"desync"`. Every second is enough |
| `net.pad([p])` / `net.unpad(v)` | the 16 buttons of `pad(p)` and the left stick (8 bits each way) in 32 bits / back to buttons, `x`, `y` (−1..1) |
| `net.held(v, button)` | a button (`"a"`, `"left"`... as `pad()`) held in an input of `net.pad` |

Fields: `net.state` (`"off"`, `"lobby"`, `"hosting"`, `"joining"`, `"joined"`,
`"playing"`, `"lost"`), `net.me` (my id), `net.seat`, `net.seats` (the match's seats),
`net.is_host`, `net.frame` (the next frame), `net.seed`, `net.delay`, `net.data`,
`net.error`.

**The rules of lockstep.** The match's game depends **only** on the frames' inputs and the
seed: random numbers with `lib.rng(seed)` (never `math.random`, `time()` or `stat()`),
nothing of the console (the camera, the quality) inside the simulation, no game state
changed in `_draw`. The drawing may differ on every console. Two players on the same
console in a network match put their inputs in the same number (16 bits each).
`online(true)` is turned on by `bmnet` when the match begins: PS asks before leaving and
calls `_leave()`.

```lua
local net = require "bmnet"
local lib = require "bmlib"
local rng

function _init() net.open({ game = "MYG1" }) end
function _leave() net.close() end

function _update()
  for _, e in ipairs(net.update()) do
    if e.type == "start" then rng = lib.rng(e.seed) end
  end
  if net.state == "lobby" and btnp("a") then net.host({ max = 2 }) end
  if net.state == "hosting" and btnp("start") then net.start() end
  if net.state == "playing" then
    net.input(net.pad(1))
    for _, inputs in net.frames() do step(inputs) end   -- the game, the same everywhere
  end
end
```

**Trying it on the PC.** Two `bmhost` on the same PC are two consoles: `BMHOST_NET_ID=0`
and `BMHOST_NET_ID=1`, with `--realtime`; the relay runs with `tools/overbit_relay.py
--port N`. `make test-bmnet` does it: the lobby, sure messages with a fifth of the packets
lost (`loss = 0.2` in `net.open`, for tests only), a lockstep match the same on both
consoles, a player leaving; on the LAN and through the relay.
