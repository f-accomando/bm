-- Titan Clash EX: Arcade Fighting Game HUD
-- Optimized for 360x360 square screen (PowKiddy RGB30).
-- Features: Animated Life bars with damage trails, Armor destruction gauges,
-- Pilot Portraits, Tag Partner reserves, Tag Battery cells, Round Timer,
-- and 3-Level Hyper Super Gauges.

local BAR_W = 104
local LIFE_COLS = { 0x38D848, 0x88FF60, 0x187820 }       -- vibrant green
local LIFE_LOW  = { 0xE83020, 0xFF6040, 0x901010 }       -- danger red
local ARMOR_COLS = { 0x38A8FF, 0x98E0FF, 0x1858B0 }      -- cyan shield
local ARMOR_CRACK = { 0x8A9EBA, 0xD0E0F0, 0x485870 }     -- cracked steel
local ENERGY_COLS = { 0x20E0A0, 0x80FFD0, 0x108050 }     -- plasma green
local HEAT_COLS = { 0xFFA020, 0xFFE060, 0xB05010 }       -- thermal orange
local HEAT_WARN = { 0xFF3020, 0xFFA080, 0x901010 }       -- overheat red

local SUPER_LV1 = { 0x2890FF, 0x88D8FF, 0x1050A0 }       -- blue
local SUPER_LV2 = { 0xE0A820, 0xFFE060, 0x906810 }       -- gold
local SUPER_LV3 = { 0xE02850, 0xFFA0C0, 0x901030 }       -- crimson

local EMPTY_BG = 0x10141C
local FRAME_COL = 0x607898
local DMG_TRAIL = 0xC82818

Hud.P_COL = { 0x58B4FF, 0xFF6A50 }

local DIGIT = { [0] = "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9" }

local function bar(x, y, w, h, k, col, right, trail)
  rectfill(x - 1, y - 1, w + 2, h + 2, 0x000000)
  rect(x - 1, y - 1, w + 2, h + 2, FRAME_COL)
  rectfill(x, y, w, h, EMPTY_BG)

  local function fill(ratio, c1, c2, c3)
    local fw = floor(w * clamp(ratio, 0, 1) + 0.5)
    if fw <= 0 then return end
    local fx = right and (x + w - fw) or x
    rectfill(fx, y, fw, h, c1)
    rectfill(fx, y, fw, 1, c2)
    if h > 3 then rectfill(fx, y + h - 1, fw, 1, c3) end
  end

  if trail and trail > k then fill(trail, DMG_TRAIL, 0xFF8870, 0x801008) end
  fill(k, col[1], col[2], col[3])
end

