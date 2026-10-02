-- The animation reel: the hero filmed in third person, ability after
-- ability, with captions; then every animation clip of its models, one
-- after the other; then a little first-person play. The actor plays for
-- real (its inputs are scripted), so effects and timings are the game's.

local Reel = { t = 0, i = 1 }
local A, D, G2                  -- the hero, a target dummy, the dummy that shoots rockets

local function face(a, tx, ty, tz)
  local dx, dy, dz = tx - a.x, ty - (a.y + a.eye), tz - a.z
  a.yaw = atan(dx, dz)
  a.pitch = atan(dy, sqrt(dx * dx + dz * dz))
end

local function place(a, x, z, yaw)
  a.x, a.y, a.z, a.vx, a.vy, a.vz = x, 0, z, 0, 0, 0
  a.yaw, a.pitch = yaw or a.yaw, 0
  a.on_ground = true
end

-- the camera around the hero, `off` radians from the front (0: facing it,
-- > 0 towards its left side as we see it), at height h, distance d
local function front(h, off, pitch, d, fov)
  Cam.orbit(A.x, A.y + h, A.z, A.yaw + pi + off, pitch, d, fov or 55)
end

local function rally_form(a, name)
  if a.form.name ~= name then Actors.set_form(a, H.rally.forms[name]) end
end

-- the shots: caption, seconds, setup(), drive(c, t) -> inputs, cam(t)
local SHOTS = {
  { cap = "RALLY", sub = "TANK - A RACING PILOT IN A WHITE MECH", len = 4.5,
    setup = function() place(A, 0, 0, pi) end,
    cam = function(t) front(1.6, 0.7 - t * 0.3, -0.08, 7.5) end },
  { cap = "WALK AND RUN", sub = "BIRD LEGS, INVERSE KINEMATICS", len = 5,
    setup = function() place(A, -6, -6, pi / 2) end,
    drive = function(c, t)
      c.mz = t < 2.2 and 0.55 or 1
      A.yaw = A.yaw + DT * 0.55
    end,
    cam = function(t) front(1.5, 1.2, -0.05, 8) end },
  { cap = "FUSION CANNONS", sub = "PRIMARY FIRE - 11 PELLETS, 6.7 SHOTS A SECOND", len = 3.5,
    setup = function() place(A, 0, -12, 0) place(D, 2, 2, pi) end,
    drive = function(c, t) face(A, D.x, D.y + 1.6, D.z) c.fire = t > 0.4 end,
    cam = function(t) front(1.9, -1.1 + t * 0.08, -0.08, 6.5, 58) end },
  { cap = "NULL FIELD", sub = "SECONDARY - EATS THE PROJECTILES IN FRONT", len = 4,
    setup = function() place(A, 0, -6, 0) place(G2, 0, 12, pi) G2.st.rocket_cd = 0 end,
    drive = function(c, t)
      face(A, G2.x, G2.y + 1.6, G2.z)
      c.fire2 = t > 0.6
      local g = Input.blank(G2.cmd)
      face(G2, A.x, A.y + 1.5, A.z)
      g.ab2_p = t > 0.3 and t < 0.32
      G2.cmd = g
    end,
    cam = function(t) front(2.2, 1.35, -0.1, 9, 58) end },
  { cap = "AFTERBURNERS", sub = "ABILITY 1 - FLIGHT ALONG THE AIM", len = 3.5,
    setup = function() place(A, -18, -16, 0.6) A.st.boost_cd = 0 end,
    drive = function(c, t)
      c.ab1_p = t > 0.3 and t < 0.32
      A.pitch = 0.18
      A.yaw = 0.6 + t * 0.15
    end,
    cam = function(t) Cam.orbit(A.x, A.y + 1.8, A.z, A.yaw + 1.2, -0.1, 7.5, 60) end },
  { cap = "SWARM ROCKETS", sub = "ABILITY 2 - 18 ROCKETS, 1.5 SECONDS", len = 3.5,
    setup = function() place(A, 0, -14, 0) A.st.rocket_cd = 0 place(D, 0, 6, pi) end,
    drive = function(c, t) face(A, D.x, D.y + 1, D.z) c.ab2_p = t > 0.4 and t < 0.42 end,
    cam = function(t) front(2.0, -0.9, -0.12, 7, 58) end },
  { cap = "JUMP", sub = "TAKE-OFF, AIR, LANDING", len = 2.8,
    setup = function() place(A, 4, -8, pi * 0.75) end,
    drive = function(c, t) c.jump_p = (t > 0.5 and t < 0.52) end,
    cam = function(t) front(1.6, 0.9, -0.05, 8.5) end },
  { cap = "REDLINE", sub = "ULTIMATE - THE PILOT JUMPS OUT, THE MECH BLOWS UP", len = 6,
    setup = function()
      rally_form(A, "mech")
      place(A, -4, -10, 0.3) A.ult = 100
      place(D, 2, 8, pi)
    end,
    drive = function(c, t) if t < 0.6 then face(A, D.x, D.y + 1, D.z) end c.ult_p = t > 0.5 and t < 0.52 end,
    cam = function(t) Cam.orbit(-6, 3.5, -2, 0.55, -0.22, 12 + t * 0.6, 62) end },
  { cap = "PIT PISTOL", sub = "ON FOOT - THE PILOT'S LIGHT GUN", len = 3.5,
    setup = function()
      rally_form(A, "pilot") A.override = nil
      place(A, 0, -10, 0.2) place(D, 1, 4, pi)
    end,
    drive = function(c, t) face(A, D.x, D.y + 1.4, D.z) c.fire = t > 0.5 c.mx = sin(t * 2) * 0.7 end,
    cam = function(t) front(1.1, -0.9, -0.04, 3.4, 58) end },
  { cap = "PIT STOP", sub = "ULTIMATE ON FOOT - A NEW MECH FROM THE SKY", len = 3.5,
    setup = function()
      rally_form(A, "pilot") A.override = nil
      place(A, 3, -6, pi) A.ult = 100
    end,
    drive = function(c, t) c.ult_p = t > 0.3 and t < 0.32 end,
    cam = function(t) Cam.orbit(3, 2.0, -6, 0.5, -0.1, 9.5, 55) end },
  { cap = "VICTORY", sub = "", len = 3,
    setup = function()
      rally_form(A, "mech")
      place(A, 0, 0, pi) A.override = "victory" A.anim.base, A.anim.bt = "victory", 0
    end,
    cam = function(t) front(1.7, 0.5 - t * 0.2, -0.05, 7, 52) end },
}

