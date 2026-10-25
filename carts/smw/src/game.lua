-- Super Mario World, first level, for the RGB30 (.b16, 360x360). A personal port: the graphics
-- come from the player's own ROM (scripts/smw_*.py). The movement is in physics.lua.
local lib = require "bmlib"
local VIEW_W, VIEW_H = 256, 224          -- the SNES picture, shown whole inside the 360x360 screen
local VX, VY = (360 - VIEW_W) // 2, (360 - VIEW_H) // 2
local LEVEL_H = 27 * 16
local START_X, START_Y = 16, 352

local level_w
local mario, prev, acc
local spawn_foes                      -- defined with the enemies, below
local coins, score, dragons = 0, 0, 0
local eggs = {}                           -- eggs from the 0x126 block: {cx, cy, t}; Yoshi hatches from them
local state = "play"                      -- play, dying, clear, over
local lives, time_left, time_acc = 5, 400, 0
local gtick = 0                           -- ticks since the start, for the decorations
local msg_i, msg_t = 1, 0                 -- the message being read (state "msg")
local psw_t = 0                           -- ticks left of the blue P switch: coins are blocks and blocks are coins
local swapped = {}                        -- cells the switch turned: {cx, cy, from, to}
local bits = {}                           -- the pieces of a broken block: {x, y, vx, vy, cell}
local st_t = 0                            -- ticks since the state began
local tape_t = 0
local changed = {}                        -- level cells the player changed: {cx, cy}, put back on a restart
local L0                                  -- the level as it was loaded
local bench = false                       -- Select shows the frame cost and logs it every 5 seconds
local b_sum, b_max, b_n, b_ms, b_peak = 0, 0, 0, 0, 0
local bumping = {}                        -- blocks jumping up: {cx, cy, t, used}
local pops = {}                           -- coins thrown up by a block: {x, y, t}
local BUMP_Y = { -2, -4, -5, -6, -5, -4, -2, 0 }
local camx, camy = 0, 192
local backdrop = 0xFF0000

local restart_level

function _init()
  local mw, mh = msize()
  level_w = mw * 8
  lib.tiles({ layer = "col", solid = 1, platform = 2 })     -- the map's flags: 1 solid, 2 platform (bmlib's convention)
  L0 = {}
  for y, row in ipairs(L16) do L0[y] = { table.unpack(row) } end
  restart_level()
end

local function set16(cx, cy, d)           -- a Map16 block is four 8x8 cells: TL, BL, TR, BR
  mset(cx * 2, cy * 2, d[1], "main")
  mset(cx * 2, cy * 2 + 1, d[2], "main")
  mset(cx * 2 + 1, cy * 2, d[3], "main")
  mset(cx * 2 + 1, cy * 2 + 1, d[4], "main")
end

local function set_col(cx, cy, id)                -- the collision layer of a Map16 block (solid, platform or nothing)
  local c = COLCELL[id] or 0
  mset(cx * 2, cy * 2, c, "col")
  mset(cx * 2, cy * 2 + 1, c, "col")
  mset(cx * 2 + 1, cy * 2, c, "col")
  mset(cx * 2 + 1, cy * 2 + 1, c, "col")
end

local function put16(cx, cy, id)                   -- a block of the level changes to another one: the number, the picture, the walls
  L16[cy + 1][cx + 1] = id
  set16(cx, cy, M16[id] or { 0, 0, 0, 0 })
  set_col(cx, cy, id)
end

