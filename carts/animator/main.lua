-- bm Animator on the console: the skeletons and animations of the 3D
-- models of a .bm. F1 play (the player), F2 rig
-- (bones and skin), F3 animate (keyframes), F4 sprites (an animation drawn
-- into the sprite sheet), Esc menu; Ctrl+S save, F5 try the game,
-- Ctrl+Z / Ctrl+Y undo / redo, [ ] model. Hold F12 (or ?) for the keys.
-- Gamepad: Y + left/right page, Y + B menu, Y + up/down model, Y + A undo.
-- The models are bm Studio's (menu: Open in bm Studio); the shared code is
-- the kernel's library bm3d (src/script/bm3d.lua).

local T = require "bm3d"
local S, C, V, Q = T.S, T.C, T.V, T.Q
local W, H = T.W, T.H
local HINT_Y, FPS = T.HINT_Y, T.FPS
local floor, abs, sqrt, max, min, pi = math.floor, math.abs, math.sqrt, math.max, math.min, math.pi
local sin, cos = math.sin, math.cos
local spack, sunpack = string.pack, string.unpack
local clamp, round = T.clamp, T.round
local M, say = T.M, T.say
local U = T.U                                    -- the mouse (bmui)
local PANEL_W = 168                              -- the lists on the left
local INFO_X = PANEL_W + 16

local function rig() return T.rig() end

-- the strip behind the lines of text over the 3D view
local function info_strip(lines)
  rectfill(PANEL_W, 16, W - PANEL_W, 16 + lines * 16, C.BG)
end

local function no_model(x, y)
  print("no models: make them in bm Studio", x, y, C.DIM)
  print("(menu: Open in bm Studio)", x, y + 16, C.DIM)
end

local function bone_names(r)
  local names = {}
  for i, b in ipairs(r.bones) do
    local d, p = 0, b.parent
    while p > 0 and d < 64 do d, p = d + 1, r.bones[p].parent end
    names[i] = string.rep(" ", d) .. b.name
  end
  return names
end

local function unique_bone(r, name, skip)
  name = name:sub(1, 16)
  local function used(n)
    for i, b in ipairs(r.bones) do if b.name == n and i ~= skip then return true end end
  end
  if not used(name) then return name end
  for k = 2, 99 do
    local s = tostring(k)
    local n = name:sub(1, 16 - #s) .. s
    if not used(n) then return n end
  end
  return name
end

-- "arm.L" <-> "arm.R", "leftLeg" <-> "rightLeg"... (the names bm Animator mirrors)
local function mirror_name(n)
  local pairs_ = { { "%.L$", ".R" }, { "%.R$", ".L" }, { "_L$", "_R" }, { "_R$", "_L" }, { "^L_", "R_" }, { "^R_", "L_" },
                   { "Left", "Right" }, { "left", "right" }, { "Right", "Left" }, { "right", "left" } }
  for _, p in ipairs(pairs_) do
    if n:find(p[1]) then return (n:gsub(p[1], p[2], 1)) end
  end
end

local function rest_pose(n)
  local p = {}
  for i = 1, n do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
  return p
end

local function copy_pose(p)
  local o = {}
  for i, b in ipairs(p) do o[i] = { q = { b.q[1], b.q[2], b.q[3], b.q[4] }, t = { b.t[1], b.t[2], b.t[3] } } end
  return o
end

-- The skin check ("test pose", key t on the rig page). Each bone in turn is bent 45 degrees round x and
-- round z; a face is marked when one of its edges stretches, or when a corner shared with a face that
-- follows another bone comes apart (the skin tears). The bone matrices are the kernel's animate() ones:
-- M[i] = M[parent] * T(head + t) * R(q) * T(-head) (src/bm/runtime.c).
local STRETCH, TEAR = 1.6, 0.02          -- an edge over 1.6x its length; a gap over 2% of the model's height

local function bone_mats(r, pose)
  local mats = {}
  for i, b in ipairs(r.bones) do
    local p = pose[i]
    local q = Q.norm(p.q)
    local x, y, z, w = q[1], q[2], q[3], q[4]
    local rot = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                  2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                  2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) }
    local h = b.head
    local loc = {}
    for row = 0, 2 do
      local a, c, d = rot[row * 3 + 1], rot[row * 3 + 2], rot[row * 3 + 3]
      loc[row * 4 + 1], loc[row * 4 + 2], loc[row * 4 + 3] = a, c, d
      loc[row * 4 + 4] = h[row + 1] + p.t[row + 1] - (a * h[1] + c * h[2] + d * h[3])
    end
    local par = b.parent and b.parent > 0 and b.parent < i and mats[b.parent]
    if not par then mats[i] = loc
    else
      local m = {}
      for row = 0, 2 do
        local a0, a1, a2, a3 = par[row * 4 + 1], par[row * 4 + 2], par[row * 4 + 3], par[row * 4 + 4]
        for col = 0, 2 do m[row * 4 + col + 1] = a0 * loc[col + 1] + a1 * loc[col + 5] + a2 * loc[col + 9] end
        m[row * 4 + 4] = a0 * loc[4] + a1 * loc[8] + a2 * loc[12] + a3
      end
      mats[i] = m
    end
  end
  return mats
end

local function xform(m, p)
  return { m[1] * p[1] + m[2] * p[2] + m[3] * p[3] + m[4],
           m[5] * p[1] + m[6] * p[2] + m[7] * p[3] + m[8],
           m[9] * p[1] + m[10] * p[2] + m[11] * p[3] + m[12] }
end