-- then every clip of the two models (the gallery)
local GALLERY = {
  { form = "mech", clips = { "idle", "walk", "run", "fire", "boost", "matrix", "missiles", "jump", "air", "land",
                             "crouch", "hit", "eject", "selfdestruct", "death", "callmech", "victory" } },
  { form = "pilot", clips = { "idle", "run", "aim", "fire", "jump", "air", "land", "eject", "call", "hit",
                              "death", "victory" } },
}
local LAYERS = { fire = "body", matrix = "body", missiles = "body", hit = "body" }
local PILOT_LAYERS = { aim = "chest", fire = "chest", hit = "chest" }
for _, g in ipairs(GALLERY) do
  for _, clip in ipairs(g.clips) do
    SHOTS[#SHOTS + 1] = {
      cap = (g.form == "mech" and "MECH  " or "PILOT  ") .. clip:upper(), sub = "ANIMATION CLIP", len = 2.2,
      gallery = true,
      setup = function()
        rally_form(A, g.form)
        place(A, 0, 0, pi)
        local f = A.form
        for _, b in ipairs({ f.hips_bone, f.body_bone, table.unpack(f.aim_bones) }) do bone_turn(A.mesh, b) end
        A.override = (g.form == "mech" and LAYERS[clip] or g.form == "pilot" and PILOT_LAYERS[clip]) and "idle" or clip
        A.anim.base, A.anim.bt = A.override, 0
        local layer = g.form == "mech" and LAYERS[clip] or PILOT_LAYERS[clip]
        if layer then Actors.layer(A, clip, true) A.anim.lk = 1 else A.anim.layer = nil end
      end,
      cam = function(t)
        local d = g.form == "mech" and 7.2 or 3.6
        front(g.form == "mech" and 1.6 or 0.95, 0.75 - t * 0.3, -0.06, d, 52)
      end,
    }
  end
