-- Icons, order cards, and the 2D marks over the kitchen: what is in a pot
-- and how far it has cooked, chopping progress, plate contents, the player
-- markers, money, combo and clock.
-- sheet.png (mkassets.py): 16x16 icons in rows of 32 (ingredients 0-22,
-- chopped 32-54, symbols from 64), 8x8 badges at y = 64, portraits 32x32
-- at y = 80.

local IC = {
  knife = 64, pot = 65, pan = 66, oven = 67, blender = 68, plate = 69, dplate = 70, bowl = 71,
  burnt = 72, ext = 73, coin = 74, star = 75, star0 = 76, heart = 77, heart0 = 78, clock = 79,
  fire = 80, drop = 81, rat = 82, duck = 83, ghost = 84, tornado = 85, steam = 86, lock = 87,
  check = 88, cross = 89, arrow = 90, bA = 91, bB = 92, bX = 93, bY = 94, register = 95,
  up = 96, snow = 97, wrench = 98, heart2 = 99, trophy = 100, skull = 101, bell = 102, speed = 103,
  shield = 104, box = 105, knife2 = 106, book = 107,
}
Hud.IC = IC
local MINI = { chop = 0, boil = 1, fry = 2, bake = 3, blend = 4, tick = 5, cross = 6, fire = 7 }
Hud.MINI = MINI

