-- Modes: the title menu, the training range (the hero against dummies), the
-- animation reel (every move of the hero, filmed in third person); the
-- match is in 81_match.

Modes = { cur = nil }

local function clear_match()
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
end

-- ---------------------------------------------------------------- drawing the game

-- the 3D scene seen by the camera (already set), then the effects; extra:
-- what the mode draws in the world (the point of the match)
local function draw_scene(skip, extra)
  World.light()
  Fx.lamps()
  if not G.gpu then World.draw_sky(Cam.pitch) end      -- 2D: under the 3D
  zclear()
  if G.gpu then World.draw_sky(Cam.pitch) end          -- 3D: no 2D before the 3D (20_world)
  World.draw()
  if extra then extra() end
  Props.draw()
  Actors.draw(Cam.x, Cam.y, Cam.z, skip)
  Proj.draw()
  Fx.draw()
end
Modes.draw_scene = draw_scene

-- ---------------------------------------------------------------- dummies

-- a dummy's command each frame: d.script(a, c)
local function face(a, tx, ty, tz)
  local dx, dy, dz = tx - a.x, ty - (a.y + a.eye), tz - a.z
  a.yaw = atan(dx, dz)
  a.pitch = atan(dy, sqrt(dx * dx + dz * dz))
end

local SCRIPTS = {
  still = function(a, c, p) if p then face(a, p.x, p.y + 1.5, p.z) end end,
  strafe = function(a, c, p)
    if p then face(a, p.x, p.y + 1.5, p.z) end
    c.mx = (floor((G.t + a.id) / 1.6) % 2 == 0) and 1 or -1
  end,
  jumper = function(a, c, p)
    if p then face(a, p.x, p.y + 1.5, p.z) end
    c.jump_p = floor(G.t * 60) % 120 == a.id * 7 % 120
  end,
  -- a friend who keeps getting hurt: the supports have someone to heal
  wounded = function(a, c, p)
    a.wound_t = (a.wound_t or 3) - DT
    if a.wound_t <= 0 then
      a.wound_t = 4
      if Actors.total(a) > 120 then Actors.damage(a, 90, nil, false, "range") end
    end
    if p then face(a, p.x, p.y + 1.5, p.z) end
  end,
  shooter = function(a, c, p)
    if not p or not p.alive then return end
    face(a, p.x, p.y + 1.2, p.z)
    c.ab2_p = (a.st.rocket_cd or 0) <= 0 and floor(G.t * 60) % 240 == 0
  end,
}

local function spawn_dummy(x, z, script, name)
  local a = Actors.spawn("rally", 2, x, 0, z, pi, { dummy = true, respawn = 2.5, name = name })
  a.script = SCRIPTS[script]
  return a
end

-- ---------------------------------------------------------------- the range

local Range = {}

