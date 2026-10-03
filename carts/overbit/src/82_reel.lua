-- The animation reel: each hero filmed in third person, ability after
-- ability, with captions (Rally also with every animation clip of its
-- models, one after the other), then a little first-person play. The actor
-- plays for real (its inputs are scripted), so effects and timings are the
-- game's. OVERBIT_HERO (build.py --hero) films one hero only.

local Reel = { t = 0, i = 1 }
local A, D, G2, B               -- the hero, a target dummy, the dummy that shoots rockets, a friend

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

-- a hero's size for the camera: (height of the aim point, distance)
local function size()
  if A.form.height > 2.4 then return 1.6, 7.5 end
  return 0.95, 4.8
end

local function rally_form(a, name)
  if a.form.name ~= name then Actors.set_form(a, H.rally.forms[name]) end
end

-- the shots: caption, seconds, setup(), drive(c, t) -> inputs, cam(t)
local SHOTS = {
  { cap = "RALLY", sub = "TANK - A RACING PILOT IN A WHITE MECH", len = 4.5,
    setup = function() place(A, 0, 0, pi) end,
    cam = function(t) front(1.6, 0.7 - t * 0.3, -0.08, 7.5) end },
  { cap = "WALK AND RUN", sub = "LEGS BY INVERSE KINEMATICS", len = 5,
    setup = function() place(A, -6, -6, pi / 2) end,
    drive = function(c, t)
      c.mz = t < 2.2 and 0.55 or 1
      A.yaw = A.yaw + DT * 0.55
    end,
    cam = function(t) front(1.5, 1.2, -0.05, 8) end },
  { cap = "ROTARY GUNS", sub = "PRIMARY FIRE - THE FOREARMS, 11 ROUNDS 6.7 TIMES A SECOND", len = 3.5,
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

for _, sh in ipairs(SHOTS) do sh.hero = sh.hero or "rally" end

-- ---------------------------------------------------------------- the other heroes

local function blank_g2(t, fire_at)
  local g = Input.blank(G2.cmd)
  face(G2, A.x, A.y + 1.2, A.z)
  g.ab2_p = fire_at and t > fire_at and t < fire_at + 0.02
  G2.cmd = g
end

-- the standard shots of a hero: the intro, then its own, then the victory
local function hero_shots(id, sub, list, fp)
  local out = {}
  out[#out + 1] = { hero = id, cap = H[id].name:upper(), sub = sub, len = 4,
    setup = function() place(A, 0, 0, pi) end,
    cam = function(t) local h, d = size() front(h, 0.7 - t * 0.3, -0.06, d) end }
  for _, sh in ipairs(list) do
    sh.hero = id
    sh.cam = sh.cam or function(t) local h, d = size() front(h, sh.off or -0.9, -0.08, d * (sh.zoom or 1.6), 58) end
    out[#out + 1] = sh
  end
  out[#out + 1] = { hero = id, cap = "VICTORY", sub = "", len = 3,
    setup = function() place(A, 0, 0, pi) A.override = "victory" A.anim.base, A.anim.bt = "victory", 0 end,
    cam = function(t) local h, d = size() front(h, 0.5 - t * 0.2, -0.05, d, 52) end }
  out[#out + 1] = { hero = id, cap = "FIRST PERSON", sub = fp.sub, len = fp.len or 6, fp = true,
    setup = function() place(A, 0, -14, 0) place(D, -2, 4, pi) place(B, 3, -9, 0) if fp.setup then fp.setup() end end,
    drive = fp.drive }
  for _, sh in ipairs(out) do SHOTS[#SHOTS + 1] = sh end
end

local function at(t, t0) return t > t0 and t < t0 + 0.02 end
local function aim_d(c) face(A, D.x, D.y + D.height * 0.55, D.z) end

hero_shots("kaiju", "TANK - SWORD AND SHIELD IN A BIG RED MECH", {
  { cap = "PLASMA SABER", sub = "PRIMARY - WIDE SWINGS OF THE SWORD, 65 DAMAGE", len = 3.5,
    setup = function() place(A, 0, -3.4, 0) place(D, 0, 1, pi) end,
    drive = function(c, t) aim_d(c) c.fire = t > 0.3 end, off = -1.2, zoom = 1.1 },
  { cap = "FUSION REPEATER", sub = "SECONDARY - THE GUN UNDER THE SHIELD", len = 3,
    setup = function() place(A, 0, -10, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire2 = t > 0.3 end, off = 1.0, zoom = 1.0 },
  { cap = "PROPULSORS", sub = "ABILITY 1 - THREE DASHES OF FUEL", len = 3.5,
    setup = function() place(A, -6, -8, 0.8) A.st.fuel = 3 end,
    drive = function(c, t) c.mz = 1 c.ab1_p = at(t, 0.3) or at(t, 1.2) or at(t, 2.1) end,
    cam = function(t) Cam.orbit(-2, 2.2, -4, 0.9, -0.12, 13, 60) end },
  { cap = "POWER BARRIER", sub = "ABILITY 2 - A WALL OF HEXAGONS; SURGING STRIKE", len = 4.5,
    setup = function() place(A, 0, -6, 0) place(G2, 0, 10, pi) G2.st.rocket_cd = 0 place(D, 1, -1.6, pi) end,
    drive = function(c, t) face(A, G2.x, G2.y + 1.6, G2.z) c.ab2 = t > 0.3 c.fire_p = at(t, 3.0) blank_g2(t, 0.6) end,
    off = 2.55, zoom = 1.25 },
  { cap = "LIMIT BREAK", sub = "ULTIMATE - A FULL CIRCLE OF THE SWORD", len = 3,
    setup = function() place(A, 0, -2, 0) A.ult = 100 place(D, 2, 2, pi) end,
    drive = function(c, t) c.ult_p = at(t, 0.4) end, off = 0.6, zoom = 1.3 },
  { cap = "EJECT", sub = "THE MECH BREAKS: THE PILOT JUMPS OUT", len = 3,
    setup = function() place(A, 0, 0, pi) end,
    drive = function(c, t) if at(t, 0.5) then A.fx.invuln_t = 0 Actors.damage(A, (Actors.total(A) + 1) / 0.7) end end,
    cam = function(t) Cam.orbit(0, 1.8, 0, 0.6, -0.1, 9, 58) end },
  { cap = "MINI REPEATER", sub = "ON FOOT - THE PILOT'S LITTLE GUN", len = 3,
    setup = function() if A.form.name ~= "pilot" then Actors.set_form(A, H.kaiju.forms.pilot) end A.override = nil
      place(A, 0, -8, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire = t > 0.4 c.mx = sin(t * 2) * 0.6 end, off = -0.9, zoom = 1.0 },
  { cap = "CALL MECH", sub = "ULTIMATE ON FOOT - BIG RED IS BACK", len = 3,
    setup = function() if A.form.name ~= "pilot" then Actors.set_form(A, H.kaiju.forms.pilot) end A.override = nil
      place(A, 3, -6, pi) A.ult = 100 end,
    drive = function(c, t) c.ult_p = at(t, 0.3) end,
    cam = function(t) Cam.orbit(3, 2.0, -6, 0.5, -0.1, 9.5, 55) end },
}, { sub = "SABER, BARRIER, REPEATER",
  setup = function() if A.form.name ~= "mech" then Actors.set_form(A, H.kaiju.forms.mech) end place(A, 0, -3.5, 0) place(D, 0, 1, pi) end,
  drive = function(c, t) aim_d(c) c.fire = t < 2 c.ab2 = t > 2.3 and t < 3.8 c.fire2 = t > 4 end })

hero_shots("sarge", "DAMAGE - A VETERAN IN TANGERINE, A PULSE RIFLE", {
  { cap = "HEAVY PULSE RIFLE", sub = "PRIMARY - 30 SHOTS, 9 A SECOND", len = 3.5,
    setup = function() place(A, 0, -10, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire = t > 0.3 end },
  { cap = "HELIX ROCKETS", sub = "SECONDARY - THREE ROCKETS TWISTED TOGETHER", len = 3,
    setup = function() place(A, 0, -12, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire2_p = at(t, 0.5) end, off = 1.1 },
  { cap = "SPRINT", sub = "ABILITY 1 - HELD", len = 3,
    setup = function() place(A, -8, -10, 0.7) end,
    drive = function(c, t) c.mz = 1 c.ab1 = t > 0.3 end,
    cam = function(t) Cam.orbit(A.x, A.y + 1.2, A.z, A.yaw + 1.9, -0.08, 5, 60) end },
  { cap = "BIOTIC FIELD", sub = "ABILITY 2 - HEALS THE TEAM AROUND IT", len = 4,
    setup = function() place(A, 0, 0, pi) place(B, 2, -1, pi) end,
    drive = function(c, t) c.ab2_p = at(t, 0.4) if at(t, 0.2) then Actors.damage(A, 120) Actors.damage(B, 120) end end,
    cam = function(t) Cam.orbit(1, 1.0, 0, 0.6 + t * 0.1, -0.35, 7.5, 60) end },
  { cap = "TACTICAL VISOR", sub = "ULTIMATE - EVERY SHOT FINDS ITS TARGET", len = 4,
    setup = function() place(A, 0, -12, 0) place(D, -3, 2, pi) place(G2, 4, 6, pi) A.ult = 100 end,
    drive = function(c, t) c.ult_p = at(t, 0.3) c.fire = t > 0.6 A.yaw = sin(t * 2) * 0.3 end, off = -1.2 },
}, { sub = "RIFLE, ROCKETS, VISOR",
  drive = function(c, t) aim_d(c) A.yaw = A.yaw + sin(t) * 0.1 c.fire = t < 2.5 c.fire2_p = at(t, 3) end })

hero_shots("frost", "DAMAGE - A CLIMATE SCIENTIST WITH A CRYO BLASTER", {
  { cap = "CRYO BLASTER", sub = "PRIMARY - A STREAM OF FROST THAT SLOWS", len = 3.5,
    setup = function() place(A, 0, -5, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire = t > 0.3 end, off = -1.1, zoom = 1.3 },
  { cap = "ICICLE", sub = "SECONDARY - DOUBLE DAMAGE ON THE HEAD", len = 3,
    setup = function() place(A, 0, -12, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire2_p = at(t, 0.5) or at(t, 1.6) end, off = 1.1 },
  { cap = "CRYO-FREEZE", sub = "ABILITY 1 - ICE AROUND HER: SHE HEALS", len = 4.5,
    setup = function() place(A, 0, 0, pi) A.st.cryo_cd = 0 end,
    drive = function(c, t) c.ab1_p = at(t, 0.4) end, off = 0.6, zoom = 1.3 },
  { cap = "ICE WALL", sub = "ABILITY 2 - FIVE PILLARS, THEY STOP EVERYONE", len = 4,
    setup = function() place(A, 0, -8, 0) A.st.wall_cd = 0 end,
    drive = function(c, t) A.pitch = -0.28 c.ab2_p = at(t, 0.4) end,
    cam = function(t) Cam.orbit(0, 1.8, -4, 2.3 - t * 0.15, -0.12, 11, 60) end },
  { cap = "BLIZZARD", sub = "ULTIMATE - A STORM THAT SLOWS AND FREEZES", len = 5,
    setup = function() place(A, 0, -10, 0) place(D, 0, 0, pi) A.ult = 100 end,
    drive = function(c, t) face(A, D.x, D.y, D.z) c.ult_p = at(t, 0.3) end,
    cam = function(t) Cam.orbit(0, 1.5, 0, 0.7 + t * 0.1, -0.3, 13, 60) end },
}, { sub = "STREAM, ICICLE, WALL",
  setup = function() place(A, 0, -6, 0) end,
  drive = function(c, t) aim_d(c) c.fire = t < 2.2 c.fire2_p = at(t, 2.6) if t > 3.6 then A.pitch = -0.25 end c.ab2_p = at(t, 3.8) end })

hero_shots("fuse", "DAMAGE - A NEON DEMOLITIONS ARTIST", {
  { cap = "FRAG LAUNCHER", sub = "PRIMARY - BOUNCING GRENADES", len = 4,
    setup = function() place(A, 0, -10, 0) place(D, 1, 0, pi) end,
    drive = function(c, t) aim_d(c) A.pitch = A.pitch + 0.15 c.fire = t > 0.3 end, off = 1.1 },
  { cap = "CONCUSSION MINE", sub = "ABILITY 1 - THROWN, BLOWN UP: THE MINE JUMP", len = 4,
    setup = function() place(A, 0, -6, 0) A.st.mines = 2 end,
    drive = function(c, t) A.pitch = -1.2 c.ab1_p = at(t, 0.3) c.fire2_p = at(t, 0.9) end,
    cam = function(t) Cam.orbit(0, 3, -6, 1.6, -0.2, 11, 60) end },
  { cap = "STEEL TRAP", sub = "ABILITY 2 - JAWS THAT BITE AND HOLD", len = 4.5,
    setup = function() place(A, 0, -6, 0) A.st.trap_cd = 0 place(D, 0, 2, pi) end,
    drive = function(c, t)
      A.pitch = -0.4 c.ab2_p = at(t, 0.3)
      local d = Input.blank(D.cmd)
      d.mz = (t > 1.4 and t < 3.0) and 0.5 or 0
      D.cmd = d
    end,
    cam = function(t) Cam.orbit(0, 1.2, -3, 1.4, -0.2, 8, 58) end },
  { cap = "BOOM WHEEL", sub = "ULTIMATE - A BOMB ON WHEELS, BLOWN UP AT WILL", len = 5,
    setup = function() place(A, 0, -14, 0) place(D, 0, 4, pi) A.ult = 100 end,
    drive = function(c, t) c.ult_p = at(t, 0.3) c.fire_p = at(t, 3.6) end,
    cam = function(t) if not (A.hero.camera and A.hero.camera(A)) then local h, d = size() front(h, -0.8, -0.1, d * 1.5) end end },
  { cap = "TOTAL MAYHEM", sub = "PASSIVE - BOMBS SPILL OUT WHEN HE FALLS", len = 3,
    setup = function() place(A, 0, 0, pi) end,
    drive = function(c, t) if at(t, 0.5) then A.fx.invuln_t = 0 Actors.damage(A, Actors.total(A) + 1) end end,
    cam = function(t) Cam.orbit(0, 1.0, 0, 0.6, -0.2, 7, 58) end },
}, { sub = "GRENADES, MINE",
  setup = function() if not A.alive then Actors.respawn(A) end place(A, 0, -10, 0) end,
  drive = function(c, t) aim_d(c) A.pitch = A.pitch + 0.12 c.fire = t < 3 c.ab1_p = at(t, 3.6) c.fire2_p = at(t, 4.6) end })

hero_shots("rail", "DAMAGE - A COMMANDER WITH A RAIL RIFLE", {
  { cap = "RAILGUN", sub = "PRIMARY - FAST BOLTS THAT CHARGE ENERGY", len = 3.5,
    setup = function() place(A, 0, -10, 0) place(D, 1, 2, pi) A.st.energy = 0 end,
    drive = function(c, t) aim_d(c) c.fire = t > 0.3 end },
  { cap = "RAIL SHOT", sub = "SECONDARY - HITSCAN, STRONGER WITH THE ENERGY", len = 3,
    setup = function() place(A, 0, -14, 0) place(D, 1, 2, pi) A.st.energy = 100 end,
    drive = function(c, t) aim_d(c) c.fire2_p = at(t, 0.8) end, off = 1.2 },
  { cap = "POWER SLIDE", sub = "ABILITY 1 - A SLIDE; JUMP OUT OF IT: HIGH", len = 3,
    setup = function() place(A, -6, -10, 0.6) A.st.slide_cd = 0 end,
    drive = function(c, t) c.mz = 1 c.ab1_p = at(t, 0.3) c.jump_p = at(t, 0.9) end,
    cam = function(t) Cam.orbit(-3, 2, -7, 2.2, -0.1, 10, 60) end },
  { cap = "DISRUPTOR SHOT", sub = "ABILITY 2 - A FIELD THAT HURTS AND SLOWS", len = 4,
    setup = function() place(A, 0, -10, 0) place(D, 0, 0, pi) A.st.dis_cd = 0 end,
    drive = function(c, t) aim_d(c) c.ab2_p = at(t, 0.4) end,
    cam = function(t) Cam.orbit(0, 1.5, -2, 0.9, -0.15, 11, 60) end },
  { cap = "OVERCLOCK", sub = "ULTIMATE - THE RAIL SHOTS GO THROUGH EVERYONE", len = 4,
    setup = function() place(A, 0, -14, 0) place(D, 0, 0, pi) place(G2, 0.3, 6, pi) A.ult = 100 end,
    drive = function(c, t) face(A, D.x, D.y + 1.5, D.z) c.ult_p = at(t, 0.3) c.fire2_p = at(t, 1.4) or at(t, 2.6) end,
    off = 1.3 },
}, { sub = "BOLTS, RAIL SHOT",
  drive = function(c, t) aim_d(c) c.fire = t < 2.5 c.fire2_p = at(t, 3.0) c.ab2_p = at(t, 4.2) end })

hero_shots("orbit", "SUPPORT - A YOUNG EXPLORER FROM A SPACE STATION", {
  { cap = "MEDIBLASTER", sub = "PRIMARY - HEALS FRIENDS, HURTS ENEMIES", len = 3.5,
    setup = function() place(A, 0, -6, 0) place(B, 1.5, 1, pi) Actors.damage(B, 150) end,
    drive = function(c, t) face(A, B.x, B.y + 1.2, B.z) c.fire = t > 0.3 end, off = -1.0 },
  { cap = "PULSAR TORPEDOES", sub = "SECONDARY - HELD TO LOCK, RELEASED TO FIRE", len = 4,
    setup = function() place(A, 0, -10, 0) place(D, -2, 2, pi) place(G2, 2, 3, pi) end,
    drive = function(c, t) face(A, 0, 1.5, 2.5) c.fire2 = t > 0.3 and t < 1.6 end,
    cam = function(t) Cam.orbit(0, 2, -5, 0.45 - t * 0.05, -0.12, 10, 62) end },
  { cap = "MARTIAN MOBILITY", sub = "PASSIVE - A SECOND JUMP, THEN HOVERING", len = 4,
    setup = function() place(A, 0, -4, 0.5) end,
    drive = function(c, t) c.mz = 0.6 c.jump_p = at(t, 0.3) or at(t, 0.8) c.jump = t > 0.8 and t < 3.4 end,
    cam = function(t) Cam.orbit(A.x, A.y + 1.0, A.z, A.yaw + 1.8, -0.05, 5, 60) end },
  { cap = "HYPER RING", sub = "ABILITY 2 - FRIENDS GOING THROUGH IT SPEED UP", len = 4,
    setup = function() place(A, 0, -6, 0) A.st.ring_cd = 0 end,
    drive = function(c, t) c.ab2_p = at(t, 0.3) c.mz = t > 1.0 and 1 or 0 end,
    cam = function(t) Cam.orbit(0, 1.4, -3, 0.75, -0.08, 7.5, 60) end },
  { cap = "ORBITAL RAY", sub = "ULTIMATE - A BEAM FROM ORBIT: HEALING, STRONGER", len = 5,
    setup = function() place(A, 0, -8, 0) place(B, 1, -1, pi) Actors.damage(B, 150) A.ult = 100 end,
    drive = function(c, t) c.ult_p = at(t, 0.3) end,
    cam = function(t) Cam.orbit(0, 3, -2, 1.0 + t * 0.1, -0.2, 14, 62) end },
}, { sub = "HEALING, TORPEDOES",
  setup = function() Actors.damage(B, 150) end,
  drive = function(c, t) face(A, B.x, B.y + 1.2, B.z) c.fire = t < 2.5 if t > 2.8 then aim_d(c) end c.fire2 = t > 3 and t < 4.4 end })

hero_shots("akari", "SUPPORT - A SHRINE GUARDIAN WITH PAPER CHARMS", {
  { cap = "HEALING OFUDA", sub = "PRIMARY - CHARMS THAT FIND A FRIEND", len = 3.5,
    setup = function() place(A, 0, -6, 0) place(B, 2, 1, pi) Actors.damage(B, 150) end,
    drive = function(c, t) face(A, B.x, B.y + 1.2, B.z) c.fire = t > 0.3 end, off = -1.0 },
  { cap = "KUNAI", sub = "SECONDARY - THREE TIMES ON THE HEAD", len = 3,
    setup = function() place(A, 0, -10, 0) place(D, 1, 2, pi) end,
    drive = function(c, t) aim_d(c) c.fire2_p = at(t, 0.5) or at(t, 1.2) or at(t, 1.9) end, off = 1.1 },
  { cap = "SWIFT STEP", sub = "ABILITY 1 - NEXT TO A FRIEND IN SIGHT", len = 3,
    setup = function() place(A, 0, -10, 0) place(B, 6, 4, pi) A.st.step_cd = 0 end,
    drive = function(c, t) face(A, B.x, B.y + 1.2, B.z) c.ab1_p = at(t, 0.8) end,
    cam = function(t) Cam.orbit(3, 1.4, -3, 1.9, -0.12, 12, 60) end },
  { cap = "PROTECTION SUZU", sub = "ABILITY 2 - UNTOUCHABLE FOR A MOMENT, HEALED", len = 3.5,
    setup = function() place(A, 0, -6, 0) place(B, 0.5, -1, pi) Actors.damage(B, 120) A.st.suzu_cd = 0 end,
    drive = function(c, t) face(A, B.x, B.y, B.z) c.ab2_p = at(t, 0.4) end, off = 1.2 },
  { cap = "KITSUNE RUSH", sub = "ULTIMATE - THE FOX'S ROAD: FASTER, STRONGER", len = 5,
    setup = function() place(A, 0, -14, 0) A.ult = 100 end,
    drive = function(c, t) c.ult_p = at(t, 0.3) c.mz = t > 1.6 and 1 or 0 end,
    cam = function(t) Cam.orbit(0, 3, -2, 2.4 - t * 0.12, -0.15, 16, 62) end },
}, { sub = "OFUDA, KUNAI",
  setup = function() Actors.damage(B, 150) end,
  drive = function(c, t) face(A, B.x, B.y + 1.2, B.z) c.fire = t < 2.5 if t > 2.8 then aim_d(c) end c.fire2_p = at(t, 3.2) or at(t, 3.9) end })

-- one hero only (build.py --hero)
local function only(ids)
  if not ids then return end
  local want = {}
  for id in ids:gmatch("[^,]+") do want[id] = true end          -- "kaiju" or "kaiju,sarge,..."
  local keep = {}
  for _, sh in ipairs(SHOTS) do if want[sh.hero] then keep[#keep + 1] = sh end end
  if #keep > 0 then SHOTS = keep end
end

Reel.shots = SHOTS

function Reel.start()
  for k in pairs(G.actors) do G.actors[k] = nil end
  Actors.feed = {}
  Proj.clear()
  Fx.clear()
  only(OVERBIT_HERO)
  Reel.shots = SHOTS
  A = Actors.spawn(SHOTS[1].hero, 1, 0, 0, 0, pi, { name = H[SHOTS[1].hero].name })
  D = Actors.spawn("rally", 2, 40, 0, 40, pi, { name = "Dummy", dummy = true, respawn = 0.5 })
  G2 = Actors.spawn("rally", 2, -40, 0, 40, pi, { name = "Gunner", dummy = true, respawn = 0.5 })
  B = Actors.spawn("sarge", 1, 40, 0, -40, 0, { name = "Buddy", dummy = true, respawn = 0.5 })
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
  -- a new hero: in place of the old one
  if A.hero.id ~= s.hero then
    for i, o in ipairs(G.actors) do if o == A then table.remove(G.actors, i) break end end
    A = Actors.spawn(s.hero, 1, 0, 0, 0, pi, { name = H[s.hero].name })
  end
  -- the extras wait far away unless the shot places them
  for _, o in ipairs({ D, G2, B }) do
    if not o.alive then Actors.respawn(o) end
    o.x, o.y, o.z = o.home.x, 0, o.home.z
    o.hp, o.armor = o.hpmax, o.armormax
    o.fx = {}
  end
  A.override = nil
  if not A.alive then Actors.respawn(A) end
  if not s.gallery and s.hero ~= "rally" then A.hero.spawn(A) end    -- a clean start: no ultimate left from before
  A.fx = {}
  s.setup()
  log(string.format("reel %d %s @%.2f %s", Reel.i, s.cap, G.t, s.sub or ""))
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
        Actors.status(a, a.cmd)
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
  if s.fp and A.alive and A.hero.camera and A.hero.camera(A) then
    Modes.draw_scene(nil)
  elseif s.fp and A.alive then
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
  rectfill(x - 4, 136, w + 12, 18, H[s.hero].rgb == 0xF26A21 and 0xF26A21 or Fx.fade(H[s.hero].rgb, 0.8))
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
