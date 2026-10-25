-- Mario's movement, tuned on traces of the PAL ROM (scripts/smw_trace.py).
-- The game runs on 50 Hz ticks. Positions are in pixels, speeds in 1/16 pixel per tick.
-- A callback tells the engine what is solid, so the same code runs in the game and in the test.
local Phys = {
  TICK = 1 / 50,
  ACCEL = 1.728,      -- speed gained a tick while the stick is held
  AIR_ACCEL = 1.896, -- the same in the air
  DECEL = 1.276,       -- speed lost a tick with the stick released (ground)
  SKID = 3.465,        -- speed lost a tick when the stick is opposite to the movement
  WALK = 24.5,       -- walking speed
  RUN = 45.5,        -- running speed (Y held)
  PSPEED = 59.5,     -- running at full P meter
  PM_START = 39.9,     -- the P meter starts to fill above this speed on the ground
  PM_RATE = 2.172,    -- P meter gained a tick while running fast on the ground
  PM_FULL = 104,
  PM_DRAIN = 1.0,
  JUMP = 76,         -- jump speed at rest; a faster Mario jumps higher
  JUMP_SPEED = 0.265,
  GRAV_HOLD = 3.4,   -- gravity while the jump button is held
  GRAV_FREE = 6.831,   -- gravity after it is released
  FALL_MAX = 71.405,
  KY = 1.191,          -- vertical speed to pixels (tuned on the jump trace)
  W = 12, H = 16,    -- Mario's box (small Mario): x+2 .. x+14, y+16 .. y+32
}

function Phys.new(x, y)
  return { x = x, y = y, vx = 0, vy = 0, pm = 0, ground = true, face = 1, held = false }
end

local function hit(solid, px, py, w, h)
  return solid(px, py) or solid(px + w - 1, py) or solid(px, py + h - 1) or solid(px + w - 1, py + h - 1)
         or (h > 16 and (solid(px, py + h // 2) or solid(px + w - 1, py + h // 2)))
end

local function move(m, solid, plat)
  -- x, then y: a wall or a floor stops Mario on its edge. The game moves first with the speed of the
  -- last tick and then changes the speed, so the position is one tick behind the speed (the traces show it).
  m.bump = nil
  local ox = m.x
  m.x = m.x + m.vx / 16
  if hit(solid, math.floor(m.x) + 2, math.floor(m.y) + 16, Phys.W, Phys.H) then
    m.x = ox
    m.vx = 0
  end
  local oy = m.y
  m.y = m.y + m.vy / 16 * Phys.KY
  m.ground = false
  local landed = false
  if plat and m.vy > 0 then            -- coming down into the row of a platform: stand on it
    local of, nf = math.floor(oy) + 32, math.floor(m.y) + 32
    if nf > of and (nf - 1) // 16 > (of - 1) // 16 then
      local px = math.floor(m.x)
      if plat(px + 2, nf - 1) or plat(px + Phys.W + 1, nf - 1) then
        m.y = ((nf - 1) // 16) * 16 - 32
        landed = true
      end
    end
  end
  if landed then
    m.ground = true
    m.vy = 0
  elseif hit(solid, math.floor(m.x) + 2, math.floor(m.y) + 16, Phys.W, Phys.H) then
    if m.vy > 0 then
      m.y = math.floor(m.y)
      while hit(solid, math.floor(m.x) + 2, m.y + 16, Phys.W, Phys.H) do m.y = m.y - 1 end
      m.ground = true
    else
      local hx, hy = math.floor(m.x), math.floor(m.y) + 16
      local cl, cr = solid(hx + 2, hy), solid(hx + Phys.W + 1, hy)
      -- the block he hit: under his middle if both sides are solid, else the solid side
      m.bump = { x = (cl and cr) and hx + 8 or (cl and hx + 2 or hx + Phys.W + 1), y = hy }
      m.y = oy
    end
    m.vy = 0
  end
  local fy = math.floor(m.y) + 32
  local on_plat = plat and fy % 16 == 0 and m.vy >= 0 and
                  (plat(math.floor(m.x) + 2, fy) or plat(math.floor(m.x) + Phys.W + 1, fy))
  if not m.ground and m.vy >= 0 and (on_plat or hit(solid, math.floor(m.x) + 2, math.floor(m.y) + 17, Phys.W, Phys.H)) then
    m.ground = true                 -- resting on the floor, a pixel above it
    m.vy = 0
  end
end

-- inp: left, right, run, jump (booleans). One 50 Hz tick. plat (optional) tells where a platform is:
-- a floor from above only, as the ledges of SMW.
function Phys.step(m, inp, solid, plat)
  move(m, solid, plat)
  local dir = (inp.right and 1 or 0) - (inp.left and 1 or 0)
  if inp.down and m.ground then dir = 0 end      -- ducking: no push, the speed runs out
  if dir ~= 0 then m.face = dir end
  local fast = m.ground and inp.run and math.abs(m.vx) >= Phys.PM_START
  if fast then m.pm = math.min(Phys.PM_FULL, m.pm + Phys.PM_RATE)
  elseif m.ground then m.pm = math.max(0, m.pm - Phys.PM_DRAIN) end
  local cap = Phys.WALK
  if inp.run then cap = (m.pm >= Phys.PM_FULL) and Phys.PSPEED or Phys.RUN end
  if dir ~= 0 then
    if m.vx * dir < 0 then                       -- against the movement: the skid, on the ground and in the air
      m.vx = m.vx + dir * Phys.SKID
      if m.vx * dir > 0 then m.vx = 0 end
    elseif m.vx * dir < cap then
      m.vx = m.vx + dir * (m.ground and Phys.ACCEL or Phys.AIR_ACCEL)
      if m.vx * dir > cap then m.vx = dir * cap end
    elseif m.ground and m.vx * dir > cap then
      m.vx = m.vx - dir * Phys.DECEL
    end
  elseif m.ground then
    if m.vx > 0 then m.vx = math.max(0, m.vx - Phys.DECEL)
    elseif m.vx < 0 then m.vx = math.min(0, m.vx + Phys.DECEL) end
  end
  if m.ground then
    m.vy = 0
  else
    m.vy = m.vy + (inp.jump and Phys.GRAV_HOLD or Phys.GRAV_FREE)     -- the jump button held: a lighter gravity, up and down
    if m.vy > Phys.FALL_MAX then m.vy = Phys.FALL_MAX end
  end
  if inp.jump and not m.held and m.ground then
    m.vy = -(Phys.JUMP + Phys.JUMP_SPEED * math.abs(m.vx))
    m.ground = false
  end
  m.held = inp.jump
end