end
-- and a little first-person play at the end
SHOTS[#SHOTS + 1] = { cap = "FIRST PERSON", sub = "FIRE, FIELD, ROCKETS", len = 6, fp = true,
  setup = function()
    rally_form(A, "mech") A.override = nil A.anim.layer = nil
    place(A, 0, -16, 0) place(D, -3, 4, pi) place(G2, 4, 14, pi) A.st.rocket_cd, G2.st.rocket_cd = 0, 0
  end,
  drive = function(c, t)
    face(A, D.x, D.y + 1.4, D.z)
    A.yaw = A.yaw + sin(t * 1.3) * 0.08
    c.fire = t > 0.4 and t < 2.2
    c.fire2 = t > 2.6 and t < 3.8
    c.ab2_p = t > 4.0 and t < 4.02
    local g = Input.blank(G2.cmd)
    face(G2, A.x, A.y + 1.5, A.z)
    g.ab2_p = t > 2.4 and t < 2.42
    G2.cmd = g
  end }

Reel.shots = SHOTS

function Reel.start()
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
  A = Actors.spawn("rally", 1, 0, 0, 0, pi, { name = "Rally" })
  D = Actors.spawn("rally", 2, 40, 0, 40, pi, { name = "Dummy", dummy = true, respawn = 0.5 })
  G2 = Actors.spawn("rally", 2, -40, 0, 40, pi, { name = "Gunner", dummy = true, respawn = 0.5 })
  G.dmg_numbers = false
  Reel.i, Reel.t = 0, 0
  Reel.next()
end

function Reel.next()
  Reel.i = Reel.i + 1
  if Reel.i > #SHOTS then Reel.i = 1 end
  Reel.t = 0
  Proj.clear()
  Fx.clear()
  local s = SHOTS[Reel.i]
  -- the extras wait far away unless the shot places them
  for _, o in ipairs({ D, G2 }) do
    if not o.alive then Actors.respawn(o) end
    o.x, o.y, o.z = o.home.x, 0, o.home.z
    o.hp, o.armor = o.hpmax, o.armormax
  end
  A.override = nil
  if not A.alive then Actors.respawn(A) end
  s.setup()
  log("reel " .. Reel.i .. " " .. s.cap)
end

function Reel.update()
  local s = SHOTS[Reel.i]
  Reel.t = Reel.t + DT
  local c = Input.blank(A.cmd)
  if s.drive then s.drive(c, Reel.t) end
  A.cmd = c
  if s.gallery then
    -- a clip on its own: the pose only, the actor stays put
    A.anim.bt = A.anim.bt + DT
    if A.anim.layer then A.anim.lt = A.anim.lt + DT end
    local m = A.mesh
    if A.anim.layer then
      animate(m, A.anim.base, A.anim.bt, A.anim.layer, A.anim.lt, 1, A.form.layer_bone)
    else
      animate(m, A.anim.base, A.anim.bt)
    end
  else
    for _, a in ipairs(G.actors) do
      if a.alive then
        a.hero.update(a, a.cmd)
      else
        a.dead_t = a.dead_t + DT
        a.hero.update(a, a.cmd)
        if a.dead_t >= a.respawn then Actors.respawn(a) end
      end
      Actors.tick_fx(a)
      Actors.animate(a)
      if a ~= A then a.cmd = Input.blank(a.cmd) end
    end
    Proj.update()
    Fx.update()
  end
  if Reel.t >= s.len then Reel.next() end
  if Input.cmd.menu_p then Modes.start("menu") end
end

function Reel.draw()
  local s = SHOTS[Reel.i]
  if s.fp and A.alive then
    Cam.first(A)
    Modes.draw_scene(A)
    if A.hero.draw_fp_extra then A.hero.draw_fp_extra(A, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch) end
    A.hero.draw_fp(A, Cam.x, Cam.y, Cam.z, Cam.yaw, Cam.pitch, Cam.roll)
    Hud.draw(A)
  else
    s.cam(Reel.t)
    Modes.draw_scene(nil)
  end
  -- the caption, sliding in
  local k = smooth01(Reel.t / 0.35)
  local x = floor(-200 + 208 * k)
  font("8x16")
  local w = #s.cap * 8
  rectfill(x - 4, 136, w + 12, 18, 0xF26A21)
  print(s.cap, x + 2, 137, 0xFFFFFF)
  if s.sub ~= "" then
    font("6x12")
    rectfill(x - 4, 155, #s.sub * 6 + 10, 14, 0x101418)
    print(s.sub, x + 1, 156, 0xE8ECF0)
  end
  font("6x12")
  print("OVERBIT  ANIMATION REEL", 4, 3, 0xFFFFFF)
  local n = string.format("%d/%d", Reel.i, #SHOTS)
  print(n, 316 - #n * 6, 3, 0xFFE070)
  font()
end

Modes.list.reel = Reel
