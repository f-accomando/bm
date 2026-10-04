-- bmnet: games over the network (2026-10-04): local net = require "bmnet"
--
-- What Overbit's network code does (carts/overbit/src/83_net.lua), for
-- every game: consoles that find each other on the LAN (UDP broadcasts) or
-- over the internet through a relay (tools/overbit_relay.py: a room passes
-- every packet to the others, as a broadcast would), a lobby (a console
-- hosts, the others join), messages (that may be lost, or sure and in
-- order) and the lockstep of action games: every console runs the whole
-- game with the same seed and only the players' inputs travel; a frame runs
-- when the inputs of everybody for it are there (docs/API.md, "bmnet").
--
-- Packets: "BMN1", the game (4 bytes), the type, from (u16), to (u16, 0:
-- everyone), then:
--   P here (for the relay)              H host: players, max, name, info
--   J join: name                        A accepted: id, seat (0: no room)
--   L the room: n, then (id, seat, name) S start: seed, delay, n, (id, seat)..., data
--   M message: data                     R sure message: seq, data
--   K received: seq                     Q leaving: seat, 1 for the host
-- and in a match, after the match's number (u16):
--   I inputs: seat, last frame, KEEP values
--   B bundle: last frame, n, seats, KEEP x (gone mask, n values)
--   X check: frame, hash

local net = { VERSION = 1, state = "off", me = 0, seat = 0, seats = {}, frame = 0, is_host = false }

local floor, min, max = math.floor, math.min, math.max
local DT = 1 / 60
local PORT, RELAY_PORT = 47320, 47310
local lan_port = PORT
local KEEP = 8                          -- inputs repeated in every packet (losses)
local MAX_DATA = 900                    -- bytes of a message
local MAX_SEATS = 8

local sock, game4 = nil, "GAME"
local relay, relay_ip, relay_port, room = nil, nil, RELAY_PORT, "PLAY"
local name = "Player"
local loss, lrng = 0, 1                 -- tests: a part of the packets lost
local clock = 0
local events = {}
local hosts = {}                        -- id -> a match found: {id, name, players, max, info, t, ip, port}
local peers = {}                        -- id -> {id, name, seat, t, ip, port}
local target                            -- the host we join
local host_heard = 0                    -- clock when the host was last heard
local host_max, host_info = 4, ""
local last_ann = -1                     -- clock of the last announcement
local started_t                         -- host: when the match began (S is repeated for 2 s)
local start_msg

-- sure messages
local out_seq, in_seq, pending, early = {}, {}, {}, {}

-- the match
local frames, mine, got, hashes = {}, {}, {}, {}
local heard, gone = {}, {}              -- seat -> clock when last heard / true once its player left
local mid = 0
local last_v = 0                        -- my last input (the frames run twice in an update repeat it)
local last_bundle = 0
local desync_said = false

