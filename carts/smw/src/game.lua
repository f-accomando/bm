-- Super Mario World, first level, for the RGB30 (.b16, 360x360). A personal port: the graphics
-- come from the player's own ROM (scripts/smw_*.py). The movement is in physics.lua.
local VIEW_W, VIEW_H = 256, 224          -- the SNES picture, shown whole inside the 360x360 screen
local VX, VY = (360 - VIEW_W) // 2, (360 - VIEW_H) // 2
local LEVEL_H = 27 * 16
local START_X, START_Y = 16, 352

local level_w
local mario, prev, acc
local spawn_koopas                    -- defined with the Koopas, below
local coins = 0
local bumping = {}                        -- blocks jumping up: {cx, cy, t, used}
local pops = {}                           -- coins thrown up by a block: {x, y, t}
local BUMP_Y = { -2, -4, -5, -6, -5, -4, -2, 0 }
local camx, camy = 0, 192
local backdrop = 0xFF0000

function _init()
  local mw, mh = msize()
  level_w = mw * 8
  spawn_koopas()
  mario = Phys.new(START_X, START_Y)
  prev = { x = mario.x, y = mario.y }
  acc = 0
end

local function set16(cx, cy, d)           -- a Map16 block is four 8x8 cells: TL, BL, TR, BR
  mset(cx * 2, cy * 2, d[1], "main")
  mset(cx * 2, cy * 2 + 1, d[2], "main")
  mset(cx * 2 + 1, cy * 2, d[3], "main")
  mset(cx * 2 + 1, cy * 2 + 1, d[4], "main")
end

