-- bm Studio on the console: the 3D models of a .bm, built with tiles and
-- blocks (as bm Studio on the PC, in the style of Crocotile 3D).
-- F1 build (block, tile, select, vertex, paint), F2 models, Esc menu;
-- Ctrl+S save, F5 try the game, Ctrl+Z / Ctrl+Y undo / redo, [ ] model.
-- Hold F12 (or ?) for the keys of the page. Gamepad: Y + left/right page,
-- Y + B menu, Y + up/down model, Y + A undo.
-- The skeletons and animations are bm Animator's (menu: Open in bm
-- Animator); the shared code (MESH and ANIM sections, project, menu) is
-- the kernel's library bm3d (src/script/bm3d.lua).

local T = require "bm3d"
local S, C, V = T.S, T.C, T.V
local W, H = T.W, T.H
local HINT_Y = T.HINT_Y
local floor, abs, sqrt, max, min, pi = math.floor, math.abs, math.sqrt, math.max, math.min, math.pi
local clamp, round = T.clamp, T.round
local M, say = T.M, T.say

-- the Lua of a new project: the viewer of bm Studio (the same text, so the
-- PC tools know it is theirs to replace)
local VIEWER = [[
-- bm Studio: viewer of the 3D models of this cartridge.
-- Left / right: model; up / down: closer / farther; A: light; B: spin;
-- X: the next animation (models with a skeleton, from bm Animator).
-- Replace it with your game: model("name") gives a model as a mesh for
-- draw3d(), models() the list of names, bounds3d(mesh) its size,
-- animate(mesh, "walk", t) the pose of an animation at time t.

local names, meshes, boxes, anims = {}, {}, {}, {}
local cur, zoom, spin, lit, angle, clip, t = 1, 1, true, true, 0.6, 1, 0

function _init()
  names = models()
  for i, n in ipairs(names) do
    meshes[i] = model(n)
    anims[i] = clips(meshes[i])
    local x0, y0, z0, x1, y1, z1 = bounds3d(meshes[i])
    boxes[i] = { (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2,
                 math.max(x1 - x0, y1 - y0, z1 - z0, 0.5) }
  end
end

function _update()
  t = t + 1 / 60
  if #names == 0 then return end
  if btnp(0) then cur = (cur - 2) % #names + 1; clip = 1 end
  if btnp(1) then cur = cur % #names + 1; clip = 1 end
  if btn(2) then zoom = math.max(0.3, zoom - 0.02) end
  if btn(3) then zoom = math.min(4, zoom + 0.02) end
  if btnp(4) then lit = not lit end
  if btnp(5) then spin = not spin end
  if btnp(6) and #anims[cur] > 0 then clip = clip % #anims[cur] + 1; t = 0 end
  if spin then angle = angle + 0.01 end
end

function _draw()
  cls(0x1C2030)
  if #names == 0 then
    print("no 3D models yet: make them with bm Studio", 16, 16, 0xFFFFFF)
    return
  end
  local b, a = boxes[cur], anims[cur]
  if #a > 0 then animate(meshes[cur], a[clip].name, t) end
  local d = b[4] * 1.6 * zoom
  zclear()
  camera3d(0, d * 0.55, -d, 0, -0.5, 60)
  light3d(-0.4, 0.8, -0.5, lit and 0.4 or 1)
  -- turn around the middle of the model: move it there, then spin
  local c, s = math.cos(angle), math.sin(angle)
  local x = -(b[1] * c + b[3] * s)
  local z = -(-b[1] * s + b[3] * c)
  draw3d(meshes[cur], x, -b[2], z, 0, angle, 0, 1, lit and 0 or 2)
  print(names[cur] .. "  " .. cur .. "/" .. #names .. (#a > 0 and ("  " .. a[clip].name) or ""), 8, 6, 0xFFFFFF)
  print(stat(4) .. " triangles  " .. stat(2) .. " fps", 8, 24, 0x8890A8)
  print("left/right model  up/down zoom  A light  B spin" .. (#a > 0 and "  X animation" or ""), 8, SCREEN_H - 20, 0x8890A8)
end
]]

-- the layout, as the other editors of the console (bm Mesh, bm Pixel): the
-- panel on the left (the tools, the tile, the model), the 3D view with two
-- lines of text over it
local PANEL_W = 168
local CHK1, CHK2 = 0x2A2E3A, 0x343846       -- the checker of the transparent pixels (bm Pixel)
local VIEW_DX = (PANEL_W + W) // 2 - W // 2
local starter = nil        -- the tiles of a new project (this cartridge's own sheet)
local sheet_w, sheet_h = 256, 256
local function sheet_size() return S.proj.sheet_w or 256, S.proj.sheet_h or 256 end

----------------------------------------------------------------- the brush and the tile picker

-- the brush: a colour, or tw x th tiles of `size` pixels from the sheet
-- (rect is all of them), turned and mirrored as it goes on
local brush = { c = nil, rect = { 0, 0, 16, 16 }, size = 16, tw = 1, th = 1, rot = 0, flip = false }
local picker = { open = false, x = 0, y = 0, size = 16, tw = 1, th = 1, colours = false, pal = 1, top = 0, left = 0 }

local function draw_tile(rect, x, y, size, rot, flip)
  -- the tile scaled into a size x size box (turned and flipped as it goes on)
  local rx, ry, rw, rh = rect[1], rect[2], rect[3], rect[4]
  local n = max(rw, rh)
  local z = size / n
  local px = max(1, floor(z + 0.5))
  for j = 0, rh - 1 do
    for i = 0, rw - 1 do
      local u, v = i, j
      if flip then u = rw - 1 - u end
      for _ = 1, rot % 4 do u, v = rh - 1 - v, u end
      local c = sget(floor(rx + i), floor(ry + j))
      if c then rectfill(x + floor(u * z), y + floor(v * z), px, px, c) end
    end
  end
end

local function draw_brush(x, y, size)
  rectfill(x - 2, y - 2, size + 4, size + 4, CHK1)
  rect(x - 2, y - 2, size + 4, size + 4, C.DIM)
  if brush.c then rectfill(x, y, size, size, brush.c)
  else draw_tile(brush.rect, x, y, size, brush.rot, brush.flip) end
end

local function picker_clamp()
  local p = picker
  p.tw = clamp(p.tw, 1, max(1, (sheet_w - p.x) // p.size))
  p.th = clamp(p.th, 1, max(1, (sheet_h - p.y) // p.size))
  p.top = clamp(p.top, max(0, p.y + p.th * p.size - 272), p.y)
  p.left = clamp(p.left, max(0, p.x + p.tw * p.size - 288), p.x)
end

local function picker_key(k)
  local p = picker
  if k == "esc" or k == "\t" or k == "back" then p.open = false; return end
  if k == "c" then p.colours = not p.colours; return end
  if p.colours then
    local n = #T.PALETTE
    if k == "left" then p.pal = (p.pal - 2) % n + 1
    elseif k == "right" then p.pal = p.pal % n + 1
    elseif k == "up" then p.pal = (p.pal - 7) % n + 1
    elseif k == "down" then p.pal = (p.pal + 5) % n + 1
    elseif k == "\n" or k == " " or k == "ok" then
      brush = { c = T.PALETTE[p.pal], rect = brush.rect, size = brush.size, tw = 1, th = 1, rot = 0, flip = false }
      p.open = false
      say(string.format("colour %06X", brush.c), C.ACC, 60)
    end
    return
  end
  local s = p.size
  if k == "left" then p.x = max(0, p.x - s)
  elseif k == "right" then p.x = min(sheet_w - s, p.x + s)
  elseif k == "up" then p.y = max(0, p.y - s)
  elseif k == "down" then p.y = min(sheet_h - s, p.y + s)
  elseif k == "d" then p.tw = p.tw + 1          -- more tiles: wider, taller
  elseif k == "a" then p.tw = p.tw - 1
  elseif k == "s" then p.th = p.th + 1
  elseif k == "w" then p.th = p.th - 1
  elseif k == "z" then
    p.size = p.size == 8 and 16 or (p.size == 16 and 32 or 8)
    p.x, p.y, p.tw, p.th = p.x // p.size * p.size, p.y // p.size * p.size, 1, 1
  elseif k == "\n" or k == " " or k == "ok" then
    brush = { rect = { p.x, p.y, p.size * p.tw, p.size * p.th }, size = p.size, tw = p.tw, th = p.th,
              rot = brush.rot, flip = brush.flip }
    p.open = false
    say(string.format("tile at %d,%d (%dx%d)%s", p.x, p.y, p.size, p.size,
                      (p.tw > 1 or p.th > 1) and string.format(", %d x %d tiles", p.tw, p.th) or ""), C.ACC, 90)
  end
  picker_clamp()
end

local function picker_pad()
  if btn(6) then                                 -- X + the pad: more or fewer tiles
    if T.rp[0] then picker_key("a") end
    if T.rp[1] then picker_key("d") end
    if T.rp[2] then picker_key("w") end
    if T.rp[3] then picker_key("s") end
    return
  end
  if T.rp[0] then picker_key("left") end
  if T.rp[1] then picker_key("right") end
  if T.rp[2] then picker_key("up") end
  if T.rp[3] then picker_key("down") end
  if btnp(4) then picker_key("ok") end
  if btnp(5) or T.tap[7] then picker_key("back") end
  if T.tap[6] then picker_key(picker.colours and "c" or "z") end
end

local function draw_picker()
  local p = picker
  cls(C.BG)
  if p.colours then
    print(string.format("colours: #%06X", T.PALETTE[p.pal]), 0, 16, C.TEXT)
    for i, c in ipairs(T.PALETTE) do
      local x, y = 16 + ((i - 1) % 6) * 40, 56 + ((i - 1) // 6) * 40
      rectfill(x, y, 32, 32, c)
      if i == p.pal then rect(x - 3, y - 3, 38, 38, C.CUR) end
    end
    rectfill(288, 56, 96, 96, T.PALETTE[p.pal])
    print(string.format("#%06X", T.PALETTE[p.pal]), 288, 160, C.TEXT)
    T.hint({ { { "up", "down", "left", "right" }, "choose" }, { { "enter" }, "take it" }, { { "c" }, "tiles" },
             { { "tab" }, "back" } })
    return
  end
  print(string.format("tiles: sheet %dx%d  at %d,%d  %dx%d", sheet_w, sheet_h, p.x, p.y, p.size, p.size), 0, 16, C.TEXT)
  local ox, oy = 16, 32
  local vw, vh = min(288, sheet_w - p.left), min(272, sheet_h - p.top)
  clip(ox, oy, 288, 272)
  rectfill(ox, oy, vw, vh, CHK1)
  -- a checker shows the transparent pixels (as bm Pixel's)
  for y = 0, vh - 1, 8 do
    for x = 0, vw - 1, 8 do
      if (x // 8 + y // 8) % 2 == 0 then rectfill(ox + x, oy + y, 8, 8, CHK2) end
    end
  end
  sspr(p.left, p.top, vw, vh, ox, oy)
  local bw, bh = p.size * p.tw, p.size * p.th
  rect(ox + p.x - p.left - 1, oy + p.y - p.top - 1, bw + 2, bh + 2, 0x000000)
  rect(ox + p.x - p.left, oy + p.y - p.top, bw, bh, C.CUR)
  clip()
  local big = 128
  rectfill(320, 32, big, big, CHK1)
  draw_tile({ p.x, p.y, bw, bh }, 320, 32, big, 0, false)
  rect(319, 31, big + 2, big + 2, C.DIM)
  if p.tw > 1 or p.th > 1 then print(string.format("%d x %d tiles: one put", p.tw, p.th), 320, 176, C.ACC) end
  local x = 320
  for _, k in ipairs({ "a", "d", "w", "s" }) do x = prompt(k, x, 256) + 1 end
  print("fewer / more tiles", T.snap(x + 3), 256, C.DIM)
  T.chip_hint("z", nil, "tile size " .. p.size, 320, 272)
  T.hint({ { { "up", "down", "left", "right" }, "choose" }, { { "enter" }, "take it" }, { { "c" }, "colours" },
           { { "tab" }, "back" } })
end

----------------------------------------------------------------- build page
local build, models_page
do

local bd = { cell = { 0, 0, 0 }, tool = 1, side = 1, grid = true, view = 1, back = false,
             cam = { yaw = 0, pitch = -0.6, dist = 8, tx = 0.5, ty = 0.5, tz = 0.5 }, yaw_to = 0,
             hot = nil, sel = {}, pts = nil, hotp = nil, psel = {}, move = nil, paint = nil,
             back_mesh = nil, back_for = nil }
local TOOLS = { "block", "tile", "select", "vertex", "paint" }
local SIDES = { "floor", "far wall", "right wall", "near wall", "left wall", "ceiling", "all sides" }
local VIEWS = { "light", "flat", "wireframe" }
local STEPS = { 1, 1 / 2, 1 / 4, 1 / 8, 1 / 16 }

-- texture corners of a rect in corner order (bottom left, top left, top
-- right, bottom right), flipped, then turned `rot` quarter turns (edit.js)
local function rect_uv(rect, rot, flip)
  local x, y, w, h = rect[1], rect[2], rect[3], rect[4]
  local b = { { x, y + h }, { x, y }, { x + w, y }, { x + w, y + h } }
  if flip then b = { b[4], b[3], b[2], b[1] } end
  local r = rot % 4
  local out = {}
  for k = 0, 3 do local q = b[(k - r) % 4 + 1]; out[k + 1] = { q[1], q[2] } end
  return out
end

-- up on the floor: the horizontal axis nearest to where the camera looks,
-- so a tile shows the right way up (edit.js floorUp)
local function floor_up(f)
  if abs(f[1]) > abs(f[3]) then return { f[1] > 0 and 1 or -1, 0, 0 } end
  return { 0, 0, f[3] < 0 and -1 or 1 }
end

-- the right and up axes of a tile on the plane `axis`, facing n
local function tile_axes(axis, n)
  local nv = V.unit(axis, n)
  local U = axis == 2 and floor_up(bd.cam.f or { 0, 0, 1 }) or { 0, 1, 0 }
  return V.cross(nv, U), U
end

-- the tile on the plane axis = level, over cell c, showing towards n (+1/-1)
local function tile_face(c, axis, level, n, b)
  local R, U = tile_axes(axis, n)
  local m = { c[1] + 0.5, c[2] + 0.5, c[3] + 0.5 }
  m[axis] = level
  local function h(a, bb)
    return { m[1] + (a * R[1] + bb * U[1]) / 2, m[2] + (a * R[2] + bb * U[2]) / 2, m[3] + (a * R[3] + bb * U[3]) / 2 }
  end
  local f = { p = { h(-1, -1), h(-1, 1), h(1, 1), h(1, -1) } }
  if b.c then f.c = b.c else f.uv = rect_uv(b.rect, b.rot, b.flip) end
  return f
end

-- one tile of the brush: the top left one of a group
local function one_tile(b)
  if b.c or (b.tw == 1 and b.th == 1) then return b end
  return { rect = { b.rect[1], b.rect[2], b.size, b.size }, rot = b.rot, flip = b.flip }
end

-- the brush's tiles on a side: tw x th faces, the group's top left on the
-- cell's... top left as the tile shows
local function group_faces(c, axis, level, n, b)
  if b.c or (b.tw == 1 and b.th == 1) then return { tile_face(c, axis, level, n, b) } end
  local R, U = tile_axes(axis, n)
  local out = {}
  for j = 0, b.th - 1 do
    for i = 0, b.tw - 1 do
      local ii = b.flip and (b.tw - 1 - i) or i
      local cell = { c[1] + round(R[1]) * i + round(U[1]) * j, c[2] + round(R[2]) * i + round(U[2]) * j,
                     c[3] + round(R[3]) * i + round(U[3]) * j }
      local sub = { rect = { b.rect[1] + ii * b.size, b.rect[2] + (b.th - 1 - j) * b.size, b.size, b.size },
                    rot = b.rot, flip = b.flip }
      out[#out + 1] = tile_face(cell, axis, cell[axis] + (level - c[axis]), n, sub)
    end
  end
  return out
end

local function spot_key(f)
  local ks = {}
  for i, p in ipairs(f.p) do ks[i] = T.pkey(p) end
  table.sort(ks)
  return table.concat(ks, ";")
end

-- adds faces: one on the same spot facing the same way is replaced (a new
-- texture); with `cancel`, one facing the other way goes away with the new
-- one (two blocks side by side have no wall between them)
local function place_faces(faces, added, cancel)
  local index = {}
  for i, f in ipairs(faces) do
    local k = spot_key(f)
    index[k] = index[k] or {}
    table.insert(index[k], i)
  end
  local remove = {}
  for _, nf in ipairs(added) do
    local k, n = spot_key(nf), T.face_normal(nf)
    local facing, opposite
    for _, i in ipairs(index[k] or {}) do
      if not remove[i] then
        local d = V.dot(T.face_normal(faces[i]), n)
        if d > 0.9 then facing = i elseif d < -0.9 then opposite = i end
      end
    end
    if cancel and opposite then
      remove[opposite] = true
    elseif facing then
      local f = faces[facing]
      f.p, f.uv, f.c = nf.p, nf.uv, nf.c
    else
      faces[#faces + 1] = nf
      index[k] = index[k] or {}
      table.insert(index[k], #faces)
    end
  end
  if next(remove) then
    local keep = {}
    for i, f in ipairs(faces) do if not remove[i] then keep[#keep + 1] = f end end
    for i = 1, #faces do faces[i] = keep[i] end
  end
end

-- the plane of a side of cell c: axis, level and the way a tile there
-- faces (into the cell); for the walls the view decides which is "far"
local function side_plane(c, s)
  if s == 1 then return 2, c[2], 1 end
  if s == 6 then return 2, c[2] + 1, -1 end
  local fa, fs, ra, rs = T.view_axes(bd.yaw_to)
  local a, d
  if s == 2 then a, d = fa, fs elseif s == 4 then a, d = fa, -fs
  elseif s == 3 then a, d = ra, rs else a, d = ra, -rs end
  return a, c[a] + (d > 0 and 1 or 0), -d
end

-- does face f lie on the square of the plane axis = level over cell c?
local function on_square(f, c, axis, level)
  for _, p in ipairs(f.p) do
    if abs(p[axis] - level) > 1e-3 then return false end
  end
  local m = T.face_center(f)
  for k = 1, 3 do
    if k ~= axis and (m[k] < c[k] - 1e-3 or m[k] > c[k] + 1 + 1e-3) then return false end
  end
  return true
end

-- the faces on the chosen sides of cell c: { index, ... }
local function faces_on(faces, c, s, outward_only)
  local out = {}
  local planes = {}
  if s == 7 then
    for k = 1, 3 do
      planes[#planes + 1] = { k, c[k], -1 }
      planes[#planes + 1] = { k, c[k] + 1, 1 }
    end
  else
    local a, l, n = side_plane(c, s)
    planes[1] = { a, l, -n }
  end
  for i, f in ipairs(faces) do
    for _, pl3 in ipairs(planes) do
      if on_square(f, c, pl3[1], pl3[2]) and (not outward_only or T.face_normal(f)[pl3[1]] * pl3[3] > 0.9) then
        out[#out + 1] = i
        break
      end
    end
  end
  return out
end

local function block_faces(c, b)
  local out = {}
  for k = 1, 3 do
    for _, s in ipairs({ -1, 1 }) do
      out[#out + 1] = tile_face(c, k, c[k] + (s > 0 and 1 or 0), s, b)
    end
  end
  return out
end

-- the brush of a face (its colour, or the rectangle of its texture)
local function brush_of(f)
  if f.c then return { c = f.c, rect = brush.rect, size = brush.size, tw = 1, th = 1, rot = 0, flip = false } end
  local u0, v0, u1, v1 = 1e9, 1e9, -1e9, -1e9
  for _, q in ipairs(f.uv) do
    u0, v0, u1, v1 = min(u0, q[1]), min(v0, q[2]), max(u1, q[1]), max(v1, q[2])
  end
  local w, h = max(1, u1 - u0), max(1, v1 - v0)
  return { rect = { u0, v0, w, h }, size = max(w, h), tw = 1, th = 1, rot = 0, flip = false }
end

local function remove_faces(faces, list)
  local gone = {}
  for _, i in ipairs(list) do gone[i] = true end
  local keep = {}
  for i, f in ipairs(faces) do if not gone[i] then keep[#keep + 1] = f end end
  return keep
end

local function is_block(faces, c)
  return #faces_on(faces, c, 7, true) > 0
end

-- the brush on a face (a colour, or a tile)
local function texture(f, b)
  if b.c then
    f.c, f.uv = b.c, nil
  else
    local uv = rect_uv(one_tile(b).rect, b.rot, b.flip)
    f.c, f.uv = nil, {}
    for k = 1, #f.p do f.uv[k] = uv[k] end
  end
end

local function build_action(erase)
  local m = M()
  if not m then return end
  local c = bd.cell
  if bd.tool == 1 then
    if not erase then
      T.begin_edit()
      place_faces(m.faces, block_faces(c, one_tile(brush)), true)
      T.commit()
      return
    end
    if not is_block(m.faces, c) then say("no block here", C.DIM, 60); return end
    T.begin_edit()
    -- its own walls go; where a neighbour touched it, the neighbour's wall
    -- shows again (with the texture of one of its faces)
    local walls = faces_on(m.faces, c, 7, true)
    local had = {}
    for _, i in ipairs(walls) do
      local n = T.face_normal(m.faces[i])
      for k = 1, 3 do if abs(n[k]) > 0.9 then had[k * 2 + (n[k] > 0 and 1 or 0)] = true end end
    end
    m.faces = remove_faces(m.faces, walls)
    local add = {}
    for k = 1, 3 do
      for _, s in ipairs({ -1, 1 }) do
        if not had[k * 2 + (s > 0 and 1 or 0)] then
          local nb = V.copy(c)
          nb[k] = nb[k] + s
          local own = faces_on(m.faces, nb, 7, true)
          if #own > 0 then
            add[#add + 1] = tile_face(c, k, c[k] + (s > 0 and 1 or 0), -s, brush_of(m.faces[own[1]]))
          end
        end
      end
    end
    place_faces(m.faces, add, false)
    T.commit()
  elseif bd.tool == 2 then
    local s = bd.side == 7 and 1 or bd.side
    if erase then
      local list = faces_on(m.faces, c, s)
      if #list == 0 then say("nothing on the " .. SIDES[s], C.DIM, 60); return end
      T.begin_edit()
      m.faces = remove_faces(m.faces, list)
      T.commit()
    else
      local a, l, n = side_plane(c, s)
      T.begin_edit()
      place_faces(m.faces, group_faces(c, a, l, n, brush), false)
      T.commit()
    end
  end
end

local function build_move(dx, dz, dy)
  local fa, fs, ra, rs = T.view_axes(bd.yaw_to)
  local c = bd.cell
  if dz ~= 0 then c[fa] = c[fa] + dz * fs end
  if dx ~= 0 then c[ra] = c[ra] + dx * rs end
  if dy ~= 0 then c[2] = c[2] + dy end
  for k = 1, 3 do c[k] = clamp(c[k], -64, 63) end
end

local function build_turn(d)
  bd.yaw_to = bd.yaw_to + d * pi / 4
end

-- the cell beside the model, the camera on it
local function frame_model()
  local m = M()
  if m and #m.faces > 0 then
    local lo, hi = T.bounds_of(m.faces)
    bd.cell = { floor((lo[1] + hi[1]) / 2), floor(lo[2] + 1e-3), floor(lo[3] + 1e-3) - 1 }
    bd.cam.dist = clamp(max(hi[1] - lo[1], hi[2] - lo[2], hi[3] - lo[3]) * 1.6 + 3, 4, 60)
  else
    bd.cell = { 0, 0, 0 }
  end
end

----------------------------------------------------------------- pointers on the faces and the corners

local function faces() return M() and M().faces or {} end

-- the face under the keyboard's pointer: arrows go to the nearest face that
-- way on the screen; the faces turned away count a little farther
local function face_pos(i)
  local f = faces()[i]
  if not f then return nil end
  local x, y, z = T.scr(T.face_center(f))
  if not x then return nil end
  local away = bd.cam.f and V.dot(T.face_normal(f), bd.cam.f) > 0.2
  return x, y, (z or 0) + (away and 4 or 0)
end

local function hot_face()
  local fs = faces()
  if bd.hot and not fs[bd.hot] then bd.hot = nil end
  if not bd.hot and #fs > 0 then
    -- the faces on the cursor's cell first, then the nearest to the middle
    local list = faces_on(fs, bd.cell, 7)
    bd.hot = list[1] or T.nav(#fs, nil, "right", face_pos)
  end
  return bd.hot and fs[bd.hot]
end

-- the corners of the model, by position: { {p=, refs={ {face, k}, ... }} }
local function points()
  if bd.pts then return bd.pts end
  local list, index = {}, {}
  for _, f in ipairs(faces()) do
    for k, p in ipairs(f.p) do
      local key = T.pkey(p)
      local pt = index[key]
      if not pt then
        pt = { p = p, refs = {} }
        index[key] = pt
        list[#list + 1] = pt
      end
      pt.refs[#pt.refs + 1] = { f, k }
    end
  end
  bd.pts = list
  return list
end

local function point_pos(i)
  local pt = points()[i]
  if not pt then return nil end
  return T.scr(pt.p)
end

local function hot_point()
  local pts = points()
  if bd.hotp and not pts[bd.hotp] then bd.hotp = nil end
  if not bd.hotp and #pts > 0 then bd.hotp = T.nav(#pts, nil, "right", point_pos) end
  return bd.hotp and pts[bd.hotp]
end

-- after an edit the corners are found again; the chosen ones stay chosen
local function changed()
  local keep, hk = {}, nil
  for pt in pairs(bd.psel) do keep[T.pkey(pt.p)] = true end
  local hp = bd.hotp and bd.pts and bd.pts[bd.hotp]
  if hp then hk = T.pkey(hp.p) end
  bd.pts, bd.psel, bd.hotp = nil, {}, nil
  for i, pt in ipairs(points()) do
    local k = T.pkey(pt.p)
    if keep[k] then bd.psel[pt] = true end
    if k == hk then bd.hotp = i end
  end
end

local function chosen_faces()
  local out = {}
  for _, f in ipairs(faces()) do if bd.sel[f] then out[#out + 1] = f end end
  return out
end

local function chosen_points()
  local out = {}
  for _, pt in ipairs(points()) do if bd.psel[pt] then out[#out + 1] = pt end end
  return out
end

local function count(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

----------------------------------------------------------------- select: the operations

-- the middle of the chosen faces' box, on a grid of 1/2
local function pivot(list)
  local lo, hi = T.bounds_of(list)
  return { round((lo[1] + hi[1])) / 2, round((lo[2] + hi[2])) / 2, round((lo[3] + hi[3])) / 2 }
end

-- every corner of the chosen faces through fn(p) -> new p
local function each_corner(list, fn)
  local seen = {}
  for _, f in ipairs(list) do
    for k, p in ipairs(f.p) do
      if not seen[p] then
        local q = fn(p)
        p[1], p[2], p[3] = q[1], q[2], q[3]
        seen[p] = true
      end
    end
  end
end

-- the corners the other way round: the face shows from the other side
local function reverse(f)
  local function rev(t) if t then local o = {}; for i = #t, 1, -1 do o[#o + 1] = t[i] end; return o end end
  f.p, f.uv, f.b = rev(f.p), rev(f.uv), rev(f.b)
end

-- corners shared by faces are separate tables after a decode; the
-- operations move each table once (seen) so shared tables are fine too
local function sel_op(name, fn)
  local list = chosen_faces()
  if #list == 0 then say("nothing chosen: space chooses the face under the pointer", C.DIM, 90); return end
  T.begin_edit()
  fn(list)
  T.commit()
  changed()
  say(name .. ": " .. #list .. " faces", C.ACC, 60)
end

local function sel_turn(list)
  local c = pivot(list)
  each_corner(list, function(p) return { c[1] + (p[3] - c[3]), p[2], c[3] - (p[1] - c[1]) } end)
end

local function sel_flip(list)
  local c = pivot(list)
  local _, _, ra = T.view_axes(bd.yaw_to)
  local o = ra == 1 and 3 or 1                   -- upside down: around the view's right axis
  each_corner(list, function(p)
    local q = V.copy(p)
    q[2] = 2 * c[2] - p[2]
    q[o] = 2 * c[o] - p[o]
    return q
  end)
end

local function sel_mirror(list)
  local c = pivot(list)
  local _, _, ra = T.view_axes(bd.yaw_to)
  each_corner(list, function(p) local q = V.copy(p); q[ra] = 2 * c[ra] - p[ra]; return q end)
  for _, f in ipairs(list) do reverse(f) end
end

local function sel_scale(list, k)
  local c = pivot(list)
  each_corner(list, function(p)
    return { c[1] + (p[1] - c[1]) * k, c[2] + (p[2] - c[2]) * k, c[3] + (p[3] - c[3]) * k }
  end)
end

local function sel_texture_turn(list)
  for _, f in ipairs(list) do
    if f.uv then
      local o = {}
      for k = 1, #f.uv do o[k] = f.uv[k % #f.uv + 1] end
      f.uv = o
    end
  end
end

-- a copy of a face (its own corner tables)
local function copy_face(f)
  local o = { p = {}, c = f.c }
  for k, p in ipairs(f.p) do o.p[k] = V.copy(p) end
  if f.uv then o.uv = {}; for k, q in ipairs(f.uv) do o.uv[k] = { q[1], q[2] } end end
  if f.b then o.b = {}; for k, b in ipairs(f.b) do o.b[k] = b end end
  return o
end

-- moving the chosen faces or corners: arrows, PgUp / PgDn; Tab the step;
-- Enter keeps it, Esc puts it back
local function start_move(what, list)
  if #list == 0 then say("nothing to move", C.DIM, 60); return end
  T.begin_edit()
  bd.move = { what = what, list = list, step = bd.move_step or 1, total = { 0, 0, 0 } }
end

local function move_by(dx, dz, dy)
  local mv = bd.move
  local fa, fs, ra, rs = T.view_axes(bd.yaw_to)
  local d = { 0, 0, 0 }
  d[ra] = d[ra] + dx * rs * mv.step
  d[fa] = d[fa] + dz * fs * mv.step
  d[2] = d[2] + dy * mv.step
  local seen = {}
  local function shift(p)
    if not seen[p] then p[1], p[2], p[3] = p[1] + d[1], p[2] + d[2], p[3] + d[3]; seen[p] = true end
  end
  if mv.what == "faces" then
    for _, f in ipairs(mv.list) do for _, p in ipairs(f.p) do shift(p) end end
  else
    for _, pt in ipairs(mv.list) do for _, r in ipairs(pt.refs) do shift(r[1].p[r[2]]) end end
  end
  mv.total = V.add(mv.total, d)
  M().dirty = true
  local ok, e = T.sync()
  if not ok then say("cannot: " .. tostring(e), C.ERR) end
  bd.pts = nil
end

local function end_move(keep)
  local mv = bd.move
  bd.move = nil
  bd.move_step = mv.step
  if keep then
    T.commit()
    say(string.format("moved %.3g, %.3g, %.3g", mv.total[1], mv.total[2], mv.total[3]), C.ACC, 90)
  else
    -- back where they were (the same faces: they stay chosen); a copy goes
    local m = M()
    if mv.copies then
      local gone = {}
      for _, f in ipairs(mv.copies) do gone[f] = true end
      local keep_f = {}
      for _, f in ipairs(m.faces) do if not gone[f] then keep_f[#keep_f + 1] = f end end
      m.faces = keep_f
      bd.sel = mv.was or {}
    else
      bd.move, mv.step, mv.total = mv, 1, V.scale(mv.total, -1)
      local t = mv.total
      local fa, fs, ra, rs = T.view_axes(bd.yaw_to)
      move_by(t[ra] * rs, t[fa] * fs, t[2])
      bd.move = nil
    end
    table.remove(S.undo)                -- the snapshot of the start: nothing changed
    m.dirty = true
    T.sync()
    say("moved back", C.DIM, 60)
  end
  changed()
end

local function move_key(k)
  if k == "up" then move_by(0, 1, 0)
  elseif k == "down" then move_by(0, -1, 0)
  elseif k == "left" then move_by(-1, 0, 0)
  elseif k == "right" then move_by(1, 0, 0)
  elseif k == "pgup" then move_by(0, 0, 1)
  elseif k == "pgdn" then move_by(0, 0, -1)
  elseif k == "\t" then
    local i = 1
    for n, s in ipairs(STEPS) do if s == bd.move.step then i = n end end
    bd.move.step = STEPS[i % #STEPS + 1]
  elseif k == "\n" or k == "g" or k == " " or k == "ok" then end_move(true)
  elseif k == "esc" or k == "back" then end_move(false)
  end
end

-- the faces joined to the chosen ones (sharing a corner): a whole object
local function connected()
  local fs = faces()
  local by = {}
  for _, f in ipairs(fs) do
    for _, p in ipairs(f.p) do
      local k = T.pkey(p)
      by[k] = by[k] or {}
      table.insert(by[k], f)
    end
  end
  local todo = chosen_faces()
  if #todo == 0 and hot_face() then todo = { hot_face() }; bd.sel[todo[1]] = true end
  while #todo > 0 do
    local f = table.remove(todo)
    for _, p in ipairs(f.p) do
      for _, g in ipairs(by[T.pkey(p)]) do
        if not bd.sel[g] then bd.sel[g] = true; todo[#todo + 1] = g end
      end
    end
  end
end

local function select_key(k)
  local m = M()
  local fs = faces()
  if k == " " or k == "ok" then
    local f = hot_face()
    if f then bd.sel[f] = not bd.sel[f] or nil end
  elseif k == "a" then
    if count(bd.sel) > 0 then bd.sel = {} else for _, f in ipairs(fs) do bd.sel[f] = true end end
  elseif k == "c" then connected(); say(count(bd.sel) .. " faces chosen (all joined)", C.ACC, 60)
  elseif k == "g" then start_move("faces", chosen_faces())
  elseif k == "r" then sel_op("turned", sel_turn)
  elseif k == "t" then sel_op("upside down", sel_flip)
  elseif k == "m" then sel_op("mirrored", sel_mirror)
  elseif k == "n" then sel_op("the other side", function(l) for _, f in ipairs(l) do reverse(f) end end)
  elseif k == "\n" then sel_op("new texture", function(l) for _, f in ipairs(l) do texture(f, brush) end end)
  elseif k == "u" then sel_op("texture turned", sel_texture_turn)
  elseif k == "." then sel_op("twice as big", function(l) sel_scale(l, 2) end)
  elseif k == "," then sel_op("half as big", function(l) sel_scale(l, 0.5) end)
  elseif k == "d" or k == "^d" then
    local list = chosen_faces()
    if #list == 0 then say("nothing chosen", C.DIM, 60); return end
    -- one undo step for the copy and its move: Esc takes the copy away
    T.begin_edit()
    local was = bd.sel
    bd.sel = {}
    local copies = {}
    for _, f in ipairs(list) do
      local o = copy_face(f)
      m.faces[#m.faces + 1] = o
      copies[#copies + 1] = o
      bd.sel[o] = true
    end
    m.dirty = true
    T.sync()
    bd.move = { what = "faces", list = copies, copies = copies, was = was, step = bd.move_step or 1, total = { 0, 0, 0 } }
    say("a copy: move it with the arrows, Enter puts it", C.ACC, 120)
  elseif k == "\b" or k == "del" then
    local list = {}
    for i, f in ipairs(fs) do if bd.sel[f] then list[#list + 1] = i end end
    if #list == 0 then say("nothing chosen", C.DIM, 60); return end
    T.begin_edit()
    m.faces = remove_faces(m.faces, list)
    T.commit()
    bd.sel, bd.hot = {}, nil
    changed()
    say(#list .. " faces deleted", C.ACC, 60)
  elseif k == "o" then
    local list = chosen_faces()
    if #list == 0 then say("nothing chosen", C.DIM, 60); return end
    local keep = {}
    for _, f in ipairs(fs) do if not bd.sel[f] then keep[#keep + 1] = f end end
    T.begin_edit()
    m.faces = keep
    T.commit()
    local i = T.new_model(m.name)
    local nm = S.models[i]
    for _, f in ipairs(list) do f.b = nil; nm.faces[#nm.faces + 1] = f end
    T.sync()
    S.undo, S.redo = {}, {}
    bd.sel, bd.hot = {}, nil
    changed()
    say(#list .. " faces moved to the new model " .. nm.name, C.ACC, 150)
  else
    return false
  end
  return true
end

local function vertex_key(k)
  if k == " " or k == "ok" then
    local pt = hot_point()
    if pt then bd.psel[pt] = not bd.psel[pt] or nil end
  elseif k == "a" then
    if count(bd.psel) > 0 then bd.psel = {} else for _, pt in ipairs(points()) do bd.psel[pt] = true end end
  elseif k == "g" then
    local list = chosen_points()
    if #list == 0 and hot_point() then list = { hot_point() } end
    start_move("points", list)
  elseif k == "m" then
    local list = chosen_points()
    if #list < 2 then say("choose two corners or more (space), then m joins them", C.DIM, 120); return true end
    local c = { 0, 0, 0 }
    for _, pt in ipairs(list) do c = V.add(c, pt.p) end
    c = V.scale(c, 1 / #list)
    T.begin_edit()
    for _, pt in ipairs(list) do
      for _, r in ipairs(pt.refs) do local p = r[1].p[r[2]]; p[1], p[2], p[3] = c[1], c[2], c[3] end
    end
    T.commit()
    changed()
    say(#list .. " corners joined", C.ACC, 60)
  else
    return false
  end
  return true
end

----------------------------------------------------------------- paint: the pixels of a face's tile

local paint_c = 0xE84A5A

-- the sheet rectangle of a face's texture
local function face_rect(f)
  local u0, v0, u1, v1 = 1e9, 1e9, -1e9, -1e9
  for _, q in ipairs(f.uv) do
    u0, v0, u1, v1 = min(u0, q[1]), min(v0, q[2]), max(u1, q[1]), max(v1, q[2])
  end
  return floor(u0 + 0.5), floor(v0 + 0.5), max(1, round(u1 - u0)), max(1, round(v1 - v0))
end

-- where a pixel of the face's texture is on the face (its middle), from the
-- triangles of the texture corners
local function pixel_world(f, u, v)
  local tris = #f.p == 4 and T.QUAD or T.TRI
  for _, t in ipairs(tris) do
    local a, b, c = f.uv[t[1]], f.uv[t[2]], f.uv[t[3]]
    local d = (b[2] - c[2]) * (a[1] - c[1]) + (c[1] - b[1]) * (a[2] - c[2])
    if abs(d) > 1e-9 then
      local l1 = ((b[2] - c[2]) * (u - c[1]) + (c[1] - b[1]) * (v - c[2])) / d
      local l2 = ((c[2] - a[2]) * (u - c[1]) + (a[1] - c[1]) * (v - c[2])) / d
      local l3 = 1 - l1 - l2
      if l1 >= -1e-6 and l2 >= -1e-6 and l3 >= -1e-6 then
        local pa, pb, pc = f.p[t[1]], f.p[t[2]], f.p[t[3]]
        return { pa[1] * l1 + pb[1] * l2 + pc[1] * l3, pa[2] * l1 + pb[2] * l2 + pc[2] * l3,
                 pa[3] * l1 + pb[3] * l2 + pc[3] * l3 }
      end
    end
  end
end

local function paint_open()
  local f = hot_face()
  if not f then say("no faces to paint", C.DIM, 60); return end
  if f.c then
    T.begin_edit()
    f.c = paint_c
    T.commit()
    say(string.format("the face is now #%06X", paint_c), C.ACC, 60)
    return
  end
  local x, y, w, h = face_rect(f)
  bd.paint = { f = f, x = x, y = y, w = w, h = h, cx = w // 2, cy = h // 2, entry = nil }
end

local function paint_set(c)
  local pp = bd.paint
  local x, y = pp.x + pp.cx, pp.y + pp.cy
  local old = sget(x, y)
  if old == c then return end
  -- the pixels of this time in the editor are one undo step
  if not pp.entry or S.undo[#S.undo] ~= pp.entry then
    T.sheet_undo({})
    pp.entry, pp.seen = S.undo[#S.undo], {}
  end
  local key = x .. "," .. y
  if not pp.seen[key] then
    pp.seen[key] = true
    local px = pp.entry.px
    px[#px + 1], px[#px + 2], px[#px + 3] = x, y, old or false
  end
  sset(x, y, c)
  S.dirty, S.sheet_dirty = true, true
end

local function paint_key(k)
  local pp = bd.paint
  if k == "left" then pp.cx = (pp.cx - 1) % pp.w
  elseif k == "right" then pp.cx = (pp.cx + 1) % pp.w
  elseif k == "up" then pp.cy = (pp.cy - 1) % pp.h
  elseif k == "down" then pp.cy = (pp.cy + 1) % pp.h
  elseif k == " " or k == "ok" then paint_set(paint_c)
  elseif k == "e" then paint_set(nil)
  elseif k == "i" or k == "x" then
    local c = sget(pp.x + pp.cx, pp.y + pp.cy)
    if c then paint_c = c; say(string.format("colour #%06X", c), C.ACC, 60) end
  elseif k == "c" then
    local rows = {}
    for i, c in ipairs(T.PALETTE) do
      rows[i] = { string.format("#%06X", c), function() paint_c = c end }
    end
    -- the colours already in this tile, after the palette
    local seen = {}
    for j = 0, pp.h - 1 do
      for i = 0, pp.w - 1 do
        local c = sget(pp.x + i, pp.y + j)
        if c and not seen[c] then
          seen[c] = true
          rows[#rows + 1] = { string.format("#%06X (in the tile)", c), function() paint_c = c end }
        end
      end
    end
    T.choose("paint colour", rows, 1)
  elseif k == "esc" or k == "\n" or k == "\t" or k == "back" then bd.paint = nil
  end
end

local function draw_paint_panel(x, y)
  local pp = bd.paint
  local n = max(pp.w, pp.h)
  local z = max(1, min(136 // n, 8))
  print("PAINT", x, y, C.DIM)
  print(string.format("%dx%d at %d,%d", pp.w, pp.h, pp.x, pp.y), x, y + 16, C.TEXT)
  local oy = y + 36
  rectfill(x, oy, pp.w * z, pp.h * z, CHK1)
  for j = 0, pp.h - 1 do
    for i = 0, pp.w - 1 do
      local c = sget(pp.x + i, pp.y + j)
      if c then rectfill(x + i * z, oy + j * z, z, z, c)
      elseif (i + j) % 2 == 0 then rectfill(x + i * z, oy + j * z, z, z, CHK2) end
    end
  end
  local blink = (S.frame // 15) % 2 == 0 and C.CUR or 0xFFFFFF
  rect(x + pp.cx * z - 1, oy + pp.cy * z - 1, z + 2, z + 2, blink)
  local ty = (oy + pp.h * z + 23) // 16 * 16       -- on a row of text
  rectfill(x, ty, 16, 16, paint_c)
  rect(x, ty, 16, 16, C.DIM)
  print(string.format("#%06X", paint_c), x + 24, ty, C.TEXT)
  print("paints the sheet:", x, ty + 32, C.DIM)
  print("the faces with this", x, ty + 48, C.DIM)
  print("tile change too", x, ty + 64, C.DIM)
end

----------------------------------------------------------------- the view

-- the faces seen from behind, darker: a mesh of the model turned inside out
local function back_mesh(m)
  if bd.back_for == m.mc and bd.back_mesh then return bd.back_mesh end
  bd.back_for, bd.back_mesh = m.mc, nil
  local v, f, uv, index = {}, {}, {}, {}
  local function vid(p)
    local key = T.pkey(p)
    local i = index[key]
    if not i then
      i = #v // 3 + 1
      index[key] = i
      v[#v + 1], v[#v + 2], v[#v + 3] = p[1], p[2], p[3]
    end
    return i
  end
  for _, fc in ipairs(m.faces) do
    local ids = {}
    for k, p in ipairs(fc.p) do ids[k] = vid(p) end
    for _, t in ipairs(#fc.p == 4 and T.QUAD or T.TRI) do
      local a, b, c = ids[t[1]], ids[t[2]], ids[t[3]]
      if a ~= b and b ~= c and a ~= c then
        f[#f + 1], f[#f + 2], f[#f + 3], f[#f + 4] = c, b, a, fc.c and fc.c or -1
        for _, j in ipairs({ t[3], t[2], t[1] }) do
          local q = fc.uv and fc.uv[j] or { 0, 0 }
          uv[#uv + 1], uv[#uv + 2] = q[1], q[2]
        end
      end
    end
  end
  if #f > 0 and #v // 3 <= 4096 then bd.back_mesh = mesh(v, f, uv) end
  return bd.back_mesh
end

local function draw_model(m)
  if not S.view then return end
  if bd.view == 3 then
    for _, f in ipairs(m.faces) do T.outline(f, 0x9AA4C0) end
    return
  end
  light3d(-0.4, 0.8, -0.5, bd.view == 1 and 0.45 or 1)
  if bd.back then
    local bm = back_mesh(m)
    if bm then light3d(-0.4, 0.8, -0.5, 0.15); draw3d(bm, 0, 0, 0, 0, 0, 0, 1, 0) end
    light3d(-0.4, 0.8, -0.5, bd.view == 1 and 0.45 or 1)
  end
  draw3d(S.view, 0, 0, 0, 0, 0, 0, 1, bd.view == 2 and 2 or 0)
end

-- the panel on the left: the tools (a list, as the lists of bm Mesh), the
-- tile or colour of the brush with the sheet around it, the model
local TOOL_C = { 0xE0A060, 0x90E090, 0x80C8FF, 0xFFE070, 0xFF8080 }
local function draw_panel()
  rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
  if bd.paint then draw_paint_panel(16, 32); return end
  print("TOOLS", 16, 32, C.DIM)
  for i, name in ipairs(TOOLS) do
    local y = 32 + i * 16
    if i == bd.tool then rectfill(0, y, PANEL_W, 16, C.SEL) end
    print(tostring(i), 8, y, TOOL_C[i])
    print(name, 24, y, i == bd.tool and 0xFFFFFF or C.TEXT)
  end
  print(brush.c and "COLOUR" or "TILE", 16, 144, C.DIM)
  draw_brush(18, 162, 28)
  if brush.c then
    print(string.format("#%06X", brush.c), 56, 160, C.TEXT)
  else
    print(string.format("%d,%d", brush.rect[1], brush.rect[2]), 56, 160, C.TEXT)
    print(brush.tw * brush.th > 1 and string.format("%dx%d tiles", brush.tw, brush.th) or
          string.format("%dx%d", brush.size, brush.size), 56, 176, C.DIM)
  end
  local t = (brush.rot > 0 and (brush.rot * 90 .. "\248 ") or "") .. (brush.flip and "mirror" or "")
  if t ~= "" then print(t, 104, 176, C.DIM) end
  T.chip_hint("tab", "Y", "tiles", 16, 192)
  -- the sheet around the brush
  if not brush.c then
    local vw, vh = PANEL_W - 32, 40
    local sx = clamp(brush.rect[1] + brush.rect[3] // 2 - vw // 2, 0, max(0, sheet_w - vw))
    local sy = clamp(brush.rect[2] + brush.rect[4] // 2 - vh // 2, 0, max(0, sheet_h - vh))
    rectfill(16, 210, vw, vh, CHK1)
    clip(16, 210, vw, vh)
    sspr(sx, sy, vw, vh, 16, 210)
    rect(16 + brush.rect[1] - sx, 210 + brush.rect[2] - sy, brush.rect[3], brush.rect[4], C.CUR)
    clip()
  end
  local m = M()
  print("MODEL", 16, 256, C.DIM)
  print((m and m.name or "-"):sub(1, 13), 64, 256, C.TEXT)
  local warn = T.counts(m, 16, 272, true)
  print(warn and "heavy for 60 fps" or ("view: " .. VIEWS[bd.view] .. (bd.back and ", back" or "")), 16, 304,
        warn and C.ACC or C.DIM)
end

build = {
  id = "build", fkey = "f1", label = "build",
  keys = { "1 2 3 4 5      block, tile, select, vertex, paint",
           "arrows PgUp/Dn the cell (block, tile); the face / corner",
           "space  Bksp    put / remove; choose (select, vertex)",
           "f  Tab         the side; the tiles and colours of the sheet",
           "r h  x         turn / mirror the tile, pick from a face",
           "select:  a all  c joined  g move  r turn  t flip  m mirror",
           "         n other side  Enter tile  u turn tile  , . size",
           "         d copy  Del delete  o to a new model",
           "vertex:  space choose  a all  g move  m join",
           "paint:   Enter the face's tile: space paint, e clear, i pick",
           "q e  w s  + -  turn, tilt, zoom; z the model; v view; b back" },
}

function build.modal() return picker.open or bd.move ~= nil or bd.paint ~= nil end

function build.reset()
  bd.for_model = M()
  frame_model()
  bd.sel, bd.psel, bd.pts, bd.hot, bd.hotp, bd.move, bd.paint = {}, {}, nil, nil, nil, nil, nil
end

function build.enter()
  if bd.for_model ~= M() then build.reset() end
  sheet_w, sheet_h = sheet_size()
end

function build.refresh()
  local old, new = bd.seen, faces()
  local sel, psel = {}, {}
  if old and old ~= new and #old == #new then
    for i, f in ipairs(old) do if bd.sel[f] then sel[new[i]] = true end end
    local oldp = bd.pts
    bd.pts = nil
    local newp = points()
    if oldp and #oldp == #newp then
      for i, pt in ipairs(oldp) do if bd.psel[pt] then psel[newp[i]] = true end end
    end
  else
    bd.pts, bd.hot, bd.hotp = nil, nil, nil
  end
  bd.sel, bd.psel, bd.paint, bd.seen = sel, psel, nil, new
end

function build.key(k)
  if picker.open then picker_key(k); return end
  if bd.move then move_key(k); return end
  if bd.paint then paint_key(k); return end
  local tool = bd.tool
  if k:match("^[1-5]$") then
    bd.tool = tonumber(k)
    if bd.tool == 2 and bd.side == 7 then bd.side = 1 end
    if bd.tool >= 3 then bd.pts = nil end
    say(TOOLS[bd.tool], C.ACC, 40)
    return
  end
  if k == "\t" then picker.open = true; sheet_w, sheet_h = sheet_size(); picker_clamp(); return end
  if k == "q" then build_turn(-1); return end
  if k == "e" then build_turn(1); return end
  if T.alt() and (k == "left" or k == "right") then build_turn(k == "left" and -1 or 1); return end
  if k == "w" then bd.cam.pitch = clamp(bd.cam.pitch - 0.1, -1.45, 0.3); return end
  if k == "s" then bd.cam.pitch = clamp(bd.cam.pitch + 0.1, -1.45, 0.3); return end
  if T.cam_key(bd.cam, k, 2, 80) then return end
  if k == "z" or k == "0" or k == "home" then frame_model(); return end
  if k == "v" then bd.view = bd.view % #VIEWS + 1; say("view: " .. VIEWS[bd.view], C.ACC, 60); return end
  if k == "b" then bd.back = not bd.back; say(bd.back and "the faces seen from behind show, darker" or "faces from one side, as on the console", C.ACC, 90); return end
  if k == "r" and tool <= 2 then brush.rot = (brush.rot + 1) % 4; return end
  if k == "h" then brush.flip = not brush.flip; return end
  if k == "x" and tool <= 2 then
    local list = faces_on(faces(), bd.cell, bd.side == 7 and 1 or bd.side)
    if #list == 0 then list = faces_on(faces(), bd.cell, 7) end
    if #list == 0 then say("no face here to pick from", C.DIM, 60); return end
    brush = brush_of(faces()[list[1]])
    say("picked " .. (brush.c and string.format("colour %06X", brush.c) or "a tile"), C.ACC, 90)
    return
  end
  if tool <= 2 then
    if k == "up" then build_move(0, 1, 0)
    elseif k == "down" then build_move(0, -1, 0)
    elseif k == "left" then build_move(-1, 0, 0)
    elseif k == "right" then build_move(1, 0, 0)
    elseif k == "pgup" then build_move(0, 0, 1)
    elseif k == "pgdn" then build_move(0, 0, -1)
    elseif k == " " or k == "ok" then build_action(false)
    elseif k == "\b" or k == "del" then build_action(true)
    elseif k == "f" then bd.side = bd.side % 6 + 1
    elseif k == "g" then bd.grid = not bd.grid
    end
    return
  end
  -- select, vertex, paint: the pointer on what is there
  if k == "up" or k == "down" or k == "left" or k == "right" then
    if tool == 4 then bd.hotp = T.nav(#points(), bd.hotp, k, point_pos)
    else hot_face(); bd.hot = T.nav(#faces(), bd.hot, k, face_pos) end
    return
  end
  if tool == 3 then select_key(k)
  elseif tool == 4 then vertex_key(k)
  elseif k == "\n" or k == " " or k == "ok" then paint_open()
  end
end

function build.pad()
  if picker.open then picker_pad(); return end
  local rp, tap = T.rp, T.tap
  if bd.move then
    if btn(6) then
      if rp[2] then move_key("pgup") end
      if rp[3] then move_key("pgdn") end
    else
      for i, k in pairs({ [0] = "left", "right", "up", "down" }) do if rp[i] then move_key(k) end end
    end
    if btnp(4) then move_key("ok") end
    if btnp(5) then move_key("back") end
    return
  end
  if bd.paint then
    for i, k in pairs({ [0] = "left", "right", "up", "down" }) do if rp[i] then paint_key(k) end end
    if btnp(4) then paint_key("ok") end
    if btnp(5) then paint_key("back") end
    if tap[6] then paint_key("i") end
    return
  end
  if btn(6) then                               -- X + the pad: a level up/down, turn the view
    if bd.tool <= 2 then
      if rp[2] then build_move(0, 0, 1) end
      if rp[3] then build_move(0, 0, -1) end
    end
    if btnp(0) then build_turn(-1) end
    if btnp(1) then build_turn(1) end
    if btnp(4) then build.key(tostring(bd.tool % #TOOLS + 1)) end
    if btnp(5) then brush.rot = (brush.rot + 1) % 4 end
    return
  end
  for i, k in pairs({ [0] = "left", "right", "up", "down" }) do if rp[i] then build.key(k) end end
  if bd.tool <= 2 then
    if btnp(4) then build_action(false) end
    if btnp(5) then build_action(true) end
    if tap[6] then bd.side = bd.side % 6 + 1 end
  elseif bd.tool == 3 or bd.tool == 4 then
    if btnp(4) then build.key(" ") end
    if btnp(5) then build.key("g") end
    if tap[6] then build.key("a") end
  else
    if btnp(4) then paint_open() end
  end
  if tap[7] then build.key("\t") end
end

function build.update()
  bd.seen = faces()                          -- the faces before an undo (refresh)
  local cam, c = bd.cam, bd.cell
  local tx, ty, tz = c[1] + 0.5, c[2] + 0.5, c[3] + 0.5
  if bd.tool >= 3 then                       -- the camera follows the pointer
    local p
    if bd.tool == 4 then local pt = hot_point(); p = pt and pt.p
    else local f = hot_face(); p = f and T.face_center(f) end
    if p then tx, ty, tz = p[1], p[2], p[3] end
  end
  cam.tx = cam.tx + (tx - cam.tx) * 0.2
  cam.ty = cam.ty + (ty - cam.ty) * 0.2
  cam.tz = cam.tz + (tz - cam.tz) * 0.2
  cam.yaw = cam.yaw + (bd.yaw_to - cam.yaw) * 0.25
end

function build.status()
  local m = M()
  return m and ("model " .. S.cur .. "/" .. #S.models .. ": " .. m.name) or ""
end

function build.draw()
  if picker.open then draw_picker(); return end
  local cam, c, m = bd.cam, bd.cell, M()
  local tool = bd.tool
  cls(C.SKY)
  zclear()
  -- the camera's own axes, before drawing (the tiles face it)
  T.look(cam, VIEW_DX, 16)
  if bd.grid then
    T.draw_grid(c[1], c[2], c[3], 6, 1, C.GRID)
    T.seg({ -0.2, 0, 0 }, { 1, 0, 0 }, 0xC04040)
    T.seg({ 0, 0, -0.2 }, { 0, 0, 1 }, 0x4060D0)
  end
  if m then draw_model(m) end
  local blink = (S.frame // 15) % 2 == 0 and C.CUR or 0xFFFFFF
  local info, info2_c = nil, C.DIM
  local info2 = (brush.c and string.format("colour #%06X", brush.c) or
                 string.format("tile %d,%d  %dx%d", brush.rect[1], brush.rect[2], brush.rect[3], brush.rect[4])) ..
                "   view: " .. VIEWS[bd.view] .. (bd.back and ", faces from behind" or "")
  if tool <= 2 then
    -- the cursor: the cell, and the side the tile tool works on
    local x0, y0, z0 = c[1], c[2], c[3]
    local x1, y1, z1 = x0 + 1, y0 + 1, z0 + 1
    local P = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
                { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } }
    local E = { { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 1 }, { 5, 6 }, { 6, 7 }, { 7, 8 }, { 8, 5 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, { 4, 8 } }
    for _, e in ipairs(E) do T.seg(P[e[1]], P[e[2]], tool == 1 and blink or C.GRID0) end
    if tool == 2 then
      local a, l = side_plane(c, bd.side)
      local q = {}
      local o1, o2 = a % 3 + 1, (a + 1) % 3 + 1
      for i, d in ipairs({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }) do
        local p = { 0, 0, 0 }
        p[a] = l
        p[o1] = c[o1] + d[1]
        p[o2] = c[o2] + d[2]
        q[i] = p
      end
      for i = 1, 4 do T.seg(q[i], q[i % 4 + 1], blink) end
      T.seg(q[1], q[3], blink)
      T.seg(q[2], q[4], blink)
    end
    info = string.format("%s  cell %d,%d,%d%s", TOOLS[tool]:upper(), c[1], c[2], c[3],
                         tool == 2 and ("  on the " .. SIDES[bd.side]) or "")
  elseif tool == 4 then
    -- the corners as bm Mesh's vertices: chosen yellow, the pointer's circled
    local pts = points()
    for _, pt in ipairs(pts) do
      local x, y = T.scr(pt.p)
      if x and not bd.psel[pt] then rectfill(x - 1, y - 1, 2, 2, C.WIRE) end
    end
    for pt in pairs(bd.psel) do
      local x, y = T.scr(pt.p)
      if x then rectfill(x - 2, y - 2, 5, 5, C.PT) end
    end
    local hp = hot_point()
    if hp then
      local x, y = T.scr(hp.p)
      if x then circ(x, y, 5, C.HOT) end
      info = string.format("VERTEX  %.3g, %.3g, %.3g  (%d faces)", hp.p[1], hp.p[2], hp.p[3], #hp.refs)
    else
      info = "VERTEX  no corners yet"
    end
    info2, info2_c = count(bd.psel) .. " corners chosen", count(bd.psel) > 0 and C.PT or C.DIM
  else
    if tool == 3 then
      for f in pairs(bd.sel) do T.outline(f, C.PT, true) end
      info2, info2_c = count(bd.sel) .. " faces chosen", count(bd.sel) > 0 and C.PT or C.DIM
    else
      info2, info2_c = string.format("paint colour #%06X", paint_c), C.DIM
    end
    local f = hot_face()
    if f then
      T.outline(f, C.HOT, true)
      local p = T.face_center(f)
      local n = T.face_normal(f)
      T.seg(p, V.add(p, V.scale(n, 0.3)), C.HOT)
      if bd.paint and bd.paint.f == f then
        local w = pixel_world(f, bd.paint.x + bd.paint.cx + 0.5, bd.paint.y + bd.paint.cy + 0.5)
        if w then
          local x, y = T.scr(w)
          if x then circ(x, y, 3, 0xFFFFFF) end
        end
      end
      info = string.format("%s  face %d/%d  %s", TOOLS[tool]:upper(), bd.hot or 0, #m.faces,
                           f.c and string.format("colour #%06X", f.c) or "tile")
    else
      info = TOOLS[tool]:upper() .. "  no faces yet: build with 1 and 2"
    end
  end
  if bd.move then
    info = string.format("MOVE %d %s  step %g  %.3g, %.3g, %.3g", #bd.move.list, bd.move.what == "faces" and "faces" or "corners",
                         bd.move.step, bd.move.total[1], bd.move.total[2], bd.move.total[3])
  end
  T.gizmo(cam, PANEL_W + 40, HINT_Y - 40)
  -- what it is: two lines over the view (as bm Mesh)
  rectfill(PANEL_W, 16, W - PANEL_W, 32, C.BG)
  local name = info:match("^(%S+)")
  local nx = print(name, PANEL_W + 8, 16, C.ACC)
  print(info:sub(#name + 1, (W - PANEL_W - 16) // 8), nx, 16, C.TEXT)
  print(info2:sub(1, (W - PANEL_W - 16) // 8), PANEL_W + 8, 32, info2_c)
  draw_panel()
  local hl
  if bd.move then
    hl = { { { "up", "down", "left", "right" }, "move" }, { { "pgup", "pgdn" }, "up/down" }, { { "tab" }, "step" },
           { { "enter" }, "done" }, { { "esc" }, "back" } }
  elseif bd.paint then
    hl = { { { "up", "down", "left", "right" }, "pixel" }, { { "space" }, "paint" }, { { "e" }, "clear" },
           { { "i" }, "pick" }, { { "c" }, "colour" }, { { "enter" }, "done" } }
  elseif tool <= 2 then
    hl = { { { "up", "down", "left", "right" }, "move" }, { { "space" }, "put" }, { { "backspace" }, "remove" },
           { { "tab" }, "tiles" }, { { "pgup", "pgdn" }, "level" }, { { "f" }, "side" }, { { "q", "e" }, "turn" } }
  elseif tool == 3 then
    hl = { { { "up", "down", "left", "right" }, "face" }, { { "space" }, "choose" }, { { "g" }, "move" },
           { { "r" }, "turn" }, { { "m" }, "mirror" }, { { "d" }, "copy" }, { { "del" }, "delete" } }
  elseif tool == 4 then
    hl = { { { "up", "down", "left", "right" }, "corner" }, { { "space" }, "choose" }, { { "g" }, "move" },
           { { "m" }, "join" }, { { "a" }, "all" } }
  else
    hl = { { { "up", "down", "left", "right" }, "face" }, { { "enter" }, "paint its tile" }, { { "tab" }, "tiles" } }
  end
  T.hint(hl)
end

----------------------------------------------------------------- models page
local mp = { spin = 0.6, cam = { yaw = 0.6, pitch = -0.35, dist = 4, tx = 0, ty = 0, tz = 0 } }

models_page = {
  id = "models", fkey = "f2", label = "models",
  keys = { "up/down        choose a model", "Enter          build it (F1)", "n              new model",
           "r              rename", "d              duplicate", "Del            delete (twice)",
           "PgUp PgDn      move it up / down the list", "-              fewer triangles (reduce)",
           "m              a model from a picture (its outline, or a service)",
           "i              the texture margin (inset)" },
}

function models_page.enter()
  T.aim(mp.cam, faces(), 2.6)
end

function models_page.refresh() T.aim(mp.cam, faces(), 2.6) end

local function choose_model(i)
  T.select_model(i)
  T.refresh()
  T.aim(mp.cam, faces(), 2.6)
end

function models_page.key(k)
  local m = M()
  if k == "up" then choose_model(S.cur - 1)
  elseif k == "down" then choose_model(S.cur + 1)
  elseif k == "\n" or k == "ok" then T.go("build")
  elseif k == "n" then
    local i = T.new_model("model")
    T.sync(); choose_model(i); S.dirty = true
    T.go("build")
    say("new model " .. M().name .. ": build it here", C.ACC)
  elseif k == "r" and m then
    T.ask("model name (up to 16 letters)", m.name, function(t)
      t = t:gsub("[%c]", ""):sub(1, 16)
      if t == "" then return end
      m.name = T.unique_name(t, S.cur)
      m.dirty = true
      T.sync(); S.dirty = true
      say("renamed: " .. m.name, C.ACC)
    end)
  elseif k == "d" and m then
    local c = { name = T.unique_name(m.name), faces = {}, dirty = true }
    if m.mc then c.faces, c.rig = T.decode_model(m.mc, m.ac) else c.rig = T.deep(m.rig) end
    table.insert(S.models, S.cur + 1, c)
    S.undo, S.redo = {}, {}
    T.sync(); choose_model(S.cur + 1); S.dirty = true
    say("copied as " .. c.name, C.ACC)
  elseif (k == "del" or k == "\b") and m then
    if T.confirm("delmodel" .. S.cur, "Del again deletes the model " .. m.name) then return end
    table.remove(S.models, S.cur)
    if #S.models == 0 then T.new_model("model") end
    S.undo, S.redo = {}, {}
    T.sync(); choose_model(min(S.cur, #S.models)); S.dirty = true
    say("deleted " .. m.name, C.ACC)
  elseif (k == "pgup" or k == "pgdn") and m then
    local j = S.cur + (k == "pgup" and -1 or 1)
    if j < 1 or j > #S.models then return end
    S.models[S.cur], S.models[j] = S.models[j], S.models[S.cur]
    S.undo, S.redo = {}, {}
    S.cur = j
    T.sync(); S.dirty = true
  elseif k == "m" then
    T.picture_chooser()
  elseif k == "-" and m and m.nt and m.nt > 1 then
    -- the reducer (src/bm/decimate.c): the triangles wanted, half by default
    T.ask("triangles (now " .. m.nt .. "; at most " .. T.TRIS_60FPS .. " a scene at 60 fps)", tostring(m.nt // 2),
          function(t)
      local n = math.tointeger(tonumber(t))
      if not n or n < 1 then say("a number of triangles", C.ERR); return end
      if n >= m.nt then say("it has " .. m.nt .. " triangles already", C.ERR); return end
      T.begin_edit()
      local nt, e = T.reduce_model(m, n)
      if not nt then table.remove(S.undo); say("cannot reduce: " .. tostring(e), C.ERR); return end
      if T.commit() then
        choose_model(S.cur)
        say("reduced to " .. nt .. " triangles" .. (nt > n and " (no further without turning faces over)" or "") ..
            "  (Ctrl+Z undoes)", C.ACC, 240)
      end
    end)
  elseif k == "i" then
    S.inset = ({ [0] = 0.125, [0.125] = 0.25, [0.25] = 0.5, [0.5] = 0 })[S.inset] or 0.25
    for _, mm in ipairs(S.models) do mm.dirty = true end
    T.sync(); S.dirty = true
    say("texture margin " .. S.inset .. " px: the tiles beside do not show at the edges", C.ACC, 120)
  elseif not T.cam_key(mp.cam, k) then
    return
  end
end

function models_page.pad()
  local rp = T.rp
  if btn(6) then T.cam_pad(mp.cam); return end
  if rp[2] then models_page.key("up") end
  if rp[3] then models_page.key("down") end
  if btnp(4) then models_page.key("\n") end
  if T.tap[6] then models_page.key("n") end
end

function models_page.update() mp.cam.yaw = mp.cam.yaw + 0.01 end

function models_page.draw()
  local m = M()
  cls(C.SKY)
  zclear()
  T.look(mp.cam, PANEL_W / 2, 28)
  if m and #m.faces > 0 then
    T.draw_grid(round(mp.cam.tx), mp.cam.floor, round(mp.cam.tz), 6, 0.5, C.GRID)
    light3d(-0.4, 0.8, -0.5, 0.4)
    if S.view then draw3d(S.view, 0, 0, 0, 0, 0, 0, 1, 0) end
  end
  -- the list (as bm Mesh's): a blue dot for a model with a skeleton
  rectfill(0, 16, PANEL_W, HINT_Y - 16, C.PANEL)
  local names = {}
  for i, mm in ipairs(S.models) do names[i] = mm.name end
  T.draw_list("MODELS " .. #S.models, names, S.cur, 0, 32, 16, PANEL_W, function(i)
    return S.models[i].rig and 0x80C8FF or C.GRID0
  end)
  -- what it is: three lines over the view
  local x = PANEL_W + 16
  rectfill(PANEL_W, 16, W - PANEL_W, 48, C.BG)
  if m then
    print(m.name, x, 16, C.ACC)
    local warn = T.counts(m, x, 32)
    print(warn or (m.rig and (#m.rig.bones .. " bones, " .. #m.rig.clips .. " animations (bm Animator)") or
          "no skeleton (bm Animator makes one)"), x, 48, warn and C.ACC or C.DIM)
  end
  print("texture margin " .. S.inset .. " px", W - 192, 16, C.DIM)
  T.hint({ { { "up", "down" }, "model" }, { { "enter" }, "build" }, { { "n" }, "new" }, { { "r" }, "rename" },
           { { "d" }, "copy" }, { { "del" }, "delete" }, { { "-" }, "reduce" }, { { "m" }, "picture" },
           { { "pgup", "pgdn" }, "order" } })
end

end

----------------------------------------------------------------- the program

T.run({
  name = "bm Studio",
  viewer = VIEWER,
  empty_model = true,
  picture = true,
  page_list = { build, models_page },
  keys_all = { "F1 build  F2 models  Esc menu  [ ] model  F6 assistant (a model from words)",
               "Ctrl+S save  F5 try the game  Ctrl+Z/Y undo/redo  + - zoom" },
  keys_pad = { "pad: Y + left/right page  Y + B menu  Y + up/down model  Y + A undo  Y + X assistant",
               "build: A put  B remove  X side  Y tiles  X + pad level and turn",
               "select, vertex: A choose  B move  X all  paint: A the face's tile" },
  init = function()
    -- the tiles of a new project: this cartridge's own sheet (the starter
    -- tiles of bm Studio)
    starter = {}
    local i = 0
    for y = 0, 47 do
      for x = 0, 255 do
        i = i + 1
        starter[i] = sget(x, y) or false
      end
    end
  end,
  new_project = function(quiet)
    cart_new()
    local i = 0
    for y = 0, 47 do
      for x = 0, 255 do
        i = i + 1
        local c = starter[i]
        if c then sset(x, y, c) end
      end
    end
    T.new_project()
    if quiet then return end
    T.go("build")
    say("new project: build with the tools 1-5; Esc > Save as gives it a name", C.ACC)
  end,
  hello = function(files)
    return files and "open a .bm, or Esc for a new project" or "a new project: F1 builds"
  end,
  menu = function(items)
    items[#items + 1] = { "Title: " .. S.proj.title:sub(1, 24), function()
      T.ask("the cartridge's title", S.proj.title, function(t)
        if t ~= "" then S.proj.title = t:sub(1, 47); S.dirty = true end
      end)
    end }
    items[#items + 1] = { "Author: " .. (S.proj.author ~= "" and S.proj.author:sub(1, 23) or "-"), function()
      T.ask("the author", S.proj.author, function(t) S.proj.author = t:sub(1, 31); S.dirty = true end)
    end }
    items[#items + 1] = { "Open in bm Animator", function() T.open_in("animator", "bm Animator") end }
  end,
  menu_info = function(x, y)
    print("skeletons and animations:", x, y, C.DIM)
    print("bm Animator (menu)", x, y + 16, C.DIM)
    print("the same files as bm Studio", x, y + 48, C.DIM)
    print("and bm Animator on the PC", x, y + 64, C.DIM)
  end,
})
