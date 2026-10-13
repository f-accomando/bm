-- forgedemo (Pi edition, 320x180): a humanoid made with bm-forge cycling
-- through its three clips, with the head turning on its own.
-- APIs (references/API.md):
--   model(name), models()            section 10  (docs/api/light3d.md:71-72)
--   bones3d, clips, animate,
--   bone3d, bone_turn                section 11  (docs/api/light3d.md:74-79)
--   draw3d, camera3d, light3d,
--   zclear                           section 12  (docs/api/light3d.md:80,88-90)
--   btn / btnp (numbers 4 A, 5 B)    section 6   (docs/api/input.md:9-10)
--   stat(4) triangles, log           section 7   (docs/api/system.md:9-21)
-- A: auto cycle on/off. Manual: B next clip, left/right turn the head.
-- On start it checks its own data and prints one log() line for bmhost.

local CLIPS = { "idle", "walk", "jump" }
local HEAD = "head"
local CYCLE = 2

local hero, ground
local t, ct, idx, auto, look, f = 0, 0, 1, true, 0, 0
local checks, fails = 0, 0

local function check(ok, what)
  checks = checks + 1
  if not ok then
    fails = fails + 1
    log("forgedemo: FAIL " .. what)
  end
end

function _init()
  hero = model("humanoid")
  ground = model("ground")
  check(hero ~= nil, "hero model")
  check(ground ~= nil, "ground model")
  local ms = models()
  check(#ms == 2 and ms[1] == "humanoid" and ms[2] == "ground", "models() list")
  if not hero then return end
  local bs = bones3d(hero)
  local has = false
  for _, b in ipairs(bs) do if b == HEAD then has = true end end
  check(#bs == 7 and has, "7 bones with head")
  local cs = clips(hero)
  local loops, lens = { true, true, false }, { 2.0, 1.0, 1.5 }
  local ok = #cs == 3
  if ok then
    for i = 1, 3 do
      local c = cs[i]
      if c.name ~= CLIPS[i] or c.loop ~= loops[i] or math.abs(c.length - lens[i]) > 1e-3 then ok = false end
    end
  end
  check(ok, "clips idle/walk/jump")
  check(math.abs(animate(hero, "walk", 0) - 1.0) < 1e-3, "animate returns walk length")
  local _, hy = bone3d(hero, HEAD)
  check(hy ~= nil and hy > 1.2 and hy < 2.2, "head height")
end

function _update()
  t = t + 1 / 60
  ct = ct + 1 / 60
  f = f + 1
  if btnp(4) then auto = not auto end
  if auto then
    if ct >= CYCLE then idx = idx % 3 + 1; ct = 0 end
    look = 0.5 * math.sin(t)
  else
    if btnp(5) then idx = idx % 3 + 1; ct = 0 end
    if btn(0) then look = math.max(-0.8, look - 0.03) end
    if btn(1) then look = math.min(0.8, look + 0.03) end
  end
  if f == 10 then
    log(string.format("forgedemo: %d/%d checks passed", checks - fails, checks))
    log("forgedemo: tris " .. stat(4))
  end
end

function _draw()
  cls(0x8CC8F0)
  zclear()
  camera3d(0, 1.1, -4, 0, -0.08, 60)
  light3d(-0.4, 0.8, -0.5, 0.45)
  draw3d(ground, 0, 0, 0, 0, 0, 0, 1, 1)
  bone_turn(hero, HEAD, 0, look, 0)
  animate(hero, CLIPS[idx], ct)
  draw3d(hero, 0, 0, 0, 0, 0, 0, 1)
  print(CLIPS[idx], 4, 4, 0x203050)
  print(stat(4) .. " tri", 4, 20, 0x203050)
end