local function ev(t) events[#events + 1] = t end

local function rand()                   -- xorshift32: not math.random (the game's)
  lrng = lrng ~ (lrng << 13) & 0xFFFFFFFF
  lrng = lrng ~ (lrng >> 17)
  lrng = lrng ~ (lrng << 5) & 0xFFFFFFFF
  return lrng / 4294967296
end

----------------------------------------------------------------- packets

local function raw(typ, to, payload, ip, port)
  if not sock then return false end
  local p = "BMN1" .. game4 .. typ .. string.pack("<I2I2", net.me, to or 0) .. (payload or "")
  if relay then
    if not relay_ip then return false end
    return udp_send(sock, relay_ip, relay_port, "OBR1" .. room .. p)
  end
  return udp_send(sock, ip or "*", port or lan_port, p)
end

-- to a console (its address if it is known: on the LAN), or everyone
local function send(typ, to, payload)
  local pe = to and to ~= 0 and (peers[to] or hosts[to])
  return raw(typ, to, payload, pe and pe.ip, pe and pe.port)
end

-- the next packet for us: type, from, payload, address, port
local function recv()
  while sock do
    local d, ip, port = udp_recv(sock)
    if not d then return nil end
    if #d >= 13 and d:sub(1, 4) == "BMN1" and d:sub(5, 8) == game4 and not (loss > 0 and rand() < loss) then
      local from, to = string.unpack("<I2I2", d, 10)
      if from ~= net.me and (to == 0 or to == net.me) then
        return d:sub(9, 9), from, d:sub(14), ip, port
      end
    end
  end
  return nil
end

----------------------------------------------------------------- open and close

-- opens the network: o.game (the game's tag, 4 characters: the packets of
-- other games are not seen; put the version in it, "PNG2"), o.port (47320,
-- the same on every console), o.relay ("name" or "name:port", 47310: over
-- the internet through tools/overbit_relay.py; the LAN without), o.room (4
-- characters, "PLAY": the consoles of a room see each other on the relay),
-- o.name (the player's name, "Player"). true, or false and why.
function net.open(o)
  o = o or {}
  net.close()
  if not net_ip() then return false, "no network" end
  local s, err = udp_open(o.port or PORT)
  if not s then return false, err or "no socket" end
  sock = s
  lan_port = o.port or PORT
  game4 = ((o.game or "GAME") .. "    "):sub(1, 4)
  name = (o.name or "Player"):sub(1, 24)
  room = ((o.room or "PLAY") .. "____"):sub(1, 4)
  relay, relay_ip, relay_port = nil, nil, RELAY_PORT
  if o.relay and o.relay ~= "" then
    local h, p = o.relay:match("^(.-):(%d+)$")
    relay, relay_port = h or o.relay, tonumber(p) or RELAY_PORT
  end
  -- my number: the address's last byte (different on a LAN) and the clock
  local last = tonumber(net_ip():match("(%d+)$")) or 1
  local id = (last % 256) * 256 + (floor(time() * 1000) + stat(3) * 7) % 256
  net.me = id == 0 and 1 or id
  lrng = net.me * 2654435761 % 4294967296
  if lrng == 0 then lrng = 1 end
  loss = o.loss or 0
  clock, events, hosts, peers, target = 0, {}, {}, {}, nil
  out_seq, in_seq, pending, early = {}, {}, {}, {}
  net.state, net.seat, net.seats, net.is_host, net.error = "lobby", 0, {}, false, nil
  return true
end

-- leaves: the others are told (in a match the host gives the seat back, a
-- host ends the match for all), the socket closes. Call it from _leave()
-- too: function _leave() net.close() end
function net.close()
  if sock then
    if net.state == "playing" or net.state == "joined" or net.state == "hosting" then
      for _ = 1, 2 do send("Q", 0, string.pack("<I2BB", mid, net.seat, net.is_host and 1 or 0)) end
    end
    udp_close(sock)
  end
  if net.state == "playing" and online then online(false) end
  sock = nil
  net.state, net.is_host = "off", false
end

----------------------------------------------------------------- the lobby

-- this console hosts a match: o.max players (2..8, 4), o.info (a line the
-- others see in the list: the mode, the map). It is seat 1.
function net.host(o)
  o = o or {}
  host_max = min(max(o.max or 4, 2), MAX_SEATS)
  host_info = (o.info or ""):sub(1, 40)
  peers = { [net.me] = { id = net.me, name = name, seat = 1, t = clock } }
  net.state, net.is_host, net.seat, target = "hosting", true, 1, nil
  last_ann = -1
end

-- the matches found (on the LAN or in the relay's room) in the last 3 s:
-- {id, name, players, max, info}, by id
function net.hosts()
  local out = {}
  for _, h in pairs(hosts) do
    out[#out + 1] = { id = h.id, name = h.name, players = h.players, max = h.max, info = h.info }
  end
  table.sort(out, function(a, b) return a.id < b.id end)
  return out
end

-- asks the host id for a seat: the event "joined" when it says yes
function net.join(id)
  target, host_heard = id, clock
  net.state, net.is_host, net.seat = "joining", false, 0
  last_ann = -1
end

-- the consoles in the room or the match: {id, name, seat, me}, by seat
function net.peers()
  local out = {}
  for _, p in pairs(peers) do
    out[#out + 1] = { id = p.id, name = p.name, seat = p.seat, me = p.id == net.me }
  end
  table.sort(out, function(a, b) return a.seat < b.seat end)
  return out
end

local function room_list()
  local parts, n = {}, 0
  for _, p in pairs(peers) do
    n = n + 1
    parts[#parts + 1] = string.pack("<I2Bs1", p.id, p.seat, p.name)
  end
  return string.pack("<B", n) .. table.concat(parts)
end

----------------------------------------------------------------- the match

local function begin(seed, delay, seats, data)
  net.state, net.seed, net.delay, net.data = "playing", seed, delay, data
  net.seats, net.frame = seats, 0
  mid = seed % 65536
  for _, t in ipairs({ frames, mine, got, hashes, heard, gone }) do
    for k in pairs(t) do t[k] = nil end
  end
  for f = 0, delay - 1 do               -- the first frames: nobody presses anything
    local b = {}
    for _, s in ipairs(seats) do b[s] = 0 end
    frames[f] = b
    mine[f] = 0
  end
  for _, s in ipairs(seats) do heard[s] = clock end
  last_bundle, desync_said, last_v = clock, false, 0
  net.stall = 0
  if online then online(true, net.is_host and "You are the host: the match ends for all." or nil) end
  ev({ type = "start", seed = seed, seat = net.seat, seats = seats, host = net.is_host, data = data })
end

-- the host starts the match with the consoles in the room: o.seed (the
-- match's: the same random numbers everywhere; one by chance if missing),
-- o.delay (frames between a press and its frame: 4 on the LAN, 7 through
-- the relay), o.data (a string for everybody: the options chosen)
function net.start(o)
  assert(net.is_host and net.state == "hosting", "bmnet: only the host starts, from the lobby")
  o = o or {}
  local seed = o.seed or floor(rand() * 0x3FFFFFFF) + 1
  local delay = o.delay or (relay and 7 or 4)
  local data = o.data or ""
  local list, seats = net.peers(), {}
  local parts = { string.pack("<I4BB", seed, delay, #list) }
  for _, p in ipairs(list) do
    parts[#parts + 1] = string.pack("<I2B", p.id, p.seat)
    seats[#seats + 1] = p.seat
  end
  parts[#parts + 1] = string.pack("<s2", data)
  start_msg, started_t = table.concat(parts), clock
  send("S", 0, start_msg)
  begin(seed, delay, seats, data)
end

-- the host: the bundles of the frames everybody has sent
local function gather()
  for _, s in ipairs(net.seats) do
    if s ~= net.seat and not gone[s] and clock - heard[s] > 3 then
      gone[s] = true
      ev({ type = "leave", seat = s })
    end
  end
  for fr = net.frame, net.frame + net.delay do
    if not frames[fr] then
      local g = got[fr]
      if not g then break end
      local ok = true
      for _, s in ipairs(net.seats) do
        if g[s] == nil and not gone[s] then ok = false break end
      end
      if not ok then break end
      local b = {}
      for _, s in ipairs(net.seats) do b[s] = not gone[s] and g[s] end
      frames[fr] = b
    end
  end
  local last = net.frame + net.delay
  while last >= net.frame and not frames[last] do last = last - 1 end
  if last >= 0 and frames[last] then
    local n = #net.seats
    local parts = { string.pack("<I2I4B", mid, last, n) }
    for _, s in ipairs(net.seats) do parts[#parts + 1] = string.char(s) end
    for k = 0, KEEP - 1 do
      local b = frames[last - KEEP + 1 + k]
      local mask, vals = 0, {}
      for i, s in ipairs(net.seats) do
        local v = b and b[s]
        if b and v == false then mask = mask | (1 << (i - 1)) end
        vals[i] = string.pack("<I4", v or 0)
      end
      parts[#parts + 1] = string.char(mask) .. table.concat(vals)
    end
    send("B", 0, table.concat(parts))
  end
end

-- my input for frame net.frame + delay: out to the host, or into its
-- bundles
local function record(v)
  local f = net.frame + net.delay
  if mine[f] == nil then mine[f] = v end
  if net.is_host then
    got[f] = got[f] or {}
    got[f][net.seat] = mine[f]
    gather()
  else
    local recs = {}
    for k = f - KEEP + 1, f do recs[#recs + 1] = string.pack("<I4", mine[k] or 0) end
    send("I", target, string.pack("<I2BI4", mid, net.seat, f) .. table.concat(recs))
  end
end

-- my input for the frame net.frame + delay: a whole number of 32 bits (the
-- buttons: net.pad(1)). Once in each _update of the match, before
-- net.frames(); the first call of a frame counts.
function net.input(v)
  if net.state ~= "playing" then return end
  last_v = floor(v or 0) & 0xFFFFFFFF
  record(last_v)
end

-- the frames of the match that can run now: for f, inputs in net.frames()
-- do ... end, inputs[seat] the seat's input (false once its player has
-- left). One frame, two when this console is behind (the second with the
-- same input of mine); none while an input is missing (net.stall: seconds
-- since the last frame ran).
function net.frames()
  local n = 0
  return function()
    if net.state ~= "playing" or n >= 2 then return nil end
    if n == 1 and not (frames[net.frame] and frames[net.frame + 1]) then return nil end
    local b = frames[net.frame]
    if not b then return nil end
    if n == 1 then record(last_v) end
    n = n + 1
    local f = net.frame
    frames[f - KEEP * 4], mine[f - KEEP * 4], got[f - KEEP * 4] = nil, nil, nil
    net.frame = f + 1
    net.stall = 0
    local copy = {}
    for _, s in ipairs(net.seats) do copy[s] = b[s] end
    return f, copy
  end
end

-- the hash of the game after the frame just run (a whole number: where
-- everybody is, the score): the consoles compare them, a difference is the
-- event "desync" (the game has gone its own way on a console). Every
-- second or so is enough.
function net.check(h)
  if net.state ~= "playing" then return end
  local fr = net.frame - 1
  h = floor(h) & 0xFFFFFFFF
  hashes[fr] = h
  hashes[fr - 600] = nil
  send("X", 0, string.pack("<I2I4I4", mid, fr, h))
end

-- player p's controller in 32 bits: pad(p)'s 16 buttons and the left
-- stick (8 bits each way); net.unpad(v) gives them back
function net.pad(p)
  local x, y = stick(p)
  local sx, sy = floor(x * 127 + 0.5) & 0xFF, floor(y * 127 + 0.5) & 0xFF
  return (pad(p) & 0xFFFF) | sx << 16 | sy << 24
end

-- buttons (as pad()), stick x and y (-1..1) of an input of net.pad
function net.unpad(v)
  if not v then return 0, 0, 0 end
  local sx, sy = v >> 16 & 0xFF, v >> 24 & 0xFF
  if sx > 127 then sx = sx - 256 end
  if sy > 127 then sy = sy - 256 end
  return v & 0xFFFF, sx / 127, sy / 127
end

local BITS = { left = 1, right = 2, up = 4, down = 8, a = 16, b = 32, start = 64, select = 128, x = 256, y = 512,
               l1 = 1024, r1 = 2048, l2 = 4096, r2 = 8192, l3 = 16384, r3 = 32768 }

-- a button ("a", "left"... as pad()) held in an input of net.pad
function net.held(v, button)
  local b = BITS[button] or error("bmnet: no button called " .. tostring(button), 2)
  return v and v & b ~= 0 or false
end

----------------------------------------------------------------- messages

-- a message (a string up to 900 bytes) to everybody, or to the console to
-- (an id): it may be lost, or come after a newer one (positions, ...)
function net.send(data, to)
  assert(#data <= MAX_DATA, "bmnet: a message is at most 900 bytes")
  send("M", to or 0, data)
end

-- a sure message: it is repeated until it arrives, and each console gets
-- the messages of another in the order they were posted (chat, a turn,
-- a choice). To everybody in the room, or to the console to.
function net.post(data, to)
  assert(#data <= MAX_DATA, "bmnet: a message is at most 900 bytes")
  local list = {}
  if to then list[1] = to
  else
    for id in pairs(peers) do
      if id ~= net.me then list[#list + 1] = id end
    end
  end
  for _, id in ipairs(list) do
    local seq = out_seq[id] or 0
    out_seq[id] = (seq + 1) & 0xFFFF
    local m = { to = id, seq = seq, data = data, t = -1 }
    pending[#pending + 1] = m
  end
end

local function sure_in(from, seq, data)
  send("K", from, string.pack("<I2", seq))
  local want = in_seq[from] or 0
  if (seq - want) & 0xFFFF >= 0x8000 then return end        -- old: already given
  local e = early[from] or {}
  early[from] = e
  e[seq] = data
  while e[want] do
    ev({ type = "msg", from = from, data = e[want], sure = true })
    e[want] = nil
    want = (want + 1) & 0xFFFF
  end
  in_seq[from] = want
end

----------------------------------------------------------------- every frame

local function handle(typ, from, d, ip, port)
  local pe = peers[from]
  if pe then pe.t, pe.ip, pe.port = clock, ip, port end
  if from == target then host_heard = clock end
  if typ == "H" then
    local n, mx, hname, info = string.unpack("<BBs1s1", d)
    hosts[from] = { id = from, name = hname, players = n, max = mx, info = info, t = clock, ip = ip, port = port }
  elseif typ == "J" and net.state == "hosting" then
    if not pe then
      local used = {}
      local count = 0
      for _, p in pairs(peers) do used[p.seat] = true; count = count + 1 end
      if count < host_max then
        local seat = 1
        while used[seat] do seat = seat + 1 end
        pe = { id = from, name = string.unpack("<s1", d), seat = seat, t = clock, ip = ip, port = port }
        peers[from] = pe
        ev({ type = "join", id = from, name = pe.name, seat = seat })
      end
    end
    send("A", from, string.pack("<I2B", from, pe and pe.seat or 0))
  elseif typ == "J" and net.state == "playing" and net.is_host then
    send("A", from, string.pack("<I2B", from, 0))           -- too late
  elseif typ == "A" and from == target and (net.state == "joining" or net.state == "joined") then
    local id, seat = string.unpack("<I2B", d)
    if id == net.me then
      if seat == 0 then
        net.state, net.error = "lobby", "the match is full or has begun"
        ev({ type = "refused", id = from })
      elseif net.state == "joining" then
        net.state, net.seat = "joined", seat
        local h = hosts[from]                       -- the room: the host is seat 1 (L tells the rest)
        peers[from] = peers[from] or { id = from, name = h and h.name or "?", seat = 1, t = clock, ip = ip,
                                       port = port }
        peers[net.me] = { id = net.me, name = name, seat = seat, t = clock }
        ev({ type = "joined", id = from, seat = seat })
      end
    end
  elseif typ == "L" and from == target and net.state == "joined" then
    local n, pos = string.unpack("<B", d)
    local now = {}
    for _ = 1, n do
      local id, seat, pname
      id, seat, pname, pos = string.unpack("<I2Bs1", d, pos)
      now[id] = true
      if not peers[id] then
        peers[id] = { id = id, name = pname, seat = seat, t = clock }
        if id ~= net.me then ev({ type = "join", id = id, name = pname, seat = seat }) end
      end
      if id == from then peers[id].ip, peers[id].port = ip, port end
    end
    for id, p in pairs(peers) do
      if not now[id] then
        peers[id] = nil
        ev({ type = "leave", id = id, seat = p.seat })
      end
    end
  elseif typ == "S" and from == target and (net.state == "joined" or net.state == "joining") then
    local seed, delay, n = string.unpack("<I4BB", d)
    local seats, seat, pos = {}, nil, 7
    for _ = 1, n do
      local id, s
      id, s, pos = string.unpack("<I2B", d, pos)
      seats[#seats + 1] = s
      if id == net.me then seat = s end
    end
    local data = string.unpack("<s2", d, pos)
    if seat then
      net.seat = seat
      begin(seed, delay, seats, data)
    end
  elseif typ == "M" and pe then                             -- (only from the room)
    ev({ type = "msg", from = from, data = d, sure = false })
  elseif typ == "R" and pe then
    local seq = string.unpack("<I2", d)
    sure_in(from, seq, d:sub(3))
  elseif typ == "K" then
    local seq = string.unpack("<I2", d)
    for i = #pending, 1, -1 do
      if pending[i].to == from and pending[i].seq == seq then table.remove(pending, i) end
    end
  elseif typ == "Q" then
    local qmid, seat, was_host = string.unpack("<I2BB", d)
    if net.state == "playing" and qmid == mid then
      if net.is_host then
        if heard[seat] and seat ~= net.seat and not gone[seat] then
          gone[seat] = true
          ev({ type = "leave", id = from, seat = seat })
        end
      elseif was_host == 1 then
        net.state = "lost"
        ev({ type = "lost", why = "the host left" })
      end
    elseif net.state == "hosting" and pe then
      peers[from] = nil
      ev({ type = "leave", id = from, seat = pe.seat })
    elseif (net.state == "joined" or net.state == "joining") and from == target then
      net.state, net.error = "lobby", "the host left"
      ev({ type = "lost", why = "the host left" })
    end
  elseif net.state == "playing" and (typ == "I" or typ == "B" or typ == "X") and #d >= 2 and
         string.unpack("<I2", d) == mid then
    if typ == "I" and net.is_host then
      local seat, last = string.unpack("<BI4", d, 3)
      if heard[seat] then
        heard[seat] = clock
        for k = 0, KEEP - 1 do
          local fr = last - KEEP + 1 + k
          if fr >= net.frame then
            got[fr] = got[fr] or {}
            if got[fr][seat] == nil then got[fr][seat] = string.unpack("<I4", d, 8 + k * 4) end
          end
        end
      end
    elseif typ == "B" and not net.is_host then
      last_bundle = clock
      local last, n = string.unpack("<I4B", d, 3)
      local seats = { d:byte(8, 7 + n) }
      local pos = 8 + n
      for k = 0, KEEP - 1 do
        local fr = last - KEEP + 1 + k
        local mask = d:byte(pos)
        local b = {}
        for i = 1, n do
          local v = string.unpack("<I4", d, pos + 1 + (i - 1) * 4)
          if mask >> (i - 1) & 1 == 1 then
            b[seats[i]] = false
            if not gone[seats[i]] and fr >= net.frame then
              gone[seats[i]] = true
              ev({ type = "leave", seat = seats[i] })
            end
          else
            b[seats[i]] = v
          end
        end
        pos = pos + 1 + n * 4
        if fr >= net.frame and fr >= 0 and not frames[fr] then frames[fr] = b end
      end
    elseif typ == "X" then
      local fr, h = string.unpack("<I4I4", d, 3)
      local own = hashes[fr]
      if own and own ~= h and not desync_said then
        desync_said = true
        ev({ type = "desync", frame = fr, id = from })
      end
    end
  end
end

-- once in each _update while the network is open: the packets that came,
-- the lobby's announcements, the sure messages again; returns the events
-- of this frame, a list of {type = ...}: "join" (id, name, seat),
-- "leave" (seat, id), "joined" (seat: we are in), "refused", "start" (seed,
-- seat, seats, host, data), "msg" (from, data, sure), "lost" (why: the
-- host has gone), "desync" (frame), "error" (text)
function net.update()
  events = {}
  if not sock then return events end
  clock = clock + DT
  if net.stall then net.stall = net.stall + DT end
  if relay and not relay_ip then
    local ip = net_resolve(relay)
    if ip == false then
      net.error = "no such relay: " .. relay
      ev({ type = "error", text = net.error })
      relay = nil
      net.close()
      return events
    elseif ip then
      relay_ip = ip
    end
  end
  for _ = 1, 64 do
    local typ, from, d, ip, port = recv()
    if not typ then break end
    local ok, err = pcall(handle, typ, from, d, ip, port)
    if not ok then log("bmnet: a broken packet " .. typ .. ": " .. tostring(err)) end
  end
  local tick = floor(clock * 2) ~= floor((clock - DT) * 2)       -- twice a second
  for id, h in pairs(hosts) do
    if clock - h.t > 3 then hosts[id] = nil end
  end
  if net.state == "hosting" then
    if tick or last_ann < 0 then
      local n = 0
      for _ in pairs(peers) do n = n + 1 end
      send("H", 0, string.pack("<BBs1s1", n, host_max, name, host_info))
      send("L", 0, room_list())
      last_ann = clock
    end
    for id, p in pairs(peers) do
      if id ~= net.me and clock - p.t > 5 then
        peers[id] = nil
        ev({ type = "leave", id = id, seat = p.seat })
      end
    end
  elseif net.state == "joining" or net.state == "joined" then
    if floor(clock * 4) ~= floor((clock - DT) * 4) or last_ann < 0 then
      send("J", target, string.pack("<s1", name))
      last_ann = clock
    end
    if clock - host_heard > 5 then
      net.state, net.error = "lobby", "the host is gone"
      ev({ type = "lost", why = net.error })
    end
  elseif net.state == "playing" then
    if net.is_host then
      if clock - started_t < 2 and tick then send("S", 0, start_msg) end
    elseif clock - last_bundle > 5 then
      net.state = "lost"
      ev({ type = "lost", why = "nothing from the host for 5 s" })
    end
  end
  if relay_ip and tick and net.state ~= "playing" then raw("P", 0, "") end
  -- the sure messages not yet received: again every 0.2 s
  for _, m in ipairs(pending) do
    if m.t < 0 or clock - m.t >= 0.2 then
      send("R", m.to, string.pack("<I2", m.seq) .. m.data)
      m.t = clock
    end
  end
  return events
end

return net
