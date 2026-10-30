-- Island Bench: a 3D and 2D graphics benchmark on the island of the map kit (assets/island.bmk).
-- Two ways to use it:
--   Manual:    change the options one by one and read the numbers (like Hero Stress and the 3D Bench).
--   Benchmark: Start + A (F5) runs a fixed list of scenes (SCENES below), one after the other, with a fixed
--              camera, and prints a table and a score. Edit SCENES to change the test.
-- The island (map, models, sprites) comes from the kit: tools/bmk, `bmres.py add` puts it in the cartridge.
-- The logic runs at 60 a second whatever the speed, so two runs draw the same frames.

local O = {}                   -- the options in force: key -> index (or value)
local G = {                    -- the state
  frame = 0, cur = 1, panel = true, cam = 1,
  list = {}, nbase = 0,        -- the objects of the island (and the extra ones for more than 100%)
  last = { ms = 0, max = 0, fps = 0, gpu = 0, wait = 0, tri = 0, vtx = 0, lua = 0, kb = 0, skip = 0 },
  lines = {}, seconds = 0,
}
local SKY = 0x8CC8F0
local PI = math.pi
local CX, CZ = 36.5, 36.5      -- the campfire, in the middle of the island (72 x 72 tiles of 1 unit)
local MAX_SKIP = 4
local ROWS

local function fmt(v, d) return string.format("%." .. (d or 1) .. "f", v) end

-- ---------------------------------------------------------------- the kit (island.bmk)
-- The object ids on the objects layer of the kit, with the model (textured, flat) and the 2D sprite of each.
-- `fixed`: always drawn, whatever the Elements percentage. `sky`: a cloud (raised and moved).
local KIT = {
  [1040] = { m = "palm",     f = "palm_f",     s = "palm2d",  cls = "palm" },
  [1042] = { m = "bush",     f = "bush_f",     s = "bush2d",  cls = "bush" },
  [1044] = { m = "rock",     f = "rock_f",     s = "rock2d",  cls = "rock" },
  [1046] = { m = "chest",    f = "chest_f",    s = "chest2d", cls = "chest",  fixed = true },
  [1048] = { m = "boat",     f = "boat_f",     s = "boat2d",  cls = "boat",   fixed = true },
  [1050] = { m = "campfire", f = "campfire_f", s = "campfire2d", cls = "fire", fixed = true },
  [1052] = { m = "grass",    f = "grass_f",    s = "grass2d", cls = "grass" },
  [1054] = { m = "cloud",    f = "cloud_f",    s = "cloud2d", cls = "cloud",  sky = true },
  [1056] = { m = "palm2",    f = "palm2_f",    s = "palm2d",  cls = "palm" },
  [1058] = { m = "palm3",    f = "palm3_f",    s = "palm2d",  cls = "palm" },
}
local EXTRA = { 1040, 1042, 1044, 1052, 1056, 1052, 1058, 1042 }   -- what the extra elements (over 100%) are

local seed = 12345
local function rnd()                      -- a fixed sequence: two runs place the same things
  seed = (seed * 1103515245 + 12345) % 2147483648
  return seed / 2147483648
end

local world
local ZONE = {}                           -- sprite name -> { x, y, w, h, frames, fps }

-- A tile of water far from the land has no floor (floor id 0: map3d makes no triangles for it): the sea model is
-- there. The map of the kit has 5184 tiles of 2 triangles; 3173 are water. RING tiles of water stay round the land.
local WATER, RING = 2, 2
local function thin_water()
  local nt = 72
  local land = {}
  for tz = 0, nt - 1 do
    for tx = 0, nt - 1 do
      local id = mget(tx * 2, tz * 2, "floor")
      land[tz * nt + tx + 1] = id ~= WATER and id ~= 0
    end
  end
  local cut = 0
  for tz = 0, nt - 1 do
    for tx = 0, nt - 1 do
      local near = false
      for dz = -RING, RING do
        for dx = -RING, RING do
          local x, z = tx + dx, tz + dz
          if x >= 0 and z >= 0 and x < nt and z < nt and land[z * nt + x + 1] then near = true end
        end
      end
      if not near then mset(tx * 2, tz * 2, 0, "floor"); cut = cut + 1 end
    end
  end
  return cut
end