function Hud.icon(n, x, y)
  sspr((n % 32) * 16, (n // 32) * 16, 16, 16, x, y)
end
function Hud.mini(n, x, y)
  sspr(n * 8, 64, 8, 8, x, y)
end
function Hud.portrait(i, x, y)
  sspr(i * 32, 80, 32, 32, x, y)
end

---------------------------------------------------------------- food icons

local parse = Food.parse

local function single_fry(k)
  return k.proc and #k.kids == 1 and parse(k.kids[1]).ing
end

-- width and height of a key drawn as icons
local function part_size(key)
  local k = parse(key)
  if k.ing or single_fry(k) then return 16, 16 end
  local w = 3
  for _, s in ipairs(k.kids) do w = w + part_size(s) + 1 end
  return w + 2, 20
end
Hud.part_size = part_size

local BOX_BG = { boil = 0xC8E0F0, fry = 0xF0D0A8, bake = 0xF0C8A0, blend = 0xF0D0F0 }

-- draws a key's icons at (x, y); returns the width
local function draw_part(key, x, y)
  local k = parse(key)
  if k.ing then
    local g = Data.ING[k.ing]
    Hud.icon(k.chopped and g.icon + 32 or g.icon, x, y)
    return 16
  end
  if single_fry(k) then
    local kk = parse(k.kids[1])
    local g = Data.ING[kk.ing]
    Hud.icon(kk.chopped and g.icon + 32 or g.icon, x, y)
    Hud.mini(MINI[k.proc], x + 9, y + 9)
    return 16
  end
  local w = part_size(key)
  panel(x, y, w, 20, BOX_BG[k.proc] or 0xE0E0E0)
  local xx = x + 3
  for _, s in ipairs(k.kids) do
    local pw, ph = part_size(s)
    draw_part(s, xx, y + 2 + (ph < 20 and 0 or -2))
    xx = xx + pw + 1
  end
  Hud.mini(MINI[k.proc] or 0, x - 3, y - 3)
  return w
end
Hud.draw_part = draw_part

-- a row of parts, wrapping at maxw; returns the height used
local function draw_parts(parts, x, y, maxw, dry)
  local cx, cy, rowh = x, y, 0
  for _, key in ipairs(parts) do
    local w, h = part_size(key)
    if cx > x and cx + w > x + maxw then cx, cy, rowh = x, cy + rowh + 2, 0 end
    if not dry then draw_part(key, cx, cy + (20 - h) // 2) end
    cx = cx + w + 3
    rowh = max(rowh, 20)
  end
  return cy + rowh - y
end
Hud.draw_parts = draw_parts

---------------------------------------------------------------- order cards

local CARD_W = 104

function Hud.card_height(r)
  if not r.card_h then r.card_h = 24 + draw_parts(r.parts, 0, 0, CARD_W - 8, true) + 8 end
  return r.card_h
end

function Hud.orders(run)
  local x = 6
  local right = run.endless and W - 126 or W - 76
  for i, o in ipairs(run.orders) do
    local r = o.rec
    local h = Hud.card_height(r)
    local k = o.t / o.T
    local y = 4 + (o.enter or 0) * -70
    local shake = 0
    if k < 0.2 then shake = floor(sin(G.t * 40) * 2) end
    if o.leave then y = y - o.leave * 70 end
    local bc = k > 0.5 and 0x40B040 or k > 0.25 and 0xE0B020 or 0xE03020
    panel(x + shake, y, CARD_W, h, 0xFFF8EC, bc)
    local name = #r.name > 12 and sub(r.name, 1, 12) or r.name
    print(name, x + shake + 5, y + 3, 0x40302A)
    draw_parts(r.parts, x + shake + 5, y + 21, CARD_W - 8)
    bar(x + shake + 4, y + h - 7, CARD_W - 8, 4, k, bc, 0xD8D0C0)
    if o.flash and o.flash > 0 then
      rect(x + shake - 1, y - 1, CARD_W + 2, h + 2, 0xFFFFFF)
    end
    x = x + CARD_W + 6
    if x + CARD_W > right then break end
  end
end

---------------------------------------------------------------- over the kitchen

local function bubble_at(x, y, z)
  return project3d(x, y, z)
end

function Hud.world_marks(run)
  -- stations: pot contents, progress, warnings; chopping; plates on counters
  for _, st in ipairs(run.stations) do
    local b = st.box
    if b and #b.items > 0 then
      local sx, sy = bubble_at(st.x, Kit.TOP + 0.75, st.z)
      if sx then
        local w = #b.items * 17 + 5
        local x0 = floor(sx - w / 2)
        local y0 = floor(sy - 22)
        if b.burnt then
          panel(x0, y0, w, 22, 0x302820, 0x000000)
          Hud.icon(IC.burnt, floor(sx - 8), y0 + 3)
        else
          local warn = b.done and st.def.burn and b.t2 > st.def.burn * 0.45
          local bg = warn and ((G.frame // 6) % 2 == 0 and 0xFF6040 or 0xFFD060) or 0xFFF8EC
          panel(x0, y0, w, 22, bg, 0x40302A)
          for i, key in ipairs(b.items) do draw_part(key, x0 + 3 + (i - 1) * 17, y0 + 3) end
          if b.done then
            Hud.mini(MINI.tick, x0 + w - 6, y0 - 4)
          else
            bar(x0 + 2, y0 + 22, w - 4, 4, b.t / st.def.cook, 0x40C040, 0x303030)
          end
        end
      end
    end
    local it = st.item
    if it then
      if it.chop and it.chop > 0 then
        local sx, sy = bubble_at(st.x, Kit.TOP + 0.35, st.z)
        if sx then bar(floor(sx - 14), floor(sy), 28, 5, it.chop, 0x40C0FF, 0x203040) end
      elseif it.plate and #it.parts > 0 then
        local sx, sy = bubble_at(st.x, Kit.TOP + 0.45, st.z)
        if sx then Hud.plate_icons(it, sx, sy) end
      end
    end
    if st.kind == "sink" and st.dirty > 0 and st.prog > 0 then
      local sx, sy = bubble_at(st.x, Kit.TOP + 0.35, st.z)
      if sx then bar(floor(sx - 14), floor(sy), 28, 5, st.prog, 0x80E0FF, 0x203040) end
    end
    if st.fire > 0 then
      local sx, sy = bubble_at(st.x, Kit.TOP + 0.9, st.z)
      if sx and (G.frame // 8) % 2 == 0 then Hud.icon(IC.fire, floor(sx - 8), floor(sy - 8)) end
    end
  end
  for _, l in ipairs(run.loose) do
    if l.item.plate and #l.item.parts > 0 then
      local sx, sy = bubble_at(l.x, l.y + 0.4, l.z)
      if sx then Hud.plate_icons(l.item, sx, sy) end
    end
  end
  -- chefs: what their plate holds, their player marker, what they say
  for _, c in ipairs(run.chefs) do
    if not c.fall or c.fall < 0.3 then
      local top = (c.head_y or 1.4) + 0.12
      local sx, sy = project3d(c.x, top, c.z)
      if sx then
        sx, sy = floor(sx), floor(sy)
        local col = PCOL[c.pad] or 0xFFFFFF
        -- a coloured arrow with the player number
        local ay = sy - 12 + floor(sin(G.t * 5 + c.n) * 1.5)
        tri(sx - 6, ay, sx + 6, ay, sx, ay + 7, col)
        panel(sx - 11, ay - 15, 22, 15, col)
        print("P" .. c.pad, sx - 8, ay - 15, 0xFFFFFF)
        if c.hold and c.hold.plate and #c.hold.parts > 0 then
          Hud.plate_icons(c.hold, sx, ay - 18)
        end
        if c.say and c.say_t > 0 then
          text_cs(c.say, sx, ay - 36, 0xFFFFFF)
        end
        if c.work and c.work.kind == "chop" and c.work.st.item then
          -- the bar over the board is enough
        end
      end
    end
  end
end

-- the icons of what is on a plate, centred above (sx, sy)
function Hud.plate_icons(plate, sx, sy)
  local n = #plate.parts
  local w = 0
  for _, k in ipairs(plate.parts) do w = w + part_size(k) + 1 end
  local x0 = floor(sx - w / 2) - 2
  local y0 = floor(sy - 24)
  local done = G.run and G.run.sigs and G.run.sigs[Food.plate_sig(plate.parts)]
  panel(x0, y0, w + 5, 24, done and 0xD8F8C8 or 0xFFFFFF, done and 0x30A030 or 0x808080)
  local x = x0 + 3
  for _, k in ipairs(plate.parts) do
    local pw, ph = part_size(k)
    draw_part(k, x, y0 + 2 + (20 - ph) // 2)
    x = x + pw + 1
  end
  if done then Hud.mini(MINI.tick, x0 + w, y0 - 3) end
end

---------------------------------------------------------------- corners

function Hud.status(run)
  -- clock, top right
  local tl = run.time_left
  if tl then
    local low = tl < 30
    panel(W - 70, 4, 66, 26, low and ((G.frame // 15) % 2 == 0 and 0xE03020 or 0x802010) or 0x302A3A, 0x000000)
    Hud.icon(IC.clock, W - 66, 9)
    print(fmt_time(tl), W - 46, 9, 0xFFFFFF)
  end
  -- money and combo, bottom left
  panel(4, H - 30, 118, 26, 0x302A3A, 0x000000)
  Hud.icon(IC.coin, 9, H - 25)
  local shown = run.coins_shown or 0
  print(fmt("%d", floor(shown)), 30, H - 25, 0xFFE060)
  if run.combo and run.combo > 1 then
    local c = ({ 0xFFFFFF, 0x80E0FF, 0xFFD040, 0xFF60C0 })[min(4, run.combo)]
    local s = "x" .. min(4, run.combo)
    print(s, 90, H - 25, c)
    if run.combo_flash and run.combo_flash > 0 then
      text_cs("COMBO " .. s .. "!", W / 2, H - 70 - floor(run.combo_flash * 30), c, 2)
    end
  end
end