local function draw16(d, x, y)
  for i, o in ipairs({ { 0, 0 }, { 0, 8 }, { 8, 0 }, { 8, 8 } }) do
    if d[i] ~= 0 then sspr((d[i] % 16) * 8, (d[i] // 16) * 8, 8, 8, x + o[1], y + o[2]) end
  end
end

local function hit_block(cx, cy)
  local k = L16[cy + 1][cx + 1]
  local kind = KIND[k]
  if kind ~= "coin" and kind ~= "item" then return end
  L16[cy + 1][cx + 1] = 0x132
  set16(cx, cy, { 0, 0, 0, 0 })
  bumping[#bumping + 1] = { cx = cx, cy = cy, t = 0 }
  if kind == "coin" then
    coins = coins + 1
    pops[#pops + 1] = { x = cx * 16, y = cy * 16 - 16, t = 0 }
  end
end

local function pick_coins()
  local x0, x1 = (math.floor(mario.x) + 2) // 16, (math.floor(mario.x) + 13) // 16
  local y0, y1 = (math.floor(mario.y) + 16) // 16, (math.floor(mario.y) + 31) // 16
  for cy = y0, y1 do
    for cx = x0, x1 do
      local row = L16[cy + 1]
      if row and COINS[row[cx + 1] or 0] then
        row[cx + 1] = 0x25
        set16(cx, cy, { 0, 0, 0, 0 })
        coins = coins + 1
      end
    end
  end
end

-- ---- Koopa Troopas (sprite 05). Speeds in 1/16 pixel a tick, as the game keeps them: it walks at 8, a
-- kicked shell slides fast, a stomped one sleeps for 330 ticks. Frame lists from scripts/smw_enemies.py.
local KL = { "05_8_42ad5a82", "05_8_04c81fb9" }      -- walking left, two frames of 8 ticks
local KR = { "05_8_2f5730ba", "05_8_c999e5e3" }      -- walking right
local KT = "05_8_5225aa56"                           -- turning
local KSHELL = "05_9_d229f1b8"
local koopas = {}

spawn_koopas = function()
  koopas = {}
  for _, k in ipairs(KOOPAS) do
    koopas[#koopas + 1] = { x = k[1], y = k[2], vx = k[4] == 1 and 0 or (k[3] == 1 and -8 or 8), vy = 0,
                            face = k[3] == 1 and -1 or 1, state = k[4] == 1 and "shell" or "walk", t = 0, turn = 0 }
  end
end

local function koopa_solid(px, py)
  return mflags(px, py, 1, 1, "col") & 1 ~= 0
end

local function koopa_step(k, solid_at)
  k.t = k.t + 1
  if k.state == "dead" then
    k.vy = math.min(k.vy + 4, 64)
    k.y = k.y + k.vy / 16 * 1.2
    return
  end
  k.vy = math.min(k.vy + 4, 64)
  local oy = k.y
  k.y = k.y + k.vy / 16 * 1.2
  if solid_at(k.x + 2, k.y + 16) or solid_at(k.x + 13, k.y + 16) then
    k.y = math.floor(oy)
    while solid_at(k.x + 2, k.y + 16) or solid_at(k.x + 13, k.y + 16) do k.y = k.y - 1 end
    k.vy = 0
  end
  if k.state == "shell" then
    if k.t > 330 then k.state, k.t, k.vx = "walk", 0, k.face * 8 end
    return
  end
  local nx = k.x + k.vx / 16
  local ahead = nx + (k.vx > 0 and 15 or 0)
  if solid_at(ahead, k.y + 8) then
    if k.state == "slide" then k.vx = -k.vx
    else k.vx = -k.vx; k.turn = 7 end
    k.face = k.vx > 0 and 1 or -1
  else
    k.x = nx
  end
  if k.turn > 0 then k.turn = k.turn - 1 end
end

local function koopa_pic(k)
  if k.state == "shell" or k.state == "slide" or k.state == "dead" then return KSHELL end
  if k.turn > 0 then return KT end
  local f = (k.t // 8) % 2 + 1
  return (k.face < 0 and KL or KR)[f]
end

local function solid(px, py)
  if px < 0 or px >= level_w then return true end
  if py < 0 then return false end
  return mflags(px, py, 1, 1, "col") & 1 ~= 0
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

-- The pose of Mario, from his state. The numbers are SMW's own pose numbers; the rules come from
-- scripts/smw_sprites.py (frames.csv): a walk cycle of two poses that goes faster with speed,
-- a skid, a duck, a look up, a jump going up, a fall, the run-jump with arms up and the spin.
local anim, spin_t, spinning = 0, 0, false
local WALK_FRAME = 122                      -- speed units a walk frame lasts: 5 ticks at walking speed

local function pose_of(m, inp)
  local ax = math.abs(m.vx)
  if m.ground then
    spinning = false
    if inp.down then return 0x3C end
    local dir = (inp.right and 1 or 0) - (inp.left and 1 or 0)
    if dir ~= 0 and m.vx * dir < -1 then return 0x0D end
    if ax < 1 then return inp.up and 0x03 or 0x00 end
    if inp.run and m.pm >= Phys.PM_FULL then
      return (anim // WALK_FRAME) % 2 == 0 and 0x07 or 0x08
    end
    return (anim // WALK_FRAME) % 2 == 0 and 0x00 or 0x01
  end
  if spinning then
    local cycle = { 0x25, 0x00, 0x0F, 0x00 }
    return cycle[(spin_t // 2) % 4 + 1]
  end
  if ax >= 56 and inp.run then return 0x0C end
  return m.vy < 0 and 0x0B or 0x24
end

function _update()
  acc = acc + 50 / 60
  local inp = inputs()
  last_inp = inp
  while acc >= 1 do
    acc = acc - 1
    prev.x, prev.y = mario.x, mario.y
    local was_ground = mario.ground
    Phys.step(mario, inp, solid)
    if was_ground and not mario.ground and mario.vy < 0 then spinning = inp.spin end
    if mario.bump then hit_block(mario.bump.x // 16, mario.bump.y // 16) end
    pick_coins()
    for i = #bumping, 1, -1 do
      local b = bumping[i]
      b.t = b.t + 1
      if b.t > #BUMP_Y then set16(b.cx, b.cy, USED); table.remove(bumping, i) end
    end
    for i = #pops, 1, -1 do
      pops[i].t = pops[i].t + 1
      if pops[i].t > 24 then table.remove(pops, i) end
    end
    local cam_l = math.floor(mario.x) - 108
    for _, k in ipairs(koopas) do
      if k.x > cam_l - 64 and k.x < cam_l + VIEW_W + 64 then koopa_step(k, koopa_solid) end
      if k.state ~= "dead" and k.x > cam_l - 16 and k.x < cam_l + VIEW_W + 16 then
        local mx0, my0 = math.floor(mario.x) + 2, math.floor(mario.y) + 16
        if mx0 < k.x + 16 and mx0 + 12 > k.x and my0 < k.y + 16 and my0 + 16 > k.y then
          if mario.vy > 0 and my0 + 16 - k.y < 12 then                 -- a stomp
            if k.state == "walk" or k.state == "slide" then k.state, k.t, k.vx = "shell", 0, 0
            elseif k.state == "shell" then k.state, k.vx = "slide", (mx0 + 6 < k.x + 8) and 60 or -60 end
            mario.vy = last_inp.jump and -88 or -48
            mario.ground = false
          elseif k.state == "shell" then                               -- a kick
            k.state, k.vx = "slide", (mx0 + 6 < k.x + 8) and 60 or -60
            k.t = 0
          elseif (k.state == "walk" or k.state == "slide") and not (k.state == "slide" and k.t < 8) then
            mario.dead = true                                          -- small Mario has no second chance
          end
        end
        if k.state == "slide" then
          for _, o in ipairs(koopas) do
            if o ~= k and o.state == "walk" and math.abs(o.x - k.x) < 14 and math.abs(o.y - k.y) < 12 then
              o.state, o.vy = "dead", -40
            end
          end
        end
      end
    end
    anim = mario.ground and anim + math.abs(mario.vx) or 0
    spin_t = spin_t + 1
    if mario.y > LEVEL_H + 32 or mario.dead then          -- fell out of the level
      mario = Phys.new(START_X, START_Y)
      prev.x, prev.y = mario.x, mario.y
      spawn_koopas()
    end
  end
end

local cur_pose = 0
local function draw_mario(x, y)
  local p = POSES[cur_pose] or POSES[0]
  local ox = p[5]
  local flip = mario.face < 0
  if flip then ox = 16 - ox - p[3] end
  sspr(p[1], p[2], p[3], p[4], x + ox, y + p[6], flip)
end

function _draw()
  cls(0)
  cur_pose = pose_of(mario, last_inp)
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
  for _, k in ipairs(koopas) do
    local e = ENEMY[koopa_pic(k)]
    if e and k.y < LEVEL_H + 32 then
      sspr(e[1], e[2], e[3], e[4], VX + math.floor(k.x) + e[5] - camx, VY + math.floor(k.y) + e[6] - camy, false, k.state == "dead")
    end
  end
  draw_mario(VX + math.floor(x) - camx, VY + math.floor(y) - camy)
  clip()
  rect(VX - 1, VY - 1, VIEW_W + 2, VIEW_H + 2, 0x303030)
  print(string.format("COINS %d", coins), 8, 8, 0xFFFFFF)
  print(string.format("x %d  vx %.0f  P %d", math.floor(mario.x), mario.vx, math.floor(mario.pm)), 8, 340, 0x808080)
end
