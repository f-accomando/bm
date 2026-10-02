-- The HUD, after Overwatch's layout, at 320x180: health in segments of 25
-- (white health, orange armour, blue shields) at the bottom left; the
-- abilities with their cooldowns and buttons at the bottom right; the
-- ultimate in the middle; crosshair, hit markers, kill feed.

Hud = {}

local W, Hh = 320, 180
local WHITE, ARMOR, SHIELD, DMG = 0xF4F4F4, 0xFFB43C, 0x5AC8FF, 0xFF5050
local INK = 0x101418

-- the button of an action, as a chip for the pad or the keyboard in use
local KEYS = {
  pad = { fire = "R2", fire2 = "L2", ab1 = "L1", ab2 = "R1", ult = "Y", jump = "A", melee = "R3", reload = "X" },
  kbd = { fire = "j", fire2 = "k", ab1 = "shift", ab2 = "e", ult = "q", jump = "space", melee = "v", reload = "r" },
}
function Hud.key(action)
  local set = Input.cmd.pad and KEYS.pad or KEYS.kbd
  return set[action]
end

local function bar_segments(x, y, a)
  -- every segment is 25 points, 1 px apart; the pools in a row
  local total = Actors.total_max(a)
  local seg = 25
  local n = math.ceil(total / seg)
  local w = min(4, floor(124 / n) - 1)
  local cx = x
  local pools = { { a.hp, a.hpmax, WHITE }, { a.armor, a.armormax, ARMOR }, { a.shield, a.shieldmax, SHIELD } }
  for _, pl in ipairs(pools) do
    local v, m, c = pl[1], pl[2], pl[3]
    local k = math.ceil(m / seg)
    for i = 0, k - 1 do
      local fill = clamp((v - i * seg) / seg, 0, 1)
      rectfill(cx, y, w, 7, 0x2A2E36)
      if fill > 0 then rectfill(cx, y + floor(7 * (1 - fill)), w, 7 - floor(7 * (1 - fill)), c) end
      cx = cx + w + 1
    end
  end
  if a.over > 0 then rectfill(x, y - 3, min(124, floor(a.over / seg * (w + 1))), 2, 0x60FF90) end
  return cx
end

