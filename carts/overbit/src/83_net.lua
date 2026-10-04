-- Net: matches on the network (M38.5). Lockstep: every console runs the
-- whole match (the same code and seed: the bots too), only the players'
-- inputs travel. Each console sends its inputs for the frame DELAY frames
-- ahead; the host gathers everyone's and sends, for every frame, the bundle
-- of all the inputs; a frame runs on a console when its bundle is there.
-- The view turns at once, the hero follows DELAY frames later. On the LAN
-- the consoles talk with broadcasts on UDP port 47310; over the internet
-- they talk to a relay (tools/overbit_relay.py) that passes everything to
-- the others in a room, as a broadcast would.
--
-- Packets: "OB1", the build (4 chars), the type, the sender's id, then:
--   H host here: players, name         J join: name
--   A accepted: id, seat                S start: seed, delay, bots, seats
--   P here (to the relay, in the lobby: it learns who is in the room)
-- and in a match, after the match's number (16 bits of its seed):
--   I inputs: seat, last frame, KEEP records (the newest last)
--   B bundle: last frame, seats, KEEP records for each seat
--   X check: frame, hash of the match  Q leaving: seat, 1 for the host

Net = { on = false }

local PORT = 47310
local KEEP = 8                  -- records repeated in every packet (losses)
local REC = "<I2i1i1I2i2B"      -- buttons, mx, mz, yaw, pitch, hero asked
local REC_SIZE = string.packsize(REC)
local BTN = { "fire", "fire2", "ab1", "ab2", "ult", "jump", "crouch", "reload", "melee" }
local LEFT = 0xFFFF             -- buttons of a seat whose player went away

local sock
local my_id = 0
local build4 = "dev "

-- ---------------------------------------------------------------- packets

local function send(typ, payload)
  if not sock then return end
  local p = "OB1" .. build4 .. typ .. string.char(my_id) .. (payload or "")
  if Net.relay then
    udp_send(sock, Net.relay_ip, Net.relay_port, "OBR1" .. Net.room .. p)
  else
    udp_send(sock, "*", PORT, p)
  end
end
Net.send = send

-- the packets that came (of this build, not our own): type, sender, data
local function packets()
  return function()
    while sock do
      local d, ip = udp_recv(sock)
      if not d then return nil end
      if #d >= 9 and d:sub(1, 3) == "OB1" and d:sub(4, 7) == build4 and d:byte(9) ~= my_id then
        return d:sub(8, 8), d:byte(9), d:sub(10), ip
      end
    end
  end
end
Net.packets = packets

function Net.open()
  if sock then return true end
  local s, err = udp_open(PORT)
  if not s then return false, err end
  sock = s
  build4 = ((OVERBIT_BUILD or "dev") .. "    "):sub(1, 4)
  Net.set_id(false)
  return true
end

-- the console's number in the packets: on a LAN the last byte of its
-- address (different for every console); through a relay, from the moment
-- the lobby opened (consoles all start the same way: math.random's seed
-- and the addresses of the Lua state are the same on every Pi)
function Net.set_id(relay)
  local last = tonumber((net_ip() or "0.0.0.1"):match("(%d+)$")) or 1
  if relay then my_id = (last * 37 + floor(time() * 1000)) % 254 + 1
  else my_id = last % 254 + (last % 254 == 0 and 1 or 0) end
end

function Net.close()
  if sock then
    if Net.on then send("Q", string.pack("<I2BB", Net.mid, Net.seat or 0, Net.host and 1 or 0)) end
    udp_close(sock)
  end
  sock, Net.on = nil, false
  if online then online(false) end
end

-- PS (Ctrl+Esc, Start+Select) in a match on the network: the system asks
-- the player leaving (online()); yes calls this, then the game ends
function _leave()
  if Net.on then
    log("overbit net: left with PS")
    Net.close()
  end
end

function Net.my_id() return my_id end

-- ---------------------------------------------------------------- inputs

local function encode(c, yaw, pitch, hero)
  local b = 0
  for i, k in ipairs(BTN) do if c[k] then b = b | (1 << (i - 1)) end end
  return string.pack(REC, b, clamp(floor(c.mx * 127 + 0.5), -127, 127), clamp(floor(c.mz * 127 + 0.5), -127, 127),
                     floor((yaw % (2 * pi)) / (2 * pi) * 65535 + 0.5) % 65536,
                     clamp(floor(pitch * 10000 + 0.5), -32767, 32767), hero or 0)
