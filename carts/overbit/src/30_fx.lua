-- Effects: particles (C points, z-tested), tracers and beams (C lines),
-- explosions with a flash of light, damage numbers, screen shake. Pools of
-- fixed size: nothing is allocated while playing.

Fx = { shake = 0, flash = 0, flash_rgb = 0xFFFFFF }

local MAXP = 360
local P = {}                  -- particles: x y z vx vy vz life age size rgb drag grav
for i = 1, MAXP do P[i] = { age = 1, life = 0 } end
local np = 0                  -- the live ones are P[1..np]

local MAXT = 48
local T = {}                  -- tracers and beams: from, to, colour, width, life
for i = 1, MAXT do T[i] = { age = 1, life = 0 } end
local nt = 0

local nums = {}               -- floating damage numbers (2D)
local lights = {}             -- short flashes of light (lamp3d 2..4)

-- the share of particles kept (the quality decides)
Fx.density = 1

local function spawn(x, y, z, vx, vy, vz, life, size, rgb, drag, grav, kind)
  if np >= MAXP then return nil end
  np = np + 1
  local p = P[np]
  p.x, p.y, p.z, p.vx, p.vy, p.vz = x, y, z, vx, vy, vz
  p.life, p.age, p.size, p.size0, p.rgb = life, 0, size, size, rgb
  p.drag, p.grav, p.kind = drag or 0, grav or 0, kind or 0
  return p
end
Fx.spawn = spawn

-- a burst of n particles from (x, y, z), speed s around (dx, dy, dz) + spread
function Fx.burst(x, y, z, n, s, spread, life, size, rgb, grav, dx, dy, dz)
  n = floor(n * Fx.density + 0.5)
  dx, dy, dz = dx or 0, dy or 0, dz or 0
  for _ = 1, n do
    local ux, uy, uz = random() * 2 - 1, random() * 2 - 1, random() * 2 - 1
    spawn(x, y, z, dx * s + ux * spread, dy * s + uy * spread, dz * s + uz * spread,
      life * (0.6 + random() * 0.6), size * (0.7 + random() * 0.6), rgb, 1.5, grav or 0)
  end
end

-- a flat ring of n particles flying out from (x, y, z) at `speed`
function Fx.ring(x, y, z, r, n, rgb, speed, life)
  n = floor(n * Fx.density + 0.5)
  for i = 1, n do
    local an = 2 * pi * i / n
    local c, s = cos(an), sin(an)
    spawn(x + c * r, y, z + s * r, c * speed, (random() - 0.5) * 0.6, s * speed, life or 0.4,
      0.22 + random() * 0.1, rgb, 2.5, 0)
  end
end

function Fx.tracer(x0, y0, z0, x1, y1, z1, rgb, width, life)
  if nt >= MAXT then return end
  nt = nt + 1
  local t = T[nt]
  t.x0, t.y0, t.z0, t.x1, t.y1, t.z1 = x0, y0, z0, x1, y1, z1
  t.rgb, t.w, t.life, t.age = rgb, width or 1, life or 0.06, 0
end

-- an explosion: fireball, sparks, smoke, light, shake
function Fx.explode(x, y, z, r, rgb)
  rgb = rgb or 0xFFB040
  Fx.burst(x, y, z, 10 * r, 0, 4 * r, 0.35, 0.35 * r, 0xFFF0C0, -1)
  Fx.burst(x, y, z, 14 * r, 0, 7 * r, 0.5, 0.25 * r, rgb, -2)
  Fx.burst(x, y, z, 8 * r, 0, 2 * r, 1.2, 0.5 * r, 0x504848, 1.5)
  Fx.light(x, y, z, 6 * r, 1.6, rgb, 0.3)
  local a = G.local_actor
  if a then
    local d = len3(a.x - x, a.y - y, a.z - z)
    Fx.shake = max(Fx.shake, clamp(1.2 * r - d * 0.05, 0, 1.2))
  end
end