function Range.start()
  clear_match()
  World.use(Range.map and "map" or "range")
  local s = World.spawn
  G.local_actor = Actors.spawn(G.hero_id, 1, s.x, s.y, s.z, s.yaw, { name = "You" })
  if Range.map then
    -- on the map: dummies on the point, a friend by the spawn
    local p = World.mark("point")
    for i, sc in ipairs({ "still", "strafe", "jumper" }) do
      local a = Actors.spawn(HERO_ORDER[(i * 3) % #HERO_ORDER + 1], 2, p.x + (i - 2) * 3, p.y, p.z + 3, -pi / 2,
        { dummy = true, respawn = 2.5, name = "Dummy" .. i })
      a.script = SCRIPTS[sc]
    end
    G.dmg_numbers = true
    return
  end
  spawn_dummy(0, 0, "still", "Dummy")
  local st = Actors.spawn(H.kaiju and "kaiju" or "rally", 2, -8, 0, 8, pi, { dummy = true, respawn = 2.5, name = "Strafer" })
  st.script = SCRIPTS.strafe
  local bud = Actors.spawn(H.sarge and "sarge" or "rally", 1, 5, 0, -8, -0.4, { dummy = true, respawn = 2.5, name = "Buddy" })
  bud.script = SCRIPTS.wounded
  spawn_dummy(9, 14, "jumper", "Jumper")
  spawn_dummy(0, 24, "shooter", "Gunner")
  G.dmg_numbers = true
end

local function update_actors()
  local me = G.local_actor
  for _, a in ipairs(G.actors) do
    if a.alive then
      if a == me and not a.script then
        local c = Input.cmd
        a.cmd = c
        if not Actors.frozen(a) then
          a.yaw = wrap_angle(a.yaw + c.look_x)
          a.pitch = clamp(a.pitch + c.look_y, -1.45, 1.45)
        end
      else
        local c = Input.blank(a.cmd)
        if a.script then a.script(a, c, me) end
      end
      Actors.status(a, a.cmd)
      a.hero.update(a, a.cmd)
    else
      a.dead_t = a.dead_t + DT
      a.hero.update(a, a.cmd)
      if a.dead_t >= a.respawn then Actors.respawn(a) end
    end
    Actors.tick_fx(a)
    Actors.animate(a)
  end
  Proj.update()
  Fx.update()
end
Modes.update_actors = update_actors

function Range.update()
  update_actors()
  if Input.cmd.menu_p then Modes.start("menu") end
end

function Range.draw()
  local me = G.local_actor
  if Dev.draw_cam() then return end
  if me.alive and me.hero.camera and me.hero.camera(me) then
    draw_scene(nil)                              -- a camera of the hero's own (Fuse's wheel)
  elseif me.alive then
    Cam.first(me)
    draw_scene(me)
    if me.hero.draw_fp_extra then me.hero.draw_fp_extra(me, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch) end
    me.hero.draw_fp(me, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch, Cam.roll)
  else
    Cam.death(me)
    draw_scene(nil)
  end
  Fx.draw2d()
  Hud.draw(me)
  font("6x12")
  uprint("TRAINING RANGE", 4, 2, 0xFFE070)
  font()
end

-- ---------------------------------------------------------------- the title menu

local Menu = { sel = 1, t = 0 }
local ITEMS = { "PLAY: CONTROL", "PLAY ONLINE", "TRAINING RANGE", "HERO", "BOTS", "ANIMATION REEL", "QUALITY",
                "3D", "RESOLUTION", "BENCHMARK" }

-- the 3D renderer: the GPU, with its vertex shader placing every model
-- (VS), with anti-aliasing, both, the GPU and the GPU+VS with the frame
-- queue (Q: it draws while the next frame's _update runs), the ARM (a
-- console without a GPU, QEMU: the ARM only; a choice this GPU cannot do
-- is skipped)
local RENDERERS = { { true, false, 0, false }, { true, false, 2, false }, { true, true, 0, false },
                    { true, true, 2, false }, { true, false, 0, true }, { true, false, 2, true },
                    { false, false, 0, false } }

local function renderer_name()
  local on, aa, vs, _, q = gpu3d()
  if not on then return "ARM" end
  return "GPU" .. (vs == 1 and " + VS1" or vs and " + VS" or "") .. (aa and " + AA 4X" or "") .. (q and " + Q" or "")
end