-- -> { faces = set of marked faces, n = how many, worst = name of the bone whose bending marks most }
local function stress_test(m)
  local r = m.rig
  local hot, n, count = {}, 0, {}
  local lo, hi = math.huge, -math.huge
  for _, f in ipairs(m.faces) do for _, p in ipairs(f.p) do lo, hi = min(lo, p[2]), max(hi, p[2]) end end
  local tear = TEAR * max(hi - lo, 1e-3)
  local groups, by_key = {}, {}              -- the corners in one place, with the faces and bones they belong to
  for _, f in ipairs(m.faces) do
    if f.b then
      for k, p in ipairs(f.p) do
        local key = T.pkey(p)
        local g = by_key[key]
        if not g then g = { p = p, items = {} }; by_key[key] = g; groups[#groups + 1] = g end
        g.items[#g.items + 1] = { f, f.b[k] or f.b[1] or 1 }
      end
    end
  end
  local mixed = {}
  for _, g in ipairs(groups) do
    for i = 2, #g.items do if g.items[i][2] ~= g.items[1][2] then mixed[#mixed + 1] = g; break end end
  end
  local function mark(f, bi)
    if not hot[f] then hot[f] = true; n = n + 1; count[bi] = (count[bi] or 0) + 1 end
  end
  for bi = 1, #r.bones do
    for ax = 1, 3, 2 do
      local pose = rest_pose(#r.bones)
      pose[bi].q = Q.axis(V.unit(ax), pi / 4)
      local mats = bone_mats(r, pose)
      for _, f in ipairs(m.faces) do
        if f.b then
          local nc, P = #f.p, {}
          for k = 1, nc do P[k] = xform(mats[f.b[k] or f.b[1] or 1] or mats[1], f.p[k]) end
          for k = 1, nc do
            local j = k % nc + 1
            local d0 = sqrt(V.dist2(f.p[k], f.p[j]))
            if d0 > 1e-3 and sqrt(V.dist2(P[k], P[j])) > STRETCH * d0 then mark(f, bi); break end
          end
        end
      end
      for _, g in ipairs(mixed) do
        local P, apart = {}, false
        for i, it in ipairs(g.items) do P[i] = xform(mats[it[2]] or mats[1], g.p) end
        for i = 1, #P do for j = i + 1, #P do if V.dist2(P[i], P[j]) > tear * tear then apart = true end end end
        if apart then for _, it in ipairs(g.items) do mark(it[1], bi) end end
      end
    end
  end
  local worst, wn = nil, 0
  for bi, c in pairs(count) do if c > wn then worst, wn = bi, c end end
  return { faces = hot, n = n, worst = worst and r.bones[worst].name }
end

-- the faces coloured by their bones (shared corners), as a mesh; the
-- chosen faces brighter, the faces the test pose marked red
local function skin_mesh(m, chosen, hot, only)
  if not m.rig or #m.faces == 0 then return nil end
  local v, f, index = {}, {}, {}
  local function vid(p)
    local k = T.pkey(p)
    local i = index[k]
    if not i then
      i = #v // 3 + 1
      index[k] = i
      v[#v + 1], v[#v + 2], v[#v + 3] = p[1], p[2], p[3]
    end
    return i
  end
  for _, fc in ipairs(m.faces) do
   if not only or only[fc.b and fc.b[1] or 1] then
    local ids = {}
    for k, p in ipairs(fc.p) do ids[k] = vid(p) end
    local col = T.bone_colour(fc.b and fc.b[1] or 1)
    if hot and hot[fc] then col = 0xFF2020 end
    if chosen and chosen[fc] then col = 0xFFFFFF end
    for _, t in ipairs(#fc.p == 4 and T.QUAD or T.TRI) do
      local a, b, c = ids[t[1]], ids[t[2]], ids[t[3]]
      if a ~= b and b ~= c and a ~= c then
        f[#f + 1], f[#f + 2], f[#f + 3], f[#f + 4] = a, b, c, col
      end
    end
   end
  end
  if #v // 3 <= 4096 and #f > 0 and #f // 4 <= 16384 then return mesh(v, f) end
end

----------------------------------------------------------------- play page (the player)
local play, rig_page, anim_page, sprites_page
do

local pl = { clip = 1, t = 0, playing = true, speed = 1, bones = false, lit = true, spin = true, blend = 0,
             cam = { yaw = 0.6, pitch = -0.35, dist = 4, tx = 0, ty = 0.5, tz = 0 } }
local pl_clips = {}

play = {
  id = "play", fkey = "f1", label = "play",
  -- the keys while F12 is held (bm3d's T.keyhelp)
  help = {
    { "up / down", "model" },
    { "left / right", "animation" },
    { "space", "play / pause" },
    { ", / .", "a frame back / on" },
    { "< / >", "slower / faster" },
    { "a / d", "turn the view" },
    { "w / s", "tilt" },
    { "+ / -", "zoom" },
    { "o / k / l", "spin / bones / light" },
    { "b", "mix with the next animation" },
    { "z", "frame the model" },
  },
  help_pad = {
    { "A", "play / pause" },
    { "B / X", "bones / spin" },
    { "X DPAD", "the view" },
  },
}

function play.reset()
  pl.clip, pl.t, pl.blend = 1, 0, 0
  pl_clips = S.view and clips(S.view) or {}
  T.aim(pl.cam, M() and M().faces or {}, 2.7)
end
play.enter = play.reset

function play.refresh()
  pl_clips = S.view and clips(S.view) or {}
  pl.clip = clamp(pl.clip, 1, max(1, #pl_clips))
end

function play.key(k)
  local cam = pl.cam
  if T.cam_key(cam, k) then pl.spin = false; return end
  if k == "up" then T.select_model(S.cur - 1); T.refresh(); play.reset()
  elseif k == "down" then T.select_model(S.cur + 1); T.refresh(); play.reset()
  elseif k == "left" or k == "right" then
    if #pl_clips > 0 then
      pl.clip = (pl.clip - 1 + (k == "right" and 1 or -1)) % #pl_clips + 1
      pl.t = 0
    end
  elseif k == " " or k == "\n" then pl.playing = not pl.playing
  elseif k == "a" then cam.yaw = cam.yaw - 0.15; pl.spin = false
  elseif k == "d" then cam.yaw = cam.yaw + 0.15; pl.spin = false
  elseif k == "w" then cam.pitch = clamp(cam.pitch - 0.1, -1.45, 1.2)
  elseif k == "s" then cam.pitch = clamp(cam.pitch + 0.1, -1.45, 1.2)
  elseif k == "o" then pl.spin = not pl.spin
  elseif k == "k" then pl.bones = not pl.bones
  elseif k == "l" then pl.lit = not pl.lit
  elseif k == "b" then pl.blend = (pl.blend + 0.25) % 1.25; if pl.blend > 1 then pl.blend = 0 end
  elseif k == "," then pl.playing = false; pl.t = pl.t - 1 / FPS
  elseif k == "." then pl.playing = false; pl.t = pl.t + 1 / FPS
  elseif k == "<" then pl.speed = max(0.125, pl.speed / 2)
  elseif k == ">" then pl.speed = min(4, pl.speed * 2)
  elseif k == "z" or k == "f" then T.aim(pl.cam, M() and M().faces or {}, 2.7)
  end
end

function play.pad()
  if btn(6) then T.cam_pad(pl.cam); pl.spin = false; return end   -- X + the pad: the view
  local rp = T.rp
  if rp[2] then play.key("up") end
  if rp[3] then play.key("down") end
  if rp[0] then play.key("left") end
  if rp[1] then play.key("right") end
  if btnp(4) then play.key(" ") end
  if btnp(5) then pl.bones = not pl.bones end
  if T.tap[6] then pl.spin = not pl.spin end
end

-- the mouse: right drag turns (no more spinning), middle moves, wheel zooms
function play.mouse()
  if T.mouse_cam(pl.cam, PANEL_W, 80, W - PANEL_W, HINT_Y - 80) and U.drag then pl.spin = false end
  if U.click(0) and U.inside(PANEL_W, 80, W - PANEL_W, HINT_Y - 80) then play.key(" ") end
end

function play.menu_items()
  return { { pl.playing and "Stop" or "Play", " " }, { "Bones", "k" }, { "Light", "l" }, { "Spin", "o" },
           { "Blend with the next", "b" }, { "Frame before", "," }, { "Frame after", "." }, { "Slower", "<" },
           { "Faster", ">" }, { "Frame the model", "z" } }
end

function play.update(dt)
  if pl.spin then pl.cam.yaw = pl.cam.yaw + 0.008 end
  if pl.playing then pl.t = pl.t + dt * pl.speed end
end

function play.draw()
  local m, cam = M(), pl.cam
  cls(C.SKY)
  zclear()
  T.look(cam, PANEL_W / 2, 28)
  if m and #m.faces > 0 then
    local step = cam.size > 6 and 1 or (cam.size > 2 and 0.5 or 0.25)
    T.draw_grid(round(cam.tx / step) * step, cam.floor, round(cam.tz / step) * step, 8, step, C.GRID)
  end
  local c = pl_clips[pl.clip]
  local view = S.view
  if view then
    if c then
      if pl.blend > 0 and #pl_clips > 1 then
        local c2 = pl_clips[pl.clip % #pl_clips + 1]
        animate(view, pl.clip, pl.t, pl.clip % #pl_clips + 1, pl.t * c2.length / max(c.length, 1e-3), pl.blend)
      else
        animate(view, pl.clip, pl.t)
      end
    end
    light3d(-0.4, 0.8, -0.5, pl.lit and 0.35 or 1)
    draw3d(view, 0, 0, 0, 0, 0, 0, 1, pl.lit and 0 or 2)
    if pl.bones and m.rig then
      for i = 1, #m.rig.bones do
        local hx, hy, hz, tx, ty, tz = bone3d(view, i)
        T.draw_bone({ hx, hy, hz }, { tx, ty, tz }, T.bone_colour(i), false)
      end
    end
  end
  -- the lists
  rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
  local names = {}
  for i, mm in ipairs(S.models) do names[i] = mm.name end
  local rows = #pl_clips > 0 and min(8, max(1, #names)) or 16
  T.draw_list("MODELS", names, S.cur, 0, 32, rows, nil, nil, function(i)
    if i ~= S.cur then T.select_model(i); T.refresh(); play.reset() end
  end)
  if #pl_clips > 0 then
    local cn = {}
    for i, cc in ipairs(pl_clips) do cn[i] = cc.name end
    local y = 32 + (rows + 2) * 16
    T.draw_list("ANIMATIONS", cn, pl.clip, 0, y, max(1, min(#cn, (HINT_Y - y - 16) // 16)), nil, nil,
                function(i, again)                -- a click: that animation; a double click plays it
      if i ~= pl.clip then pl.clip, pl.t = i, 0 end
      if again then pl.playing = true end
    end)
  end
  -- what it is
  local x = INFO_X
  info_strip(c and pl.blend > 0 and #pl_clips > 1 and 4 or 3)
  if not m then
    no_model(x, 32)
  else
    print(m.name, x, 32, C.ACC)
    print((m.nv or 0) .. " vertices, " .. (m.nt or 0) .. " triangles" ..
          (m.rig and (", " .. #m.rig.bones .. " bones") or ""), x, 48, C.TEXT)
    if not view then
      print("this model has no faces yet: build it in bm Studio", x, 64, C.DIM)
    elseif c then
      local len = max(c.length, 1e-3)
      local tt = c.loop and (pl.t % len) or clamp(pl.t, 0, len)
      print(string.format("%s %s  %.2f / %.2f s  x%g%s", pl.playing and ">" or "||", c.name, tt, c.length,
                          pl.speed, c.loop and "  loop" or ""), x, 64, C.TEXT)
      if pl.blend > 0 and #pl_clips > 1 then
        print(string.format("mixed with %s: %d%%", pl_clips[pl.clip % #pl_clips + 1].name, round(pl.blend * 100)),
              x, 80, C.ACC)
      end
    elseif m.rig then
      print("a skeleton with no animations yet (F3)", x, 64, C.DIM)
    else
      print("no skeleton yet: F2 makes one", x, 64, C.DIM)
    end
  end
  print(string.format("%d fps  %.1f ms", stat(2), stat(1)), W - 128, 32, C.DIM)
  T.hint({ { { "up", "down" }, "model" }, { { "left", "right" }, "anim" }, { { "space" }, "play" },
           { { "a", "d", "w", "s" }, "view" }, { { "+", "-" }, "zoom" }, { { "k" }, "bones" } })
end
end

----------------------------------------------------------------- rig page (bones and skin)
do

local rg = { bone = 1, tail = true, skin = false, mesh = nil, hot = nil, sel = {},
             cam = { yaw = 0.5, pitch = -0.3, dist = 4, tx = 0, ty = 1, tz = 0 } }

local function nearest_bone(r, p)
  local best, bd2 = 1, 1e18
  for i, b in ipairs(r.bones) do
    local ab, ap = V.sub(b.tail, b.head), V.sub(p, b.head)
    local l2 = V.dot(ab, ab)
    local u = l2 > 1e-12 and clamp(V.dot(ap, ab) / l2, 0, 1) or 0
    local d = V.sub(ap, V.scale(ab, u))
    local dd = V.dot(d, d)
    if dd < bd2 - 1e-9 then best, bd2 = i, dd end
  end
  return best
end

-- the corners to their nearest bones: each face whole (rigid parts, as on
-- the PS1) or each corner (the mesh stretches at the joints); only the
-- chosen faces, if some are
local function auto_skin(m, smooth, list)
  for _, f in ipairs(list or m.faces) do
    f.b = {}
    if smooth then
      for k, p in ipairs(f.p) do f.b[k] = nearest_bone(m.rig, p) end
    else
      local b = nearest_bone(m.rig, T.face_center(f))
      for k = 1, #f.p do f.b[k] = b end
    end
  end
end

local function chosen_list()
  local out = {}
  for _, f in ipairs(M() and M().faces or {}) do if rg.sel[f] then out[#out + 1] = f end end
  return #out > 0 and out or nil
end

-- Parts: h shows only the faces of the chosen bone (rg.only = "bone"), then with the bones below it ("tree"),
-- then all again. The faces are drawn in the colour of their bone (the skin view).
local function only_set()
  local r = rig()
  if not rg.only or not r or not r.bones[rg.bone] then return nil end
  return T.part_bones(r, rg.bone, rg.only == "tree")
end

local function remesh()
  local only = only_set()
  rg.mesh = (rg.skin or only) and M() and skin_mesh(M(), rg.skin and rg.sel or nil, rg.skin and rg.stress and rg.stress.faces or nil, only) or nil
end

rig_page = {
  id = "rig", fkey = "f2", label = "rig",
  help = {
    { "up / down", "bone" },
    { "w a s d r f", "move its end" },
    { "shift w a s d r f", "move it a little" },
    { "tab", "head / tail" },
    { "j", "head alone: it leaves the parent's tail (shoulder, hip)" },
    { "n", "new bone (child of this one)" },
    { "h", "show only the faces of this bone, then with the bones below it, then all" },
    { "o / i", "turn axis x y z / step 5, 15, 45, 90 degrees" },
    { ", . / u", "turn this bone and the bones below it round its head (rest pose) / type the degrees" },
    { "x / del", "delete the bone" },
    { "m", "mirror it and its children (.L / .R)" },
    { "enter / p", "rename / parent" },
    { "k / shift k", "auto skin: rigid parts / smooth" },
    { "v", "skin: the faces and their bones" },
    { "t", "test pose: bend each bone, mark red the faces that tear" },
    { "q / e", "turn the view" },
    { "+ / -", "zoom" },
    { "z", "frame the model" },
    "skin (v)",
    { "up down left right", "a face" },
    { "space", "choose" },
    { "c / a", "all joined / assign to the bone" },
    { "b", "choose all the faces of the bone" },
    { "pgup / pgdn", "the bone" },
    { "backspace", "choose none" },
    { "k / shift k", "auto (the chosen faces)" },
  },
  help_pad = {
    { "UPDOWN", "bone" },
    { "A DPAD", "move its end" },
    { "X UPDOWN", "nearer / farther" },
    { "B / X", "head or tail / new bone" },
    { "A / B / X", "skin: choose / assign / done" },
  },
}

function rig_page.reset()
  rg.bone, rg.hot, rg.sel, rg.for_model, rg.stress = 1, nil, {}, M(), nil
  T.aim(rg.cam, M() and M().faces or {}, 2.4)
  remesh()
end
-- a model chosen elsewhere (the player, [ ]): the camera on it
rig_page.enter = function()
  if rg.for_model ~= M() then
    rg.for_model, rg.hot, rg.sel = M(), nil, {}
    T.aim(rg.cam, M() and M().faces or {}, 2.4)
  end
  remesh()
end

function rig_page.refresh()
  local r = rig()
  rg.bone = r and clamp(rg.bone, 1, #r.bones) or 1
  rg.sel, rg.hot = {}, nil
  remesh()
end

local function new_skeleton(m)
  local lo, hi = T.bounds_of(m.faces)
  local cx, cz = (lo[1] + hi[1]) / 2, (lo[3] + hi[3]) / 2
  local function g(v) return round(v * 8) / 8 end
  m.rig = { bones = { { name = "root", parent = 0, head = { g(cx), g(lo[2]), g(cz) },
                        tail = { g(cx), g(lo[2] + max(0.25, (hi[2] - lo[2]) * 0.5)), g(cz) } } },
            clips = {} }
  for _, f in ipairs(m.faces) do
    f.b = {}
    for k = 1, #f.p do f.b[k] = 1 end
  end
end

local function add_pose_bone(r)
  for _, c in ipairs(r.clips) do
    for _, key in ipairs(c.keys) do key.pose[#r.bones] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
  end
end

local function new_bone()
  local m = M()
  if not m or #m.faces == 0 then say("this model has no faces: build it in bm Studio", C.ERR); return end
  T.begin_edit()
  if not m.rig then
    new_skeleton(m)
    rg.bone = 1
    T.commit()
    say("a skeleton: one bone, root; n adds bones to it", C.ACC)
    return
  end
  local r = m.rig
  if #r.bones >= 64 then table.remove(S.undo); say("at most 64 bones", C.ERR); return end
  local p = r.bones[rg.bone]
  local dir = V.sub(p.tail, p.head)
  local l = sqrt(V.dot(dir, dir))
  dir = l > 1e-6 and V.scale(dir, 0.5 / l) or { 0, 0.5, 0 }
  local nb = { name = unique_bone(r, "bone" .. (#r.bones + 1)), parent = rg.bone, head = V.copy(p.tail),
               tail = V.add(p.tail, { round(dir[1] * 8) / 8, round(dir[2] * 8) / 8, round(dir[3] * 8) / 8 }) }
  if V.dist2(nb.tail, nb.head) < 1e-6 then nb.tail[2] = nb.tail[2] + 0.5 end
  r.bones[#r.bones + 1] = nb
  add_pose_bone(r)
  rg.bone = #r.bones
  rg.tail = true
  T.commit()
  say("new bone " .. nb.name .. ": move its tail with w a s d r f", C.ACC)
end

local function delete_bone()
  local m = M()
  local r = m and m.rig
  if not r then return end
  T.begin_edit()
  if #r.bones == 1 then
    m.rig = nil
    for _, f in ipairs(m.faces) do f.b = nil end
    T.commit()
    remesh()
    say("the skeleton is gone", C.ACC)
    return
  end
  local d = rg.bone
  local parent = r.bones[d].parent
  table.remove(r.bones, d)
  for _, b in ipairs(r.bones) do
    if b.parent == d then b.parent = parent
    elseif b.parent > d then b.parent = b.parent - 1 end
  end
  local to = parent > 0 and parent or 1
  if to > d then to = to - 1 end
  for _, f in ipairs(m.faces) do
    if f.b then
      for k = 1, #f.b do
        if f.b[k] == d then f.b[k] = to elseif f.b[k] > d then f.b[k] = f.b[k] - 1 end
      end
    end
  end
  for _, c in ipairs(r.clips) do
    for _, key in ipairs(c.keys) do table.remove(key.pose, d) end
  end
  rg.bone = clamp(rg.bone - 1, 1, #r.bones)
  T.commit()
  remesh()
end

-- the chosen bone and its children, mirrored left <-> right (x), with
-- the poses of the animations (animator.js mirrorBones)
local function mirror_bones()
  local r = rig()
  if not r then return end
  local chain, inchain = { rg.bone }, { [rg.bone] = true }
  for k, b in ipairs(r.bones) do if inchain[b.parent] then chain[#chain + 1] = k; inchain[k] = true end end
  if #r.bones + #chain > 64 then say("at most 64 bones", C.ERR); return end
  T.begin_edit()
  local map = {}
  for _, k in ipairs(chain) do
    local b = r.bones[k]
    local name = mirror_name(b.name)
    if not name then                                 -- no side yet: .L on +x
      local left = (b.head[1] + b.tail[1]) / 2 >= 0
      b.name = unique_bone(r, b.name:sub(1, 13) .. (left and ".L" or ".R"), k)
      name = mirror_name(b.name)
    end
    local copy = { name = unique_bone(r, name), parent = map[b.parent] or b.parent,
                   head = { -b.head[1], b.head[2], b.head[3] }, tail = { -b.tail[1], b.tail[2], b.tail[3] } }
    r.bones[#r.bones + 1] = copy
    map[k] = #r.bones
    for _, c in ipairs(r.clips) do
      for _, key in ipairs(c.keys) do
        local p = key.pose[k] or { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
        key.pose[#r.bones] = { q = { p.q[1], -p.q[2], -p.q[3], p.q[4] }, t = { -p.t[1], p.t[2], p.t[3] } }
      end
    end
  end
  T.commit()
  say(#chain .. " bones mirrored: the faces on the other side follow them after k or a", C.ACC, 150)
end

-- moves the chosen end of the bone: every joint at that point goes with it
-- (a chain stays joined). With rg.alone (j) and a head chosen, only this head moves: the
-- bone leaves the tail of its parent (a shoulder, a hip). The parent, the tail, the poses stay.
local function rig_move(dx, dy, dz, step)
  local r = rig()
  if not r then return end
  local fa, fs, ra, rs = T.view_axes(rg.cam.yaw)
  local d = { 0, 0, 0 }
  d[ra] = d[ra] + dx * rs * step
  d[2] = d[2] + dy * step
  d[fa] = d[fa] + dz * fs * step
  local b = r.bones[rg.bone]
  local at = V.copy(rg.tail and b.tail or b.head)
  if rg.alone and not rg.tail then
    local nh = V.add(b.head, d)
    if V.dist2(nh, b.tail) < (1 / 16) ^ 2 then say("the bone would be too short", C.ERR, 90); return end
    T.begin_edit()
    b.head = nh
    T.commit_anim()
    return
  end
  T.begin_edit()
  for _, o in ipairs(r.bones) do
    if V.dist2(o.head, at) < 1e-8 then o.head = V.add(o.head, d) end
    if V.dist2(o.tail, at) < 1e-8 then o.tail = V.add(o.tail, d) end
  end
  T.commit_anim()
end

-- Turns the bone, the bones below it and the faces that follow them round the head of the bone, for good (the
-- rest pose): a weapon on its own bone (grip) takes a new angle in every animation. o axis, i step, comma and point turn, u degrees.
local AXES = { { "x (across)", { 1, 0, 0 } }, { "y (up)", { 0, 1, 0 } }, { "z (along)", { 0, 0, 1 } } }
local STEPS = { 5, 15, 45, 90 }
rg.axis, rg.stepi = 2, 2

local function turn_part(deg)
  local r, m = rig(), M()
  if not r or not m then return end
  T.begin_edit()
  local n = T.turn_part(m, r, rg.bone, AXES[rg.axis][2], deg * pi / 180)
  T.commit()
  rg.stress = nil
  remesh()
  say(string.format("%s turned %g degrees round %s (%d corners)", r.bones[rg.bone].name, deg, AXES[rg.axis][1], n), C.ACC, 150)
end

local function choose_parent()
  local r = rig()
  if not r or rg.bone == 1 then say("the first bone has no parent", C.DIM, 60); return end
  -- not itself nor its children: a parent comes before its child in the list
  local rows = {}
  for i = 1, rg.bone - 1 do
    local b = r.bones[i]
    rows[#rows + 1] = { b.name, function()
      T.begin_edit()
      r.bones[rg.bone].parent = i
      T.commit_anim()
      say(r.bones[rg.bone].name .. " follows " .. b.name, C.ACC, 90)
    end }
  end
  T.choose("the parent of " .. r.bones[rg.bone].name, rows, max(1, r.bones[rg.bone].parent))
end

local function face_pos(i)
  local f = M().faces[i]
  local x, y, z = T.scr(T.face_center(f))
  if not x then return nil end
  local away = rg.cam.f and V.dot(T.face_normal(f), rg.cam.f) > 0.2
  return x, y, (z or 0) + (away and 4 or 0)
end

local function skin_key(k)
  local m, r = M(), rig()
  local fs = m.faces
  if k == "up" or k == "down" or k == "left" or k == "right" then
    rg.hot = T.nav(#fs, rg.hot, k, face_pos)
  elseif k == " " or k == "ok" then
    local f = rg.hot and fs[rg.hot]
    if f then rg.sel[f] = not rg.sel[f] or nil; remesh() end
  elseif k == "c" then
    local by = {}
    for _, f in ipairs(fs) do
      for _, p in ipairs(f.p) do local key = T.pkey(p); by[key] = by[key] or {}; table.insert(by[key], f) end
    end
    local todo = chosen_list() or {}
    if #todo == 0 and rg.hot then todo = { fs[rg.hot] }; rg.sel[fs[rg.hot]] = true end
    while #todo > 0 do
      local f = table.remove(todo)
      for _, p in ipairs(f.p) do
        for _, g in ipairs(by[T.pkey(p)]) do if not rg.sel[g] then rg.sel[g] = true; todo[#todo + 1] = g end end
      end
    end
    remesh()
  elseif k == "a" then
    local list = chosen_list() or (rg.hot and { fs[rg.hot] }) or {}
    if #list == 0 then say("choose faces (space), then a gives them to the bone", C.DIM, 90); return end
    T.begin_edit()
    for _, f in ipairs(list) do f.b = {}; for j = 1, #f.p do f.b[j] = rg.bone end end
    T.commit()
    rg.stress = nil
    rg.sel = {}
    remesh()
    say(#list .. " faces follow " .. r.bones[rg.bone].name, C.ACC, 90)
  elseif k == "b" then                           -- the faces of the bone, all at once
    rg.sel = {}
    local n = 0
    for _, f in ipairs(fs) do if (f.b and f.b[1] or 1) == rg.bone then rg.sel[f] = true; n = n + 1 end end
    remesh()
    say(n .. " faces follow " .. r.bones[rg.bone].name .. " (all chosen)", C.ACC, 120)
  elseif k == "pgup" then rg.bone = (rg.bone - 2) % #r.bones + 1; remesh()
  elseif k == "pgdn" then rg.bone = rg.bone % #r.bones + 1; remesh()
  elseif k == "\b" or k == "del" then rg.sel = {}; remesh()
  end
end

function rig_page.key(k)
  local m, r = M(), rig()
  if T.cam_key(rg.cam, k, 0.5, 100) then return end
  if k == "q" then rg.cam.yaw = rg.cam.yaw - pi / 8; return end
  if k == "e" then rg.cam.yaw = rg.cam.yaw + pi / 8; return end
  if k == "z" then T.aim(rg.cam, m and m.faces or {}, 2.4); return end
  if k == "n" then new_bone(); remesh(); return end
  if not r then return end
  if k == "v" then
    rg.skin = not rg.skin
    rg.sel = {}
    remesh()
    say(rg.skin and "skin: arrows choose a face, space marks it, a gives it to the bone" or "bones", C.ACC, 120)
    return
  end
  if k == "t" then
    if rg.stress then rg.stress = nil; remesh(); say("test pose off", C.ACC); return end
    if #m.faces == 0 then return end
    rg.stress = stress_test(m)
    rg.skin, rg.sel = true, {}
    remesh()
    if rg.stress.n == 0 then say("test pose: the skin holds (bent 45 degrees, each bone)", C.ACC, 150)
    else
      say(rg.stress.n .. " faces tear or stretch (red), most when " .. rg.stress.worst .. " bends: v, choose them, a", C.ERR, 240)
    end
    return
  end
  if k == "h" then
    rg.only = (rg.only == nil and "bone") or (rg.only == "bone" and "tree") or nil
    remesh()
    local name = r.bones[rg.bone].name
    say(rg.only == "bone" and ("only the faces of " .. name .. " (h: with the bones below)")
        or rg.only == "tree" and ("only " .. name .. " and the bones below (h: all)") or "all the faces", C.ACC, 120)
    return
  end
  if k == "k" or k == "K" then
    T.begin_edit(); auto_skin(m, k == "K", rg.skin and chosen_list()); T.commit()
    rg.stress = nil
    remesh()
    say(k == "K" and "every corner follows its nearest bone (smooth)" or "every face follows its nearest bone", C.ACC)
    return
  end
  if rg.skin then skin_key(k); return end
  if k == "up" then rg.bone = (rg.bone - 2) % #r.bones + 1; remesh()
  elseif k == "down" then rg.bone = rg.bone % #r.bones + 1; remesh()
  elseif k == "o" then rg.axis = rg.axis % 3 + 1; say("turn axis: " .. AXES[rg.axis][1], C.ACC, 90)
  elseif k == "i" then rg.stepi = rg.stepi % #STEPS + 1; say("turn step: " .. STEPS[rg.stepi] .. " degrees", C.ACC, 90)
  elseif k == "," or k == "." then turn_part(k == "." and STEPS[rg.stepi] or -STEPS[rg.stepi])
  elseif k == "u" then
    T.ask("turn " .. r.bones[rg.bone].name .. " and its children, degrees round " .. AXES[rg.axis][1], "15", function(t)
      local d = T.G.parse_number(t)
      if d then turn_part(d) end
    end)
  elseif k == "\t" then rg.tail = not rg.tail
  elseif k == "j" then
    rg.alone = not rg.alone
    if rg.alone then rg.tail = false end
    say(rg.alone and "head alone: w a s d r f move only this head (j: joints go together)" or "joints go together", C.ACC, 120)
  elseif k == "x" or k == "del" then delete_bone()
  elseif k == "m" then mirror_bones()
  elseif k == "p" then choose_parent()
  elseif k == "\n" then
    local b = r.bones[rg.bone]
    T.ask("bone name (up to 16 letters)", b.name, function(t)
      t = t:gsub("[%c]", "")
      if t == "" then return end
      T.begin_edit()
      b.name = unique_bone(r, t, rg.bone)
      T.commit_anim()
    end)
  else
    local moves = { a = { -1, 0, 0 }, d = { 1, 0, 0 }, w = { 0, 1, 0 }, s = { 0, -1, 0 }, r = { 0, 0, 1 }, f = { 0, 0, -1 } }
    local mv = moves[k] or moves[k:lower()]
    if mv then rig_move(mv[1], mv[2], mv[3], moves[k] and 1 / 8 or 1 / 32) end
  end
end

-- the mouse: right drag turns, middle moves, wheel zooms. Bones: a click
-- on a bone's end chooses it, the left button dragged moves it (steps of
-- 1/8 across the screen and up). Skin: the face under the pointer, a click
-- chooses it.
function rig_page.mouse()
  local view = U.inside(PANEL_W, 48, W - PANEL_W, HINT_Y - 48)
  T.mouse_cam(rg.cam, PANEL_W, 48, W - PANEL_W, HINT_Y - 48, { lo = 0.5, hi = 100 })
  local m, r = M(), rig()
  if not m or not view then rg.acc = nil; return end
  if rg.skin then
    if U.moved then rg.hot = T.face_at(m.faces, U.x, U.y) or rg.hot end
    if U.click(0) then skin_key(" ") end
    return
  end
  if not r then return end
  if U.click(0) then
    local ends = {}
    for i, b in ipairs(r.bones) do ends[#ends + 1] = { p = b.head, i = i, tail = false }; ends[#ends + 1] = { p = b.tail, i = i, tail = true } end
    local k = T.point_at(ends, U.x, U.y, 10)
    if k then rg.bone, rg.tail = ends[k].i, ends[k].tail end
    rg.acc = k and { 0, 0 } or nil
  end
  local d = U.drag
  if rg.acc and d and d.b == 0 then
    local pps = (1 / 8) * T.FOCAL / max(rg.cam.dist, 0.1)      -- pixels of a step
    rg.acc[1], rg.acc[2] = rg.acc[1] + d.dx, rg.acc[2] - d.dy
    while abs(rg.acc[1]) >= pps do
      local sx = rg.acc[1] > 0 and 1 or -1
      rig_move(sx, 0, 0, 1 / 8)
      rg.acc[1] = rg.acc[1] - sx * pps
    end
    while abs(rg.acc[2]) >= pps do
      local sy = rg.acc[2] > 0 and 1 or -1
      rig_move(0, sy, 0, 1 / 8)
      rg.acc[2] = rg.acc[2] - sy * pps
    end
  elseif not U.down(0) then
    rg.acc = nil
  end
end

function rig_page.menu_items()
  if not rig() then return { { "New skeleton", "n" } } end
  if rg.skin then
    return { { "Choose", " " }, { "Give to the bone", "a" }, { "All joined", "c" }, { "None", "\b" },
             { "Auto skin", "k" }, { "Auto skin, smooth", "K" }, { "Bones", "v" } }
  end
  return { { "New bone", "n" }, { "Rename...", "\n" }, { "Head / tail", "\t" }, { "Head alone", "j" }, { "Parent...", "p" },
           { "Mirror the bones", "m" }, { "Delete the bone", "x" }, "-", { "Skin", "v" }, { "Auto skin", "k" },
           { "Frame the model", "z" } }
end

function rig_page.pad()
  local r, rp = rig(), T.rp
  if rg.skin then
    for i, k in pairs({ [0] = "left", "right", "up", "down" }) do if rp[i] then skin_key(k) end end
    if btnp(4) then skin_key(" ") end
    if btnp(5) then skin_key("a") end
    if T.tap[6] then rig_page.key("v") end
    return
  end
  if btn(4) and r then                           -- A + the pad: move the end
    if rp[0] then rig_move(-1, 0, 0, 1 / 16) end
    if rp[1] then rig_move(1, 0, 0, 1 / 16) end
    if rp[2] then rig_move(0, 1, 0, 1 / 16) end
    if rp[3] then rig_move(0, -1, 0, 1 / 16) end
    return
  end
  if btn(6) and r then                           -- X + up/down: nearer / farther
    if rp[2] then rig_move(0, 0, 1, 1 / 16) end
    if rp[3] then rig_move(0, 0, -1, 1 / 16) end
    return
  end
  if rp[2] then rig_page.key("up") end
  if rp[3] then rig_page.key("down") end
  if rp[0] then rg.cam.yaw = rg.cam.yaw - 0.06 end
  if rp[1] then rg.cam.yaw = rg.cam.yaw + 0.06 end
  if btnp(5) then rig_page.key("\t") end
  if T.tap[6] then rig_page.key("n") end
end

function rig_page.draw()
  local m, cam = M(), rg.cam
  local r = m and m.rig
  cls(C.SKY)
  zclear()
  T.look(cam, PANEL_W / 2, 12)
  if m and #m.faces > 0 then T.draw_grid(round(cam.tx), cam.floor, round(cam.tz), 6, 0.5, C.GRID) end
  light3d(-0.4, 0.8, -0.5, 0.45)
  if (rg.skin or rg.only) and rg.mesh then draw3d(rg.mesh, 0, 0, 0, 0, 0, 0, 1, 0)
  elseif S.view then
    if r then animate(S.view) end
    draw3d(S.view, 0, 0, 0, 0, 0, 0, 1, 0)
  end
  rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
  if not m then no_model(16, 32); return end
  if not r then
    print("NO SKELETON", 16, 32, C.DIM)
    T.chip_hint("n", "X", "make one", 16, 64)
    print("(one bone, then", 16, 96, C.DIM)
    print(" n adds more)", 16, 112, C.DIM)
    T.hint({ { { "n" }, "new skeleton" }, { { "q", "e" }, "turn" }, { { "+", "-" }, "zoom" } })
    return
  end
  for i, b in ipairs(r.bones) do
    if i ~= rg.bone then T.draw_bone(b.head, b.tail, T.bone_colour(i), false) end
  end
  local b = r.bones[rg.bone]
  T.draw_bone(b.head, b.tail, C.CUR, true)
  local ex, ey = T.scr(rg.tail and b.tail or b.head)
  if ex and not rg.skin then circ(ex, ey, 6, C.HOT) end
  T.gizmo(cam, PANEL_W + 40, HINT_Y - 40)
  T.draw_list("BONES " .. #r.bones, bone_names(r), rg.bone, 0, 32, 16, PANEL_W, T.bone_colour, function(i, again)
    rg.bone = i
    if again and not rg.skin then rig_page.key("\n") end      -- a double click: its name
  end)
  local e = rg.tail and b.tail or b.head
  info_strip(2)
  if rg.skin then
    local f = rg.hot and m.faces[rg.hot]
    if f then T.outline(f, C.HOT, true) end
    local n = 0
    for _ in pairs(rg.sel) do n = n + 1 end
    print(string.format("SKIN  bone %s  %d faces chosen", b.name, n), INFO_X, 32, C.ACC)
    print(f and ("this face follows " .. (r.bones[f.b and f.b[1] or 1] or b).name) or "arrows: a face", INFO_X, 48, C.DIM)
    T.hint({ { { "up", "down", "left", "right" }, "face" }, { { "space" }, "choose" }, { { "a" }, "assign" },
             { { "pgup", "pgdn" }, "bone" }, { { "c" }, "joined" }, { { "k" }, "auto" }, { { "v" }, "bones" } })
  else
    local parent = b.parent > 0 and r.bones[b.parent].name or "-"
    print(string.format("%s of %s: %.3f, %.3f, %.3f", rg.tail and "tail" or "head", b.name, e[1], e[2], e[3]),
          INFO_X, 32, C.ACC)
    print("parent: " .. parent .. ((rg.alone and not rg.tail) and "   head alone (j)" or "") ..
          (rg.only and ("   ONLY " .. rg.only) or "") .. "   turn " .. AXES[rg.axis][1]:sub(1, 1) .. " " .. STEPS[rg.stepi], INFO_X, 48, C.DIM)
    T.hint({ { { "up", "down" }, "bone" }, { { "w", "a", "s", "d", "r", "f" }, "move" }, { { "tab" }, "head/tail" }, { { "j" }, "alone" },
             { { "n" }, "new" }, { { "m" }, "mirror" }, { { "k" }, "auto skin" }, { { "v" }, "skin" } })
  end
end
end

----------------------------------------------------------------- animate page (keyframes)
do

local an = { clip = 1, t = 0, playing = false, move = false, copied = nil, onion = false, bone = 1,
             cam = { yaw = 0.5, pitch = -0.3, dist = 4, tx = 0, ty = 1, tz = 0 } }

local ease = T.ease

-- the pose of a clip at time t (as the kernel's animate() in runtime.c)
local function sample(r, c, t)
  local n, keys = #r.bones, c.keys
  local L = c.length > 0 and c.length or 1
  if c.loop then t = t % L else t = clamp(t, 0, L) end
  local function fill(p)
    for i = #p + 1, n do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
    return p
  end
  if #keys == 1 then return fill(copy_pose(keys[1].pose)) end
  local a, b, ta, tb
  if t < keys[1].t then
    if not c.loop then return fill(copy_pose(keys[1].pose)) end
    a, b = keys[#keys], keys[1]
    ta, tb = a.t - L, b.t
  elseif t >= keys[#keys].t then
    if not c.loop then return fill(copy_pose(keys[#keys].pose)) end
    a, b = keys[#keys], keys[1]
    ta, tb = a.t, b.t + L
  else
    local k = 1
    while k + 1 <= #keys and keys[k + 1].t <= t do k = k + 1 end
    a, b = keys[k], keys[k + 1]
    ta, tb = a.t, b.t
  end
  local u = tb > ta and ease(c.mode, (t - ta) / (tb - ta)) or 0
  local out = {}
  for i = 1, n do
    local pa = a.pose[i] or { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
    local pb = b.pose[i] or pa
    out[i] = { q = Q.slerp(pa.q, pb.q, u),
               t = { pa.t[1] + (pb.t[1] - pa.t[1]) * u, pa.t[2] + (pb.t[2] - pa.t[2]) * u, pa.t[3] + (pb.t[3] - pa.t[3]) * u } }
  end
  return out
end

local function key_at(c, t)
  for i, k in ipairs(c.keys) do if abs(k.t - t) < 1e-4 then return i end end
end

local function set_key(c, t, pose)
  local i = key_at(c, t)
  if i then c.keys[i].pose = copy_pose(pose); return i end
  c.keys[#c.keys + 1] = { t = t, pose = copy_pose(pose) }
  table.sort(c.keys, function(x, y) return x.t < y.t end)
  return key_at(c, t)
end

local function clip_obj()
  local r = rig()
  return r and r.clips[an.clip]
end

local function snap_t(t) return round(t * FPS) / FPS end

anim_page = {
  id = "anim", fkey = "f3", label = "animate",
  help = {
    { "up / down", "bone" },
    { "left / right", "a frame back / on" },
    { "home / end", "the start / the end" },
    { "space", "play / stop" },
    { "w s / a d / q e", "turn the bone 15 degrees" },
    { "shift w s a d q e", "turn it 5 degrees" },
    { "g", "turn / move" },
    { "t", "IK on / off: the hand or foot goes to a target (w s a d q e move it)" },
    { "k / x", "add / delete the keyframe here" },
    { ", / .", "the keyframe before / after" },
    { "( / )", "move the keyframe a frame earlier / later" },
    { "ctrl c / ctrl v", "copy / paste the pose" },
    { "r / m", "rest bone / mirror the pose" },
    { "l / i", "loop / ease (linear, smooth, step, ease-in, ease-out)" },
    { "< / >", "shorter / longer" },
    { "o", "onion: the poses before and after" },
    { "n / ctrl d", "new / duplicate animation" },
    { "ctrl shift i", "animations from another model (by bone name)" },
    { "enter", "rename the animation" },
    { "pgup / pgdn", "the animation" },
    { "backspace", "delete the animation (twice)" },
    { "+ / -", "zoom" },
    { "alt up down left right", "orbit" },
    { "z", "frame the model" },
  },
  help_pad = {
    { "A DPAD", "turn the bone" },
    { "X LEFTRIGHT", "turn it around z" },
    { "X UPDOWN", "the animation" },
    { "B", "play / stop" },
    { "X", "keyframe" },
  },
}

function anim_page.reset()
  local r = rig()
  an.clip = r and clamp(an.clip, 1, max(1, #r.clips)) or 1
  an.t, an.playing = 0, false
  an.bone, an.for_model = 1, M()
  an.ik = nil
  T.aim(an.cam, M() and M().faces or {}, 3)
end

function anim_page.enter()
  if an.for_model ~= M() then anim_page.reset() end
end

function anim_page.refresh()
  local r = rig()
  if r then
    an.bone = clamp(an.bone, 1, #r.bones)
    an.clip = clamp(an.clip, 1, max(1, #r.clips))
  else
    an.bone, an.clip = 1, 1
  end
end

-- a name no other animation has: "walk", "walk2"... ("anim1" -> "anim2")
local function unique_clip(r, name)
  local used = {}
  for _, c in ipairs(r.clips) do used[c.name] = true end
  name = name:sub(1, 16)
  if not used[name] then return name end
  local stem = name:gsub("%d+$", "")
  if stem == "" then stem = "anim" end
  for k = 2, 999 do
    local s = tostring(k)
    local n = stem:sub(1, 16 - #s) .. s
    if not used[n] then return n end
  end
  return name
end

local function new_clip()
  local r = rig()
  if not r then say("make a skeleton first (F2)", C.ERR); return end
  if #r.clips >= 255 then say("at most 255 animations", C.ERR); return end
  T.begin_edit()
  local name = unique_clip(r, "anim" .. (#r.clips + 1))
  r.clips[#r.clips + 1] = { name = name, mode = 1, loop = true, length = 1,
                            keys = { { t = 0, pose = rest_pose(#r.bones) } } }
  an.clip, an.t = #r.clips, 0
  T.commit_anim()
  say("new animation " .. name .. ": turn the bones (w/s a/d q/e), each turn is a keyframe", C.ACC, 300)
end

-- playing stops on the frame it is at (the keys go on frames)
local function stop_play(c)
  if not an.playing then return end
  an.playing = false
  local L = max(c.length, 1e-3)
  an.t = clamp(snap_t(c.loop and an.t % L or an.t), 0, c.length)
end

-- a new pose at the current time: a keyframe there (auto key)
local function set_pose(c, pose)
  an.t = clamp(an.t, 0, c.length)
  set_key(c, an.t, pose)
  T.commit_anim()
end

-- turns (or moves) the chosen bone at the current time
local ik_move                                    -- (below) with IK on, the same keys, the pad and the mouse move the target
local function turn(axis, deg)
  local r, c = rig(), clip_obj()
  if not c then say("no animation: n makes one", C.ERR); return end
  stop_play(c)
  if an.ik then ik_move(axis, deg); return end
  T.begin_edit()
  local pose = sample(r, c, an.t)
  local b = pose[an.bone]
  if an.move then
    b.t[axis] = b.t[axis] + deg / 15 / 16
  else
    b.q = Q.norm(Q.mul(Q.axis(V.unit(axis), math.rad(deg)), b.q))
  end
  set_pose(c, pose)
end

-- Two-bone IK (APP-29; the math is T.ik_chain in bm3d). t on a bone with a parent and a grandparent (a hand,
-- a foot) puts a handle on the head of that bone: w s a d q e move its target (x, y, z, as g does for a bone)
-- and the solver keys the two bones above it at the current time; the chosen bone keeps its turn in the world.
-- The target is not saved: it is read again from the pose whenever the time, the animation, the bone or the
-- pose changes by other means (undo, a turn).
local function ik_chain_of(r, b)
  local bb = r.bones[b]
  local p = bb and bb.parent or 0
  local g = p > 0 and r.bones[p].parent or 0
  if p > 0 and g > 0 then return g, p end
end

local function ik_open(r, c, b)
  local g, p = ik_chain_of(r, b)
  if not g then return nil end
  local pose = sample(r, c, an.t)
  return { g = g, p = p, b = b, pole = T.ik_pole(r, g, p, b, pose), reach = true }
end

-- keeps an.ik true to the pose: nil when it cannot stay on
local function ik_sync(r, c)
  local ik = an.ik
  if not ik then return nil end
  if not r or not c then an.ik = nil; return nil end
  if an.bone ~= ik.b or not r.bones[ik.b] then
    an.ik = ik_open(r, c, an.bone)
    ik = an.ik
    if not ik then say("IK off: this bone has no parent and grandparent", C.DIM, 90); return nil end
    ik.t = nil
  end
  local eff = T.fk(r, sample(r, c, an.t)).head[ik.b]
  if ik.t ~= an.t or ik.clip ~= an.clip or not ik.eff or V.dist2(eff, ik.eff) > 1e-8 then
    ik.target, ik.eff, ik.t, ik.clip, ik.reach = eff, eff, an.t, an.clip, true
  end
  return ik
end

local function ik_name(r, ik)
  return r.bones[ik.g].name .. " > " .. r.bones[ik.p].name .. " > " .. r.bones[ik.b].name
end

local function ik_toggle()
  local r, c = rig(), clip_obj()
  if an.ik then an.ik = nil; say("IK off", C.DIM, 60); return end
  if not c then say("no animation: n makes one", C.ERR); return end
  an.ik = ik_open(r, c, an.bone)
  if not an.ik then
    say("IK needs a bone with a parent and a grandparent (a hand, a foot)", C.ERR, 150)
    return
  end
  ik_sync(r, c)
  say("IK on: " .. ik_name(r, an.ik) .. "  (w s a d q e move the target)", C.ACC, 150)
end

-- one step of the target (axis 1 x, 2 y, 3 z; a 15 degree turn is a 1/16 step): one undo step
function ik_move(axis, deg)
  local r, c = rig(), clip_obj()
  stop_play(c)
  local ik = ik_sync(r, c)
  if not ik then return end
  T.begin_edit()
  ik.target = { ik.target[1], ik.target[2], ik.target[3] }
  ik.target[axis] = ik.target[axis] + deg / 15 / 16
  local pose, info = T.ik_chain(r, sample(r, c, an.t), ik.g, ik.p, ik.b, ik.target, ik.pole)
  set_pose(c, pose)
  ik.eff, ik.t, ik.clip, ik.reach = info.eff, an.t, an.clip, info.reached
  if not info.reached then say("IK: out of reach, the limb is straight to the target", C.ERR, 90) end
end

-- the pose left <-> right: each bone takes its mirror's (.L / .R), mirrored
local function mirror_pose(r, c)
  local src = sample(r, c, an.t)
  local out = copy_pose(src)
  for i, b in ipairs(r.bones) do
    local other, j = mirror_name(b.name), i
    if other then for k, o in ipairs(r.bones) do if o.name == other then j = k end end end
    local p = src[j]
    out[i] = { q = { p.q[1], -p.q[2], -p.q[3], p.q[4] }, t = { -p.t[1], p.t[2], p.t[3] } }
  end
  return out
end

local function delete_clip_check(r, c)
  if T.confirm("delclip" .. an.clip, "Backspace again deletes the animation " .. c.name) then return end
  T.begin_edit()
  table.remove(r.clips, an.clip)
  an.clip = clamp(an.clip, 1, max(1, #r.clips))
  T.commit_anim()
  say("animation deleted", C.ACC)
end

-- Ctrl+Shift+I: pick a model (/bmm, /glb); its animations go onto this skeleton by bone name
-- (T.retarget), one undo step, the new names kept apart from the ones here
function anim_page.retarget_flow()
  local r = rig()
  if not r then say("make a skeleton first (F2)", C.ERR); return end
  T.import_flow(T.ui, function(item)
    if not item.ac then say("that model has no skeleton: it has no animations", C.ERR, 300); return end
    local ok, src = pcall(T.decode_rig, item.ac)
    if not ok or #src.bones == 0 or #src.clips == 0 then say("that model has no animations", C.ERR, 300); return end
    local clips, info = T.retarget(src, r)
    local room = 255 - #r.clips
    if room <= 0 then say("at most 255 animations", C.ERR); return end
    if info.matched == 0 then
      say("no bone of that model has the name of one here: nothing added", C.ERR, 300)
      return
    end
    T.begin_edit()
    local n = 0
    for i = 1, min(#clips, room) do
      clips[i].name = unique_clip(r, clips[i].name)
      r.clips[#r.clips + 1] = clips[i]
      n = n + 1
    end
    an.clip, an.t = #r.clips - n + 1, 0
    T.commit_anim()
    local text = n .. " animations, " .. info.matched .. " bones matched"
    if #info.missing > 0 then text = text .. ", missing: " .. table.concat(info.missing, " ") end
    if #text > 76 then text = text:sub(1, 73) .. "..." end
    say(text, #info.missing > 0 and C.TEXT or C.ACC, 400)
  end)
end

function anim_page.key(k)
  local r, c = rig(), clip_obj()
  if T.cam_key(an.cam, k, 0.5, 100) then return end
  if k == "n" then new_clip(); return end
  if k == "^I" then anim_page.retarget_flow(); return end
  if k == "z" then T.aim(an.cam, M() and M().faces or {}, 3); return end
  if not r then return end
  if k == "up" then an.bone = (an.bone - 2) % #r.bones + 1; return
  elseif k == "down" then an.bone = an.bone % #r.bones + 1; return
  elseif k == "pgup" and #r.clips > 0 then an.clip = (an.clip - 2) % #r.clips + 1; an.t = 0; return
  elseif k == "pgdn" and #r.clips > 0 then an.clip = an.clip % #r.clips + 1; an.t = 0; return
  end
  if not c then return end
  if k ~= " " then stop_play(c) end
  local turns = { w = { 1, 15 }, s = { 1, -15 }, a = { 2, 15 }, d = { 2, -15 }, q = { 3, 15 }, e = { 3, -15 } }
  if k == "t" then ik_toggle(); return end
  if turns[k] then turn(turns[k][1], turns[k][2]); return end
  if turns[k:lower()] then turn(turns[k:lower()][1], turns[k:lower()][2] / 3); return end
  if k == "left" then an.playing = false; an.t = max(0, snap_t(an.t) - 1 / FPS)
  elseif k == "right" then an.playing = false; an.t = min(c.length, snap_t(an.t) + 1 / FPS)
  elseif k == "home" then an.t = 0
  elseif k == "end" then an.t = c.length
  elseif k == " " then
    if an.playing then stop_play(c) else an.playing = true end
  elseif k == "," or k == "." then                       -- the keyframe before / after
    local best
    for _, key in ipairs(c.keys) do
      if k == "," and key.t < an.t - 1e-4 then best = key.t end
      if k == "." and key.t > an.t + 1e-4 and not best then best = key.t end
    end
    if best then an.t = best else say("no keyframe " .. (k == "," and "before" or "after"), C.DIM, 40) end
  elseif k == "(" or k == ")" then                       -- move the keyframe here a frame
    local i = key_at(c, an.t)
    if not i then say("no keyframe here", C.DIM, 60); return end
    local nt = snap_t(an.t) + (k == "(" and -1 or 1) / FPS
    if nt < -1e-6 or nt > c.length + 1e-6 or key_at(c, nt) then say("no room there", C.DIM, 60); return end
    T.begin_edit()
    c.keys[i].t = nt
    table.sort(c.keys, function(x, y) return x.t < y.t end)
    an.t = nt
    T.commit_anim()
  elseif k == "g" then an.move = not an.move; say(an.move and "w/s a/d q/e move the bone" or "w/s a/d q/e turn the bone", C.ACC, 90)
  elseif k == "k" then T.begin_edit(); set_pose(c, sample(r, c, an.t))
  elseif k == "x" or k == "del" then
    local i = key_at(c, an.t)
    if not i then say("no keyframe here", C.DIM, 60)
    elseif #c.keys == 1 then say("the last keyframe stays", C.DIM, 60)
    else T.begin_edit(); table.remove(c.keys, i); T.commit_anim() end
  elseif k == "^c" then an.copied = sample(r, c, an.t); say("pose copied", C.ACC, 60)
  elseif k == "^v" and an.copied then T.begin_edit(); set_pose(c, an.copied)
  elseif k == "m" then T.begin_edit(); set_pose(c, mirror_pose(r, c)); say("pose mirrored", C.ACC, 60)
  elseif k == "r" then
    T.begin_edit()
    local pose = sample(r, c, an.t)
    pose[an.bone] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } }
    set_pose(c, pose)
  elseif k == "o" then an.onion = not an.onion
  elseif k == "l" then T.begin_edit(); c.loop = not c.loop; T.commit_anim()
  elseif k == "i" then T.begin_edit(); c.mode = (c.mode + 1) % 5; T.commit_anim()
  elseif k == ">" then T.begin_edit(); c.length = snap_t(c.length) + 1 / FPS; T.commit_anim()
  elseif k == "<" then
    local last = c.keys[#c.keys].t
    local l = max(1 / FPS, last, snap_t(c.length) - 1 / FPS)
    if l < c.length - 1e-6 then T.begin_edit(); c.length = l; an.t = min(an.t, l); T.commit_anim()
    else say("the length stays past the last keyframe", C.DIM, 60) end
  elseif k == "\n" then
    T.ask("animation name (up to 16 letters)", c.name, function(t)
      t = t:gsub("[%c]", "")
      if t == "" or t == c.name then return end
      T.begin_edit()
      c.name = unique_clip(r, t)
      T.commit_anim()
    end)
  elseif k == "^d" then
    if #r.clips >= 255 then say("at most 255 animations", C.ERR); return end
    T.begin_edit()
    local o = T.deep(c)
    o.name = unique_clip(r, c.name)
    table.insert(r.clips, an.clip + 1, o)
    an.clip = an.clip + 1
    T.commit_anim()
    say("copied as " .. o.name, C.ACC, 90)
  elseif k == "\b" then delete_clip_check(r, c)
  end
end

function anim_page.pad()
  local c, rp = clip_obj(), T.rp
  if btn(4) and c then                           -- A + the pad: turn the bone
    if rp[0] then turn(2, 15) end
    if rp[1] then turn(2, -15) end
    if rp[2] then turn(1, 15) end
    if rp[3] then turn(1, -15) end
    return
  end
  if btn(6) and c then                           -- X + the pad: around z; X + up/down: the clip
    if rp[0] then turn(3, 15) end
    if rp[1] then turn(3, -15) end
    if rp[2] then anim_page.key("pgup") end
    if rp[3] then anim_page.key("pgdn") end
    return
  end
  if rp[2] then anim_page.key("up") end
  if rp[3] then anim_page.key("down") end
  if rp[0] then anim_page.key("left") end
  if rp[1] then anim_page.key("right") end
  if btnp(5) then anim_page.key(" ") end
  if T.tap[6] then if c then anim_page.key("k") else anim_page.key("n") end end
end

function anim_page.update(dt)
  local c = clip_obj()
  if an.playing and c then
    an.t = an.t + dt
    if not c.loop and an.t >= c.length then an.t, an.playing = c.length, false end
  end
end

local function draw_timeline(c)
  local x0, y0, w = PANEL_W + 16, HINT_Y - 48, W - PANEL_W - 32
  U.zone(PANEL_W, y0, W - PANEL_W, 48, "time", x0, w)
  rectfill(PANEL_W, y0, W - PANEL_W, 48, C.PANEL)
  local L = max(c.length, 1e-3)
  local nf = round(L * FPS)
  for i = 0, nf do
    local x = x0 + floor(i / max(nf, 1) * w)
    line(x, y0 + 30, x, y0 + (i % FPS == 0 and 18 or 25), C.DIM)
    if i % FPS == 0 then print(tostring(i // FPS), x + 2, y0, C.DIM) end
  end
  line(x0, y0 + 30, x0 + w, y0 + 30, C.DIM)
  for _, k in ipairs(c.keys) do
    local x = x0 + floor(k.t / L * w)
    tri(x, y0 + 30, x - 5, y0 + 36, x + 5, y0 + 36, C.ACC)
    tri(x - 5, y0 + 36, x, y0 + 42, x + 5, y0 + 36, C.ACC)
  end
  local tt = c.loop and (an.t % L) or clamp(an.t, 0, L)
  local x = x0 + floor(tt / L * w)
  line(x, y0 + 16, x, y0 + 46, 0xFFFFFF)
  return tt
end

-- the mouse: the timeline (a click or a drag puts the time there), the
-- bones (a click on one chooses it; the left button dragged turns it, across
-- the screen round y, up and down round x: a keyframe), right drag turns
-- the view, middle moves it, the wheel zooms
function anim_page.mouse()
  local r, c = rig(), clip_obj()
  local z = U.at()
  if c and z and z.kind == "time" and U.down(0) and not an.turning then
    local L = max(c.length, 1e-3)
    stop_play(c)
    an.t = clamp(snap_t((U.x - z.a) / z.b * L), 0, c.length)
    return
  end
  local view = U.inside(PANEL_W, 48, W - PANEL_W, HINT_Y - 96)
  T.mouse_cam(an.cam, PANEL_W, 48, W - PANEL_W, HINT_Y - 96, { lo = 0.5, hi = 100 })
  if not r or not S.view then return end
  if view and U.click(0) then
    local ends = {}
    for i = 1, #r.bones do
      local hx, hy, hz, tx, ty, tz = bone3d(S.view, i)
      ends[#ends + 1] = { p = { (hx + tx) / 2, (hy + ty) / 2, (hz + tz) / 2 }, i = i }
      ends[#ends + 1] = { p = { tx, ty, tz }, i = i }
    end
    local k = T.point_at(ends, U.x, U.y, 12)
    if k then an.bone = ends[k].i end
    an.turning = k and c and { 0, 0 } or nil
  end
  local d = U.drag
  if an.turning and d and d.b == 0 then
    an.turning[1], an.turning[2] = an.turning[1] + d.dx, an.turning[2] + d.dy
    while abs(an.turning[1]) >= 8 do
      local sx = an.turning[1] > 0 and 1 or -1
      turn(2, sx * 5)
      an.turning[1] = an.turning[1] - sx * 8
    end
    while abs(an.turning[2]) >= 8 do
      local sy = an.turning[2] > 0 and 1 or -1
      turn(1, sy * 5)
      an.turning[2] = an.turning[2] - sy * 8
    end
  elseif not U.down(0) then
    an.turning = nil
  end
end

function anim_page.menu_items()
  if not rig() then return { { "New animation", "n" } } end
  return { { an.playing and "Stop" or "Play", " " }, { "Keyframe", "k" }, { "Delete the keyframe", "x" },
           { an.ik and "IK off" or "IK handle", "t" }, { "Copy the pose", "^c" }, { "Paste the pose", "^v" }, { "Rest the bone", "r" }, { "Mirror the pose", "m" },
           "-", { "Move / turn", "g" }, { "Loop", "l" }, { "Ease", "i" }, { "Onion skin", "o" }, "-",
           { "New animation", "n" }, { "Duplicate", "^d" }, { "Rename...", "\n" },
           { "Animations from another model...", "^I" }, { "Frame the model", "z" } }
end

function anim_page.draw()
  local m, cam = M(), an.cam
  local r = m and m.rig
  local c = clip_obj()
  local view = S.view
  cls(C.SKY)
  zclear()
  T.look(cam, PANEL_W / 2, -12)
  if m and #m.faces > 0 then T.draw_grid(round(cam.tx), cam.floor, round(cam.tz), 6, 0.5, C.GRID) end
  light3d(-0.4, 0.8, -0.5, 0.45)
  -- onion skin: the bones of the keyframes before and after
  if view and r and c and an.onion then
    local before, after
    for _, key in ipairs(c.keys) do
      if key.t < an.t - 1e-4 then before = key.t end
      if key.t > an.t + 1e-4 and not after then after = key.t end
    end
    for _, ot in ipairs({ { before, 0x6080FF }, { after, 0xFF8060 } }) do
      if ot[1] then
        animate(view, an.clip, ot[1])
        for i = 1, #r.bones do
          local hx, hy, hz, tx, ty, tz = bone3d(view, i)
          if hx then T.draw_bone({ hx, hy, hz }, { tx, ty, tz }, ot[2], false) end
        end
      end
    end
  end
  if view then
    if r then
      if c then animate(view, an.clip, an.t) else animate(view) end
    end
    draw3d(view, 0, 0, 0, 0, 0, 0, 1, 0)
  end
  rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
  if not m then no_model(16, 32); return end
  if not r then
    print("NO SKELETON", 16, 32, C.DIM)
    print("make one in F2", 16, 64, C.TEXT)
    T.hint({ { { "f2" }, "rig: bones and skin first" } })
    return
  end
  if view then
    for i = 1, #r.bones do
      local hx, hy, hz, tx, ty, tz = bone3d(view, i)
      if hx and i ~= an.bone then T.draw_bone({ hx, hy, hz }, { tx, ty, tz }, T.bone_colour(i), false) end
    end
    local hx, hy, hz, tx, ty, tz = bone3d(view, an.bone)
    if hx then T.draw_bone({ hx, hy, hz }, { tx, ty, tz }, C.CUR, true) end
    local ik = c and ik_sync(r, c)
    if ik then                                   -- the IK handle: a cross on the target; a line to the hand when out of reach
      local col = ik.reach and C.ACC or C.ERR
      local tx2, ty2 = T.scr(ik.target)
      if tx2 then
        if not ik.reach then
          local ex, ey = T.scr(ik.eff)
          if ex then line(ex, ey, tx2, ty2, col) end
        end
        line(tx2 - 6, ty2, tx2 + 6, ty2, col); line(tx2, ty2 - 6, tx2, ty2 + 6, col)
        circ(tx2, ty2, 4, col)
      end
    end
  end
  -- the animations, then the bones
  local cn = {}
  for i, cc in ipairs(r.clips) do cn[i] = cc.name end
  local rows = clamp(#cn, 1, 4)
  T.draw_list("ANIMATIONS", cn, an.clip, 0, 32, rows, PANEL_W, nil, function(i, again)
    if i ~= an.clip then an.clip, an.t, an.playing = i, 0, false end
    if again then anim_page.key("\n") end                  -- a double click: its name
  end)
  local by = 32 + (rows + 2) * 16
  T.draw_list("BONES", bone_names(r), an.bone, 0, by, (HINT_Y - by - 16) // 16, PANEL_W, T.bone_colour,
              function(i) an.bone = i end)
  local x = INFO_X
  info_strip(c and (an.ik and 3 or 2) or 1)
  if not c then
    print("no animations yet: n makes one, ctrl shift i takes them from a model", x, 32, C.ACC)
    T.hint({ { { "n" }, "new animation" }, { { "up", "down" }, "bone" }, { { "+", "-" }, "zoom" },
             { { "ctrl", "shift", "i" }, "other anims" } })
    return
  end
  print(string.format("%s  %d/%d  %s%s  %.2f s%s", c.name, an.clip, #r.clips, T.MODES[c.mode], c.loop and "  loop" or "",
                      c.length, an.onion and "  onion" or ""), x, 32, C.ACC)
  T.gizmo(cam, PANEL_W + 40, HINT_Y - 88)
  local tt = draw_timeline(c)
  local ik = ik_sync(r, c)
  print(string.format("%s %s   %.2f s  frame %d%s", ik and "IK" or an.move and "MOVE" or "TURN", r.bones[an.bone].name, tt,
                      round(tt * FPS), key_at(c, tt) and "  key" or ""), x, 48, key_at(c, tt) and C.ACC or C.TEXT)
  if ik then
    print("IK on: " .. ik_name(r, ik) .. (ik.reach and "" or "   OUT OF REACH: straight to the target"),
          x, 64, ik.reach and C.ACC or C.ERR)
  end
  T.hint({ { { "left", "right" }, "time" }, { { "space" }, "play" }, { { "w", "s", "a", "d" }, ik and "target" or "turn" },
           { { "k" }, "key" }, ik and { { "t" }, "IK off" } or { { "ctrl", "shift", "i" }, "retarget" },
           ik and { { "ctrl", "shift", "i" }, "retarget" } or { { "t" }, "IK" } })       -- (q e: in the F12 help)
end

----------------------------------------------------------------- sprites page (the animation into the sheet)

local KEY = 0xFF00FF                             -- the background of a frame: transparent in the sprite
local SIZES = { 16, 24, 32, 48, 64, 96, 128 }
local DIRS = { 1, 2, 4, 8 }
local MAXPX = 512 * 512                          -- the pixels of one sprite sheet put

local sp = { row = 1, clip = 1, frames = 8, size = 48, dirs = 4, pitch = 30, flat = true, lit = true,
             outline = true, colours = 0, fit = 1, boxes = true, t = 0, dir = 0, job = nil, last = nil, frame_boxes = nil }

local function sp_clips()
  local r = rig()
  return r and r.clips or {}
end

local ROWS = {
  { "animation", function(d)
      local n = #sp_clips()
      if n > 0 then sp.clip = (sp.clip - 1 + d) % n + 1 end
    end, function() local c = sp_clips()[sp.clip]; return c and c.name or "(the rest pose)" end },
  { "frames", function(d) sp.frames = clamp(sp.frames + d, 1, 16) end, function() return tostring(sp.frames) end },
  { "size", function(d)
      local i = 1
      for n, s in ipairs(SIZES) do if s == sp.size then i = n end end
      sp.size = SIZES[clamp(i + d, 1, #SIZES)]
    end, function() return sp.size .. " x " .. sp.size end },
  { "directions", function(d)
      local i = 1
      for n, s in ipairs(DIRS) do if s == sp.dirs then i = n end end
      sp.dirs = DIRS[clamp(i + d, 1, #DIRS)]
    end, function() return tostring(sp.dirs) end },
  { "from above", function(d) sp.pitch = clamp(sp.pitch + d * 15, 0, 75) end, function() return sp.pitch .. " deg" end },
  { "camera", function() sp.flat = not sp.flat end, function() return sp.flat and "flat" or "perspective" end },
  { "light", function() sp.lit = not sp.lit end, function() return sp.lit and "on" or "off (flat colours)" end },
  { "outline", function() sp.outline = not sp.outline end, function() return sp.outline and "dark" or "none" end },
  { "colours", function(d)
      local list = { 0, 32, 16, 8 }
      local i = 1
      for n, v in ipairs(list) do if v == sp.colours then i = n end end
      sp.colours = list[clamp(i + d, 1, #list)]
    end, function() return sp.colours == 0 and "all" or tostring(sp.colours) end },
  { "fill", function(d) sp.fit = clamp(sp.fit + d * 0.1, 0.5, 1.6) end, function() return round(sp.fit * 100) .. "%" end },
  { "hitboxes", function() sp.boxes = not sp.boxes end, function() return sp.boxes and "from bones" or "none" end },
}

local function bone_box_kind(name)
  local s = name:lower()
  if s:match("hit") or s:match("weapon") or s:match("punch") or s:match("fist") or
     s:match("sword") or s:match("blade") or s:match("spear") or s:match("attack") then
    return "hit", 1
  elseif s:match("root") or s:match("body") or s:match("hips") or s:match("pelvis") then
    return "body", 2
  else
    return "hurt", 0
  end
end

local function box_colour(kind)
  if kind == "hurt" or kind == 0 then return 0x40A0FF
  elseif kind == "hit" or kind == 1 then return 0xFF4040
  elseif kind == "body" or kind == 2 then return 0x40FF40
  else return 0xFFFF40
  end
end

local function bone_boxes(view, box, cx, cy, cz, ang, x, z)
  local m = M()
  if not m or not m.rig or #m.rig.bones == 0 or not bone3d or not project3d then return {} end
  local co, si = cos(ang), sin(ang)
  local out = {}
  local pad = max(2, round(box[3] * 0.05))
  for i = 1, #m.rig.bones do
    local b = m.rig.bones[i]
    local hx, hy, hz, tx, ty, tz = bone3d(view, i)
    if hx then
      local whx = (hx * co + hz * si) + x
      local why = hy - cy
      local whz = (-hx * si + hz * co) + z
      local wtx = (tx * co + tz * si) + x
      local wty = ty - cy
      local wtz = (-tx * si + tz * co) + z
      local shx, shy = project3d(whx, why, whz)
      local stx, sty = project3d(wtx, wty, wtz)
      if shx and stx then
        local bx0 = min(shx, stx) - pad
        local by0 = min(shy, sty) - pad
        local bx1 = max(shx, stx) + pad
        local by1 = max(shy, sty) + pad
        local rx0 = round(bx0 - box[1])
        local ry0 = round(by0 - box[2])
        local rw = max(2, round(bx1 - bx0))
        local rh = max(2, round(by1 - by0))
        local kname, knum = bone_box_kind(b.name)
        out[#out + 1] = {
          x = rx0, y = ry0, w = rw, h = rh,
          kind = kname, kind_num = knum,
          bone = b.name
        }
      end
    end
  end
  return out
end

-- the time of frame f (0-based): spread over the animation (a loop does
-- not repeat its first frame at the end)
local function frame_time(c, f)
  if not c then return 0 end
  local n = sp.frames
  if c.loop then return c.length * f / n end
  return n > 1 and c.length * f / (n - 1) or 0
end

-- the model drawn in a box of the screen: its middle in the middle of the
-- box, turned `dir` steps of the directions; the camera from the front and
-- above (the same for every frame)
local function render(view, box, dir, t)
  local m = M()
  local c = sp_clips()[sp.clip]
  if m.rig then
    if c then animate(view, sp.clip, t) else animate(view) end
  end
  local x0, y0, z0, x1, y1, z1 = bounds3d(view)
  local cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
  local ext = max(x1 - x0, y1 - y0, z1 - z0, 0.25) * 1.25 / sp.fit
  local fov = sp.flat and 10 or 40                  -- (camera3d: at least 10 degrees)
  local focal = (W / 2) / math.tan(math.rad(fov / 2))
  local d = focal * ext / box[3]
  local pitch = -math.rad(sp.pitch)
  -- the camera looks at the model's middle; the box's middle is (bx, by)
  local bx, by = box[1] + box[3] / 2, box[2] + box[4] / 2
  local f = { 0, sin(pitch), cos(pitch) }
  local u = { 0, cos(pitch), -sin(pitch) }
  local a, b = -(bx - W / 2) * d / focal, (by - H / 2) * d / focal
  camera3d(-f[1] * d + a, -f[2] * d + u[2] * b, -f[3] * d + u[3] * b, 0, pitch, fov)
  light3d(-0.4, 0.8, -0.5, sp.lit and 0.4 or 1)
  local ang = dir * 2 * pi / sp.dirs
  local co, si = cos(ang), sin(ang)
  -- turn around the middle of the model: move it there, then turn
  local x = -(cx * co + cz * si)
  local z = -(-cx * si + cz * co)
  clip(box[1], box[2], box[3], box[4])
  rectfill(box[1], box[2], box[3], box[4], KEY)
  zclear()
  draw3d(view, x, -cy, z, 0, ang, 0, 1, sp.lit and 0 or 2)
  clip()
  sp.frame_boxes = bone_boxes(view, box, cx, cy, cz, ang, x, z)
end

-- where the sprites go: below what the sheet already has (or a taller sheet)
local function place(w, h)
  local sw, sh = cart_sheet()
  local used, budget = 0, 65536
  -- the lowest row with something on it (from the bottom, up to a limit)
  for y = sh - 1, 0, -1 do
    local any = false
    for x = 0, sw - 1 do if sget(x, y) then any = true; break end end
    budget = budget - sw
    if any then used = y + 1; break end
    if budget <= 0 then used = y; break end
  end
  local y = (used + 7) // 8 * 8
  local nw, nh = max(sw, (w + 7) // 8 * 8), max(sh, y + (h + 7) // 8 * 8)
  if nw > 4096 or nh > 4096 then return nil end
  if nw ~= sw or nh ~= sh then cart_sheet(nw, nh) end
  return 0, y, nw ~= sw or nh ~= sh
end

-- outline and fewer colours, then into the sheet; one direction a frame
-- (the Lua of a frame has a budget)
local function nearest(c, pal)
  local r, g, b = c >> 16 & 255, c >> 8 & 255, c & 255
  local best, bd = pal[1], 1e18
  for _, p in ipairs(pal) do
    local dr, dg, db = r - (p >> 16 & 255), g - (p >> 8 & 255), b - (p & 255)
    local d = dr * dr * 3 + dg * dg * 4 + db * db * 2
    if d < bd then best, bd = p, d end
  end
  return best
end

local function register_zone_boxes(zn, zx, zy, zw, zh, zframes, zfps, bxs)
  if not cart_data then return false end
  -- 1. SPRITES section (11)
  local cur_sprites = cart_data(11)
  local zlist = {}
  local keep_slices, keep_down = 0, 0   -- slices of the zone that is replaced stay (and their direction)
  if cur_sprites and #cur_sprites >= 4 then
    local n = sunpack("<I2", cur_sprites, 1)
    for i = 0, n - 1 do
      local off = 5 + i * 28
      local name, x, y, w, h, frames, fps, slices, down = sunpack("<c16I2I2I2I2BBBB", cur_sprites, off)
      name = name:match("^[^\0]*")
      if name == zn then keep_slices, keep_down = slices, down & 1 end
      if name ~= zn then
        zlist[#zlist + 1] = { name = name, x = x, y = y, w = w, h = h, frames = frames, fps = fps, slices = slices, down = down & 1 }
      end
    end
  end
  -- keep the old slices only if the re-baked zone still fits the sheet
  local sheet_w, sheet_h = cart_sheet()
  if keep_slices > 0 and sheet_w then
    local ww, hh = zw * max(zframes, keep_slices), zh   -- stack: slices to the right
    if keep_down == 1 then ww, hh = zw * zframes, zh * keep_slices end   -- stackv: slices going down
    if zx + ww > sheet_w or (sheet_h and zy + hh > sheet_h) then keep_slices, keep_down = 0, 0 end
  end
  zlist[#zlist + 1] = { name = zn, x = zx, y = zy, w = zw, h = zh, frames = zframes, fps = zfps, slices = keep_slices, down = keep_down }
  local zparts = { spack("<I2I2", #zlist, 0) }
  for _, z in ipairs(zlist) do
    zparts[#zparts + 1] = spack("<c16I2I2I2I2BBBB", z.name, z.x, z.y, z.w, z.h, z.frames, z.fps, z.slices, z.down)
  end
  cart_data(11, table.concat(zparts))

  -- 2. BOXES section (14)
  local cur_boxes = cart_data(14)
  local blist = {}
  if cur_boxes and #cur_boxes >= 4 then
    local n = sunpack("<I2", cur_boxes, 1)
    for i = 0, n - 1 do
      local off = 5 + i * 28
      local zname, frame, kind, bx, by, bw, bh = sunpack("<c16BBi2i2I2I2", cur_boxes, off)
      zname = zname:match("^[^\0]*")
      if zname ~= zn then
        blist[#blist + 1] = { zone = zname, frame = frame, kind = kind, x = bx, y = by, w = bw, h = bh }
      end
    end
  end
  for _, b in ipairs(bxs) do
    blist[#blist + 1] = { zone = zn, frame = b.frame, kind = b.kind, x = b.x, y = b.y, w = b.w, h = b.h }
  end
  if #blist > 0 then
    local bparts = { spack("<I2I2", #blist, 0) }
    for _, b in ipairs(blist) do
      bparts[#bparts + 1] = spack("<c16BBi2i2I2I2xx", b.zone, b.frame, b.kind, b.x, b.y, b.w, b.h)
    end
    cart_data(14, table.concat(bparts))
  end
  return true
end

local function start_job()
  local m = M()
  if not m or not S.view then say("no model to draw", C.ERR); return end
  local w, h = sp.size * sp.frames, sp.size * sp.dirs
  if w * h > MAXPX then say(string.format("%dx%d pixels: too many, fewer frames or a smaller size", w, h), C.ERR, 150); return end
  sp.job = { dir = 0, w = w, h = h, px = {}, boxes = {}, phase = "draw" }
  say("drawing the sprites...", C.ACC, 60)
end

-- a step of the job, from _draw (the drawing needs the screen)
local function job_step()
  local j = sp.job
  local view = S.view
  local s = sp.size
  local c = sp_clips()[sp.clip]
  if j.phase == "draw" then
    local d = j.dir
    for f = 0, sp.frames - 1 do
      local box = { 0, 0, s, s }
      render(view, box, d, frame_time(c, f))
      if sp.boxes and sp.frame_boxes then
        j.boxes[d * sp.frames + f] = sp.frame_boxes
      end
      for y = 0, s - 1 do
        local row = (d * s + y) * j.w + f * s
        for x = 0, s - 1 do
          local p = pget(x, y)
          if p ~= KEY then j.px[row + x] = p end
        end
      end
    end
    j.dir = d + 1
    if j.dir >= sp.dirs then j.phase, j.dir = "outline", 0 end
  elseif j.phase == "outline" then
    if sp.outline then
      local add = {}
      for y = 0, j.h - 1 do
        for x = 0, j.w - 1 do
          local i = y * j.w + x
          if not j.px[i] then
            local fx, fy = x % s, y % s
            if (fx > 0 and j.px[i - 1]) or (fx < s - 1 and j.px[i + 1]) or
               (fy > 0 and j.px[i - j.w]) or (fy < s - 1 and j.px[i + j.w]) then
              add[#add + 1] = i
            end
          end
        end
      end
      for _, i in ipairs(add) do j.px[i] = 0x101018 end
    end
    j.phase = "colours"
  elseif j.phase == "colours" then
    if sp.colours > 0 then
      local count, list = {}, {}
      for _, p in pairs(j.px) do
        if not count[p] then count[p] = 0; list[#list + 1] = p end
        count[p] = count[p] + 1
      end
      if #list > sp.colours then
        table.sort(list, function(a, b) return count[a] > count[b] end)
        local pal = {}
        for i = 1, sp.colours do pal[i] = list[i] end
        local map = {}
        for _, p in ipairs(list) do map[p] = nearest(p, pal) end
        for i, p in pairs(j.px) do j.px[i] = map[p] end
      end
    end
    j.phase = "put"
  elseif j.phase == "put" then
    local x0, y0, grew = place(j.w, j.h)
    if not x0 then say("the sheet would pass 4096 pixels: not put", C.ERR, 200); sp.job = nil; return end
    local undo, n = {}, 0
    for i, p in pairs(j.px) do
      local x, y = x0 + i % j.w, y0 + i // j.w
      n = n + 1
      if n <= 100000 then undo[#undo + 1], undo[#undo + 2], undo[#undo + 3] = x, y, sget(x, y) or false end
      sset(x, y, p)
    end
    if n <= 100000 then T.sheet_undo(undo) end
    S.dirty, S.sheet_dirty = true, true
    S.proj.sheet_w, S.proj.sheet_h = cart_sheet()
    local name = c and c.name or "rest"
    local zn = (M().name .. "_" .. name):lower():gsub("[^%w_]", "_"):sub(1, 16)
    local len = c and max(c.length, 0.1) or 1
    local fps = round(sp.frames / len)
    local bxs = {}
    if sp.boxes and j.boxes then
      for f = 0, sp.frames - 1 do
        local f_boxes = j.boxes[f] or {}
        for _, b in ipairs(f_boxes) do
          bxs[#bxs + 1] = { frame = f + 1, kind = b.kind_num, x = b.x, y = b.y, w = b.w, h = b.h }
        end
      end
    end
    register_zone_boxes(zn, x0, y0, s, s, sp.frames, fps, bxs)
    sp.last = { string.format("-- %s %s: %d frames x %d directions, %dx%d at (%d,%d)", M().name, name, sp.frames,
                              sp.dirs, s, s, x0, y0),
                string.format("zone: \"%s\" (%d fps, %d boxes)  zspr(\"%s\", x, y, f)", zn, fps, #bxs, zn),
                string.format("sspr(%d + f * %d, %d + d * %d, %d, %d, x, y)", x0, s, y0, s, s, s) }
    say(string.format("%d sprites%s put in sheet at %d,%d%s", sp.frames * sp.dirs,
                      #bxs > 0 and (" and " .. #bxs .. " boxes") or "", x0, y0,
                      grew and " (the sheet is bigger now)" or ""), C.ACC, 300)
    sp.job = nil
  end
end

sprites_page = {
  id = "sprites", fkey = "f4", label = "sprites",
  help = {
    { "up / down", "a setting" },
    { "left / right", "change it" },
    { "enter", "put the sprites in the sheet (saved with ctrl s)" },
  },
  help_pad = {
    { "A", "put the sprites in the sheet" },
  },
}

function sprites_page.reset() sp.clip = 1; sp.last = nil end
function sprites_page.refresh() sp.clip = clamp(sp.clip, 1, max(1, #sp_clips())) end
function sprites_page.modal() return sp.job ~= nil end

function sprites_page.key(k)
  if sp.job then return end
  if k == "up" then sp.row = (sp.row - 2) % #ROWS + 1
  elseif k == "down" then sp.row = sp.row % #ROWS + 1
  elseif k == "left" or k == "right" then ROWS[sp.row][2](k == "right" and 1 or -1)
  elseif k == "\n" then start_job()
  end
end

-- the mouse: a click chooses a row, the wheel changes its value (a click
-- on the value's right half: more, left half: less)
function sprites_page.mouse()
  if sp.job then return end
  local z = U.at()
  if not z or z.kind ~= "sprow" then return end
  if U.wheel ~= 0 then sp.row = z.a; ROWS[sp.row][2](U.wheel > 0 and 1 or -1) end
  if U.click(0) then
    if sp.row == z.a and U.x >= 128 then ROWS[sp.row][2](U.x >= 224 and 1 or -1) end
    sp.row = z.a
  end
end

function sprites_page.menu_items()
  if sp.job then return {} end
  return { { "Draw the sprites", "\n" } }
end

function sprites_page.pad()
  local rp = T.rp
  if rp[2] then sprites_page.key("up") end
  if rp[3] then sprites_page.key("down") end
  if rp[0] then sprites_page.key("left") end
  if rp[1] then sprites_page.key("right") end
  if btnp(4) then sprites_page.key("\n") end
end

function sprites_page.update(dt)
  sp.t = sp.t + dt
  local c = sp_clips()[sp.clip]
  local len = c and max(c.length, 0.1) or 1
  if sp.t > len * 2 then sp.t = 0; sp.dir = (sp.dir + 1) % sp.dirs end
end

function sprites_page.draw()
  if sp.job then job_step() end
  local m = M()
  cls(C.BG)
  -- the preview: the frame of now, at its size and twice as big
  local s = sp.size
  local view = S.view
  local px, py = 336, 48
  if view and m then
    local c = sp_clips()[sp.clip]
    local f = c and floor((sp.t % max(c.length, 1e-3)) / max(c.length, 1e-3) * sp.frames) or 0
    local box = { px, py, s, s }
    rectfill(px - 1, py - 1, s + 2, s + 2, C.DIM)
    render(view, box, sp.dir % sp.dirs, frame_time(c, f))
    -- the key colour shows as the panel's; twice as big beside it
    local zx, big = px + s + 16, s <= 64
    -- the zoom: 2 at 640x360, as big as the room right of the sprite and under it allows
    local z = clamp(min((W - zx - 16) // s, (HINT_Y - 48 - 64 - py) // s), 2, 12)
    if big then rectfill(zx, py, s * z, s * z, C.PANEL) end
    for y = 0, s - 1 do
      for x = 0, s - 1 do
        local p = pget(px + x, py + y)
        if p == KEY then pset(px + x, py + y, C.PANEL)
        elseif big then rectfill(zx + x * z, py + y * z, z, z, p) end
      end
    end
    if sp.boxes and sp.frame_boxes then
      for _, b in ipairs(sp.frame_boxes) do
        local col = box_colour(b.kind)
        rect(px + b.x, py + b.y, b.w, b.h, col)
        if big then
          rect(zx + b.x * z, py + b.y * z, b.w * z, b.h * z, col)
        end
      end
    end
    local nbx = (sp.boxes and sp.frame_boxes and #sp.frame_boxes > 0) and ("  " .. #sp.frame_boxes .. " boxes") or ""
    print(string.format("direction %d/%d  frame %d/%d%s", sp.dir % sp.dirs + 1, sp.dirs, f + 1, sp.frames, nbx), px,
          (py + (big and s * z or s) + 23) // 16 * 16, C.DIM)
  end
  rectfill(0, 16, 320, HINT_Y - 16, C.PANEL)
  print("SPRITES", 16, 32, C.DIM)
  print(m and m.name or "-", 96, 32, C.ACC)
  for i, row in ipairs(ROWS) do
    local y = 64 + (i - 1) * 16
    U.zone(0, y, 320, 16, "sprow", i)
    if i == sp.row then rectfill(0, y, 320, 16, C.SEL) end
    print(row[1], 16, y, C.DIM)
    print(row[3](), 128, y, i == sp.row and 0xFFFFFF or C.TEXT)
  end
  local w, h = s * sp.frames, s * sp.dirs
  print(string.format("%dx%d pixels in the sheet", w, h), 16, 64 + #ROWS * 16 + 16, w * h > MAXPX and C.ERR or C.DIM)
  if sp.job then
    print("drawing... " .. sp.job.phase, 16, 64 + #ROWS * 16 + 32, C.ACC)
  elseif sp.last then
    rectfill(0, HINT_Y - 48, W, 48, C.BAR)
    for i, l in ipairs(sp.last) do print(l:sub(1, W // 8 - 1), 0, HINT_Y - 48 + (i - 1) * 16, i == 2 and C.OK or C.DIM) end
  end
  if not m then no_model(336, 48) end
  T.hint({ { { "up", "down" }, "setting" }, { { "left", "right" }, "change" }, { { "enter" }, "put in the sheet" } })
end
end

----------------------------------------------------------------- the program

-- the screen changed size (1280x720 where the GPU draws): what was cut from W and H again
local function relayout(w, h)
  W, H, HINT_Y = w, h, T.HINT_Y
end

T.run({
  name = "bm Animator",
  resize = relayout,
  empty_model = false,
  page_list = { play, rig_page, anim_page, sprites_page },
  hello = function(files)
    return files and "open a .bm with 3D models (made in bm Studio)" or "no .bm files: make models in bm Studio"
  end,
  menu = function(items)
    items[#items + 1] = { "Open in bm Studio", function() T.open_in("studio", "bm Studio") end }
  end,
  menu_info = function(x, y)
    print("the models are bm Studio's:", x, y, C.DIM)
    print("menu: Open in bm Studio", x, y + 16, C.DIM)
    print("the same files as bm Mesh,", x, y + 48, C.DIM)
    print("bm Pixel and the bm SDK", x, y + 64, C.DIM)
  end,
})