local function draw_clock(secs)
  panel(158, 6, 44, 28, 0x080C14, 0x6880A4)
  local s = clamp(secs, 0, 99)
  sprite(DIGIT[s // 10], 171, 20)
  sprite(DIGIT[s % 10], 189, 20)
end

local function draw_round_wins(wins1, wins2)
  for k = 1, 2 do
    local x1 = 148 - (k - 1) * 10
    circfill(x1, 14, 4, 0x000000)
    circfill(x1, 14, 3, k <= wins1 and 0xFFD040 or 0x2A3240)
    if k <= wins1 then pset(x1 - 1, 13, 0xFFFFFF) end

    local x2 = 212 + (k - 1) * 10
    circfill(x2, 14, 4, 0x000000)
    circfill(x2, 14, 3, k <= wins2 and 0xFFD040 or 0x2A3240)
    if k <= wins2 then pset(x2 - 1, 13, 0xFFFFFF) end
  end
end

local function draw_side(f, side_num)
  local right = side_num == 2
  local px = right and 328 or 6
  local p_mini_x = right and 308 or 34
  local bar_x = right and 202 or 54

  -- 1. Pilot Portrait
  draw_pilot_face(f.C.pilot_id or side_num, px, 6, right)

  -- 2. Tag Partner Mini-Portrait & Reserve Life (if Tag team)
  if f.team and f.team.partner and f.team.partner ~= f then
    local p = f.team.partner
    draw_pilot_face(p.C.pilot_id or (side_num + 2), p_mini_x, 6, right)
    -- small partner HP gauge
    bar(p_mini_x, 24, 16, 4, p.life / 1000, p.life > 250 and LIFE_COLS or LIFE_LOW, right)
  end

  -- 3. Main Life Bar
  local l_col = f.life > 250 and LIFE_COLS or LIFE_LOW
  bar(bar_x, 8, BAR_W, 10, f.life / 1000, l_col, right, f.life_show / 1000)

  -- 4. Armor Bar (scales with maximum armor)
  local arm_w = floor(BAR_W * clamp(f.armor_max / 500, 0.5, 1.0))
  local arm_x = right and (bar_x + BAR_W - arm_w) or bar_x
  local arm_c = f.shoulder == "cracked" and ARMOR_CRACK or ARMOR_COLS
  bar(arm_x, 20, arm_w, 4, f.armor / f.armor_max, arm_c, right)

  -- Armor notch at 50%
  local notch_x = right and (arm_x + arm_w - floor(arm_w / 2)) or (arm_x + floor(arm_w / 2))
  rectfill(notch_x, 19, 1, 6, 0xFFFFFF)

  -- 5. Tag Switch Batteries (2 slots)
  for b = 1, 2 do
    local bx = right and (bar_x + BAR_W - b * 12) or (bar_x + (b - 1) * 12)
    local ready = f.tag_slots >= b
    rectfill(bx, 26, 9, 4, 0x000000)
    rectfill(bx + 1, 27, 7, 2, ready and 0x38E068 or 0x283830)
  end

  -- 6. Names and Status alerts
  local name_str = (f.cpu and "CPU " or "P" .. side_num .. " ") .. f.C.name
  if f.shoulder == "gone" and G.frame % 36 < 24 then
    name_str = "ARMOR BROKEN!"
  end
  local name_col = f.shoulder == "gone" and 0xFF4A28 or (right and 0xFF9A88 or 0x88C4FF)

  if right then
    text_r(name_str, 324, 34, name_col, 1)
  else
    text(name_str, 6, 34, name_col, 1)
  end

  -- 7. Bottom Resources: Energy & Heat
  local bot_x = right and (W - 14 - 140) or 14
  local e_w, h_w = 66, 66

  -- Energy
  bar(bot_x, 326, e_w, 5, f.energy / 100, ENERGY_COLS, right)
  -- Heat / Hot
  local is_hot = f.overheat > 0
  bar(bot_x + 74, 326, h_w, 5, f.heat / 100, is_hot and HEAT_WARN or HEAT_COLS, right)
  if is_hot and G.frame % 20 < 12 then
    text("OVERHEAT", bot_x + 76, 318, 0xFF4A28, 1)
  end

  -- 8. Super Meter (3 Levels!)
  local lv = f.super_meter // 100
  local super_k = (f.super_meter % 100) / 100
  if lv >= 3 then super_k = 1.0 end
  local super_c = lv >= 2 and SUPER_LV3 or (lv >= 1 and SUPER_LV2 or SUPER_LV1)

  bar(bot_x, 335, 140, 10, super_k, super_c, right)

  local lv_text = lv >= 3 and "MAX 3" or ("LV." .. lv)
  local tx = right and (bot_x + 144) or (bot_x - 30)
  text(lv_text, bot_x + 4, 336, 0xFFFFFF, 1)

  -- 9. Combo notification
  if f.combo_show and f.combo_show >= 2 and f.combo_t then
    local cx = right and (W - 120) or 20
    text(f.combo_show .. " HITS!", cx, 68, 0xFFD040, 2)
    local rating = f.combo_show >= 6 and "TITAN RAVE!!" or (f.combo_show >= 4 and "CRITICAL COMBO!" or "NICE HIT!")
    text(rating, cx, 86, 0xFFFFFF, 1)
  end
end

function Hud.draw(fighters, secs, wins1, wins2)
  if not fighters or #fighters < 2 then return end
  draw_side(fighters[1], 1)
  draw_side(fighters[2], 2)
  draw_clock(secs)
  draw_round_wins(wins1, wins2)
end