local function next_renderer()
  local on, aa, vs, _, q = gpu3d()
  local cur = #RENDERERS
  for i, r in ipairs(RENDERERS) do
    if r[1] == on and r[2] == (aa or false) and r[3] == (vs or 0) and r[4] == (q or false) then cur = i end
  end
  for k = 1, #RENDERERS do
    local r = RENDERERS[(cur + k - 1) % #RENDERERS + 1]
    local on2, aa2, vs2, _, q2 = gpu3d(r[1], r[2], r[3], r[4])
    if not r[1] then return end
    if not on2 then Menu.no_gpu = true gpu3d(false, false, 0, false) return end
    if aa2 == r[2] and (vs2 or 0) == r[3] and (q2 or false) == r[4] then return end
  end
end

-- the resolution (screen()): the console's modes, 320x180 to 1920x1080;
-- the ARM's only up to 640x360 (its pixels cost it, the GPU's do not). The
-- choice is saved (save(), with the relay of 83_net) and set again at the
-- next start (Modes.saved_res).
local function res_modes()
  local list = {}
  for i = 1, 16 do
    local w, h = screen(i)
    if not w then break end
    if G.gpu or w * h <= 640 * 360 then list[#list + 1] = { w, h } end
  end
  return list
end

local function res_set(w, h)
  if not screen(w, h) then return end
  local t = saved() or {}
  t.res = w .. "x" .. h
  save(t)
end

local function next_res(k)
  local list = res_modes()
  local cur = 0
  for i, m in ipairs(list) do if m[1] == SW and m[2] == SH then cur = i end end
  if cur == 0 then cur = k > 0 and 0 or 1 end
  local m = list[(cur - 1 + k) % #list + 1]
  res_set(m[1], m[2])
end

function Modes.saved_res()
  local t = screen and saved()
  local r = OVERBIT_RES or (t and t.res)          -- OVERBIT_RES = "960x540": tests, reels
  if not screen or type(r) ~= "string" then return end
  local w, h = r:match("^(%d+)x(%d+)$")
  if not w then return end
  w, h = tonumber(w), tonumber(h)
  for _, m in ipairs(res_modes()) do
    if m[1] == w and m[2] == h then screen(w, h) log(string.format("overbit resolution %dx%d", w, h)) return end
  end
end

G.hero_id = "rally"          -- OVERBIT_HERO (tests) in _init
G.bot_diff = 2               -- the bots: 1 easy, 2 normal, 3 hard (Bots.SKILL)

local function menu_hero()
  if Menu.hero then
    for i, a in ipairs(G.actors) do if a == Menu.hero then table.remove(G.actors, i) break end end
  end
  Menu.hero = Actors.spawn(G.hero_id, 1, 0, 0, 0, pi * 0.85, { name = H[G.hero_id].name })
  Menu.vt = 0
end

-- the next hero of the roster (k = 1 or -1)
local function next_hero(k)
  local n = #HERO_ORDER
  for i, id in ipairs(HERO_ORDER) do
    if id == G.hero_id then G.hero_id = HERO_ORDER[(i - 1 + k) % n + 1] break end
  end
  menu_hero()
end

function Menu.start()
  clear_match()
  Menu.hero = nil
  menu_hero()
  Menu.t = 0
end

function Menu.update()
  Menu.t = Menu.t + DT
  local c = Input.cmd
  if c.up_p then Menu.sel = (Menu.sel - 2) % #ITEMS + 1 Snd.play("ui") end
  if c.down_p then Menu.sel = Menu.sel % #ITEMS + 1 Snd.play("ui") end
  if ITEMS[Menu.sel] == "HERO" and (c.left_p or c.right_p) then
    next_hero(c.right_p and 1 or -1)
    Snd.play("ui")
  end
  if ITEMS[Menu.sel] == "BOTS" and (c.left_p or c.right_p) then
    G.bot_diff = (G.bot_diff - 1 + (c.right_p and 1 or -1)) % 3 + 1
    Snd.play("ui")
  end
  if ITEMS[Menu.sel] == "RESOLUTION" and (c.left_p or c.right_p) then
    next_res(c.right_p and 1 or -1)
    Snd.play("ui")
  end
  local a = Menu.hero
  -- the hero on the title: idle, now and then its victory pose
  Menu.vt = Menu.vt + DT
  local ph = Menu.vt % 9
  a.override = ph > 6 and "victory" or nil
  if ph > 6 and ph - DT <= 6 then a.anim.base, a.anim.bt = "victory", 0 end
  Actors.animate(a)
  if c.jump_p or c.menu_p or c.fire_p then
    local it = ITEMS[Menu.sel]
    Snd.play("ui")
    if it == "PLAY: CONTROL" then Modes.start("match")
    elseif it == "PLAY ONLINE" then Modes.start("lobby")
    elseif it == "TRAINING RANGE" then Modes.start("range")
    elseif it == "HERO" then next_hero(1)
    elseif it == "BOTS" then G.bot_diff = G.bot_diff % 3 + 1
    elseif it == "ANIMATION REEL" then Modes.start("reel")
    elseif it == "QUALITY" then
      if G.qauto then G.qauto = false Quality.set(4)
      elseif G.quality > 0 then Quality.set(G.quality - 1)
      else G.qauto = true Quality.set(3) end
    elseif it == "3D" then next_renderer()
    elseif it == "RESOLUTION" then next_res(1)
    elseif it == "BENCHMARK" then Modes.start("bench") end
  end
end

function Menu.draw()
  local a = Menu.hero
  local t = Menu.t
  Cam.orbit(a.x, 1.7, a.z, pi + 0.5 + sin(t * 0.15) * 0.5, -0.06, 8.5, 55)
  draw_scene(nil)
  -- the title (smaller layout on a screen of 180 or 216 lines)
  local small = LH < 270
  font("8x16")
  local title = "OVERBIT"
  local cx = LW // 2
  local ty = small and 4 or 18
  uprint(title, cx - #title * 8 + 1, ty + 1, 0x101418, 2)
  uprint(title, cx - #title * 8, ty, 0xFFFFFF, 2)
  font("6x12")
  uprint("a hero shooter for bm", cx - 21 * 3, small and 38 or 54, 0xFFE070)
  local top, step = small and 54 or 80, small and 12 or 15
  for i, it in ipairs(ITEMS) do
    local s = it
    if it == "QUALITY" then s = "QUALITY: " .. (G.qauto and "AUTO" or Quality.names[G.quality + 1]) end
    if it == "3D" then s = "3D: " .. renderer_name() .. (Menu.no_gpu and " (NO GPU)" or "") end
    if it == "RESOLUTION" then s = "RESOLUTION: < " .. SW .. "x" .. SH .. " >" end
    if it == "HERO" then s = "HERO: < " .. H[G.hero_id].name:upper() .. " >" end
    if it == "BOTS" then s = "BOTS: < " .. Bots.SKILL[G.bot_diff].name .. " >" end
    local y = top + (i - 1) * step
    local sel = i == Menu.sel
    if sel then urectfill(10, y - 2, #s * 6 + 12, step, 0xF26A21) end
    uprint(s, 16, y, sel and 0xFFFFFF or 0xD8DCE2)
  end
  -- the chosen hero: name, role, line
  local h = H[G.hero_id]
  local role = h.role:upper()
  local rx = LW - 8
  if small then
    uprint(h.name:upper(), rx - #h.name * 6, 4, h.rgb)
    uprint(role, rx - #role * 6, 17, 0xD8DCE2)
  else
    uprint(h.name:upper(), rx - #h.name * 6, LH - 56, h.rgb)
    uprint(role, rx - #role * 6, LH - 42, 0xD8DCE2)
    uprint(h.desc, rx - #h.desc * 6, LH - 28, 0x9AA0A8)
    uprint("build " .. (OVERBIT_BUILD or "dev"), 6, LH - 14, 0x9AA0A8)
  end
  font()
end

-- ---------------------------------------------------------------- registry

-- the range on the map (M38.3, before the match): walk Partenope
local Explore = setmetatable({}, { __index = Range })
function Explore.start()
  Range.map = true
  Range.start()
  Range.map = false
end
function Explore.update() Range.update() end
function Explore.draw()
  Range.draw()
end

local MODES = { range = Range, menu = Menu, explore = Explore }
Modes.list = MODES

function Modes.start(name)
  -- the game's time at 60 _update a second: when a frame costs more than
  -- 1/60 s up to 4 _update run before the next _draw (frames skipped, the
  -- match does not slow down); the benchmark measures one _update a frame
  frameskip(name == "bench" and 1 or 4)
  G.mode = name
  Modes.cur = MODES[name]
  if Modes.cur.start then Modes.cur.start() end
end

function Modes.update()
  Modes.cur.update()
end

function Modes.draw()
  Modes.cur.draw()
end
