-- Host test of Yharnam's drawn map (carts/yharnam/map_ground.csv and map_overlay.csv, the SDK's map page): the
-- cartridge built with the map of the repository draws it instead of making the ground. In the fake bm, with
-- the map's layers as mkbm.py packs them: the cartridge knows it (MAP.drawn), no chunk ever writes the drawn
-- layers, the view draws them on the world's cells where it is on the map and the ground of the chunks (the
-- ring, a layer of its own) only beyond the edge, behind the mist; the chunks still make their houses, trees
-- and lamps; a hunt runs. (sim.lua runs the cartridge with no map: the ground made, as before.)
--
--   luahost tests/yharnam/drawn.lua carts/yharnam/main.lua carts/yharnam
local SRC, DIR = arg[1], arg[2]
assert(SRC and DIR, "luahost tests/yharnam/drawn.lua carts/yharnam/main.lua carts/yharnam")
local B = dofile((arg[0]:match("^(.*/)") or "") .. "fakebm.lua")
local env = B.env
local checks = 0
local function check(cond, msg)
  checks = checks + 1
  if not cond then io.stderr:write("FAIL: " .. msg .. "\n") os.exit(1) end
end

-- the map of the repository (B.load_map: 256 x 256 cells a layer, every cell 0..65535)
B.load_map({ { "ground", DIR .. "/map_ground.csv" }, { "overlay", DIR .. "/map_overlay.csv" } })
local holes = 0
for y = 0, 255 do for x = 0, 255 do if env.mget(x, y, 1) == 0 then holes = holes + 1 end end end
local Y = B.load(SRC)
local M, CS, TS = Y.MAP, Y.CS, Y.TS
local N = M.size * CS * 2                    -- cells across the map
check(N == 256, "the map is 256 x 256 cells (" .. N .. ")")
check(M.drawn == 2 and M.ring == 3, "the drawn map: 2 layers drawn, the ring a third (" .. tostring(M.drawn) .. ", " ..
      tostring(M.ring) .. ")")
local names = env.mlayers()
check(#names == 3 and names[1] == "ground" and names[2] == "overlay" and names[3] == "_ring", "the layers: " ..
      table.concat(names, " "))

-- every map() of a frame: the drawn layers on the world's cells, the ring only beyond the edge
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
    if l <= M.drawn then
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
check((B.writes[3] or 0) > 0, "the chunks beyond the edge write the ring")
frame(1 << 4); frame(0)                       -- A: the hunt
for i = 1, 240 do frame(i % 80 < 40 and 1 << 3 or 1 << 1) end   -- down and right, in turn
check(not B.writes[1] and not B.writes[2], "no chunk writes the drawn layers")
local ch = Y.ensure(1, 0)                     -- (a district of houses: "t" in the layout of Central Yharnam)
local houses, things = 0, 0
for _, o in ipairs(ch.objs) do if o.bld then houses = houses + 1 else things = things + 1 end end
check(houses > 0 and things > 0, "the chunks still make their houses and things (" .. houses .. ", " .. things .. ")")

-- the middle of the map: only the drawn layers, no ground made
Y.teleport(4 * 256 + 128, 4 * 256 + 128)
for _ = 1, 30 do frame(0) end
local before, rc = B.writes[3] or 0, ring_calls
for _ = 1, 60 do frame(1 << 1) end
check(ring_calls == rc, "in the middle of the map: no ring drawn")
check((B.writes[3] or 0) == before, "in the middle of the map: no ground made")
-- the far corner: the ring again, beyond the south-east edge
Y.teleport(8 * 256 - 40, 8 * 256 - 40)
for _ = 1, 30 do frame(0) end
check(ring_calls > rc and (B.writes[3] or 0) > before, "the far corner: the ring beyond the edge")
check(not B.writes[1] and not B.writes[2], "still no chunk writes the drawn layers")

io.write(string.format("yharnam drawn map: %d checks passed (%d map() of the drawn layers, %d of the ring; %d " ..
                       "cells of the ground left empty)\n", checks, drawn_calls, ring_calls, holes))
