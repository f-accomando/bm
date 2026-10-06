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
local clamp, round = T.clamp, T.round
local M, say = T.M, T.say
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

-- the faces coloured by their bones (shared corners), as a mesh; the
-- chosen faces brighter
local function skin_mesh(m, chosen)
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
    local ids = {}
    for k, p in ipairs(fc.p) do ids[k] = vid(p) end
    local col = T.bone_colour(fc.b and fc.b[1] or 1)
    if chosen and chosen[fc] then col = 0xFFFFFF end
    for _, t in ipairs(#fc.p == 4 and T.QUAD or T.TRI) do
      local a, b, c = ids[t[1]], ids[t[2]], ids[t[3]]
      if a ~= b and b ~= c and a ~= c then
        f[#f + 1], f[#f + 2], f[#f + 3], f[#f + 4] = a, b, c, col
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
  T.draw_list("MODELS", names, S.cur, 0, 32, rows)
  if #pl_clips > 0 then
    local cn = {}
    for i, cc in ipairs(pl_clips) do cn[i] = cc.name end
    local y = 32 + (rows + 2) * 16
    T.draw_list("ANIMATIONS", cn, pl.clip, 0, y, max(1, min(#cn, (HINT_Y - y - 16) // 16)))
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

local function remesh()
  rg.mesh = rg.skin and M() and skin_mesh(M(), rg.sel) or nil
end

rig_page = {
  id = "rig", fkey = "f2", label = "rig",
  help = {
    { "up / down", "bone" },
    { "w a s d r f", "move its end" },
    { "shift w a s d r f", "move it a little" },
    { "tab", "head / tail" },
    { "n", "new bone (child of this one)" },
    { "x / del", "delete the bone" },
    { "m", "mirror it and its children (.L / .R)" },
    { "enter / p", "rename / parent" },
    { "k / shift k", "auto skin: rigid parts / smooth" },
    { "v", "skin: the faces and their bones" },
    { "q / e", "turn the view" },
    { "+ / -", "zoom" },
    { "z", "frame the model" },
    "skin (v)",
    { "up down left right", "a face" },
    { "space", "choose" },
    { "c / a", "all joined / assign to the bone" },
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
  rg.bone, rg.hot, rg.sel, rg.for_model = 1, nil, {}, M()
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
-- (a chain stays joined)
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
  T.begin_edit()
  for _, o in ipairs(r.bones) do
    if V.dist2(o.head, at) < 1e-8 then o.head = V.add(o.head, d) end
    if V.dist2(o.tail, at) < 1e-8 then o.tail = V.add(o.tail, d) end
  end
  T.commit_anim()
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
    rg.sel = {}
    remesh()
    say(#list .. " faces follow " .. r.bones[rg.bone].name, C.ACC, 90)
  elseif k == "pgup" then rg.bone = (rg.bone - 2) % #r.bones + 1
  elseif k == "pgdn" then rg.bone = rg.bone % #r.bones + 1
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
  if k == "k" or k == "K" then
    T.begin_edit(); auto_skin(m, k == "K", rg.skin and chosen_list()); T.commit()
    remesh()
    say(k == "K" and "every corner follows its nearest bone (smooth)" or "every face follows its nearest bone", C.ACC)
    return
  end
  if rg.skin then skin_key(k); return end
  if k == "up" then rg.bone = (rg.bone - 2) % #r.bones + 1
  elseif k == "down" then rg.bone = rg.bone % #r.bones + 1
  elseif k == "\t" then rg.tail = not rg.tail
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
  if rg.skin and rg.mesh then draw3d(rg.mesh, 0, 0, 0, 0, 0, 0, 1, 0)
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
  T.draw_list("BONES " .. #r.bones, bone_names(r), rg.bone, 0, 32, 16, PANEL_W, T.bone_colour)
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
    print("parent: " .. parent, INFO_X, 48, C.DIM)
    T.hint({ { { "up", "down" }, "bone" }, { { "w", "a", "s", "d", "r", "f" }, "move" }, { { "tab" }, "head/tail" },
             { { "n" }, "new" }, { { "m" }, "mirror" }, { { "k" }, "auto skin" }, { { "v" }, "skin" } })
  end
end
end

----------------------------------------------------------------- animate page (keyframes)
do

local an = { clip = 1, t = 0, playing = false, move = false, copied = nil, onion = false, bone = 1,
             cam = { yaw = 0.5, pitch = -0.3, dist = 4, tx = 0, ty = 1, tz = 0 } }

local function ease(mode, u)
  if mode == 2 then return 0 end
  if mode == 1 then return u * u * (3 - 2 * u) end
  return u
end

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
    { "k / x", "add / delete the keyframe here" },
    { ", / .", "the keyframe before / after" },
    { "( / )", "move the keyframe a frame earlier / later" },
    { "ctrl c / ctrl v", "copy / paste the pose" },
    { "r / m", "rest bone / mirror the pose" },
    { "l / i", "loop / ease (linear, smooth, step)" },
    { "< / >", "shorter / longer" },
    { "o", "onion: the poses before and after" },
    { "n / ctrl d", "new / duplicate animation" },
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
local function turn(axis, deg)
  local r, c = rig(), clip_obj()
  if not c then say("no animation: n makes one", C.ERR); return end
  stop_play(c)
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

function anim_page.key(k)
  local r, c = rig(), clip_obj()
  if T.cam_key(an.cam, k, 0.5, 100) then return end
  if k == "n" then new_clip(); return end
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
  elseif k == "i" then T.begin_edit(); c.mode = (c.mode + 1) % 3; T.commit_anim()
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
  end
  -- the animations, then the bones
  local cn = {}
  for i, cc in ipairs(r.clips) do cn[i] = cc.name end
  local rows = clamp(#cn, 1, 4)
  T.draw_list("ANIMATIONS", cn, an.clip, 0, 32, rows, PANEL_W)
  local by = 32 + (rows + 2) * 16
  T.draw_list("BONES", bone_names(r), an.bone, 0, by, (HINT_Y - by - 16) // 16, PANEL_W, T.bone_colour)
  local x = INFO_X
  info_strip(c and 2 or 1)
  if not c then
    print("no animations yet: n makes one", x, 32, C.ACC)
    T.hint({ { { "n" }, "new animation" }, { { "up", "down" }, "bone" }, { { "+", "-" }, "zoom" } })
    return
  end
  print(string.format("%s  %d/%d  %s%s  %.2f s%s", c.name, an.clip, #r.clips, T.MODES[c.mode], c.loop and "  loop" or "",
                      c.length, an.onion and "  onion" or ""), x, 32, C.ACC)
  T.gizmo(cam, PANEL_W + 40, HINT_Y - 88)
  local tt = draw_timeline(c)
  print(string.format("%s %s   %.2f s  frame %d%s", an.move and "MOVE" or "TURN", r.bones[an.bone].name, tt,
                      round(tt * FPS), key_at(c, tt) and "  key" or ""), x, 48, key_at(c, tt) and C.ACC or C.TEXT)
  T.hint({ { { "left", "right" }, "time" }, { { "space" }, "play" }, { { "w", "s", "a", "d", "q", "e" }, "turn" },
           { { "k" }, "key" }, { { ",", "." }, "keys" }, { { "m" }, "mirror" }, { { "o" }, "onion" } })
end

----------------------------------------------------------------- sprites page (the animation into the sheet)

local KEY = 0xFF00FF                             -- the background of a frame: transparent in the sprite
local SIZES = { 16, 24, 32, 48, 64, 96, 128 }
local DIRS = { 1, 2, 4, 8 }
local MAXPX = 512 * 512                          -- the pixels of one sprite sheet put

local sp = { row = 1, clip = 1, frames = 8, size = 48, dirs = 4, pitch = 30, flat = true, lit = true,
             outline = true, colours = 0, fit = 1, t = 0, dir = 0, job = nil, last = nil }

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
}

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

local function start_job()
  local m = M()
  if not m or not S.view then say("no model to draw", C.ERR); return end
  local w, h = sp.size * sp.frames, sp.size * sp.dirs
  if w * h > MAXPX then say(string.format("%dx%d pixels: too many, fewer frames or a smaller size", w, h), C.ERR, 150); return end
  sp.job = { dir = 0, w = w, h = h, px = {}, phase = "draw" }
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
    sp.last = { string.format("-- %s %s: %d frames x %d directions, %dx%d at (%d,%d)", M().name, name, sp.frames,
                              sp.dirs, s, s, x0, y0),
                string.format("sspr(%d + f * %d, %d + d * %d, %d, %d, x, y)", x0, s, y0, s, s, s),
                "f: the frame (0-" .. (sp.frames - 1) .. "), d: the direction (0-" .. (sp.dirs - 1) .. ")" }
    say(string.format("%d sprites put in the sheet at %d,%d%s", sp.frames * sp.dirs, x0, y0,
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
    if big then rectfill(zx, py, s * 2, s * 2, C.PANEL) end
    for y = 0, s - 1 do
      for x = 0, s - 1 do
        local p = pget(px + x, py + y)
        if p == KEY then pset(px + x, py + y, C.PANEL)
        elseif big then rectfill(zx + x * 2, py + y * 2, 2, 2, p) end
      end
    end
    print(string.format("direction %d/%d  frame %d/%d", sp.dir % sp.dirs + 1, sp.dirs, f + 1, sp.frames), px,
          (py + (big and s * 2 or s) + 23) // 16 * 16, C.DIM)
  end
  rectfill(0, 16, 320, HINT_Y - 16, C.PANEL)
  print("SPRITES", 16, 32, C.DIM)
  print(m and m.name or "-", 96, 32, C.ACC)
  for i, row in ipairs(ROWS) do
    local y = 64 + (i - 1) * 16
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
    for i, l in ipairs(sp.last) do print(l:sub(1, 79), 0, HINT_Y - 48 + (i - 1) * 16, i == 2 and C.OK or C.DIM) end
  end
  if not m then no_model(336, 48) end
  T.hint({ { { "up", "down" }, "setting" }, { { "left", "right" }, "change" }, { { "enter" }, "put in the sheet" } })
end
end

----------------------------------------------------------------- the program

T.run({
  name = "bm Animator",
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
