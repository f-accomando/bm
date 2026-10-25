-- Super Mario World, first level, for the RGB30 (.b16, 360x360). A personal port: the graphics
-- come from the player's own ROM (scripts/smw_*.py). The movement is in physics.lua.
local VIEW_W, VIEW_H = 256, 224          -- the SNES picture, shown whole inside the 360x360 screen
local VX, VY = (360 - VIEW_W) // 2, (360 - VIEW_H) // 2
local LEVEL_H = 27 * 16
local START_X, START_Y = 16, 352

local level_w
local mario, prev, acc
local coins = 0
local bumping = {}                        -- blocks jumping up: {cx, cy, t, used}
local pops = {}                           -- coins thrown up by a block: {x, y, t}
local BUMP_Y = { -2, -4, -5, -6, -5, -4, -2, 0 }
local camx, camy = 0, 192
local backdrop = 0xFF0000

function _init()
  local mw, mh = msize()
  level_w = mw * 8
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
    anim = mario.ground and anim + math.abs(mario.vx) or 0
    spin_t = spin_t + 1
    if mario.y > LEVEL_H + 32 then          -- fell out of the level
      mario = Phys.new(START_X, START_Y)
      prev.x, prev.y = mario.x, mario.y
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
  draw_mario(VX + math.floor(x) - camx, VY + math.floor(y) - camy)
  clip()
  rect(VX - 1, VY - 1, VIEW_W + 2, VIEW_H + 2, 0x303030)
  print(string.format("COINS %d", coins), 8, 8, 0xFFFFFF)
  print(string.format("x %d  vx %.0f  P %d", math.floor(mario.x), mario.vx, math.floor(mario.pm)), 8, 340, 0x808080)
end