local function island_init()
  G.cut_tiles = thin_water()
  world = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25 })
  for _, n in ipairs(zones()) do
    local x, y, w, h, fr, fps = zone(n)
    ZONE[n] = { x, y, w, h, fr or 1, fps or 0 }
  end
  local base = {}
  local mw, mh = msize()
  for cz = 0, mh - 1, 2 do
    for cx = 0, mw - 1, 2 do
      local id = mget(cx, cz, "objects")
      local k = KIT[id]
      if k then
        local x, z = cx / 2 + 0.5, cz / 2 + 0.5
        local o = { k = k, x = x, z = z, y = map3d_height(world, x, z) or 0, ry = rnd() * 2 * PI }
        if k.fixed then
          G.list[#G.list + 1] = o
        else
          base[#base + 1] = o
        end
      end
    end
  end
  -- the others in a fixed shuffled order: a lower percentage takes away from everywhere, not from one side
  for i = #base, 2, -1 do
    local j = 1 + math.floor(rnd() * i)
    base[i], base[j] = base[j], base[i]
  end
  G.nfixed = #G.list
  for _, o in ipairs(base) do G.list[#G.list + 1] = o end
  G.nbase = #base
  -- extra elements on dry land, away from the fire: up to 100% more
  local n, tries = 0, 0
  while n < #base and tries < 4000 do
    tries = tries + 1
    local x, z = 8 + rnd() * 56, 8 + rnd() * 56
    local y = map3d_height(world, x, z)
    local dx, dz = x - CX, z - CZ
    if y and y >= 0.5 and dx * dx + dz * dz > 4 then
      n = n + 1
      G.list[#G.list + 1] = { k = KIT[EXTRA[(n - 1) % #EXTRA + 1]], x = x, z = z, y = y, ry = rnd() * 2 * PI }
    end
  end
  G.ntotal = #G.list
end

-- ---------------------------------------------------------------- the options
local function choice(key, name, labels, init, help)
  local r = { key = key, name = name, labels = labels, help = help }
  O[key] = init
  function r.get() return labels[O[key]] end
  function r.step(d) O[key] = (O[key] - 1 + d) % #labels + 1 end
  function r.toggle() O[key] = O[key] == 1 and #labels or 1 end
  return r
end

local RES = { "320x180", "384x216", "480x270", "640x360", "960x540", "1280x720", "1920x1080" }

local function gpu_state()
  local on, aa, vs, ver, q, imp, later, probe = gpu3d()
  return on, aa, vs, ver, q, imp, later, probe or 0
end

local function build_rows()
  local mode = choice("mode", "Mode", { "Manual", "Benchmark" }, 1, { "Benchmark runs the SCENES list.", "Start + A or F5 starts it." })
  function mode.toggle() G.bench_start() end
  function mode.step() G.bench_start() end
  local el = { key = "el", name = "Elements", help = { "Palms, bushes, rocks and grass on the island.", "100% = the map (135); over: more of them." } }
  O.el = 50                                 -- the light settings first: the player raises them
  function el.get() return O.el .. "% (" .. G.shown() .. ")" end
  function el.step(d) O.el = math.max(0, math.min(200, O.el + d * 10)) end
  function el.toggle() O.el = O.el == 0 and 50 or 0 end
  local backend = { key = "backend", name = "Backend", help = { "ARM or GPU. The Mali (RGB30) takes opaque", "textures and grey light only: it falls to ARM." } }
  function backend.get() return (gpu_state()) and "GPU" or "ARM" end
  function backend.step() gpu3d(not (gpu_state())) end
  backend.toggle = backend.step
  local aa = { key = "aa", name = "AA", help = { "4x MSAA, GPU only.", "Shadows on the GPU: not with AA." } }
  function aa.get() local on, a = gpu_state(); return (a and "4x" or "off") .. (on and "" or " (ARM: ignored)") end
  function aa.step() local _, a = gpu_state(); gpu3d(nil, not a) end
  aa.toggle = aa.step
  local res = { key = "res", name = "Resolution", help = { "screen() mode. 1920x1080: use Backend GPU.", "" } }
  function res.get() return SCREEN_W .. "x" .. SCREEN_H end
  function res.step(d)
    local i = 4
    for k, v in ipairs(RES) do if v == SCREEN_W .. "x" .. SCREEN_H then i = k end end
    i = (i - 1 + d) % #RES + 1
    local w, h = RES[i]:match("(%d+)x(%d+)")
    screen(tonumber(w), tonumber(h))
  end
  res.toggle = function() res.step(1) end
  local cam = { key = "cam", name = "Camera", help = { "Select changes it too.", "Free: left stick moves, right stick looks." } }
  function cam.get() return G.CAMS[G.cam] end
  function cam.step(d) G.cam = (G.cam - 1 + d) % #G.CAMS + 1 end
  cam.toggle = function() cam.step(1) end

  ROWS = {
    mode, el,
    choice("time", "Time", { "Day (sun)", "Night (moon)" }, 1, { "Day: sun light. Night: moon light, stars,", "dark ambient: the fire and the lamps count." }),
    choice("weather", "Weather", { "Clear", "Rain", "Snow", "Fog" }, 1, { "Rain and snow: particles round the camera.", "Fog: fog3d. All: lighter or darker sky." }),
    choice("amount", "Weather amount", { "50%", "100%", "200%" }, 2, { "Particles of rain or snow a frame.", "tri stays; point work rises." }),
    choice("angle", "Light angle", { "low", "mid", "high" }, 2, { "Height of the sun or moon: low gives", "long shadows." }),
    choice("fire", "Campfire", { "off", "on" }, 1, { "The fire: a point light (lamp3d), flames", "(2D sprite in the 3D) and sparks." }),
    choice("shadow", "Shadows", { "off", "dark", "dither" }, 1, { "draw3d flag 8 under palms, rocks, chest, boat.", "dark = shadow3d(0), dither = shadow3d(1)." }),
    choice("look", "Look", { "Texture", "Flat" }, 1, { "Textured models, or the flat colour ones", "(_f) with textures3d(false)." }),
    choice("objects", "Objects as", { "3D models", "2D sprites", "Mix by distance" }, 3, { "sprite3d shows the 2D sprite of each object.", "Mix: 3D near, 2D far (as in Doom)." }),
    choice("sea", "Sea", { "off", "on" }, 1, { "The sea: one model of 1152 triangles.", "" }),
    choice("light", "Lighting", { "none", "flat", "smooth" }, 2, { "none, flat or smooth (per vertex) light.", "" }),
    backend, aa, res, cam,
  }
  G.ROW = {}
  ROWS[#ROWS + 1] = choice("guard", "Block under 30 fps", { "off", "on" }, 2,
    { "A change that takes the frame rate under 30 fps", "freezes the options; 1 s later the last good ones return." })
  for i, r in ipairs(ROWS) do G.ROW[r.key] = i end
end

G.CAMS = { "orbit", "overview", "close", "free" }

-- The rows in groups: a group is one line that opens or closes with A (or Left, Right, or its digit key).
local GROUPS = {
  { name = "Test",        keys = { "mode", "el" },                          open = true },
  { name = "Environment", keys = { "time", "weather", "amount", "angle" },  open = true },
  { name = "Light",       keys = { "fire", "shadow" },                      open = false },
  { name = "Look",        keys = { "look", "objects", "sea", "light" },     open = false },
  { name = "Screen",      keys = { "backend", "aa", "res", "cam", "guard" },         open = false },
}
local VIS = {}                                -- what the panel shows now: the group lines and the rows of the open groups

local function build_vis()
  local key = VIS[G.cur] and (VIS[G.cur].hdr and VIS[G.cur].name or VIS[G.cur].key)
  VIS = {}
  for gi, g in ipairs(GROUPS) do
    VIS[#VIS + 1] = { hdr = true, name = g.name, g = g, n = gi }
    if g.open then
      for _, k in ipairs(g.keys) do VIS[#VIS + 1] = ROWS[G.ROW[k]] end
    end
  end
  for i, r in ipairs(VIS) do                 -- the cursor stays on the same line when it can
    if key and (r.hdr and r.name or r.key) == key then G.cur = i end
  end
  G.cur = math.max(1, math.min(G.cur, #VIS))
end

function G.shown()
  local n = math.floor(G.nbase * O.el / 100 + 0.5)
  return math.min(n, G.ntotal - G.nfixed)
end

-- ---------------------------------------------------------------- the scenes of the benchmark
-- One line a scene: the options it sets (the others stay as they are in the SCENES base below).
-- Each scene runs WARM frames (not counted) and then MEASURE frames; the median of the frame time is its result.
local WARM, MEASURE = 30, 180
local BASE = { el = 100, time = 1, weather = 1, amount = 2, angle = 2, fire = 2, shadow = 1, look = 1, objects = 1,
               sea = 2, light = 3, cam = 1 }
local SCENES = {
  { "day clear",             {} },
  { "day clear 200%",        { el = 200 } },
  { "day clear no sea",      { sea = 1, el = 0 } },
  { "day shadows",           { shadow = 2 } },
  { "day shadows 200%",      { shadow = 2, el = 200 } },
  { "day rain",              { weather = 2 } },
  { "day rain 200%",         { weather = 2, amount = 3, el = 200 } },
  { "day snow",              { weather = 3 } },
  { "day fog",               { weather = 4 } },
  { "night",                 { time = 2, shadow = 2 } },
  { "night rain",            { time = 2, weather = 2, shadow = 2 } },
  { "night snow fog",        { time = 2, weather = 3, shadow = 2 } },
  { "flat colours",          { look = 2 } },
  { "2D sprites",            { objects = 2 } },
  { "2D sprites 200%",       { objects = 2, el = 200 } },
  { "mix 3D and 2D",         { objects = 3, el = 200 } },
}
G.SCENES = SCENES

-- ---------------------------------------------------------------- the weather and the light
local WCOL = {      -- sky colour [time][weather]
  { 0x8CC8F0, 0x7A8696, 0xB8C4D0, 0xC8D0D8 },
  { 0x0A1030, 0x10141C, 0x1C2230, 0x181C28 },
}
local ENVSIG
local STARS = {}
for i = 1, 90 do
  local a, b = rnd() * 2 * PI, 0.15 + rnd() * 1.2
  STARS[i] = { math.cos(a) * math.cos(b), math.sin(b), math.sin(a) * math.cos(b), rnd() }
end
local LDIR = { { -0.4, 0.35, -0.5 }, { -0.4, 0.8, -0.5 }, { -0.25, 1.2, -0.3 } }

-- The Mali of the RGB30 draws flat colours and opaque textures only. A point3d, a line3d, a sprite3d, a shadow (draw3d
-- flag 8) or a particle hands the 3D of the cartridge to the ARM, for good until it restarts (r3d.c, colour_ok). So with
-- the GPU on a Mali the effects are not drawn: the GPU keeps the frame, and the ARM backend has them.
local function fx_on()
  if G.mali == nil then
    local st = gpu3d_stats and gpu3d_stats() or {}
    G.mali = tostring(st.status):find("Mali") ~= nil
  end
  return not (G.mali and (gpu_state()))
end

-- With the Mali the light must have no colour ("light in colour" hands the 3D to the ARM): the greys of the colours.
local function grey(c)
  local r, g, b = (c >> 16) & 255, (c >> 8) & 255, c & 255
  local k = math.floor(r * 0.30 + g * 0.59 + b * 0.11)
  return k << 16 | k << 8 | k
end

local function apply_env()
  local mono = not fx_on()
  local sig = O.time * 1000 + O.weather * 100 + O.angle * 10 + O.look + (mono and 10000 or 0)
  if sig == ENVSIG then return end
  ENVSIG = sig
  local sky3d_c = sky3d
  local sky3d = function(a, b, c) if mono then sky3d_c(grey(a), grey(b), grey(c)) else sky3d_c(a, b, c) end end
  local night, w = O.time == 2, O.weather
  SKY = WCOL[O.time][w]
  local d = LDIR[O.angle]
  if night then
    -- the moon: a cold, weak light; the ambient is low so the fire and the stars show
    local m = ({ 0x8090D0, 0x586080, 0x6C7898, 0x505870 })[w]
    sky3d(m, 0x182038, 0x080C18)
    shine3d(0, 16, 0)
    light3d(-d[1], d[2], -d[3], ({ 0.16, 0.12, 0.16, 0.12 })[w])
  else
    local s = ({ 0xFFF0D8, 0xA0A8B4, 0xD8DCE4, 0xC8CCD0 })[w]
    sky3d(s, ({ 0x8CB4FF, 0x707C8C, 0xA0ACC0, 0xB0B8C4 })[w], 0x504838)
    shine3d(0, 16, 0)
    light3d(d[1], d[2], d[3], ({ 0.45, 0.4, 0.5, 0.5 })[w])
  end
  if w == 4 then fog3d(SKY, 4, night and 18 or 24)
  elseif w == 1 then fog3d()
  else fog3d(SKY, 16, 56) end
  textures3d(O.look == 1)
end

local function flicker(t)
  return 0.85 + 0.15 * math.sin(t * 17) * math.sin(t * 5.3 + 1) + 0.05 * math.sin(t * 41)
end

-- rain and snow: particles round the camera
local function weather_update()
  local w = O.weather
  if not fx_on() or (w ~= 2 and w ~= 3) then
    if G.had_particles then particles(2048); G.had_particles = false end   -- (a new pool: empty)
    return
  end
  G.had_particles = true
  local n = ({ 12, 24, 48 })[O.amount]
  if w == 3 then n = n // 2 end
  local cx, cy, cz = G.cx, G.cy, G.cz
  for _ = 1, n do
    local x = cx + (rnd() - 0.5) * 30
    local z = cz + (rnd() - 0.5) * 30
    local y = cy + 4 + rnd() * 8
    if w == 2 then
      particles_add(x, y, z, 0.03, -0.4, 0.02, 30, O.time == 2 and 0x8098C0 or 0xA8C0E8)
    else
      particles_add(x, y, z, (rnd() - 0.5) * 0.03, -0.05, (rnd() - 0.5) * 0.03, 240, O.time == 2 and 0xB0B8D0 or 0xFFFFFF)
    end
  end
  particles_step(0)
end

-- ---------------------------------------------------------------- the camera
local function look(x, y, z, tx, ty, tz)
  local dx, dy, dz = tx - x, ty - y, tz - z
  return math.atan(dx, dz), math.atan(dy, math.sqrt(dx * dx + dz * dz))
end

local fx, fy, fz, fyaw, fpitch = CX, 6, CZ - 14, 0, -0.3
function G.camera_update(t)
  local x, y, z, yaw, pitch
  local c = G.bench and 1 or G.cam
  if c == 1 then
    local a = t * 2 * PI / 45
    x, y, z = CX + 15 * math.sin(a), 7.5, CZ + 15 * math.cos(a)
    yaw, pitch = look(x, y, z, CX, 1.2, CZ)
  elseif c == 2 then
    x, y, z = CX, 26, CZ - 34
    yaw, pitch = look(x, y, z, CX, 0, CZ)
  elseif c == 3 then
    local a = t * 2 * PI / 30
    x, y, z = CX + 5 * math.sin(a), 3, CZ + 5 * math.cos(a)
    yaw, pitch = look(x, y, z, CX, 0.8, CZ)
  else
    local sx, sy = stick()
    local rx, ry = stick(nil, 1)
    fyaw = fyaw + rx * 0.03
    fpitch = math.max(-1.4, math.min(1.4, fpitch - ry * 0.03))
    fx = fx + (math.sin(fyaw) * -sy + math.cos(fyaw) * sx) * 0.12
    fz = fz + (math.cos(fyaw) * -sy - math.sin(fyaw) * sx) * 0.12
    x, y, z, yaw, pitch = fx, fy, fz, fyaw, fpitch
  end
  G.cx, G.cy, G.cz, G.cyaw, G.cpitch = x, y, z, yaw, pitch
end

-- ---------------------------------------------------------------- the drawing of the island
local FIRE_FRAMES
local drawn3, drawn2 = 0, 0

local function sprite_of(k, x, y, z, t)
  local zn = ZONE[k.s]
  if not zn then return false end
  local fr = zn[5]
  local f = 0
  if fr > 1 and zn[6] > 0 then f = math.floor(t * zn[6]) % fr end
  local w, h = zn[3], zn[4]
  sprite3d(zn[1] + f * w, zn[2], w, h, x, y + h / 32, z, w / 16)
  return true
end

local function draw_objects(t)
  local flags = ({ 2, 0, 4 })[O.light]
  local look_flat = O.look == 2
  local mode = O.objects
  local fx = fx_on()
  local shadows = O.shadow > 1 and fx
  local n = G.nfixed + G.shown()
  local cx, cz = G.cx, G.cz
  drawn3, drawn2 = 0, 0
  for i = 1, G.ntotal do
    -- the fixed ones first, then the first G.shown() of the others, then the extra ones (the list is in that order)
    local o = G.list[i]
    local k = o.k
    local on = i <= G.nfixed or (i - G.nfixed) <= G.shown()
    if on and (O.el > 0 or k.fixed) then
      local x, y, z = o.x, o.y, o.z
      if k.sky then
        x = (x + t * 0.4) % 72
        y = 9 + 0.5 * math.sin(t * 0.3 + i)
      end
      if visible3d(x, y + 0.5, z, k.sky and 4 or 2) then
        local dx, dz = x - cx, z - cz
        local d2 = dx * dx + dz * dz
        local as2d = fx and (mode == 2 or (mode == 3 and d2 > 144))
        if as2d and ZONE[k.s] then
          drawn2 = drawn2 + 1
          sprite_of(k, x, y, z, t)
        else
          local m = model(look_flat and k.f or k.m)
          if m then
            drawn3 = drawn3 + 1
            local sc = k.sky and 1.6 or 1
            draw3d(m, x, y, z, 0, o.ry, 0, sc, flags)
            if shadows and not k.sky and d2 < 400 then draw3d(m, x, y, z, 0, o.ry, 0, sc, 8) end
          end
        end
      end
    end
  end
end

local function draw_sky()
  if not fx_on() then return end
  local c = G.cx
  local d = LDIR[O.angle]
  local night = O.time == 2
  -- the sun (a warm disc) or the moon (a pale one), where the light comes from
  local len = math.sqrt(d[1] * d[1] + d[2] * d[2] + d[3] * d[3])
  local sx, sy, sz = G.cx - d[1] / len * 70, G.cy + d[2] / len * 70, G.cz - d[3] / len * 70
  if night then
    point3d(G.cx + d[1] / len * 70 * -1, sy, G.cz - d[3] / len * 70, 4, 0xE8ECFF)
    if O.weather == 1 or O.weather == 3 then
      for i = 1, #STARS do
        local s = STARS[i]
        point3d(G.cx + s[1] * 75, G.cy + s[2] * 75, G.cz + s[3] * 75, 0.25 + s[4] * 0.2, s[4] > 0.7 and 0xFFFFFF or 0xA0A8C0)
      end
    end
  elseif O.weather == 1 then
    point3d(sx, sy, sz, 5, 0xFFF4C0)
  end
end

local function draw_fire(t)
  if O.fire == 1 then lamp3d(1); return end
  local y = (map3d_height(world, CX, CZ) or 2) + 0.2
  local f = flicker(t)
  lamp3d(1, CX, y + 0.8, CZ, 9, 1.6 * f, 0xFF9040)
  -- flames: the 2D animation of the campfire in the 3D (sprite3d, frames by time)
  if not fx_on() then return end
  local zn = ZONE.campfire2d
  if zn then
    local fr = zn[5]
    local fi = math.floor(t * 8) % fr
    sprite3d(zn[1] + fi * zn[3], zn[2], zn[3], zn[4], CX, y + 0.7, CZ, 1.1 * f)
  end
  -- sparks
  for i = 1, 10 do
    local u = (t * 0.9 + i * 0.173) % 1
    local a = i * 2.399 + math.floor(t * 0.9 + i * 0.173) * 1.7
    point3d(CX + math.sin(a) * 0.35 * u, y + 0.7 + u * 1.8, CZ + math.cos(a) * 0.35 * u, 0.07 * (1 - u) + 0.02,
            u < 0.5 and 0xFFD060 or 0xFF7030)
  end
end

-- ---------------------------------------------------------------- the numbers
local W = { n = 0, ms = 0, max = 0, gpu = 0, wait = 0, tri = 0, vtx = 0, lua = 0, skip = 0 }
local t0 = 0
local B = nil                                       -- the benchmark in progress

local function add_line(s)
  log(s)
  local L = G.lines
  L[#L + 1] = s
  if #L > 160 then table.remove(L, 1) end
end

local function median(t)
  local c = { table.unpack(t) }
  table.sort(c)
  return c[(#c + 1) // 2] or 0
end

function G.opt_string()
  local on, a, v = gpu_state()
  return table.concat({ on and "gpu" or "arm", "aa" .. (a and 1 or 0), "vs" .. (v and 1 or 0),
    "t" .. O.time, "w" .. O.weather, "el" .. O.el, "shd" .. (O.shadow - 1), O.look == 1 and "tex" or "flat",
    "obj" .. O.objects, "fire" .. (O.fire - 1), "sea" .. (O.sea - 1) }, ",")
end

local function bench_scene()
  local sc = SCENES[B.i]
  for k, v in pairs(BASE) do if k ~= "cam" then O[k] = v end end
  for k, v in pairs(sc[2]) do O[k] = v end
  B.phase, B.f, B.ms, B.gpu, B.tri, B.max = "warm", 0, {}, 0, 0, 0
  ENVSIG = nil
  G.frame0 = G.frame
end

function G.bench_start()
  if B then return end
  B = { i = 0, res = {}, saved = {} }
  for k, v in pairs(O) do B.saved[k] = v end
  B.saved_cam = G.cam
  G.bench_note = nil
  add_line("bench start scenes=" .. #SCENES .. " res=" .. SCREEN_W .. "x" .. SCREEN_H)
  B.i = 1
  bench_scene()
end

function G.benching() return B ~= nil end

local function bench_done()
  local sum, n = 0, 0
  for _, r in ipairs(B.res) do sum = sum + 1000 / math.max(r.ms, 0.1); n = n + 1 end
  local score = n > 0 and sum / n or 0
  add_line(string.format("bench done score=%s (mean of 1000/ms over %d scenes) res=%dx%d", fmt(score), n, SCREEN_W, SCREEN_H))
  G.bench_result = B.res
  G.bench_note = string.format("benchmark done: score %s", fmt(score))
  for k, v in pairs(B.saved) do O[k] = v end
  G.cam = B.saved_cam
  ENVSIG = nil
  B = nil
  G.save()
end

function G.stats_frame()
  local ms, gpu, wait = stat(1), stat(16), stat(18)
  G.guard_frame(ms)
  local tri, vtx, lua = stat(4), stat(7), stat(10)
  W.n = W.n + 1
  W.skip = W.skip + math.max(0, stat(15) - 1)
  W.ms, W.gpu, W.wait = W.ms + ms, W.gpu + gpu, W.wait + wait
  W.tri, W.vtx, W.lua = W.tri + tri, W.vtx + vtx, W.lua + lua
  if ms > W.max then W.max = ms end
  if B then
    B.f = B.f + 1
    if B.phase == "warm" then
      if B.f >= WARM then B.phase, B.f = "measure", 0 end
    else
      B.ms[B.f], B.gpu, B.tri = ms, B.gpu + gpu, B.tri + tri
      if ms > B.max then B.max = ms end
      if B.f >= MEASURE then
        local r = { name = SCENES[B.i][1], ms = median(B.ms), max = B.max, gpu = B.gpu / MEASURE, tri = B.tri / MEASURE }
        B.res[#B.res + 1] = r
        add_line(string.format("bench %d %s ms=%s max=%s gpu=%s tri=%d", B.i, r.name, fmt(r.ms), fmt(r.max), fmt(r.gpu), math.floor(r.tri)))
        B.i = B.i + 1
        if B.i > #SCENES then bench_done() else bench_scene() end
      end
    end
  end
  local now = time()
  if now - t0 >= 1 then
    local n = math.max(1, W.n)
    local L = G.last
    L.ms, L.max, L.gpu, L.wait = W.ms / n, W.max, W.gpu / n, W.wait / n
    L.tri, L.vtx, L.lua, L.fps = W.tri / n, W.vtx / n, W.lua / n, stat(2)
    L.kb, L.skip = stat(0), W.skip
    G.seconds = G.seconds + 1
    log(string.format("ikeys f=%d row=%d", G.frame, G.cur))
    add_line(string.format(
      "islandbench 1 t=%d scene=%s res=%dx%d opt=%s | ms=%s max=%s fps=%d gpu=%s wait=%s tri=%d vtx=%d lua=%d kb=%d skip=%d o3=%d o2=%d",
      math.floor(now), B and SCENES[B.i][1]:gsub(" ", "_") or "manual", SCREEN_W, SCREEN_H, G.opt_string(), fmt(L.ms), fmt(L.max),
      math.floor(L.fps), fmt(L.gpu), fmt(L.wait), math.floor(L.tri), math.floor(L.vtx), math.floor(L.lua / 1000), math.floor(L.kb), L.skip,
      drawn3, drawn2))
    W = { n = 0, ms = 0, max = 0, gpu = 0, wait = 0, tri = 0, vtx = 0, lua = 0, skip = 0 }
    t0 = now
  end
end

function G.save()
  local head = string.format("islandbench 1 saved t=%d res=%dx%d\n", math.floor(time()), SCREEN_W, SCREEN_H)
  local gs = gpu3d_stats and gpu3d_stats() or {}
  head = head .. "gpu: " .. tostring(gs.status) .. "\noptions: " .. tostring(gs.options) .. "\nprobes: " .. tostring(gs.probes) .. "\n"
  local text = head .. table.concat(G.lines, "\n") .. "\n"
  local ok = doc_write("islandbench.txt", text)
  report("islandbench", text)
  G.save_note = ok and "saved islandbench.txt" or "doc_write failed"
  G.save_t = time()
end

-- ---------------------------------------------------------------- the keys
local rep = {}
local REPEAT_AFTER = 10 * MAX_SKIP
local function press(name)
  if btn(name) then
    local c = (rep[name] or 0) + 1
    rep[name] = c
    return c == 1 or (c > REPEAT_AFTER and c % 4 == 0)
  end
  rep[name] = 0
  return false
end
local keys = {}
local function key_edge(u)
  local d = keydown(u)
  local e = d and not keys[u]
  keys[u] = d
  return e
end

-- The 30 fps guard. A change of an option (or of the backend, AA, resolution) is watched: after SETTLE frames the
-- mean frame time of WINDOW frames must stay under 33.4 ms. If not, the options freeze for one second and then the
-- last good ones come back (G.good). A benchmark scene or a run with the guard off is not watched.
local SETTLE, WINDOW, LIMIT_MS = 10, 20, 1000 / 30
local function sig()
  local on, aa = gpu_state()
  local t = {}
  for k, v in pairs(O) do t[#t + 1] = k .. v end
  table.sort(t)
  return table.concat(t, ",") .. (on and "g" or "a") .. (aa and "q" or "n") .. SCREEN_W .. "x" .. SCREEN_H .. G.cam
end
local function snapshot()
  local on, aa = gpu_state()
  local o = {}
  for k, v in pairs(O) do o[k] = v end
  return { o = o, on = on, aa = aa, w = SCREEN_W, h = SCREEN_H, cam = G.cam }
end
local function restore(sn)
  for k, v in pairs(sn.o) do O[k] = v end
  G.cam = sn.cam
  gpu3d(sn.on, sn.aa)
  if SCREEN_W ~= sn.w or SCREEN_H ~= sn.h then screen(sn.w, sn.h) end
  ENVSIG = nil
end

function G.guard_frame(ms)
  if B or O.guard ~= 2 then G.pend, G.sig = nil, nil; return end
  if G.frozen_until then
    if time() >= G.frozen_until then
      restore(G.good)
      G.frozen_until, G.pend, G.sig = nil, nil, nil
      G.bench_note = nil
    end
    return
  end
  local sg = sig()
  if sg ~= G.sig then G.sig = sg; G.pend = { n = 0, ms = 0 } end
  local p = G.pend
  if not p then return end
  p.n = p.n + 1
  if p.n > SETTLE then p.ms = p.ms + ms end
  if p.n >= SETTLE + WINDOW then
    if p.ms / WINDOW > LIMIT_MS + 0.4 and G.good and G.good_sig ~= sg then
      G.frozen_until = time() + 1
      G.bench_note = "under 30 fps: options frozen, the last good ones come back"
      add_line(string.format("guard: %s ms over %d frames, back to the last good options", fmt(p.ms / WINDOW), WINDOW))
    else
      G.good, G.good_sig = snapshot(), sg       -- (a state that is slow but is the last good one stays: nothing better)
    end
    G.pend = nil
  end
end

function G.input()
  local start = btn("start")
  if G.frozen_until then return end
  if B then
    if btnp("select") or btnp("b") or key_edge(0x29) then          -- abort: Select, B or Esc
      for k, v in pairs(B.saved) do O[k] = v end
      G.cam = B.saved_cam
      ENVSIG = nil
      B = nil
      G.bench_note = "benchmark stopped"
    end
    return
  end
  if #VIS == 0 then build_vis() end
  if press("up") then G.cur = (G.cur - 2) % #VIS + 1 end
  if press("down") then G.cur = G.cur % #VIS + 1 end
  local row = VIS[G.cur]
  if row.hdr then
    if press("left") or press("right") or (not start and btnp("a")) then row.g.open = not row.g.open; build_vis() end
  else
    if press("left") then row.step(-1) end
    if press("right") then row.step(1) end
    if not start and btnp("a") then row.toggle() end
  end
  if press("l1") then ROWS[G.ROW.el].step(-1) end
  if press("r1") then ROWS[G.ROW.el].step(1) end
  if btnp("x") then ROWS[G.ROW.time].step(1) end
  if btnp("y") then ROWS[G.ROW.weather].step(1) end
  if btnp("select") then G.cam = G.cam % #G.CAMS + 1 end
  if btnp("b") or key_edge(0x0B) then G.panel = not G.panel end
  for k = 1, #GROUPS do                      -- the digits open and close the groups
    if key_edge(0x1D + k) then GROUPS[k].open = not GROUPS[k].open; build_vis() end
  end
  if start then
    G.start_held = true
    if btnp("a") then G.bench_start(); G.start_used = true end
  elseif G.start_held then
    G.start_held = false
    if not G.start_used then G.save() end
    G.start_used = false
  end
  if key_edge(0x3E) then G.bench_start() end            -- F5
  if key_edge(0x3F) then G.save() end                   -- F6
end

-- ---------------------------------------------------------------- the panel
local TEXT, CUR, DIM, NUM = 0xE0E0E0, 0xFFE060, 0x707880, 0x90E0A0

local function panel_draw()
  if not G.panel then return end
  local Wd, Hh = SCREEN_W, SCREEN_H
  font("6x12")
  local L = G.last
  if B then
    rectfill(0, 0, 232, 12 * (#SCENES + 4) + 6, 0x101820)
    print("BENCHMARK  " .. B.i .. "/" .. #SCENES .. "  (Select stops)", 4, 4, CUR)
    for i, s in ipairs(SCENES) do
      local r = B.res[i]
      local c = i == B.i and CUR or (r and TEXT or DIM)
      print(string.format("%-18s %s", s[1], r and (fmt(r.ms) .. " ms") or (i == B.i and (B.phase == "warm" and "warm" or "...") or "")), 4, 4 + 12 * i, c)
    end
  elseif Hh >= 270 then
    if #VIS == 0 then build_vis() end
    local help = Hh >= 330 and not VIS[G.cur].hdr and VIS[G.cur].help
    rectfill(0, 0, 268, 12 * (#VIS + (help and 4 or 2)) + 6, 0x101820)
    local on = gpu_state()
    local gst = gpu3d_stats and gpu3d_stats() or {}
    print("ISLAND BENCH  " .. (on and "gpu" or "arm") .. "  " .. G.CAMS[G.cam] .. "  " .. tostring(gst.status or ""):sub(1, 24), 4, 4, TEXT)
    for i, r in ipairs(VIS) do
      local c = i == G.cur and CUR or (r.hdr and NUM or TEXT)
      local text = r.hdr and ((r.g.open and "- " or "+ ") .. r.name:upper()) or ("  " .. string.format("%-15s", r.name) .. " " .. r.get())
      print((i == G.cur and ">" or " ") .. text, 4, 4 + 12 * i, c)
    end
    if help then
      print(help[1], 4, 4 + 12 * (#VIS + 1), CUR)
      print(help[2], 4, 4 + 12 * (#VIS + 2), CUR)
    end
    print("Start+A: benchmark   Start: save", 4, 4 + 12 * (#VIS + (help and 3 or 1)), DIM)
  else
    if #VIS == 0 then build_vis() end
    local r = VIS[G.cur]
    rectfill(0, 0, 200, 16, 0x101820)
    print(string.format("%d %s %s", G.cur, r.name, r.hdr and "(group)" or r.get()), 4, 3, CUR)
  end
  -- the bottom bar, one line across the screen: a bar (16.7 ms is a full bar) and the numbers
  local by = Hh - 14
  rectfill(0, by, Wd, 14, 0x101820)
  local full = 60
  local w = math.min(full * 2, math.floor(L.ms / 16.7 * full))
  rectfill(4, by + 3, full, 8, 0x202830)
  rectfill(4, by + 3, math.min(w, full), 8, 0x40C060)
  if w > full then rectfill(4 + full, by + 3, math.min(w - full, 10), 8, 0xE04040) end
  local text = string.format("ms %s/%s fps %d skip %d|gpu %s wait %s|tri %d vtx %d lua %dk|3D %d 2D %d/%d|%dx%d",
    fmt(L.ms), fmt(L.max), math.floor(L.fps), L.skip or 0, fmt(L.gpu), fmt(L.wait), math.floor(L.tri), math.floor(L.vtx),
    math.floor(L.lua / 1000), drawn3, drawn2, G.nfixed + G.shown(), SCREEN_W, SCREEN_H)
  local note = G.bench_note
  if not note and G.save_note and time() - (G.save_t or 0) < 3 then note = G.save_note end
  if note then text = note .. " | " .. text end
  if not fx_on() then text = "Mali: no effects | " .. text end
  print(text, 70, by + 1, NUM)
end

-- ---------------------------------------------------------------- the cartridge
function _init()
  island_init()
  build_rows()
  particles(2048)
  gpu3d(true, nil, nil, 2, nil, true)
  textures3d(true)
  frameskip(MAX_SKIP)
  G.good, G.good_sig = snapshot(), sig()
  log("islandbench 1 start objects=" .. G.ntotal .. " floor tiles cut=" .. tostring(G.cut_tiles))
end

function _update()
  G.frame = G.frame + 1
  G.input()
  G.camera_update(G.frame / 60)
  weather_update()
end

function _draw()
  G.stats_frame()
  apply_env()
  cls(SKY)
  zclear()
  camera3d(G.cx, G.cy, G.cz, G.cyaw, G.cpitch, 60)
  local t = G.frame / 60
  draw_sky()
  map3d_draw(world, ({ 2, 0, 4 })[O.light])
  if O.sea == 2 then
    local m = model(O.look == 2 and "sea_f" or "sea")
    if m then draw3d(m, 36, -0.05 + 0.03 * math.sin(t * 1.3), 36, 0, 0, 0, 72, ({ 2, 0, 4 })[O.light]) end
  end
  draw_objects(t)
  draw_fire(t)
  if fx_on() and (O.weather == 2 or O.weather == 3) then particles_draw(O.weather == 2 and 0.04 or 0.07) end
  panel_draw()
end