-- The blue P switch turns the turn blocks (0x11E) into coins and the coins (0x02B) into turn blocks, for 704 ticks.
local function swap_pswitch(on)
  if on then
    swapped = {}
    for cy = 0, #L16 - 1 do
      for cx = 0, #L16[1] - 1 do
        local id = L16[cy + 1][cx + 1]
        local to = (id == 0x11E and 0x02B) or (id == 0x02B and 0x11E) or nil
        if to then
          swapped[#swapped + 1] = { cx, cy, id, to }
          changed[#changed + 1] = { cx, cy }
          put16(cx, cy, to)
        end
      end
    end
  else
    for _, c in ipairs(swapped) do
      if L16[c[2] + 1][c[1] + 1] == c[4] then put16(c[1], c[2], c[3]) end    -- not taken or broken meanwhile
    end
    swapped = {}
  end
end

local function draw16(d, x, y)
  for i, o in ipairs({ { 0, 0 }, { 0, 8 }, { 8, 0 }, { 8, 8 } }) do
    if d[i] ~= 0 then sspr((d[i] % 16) * 8, (d[i] // 16) * 8, 8, 8, x + o[1], y + o[2]) end
  end
end

local function hit_block(cx, cy)
  local k = L16[cy + 1][cx + 1]
  local kind = KIND[k]
  if kind ~= "coin" and kind ~= "egg" and kind ~= "vine" then return end
  L16[cy + 1][cx + 1] = 0x132
  changed[#changed + 1] = { cx, cy }
  set16(cx, cy, { 0, 0, 0, 0 })
  bumping[#bumping + 1] = { cx = cx, cy = cy, t = 0 }
  if kind == "coin" then
    coins = coins + 1
    pops[#pops + 1] = { x = cx * 16, y = cy * 16 - 16, t = 0 }
  elseif kind == "egg" then
    eggs[#eggs + 1] = { cx = cx, cy = cy, t = 0 }
  end
end

local function pick_coins()
  local x0, x1 = (math.floor(mario.x) + 2) // 16, (math.floor(mario.x) + 13) // 16
  local y0, y1 = (math.floor(mario.y) + 16) // 16, (math.floor(mario.y) + 31) // 16
  for cy = y0, y1 do
    for cx = x0, x1 do
      local row = L16[cy + 1]
      local id = row and row[cx + 1] or 0
      if id == DRAGON_TOP or id == DRAGON_BOT then
        local top = id == DRAGON_TOP and cy or cy - 1             -- take both halves
        for _, yy in ipairs({ top, top + 1 }) do
          L16[yy + 1][cx + 1] = 0x25
          changed[#changed + 1] = { cx, yy }
          set16(cx, yy, { 0, 0, 0, 0 })
        end
        dragons = dragons + 1
        score = score + 1000
        if dragons % 5 == 0 then lives = lives + 1 end             -- five dragon coins: a 1-up
      elseif row and COINS[id] then
        row[cx + 1] = 0x25
        changed[#changed + 1] = { cx, cy }
        set16(cx, cy, { 0, 0, 0, 0 })
        coins = coins + 1
        score = score + 200
      end
    end
  end
end

-- ---- Enemies: a list of tables with a kind; FOE[kind] says how each one steps, which zone it shows and what a stomp does.
-- ---- Koopa Troopas (sprite 05). Speeds in 1/16 pixel a tick, as the game keeps them: it walks at 8 and
-- turns at a wall and at the end of a ledge (they are red), a kicked shell slides fast, a stomped one sleeps
-- for 330 ticks. Zones koopa_l, koopa_r, koopa_turn, shell and shell_slide, with their boxes (scripts/smw_enemies.py).
local foes = {}
local FOE = {}
local H = lib.hits()                           -- the boxes of a tick, and who hits whom

spawn_foes = function()
  foes = {}
  for _, c in ipairs(CHUCKS) do
    foes[#foes + 1] = { kind = "chuck", x = c[1], y = c[2], w = 16, h = 16, vx = 0, vy = 0, face = -1, hp = 3, state = "idle", t = 0, turn = 0 }
  end
  for _, m in ipairs(PSWITCHES) do
    foes[#foes + 1] = { kind = "pswitch", x = m[1], y = m[2], w = 16, h = 16, vx = 0, vy = 0, face = 1, state = "idle", t = 0, turn = 0 }
  end
  for _, a in ipairs(PLANT_AT) do
    foes[#foes + 1] = { kind = "plant", x = a[1], y = PLANT_PATH[1], w = 24, h = 16, vx = 0, vy = 0, face = 1, state = "walk", t = 0, turn = 0 }
  end
  for _, k in ipairs(KOOPAS) do
    foes[#foes + 1] = { kind = "koopa", x = k[1], y = k[2], w = 16, h = 16, vx = k[4] == 1 and 0 or (k[3] == 1 and -8 or 8), vy = 0,
                            face = k[3] == 1 and -1 or 1, state = k[4] == 1 and "shell" or "walk", t = 0, turn = 0 }
  end
end

local function floor_at(px, py)
  return mflags(px, py, 1, 1, "col") & 3 ~= 0
end

local function koopa_step(k)
  k.t = k.t + 1
  k.vy = math.min(k.vy + 4, 64)
  if k.state == "dead" then
    k.y = k.y + k.vy / 16 * 1.2
    return
  end
  local _, hy = lib.move(k, 0, k.vy / 16 * 1.2)
  if hy ~= 0 then k.vy = 0 end
  if k.state == "shell" then
    if k.t > 330 then k.state, k.t, k.vx = "walk", 0, k.face * 8 end
    return
  end
  local hx = lib.move(k, k.vx / 16, 0)
  local at_edge = k.state == "walk" and hy == 1 and not floor_at(k.x + (k.vx > 0 and 17 or -2), k.y + 17)
  if hx ~= 0 or at_edge then
    k.vx = -k.vx
    if k.state == "walk" then k.turn = 7 end
    k.face = k.vx > 0 and 1 or -1
  end
  if k.turn > 0 then k.turn = k.turn - 1 end
end

local function koopa_zone(k)               -- zone, frame, flip_x, flip_y
  if k.state == "dead" then return "shell", 1, false, true end
  if k.state == "shell" then return "shell", 1, false, false end
  if k.state == "slide" then return "shell_slide", 1, false, false end
  if k.turn > 0 then return "koopa_turn", 1, false, false end
  return (k.face < 0 and "koopa_l" or "koopa_r"), (k.t // 8) % 2 + 1, false, false
end

local function koopa_stomp(k)
  if k.state == "walk" or k.state == "slide" then k.state, k.t, k.vx = "shell", 0, 0
  elseif k.state == "shell" then k.state, k.vx = "slide", (mario.x + 8 < k.x + 8) and 60 or -60 end
end

FOE.koopa = { step = koopa_step, zone = koopa_zone, stomp = koopa_stomp,
              hurts = function(k, mx) return k.state == "walk" or (k.state == "slide" and k.t >= 8 and (k.vx > 0) == (mx + 8 > k.x + 8)) end }

-- The jumping plant (sprite 4F) of the pipe at x 4368: it goes up and comes back along the path the game gave it
-- (PLANT_PATH, one y a tick), waits, and goes again. It cannot be stomped.
local function plant_step(k)
  k.t = k.t + 1
  local n = #PLANT_PATH
  local u = k.t % (n * 2 + 60)
  if u < n then k.y = PLANT_PATH[u + 1]
  elseif u < n * 2 then k.y = PLANT_PATH[n - (u - n)]
  else k.y = PLANT_PATH[1] end
end

FOE.plant = { step = plant_step, zone = function(k) return "plant", (k.t // 6) % 2 + 1, false, false end,
              stomp = function() end, hurts = function() return true end }

-- The Chargin' Chuck (sprite 91). It looks at Mario and, when he is near, runs at him for 18 ticks at speed 24, waits,
-- and does it again. Three stomps beat it: after each it squashes for a moment. Pictures facing left; the frames and
-- the 18-tick charge come from scripts/smw_diff.py (seq.csv).
local function chuck_step(k)
  k.t = k.t + 1
  k.vy = math.min(k.vy + 4, 64)
  local _, hy = lib.move(k, 0, k.vy / 16 * 1.2)
  if hy ~= 0 then k.vy = 0 end
  if k.state == "dead" then k.y = k.y + k.vy / 16 * 1.2; return end
  if k.state ~= "charge" and k.state ~= "hurt" then k.face = (mario.x + 8 < k.x + 8) and -1 or 1 end
  if k.state == "idle" then
    if k.t > 40 and math.abs(mario.x - k.x) < 112 then k.state, k.t, k.vx = "charge", 0, k.face * 24 end
  elseif k.state == "charge" then
    if lib.move(k, k.vx / 16, 0) ~= 0 then k.vx = -k.vx; k.face = -k.face end
    if k.t >= 18 then k.state, k.t, k.vx = "wait", 0, 0 end
  elseif k.state == "wait" then
    if k.t >= 40 then k.state, k.t = "idle", 0 end
  elseif k.state == "hurt" then
    if k.t >= 14 then k.state, k.t = "wait", 0 end
  end
end

local function chuck_zone(k)
  local flip = k.face > 0
  if k.state == "dead" then return "chuck_hurt", 3, flip, true end
  if k.state == "charge" then return "chuck_run", (k.t // 4) % 4 + 1, flip, false end
  if k.state == "hurt" then return "chuck_hurt", math.min(3, k.t // 5 + 1), flip, false end
  if k.state == "wait" then return "chuck_wait", (k.t // 8) % 2 + 1, flip, false end
  return "chuck_idle", (k.t // 8) % 2 + 1, flip, false
end

local function chuck_stomp(k)
  if k.state == "hurt" or k.state == "dead" then return end
  k.hp = k.hp - 1
  log("chuck stomped, hp", k.hp)
  k.t = 0
  k.vx = 0
  if k.hp <= 0 then k.state, k.vy = "dead", -40 else k.state = "hurt" end
end

FOE.chuck = { step = chuck_step, zone = chuck_zone, stomp = chuck_stomp,
              hurts = function(k) return k.state == "idle" or k.state == "charge" or k.state == "wait" end }

-- The blue P switch (sprite 3E): stomp it and it goes flat, a puff, and the blocks and the coins change places.
local function pswitch_zone(k)
  if k.state == "idle" then return "pswitch", 1, false, false end
  if k.t < 20 then return "pswitch_flat", 1, false, false end
  return "puff", math.min(4, (k.t - 20) // 4 + 1), false, false
end

FOE.pswitch = {
  step = function(k) k.t = k.t + 1; if k.state == "pressed" and k.t > 36 then k.state = "gone" end end,
  zone = pswitch_zone,
  stomp = function(k)
    if k.state ~= "idle" then return end
    k.state, k.t = "pressed", 0
    psw_t = 704
    swap_pswitch(true)
  end,
  hurts = function() return false end }

local function foe_step(k) FOE[k.kind].step(k) end
local function foe_zone(k) return FOE[k.kind].zone(k) end

-- The goal tape (sprite 0x7B) goes up one pixel a tick from y 368 to 244 and down again (248 ticks a cycle).
-- Measured with Mario held away from it, scripts/smw_goal.py.
local function tape_y(t)
  local u = t % 248
  if u < 124 then return 368 - u end
  return 244 + (u - 124)
end

restart_level = function()
  for _, c in ipairs(changed) do
    put16(c[1], c[2], L0[c[2] + 1][c[1] + 1])
  end
  changed, bumping, pops, eggs, bits, swapped, psw_t = {}, {}, {}, {}, {}, {}, 0
  spawn_foes()
  mario = Phys.new(START_X, START_Y)
  prev = { x = mario.x, y = mario.y }
  acc, tape_t, time_left, time_acc, st_t = 0, 0, 400, 0, 0
  state = "play"
end

local function die()
  --GOD--
  if state ~= "play" then return end
  state, st_t = "dying", 0
  mario.vx, mario.vy = 0, 0
end

local function solid(px, py)
  if px < 0 or px >= level_w then return true end
  if py < 0 then return false end
  for _, m in ipairs(MSGS) do                         -- the message blocks are solid sprites
    if px >= m[1] and px < m[1] + 16 and py >= m[2] and py < m[2] + 16 then return true end
  end
  return lib.solid(px, py)
end

local function platform(px, py)
  if py < 0 then return false end
  return mflags(px, py, 1, 1, "col") & lib.PLATFORM ~= 0
end

local last_inp = {}
local function inputs()
  return {
    left = btn("left"), right = btn("right"), up = btn("up"), down = btn("down"),
    run = btn("y") or btn("x"),
    jump = btn("b") or btn("a"),
    spin = btn("a") and not btn("b"),
  }
end

-- The animation of Mario, from his state: a zone of the sheet and a frame. The zones are made of SMW's own
-- poses (mario_walk: poses 00 and 01, and so on); the rules come from scripts/smw_sprites.py (frames.csv): a walk
-- cycle of two poses that goes faster with speed, a skid, a duck, a look up, a jump going up, a fall, the
-- run-jump with arms up and the spin.
local anim, spin_t, spinning = 0, 0, false
local WALK_FRAME = 122                      -- speed units a walk frame lasts: 5 ticks at walking speed

local function pose_of(m, inp)
  local ax = math.abs(m.vx)
  if m.ground then
    spinning = false
    if inp.down then return "mario_duck", 1 end
    local dir = (inp.right and 1 or 0) - (inp.left and 1 or 0)
    if dir ~= 0 and m.vx * dir < -1 then return "mario_skid", 1 end
    if ax < 1 then return inp.up and "mario_look" or "mario_stand", 1 end
    local f = (anim // WALK_FRAME) % 2 + 1
    if inp.run and m.pm >= Phys.PM_FULL then return "mario_prun", f end
    return "mario_walk", f
  end
  if spinning then return "mario_spin", (spin_t // 2) % 4 + 1 end
  if ax >= 56 and inp.run then return "mario_pjump", 1 end
  return m.vy < 0 and "mario_jump" or "mario_fall", 1
end

local function play_tick(inp)
  prev.x, prev.y = mario.x, mario.y
  local was_ground, vy0, spin0 = mario.ground, mario.vy, spinning
  Phys.step(mario, inp, solid, platform)
  if was_ground and not mario.ground and mario.vy < 0 then spinning = inp.spin end
  if state == "play" then
    if mario.bump then
      local bx, by = mario.bump.x, mario.bump.y
      for i, m in ipairs(MSGS) do                       -- a message block: read it
        if bx >= m[1] and bx < m[1] + 16 and by >= m[2] and by < m[2] + 16 then state, msg_i, msg_t = "msg", i, 0 end
      end
      if state == "play" then hit_block(bx // 16, by // 16) end
    end
    pick_coins()
    if spin0 and vy0 > 0 and mario.ground and not was_ground then       -- a spin jump lands on turn blocks: they break
      local cy = (math.floor(mario.y) + 32) // 16
      local broke = false
      for cx = (math.floor(mario.x) + 2) // 16, (math.floor(mario.x) + 13) // 16 do
        if L16[cy + 1] and L16[cy + 1][cx + 1] == 0x11E then
          local d = M16[0x11E]
          put16(cx, cy, 0x25)
          changed[#changed + 1] = { cx, cy }
          for _, pc in ipairs({ { d[1], 0, 0, -1.2, -4.2 }, { d[3], 8, 0, 1.2, -4.2 }, { d[2], 0, 8, -0.8, -2.6 }, { d[4], 8, 8, 0.8, -2.6 } }) do
            bits[#bits + 1] = { cell = pc[1], x = cx * 16 + pc[2], y = cy * 16 + pc[3], vx = pc[4], vy = pc[5], t = 0 }
          end
          broke = true
        end
      end
      if broke then mario.vy, mario.ground = -60, false; score = score + 50 end
    end
  end
  for i = #bits, 1, -1 do                               -- the pieces fly and fall
    local b = bits[i]
    b.t, b.vy = b.t + 1, b.vy + 0.3
    b.x, b.y = b.x + b.vx, b.y + b.vy
    if b.t > 70 then table.remove(bits, i) end
  end
  for i = #bumping, 1, -1 do
    local b = bumping[i]
    b.t = b.t + 1
    if b.t > #BUMP_Y then set16(b.cx, b.cy, USED); table.remove(bumping, i) end
  end
  for i = #pops, 1, -1 do
    pops[i].t = pops[i].t + 1
    if pops[i].t > 24 then table.remove(pops, i) end
  end
  for _, e in ipairs(eggs) do e.t = e.t + 1 end
  local cam_l = math.floor(mario.x) - 108
  H:clear()
  local mz, mf = pose_of(mario, inp)
  mario.zone, mario.frame = mz, mf
  local mcx, mcy = math.floor(mario.x) + ZA[mz][1], math.floor(mario.y) + ZA[mz][2]
  local mflip = mario.face < 0
  if state == "play" then
    H:zone(mario, mz, mf, mcx, mcy, mflip, { team = "p" })                       -- his body can be hurt
    for _, kind in ipairs({ 3, 4 }) do                                           -- feet (stomp) and body (kick)
      for _, b in ipairs(zboxes(mz, mf, kind)) do
        local bx, by, bw, bh = lib.box(b, mcx, mcy, mflip, 32)
        H:hit(mario, bx, by, bw, bh, { team = "p", kind = kind == 3 and "stomp" or "touch" })
      end
    end
  end
  for _, k in ipairs(foes) do
    if k.x > cam_l - 64 and k.x < cam_l + VIEW_W + 64 then foe_step(k) end
    if k.state ~= "dead" and k.state ~= "gone" and k.state ~= "pressed" and k.x > cam_l - 16 and k.x < cam_l + VIEW_W + 16 then
      local zn, fr = foe_zone(k)
      H:zone(k, zn, fr, math.floor(k.x) + ZA[zn][1], math.floor(k.y) + ZA[zn][2], false,
             { team = k.state == "slide" and "s" or "e", attack = { kind = k.state } })
    end
  end
  local contacts = H:check()
  local stomped = {}
  for _, c in ipairs(contacts) do                                  -- Mario hits a Koopa: a stomp or a kick
    if c.by == mario and state == "play" then
      local k = c.to
      if c.hit.kind == "stomp" and mario.vy > 0 and not stomped[k] then
        stomped[k] = true
        FOE[k.kind].stomp(k)
        mario.vy = last_inp.jump and -88 or -48
        mario.ground = false
        score = score + 100
      elseif c.hit.kind == "touch" and k.kind == "koopa" and k.state == "shell" and not stomped[k] then
        stomped[k] = true
        k.state, k.vx, k.t = "slide", (mario.x + 8 < k.x + 8) and 60 or -60, 0
      end
    end
  end
  for _, c in ipairs(contacts) do                                  -- a Koopa or a shell hits Mario or a Koopa
    local k = c.by
    if k ~= mario and k.state ~= "dead" and not stomped[k] then
      if c.to == mario and state == "play" then
        -- a walking Koopa hurts; a shell hurts only what it runs into, not the one behind it
        if FOE[k.kind].hurts(k, mario.x) then
          die()                                                    -- small Mario has no second chance
        end
      elseif c.to ~= mario and k.state == "slide" and c.to.kind == "koopa" and c.to.state == "walk" then
        c.to.state, c.to.vy = "dead", -40
        score = score + 200
      end
    end
  end
  anim = mario.ground and anim + math.abs(mario.vx) or 0
  spin_t = spin_t + 1
end

local function tick(inp)
  st_t = st_t + 1
  gtick = gtick + 1
  if state == "play" then
    tape_t = tape_t + 1
    time_acc = time_acc + 1
    if time_acc >= 35 then                 -- the clock drops one every 35 ticks
      time_acc = 0
      time_left = time_left - 1
      if time_left <= 0 then die() end
    end
    if psw_t > 0 then
      psw_t = psw_t - 1
      if psw_t == 0 then swap_pswitch(false) end
    end
    play_tick(inp)
    if state == "play" and mario.y > LEVEL_H + 32 then die() end
    -- the tape: Mario touches the bar and the course is clear
    local _, _, tw, th = zone("tape")
    local ty = tape_y(tape_t) + ZA.tape[2]
    local bx = TAPE_X + ZA.tape[1]
    local mx0, my0 = math.floor(mario.x) + 2, math.floor(mario.y) + 16
    if state == "play" and mx0 + 12 > bx and mx0 < bx + tw and my0 + 16 >= ty and my0 <= ty + th then
      state, st_t = "clear", 0
      score = score + time_left * 50
      log("course clear, time left", time_left)
    end
  elseif state == "clear" then
    tape_t = tape_t + 1
    local auto = { right = st_t < 160 }
    last_inp = auto
    play_tick(auto)
  elseif state == "dying" then
    prev.x, prev.y = mario.x, mario.y
    if st_t == 20 then mario.vy = -80 end
    if st_t > 20 then
      mario.vy = math.min(mario.vy + Phys.GRAV_HOLD, Phys.FALL_MAX)
      mario.y = mario.y + mario.vy / 16 * Phys.KY
    end
    for _, k in ipairs(foes) do foe_step(k) end
    if st_t > 130 then
      lives = lives - 1
      if lives <= 0 then state, st_t = "over", 0 else restart_level() end
    end
  end
  --TRACE--
end

function _update()
  if btnp("select") then bench = not bench end
  if state == "msg" then
    msg_t = msg_t + 1
    if msg_t > 20 and (btnp("a") or btnp("b") or btnp("start")) then state = "play" end
    return
  end
  if state == "over" or (state == "clear" and st_t > 200) then
    if btnp("start") or btnp("a") or btnp("b") then
      if state == "over" then lives, coins, score = 5, 0, 0 end
      restart_level()
    end
    return
  end
  acc = acc + 50 / 60
  local inp = inputs()
  if state == "play" then last_inp = inp end
  while acc >= 1 do
    acc = acc - 1
    tick(inp)
  end
end

local function draw_mario(x, y)
  local z, f = mario.zone or "mario_stand", mario.frame or 1
  if state == "dying" then z, f = "mario_dead", 1 end
  zspr(z, x + ZA[z][1], y + ZA[z][2], f, mario.face < 0)
end

function _draw()
  cls(0)
  local a = acc
  local x = prev.x + (mario.x - prev.x) * a
  local y = prev.y + (mario.y - prev.y) * a
  camx = math.max(0, math.min(level_w - VIEW_W, math.floor(x) - 108))
  camy = math.max(0, math.min(LEVEL_H - VIEW_H, math.floor(y) - 160))
  clip(VX, VY, VIEW_W, VIEW_H)
  rectfill(VX, VY, VIEW_W, VIEW_H, backdrop)
  local mx, my = camx // 8, camy // 8
  map(mx, my, VX - (camx % 8), VY - (camy % 8), VIEW_W // 8 + 1, VIEW_H // 8 + 1, "main")
  for _, b in ipairs(bumping) do draw16(USED, VX + b.cx * 16 - camx, VY + b.cy * 16 + BUMP_Y[b.t] - camy) end
  for _, p in ipairs(pops) do
    local up = p.t < 12 and p.t * 3 or 36 - (p.t - 12) * 3
    draw16(COIN16, VX + p.x - camx, VY + p.y - up - camy)
  end
  for _, e in ipairs(eggs) do                      -- the egg rises, wobbles, cracks, and Yoshi hatches (frames of scripts/smw_items.py)
    local t, x0, y0 = e.t, e.cx * 16, e.cy * 16
    local zn, fr, y
    if t < 58 then
      y = t < 26 and (y0 - 3 - 13 * t // 25) or (y0 - 16)
      zn, fr = "egg", t < 36 and 1 or (t < 52 and 2 or 3)
    else
      y = y0 - 32
      zn = "yoshi"
      if t < 59 then fr = 1
      elseif t < 101 then fr = (t // 8) % 2 == 0 and 2 or 3
      else fr = 4 end
    end
    zspr(zn, VX + x0 - camx, VY + y - camy, fr)
  end
  for _, d in ipairs(DECOR) do                      -- flowers and tufts: they only turn, a frame every 16 ticks
    if d[2] > camx - 32 and d[2] < camx + VIEW_W + 32 then
      zspr(d[1], VX + d[2] - camx, VY + d[3] - camy, (gtick // 16) % (d[1] == "flower" and 4 or 3) + 1)
    end
  end
  for _, m in ipairs(MSGS) do zspr("msg", VX + m[1] - camx, VY + m[2] - camy, (gtick // 32) % 2 + 1) end
  for _, k in ipairs(foes) do
    if k.y < LEVEL_H + 32 and k.state ~= "gone" then
      local zn, fr, fx, fy = foe_zone(k)
      if k.kind == "plant" then clip(VX, VY, VIEW_W, math.max(0, 336 - camy)) end    -- it comes out of the pipe
      zspr(zn, VX + math.floor(k.x) + ZA[zn][1] - camx, VY + math.floor(k.y) + ZA[zn][2] - camy, fr, fx, fy)
      if k.kind == "plant" then clip(VX, VY, VIEW_W, VIEW_H) end
    end
  end
  for _, b in ipairs(bits) do
    sspr((b.cell % 16) * 8, (b.cell // 16) * 8, 8, 8, VX + math.floor(b.x) - camx, VY + math.floor(b.y) - camy)
  end
  zspr("tape", VX + TAPE_X + ZA.tape[1] - camx, VY + math.floor(tape_y(tape_t)) + ZA.tape[2] - camy)
  draw_mario(VX + math.floor(x) - camx, VY + math.floor(y) - camy)
  if state == "msg" then                              -- the message box, as the game wrote it
    local p = MSGPOS[msg_i]
    zspr("msg" .. p[5] .. "_a", VX + p[1], VY + p[2])
    if zone("msg" .. p[5] .. "_b") then zspr("msg" .. p[5] .. "_b", VX + p[1] + 128, VY + p[2]) end
  end
  clip()
  rect(VX - 1, VY - 1, VIEW_W + 2, VIEW_H + 2, 0x303030)
  print(string.format("MARIO x%d", lives), VX, 28, 0xFFFFFF)
  print(string.format("COINS %d", coins), VX + 108, 28, 0xFFE060)
  print(string.format("DRAGON %d", dragons % 5), VX + 108, 44, 0xF8A0C8)
  print(string.format("TIME %d", time_left), VX + 190, 28, 0xFFFFFF)
  print(string.format("%07d", score), VX + 190, 44, 0xB0B0B0)
  local ms = stat(1)                                -- the cost of the last frame, in ms
  b_sum, b_n, b_max = b_sum + ms, b_n + 1, math.max(b_max, ms)
  if b_n >= 60 then
    b_ms, b_peak, b_sum, b_n, b_max = b_sum / b_n, b_max, 0, 0, 0
    if stat(3) % 300 < 60 then
      log(string.format("bench %.3f ms avg %.3f max, %d fps, lua %d KB, %d instr", b_ms, b_peak, stat(2), stat(0), stat(10)))
    end
  end
  if bench then
    print(string.format("%.2f ms  max %.2f  %d fps", b_ms, b_peak, stat(2)), 8, 322, 0x80FF80)
    print(string.format("lua %d KB  %d instr  x %d  vx %.0f  P %d", stat(0), stat(10), math.floor(mario.x), mario.vx,
                        math.floor(mario.pm)), 8, 338, 0x808080)
  end
  local function banner(a, b)
    rectfill(VX + 28, VY + 70, VIEW_W - 56, 70, 0x101820)
    print(a, VX + (VIEW_W - #a * 16) // 2, VY + 86, 0xFFFFFF, 2)
    if b then print(b, VX + (VIEW_W - #b * 8) // 2, VY + 130, 0xE0E0E0) end
  end
  if state == "clear" and st_t > 160 then
    banner("COURSE CLEAR!", string.format("TIME BONUS %d", time_left * 50))
    if st_t > 200 then print("PRESS B", VX + (VIEW_W - 56) // 2, VY + 150, 0xFFE060) end
  elseif state == "over" then
    banner("GAME OVER", "PRESS B")
  end
end
