-- Original procedural sprites for the .b16 Titan Clash.
-- Robots are drawn with layered shapes so configurations change the look
-- without needing a giant sheet. Fits easily in a 1024x1024 bank.

SPR = {}

local function add(name, sx, sy, w, h, dx, dy)
  SPR[name] = {sx, sy, w, h, dx, dy}
end

-- simple placeholders; real drawing is in Fighter.draw
add("idle", 0, 0, 64, 96, -32, -96)
add("walk1", 64, 0, 64, 96, -32, -96)
add("walk2", 128, 0, 64, 96, -32, -96)
-- more frames as needed; the draw function uses state to choose colors and shapes

function sprite(name, x, y, flip)
  -- fallback if called; most drawing is custom
  local r = SPR[name]
  if r then
    -- draw a simple box for now
    rectfill(x + r[5], y + r[6], r[3], r[4], 0x4060A0)
  end
end
