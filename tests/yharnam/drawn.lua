-- Host test of Yharnam's drawn map (carts/yharnam/map_ground.csv, map_overlay.csv, map_objects.csv: the SDK's map
-- page). In the fake bm, with the map's layers as mkbm.py packs them:
--  1. the map mapgen.lua makes from the street plan (MAPDIR) gives the same town as no map at all: every chunk of
--     the hunt, its objects, colliders, lights, fires, smokes, lamps, solid ground and hunter's lamp;
--  2. the "objects" layer decides: a placeholder taken away takes its thing (and its collider, its solid ground),
--     one put on an empty tile makes it there (a tree, a lamp, a house as wide as its body tiles), the same each
--     time the chunk is made;
--  3. the cartridge with the repository's map (DIR): the layers known (MAP.drawn, MAP.objects, MAP.ring), no chunk
--     writes them, the view draws ground and overlay on the world's cells and never the objects, the ring only
--     beyond the edge; a hunt runs.
-- (sim.lua runs the cartridge with no map: the plan's town, as before.)
--
--   luahost tests/yharnam/drawn.lua carts/yharnam/main.lua carts/yharnam MAPDIR
local SRC, DIR, MAPDIR = arg[1], arg[2], arg[3]
assert(SRC and DIR and MAPDIR, "luahost tests/yharnam/drawn.lua carts/yharnam/main.lua carts/yharnam MAPDIR")
local HERE = arg[0]:match("^(.*/)") or ""
local checks = 0
local function check(cond, msg)
  checks = checks + 1
  if not cond then io.stderr:write("FAIL: " .. msg .. "\n") os.exit(1) end
end
local function layers(dir)
  return { { "ground", dir .. "/map_ground.csv" }, { "overlay", dir .. "/map_overlay.csv" },
           { "objects", dir .. "/map_objects.csv" } }
end
-- a cartridge in a fake bm of its own: with this map (nil: none)
local function cart(map)
  local B = dofile(HERE .. "fakebm.lua")
  if map then B.load_map(map) end
  return B, B.load(SRC)
end

-- everything a chunk is, as text
local function snap(ch)
  local s = {}
  local function add(...)
    local t = table.pack(...)
    for i = 1, t.n do t[i] = tostring(t[i]) end
    s[#s + 1] = table.concat(t, ",")
  end
  for _, o in ipairs(ch.objs) do
    add("o", o.name or "house", o.x, o.y, o.z, o.key, o.broken, o.w, o.h, o.top)
    if o.bld then add(table.concat(o.bld, " ")) end
  end
  for _, c in ipairs(ch.cols) do add("c", c.x, c.y, c.rx, c.ry) end
  for _, l in ipairs(ch.lights) do add("l", l.x, l.y, l.r, l.lv, l.fl, l.d) end
  for _, f in ipairs(ch.fires) do add("f", f.x, f.y, f.w, f.rate, f.big) end
  for _, m in ipairs(ch.smokes) do add("s", m.x, m.y) end
  for _, m in ipairs(ch.lamps) do add("L", m.x, m.y, m.key) end
  for i = 1, #ch.kind do add("k", ch.kind[i], ch.solid[i]) end
  if ch.shrine then add("S", ch.shrine.x, ch.shrine.y, ch.shrine.key) end
  return table.concat(s, ";")
end

---------------------------------------------------------------- 1. the generated map: the plan's town
local _, P0 = cart(nil)
local _, P1 = cart(layers(MAPDIR))
local M = P1.MAP
check(M.objects == 3 and #M.drawn == 2 and M.ring == 4, "the generated map: ground, overlay drawn, objects read")
local nobj, nunit = 0, 0
for cy = 0, M.size - 1 do
  for cx = 0, M.size - 1 do
    local a, b = P0.ensure(cx, cy), P1.ensure(cx, cy)
    check(snap(a) == snap(b), "chunk " .. cx .. "," .. cy .. ": the same with the generated map as with none")
    nobj, nunit = nobj + #b.objs, nunit + #b.units
  end
end

---------------------------------------------------------------- 2. the objects layer decides
local B2, E = cart(layers(MAPDIR))
local env, CS = B2.env, E.CS
local K = E.MAP.KIND
local PLACE = {}
for c, k in pairs(E.MAP.kind_at) do PLACE[k] = c end
local function put(k, tx, ty)                -- a placeholder (nil: none) on world tile (tx, ty)
  local c = k and PLACE[k] or 0
  local x, y = tx * 2, ty * 2
  env.mset(x, y, c, 3); env.mset(x + 1, y, k and c + 1 or 0, 3)
  env.mset(x, y + 1, k and c + 512 or 0, 3); env.mset(x + 1, y + 1, k and c + 513 or 0, 3)
end
local function at(tx, ty) return E.MAP.kind_at[env.mget(tx * 2, ty * 2, 3)] end
-- a chunk of houses with a tree in a yard, as the plan makes it
local cx, cy, plan, tree, house
for qy = 0, M.size - 1 do
  for qx = 0, M.size - 1 do
    if not plan then
      local c, t, h = P0.ensure(qx, qy), nil, nil
      for _, u in ipairs(c.units) do
        if not t and u.k == "tree" then t = u end
        if not h and u.w then h = u end
      end
      if t and h then cx, cy, plan, tree, house = qx, qy, c, t, h end
    end
  end
end
check(plan, "a chunk with a tree and a house")
local tx0, ty0 = cx * CS, cy * CS
-- empty tiles: no placeholder, nothing solid, on the street or a yard
local empty = {}
for ly = 1, CS - 2 do
  for lx = 1, CS - 2 do
    if not at(tx0 + lx, ty0 + ly) and not plan.solid[ly * CS + lx + 1] then empty[#empty + 1] = { lx, ly } end
  end
end
check(#empty >= 2, "empty tiles in the town chunk")
local t_new, l_new = empty[1], empty[#empty]
put(nil, tx0 + tree.lx, ty0 + tree.ly)                   -- the tree taken away
put("tree", tx0 + t_new[1], ty0 + t_new[2])              -- one on an empty tile
put("lamp", tx0 + l_new[1], ty0 + l_new[2])              -- a lamp on another
put(nil, tx0 + house.lx, ty0 + house.ly)                 -- the house's corner taken away: no house
-- a house where there was none: a 5 x 7 corner of the map with no placeholder (a park's or a forest's)
local hx, hy
for qy = 0, M.size - 1 do
  for qx = 0, M.size - 1 do
    for ly = 0, CS - 7 do
      for lx = 0, CS - 5 do
        if not hx then
          local free = true
          for y = ly, ly + 6 do for x = lx, lx + 4 do if at(qx * CS + x, qy * CS + y) then free = false end end end
          if free then hx, hy = qx * CS + lx, qy * CS + ly end
        end
      end
    end
  end
end
check(hx, "room for a house on the map")
put("house7", hx, hy)
for x = 1, 4 do put("roof", hx + x, hy) end
for y = 1, 6 do for x = 0, 4 do put("body", hx + x, hy + y) end end
for k in pairs(B2.writes) do B2.writes[k] = nil end

local ch = E.ensure(cx, cy)
local function units_at(c, lx, ly)
  local out = {}
  for _, o in ipairs(c.objs) do if o.u and o.u.lx == lx and o.u.ly == ly then out[#out + 1] = o end end
  return out
end
local gone_tree = units_at(ch, tree.lx, tree.ly)
check(#gone_tree == 0, "a placeholder taken away: its tree is gone")
local new_tree = units_at(ch, t_new[1], t_new[2])
check(#new_tree == 1 and K[new_tree[1].name] == "tree" and new_tree[1].c, "a tree placed: there, with its collider")
local tile_x, tile_y = math.floor(new_tree[1].x) // 16 - tx0, math.floor(new_tree[1].y) // 16 - ty0
check(tile_x == t_new[1] and tile_y == t_new[2], "the tree on its tile")
local lamp_key = (tx0 + l_new[1]) * 1000003 + ty0 + l_new[2]
local found = false
for _, l in ipairs(ch.lamps) do if l.key == lamp_key then found = true end end
check(found and E.lamps[lamp_key] ~= nil, "a lamp placed: one of the chunk's lamps, lit or not")
for _, o in ipairs(ch.objs) do check(not (o.bld and o.x == house.lx * 16 + tx0 * 16 and o.top == ty0 * 16 + house.ly * 16),
                                     "its corner taken away: the house is gone") end
local walk = true
for y = house.ly, house.ly + house.h - 1 do
  for x = house.lx, house.lx + house.w - 1 do if ch.solid[y * CS + x + 1] then walk = false end end
end
check(walk, "where the house stood: walkable")
local nlights = 0
for _, l in ipairs(ch.lights) do if l.u == house then nlights = nlights + 1 end end
check(nlights == 0, "the house's lit windows gone with it")
local again = snap(ch)
E.ensure(cx + 4, cy)                                      -- (the same ring slot: the chunk is made again)
check(snap(E.ensure(cx, cy)) == again, "the chunk made again: the same")
-- the house placed: as wide as its body tiles, 7 tall (H7), solid
local hc = E.ensure(hx // CS, hy // CS)
local hlx, hly = hx % CS, hy % CS
local made
for _, o in ipairs(hc.objs) do if o.bld and o.u and o.u.lx == hlx and o.u.ly == hly then made = o end end
check(made and made.w == 5 and made.h == 7, "a house placed: 5 wide (its body tiles), 7 tall")
local solid = true
for y = hly, hly + 6 do for x = hlx, hlx + 4 do if not hc.solid[y * CS + x + 1] then solid = false end end end
check(solid and hc.kind[hly * CS + hlx + 1] == E.kinds.house, "the house placed: solid, a house")
check(not B2.writes[1] and not B2.writes[2] and not B2.writes[3], "no chunk writes the drawn map")

---------------------------------------------------------------- 3. the repository's map, a hunt on it
local B, Y = cart(layers(DIR))
env = B.env
M = Y.MAP
local TS = Y.TS
local N = M.size * CS * 2                    -- cells across the map
check(N == 256, "the map is 256 x 256 cells (" .. N .. ")")
check(#M.drawn == 2 and M.drawn[1] == 1 and M.drawn[2] == 2 and M.objects == 3 and M.ring == 4,
      "the repository's map: ground and overlay drawn, objects read, the ring a fourth")
local names = env.mlayers()
check(#names == 4 and names[1] == "ground" and names[2] == "overlay" and names[3] == "objects" and names[4] == "_ring",
      "the layers: " .. table.concat(names, " "))
local holes, stray, placed = 0, 0, 0
for y = 0, 255 do
  for x = 0, 255 do
    if env.mget(x, y, 1) == 0 then holes = holes + 1 end
    local v, c = env.mget(x, y, 3), env.mget(x - x % 2, y - y % 2, 3)    -- the cell, its tile's first
    if x % 2 == 0 and y % 2 == 0 and M.kind_at[v] then placed = placed + 1
    elseif v ~= 0 and not (M.kind_at[c] and v == c + x % 2 + y % 2 * 512) then stray = stray + 1 end
  end
end

-- every map() of a frame: the drawn layers on the world's cells, never the objects, the ring only beyond the edge
local calls = {}
B.on_map = function(mx, my, x, y, mw, mh, l) calls[#calls + 1] = { mx, my, x, y, mw, mh, l } end
local ring_calls, drawn_calls = 0, 0
local function frame(buttons)
  calls = {}
  B.frame(buttons)
  local cx, cy = Y.camera()
  local tx0, ty0 = math.floor(cx) // TS, math.floor(cy) // TS
  local tx1, ty1 = (math.floor(cx) + 359) // TS, (math.floor(cy) + 359) // TS
  local n = N // 2 - 1
  local ax, ay, bx, by = math.max(tx0, 0), math.max(ty0, 0), math.min(tx1, n), math.min(ty1, n)
  local seen = {}
  for _, c in ipairs(calls) do
    local mx, my, x, y, mw, mh, l = table.unpack(c)
    check(l ~= M.objects, "the objects layer is never drawn")
    if l == 1 or l == 2 then
      drawn_calls = drawn_calls + 1
      check(x == mx * 8 and y == my * 8 and mx % 2 == 0 and my % 2 == 0, "a drawn layer on the world's cells")
      check(mx == ax * 2 and my == ay * 2 and mw == (bx - ax + 1) * 2 and mh == (by - ay + 1) * 2,
            string.format("layer %d: the view's part of the map (%d,%d %dx%d, not %d,%d %dx%d)", l, ax * 2, ay * 2,
                          (bx - ax + 1) * 2, (by - ay + 1) * 2, mx, my, mw, mh))
      seen[l] = true
    else
      ring_calls = ring_calls + 1
      check(l == M.ring, "only the drawn layers and the ring are drawn")
      local px0, py0, px1, py1 = x, y, x + mw * 8 - 1, y + mh * 8 - 1
      check(px1 < 0 or py1 < 0 or px0 >= N * 8 or py0 >= N * 8, string.format(
            "the ring only beyond the edge (%d,%d to %d,%d)", px0, py0, px1, py1))
    end
  end
  local st = Y.state()                        -- (the screens with no town: the viewer, the hunt lost or over)
  if st ~= "gallery" and st ~= "lost" and st ~= "end" and ax <= bx and ay <= by then
    check(seen[1] and seen[2], "both drawn layers, every frame")
  end
end

B.call(env._init)
for _ = 1, 30 do frame(0) end                 -- the title, at the start (a corner of the map: the edge in view)
check(ring_calls > 0, "the start shows what is beyond the edge: the ring")
check((B.writes[4] or 0) > 0, "the chunks beyond the edge write the ring")
frame(1 << 4); frame(0)                       -- A: the hunt
for i = 1, 240 do frame(i % 80 < 40 and 1 << 3 or 1 << 1) end   -- down and right, in turn
check(not B.writes[1] and not B.writes[2] and not B.writes[3], "no chunk writes the drawn layers")
-- the middle of the map: only the drawn layers, no ground made
Y.teleport(4 * 256 + 128, 4 * 256 + 128)
for _ = 1, 30 do frame(0) end
local before, rc = B.writes[4] or 0, ring_calls
for _ = 1, 60 do frame(1 << 1) end
check(ring_calls == rc, "in the middle of the map: no ring drawn")
check((B.writes[4] or 0) == before, "in the middle of the map: no ground made")
-- the far corner: the ring again, beyond the south-east edge
Y.teleport(8 * 256 - 40, 8 * 256 - 40)
for _ = 1, 30 do frame(0) end
check(ring_calls > rc and (B.writes[4] or 0) > before, "the far corner: the ring beyond the edge")
check(not B.writes[1] and not B.writes[2] and not B.writes[3], "still no chunk writes the drawn layers")

io.write(string.format("yharnam drawn map: %d checks passed (64 chunks as the plan's: %d objects, %d units; the " ..
                       "repository's map: %d placeholders, %d cells of objects off the 16x16 grid or unknown (not " ..
                       "read), %d cells of ground empty; %d map() of the drawn layers, %d of the ring)\n", checks,
                       nobj, nunit, placed, stray, holes, drawn_calls, ring_calls))
