-- Overbit model viewer (dev kit): a model of the cartridge, turning, with an
-- animation; the light of the game. Up/down: model, left/right: animation,
-- A: detail level, B: shadow. For bmhost: VIEW_MODEL, VIEW_CLIP, VIEW_YAW.
local names, mi, ci, detail = {}, 1, 1, 3
local meshes, clipl = {}, {}
local t0 = 0

function _init()
  names = models()
  for i, n in ipairs(names) do
    meshes[i] = model(n)
    clipl[i] = clips(meshes[i])
  end
  if VIEW_MODEL then for i, n in ipairs(names) do if n == VIEW_MODEL then mi = i end end end
  if VIEW_CLIP then for i, c in ipairs(clipl[mi]) do if c.name == VIEW_CLIP then ci = i end end end
end

function _update()
  if btnp(2) then mi = (mi - 2) % #names + 1; ci = 1; t0 = time() end
  if btnp(3) then mi = mi % #names + 1; ci = 1; t0 = time() end
  if btnp(0) then ci = (ci - 2) % math.max(1, #clipl[mi]) + 1; t0 = time() end
  if btnp(1) then ci = ci % math.max(1, #clipl[mi]) + 1; t0 = time() end
  if btnp(4) then detail = (detail + 3) % 4 end
end

function _draw()
  cls(0x6f8fb0)
  local m, cl = meshes[mi], clipl[mi][ci]
  local fp = names[mi]:find("fp") ~= nil
  local h = fp and 0 or (names[mi]:find("mech") and 1.6 or 0.9)
  local dist = fp and 0 or (names[mi]:find("mech") and 6.5 or 3.2)
  local yaw = VIEW_YAW or (time() * 0.6)
  zclear()
  camera3d(math.sin(yaw) * -dist, h + (fp and 0 or 0.4), math.cos(yaw) * -dist, yaw, fp and 0 or -0.08, fp and 96 or 50)
  light3d(-0.5, 0.8, -0.35, 0.42)
  sky3d(0xFFF2DC, 0xBFD8FF, 0x8a7a68)
  shine3d(0.9, 16, 0.25)
  -- the floor
  if not fp then
    local fl = FLOOR or mesh({-6,0,-6, 6,0,-6, 6,0,6, -6,0,6}, {1,4,3,0x5a6470, 1,3,2,0x5a6470})
    FLOOR = fl
    draw3d(fl, 0, 0, 0, 0, 0, 0, 1, 1)
  end
  if cl then animate(m, cl.name, time() - t0) end
  if not fp then draw3d(m, 0, 0, 0, 0, 0, 0, 1, 8) end
  draw3d(m, 0, 0, 0, 0, 0, 0, 1, 4 + (3 - detail) * 16 + (fp and 64 or 0))
  print(names[mi] .. "  " .. (cl and cl.name or "-") .. "  lod " .. detail, 2, 2, 0xffffff)
  print(string.format("%d tri %d px %.1fms", stat(4), stat(5), stat(1)), 2, 164, 0xffffff)
end
