-- Modes: the title menu, the training range (Rally against dummies), the
-- animation reel (every move of the hero, filmed in third person).

Modes = { cur = nil }

local function clear_match()
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
end

-- ---------------------------------------------------------------- drawing the game

-- the 3D scene seen by the camera (already set), then the effects
local function draw_scene(skip)
  World.light()
  Fx.lamps()
  World.draw_sky(Cam.pitch)
  zclear()
  World.draw()
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
  local s = World.spawn
  G.local_actor = Actors.spawn("rally", 1, s.x, s.y, s.z, s.yaw, { name = "You" })
  spawn_dummy(0, 0, "still", "Dummy")
  spawn_dummy(-8, 8, "strafe", "Strafer")
  spawn_dummy(9, 14, "jumper", "Jumper")
  spawn_dummy(0, 24, "shooter", "Gunner")
  G.dmg_numbers = true
end

local function update_actors()
  local me = G.local_actor
  for _, a in ipairs(G.actors) do
    if a.alive then
      if a == me then
        local c = Input.cmd
        a.cmd = c
        a.yaw = wrap_angle(a.yaw + c.look_x)
        a.pitch = clamp(a.pitch + c.look_y, -1.45, 1.45)
      else
        local c = Input.blank(a.cmd)
        if a.script then a.script(a, c, me) end
      end
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
  if me.alive then
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
  print("TRAINING RANGE", 4, 2, 0xFFE070)
  font()
end

-- ---------------------------------------------------------------- the title menu

local Menu = { sel = 1, t = 0 }
local ITEMS = { "TRAINING RANGE", "ANIMATION REEL", "QUALITY", "BENCHMARK" }

function Menu.start()
  clear_match()
  Menu.hero = Actors.spawn("rally", 1, 0, 0, 0, pi * 0.85, { name = "Rally" })
  Menu.t = 0
end

function Menu.update()
  Menu.t = Menu.t + DT
  local c = Input.cmd
  if c.up_p then Menu.sel = (Menu.sel - 2) % #ITEMS + 1 Snd.play("ui") end
  if c.down_p then Menu.sel = Menu.sel % #ITEMS + 1 Snd.play("ui") end
  local a = Menu.hero
  -- the hero on the title: idle, now and then its victory pose
  local ph = Menu.t % 9
  a.override = ph > 6 and "victory" or nil
  if ph > 6 and ph - DT <= 6 then a.anim.base, a.anim.bt = "victory", 0 end
  Actors.animate(a)
  if c.jump_p or c.menu_p or c.fire_p then
    local it = ITEMS[Menu.sel]
    Snd.play("ui")
    if it == "TRAINING RANGE" then Modes.start("range")
    elseif it == "ANIMATION REEL" then Modes.start("reel")
    elseif it == "QUALITY" then
      if G.qauto then G.qauto = false Quality.set(4)
      elseif G.quality > 0 then Quality.set(G.quality - 1)
      else G.qauto = true Quality.set(3) end
    elseif it == "BENCHMARK" then Modes.start("bench") end
  end
end

function Menu.draw()
  local a = Menu.hero
  local t = Menu.t
  Cam.orbit(a.x, 1.7, a.z, pi + 0.5 + sin(t * 0.15) * 0.5, -0.06, 8.5, 55)
  draw_scene(nil)
  -- the title
  font("8x16")
  local title = "OVERBIT"
  print(title, 160 - #title * 8 + 1, 15, 0x101418, 2)
  print(title, 160 - #title * 8, 14, 0xFFFFFF, 2)
  font("6x12")
  print("a hero shooter for bm", 160 - 21 * 3, 48, 0xFFE070)
  for i, it in ipairs(ITEMS) do
    local s = it
    if it == "QUALITY" then s = "QUALITY: " .. (G.qauto and "AUTO" or Quality.names[G.quality + 1]) end
    local y = 96 + (i - 1) * 16
    local sel = i == Menu.sel
    if sel then rectfill(8, y - 2, #s * 6 + 12, 14, 0xF26A21) end
    print(s, 14, y, sel and 0xFFFFFF or 0xD8DCE2)
  end
  print("build " .. (OVERBIT_BUILD or "dev"), 4, 168, 0x9AA0A8)
  font()
end

-- ---------------------------------------------------------------- registry

local MODES = { range = Range, menu = Menu }
Modes.list = MODES

function Modes.start(name)
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