local function ability_box(x, y, ab, a)
  local left, total, active, usable = ab.state(a)
  local ready = left <= 0 and usable
  local bg = active and 0xFFB43C or (ready and 0xE8ECF0 or 0x50565F)
  rectfill(x, y, 22, 18, INK)
  rect(x, y, 22, 18, bg)
  -- the icon: a few strokes per ability (until the sheet has pictures)
  Hud.icon(ab.icon, x + 11, y + 9, ready and 0xFFFFFF or 0x8A9098, a)
  if left > 0 and usable then
    local s = left >= 1 and tostring(math.ceil(left)) or string.format("%.1f", left)
    rectfill(x + 1, y + 1, 20, 16, 0x202428)
    print(s, x + 11 - #s * 3, y + 3, 0xFFFFFF)
  end
  local k = Hud.key(ab.key)
  if k then
    local pw = prompt(k, true)
    prompt(k, x + 11 - pw // 2, y + 19, true)
  end
end

-- tiny pictures of the abilities (drawn, not from the sheet)
function Hud.icon(n, x, y, c, a)
  if n == 1 then                   -- afterburners: two flames
    tri(x - 6, y + 5, x - 3, y - 6, x, y + 5, c)
    tri(x, y + 5, x + 3, y - 6, x + 6, y + 5, c)
  elseif n == 2 then               -- rockets: three darts
    for i = -1, 1 do
      line(x + i * 4 - 2, y + 5, x + i * 4 + 2, y - 5, c)
      pset(x + i * 4 + 2, y - 5, 0xFF8A30)
    end
  elseif n == 3 then               -- field: a fan
    tri(x - 7, y + 4, x + 7, y - 6, x + 7, y + 6, c)
  else
    circ(x, y, 5, c)
  end
end

local function ult_meter(cx, cy, a)
  local v = a.ult
  local r = 13
  circfill(cx, cy, r + 2, INK)
  -- the charged part as a fan of triangles
  local n = floor(v / 100 * 24)
  local col = v >= 100 and ((floor(G.t * 4) % 2 == 0) and 0xFFE070 or 0xFFFFFF) or 0x5AC8FF
  for i = 0, n - 1 do
    local a0, a1 = -pi / 2 + i * 2 * pi / 24, -pi / 2 + (i + 1) * 2 * pi / 24
    tri(cx, cy, cx + floor(cos(a0) * r), cy + floor(sin(a0) * r), cx + floor(cos(a1) * r), cy + floor(sin(a1) * r), col)
  end
  circfill(cx, cy, r - 4, INK)
  if v >= 100 then
    local k = Hud.key("ult")
    local pw = prompt(k, true)
    prompt(k, cx - pw // 2, cy - 6, true)
  else
    local s = tostring(floor(v)) .. "%"
    print(s, cx - #s * 3, cy - 5, 0xFFFFFF)
  end
end

local function crosshair(a)
  local cx, cy = W // 2, Hh // 2
  local f = a.form
  if a.hero.id == "rally" and f.name == "mech" then
    circ(cx, cy, 11, 0xFFFFFF)
    pset(cx, cy, 0xFFFFFF)
  else
    rectfill(cx - 5, cy, 3, 1, 0xFFFFFF)
    rectfill(cx + 3, cy, 3, 1, 0xFFFFFF)
    rectfill(cx, cy - 5, 1, 3, 0xFFFFFF)
    rectfill(cx, cy + 3, 1, 3, 0xFFFFFF)
  end
  if a.hit_marker_t and a.hit_marker_t > 0 then
    local c = a.hit_marker == 2 and DMG or 0xFFFFFF
    line(cx - 7, cy - 7, cx - 3, cy - 3, c)
    line(cx + 7, cy - 7, cx + 3, cy - 3, c)
    line(cx - 7, cy + 7, cx - 3, cy + 3, c)
    line(cx + 7, cy + 7, cx + 3, cy + 3, c)
  end
end

local function meters(a)
  -- extra meters of the hero (Rally's Null Field) under the crosshair
  for _, ab in ipairs(a.hero.hud) do
    if ab.meter then
      local v, on = ab.meter(a)
      if v then
        local x, y = W // 2 - 20, Hh // 2 + 18
        rectfill(x - 1, y - 1, 42, 5, INK)
        rectfill(x, y, floor(40 * v), 3, on and 0xFFB43C or 0xE8ECF0)
      end
    end
  end
end

local function damage_arrow(a)
  local k = a.last_hit_by
  if not k or k == a or G.t - (a.dmg_t or -9) > 0.8 then return end
  local ang = atan(k.x - a.x, k.z - a.z) - a.yaw
  local cx, cy = W // 2, Hh // 2
  local r = 40
  local x, y = cx + sin(ang) * r, cy - cos(ang) * r
  local px, py = cos(ang) * 6, sin(ang) * 6
  tri(floor(x - px), floor(y - py), floor(x + px), floor(y + py),
      floor(cx + sin(ang) * (r + 7)), floor(cy - cos(ang) * (r + 7)), DMG)
end

local function feed()
  local y = 2
  for i = #Actors.feed, 1, -1 do
    local f = Actors.feed[i]
    if G.t - f.t < 6 then
      local s = f.killer .. " > " .. f.victim
      local x = W - #s * 6 - 4
      rectfill(x - 2, y - 1, #s * 6 + 4, 12, INK)
      print(f.killer, x, y, TEAM_RGB[f.kt] or 0xFFFFFF)
      print(">", x + #f.killer * 6 + 6, y, 0xFFFFFF)
      print(f.victim, x + (#f.killer + 3) * 6, y, TEAM_RGB[f.vt] or 0xFFFFFF)
      y = y + 13
    end
  end
end

function Hud.draw(a)
  font("6x12")
  if a.alive then
    crosshair(a)
    meters(a)
    damage_arrow(a)
    -- health, bottom left
    local x, y = 8, Hh - 22
    local s = floor(Actors.total(a) + 0.5) .. "/" .. floor(Actors.total_max(a))
    print(a.hero.name .. "  " .. s, x, y - 13, 0xFFFFFF)
    bar_segments(x, y, a)
    -- abilities, bottom right
    local nab = 0
    for _, ab in ipairs(a.hero.hud) do if ab.state then nab = nab + 1 end end
    local ax = W - 6 - 27 * nab
    for _, ab in ipairs(a.hero.hud) do
      if ab.state then
        ability_box(ax, Hh - 34, ab, a)
        ax = ax + 27
      end
    end
    ult_meter(W // 2, Hh - 22, a)
    -- low health: red corners
    if a.hp < a.hpmax * 0.3 and a.armor <= 0 and floor(G.t * 3) % 2 == 0 then
      rect(0, 0, W, Hh, DMG)
      rect(1, 1, W - 2, Hh - 2, DMG)
    end
  else
    local s = "RESPAWN IN " .. math.ceil(max(0, a.respawn - a.dead_t))
    rectfill(W // 2 - #s * 3 - 4, 40, #s * 6 + 8, 14, INK)
    print(s, W // 2 - #s * 3, 41, 0xFFFFFF)
  end
  feed()
  if Fx.flash > 0 then
    rect(0, 0, W, Hh, Fx.flash_rgb)
    rect(1, 1, W - 2, Hh - 2, Fx.flash_rgb)
  end
  font()
end