function Fx.light(x, y, z, radius, k, rgb, life)
  for i = 1, 3 do
    local l = lights[i]
    if not l or l.age >= l.life then
      lights[i] = { x = x, y = y, z = z, r = radius, k = k, rgb = rgb, life = life, age = 0 }
      return
    end
  end
end

function Fx.number(x, y, z, n, crit, heal)
  if #nums > 24 then table.remove(nums, 1) end
  nums[#nums + 1] = { x = x, y = y, z = z, n = n, crit = crit, heal = heal, age = 0 }
end

function Fx.update()
  local i = 1
  while i <= np do
    local p = P[i]
    p.age = p.age + DT
    if p.age >= p.life then
      P[i], P[np] = P[np], P[i]                  -- swap with the last one alive
      np = np - 1
    else
      local k = 1 - p.drag * DT
      p.vx, p.vy, p.vz = p.vx * k, (p.vy - p.grav * 9.8 * DT) * k, p.vz * k
      p.x, p.y, p.z = p.x + p.vx * DT, p.y + p.vy * DT, p.z + p.vz * DT
      if p.y < 0.02 then p.y, p.vy = 0.02, -p.vy * 0.3 end
      i = i + 1
    end
  end
  i = 1
  while i <= nt do
    local t = T[i]
    t.age = t.age + DT
    if t.age >= t.life then
      T[i], T[nt] = T[nt], T[i]
      nt = nt - 1
    else
      i = i + 1
    end
  end
  for j = #nums, 1, -1 do
    local n = nums[j]
    n.age = n.age + DT
    n.y = n.y + DT * 0.8
    if n.age > 0.9 then table.remove(nums, j) end
  end
  for j = 1, 3 do
    local l = lights[j]
    if l then l.age = l.age + DT end
  end
  Fx.shake = max(0, Fx.shake - DT * 2.5)
  Fx.flash = max(0, Fx.flash - DT * 3)
end

-- the lamps of the flashes (before drawing the scene)
function Fx.lamps()
  for j = 1, 3 do
    local l = lights[j]
    if l and l.age < l.life then
      local k = 1 - l.age / l.life
      lamp3d(j + 1, l.x, l.y, l.z, l.r, l.k * k, l.rgb)
    else
      lamp3d(j + 1)
    end
  end
end

local function fade(rgb, k)
  local r, g, b = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
  return (floor(r * k) << 16) | (floor(g * k) << 8) | floor(b * k)
end
Fx.fade = fade

function Fx.draw()
  local cx, cy, cz = Cam.x, Cam.y, Cam.z
  for i = 1, np do
    local p = P[i]
    local dx, dy, dz = p.x - cx, p.y - cy, p.z - cz
    if dx * dx + dy * dy + dz * dz > 0.36 then         -- not at the eye (a disc over the screen)
      local u = p.age / p.life
      local rgb = p.rgb
      if u > 0.6 then rgb = fade(rgb, 1 - (u - 0.6) * 2) end
      point3d(p.x, p.y, p.z, p.size * (1 - u * 0.5), rgb, u > 0.5 and 1 or 0)
    end
  end
  for i = 1, nt do
    local t = T[i]
    line3d(t.x0, t.y0, t.z0, t.x1, t.y1, t.z1, t.rgb, t.w)
  end
end

-- after the 3D: numbers over the hits
function Fx.draw2d()
  for _, n in ipairs(nums) do
    local sx, sy = project3d(n.x, n.y, n.z)
    if sx then
      local s = tostring(n.n)
      local c = n.heal and 0x60FF90 or (n.crit and 0xFF4040 or 0xFFFFFF)
      if n.age > 0.6 then c = fade(c, 1 - (n.age - 0.6) / 0.3) end
      print(s, floor(sx) - #s * 3, floor(sy) - 6, c)
    end
  end
end

function Fx.clear()
  np, nt = 0, 0
  nums = {}
  lights = {}
  lamp3d()
end