end
local NEUTRAL = string.pack(REC, 0, 0, 0, 0, 0, 0)
local GONE = string.pack(REC, LEFT, 0, 0, 0, 0, 0)

-- a record into a command (the presses: against the previous record);
-- yaw, pitch, the hero asked (0: none), and whether the player left
local function decode(r, prev, c)
  local b, mx, mz, yaw, pitch, hero = string.unpack(REC, r)
  if b == LEFT then return nil, nil, 0, true end
  local pb = prev and string.unpack("<I2", prev) or 0
  if pb == LEFT then pb = 0 end
  for i, k in ipairs(BTN) do
    local bit = 1 << (i - 1)
    c[k] = b & bit ~= 0
    c[k .. "_p"] = c[k] and pb & bit == 0
  end
  c.mx, c.mz = mx / 127, mz / 127
  c.look_x, c.look_y = 0, 0
  return yaw / 65535 * 2 * pi, pitch / 10000, hero, false
end

-- ---------------------------------------------------------------- the match

local frames = {}               -- frame -> { [seat] = record } (the bundles)
local mine = {}                 -- frame -> my record
local got = {}                  -- host: frame -> { [seat] = record }
local prevs = {}                -- seat -> the record of the last frame run

-- the start of a match on the network, the same on every console
function Net.begin(info)
  Net.on = true
  Net.delay = info.delay
  Net.seat = info.seat
  Net.host = info.host
  Net.seats = info.seats        -- the seats played by people, sorted
  Net.mid = info.seed % 65536   -- this match's number (the packets of others are not ours)
  Net.frame = 0                 -- the next frame to run
  Net.clock = 0                 -- seconds on this console (not the match's)
  Net.heard = {}                -- seat -> Net.clock when last heard (host)
  Net.left = {}                 -- seat -> true once its player went away
  Net.last_bundle = 0
  Net.view_yaw, Net.view_pitch = nil, nil
  Net.hero_req = nil
  Net.desync, Net.lost = nil, false
  Net.hashes = {}
  for _, t in ipairs({ frames, mine, got, prevs }) do for k in pairs(t) do t[k] = nil end end
  -- the first DELAY frames: everyone still
  for f = 0, Net.delay - 1 do
    local b = {}
    for _, s in ipairs(Net.seats) do b[s] = NEUTRAL end
    frames[f] = b
    mine[f] = NEUTRAL
  end
  for _, s in ipairs(Net.seats) do Net.heard[s] = 0 end
  -- PS asks before leaving (not suspended: the others play on)
  if online then online(true, Net.host and "You are the host: the match ends for all." or nil) end
end

-- the host: the bundles of the frames everybody has sent (a seat silent for
-- 3 s is gone: a bot takes it, the same on every console)
local function host_gather()
  for _, s in ipairs(Net.seats) do
    if s ~= Net.seat and not Net.left[s] and Net.clock - Net.heard[s] > 3 then
      Net.left[s] = true
      log(string.format("overbit net seat %d gone", s))
    end
  end
  for fr = Net.frame, Net.frame + Net.delay do
    if not frames[fr] then
      local g = got[fr]
      if not g then break end
      local ok = true
      for _, s in ipairs(Net.seats) do if not g[s] and not Net.left[s] then ok = false break end end
      if not ok then break end
      local b = {}
      for _, s in ipairs(Net.seats) do b[s] = Net.left[s] and GONE or g[s] end
      frames[fr] = b
    end
  end
  -- the last KEEP bundles to everyone
  local last = Net.frame + Net.delay
  while last >= Net.frame and not frames[last] do last = last - 1 end
  if last >= 0 and frames[last] then
    local parts = { string.pack("<I4B", last, #Net.seats) }
    for _, s in ipairs(Net.seats) do parts[#parts + 1] = string.char(s) end
    for k = 0, KEEP - 1 do
      local b = frames[last - KEEP + 1 + k]
      for _, s in ipairs(Net.seats) do parts[#parts + 1] = b and b[s] or NEUTRAL end
    end
    send("B", string.pack("<I2", Net.mid) .. table.concat(parts))
  end
end

-- every console frame: my inputs out (for frame + DELAY), what came in;
-- true when the next frame of the match can run
function Net.tick(me, c)
  Net.clock = Net.clock + DT
  if not Net.view_yaw then Net.view_yaw, Net.view_pitch = me.yaw, me.pitch end
  -- the view turns now; the hero follows when its frame runs
  Net.view_yaw = wrap_angle(Net.view_yaw + c.look_x)
  Net.view_pitch = clamp(Net.view_pitch + c.look_y, -1.45, 1.45)
  local f = Net.frame + Net.delay
  if not mine[f] then
    mine[f] = encode(c, Net.view_yaw, Net.view_pitch, Net.hero_req)
    Net.hero_req = nil
  end
  if Net.host then
    got[f] = got[f] or {}
    got[f][Net.seat] = mine[f]
  else
    local recs = {}
    for k = f - KEEP + 1, f do recs[#recs + 1] = mine[k] or NEUTRAL end
    send("I", string.pack("<I2BI4", Net.mid, Net.seat, f) .. table.concat(recs))
  end
  for typ, _, d in packets() do
    -- only this match's packets, without their number
    if typ == "I" or typ == "B" or typ == "X" or typ == "Q" then
      if #d < 2 or string.unpack("<I2", d) ~= Net.mid then typ = "" else d = d:sub(3) end
    end
    if typ == "I" and Net.host then
      local seat, last = string.unpack("<BI4", d)
      if Net.heard[seat] then
        Net.heard[seat] = Net.clock
        for k = 0, KEEP - 1 do
          local fr = last - KEEP + 1 + k
          if fr >= Net.frame then
            got[fr] = got[fr] or {}
            got[fr][seat] = got[fr][seat] or d:sub(6 + k * REC_SIZE, 5 + (k + 1) * REC_SIZE)
          end
        end
      end
    elseif typ == "B" and not Net.host then
      Net.last_bundle = Net.clock
      local last, n = string.unpack("<I4B", d)
      local seats = { d:byte(6, 5 + n) }
      local pos = 6 + n
      for k = 0, KEEP - 1 do
        local fr = last - KEEP + 1 + k
        local b = {}
        for i = 1, n do
          b[seats[i]] = d:sub(pos, pos + REC_SIZE - 1)
          pos = pos + REC_SIZE
        end
        if fr >= Net.frame and not frames[fr] then frames[fr] = b end
      end
    elseif typ == "X" then
      local fr, h = string.unpack("<I4I4", d)
      local own = Net.hashes[fr]
      if own and own ~= h and not Net.desync then
        Net.desync = fr
        log(string.format("overbit net desync at frame %d", fr))
      end
    elseif typ == "Q" and Net.host then
      -- a player left (PS, the menu): a bot takes the seat from now on
      local seat = #d >= 1 and d:byte(1) or 0
      if Net.heard[seat] and seat ~= Net.seat and not Net.left[seat] then
        Net.left[seat] = true
        log(string.format("overbit net seat %d left", seat))
      end
    elseif typ == "Q" and Net.clock > 1 then
      -- the host closed the match (another player's Q: the host's bundles go on)
      if #d >= 2 then Net.lost = Net.lost or d:byte(2) == 1
      else Net.lost = Net.lost or (frames[Net.frame] == nil) end
    end
  end
  if Net.host then host_gather()
  elseif Net.clock - Net.last_bundle > 5 then Net.lost = true end
  return frames[Net.frame] ~= nil
end

-- is the frame after next ready too (a console behind catches up)?
function Net.behind()
  return frames[Net.frame + 1] ~= nil and frames[Net.frame + 2] ~= nil
end

-- the frame runs: each seat's command from the bundle into its actor
-- (a.net_cmd, yaw, pitch); returns the heroes asked { {seat, id} } and the
-- seats that went away
function Net.apply(by_seat)
  local b = frames[Net.frame]
  local asks, gone = {}, {}
  for _, s in ipairs(Net.seats) do
    local a = by_seat[s]
    if a then
      a.net_cmd = Input.blank(a.net_cmd)
      local yaw, pitch, hero, left = decode(b[s], prevs[s], a.net_cmd)
      prevs[s] = b[s]
      if left then
        if a.human then gone[#gone + 1] = s end
      else
        if a.alive and not Actors.frozen(a) then a.yaw, a.pitch = yaw, pitch end
        if hero > 0 and HERO_ORDER[hero] then asks[#asks + 1] = { s, HERO_ORDER[hero] } end
      end
    end
  end
  frames[Net.frame - KEEP * 4] = nil
  mine[Net.frame - KEEP * 4] = nil
  got[Net.frame - KEEP * 4] = nil
  Net.frame = Net.frame + 1
  return asks, gone
end

-- every second: a hash of where everyone is; a console whose hash differs
-- for the same frame has gone its own way (shown, logged)
function Net.check(fr, actors)
  if fr % (OVERBIT_NET_DEBUG and 10 or 60) ~= 0 then return end
  local h = 0
  for _, a in ipairs(actors) do
    h = (h * 31 + floor(a.x * 64) * 7 + floor(a.z * 64) * 13 + floor(Actors.total(a))) & 0xFFFFFFF
  end
  Net.hashes[fr] = h
  Net.hashes[fr - 600] = nil
  if OVERBIT_NET_DEBUG then
    for _, a in ipairs(actors) do
      log(string.format("netdbg %d %d %s %.4f %.4f %.4f %.3f %.1f %s", fr, a.seat or 0, a.hero.id, a.x, a.y, a.z, a.yaw,
                        Actors.total(a), a.alive and "a" or "d"))
    end
  end
  send("X", string.pack("<I2I4I4", Net.mid, fr, h))
end

-- ---------------------------------------------------------------- the lobby

-- The mode "lobby" (menu: PLAY ONLINE): host a match, or join one of those
-- found on the LAN (or in the relay's room); the host starts it, the empty
-- seats go to bots. Over the internet: the relay's address and a room name,
-- typed here and saved.
local Lobby = { sel = 1 }
Modes.list.lobby = Lobby

-- the seats in the order players come: blue 1, red 6, blue 2, red 7, ...
local function seat_of(k) return k % 2 == 1 and (k + 1) // 2 or 5 + k // 2 end

local ROWS = { "HOST A MATCH", "RELAY", "ROOM" }

function Lobby.start()
  Net.close()
  math.randomseed(floor(time() * 1000))       -- the host's seed of the match
  Lobby.hosts, Lobby.players, Lobby.state, Lobby.t = {}, {}, "choose", 0
  Lobby.sel, Lobby.edit = 1, nil
  local cfg = saved() or {}
  Lobby.relay_name = cfg.relay or ""
  Lobby.room = cfg.room or "PLAY"
  Lobby.use_relay = cfg.use_relay and Lobby.relay_name ~= "" or false
  if OVERBIT_RELAY then Lobby.relay_name, Lobby.use_relay = OVERBIT_RELAY, true end    -- tests
  Lobby.err = nil
  if not net_ip() then Lobby.err = "no network" return end
  local ok, err = Net.open()
  if not ok then Lobby.err = err end
  Lobby.apply_relay()
end

-- the relay in use ("name" or "name:port"; looked up while the lobby runs)
function Lobby.apply_relay()
  local name = Lobby.use_relay and Lobby.relay_name ~= "" and Lobby.relay_name or nil
  Net.relay_port = PORT
  if name and name:find(":") then
    local h, p = name:match("^(.-):(%d+)$")
    if h then name, Net.relay_port = h, tonumber(p) end
  end
  Net.relay = name
  Net.relay_ip = nil
  if sock then Net.set_id(name ~= nil) end
  Net.room = (Lobby.room .. "____"):sub(1, 4)
end

local function host_start()
  local seed = math.random(1, 2 ^ 30)
  local delay = Net.relay and 7 or 4
  local parts = { string.pack("<I4BBB", seed, delay, G.bot_diff, #Lobby.players) }
  local seats = {}
  for _, p in ipairs(Lobby.players) do
    parts[#parts + 1] = string.pack("<BB", p.id, p.seat)
    seats[#seats + 1] = p.seat
  end
  local msg = table.concat(parts)
  for _ = 1, 4 do send("S", msg) end
  Lobby.go(seed, delay, G.bot_diff, seats, 1, true)
end

function Lobby.go(seed, delay, diff, seats, seat, host)
  table.sort(seats)
  Net.begin({ delay = delay, seat = seat, host = host, seats = seats, seed = seed })
  Modes.list.match.net = { seed = seed, seats = seats, seat = seat, diff = diff }
  log(string.format("overbit net start: seed %d, seat %d of %d, %s", seed, seat, #seats, host and "host" or "guest"))
  Modes.start("match")
end

-- typing an address: letters, digits, dots and dashes from the keyboard;
-- on the pad, up and down change the last character, right adds one
local KEYCH = {}
for i = 0, 25 do KEYCH[0x04 + i] = string.char(97 + i) end
for i = 0, 8 do KEYCH[0x1E + i] = string.char(49 + i) end
KEYCH[0x27], KEYCH[0x37], KEYCH[0x2D], KEYCH[0x33] = "0", ".", "-", ":"
local CHARS = "abcdefghijklmnopqrstuvwxyz0123456789.-:"
local held = {}
local enter_prev = false

local function edit_text(s, c)
  local now = {}
  for _, u in ipairs(keys()) do now[u] = true end
  for u in pairs(now) do
    if not held[u] then
      if KEYCH[u] and #s < 40 then s = s .. KEYCH[u] end
      if u == 0x2A then s = s:sub(1, -2) end                -- backspace
    end
  end
  held = now
  if c.pad then
    local last = s:sub(-1)
    local i = CHARS:find(last, 1, true) or 0
    if c.up_p then s = s:sub(1, -2) .. CHARS:sub(i % #CHARS + 1, i % #CHARS + 1) end
    if c.down_p then local j = (i - 2) % #CHARS + 1 s = s:sub(1, -2) .. CHARS:sub(j, j) end
    if c.right_p then s = s .. "a" end
    if c.left_p then s = s:sub(1, -2) end
  end
  return s
end

local function save_cfg()
  local t = saved() or {}       -- (with the resolution, 80_modes)
  t.relay, t.room, t.use_relay = Lobby.relay_name, Lobby.room, Lobby.use_relay
  save(t)
end

function Lobby.update()
  Lobby.t = Lobby.t + DT
  local c = Input.cmd
  local enter = keydown(0x28) and not enter_prev        -- Enter: confirms here (not the menu)
  enter_prev = keydown(0x28)
  if Lobby.edit then
    -- typing the relay or the room; Enter (or A on the pad) ends
    local k = Lobby.edit
    if k == "relay" then Lobby.relay_name = edit_text(Lobby.relay_name, c)
    else Lobby.room = edit_text(Lobby.room:lower(), c):upper():sub(1, 4) end
    if enter or (c.pad and c.jump_p) then
      Lobby.edit = nil
      if k == "relay" then Lobby.use_relay = Lobby.relay_name ~= "" end
      Lobby.apply_relay()
      save_cfg()
    end
    return
  end
  -- back: B on the pad (or Start), C on the keyboard
  if c.crouch_p or (c.pad and c.menu_p) then Net.close() Modes.start("menu") return end
  if Lobby.err then return end
  -- the relay's address, looked up
  if Net.relay and not Net.relay_ip then
    local ip = net_resolve(Net.relay)
    if ip == false then Lobby.relay_err = "no such relay" Net.relay = nil
    elseif ip then Net.relay_ip, Lobby.relay_err = ip, nil end
  end
  for typ, from, d, ip in packets() do
    if typ == "H" then
      local n, name = string.unpack("<Bs1", d)
      Lobby.hosts[from] = { name = name, n = n, t = Lobby.t }
    elseif typ == "J" and Lobby.state == "hosting" then
      local known
      for _, p in ipairs(Lobby.players) do if p.id == from then known = p end end
      if not known and #Lobby.players < 10 then
        known = { id = from, name = string.unpack("<s1", d), seat = seat_of(#Lobby.players + 1) }
        Lobby.players[#Lobby.players + 1] = known
        Snd.play("ui")
        log(string.format("overbit net join %d seat %d", from, known.seat))
      end
      if known then send("A", string.pack("<BB", from, known.seat)) end
    elseif typ == "A" and Lobby.state == "joining" and from == Lobby.target then
      local id, seat = string.unpack("<BB", d)
      if id == my_id then Lobby.state, Lobby.seat = "waiting", seat end
    elseif typ == "S" and from == Lobby.target and (Lobby.state == "waiting" or Lobby.state == "joining") then
      local seed, delay, diff, n = string.unpack("<I4BBB", d)
      local seats, mine_seat = {}, nil
      for k = 1, n do
        local id, seat = string.unpack("<BB", d, 8 + (k - 1) * 2)
        seats[#seats + 1] = seat
        if id == my_id then mine_seat = seat end
      end
      if mine_seat then Lobby.go(seed, delay, diff, seats, mine_seat, false) return end
    end
  end
  for id, h in pairs(Lobby.hosts) do if Lobby.t - h.t > 3 then Lobby.hosts[id] = nil end end
  -- with a relay: say we are here, so that it passes us the room's packets
  if Net.relay_ip and floor(Lobby.t * 2) ~= floor((Lobby.t - DT) * 2) then send("P") end
  if Lobby.state == "hosting" then
    if floor(Lobby.t * 2) ~= floor((Lobby.t - DT) * 2) then
      send("H", string.pack("<Bs1", #Lobby.players, (Net.relay and "room " .. Net.room or net_ip() or "?")))
    end
    if c.jump_p or c.fire_p or enter then host_start() end
  elseif Lobby.state == "joining" or Lobby.state == "waiting" then
    if Lobby.state == "joining" and floor(Lobby.t * 4) ~= floor((Lobby.t - DT) * 4) then
      send("J", string.pack("<s1", "Player"))
    end
  else
    local list = {}
    for _, r in ipairs(ROWS) do list[#list + 1] = r end
    local ids = {}
    for id in pairs(Lobby.hosts) do ids[#ids + 1] = id end
    table.sort(ids)
    for _, id in ipairs(ids) do list[#list + 1] = id end
    Lobby.list = list
    if c.up_p then Lobby.sel = (Lobby.sel - 2) % #list + 1 Snd.play("ui") end
    if c.down_p then Lobby.sel = Lobby.sel % #list + 1 Snd.play("ui") end
    Lobby.sel = min(Lobby.sel, #list)
    if c.jump_p or c.fire_p or enter then
      Snd.play("ui")
      local it = list[Lobby.sel]
      if it == "HOST A MATCH" then
        Lobby.state = "hosting"
        Lobby.players = { { id = my_id, name = "Host", seat = 1 } }
      elseif it == "RELAY" then
        Lobby.edit = "relay"
      elseif it == "ROOM" then
        Lobby.edit = "room"
      else
        Lobby.state, Lobby.t, Lobby.target = "joining", 0, it
      end
    end
  end
end

function Lobby.draw()
  cls(0x0A0E14)
  font("6x12")
  uprint("PLAY ONLINE", 8, 6, 0xFFE070)
  if Lobby.err then
    uprint("No network: " .. Lobby.err, 8, 30, 0xFF6060)
    uprint("Connect the console to WiFi (or a cable) first.", 8, 46, 0xD8DCE2)
    font()
    return
  end
  uprint("this console " .. (net_ip() or "-") .. (Net.relay and ("   relay " .. Net.relay .. " room " .. Net.room) or
        "   on the LAN"), 8, 20, 0x7A8290)
  if Lobby.relay_err then uprint(Lobby.relay_err, LW - 8 - #Lobby.relay_err * 6, 6, 0xFF6060) end
  if Lobby.state == "choose" then
    for i, it in ipairs(Lobby.list or ROWS) do
      local s = it
      if it == "RELAY" then s = "RELAY: " .. (Lobby.relay_name ~= "" and Lobby.relay_name or "(none: the LAN)")
      elseif it == "ROOM" then s = "ROOM: " .. Lobby.room
      elseif type(it) == "number" then
        local h = Lobby.hosts[it]
        s = h and ("JOIN " .. h.name .. "  (" .. h.n .. " in)") or "?"
      end
      if (it == "RELAY" or it == "ROOM") and Lobby.edit == it:lower() then s = s .. "_" end
      local y = 40 + (i - 1) * 14
      if i == Lobby.sel then urectfill(6, y - 1, #s * 6 + 8, 13, Lobby.edit and 0x46B4FF or 0xF26A21) end
      uprint(s, 10, y, i == Lobby.sel and 0xFFFFFF or 0xD8DCE2)
    end
    uprint(Lobby.edit and "type it, then Enter" or "the matches found appear here", 8, LH - 14, 0x7A8290)
  elseif Lobby.state == "hosting" then
    uprint("HOSTING: on the other consoles, PLAY ONLINE and JOIN", 8, 38, 0xD8DCE2)
    for i, p in ipairs(Lobby.players) do
      local blue = p.seat <= 5
      uprint((i == 1 and "you" or "player " .. i) .. "  " .. (blue and "BLUE" or "RED"), 16, 44 + i * 12,
            blue and 0x46B4FF or 0xFF4646)
    end
    local x = uprompt(Input.cmd.pad and "A" or "space", 8, LH - 15, true)
    uprint("START (bots in the empty seats)", x + 3, LH - 15, 0x7A8290)
  else
    uprint(Lobby.state == "waiting" and "IN: waiting for the host to start" or "JOINING...", 8, 44, 0xD8DCE2)
  end
  font()
end
